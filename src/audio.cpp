// audio.cpp
#include "audio.h"
#include <cmath>
#include <algorithm>
#include <fftw3.h>
#include <QMutexLocker>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <libopenmpt/libopenmpt.hpp>

namespace Aegis {

    EqualizerProcessor::EqualizerProcessor() {
        for (int i = 0; i < Bands; ++i) {
            m_gains[i].store(0.0);
        }
    }

    void EqualizerProcessor::setGain(int band, double gainDb) {
        if (band < 0 || band >= Bands) return;
        gainDb = std::clamp(gainDb, -12.0, 12.0);
        m_gains[band].store(gainDb);
    }

    void EqualizerProcessor::setGains(const QVector<double>& gains) {
        for (int i = 0; i < std::min(gains.size(), static_cast<int>(Bands)); ++i) {
            setGain(i, gains[i]);
        }
    }

    void EqualizerProcessor::process(float* data, int samples, int sampleRate) {
        if (!m_enabled.load() || samples < 2) return;

        if (m_sampleRate.load() != sampleRate) {
            m_sampleRate.store(sampleRate);
            // Reset filters on sample rate change to avoid clicks
            for (auto& f : m_leftFilters) f.reset();
            for (auto& f : m_rightFilters) f.reset();
        }

        {
            std::lock_guard<std::mutex> lock(m_coefficientMutex);
            for (int i = 0; i < Bands; ++i) {
                double g = m_gains[i].load();
                if (std::abs(g) > 0.01) {
                    m_leftFilters[i].configurePeak(DefaultFrequencies[i], sampleRate, g);
                    m_rightFilters[i].configurePeak(DefaultFrequencies[i], sampleRate, g);
                }
            }
        }

        for (int i = 0; i < samples; i += 2) {
            double left = data[i];
            double right = data[i + 1];

            for (int b = 0; b < Bands; ++b) {
                if (std::abs(m_gains[b].load()) > 0.01) {
                    left = m_leftFilters[b].process(left);
                    right = m_rightFilters[b].process(right);
                }
            }

            data[i] = static_cast<float>(left);
            data[i + 1] = static_cast<float>(right);
        }
    }

    KaraokeProcessor::KaraokeProcessor()
    : m_echoBuffer(MaxEchoDelay * 2, 0.0f) {
    }

    KaraokeProcessor::~KaraokeProcessor() = default;

    void KaraokeProcessor::setKeyChange(int semitones) {
        m_keySemitones.store(std::clamp(semitones, -12, 12));
    }

    void KaraokeProcessor::updatePitchRatio(int sampleRate) {
        int semitones = m_keySemitones.load();
        if (semitones == 0) {
            std::unique_lock<std::shared_mutex> lock(m_stretcherMutex);
            m_stretcher.reset();
            return;
        }

        std::unique_lock<std::shared_mutex> lock(m_stretcherMutex);
        if (!m_stretcher || m_currentSampleRate.load() != sampleRate) {
            m_currentSampleRate.store(sampleRate);
            m_stretcher = std::make_unique<RubberBand::RubberBandStretcher>(
                sampleRate, 2,
                RubberBand::RubberBandStretcher::DefaultOptions
            );
        }

        double ratio = std::pow(2.0, semitones / 12.0);
        m_stretcher->setPitchScale(ratio);
    }

    void KaraokeProcessor::setEnabled(bool enabled) {
        m_enabled.store(enabled);
        if (!enabled) {
            std::fill(m_echoBuffer.begin(), m_echoBuffer.end(), 0.0f);
            m_echoPos.store(0);
        }
    }

    void KaraokeProcessor::process(float* data, int frames, int sampleRate) {
        if (!m_enabled.load()) return;

        // Pitch shifting setup
        if (m_keySemitones.load() != 0 || m_stretcher) {
            updatePitchRatio(sampleRate);
        }

        // Vocal suppression (MS processing)
        if (m_vocalSuppression.load()) {
            applyVocalSuppression(data, frames);
        }

        // Volume controls
        double musicVol = m_musicVolume.load();
        double vocalVol = m_vocalVolume.load();
        if (musicVol != 1.0 || vocalVol != 0.0) {
            for (int i = 0; i < frames * 2; i += 2) {
                data[i] *= musicVol;     // Left channel treated as music
                data[i+1] *= vocalVol;   // Right channel treated as vocal
            }
        }

        // Pitch shifting with thread-safe stretcher access
        if (m_keySemitones.load() != 0) {
            std::shared_lock<std::shared_mutex> lock(m_stretcherMutex);
            if (m_stretcher) {
                m_stretcher->process(data, frames, false);
                size_t available = m_stretcher->available();
                if (available >= static_cast<size_t>(frames)) {
                    m_stretcher->retrieve(data, frames);
                }
            }
        }

        // Echo effect
        if (m_echoLevel.load() > 0.001) {
            applyEcho(data, frames);
        }
    }

    void KaraokeProcessor::applyVocalSuppression(float* data, int frames) {
        for (int i = 0; i < frames * 2; i += 2) {
            float left = data[i];
            float right = data[i + 1];
            float center = (left + right) * 0.5f;
            float side = (left - right) * 0.5f;

            data[i] = side * 2.0f;
            data[i + 1] = -side * 2.0f;
        }
    }

    void KaraokeProcessor::applyEcho(float* data, int frames) {
        float level = m_echoLevel.load();
        size_t delaySamples = static_cast<size_t>(m_currentSampleRate.load() * 0.3);
        delaySamples = std::min(delaySamples, MaxEchoDelay);

        size_t pos = m_echoPos.load();

        for (int i = 0; i < frames * 2; i += 2) {
            for (int ch = 0; ch < 2; ++ch) {
                size_t idx = (pos + ch) % (MaxEchoDelay * 2);
                float echoSample = m_echoBuffer[idx];

                m_echoBuffer[idx] = data[i + ch] + echoSample * 0.5f;
                data[i + ch] = data[i + ch] * (1.0f - level) + echoSample * level;
            }
            pos = (pos + 2) % (MaxEchoDelay * 2);
        }
        m_echoPos.store(pos);
    }

    Crossfader::Crossfader() = default;

    void Crossfader::start(double durationMs, Curve curve) {
        m_duration = durationMs;
        m_curve = curve;
        m_startTime = QDateTime::currentMSecsSinceEpoch();
        m_state.store(State::FadingOut);
        m_progress.store(0.0);
    }

    void Crossfader::stop() {
        m_state.store(State::Idle);
        m_progress.store(0.0);
    }

    double Crossfader::currentGain() const {
        State s = m_state.load();
        if (s == State::Idle) return 1.0;
        if (s == State::Complete) return 0.0;

        double p = progress();
        if (s == State::FadingIn) p = 1.0 - p;

        switch(m_curve) {
            case Curve::EqualPower:
                return std::cos(p * M_PI / 2.0);
            case Curve::Exponential:
                return 1.0 - (p * p);
            case Curve::Linear:
            default:
                return 1.0 - p;
        }
    }

    double Crossfader::progress() const {
        if (m_state.load() == State::Idle) return 0.0;
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startTime;
        return std::min(1.0, elapsed / m_duration);
    }

    ModTrackerPlayback::ModTrackerPlayback(AudioEngine* engine)
    : QObject(engine), m_engine(engine) {
        m_renderBuffer.resize(CHUNK_FRAMES * CHANNELS);
        m_generationTimer.setInterval(10);
        connect(&m_generationTimer, &QTimer::timeout, this, &ModTrackerPlayback::generateAudioChunk);
    }

    ModTrackerPlayback::~ModTrackerPlayback() = default;

    bool ModTrackerPlayback::load(const QString& path) {
        try {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                emit error("Cannot open file: " + path);
                return false;
            }

            QByteArray data = file.readAll();
            file.close();

            if (data.isEmpty()) {
                emit error("Empty file");
                return false;
            }

            std::vector<char> vec(data.begin(), data.end());
            m_module = std::make_unique<openmpt::module>(vec);

            m_module->set_render_param(openmpt::module::RENDER_STEREOSEPARATION_PERCENT, 100);
            m_module->set_render_param(openmpt::module::RENDER_INTERPOLATIONFILTER_LENGTH, 4);

            m_duration = m_module->get_duration_seconds();
            emit positionChanged();
            return true;

        } catch (const std::exception& e) {
            emit error(QString::fromUtf8(e.what()));
            return false;
        }
    }

    void ModTrackerPlayback::play() {
        if (!m_module || m_playing.load()) return;
        m_playing.store(true);
        m_generationTimer.start();
        emit playingChanged();
    }

    void ModTrackerPlayback::pause() {
        if (!m_playing.load()) return;
        m_playing.store(false);
        m_generationTimer.stop();
        emit playingChanged();
    }

    void ModTrackerPlayback::stop() {
        bool wasPlaying = m_playing.load();
        m_playing.store(false);
        m_generationTimer.stop();
        if (m_module) {
            m_module->set_position_seconds(0);
        }
        m_position.store(0);
        emit positionChanged();
        if (wasPlaying) emit playingChanged();
    }

    void ModTrackerPlayback::seek(double seconds) {
        if (!m_module) return;
        seconds = std::clamp(seconds, 0.0, m_duration.load());
        m_module->set_position_seconds(seconds);
        m_position.store(seconds);
        emit positionChanged();
    }

    void ModTrackerPlayback::generateAudioChunk() {
        if (!m_module || !m_playing.load()) return;

        try {
            std::size_t frames = m_module->read_interleaved_stereo(
                SAMPLE_RATE, CHUNK_FRAMES, m_renderBuffer.data()
            );

            if (frames == 0) {
                emit finished();
                stop();
                return;
            }

            float vol = m_volume.load();
            if (vol != 1.0f) {
                for (size_t i = 0; i < frames * CHANNELS; ++i) {
                    m_renderBuffer[i] *= vol;
                }
            }

            if (m_engine) {
                m_engine->processBuffer(m_renderBuffer.data(), frames, SAMPLE_RATE, CHANNELS);
            }

            double pos = m_module->get_position_seconds();
            m_position.store(pos);
            emit positionChanged();

        } catch (const std::exception& e) {
            emit error(QString::fromUtf8(e.what()));
            stop();
        }
    }

    AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , m_fftIn(static_cast<float*>(fftwf_alloc_real(m_fftSize)), fftwf_free)
    , m_fftOut(reinterpret_cast<fftwf_complex*>(fftwf_alloc_complex(m_fftSize/2 + 1)), fftwf_free)
    , m_trackerPlayback(std::make_unique<ModTrackerPlayback>(this))
    {
        m_fftPlan = fftwf_plan_dft_r2c_1d(m_fftSize, m_fftIn.get(), m_fftOut.get(), FFTW_MEASURE);
        m_fftWindow.resize(m_fftSize);
        for (int i = 0; i < m_fftSize; ++i) {
            m_fftWindow[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (m_fftSize - 1)));
        }
        m_spectrum.resize(m_bands);
        std::fill(m_spectrum.begin(), m_spectrum.end(), 0.0);

        ebur128_state* state = ebur128_init(2, 48000, EBUR128_MODE_M | EBUR128_MODE_S | EBUR128_MODE_I);
        if (!state) throw std::runtime_error("EBU R128 init failed");
        m_eburState.reset(state);

        connect(m_trackerPlayback.get(), &ModTrackerPlayback::finished, this, &AudioEngine::processingFinished);
    }

    AudioEngine::~AudioEngine() {
        if (m_fftPlan) fftwf_destroy_plan(reinterpret_cast<fftwf_plan>(m_fftPlan));
    }

    void AudioEngine::setProcessingEnabled(bool enabled) {
        m_processingEnabled.store(enabled);
    }

    void AudioEngine::setKaraokeEnabled(bool enabled) {
        m_karaokeEnabled.store(enabled);
        m_karaoke.setEnabled(enabled);
        emit karaokeChanged();
    }

    bool AudioEngine::isTrackerFile(const QString& path) const {
        static const QStringList exts = {
            "mod", "xm", "s3m", "it", "mtm", "stm", "dsm", "imf",
            "far", "ult", "669", "med", "okt", "dmf", "mt2", "j2b",
            "umx", "ptm", "mptm", "ft", "fst", "sfx"
        };
        QFileInfo info(path);
        return exts.contains(info.suffix().toLower());
    }

    bool AudioEngine::loadTrackerModule(const QString& path) {
        if (m_trackerPlayback->isLoaded()) {
            unloadTracker();
        }
        if (!m_trackerPlayback->load(path)) return false;
        m_currentSource = AudioSource::TrackerModule;
        emit trackerModeChanged(true);
        return true;
    }

    void AudioEngine::unloadTracker() {
        m_trackerPlayback->unload();
        m_currentSource = AudioSource::ExternalPCM;
        emit trackerModeChanged(false);
    }

    void AudioEngine::playTracker() { m_trackerPlayback->play(); }
    void AudioEngine::pauseTracker() { m_trackerPlayback->pause(); }
    void AudioEngine::stopTracker() { m_trackerPlayback->stop(); }
    void AudioEngine::seekTracker(double seconds) { m_trackerPlayback->seek(seconds); }

    void AudioEngine::processPCM(const QByteArray &data, int sampleRate, int channels) {
        if (m_currentSource == AudioSource::TrackerModule) return;
        if (data.isEmpty()) return;

        const float* ptr = reinterpret_cast<const float*>(data.constData());
        int frames = data.size() / (channels * sizeof(float));
        processBuffer(const_cast<float*>(ptr), frames, sampleRate, channels);
    }

    void AudioEngine::processBuffer(float* interleavedData, int frames, int sampleRate, int channels) {
        if (!interleavedData || frames == 0) return;

        if (m_processingEnabled.load() && m_equalizer.enabled() && channels == 2) {
            m_equalizer.process(interleavedData, frames * channels, sampleRate);
        }

        if (m_karaokeEnabled.load() && channels == 2) {
            m_karaoke.process(interleavedData, frames, sampleRate);
        }

        if (m_crossfader.state() != Crossfader::State::Idle && channels == 2) {
            float gain = static_cast<float>(m_crossfader.currentGain());
            for (int i = 0; i < frames * channels; ++i) {
                interleavedData[i] *= gain;
            }
        }

        std::vector<float> mono(frames);
        for (int i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels; ++ch) {
                sum += interleavedData[i * channels + ch];
            }
            mono[i] = sum / channels;
        }

        calculateFFT(mono);
        calculateLoudness(interleavedData, frames * channels);
    }

    void AudioEngine::calculateFFT(const std::vector<float> &input) {
        int n = std::min(static_cast<int>(input.size()), m_fftSize);
        for (int i = 0; i < n; ++i) {
            m_fftIn.get()[i] = input[i] * m_fftWindow[i];
        }
        std::fill_n(m_fftIn.get() + n, m_fftSize - n, 0.0f);

        fftwf_execute(reinterpret_cast<fftwf_plan>(m_fftPlan));

        const float minFreq = 20.0f, maxFreq = 20000.0f;
        const float binWidth = 48000.0f / m_fftSize;
        std::vector<double> localSpectrum(m_bands, 0.0);

        for (int i = 0; i < m_bands; ++i) {
            float t = i / float(m_bands - 1);
            float logStart = std::log10(minFreq) + t * (std::log10(maxFreq) - std::log10(minFreq));
            float logEnd = std::log10(minFreq) + (t + 1.0f/m_bands) * (std::log10(maxFreq) - std::log10(minFreq));

            int binStart = std::max(1, static_cast<int>(std::pow(10.0f, logStart) / binWidth));
            int binEnd = std::min(m_fftSize/2, static_cast<int>(std::pow(10.0f, logEnd) / binWidth));

            if (binEnd <= binStart) continue;

            double sum = 0;
            for (int j = binStart; j < binEnd; ++j) {
                float real = m_fftOut.get()[j][0];
                float imag = m_fftOut.get()[j][1];
                sum += std::sqrt(real*real + imag*imag);
            }

            float avg = sum / (binEnd - binStart);
            float db = 20 * std::log10(avg + 1e-6);
            float normalized = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
            localSpectrum[i] = normalized * 0.8 + m_spectrum[i] * 0.2;
        }

        {
            QMutexLocker lock(&m_mutex);
            m_spectrum = QVector<double>(localSpectrum.begin(), localSpectrum.end());
        }
        emit spectrumUpdated();
    }

    void AudioEngine::calculateLoudness(const float* data, size_t samples) {
        if (!m_eburState || !data) return;
        ebur128_add_frames_float(m_eburState.get(), data, samples / 2);

        double momentary = -70.0, shortTerm = -70.0;
        ebur128_loudness_momentary(m_eburState.get(), &momentary);
        ebur128_loudness_shortterm(m_eburState.get(), &shortTerm);

        m_momentary.store(momentary);
        m_shortTerm.store(shortTerm);
        ebur128_loudness_global(m_eburState.get(), &m_integrated);

        emit loudnessUpdated();
    }

    void EburStateDeleter::operator()(ebur128_state* p) const {
        if (p) ebur128_destroy(&p);
    }

} // namespace Aegis
