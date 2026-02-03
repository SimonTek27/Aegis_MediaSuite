// audioeditor.cpp
#include "audioeditor.h"
#include "audio_output.h"  // For playback
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QtMath>
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

} // namespace Aegis
