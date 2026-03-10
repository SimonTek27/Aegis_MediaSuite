// audio.cpp - Audio Engine Core Implementation

// audio_output.h MUST be included before audio.h: audio.h forward-declares
// AudioOutput, but unique_ptr<AudioOutput> members require the complete type
// at the point where AudioEngine's constructor/destructor are instantiated.
#include "audio_output.h"
#include "audio.h"
#include <cmath>
#include <algorithm>
#include <fftw3.h>
#include <QMutexLocker>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <libopenmpt/libopenmpt.hpp>

#ifdef __SSE2__
#include <emmintrin.h>
#endif

#ifdef __SSE4_1__
#include <smmintrin.h>  // SSE4.1: _mm_blendv_ps
#endif

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace Aegis {

    // ============================================================================
    // Lock-Free Ring Buffer for Echo Effect
    // ============================================================================

    template<typename T, size_t Size>
    class LockFreeRingBuffer {
        static_assert((Size & (Size - 1)) == 0, "Size must be power of two");

    private:
        std::array<T, Size> m_buffer;
        std::atomic<size_t> m_writeIndex{0};
        std::atomic<size_t> m_readIndex{0};
        static constexpr size_t MASK = Size - 1;

        // Padding to prevent false sharing
        char padding[64];

    public:
        LockFreeRingBuffer() {
            m_buffer.fill(T(0));
            std::memset(padding, 0, sizeof(padding));  // Fix: initialize padding to avoid UB with sanitizers
        }

        bool push(const T& value) {
            size_t write = m_writeIndex.load(std::memory_order_relaxed);
            size_t next = (write + 1) & MASK;

            if (next == m_readIndex.load(std::memory_order_acquire)) {
                return false;  // Buffer full
            }

            m_buffer[write] = value;
            m_writeIndex.store(next, std::memory_order_release);
            return true;
        }

        bool pop(T& value) {
            size_t read = m_readIndex.load(std::memory_order_relaxed);

            if (read == m_writeIndex.load(std::memory_order_acquire)) {
                return false;  // Buffer empty
            }

            value = m_buffer[read];
            m_readIndex.store((read + 1) & MASK, std::memory_order_release);
            return true;
        }

        T* front() {
            size_t read = m_readIndex.load(std::memory_order_acquire);
            return (read == m_writeIndex.load(std::memory_order_acquire))
            ? nullptr : &m_buffer[read];
        }

        void advanceRead(size_t count = 1) {
            m_readIndex.store((m_readIndex.load(std::memory_order_relaxed) + count) & MASK,
                              std::memory_order_release);
        }

        size_t available() const {
            size_t write = m_writeIndex.load(std::memory_order_acquire);
            size_t read = m_readIndex.load(std::memory_order_acquire);
            return (write - read) & MASK;
        }

        void reset() {
            m_writeIndex.store(0, std::memory_order_release);
            m_readIndex.store(0, std::memory_order_release);
            std::fill(m_buffer.begin(), m_buffer.end(), T(0));
        }
    };

    // ============================================================================
    // KaraokeProcessor Private Implementation
    // ============================================================================

    class KaraokeProcessor::Private {
    public:
        // Processing parameters with proper memory ordering
        std::atomic<int> keySemitones{0};
        std::atomic<bool> vocalSuppression{true};
        std::atomic<double> musicVolume{1.0};
        std::atomic<double> vocalVolume{0.0};
        std::atomic<double> echoLevel{0.0};
        std::atomic<bool> enabled{false};
        std::atomic<int> currentSampleRate{48000};

        // Pitch shifting with RCU-like semantics
        std::shared_mutex stretcherMutex;
        std::shared_ptr<RubberBand::RubberBandStretcher> stretcher;

        // Lock-free echo buffer
        // NOTE: LockFreeRingBuffer requires size to be a power of 2.
        // 96000 * 2 = 192000 is not a power of 2; use 131072 (2^17) or 262144 (2^18).
        // 131072 samples @ 48kHz ≈ 2.73s of mono delay — sufficient for echo effect.
        static constexpr size_t MaxEchoDelay = 65536;  // 2^16, ~1.36s mono at 48kHz
        using EchoBuffer = LockFreeRingBuffer<float, MaxEchoDelay * 2>;  // 2^17 = 131072
        EchoBuffer echoBuffer;

        // SIMD-aligned temporary buffers
        alignas(32) std::vector<float> tempInput;
        alignas(32) std::vector<float> tempOutput;

        Private() {
            tempInput.reserve(4096);
            tempOutput.reserve(4096);
        }

        ~Private() = default;

        // Update pitch stretcher with copy-on-write semantics
        void updateStretcher(int sampleRate) {
            int semitones = keySemitones.load(std::memory_order_acquire);

            if (semitones == 0) {
                std::unique_lock lock(stretcherMutex);
                stretcher.reset();
                return;
            }

            // Create new stretcher if needed
            if (!stretcher || currentSampleRate.load() != sampleRate) {
                auto newStretcher = std::make_shared<RubberBand::RubberBandStretcher>(
                    sampleRate, 2,
                    RubberBand::RubberBandStretcher::OptionProcessRealTime |
                    RubberBand::RubberBandStretcher::OptionEngineFaster
                );

                double ratio = std::pow(2.0, semitones / 12.0);
                newStretcher->setPitchScale(ratio);

                {
                    std::unique_lock lock(stretcherMutex);
                    stretcher.swap(newStretcher);
                }

                currentSampleRate.store(sampleRate, std::memory_order_release);
            }
        }

        #ifdef __AVX2__
        // AVX2-optimized vocal suppression
        void applyVocalSuppressionAVX2(float* data, int frames) {
            const int simdFrames = frames & ~3;  // Multiple of 4

            for (int i = 0; i < simdFrames * 2; i += 8) {
                __m256 leftRight = _mm256_loadu_ps(&data[i]);

                // Deinterleave: [L0,R0,L1,R1,L2,R2,L3,R3] -> [L0,L1,L2,L3], [R0,R1,R2,R3]
                // Fix: _mm256_unpacklo/hi + shuffle produces wrong cross-lane results for frames>4.
                // Use _mm256_permutevar8x32_ps with explicit gather indices instead.
                const __m256i idx_left  = _mm256_set_epi32(6,4,2,0,6,4,2,0);
                const __m256i idx_right = _mm256_set_epi32(7,5,3,1,7,5,3,1);
                __m256 left  = _mm256_permutevar8x32_ps(leftRight, idx_left);
                __m256 right = _mm256_permutevar8x32_ps(leftRight, idx_right);

                // center = (left + right) * 0.5
                // side = (left - right) * 0.5
                __m256 center = _mm256_mul_ps(_mm256_add_ps(left, right),
                                              _mm256_set1_ps(0.5f));
                __m256 side = _mm256_mul_ps(_mm256_sub_ps(left, right),
                                            _mm256_set1_ps(0.5f));

                // output L = side * 2, output R = -side * 2
                __m256 outL = _mm256_mul_ps(side, _mm256_set1_ps(2.0f));
                __m256 outR = _mm256_mul_ps(side, _mm256_set1_ps(-2.0f));

                // Interleave back
                __m256 outLo = _mm256_unpacklo_ps(outL, outR);
                __m256 outHi = _mm256_unpackhi_ps(outL, outR);
                __m256 out = _mm256_shuffle_ps(outLo, outHi, _MM_SHUFFLE(1,0,1,0));

                _mm256_storeu_ps(&data[i], out);
            }

            // Handle remaining frames
            for (int i = simdFrames * 2; i < frames * 2; i += 2) {
                float left = data[i];
                float right = data[i + 1];
                float center = (left + right) * 0.5f;
                float side = (left - right) * 0.5f;

                data[i] = side * 2.0f;
                data[i + 1] = -side * 2.0f;
            }
        }
        #elif defined(__SSE2__)
        // SSE2-optimized vocal suppression
        void applyVocalSuppressionSSE2(float* data, int frames) {
            const int simdFrames = frames & ~1;  // Multiple of 2

            for (int i = 0; i < simdFrames * 2; i += 4) {
                __m128 leftRight = _mm_loadu_ps(&data[i]);

                // Extract left and right channels
                __m128 left = _mm_shuffle_ps(leftRight, leftRight, _MM_SHUFFLE(2,0,2,0));
                __m128 right = _mm_shuffle_ps(leftRight, leftRight, _MM_SHUFFLE(3,1,3,1));

                // center = (left + right) * 0.5
                // side = (left - right) * 0.5
                __m128 center = _mm_mul_ps(_mm_add_ps(left, right), _mm_set1_ps(0.5f));
                __m128 side = _mm_mul_ps(_mm_sub_ps(left, right), _mm_set1_ps(0.5f));

                // output L = side * 2, output R = -side * 2
                __m128 outL = _mm_mul_ps(side, _mm_set1_ps(2.0f));
                __m128 outR = _mm_mul_ps(side, _mm_set1_ps(-2.0f));

                // Interleave
                __m128 out = _mm_unpacklo_ps(outL, outR);

                _mm_storeu_ps(&data[i], out);
            }

            // Handle remaining frames
            for (int i = simdFrames * 2; i < frames * 2; i += 2) {
                float left = data[i];
                float right = data[i + 1];
                float center = (left + right) * 0.5f;
                float side = (left - right) * 0.5f;

                data[i] = side * 2.0f;
                data[i + 1] = -side * 2.0f;
            }
        }
        #endif

        // Lock-free echo processing
        void applyEcho(float* data, int frames) {
            float level = echoLevel.load(std::memory_order_acquire);
            if (level < 0.001f) return;

            for (int i = 0; i < frames * 2; i += 2) {
                float left = data[i];
                float right = data[i + 1];

                // Try to read echo samples
                float echoLeft = 0.0f;
                float echoRight = 0.0f;
                echoBuffer.pop(echoLeft);
                echoBuffer.pop(echoRight);

                // Write new echo samples
                echoBuffer.push(left + echoLeft * 0.5f);
                echoBuffer.push(right + echoRight * 0.5f);

                // Mix with dry signal
                data[i] = left * (1.0f - level) + echoLeft * level;
                data[i + 1] = right * (1.0f - level) + echoRight * level;
            }
        }
    };

    KaraokeProcessor::KaraokeProcessor()
    : d(std::make_unique<Private>()) {
    }

    KaraokeProcessor::~KaraokeProcessor() = default;

    void KaraokeProcessor::setKeyChange(int semitones) {
        d->keySemitones.store(std::clamp(semitones, -12, 12),
                              std::memory_order_release);
    }

    void KaraokeProcessor::setVocalSuppressionEnabled(bool enabled) {
        d->vocalSuppression.store(enabled, std::memory_order_release);
    }

    void KaraokeProcessor::setMusicVolume(double volume) {
        d->musicVolume.store(std::clamp(volume, 0.0, 2.0),
                             std::memory_order_release);
    }

    void KaraokeProcessor::setVocalVolume(double volume) {
        d->vocalVolume.store(std::clamp(volume, 0.0, 2.0),
                             std::memory_order_release);
    }

    void KaraokeProcessor::setEchoLevel(double level) {
        d->echoLevel.store(std::clamp(level, 0.0, 1.0),
                           std::memory_order_release);
    }

    void KaraokeProcessor::setEnabled(bool enabled) {
        d->enabled.store(enabled, std::memory_order_release);
        if (!enabled) {
            d->echoBuffer.reset();
        }
    }

    bool KaraokeProcessor::enabled() const {
        return d->enabled.load(std::memory_order_acquire);
    }

    bool KaraokeProcessor::vocalSuppressionEnabled() const {
        return d->vocalSuppression.load(std::memory_order_acquire);
    }

    void KaraokeProcessor::process(float* interleavedData, int frames, int sampleRate) {
        if (!d->enabled.load(std::memory_order_acquire) || frames <= 0) {
            return;
        }

        // Update pitch stretcher if needed
        if (d->keySemitones.load(std::memory_order_acquire) != 0) {
            d->updateStretcher(sampleRate);
        }

        // Apply effects in order
        if (d->vocalSuppression.load(std::memory_order_acquire)) {
            #ifdef __AVX2__
            d->applyVocalSuppressionAVX2(interleavedData, frames);
            #elif defined(__SSE2__)
            d->applyVocalSuppressionSSE2(interleavedData, frames);
            #else
            // Scalar fallback
            for (int i = 0; i < frames * 2; i += 2) {
                float left = interleavedData[i];
                float right = interleavedData[i + 1];
                float center = (left + right) * 0.5f;
                float side = (left - right) * 0.5f;

                interleavedData[i] = side * 2.0f;
                interleavedData[i + 1] = -side * 2.0f;
            }
            #endif
        }

        // Volume mixing (with SIMD optimization)
        double musicVol = d->musicVolume.load(std::memory_order_acquire);
        double vocalVol = d->vocalVolume.load(std::memory_order_acquire);

        if (musicVol != 1.0 || vocalVol != 0.0) {
            const int samples = frames * 2;

            #ifdef __SSE4_1__
            const int simdSamples = samples & ~3;

            for (int i = 0; i < simdSamples; i += 4) {
                __m128 v = _mm_loadu_ps(&interleavedData[i]);
                __m128 music = _mm_set1_ps(static_cast<float>(musicVol));
                __m128 vocal = _mm_set1_ps(static_cast<float>(vocalVol));

                // Alternate between music and vocal gains
                __m128 mask = _mm_set_ps(1.0f, 0.0f, 1.0f, 0.0f);
                __m128 mixed = _mm_add_ps(
                    _mm_mul_ps(v, _mm_blendv_ps(music, vocal, mask)),
                                          _mm_mul_ps(v, _mm_blendv_ps(vocal, music, mask))
                );

                _mm_storeu_ps(&interleavedData[i], mixed);
            }

            for (int i = simdSamples; i < samples; i += 2)
                #else
                for (int i = 0; i < samples; i += 2)
                    #endif
                {
                    interleavedData[i] *= musicVol;
                    interleavedData[i + 1] *= vocalVol;
                }
        }

        // Pitch shifting with shared_ptr copy for thread safety
        if (d->keySemitones.load(std::memory_order_acquire) != 0) {
            std::shared_ptr<RubberBand::RubberBandStretcher> stretcher;
            {
                std::shared_lock lock(d->stretcherMutex);
                stretcher = d->stretcher;  // Atomic shared_ptr copy
            }

            if (stretcher) {
                const float* in[2] = {interleavedData, interleavedData + 1};
                float* out[2] = {interleavedData, interleavedData + 1};

                stretcher->process(in, static_cast<size_t>(frames), false);
                size_t available = stretcher->available();
                if (available >= static_cast<size_t>(frames)) {
                    stretcher->retrieve(out, static_cast<size_t>(frames));
                }
            }
        }

        // Echo effect
        if (d->echoLevel.load(std::memory_order_acquire) > 0.001f) {
            d->applyEcho(interleavedData, frames);
        }
    }

    // ============================================================================
    // EqualizerProcessor Implementation
    // ============================================================================

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
        int limit = std::min<int>(static_cast<int>(gains.size()), static_cast<int>(Bands));
        for (int i = 0; i < limit; ++i) {
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

    // ============================================================================
    // Crossfader Implementation
    // ============================================================================

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

    // ============================================================================
    // ModTrackerPlayback Implementation
    // ============================================================================

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

    QString ModTrackerPlayback::title() const {
        if (!m_module) return QString();
        return QString::fromStdString(m_module->get_metadata("title"));
    }

    QString ModTrackerPlayback::artist() const {
        if (!m_module) return QString();
        return QString::fromStdString(m_module->get_metadata("artist"));
    }

    int ModTrackerPlayback::getNumPatterns() const {
        if (!m_module) return 0;
        return m_module->get_num_patterns();
    }

    int ModTrackerPlayback::getNumChannels() const {
        if (!m_module) return 0;
        return m_module->get_num_channels();
    }

    int ModTrackerPlayback::getCurrentPattern() const {
        if (!m_module) return 0;
        return m_module->get_current_pattern();
    }

    int ModTrackerPlayback::getCurrentRow() const {
        if (!m_module) return 0;
        return m_module->get_current_row();
    }

    QStringList ModTrackerPlayback::supportedExtensions() const {
        return {
            QStringLiteral("mod"), QStringLiteral("xm"),  QStringLiteral("it"),
            QStringLiteral("s3m"), QStringLiteral("mptm"), QStringLiteral("mo3"),
            QStringLiteral("669"), QStringLiteral("mtm"),  QStringLiteral("umx"),
            QStringLiteral("okt"), QStringLiteral("stm"),  QStringLiteral("far"),
            QStringLiteral("amf"), QStringLiteral("ams"),  QStringLiteral("dbm"),
            QStringLiteral("med"), QStringLiteral("mid")
        };
    }

    void ModTrackerPlayback::unload() {
        stop();
        m_module.reset();
        m_duration.store(0.0);
        m_position.store(0.0);
        emit metadataChanged();
    }

    bool ModTrackerPlayback::isTrackerFile(const QString& path) const {
        const QString ext = QFileInfo(path).suffix().toLower();
        return supportedExtensions().contains(ext);
    }

    // ============================================================================
    // AudioEngine Implementation
    // ============================================================================

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
        emit karaokeChanged(enabled);
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

    QVariantList AudioEngine::spectrumData() const {
        QMutexLocker lock(&m_mutex);
        QVariantList list;
        for (double v : m_spectrum) {
            list.append(v);
        }
        return list;
    }

    double AudioEngine::integratedLoudness() {
        return m_integrated;
    }

    void AudioEngine::EburStateDeleter::operator()(ebur128_state* p) const {
        if (p) ebur128_destroy(&p);
    }

    QVector<float> AudioEngine::calculateSpectrum(const float* data, int samples, int channels) {
        if (!data || samples <= 0 || channels <= 0) return {};

        // Mix down to mono
        std::vector<float> mono(samples);
        for (int i = 0; i < samples; ++i) {
            float s = 0.0f;
            for (int c = 0; c < channels; ++c)
                s += data[i * channels + c];
            mono[i] = s / channels;
        }

        const int fftSamples = qMin(samples, m_fftSize);
        for (int i = 0; i < fftSamples; ++i)
            m_fftIn.get()[i] = mono[i] * m_fftWindow[i];
        for (int i = fftSamples; i < m_fftSize; ++i)
            m_fftIn.get()[i] = 0.0f;

        fftwf_execute(reinterpret_cast<fftwf_plan>(m_fftPlan));

        const int bins = m_fftSize / 2 + 1;
        QVector<float> result(bins);
        for (int i = 0; i < bins; ++i) {
            float re = m_fftOut.get()[i][0];
            float im = m_fftOut.get()[i][1];
            result[i] = std::sqrt(re * re + im * im) / m_fftSize;
        }
        return result;
    }

} // namespace Aegis
