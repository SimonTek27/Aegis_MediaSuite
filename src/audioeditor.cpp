// audioeditor.cpp
#include "audioeditor.h"
#include "audio_output.h"  // For playback
#include "mpv_backend.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QtMath>
#include <QMediaDevices>
#include <QAudioDevice>
#include <algorithm>
#include <cstring>
#include <sndfile.h>

namespace Aegis {

    // ... (AudioBuffer, WaveformCache implementations unchanged) ...

    // ============================================================================
    // AudioEditor Implementation
    // ============================================================================

    AudioEditor::AudioEditor(AudioEngine* engine, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
    , m_effects(std::make_unique<EffectChain>())
    , m_previewBackend(std::make_unique<MpvBackend>(this))
    , m_waveformCache(new WaveformCache(this))
    {
        m_format.sampleRate = 44100;
        m_format.channels = 2;
        m_format.bitsPerSample = 32;
        m_format.isFloat = true;
    }

    AudioEditor::~AudioEditor() = default;

    bool AudioEditor::open(const QString& filePath) {
        // Use libsndfile for loading (could use MpvBackend for format support)
        SF_INFO info{};
        SNDFILE* file = sf_open(filePath.toUtf8().constData(), SFM_READ, &info);

        if (!file) {
            emit error(tr("Failed to open file"));
            return false;
        }

        m_format.sampleRate = info.samplerate;
        m_format.channels = info.channels;

        // Load into EnhancedAudioBuffer (Pillar 2)
        m_buffer.resize(info.frames, info.channels);
        sf_readf_float(file, m_buffer.data(), info.frames);
        sf_close(file);

        m_filePath = filePath;
        m_modified = false;
        m_selection = Selection{0, 0};

        // Build waveform cache
        m_waveformCache->build(m_buffer);

        emit filePathChanged();
        emit bufferChanged();
        emit modifiedChanged();
        return true;
    }

    bool AudioEditor::applyEffect(std::shared_ptr<AudioEffect> effect) {
        if (!effect || m_selection.isEmpty()) return false;

        // Apply through EffectChain (Pillar 2)
        m_effects->clear();
        m_effects->addEffect(effect);

        // Process selection only
        EnhancedAudioBuffer selection = m_buffer.slice(m_selection.start, m_selection.length());
        m_effects->processOffline(selection, Selection{0, selection.frames()}, m_format.sampleRate);

        // Copy back
        m_buffer.copyFrom(selection, 0, static_cast<int>(m_selection.start), selection.frames());

        markModified();
        m_waveformCache->build(m_buffer);
        emit bufferChanged();
        emit effectFinished(true);
        return true;
    }

    QVector<float> AudioEditor::getSpectrum(const Selection& sel, int bins) {
        // Use AudioEngine's FFT capabilities (Pillar 1)
        if (!m_engine || sel.isEmpty()) return QVector<float>();

        // Extract selection to buffer
        EnhancedAudioBuffer selection = m_buffer.slice(sel.start, sel.length());

        // Get mono mix for analysis
        QVector<float> mono(selection.frames());
        for (qint64 i = 0; i < selection.frames(); ++i) {
            float sum = 0;
            for (int ch = 0; ch < selection.channels(); ++ch) {
                sum += selection.sampleAt(ch, i);
            }
            mono[i] = sum / selection.channels();
        }

        // Delegate to AudioEngine's FFT
        return m_engine->calculateSpectrum(mono.data(), mono.size(), bins);
    }

    void AudioEditor::play() {
        if (!m_previewBackend) return;

        // Export current buffer to temp file for preview
        QString tempPath = QDir::temp().filePath("aegis_preview.wav");
        SF_INFO info{};
        info.samplerate = m_format.sampleRate;
        info.channels = m_format.channels;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

        SNDFILE* file = sf_open(tempPath.toUtf8().constData(), SFM_WRITE, &info);
        if (file) {
            sf_writef_float(file, m_buffer.data(), m_buffer.frames());
            sf_close(file);

            m_previewBackend->load(tempPath);
            m_previewBackend->play();
            m_playing = true;
            emit playbackStateChanged();
        }
    }

    void AudioEditor::stop() {
        if (m_previewBackend) {
            m_previewBackend->stop();
            m_playing = false;
            emit playbackStateChanged();
        }
    }

    // ... (remaining methods: save, export, edit operations, etc.) ...

    // ─── WaveformCache ────────────────────────────────────────────────────────

    WaveformCache::WaveformCache(QObject* parent)
        : QObject(parent)
    {}

    void WaveformCache::build(const EnhancedAudioBuffer& buffer, int pixelsPerSecond) {
        QWriteLocker lock(&m_lock);
        m_levels.clear();
        m_cacheResolution = pixelsPerSecond;

        if (buffer.frames() == 0 || buffer.channels() == 0) {
            emit cacheUpdated();
            return;
        }

        m_totalFrames = buffer.frames();
        // Without a sample rate in the buffer, treat pixelsPerSecond as pixels per 1000 frames
        const int framesPerPixel = qMax(1, static_cast<int>(m_totalFrames) / qMax(1, pixelsPerSecond));
        const int totalPixels = static_cast<int>((m_totalFrames + framesPerPixel - 1) / framesPerPixel);

        m_levels.reserve(totalPixels);
        const float* data = buffer.data();
        const int ch = buffer.channels();

        for (int px = 0; px < totalPixels; ++px) {
            const int start = px * framesPerPixel;
            const int end   = qMin(start + framesPerPixel, static_cast<int>(m_totalFrames));
            float minV = 0.0f, maxV = 0.0f, rms = 0.0f;

            for (int f = start; f < end; ++f) {
                float sample = 0.0f;
                for (int c = 0; c < ch; ++c)
                    sample += data[f * ch + c];
                sample /= ch;
                minV = qMin(minV, sample);
                maxV = qMax(maxV, sample);
                rms += sample * sample;
            }
            rms = (end > start) ? std::sqrt(rms / (end - start)) : 0.0f;
            m_levels.append({minV, maxV, rms});
        }
        emit cacheUpdated();
    }

    void WaveformCache::clear() {
        QWriteLocker lock(&m_lock);
        m_levels.clear();
        m_totalFrames = 0;
        emit cacheUpdated();
    }

    QVector<WaveformLevel> WaveformCache::getLevels(qint64 startFrame, qint64 endFrame, int width) const {
        QReadLocker lock(&m_lock);
        if (m_levels.isEmpty() || width <= 0) return {};

        QVector<WaveformLevel> result;
        result.reserve(width);

        const double framesPerPixel = static_cast<double>(endFrame - startFrame) / width;
        const double levelsPerFrame = static_cast<double>(m_levels.size()) / m_totalFrames;

        for (int px = 0; px < width; ++px) {
            const double f0 = startFrame + px * framesPerPixel;
            const double f1 = f0 + framesPerPixel;
            const int l0 = qBound(0, static_cast<int>(f0 * levelsPerFrame), m_levels.size() - 1);
            const int l1 = qBound(l0, static_cast<int>(f1 * levelsPerFrame), m_levels.size() - 1);

            WaveformLevel lv = m_levels[l0];
            for (int l = l0 + 1; l <= l1; ++l) {
                lv.min = qMin(lv.min, m_levels[l].min);
                lv.max = qMax(lv.max, m_levels[l].max);
                lv.rms = qMax(lv.rms, m_levels[l].rms);
            }
            result.append(lv);
        }
        return result;
    }

    // ─── AudioEditor::markModified ────────────────────────────────────────────

    void AudioEditor::markModified() {
        m_modified = true;
        emit modifiedChanged();
    }

    // ─── AudioEditor stub implementations ────────────────────────────────────

    bool AudioEditor::save()                              { return saveAs(m_filePath); }
    bool AudioEditor::saveAs(const QString& path)         { Q_UNUSED(path) return false; }
    bool AudioEditor::exportFile(const QString& path, const QString& fmt) { Q_UNUSED(path) Q_UNUSED(fmt) return false; }
    void AudioEditor::close()                             { m_buffer = EnhancedAudioBuffer(); emit bufferChanged(); }

    void AudioEditor::cut()    { copy(); deleteSelection(); }
    void AudioEditor::copy()   {
        /* clipboard not yet implemented */ Q_UNUSED(m_selection)
        m_hasClipboard = true;
        emit clipboardChanged();
    }
    void AudioEditor::paste(qint64 position) {
        Q_UNUSED(position) markModified();
    }
    void AudioEditor::deleteSelection() {
        if (m_selection.isEmpty()) return;
        markModified();
        m_selection.clear();
        emit selectionChanged();
    }
    void AudioEditor::insertSilence(qint64 position, qint64 frames) { Q_UNUSED(position) Q_UNUSED(frames) markModified(); }
    void AudioEditor::trim()        { markModified(); }
    void AudioEditor::trimOutside() { markModified(); }

    void AudioEditor::selectAll()   { m_selection = { 0, m_buffer.frames() }; emit selectionChanged(); }
    void AudioEditor::selectNone()  { m_selection.clear(); emit selectionChanged(); }
    void AudioEditor::selectRange(qint64 start, qint64 end) { m_selection = { start, end }; emit selectionChanged(); }
    void AudioEditor::extendSelection(qint64 end) { m_selection.end = end; emit selectionChanged(); }
    void AudioEditor::gotoStart()          { m_position = 0.0; emit positionChanged(); }
    void AudioEditor::gotoEnd()            { m_position = duration(); emit positionChanged(); }
    void AudioEditor::gotoSelectionStart() { emit positionChanged(); }
    void AudioEditor::gotoSelectionEnd()   { emit positionChanged(); }

    // ── QML alias implementations ──────────────────────────────────────────
    void AudioEditor::togglePlayback() {
        if (isPlaying()) pause(); else play();
    }

    void AudioEditor::toggleRecording() {
        m_recording = !m_recording;
        emit recordingStateChanged();
    }

    double AudioEditor::selectionStartSecs() const {
        if (m_format.sampleRate <= 0) return 0.0;
        return static_cast<double>(m_selection.start) / m_format.sampleRate;
    }

    double AudioEditor::selectionEndSecs() const {
        if (m_format.sampleRate <= 0) return 0.0;
        return static_cast<double>(m_selection.end) / m_format.sampleRate;
    }

    QStringList AudioEditor::availableDevices() {
        // Return PipeWire/PulseAudio sinks via QAudioDevice enumeration
        QStringList devices;
        for (const QAudioDevice& dev : QMediaDevices::audioOutputs())
            devices << dev.description();
        return devices;
    }

    bool AudioEditor::amplify(float gain) {
        if (m_buffer.isEmpty()) return false;
        for (int i = 0; i < m_buffer.totalSamples(); ++i)
            m_buffer.data()[i] *= gain;
        markModified(); emit bufferChanged();
        return true;
    }
    bool AudioEditor::normalize(float target) {
        if (m_buffer.isEmpty()) return false;
        float peak = 0.0f;
        for (int i = 0; i < m_buffer.totalSamples(); ++i)
            peak = qMax(peak, std::abs(m_buffer.data()[i]));
        if (peak > 0.0f) amplify(target / peak);
        return true;
    }
    bool AudioEditor::fadeIn()  { markModified(); emit bufferChanged(); return true; }
    bool AudioEditor::fadeOut() { markModified(); emit bufferChanged(); return true; }
    bool AudioEditor::reverse() { markModified(); emit bufferChanged(); return true; }
    bool AudioEditor::invert()  {
        for (int i = 0; i < m_buffer.totalSamples(); ++i)
            m_buffer.data()[i] = -m_buffer.data()[i];
        markModified(); emit bufferChanged();
        return true;
    }
    bool AudioEditor::silence() { markModified(); emit bufferChanged(); return true; }
    bool AudioEditor::filter(FilterEffect::FilterType, float, float) { markModified(); emit bufferChanged(); return true; }

    float AudioEditor::getPeakLevel(const Selection& sel) const {
        float peak = 0.0f;
        if (m_buffer.isEmpty()) return peak;
        const qint64 start = qBound(0LL, sel.start, m_buffer.frames());
        const qint64 end   = qBound(start, sel.end, m_buffer.frames());
        const int ch = m_buffer.channels();
        for (qint64 f = start; f < end; ++f)
            for (int c = 0; c < ch; ++c)
                peak = qMax(peak, std::abs(m_buffer.sampleAt(c, f)));
        return peak;
    }
    float AudioEditor::getRMSLevel(const Selection& sel) const {
        if (m_buffer.isEmpty()) return 0.0f;
        const qint64 start = qBound(0LL, sel.start, m_buffer.frames());
        const qint64 end   = qBound(start, sel.end, m_buffer.frames());
        if (start == end) return 0.0f;
        double sum = 0.0;
        const int ch = m_buffer.channels();
        for (qint64 f = start; f < end; ++f)
            for (int c = 0; c < ch; ++c) {
                float s = m_buffer.sampleAt(c, f);
                sum += s * s;
            }
        return static_cast<float>(std::sqrt(sum / ((end - start) * ch)));
    }

    QVector<float> AudioEditor::getWaveformData(int channel, qint64 start, qint64 end, int pixels) {
        QVector<float> result;
        if (m_buffer.isEmpty() || pixels <= 0) return result;
        const qint64 s = qBound(0LL, start, m_buffer.frames());
        const qint64 e = qBound(s, end, m_buffer.frames());
        const qint64 framesPerPx = qMax(1LL, (e - s) / pixels);
        result.reserve(pixels);
        for (int px = 0; px < pixels; ++px) {
            const qint64 pStart = s + px * framesPerPx;
            const qint64 pEnd   = qMin(pStart + framesPerPx, e);
            float peak = 0.0f;
            for (qint64 f = pStart; f < pEnd; ++f)
                peak = qMax(peak, std::abs(m_buffer.sampleAt(channel, f)));
            result.append(peak);
        }
        return result;
    }

    void AudioEditor::playSelection() { play(); }
    void AudioEditor::pause() {
        m_playing = false;
        emit playbackStateChanged();
    }

    double AudioEditor::duration() const {
        return m_buffer.frames() > 0 ? static_cast<double>(m_buffer.frames()) / 44100.0 : 0.0;
    }
    bool AudioEditor::isPlaying() const { return m_playing; }
    void AudioEditor::setPosition(double pos) { m_position = pos; emit positionChanged(); }
    void AudioEditor::setSelection(const Selection& sel) { m_selection = sel; emit selectionChanged(); }

} // namespace Aegis
