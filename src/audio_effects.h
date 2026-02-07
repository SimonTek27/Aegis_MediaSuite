// audio_effects.h - Complete Audio Effects Processing System
// Provides modular audio effects with real-time and offline processing support
// Includes enhanced buffer management, DSP utilities, and effect chain management

#pragma once

#include "audio.h"
#include <QObject>
#include <QVariantMap>
#include <QReadWriteLock>
#include <QMutex>
#include <QMap>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <atomic>
#include <memory>
#include <complex>

#ifdef __SSE2__
#include <emmintrin.h>
#endif

namespace Aegis {

    // ============================================================================
    // Smoothed Parameter for Real-time Parameter Changes
    // ============================================================================

    /**
     * @brief Smoothly interpolates parameter changes to prevent audio artifacts
     *
     * Uses exponential smoothing to gradually change parameter values over time.
     * Essential for real-time parameter automation without clicks or pops.
     */
    class SmoothedParameter {
    public:
        /**
         * @brief Construct smoothed parameter
         * @param initial Initial parameter value
         * @param smoothingTimeMs Smoothing time constant in milliseconds
         */
        SmoothedParameter(float initial = 0.0f, float smoothingTimeMs = 5.0f)
        : m_target(initial), m_current(initial), m_smoothingTimeMs(smoothingTimeMs) {}

        /**
         * @brief Set target value (will smooth to this value)
         * @param value New target value
         */
        void setTarget(float value) { m_target.store(value); }

        /**
         * @brief Set value immediately (no smoothing)
         * @param value New immediate value
         */
        void setImmediate(float value) {
            m_target.store(value);
            m_current = value;
        }

        /**
         * @brief Get current smoothed value
         * @return Current interpolated value
         */
        float getCurrent() const { return m_current; }

        /**
         * @brief Get target value
         * @return Target value being smoothed toward
         */
        float getTarget() const { return m_target.load(); }

        /**
         * @brief Check if parameter has reached target
         * @return True if settled within tolerance
         */
        bool isSettled() const { return std::abs(m_current - m_target.load()) < 0.0001f; }

        /**
         * @brief Update smoothing state for processed samples
         * @param samples Number of samples processed since last update
         * @param sampleRate Current audio sample rate
         */
        void process(int samples, int sampleRate) {
            float target = m_target.load();
            if (std::abs(m_current - target) < 0.00001f) return;

            // Calculate smoothing coefficient on first use
            if (m_smoothingCoeff == 0.0f) {
                float samplesPerMs = sampleRate / 1000.0f;
                float samplesForTime = m_smoothingTimeMs * samplesPerMs;
                m_smoothingCoeff = std::exp(-1.0f / samplesForTime);
            }

            // Apply exponential smoothing
            float coeff = std::pow(m_smoothingCoeff, samples);
            m_current = target + (m_current - target) * coeff;
        }

    private:
        std::atomic<float> m_target;     ///< Target value (thread-safe)
        float m_current = 0.0f;          ///< Current smoothed value
        float m_smoothingCoeff = 0.0f;   ///< Pre-calculated smoothing coefficient
        float m_smoothingTimeMs;         ///< Smoothing time constant
    };

    // ============================================================================
    // Enhanced Audio Buffer with SIMD Optimization
    // ============================================================================

    /**
     * @brief High-performance audio buffer with SIMD optimizations
     *
     * Provides efficient audio data storage and manipulation with SSE/AVX optimizations.
     * Supports multi-channel audio, sample-accurate editing, and vectorized operations.
     */
    class EnhancedAudioBuffer {
    public:
        EnhancedAudioBuffer() = default;

        /**
         * @brief Construct buffer with specified size
         * @param frames Number of sample frames
         * @param channels Number of audio channels
         */
        explicit EnhancedAudioBuffer(qint64 frames, int channels = 2) { resize(frames, channels); }

        // Data access
        float* data() { return m_data.data(); }
        const float* data() const { return m_data.data(); }

        /**
         * @brief Get pointer to specific channel data (interleaved)
         * @param channel Channel index (0-based)
         * @return Pointer to first sample of channel, or nullptr if invalid
         */
        float* channelData(int channel) {
            if (channel < 0 || channel >= m_channels) return nullptr;
            return m_data.data() + channel;
        }

        const float* channelData(int channel) const {
            if (channel < 0 || channel >= m_channels) return nullptr;
            return m_data.data() + channel;
        }

        /**
         * @brief Get sample at specific position
         * @param channel Channel index
         * @param frame Frame index
         * @return Sample value, or 0.0f if out of bounds
         */
        float sampleAt(int channel, qint64 frame) const {
            if (channel < 0 || channel >= m_channels || frame < 0 || frame >= m_frames) return 0.0f;
            return m_data[frame * m_channels + channel];
        }

        /**
         * @brief Set sample at specific position
         * @param channel Channel index
         * @param frame Frame index
         * @param value Sample value
         */
        void setSample(int channel, qint64 frame, float value) {
            if (channel < 0 || channel >= m_channels || frame < 0 || frame >= m_frames) return;
            m_data[frame * m_channels + channel] = value;
        }

        // Buffer information
        int samples() const { return m_frames; }
        qint64 frames() const { return m_frames; }
        int channels() const { return m_channels; }
        qint64 totalSamples() const { return static_cast<qint64>(m_frames) * m_channels; }
        bool isEmpty() const { return m_frames == 0; }

        /**
         * @brief Resize buffer (preserves existing data if possible)
         * @param frames New frame count
         * @param channels New channel count
         */
        void resize(qint64 frames, int channels) {
            m_frames = frames;
            m_channels = channels;
            m_data.resize(static_cast<size_t>(frames) * channels);
        }

        // Buffer manipulation
        void clear() { m_data.clear(); m_frames = 0; }
        void fill(float value) { std::fill(m_data.begin(), m_data.end(), value); }
        void zero() { fill(0.0f); }

        /**
         * @brief Copy data from another buffer
         * @param src Source buffer
         * @param srcOffset Source start frame
         * @param dstOffset Destination start frame
         * @param count Number of frames to copy
         */
        void copyFrom(const EnhancedAudioBuffer &src, int srcOffset, int dstOffset, int count) {
            int srcStart = srcOffset * src.m_channels;
            int dstStart = dstOffset * m_channels;
            qint64 maxCopy = std::min<qint64>(src.totalSamples() - srcStart,
                                              totalSamples() - dstStart);
            int copyCount = std::min(count * src.m_channels,
                                     static_cast<int>(maxCopy));
            if (copyCount > 0) {
                std::memcpy(m_data.data() + dstStart, src.data() + srcStart,
                            static_cast<size_t>(copyCount) * sizeof(float));
            }
        }

        /**
         * @brief Extract sub-range as new buffer
         * @param startFrame Start frame index
         * @param count Number of frames to extract
         * @return New buffer containing the slice
         */
        EnhancedAudioBuffer slice(qint64 startFrame, qint64 count) const {
            if (startFrame < 0) startFrame = 0;
            if (startFrame >= m_frames) return EnhancedAudioBuffer();
            if (startFrame + count > m_frames) count = m_frames - startFrame;

            EnhancedAudioBuffer result(count, m_channels);
            result.copyFrom(*this, static_cast<int>(startFrame), 0, static_cast<int>(count));
            return result;
        }

        /**
         * @brief Insert buffer at specified position
         * @param pos Insertion position in frames
         * @param other Buffer to insert
         */
        void insert(qint64 pos, const EnhancedAudioBuffer& other) {
            if (other.isEmpty()) return;
            if (pos < 0) pos = 0;
            if (pos > m_frames) pos = m_frames;

            qint64 oldFrames = m_frames;
            resize(m_frames + other.m_frames, m_channels);

            if (pos < oldFrames) {
                std::memmove(m_data.data() + (pos + other.m_frames) * m_channels,
                             m_data.data() + pos * m_channels,
                             (oldFrames - pos) * m_channels * sizeof(float));
            }

            std::memcpy(m_data.data() + pos * m_channels,
                        other.m_data.data(),
                        other.m_data.size() * sizeof(float));
        }

        /**
         * @brief Remove range from buffer
         * @param start Start frame index
         * @param count Number of frames to remove
         */
        void remove(qint64 start, qint64 count) {
            if (start < 0 || start >= m_frames || count <= 0) return;
            if (start + count > m_frames) count = m_frames - start;

            qint64 remaining = m_frames - start - count;
            if (remaining > 0) {
                std::memmove(m_data.data() + start * m_channels,
                             m_data.data() + (start + count) * m_channels,
                             remaining * m_channels * sizeof(float));
            }
            resize(m_frames - count, m_channels);
        }

        // Mixing operations (SIMD optimized)

        /**
         * @brief Mix another buffer into this one
         * @param other Buffer to mix in
         * @param gain Gain applied to other buffer before mixing
         * @param dstOffset Destination offset in frames
         */
        void mix(const EnhancedAudioBuffer &other, float gain = 1.0f, int dstOffset = 0) {
            int count = std::min(other.totalSamples(), totalSamples() - dstOffset * m_channels);
            if (count <= 0) return;

            const float *src = other.data();
            float *dst = m_data.data() + dstOffset * m_channels;

            // SIMD-optimized mixing (SSE2)
            #ifdef __SSE2__
            int i = 0;
            __m128 gainVec = _mm_set1_ps(gain);
            for (; i <= count - 4; i += 4) {
                __m128 s = _mm_loadu_ps(src + i);
                __m128 d = _mm_loadu_ps(dst + i);
                _mm_storeu_ps(dst + i, _mm_add_ps(d, _mm_mul_ps(s, gainVec)));
            }
            #endif

            // Remainder processing
            for (int j = i; j < count; j++) dst[j] += src[j] * gain;
        }

        /**
         * @brief Apply constant gain to entire buffer
         * @param gain Linear gain multiplier
         */
        void applyGain(float gain) {
            if (gain == 1.0f) return;

            #ifdef __SSE2__
            int total = totalSamples();
            int i = 0;
            __m128 g = _mm_set1_ps(gain);
            for (; i <= total - 4; i += 4) {
                __m128 v = _mm_loadu_ps(m_data.data() + i);
                _mm_storeu_ps(m_data.data() + i, _mm_mul_ps(v, g));
            }
            for (; i < total; i++) m_data[i] *= gain;
            #else
            for (auto& sample : m_data) sample *= gain;
            #endif
        }

        /**
         * @brief Apply linear gain ramp to buffer section
         * @param startFrame Start frame for ramp
         * @param endFrame End frame for ramp
         * @param startGain Gain at start frame
         * @param endGain Gain at end frame
         */
        void applyGainRamp(qint64 startFrame, qint64 endFrame, float startGain, float endGain) {
            if (startFrame < 0) startFrame = 0;
            if (endFrame > m_frames) endFrame = m_frames;
            qint64 frames = endFrame - startFrame;
            if (frames <= 0) return;

            for (qint64 f = 0; f < frames; f++) {
                float t = f / static_cast<float>(frames);
                float gain = startGain + (endGain - startGain) * t;
                for (int ch = 0; ch < m_channels; ch++) {
                    m_data[(startFrame + f) * m_channels + ch] *= gain;
                }
            }
        }

        // Audio analysis

        /**
         * @brief Find peak amplitude in buffer
         * @param channel Channel to analyze (-1 for all channels)
         * @return Peak amplitude (0.0 to 1.0 typically)
         */
        float peakLevel(int channel = -1) const {
            if (m_data.empty()) return 0.0f;
            float peak = 0.0f;

            if (channel < 0) {
                for (float sample : m_data) peak = std::max(peak, std::abs(sample));
            } else if (channel < m_channels) {
                for (qint64 i = 0; i < m_frames; ++i) {
                    peak = std::max(peak, std::abs(sampleAt(channel, i)));
                }
            }
            return peak;
        }

        /**
         * @brief Calculate RMS level in buffer
         * @param channel Channel to analyze (-1 for all channels)
         * @return RMS amplitude
         */
        float rmsLevel(int channel = -1) const {
            if (m_frames == 0) return 0.0f;
            double sum = 0.0;

            if (channel < 0) {
                for (float sample : m_data) sum += sample * sample;
                return std::sqrt(sum / m_data.size());
            } else if (channel < m_channels) {
                for (qint64 i = 0; i < m_frames; ++i) {
                    float s = sampleAt(channel, i);
                    sum += s * s;
                }
                return std::sqrt(sum / m_frames);
            }
            return 0.0f;
        }

        /**
         * @brief Calculate DC offset (average value)
         * @param channel Channel to analyze
         * @return DC offset value
         */
        float dcOffset(int channel) const {
            if (m_frames == 0 || channel < 0 || channel >= m_channels) return 0.0f;
            double sum = 0.0;
            for (qint64 i = 0; i < m_frames; ++i) {
                sum += sampleAt(channel, i);
            }
            return static_cast<float>(sum / m_frames);
        }

        /**
         * @brief Append buffer to end of this buffer
         * @param other Buffer to append
         */
        void append(const EnhancedAudioBuffer& other) {
            if (other.isEmpty()) return;
            if (isEmpty()) { *this = other; return; }
            insert(m_frames, other);
        }

    private:
        std::vector<float> m_data;   ///< Interleaved audio samples
        qint64 m_frames = 0;         ///< Number of sample frames
        int m_channels = 2;          ///< Number of audio channels
    };

    // ============================================================================
    // Effect Context and Base Class
    // ============================================================================

    /**
     * @brief Processing context for audio effects
     *
     * Determines the processing mode which affects latency requirements,
     * quality settings, and available operations.
     */
    enum class EffectContext {
        Realtime,       ///< Low latency, no lookahead, CPU-optimized
        Offline,        ///< Full file access, lookahead allowed, highest quality
        Preview         ///< Fast, approximate processing for UI feedback
    };

    /**
     * @brief Base class for all audio effects
     *
     * Provides interface for real-time and offline audio processing.
     * Supports parameter automation, state serialization, and effect chaining.
     */
    class AudioEffect : public QObject {
        Q_OBJECT
    public:
        explicit AudioEffect(QObject *parent = nullptr) : QObject(parent) {}
        virtual ~AudioEffect() = default;

        // Effect metadata
        virtual QString name() const = 0;                ///< Effect display name
        virtual QString description() const = 0;         ///< Effect description
        virtual QString category() const { return "General"; } ///< Effect category

        // Context support
        virtual bool supportsRealtime() const { return true; }
        virtual bool supportsOffline() const { return true; }
        virtual bool supportsPreview() const { return true; }

        // Real-time processing
        virtual void process(EnhancedAudioBuffer &buffer, int sampleRate,
                             EffectContext context = EffectContext::Realtime) = 0;

                             // Offline processing with selection support
                             virtual void processOffline(EnhancedAudioBuffer &buffer, const Selection& selection, int sampleRate) {
                                 if (selection.isEmpty() || !selection.isValid()) {
                                     process(buffer, sampleRate, EffectContext::Offline);
                                     return;
                                 }

                                 qint64 startFrame = selection.start;
                                 qint64 length = selection.length();

                                 EnhancedAudioBuffer selected = buffer.slice(startFrame, length);
                                 process(selected, sampleRate, EffectContext::Offline);

                                 // Copy processed selection back to original buffer
                                 buffer.copyFrom(selected, 0, static_cast<int>(startFrame), static_cast<int>(length));
                             }

                             // Effect state
                             bool isEnabled() const { return m_enabled; }
                             void setEnabled(bool enabled) {
                                 m_enabled = enabled;
                                 emit enabledChanged(enabled);
                             }

                             // Latency information (for delay compensation)
                             virtual int latencySamples() const { return 0; }   ///< Processing latency in samples
                             virtual int tailSamples() const { return 0; }      ///< Effect tail length

                             // Parameter management
                             virtual void setParameter(const QString& name, const QVariant& value) {
                                 QWriteLocker lock(&m_paramLock);
                                 m_parameters[name] = value;
                                 emit parameterChanged(name, value);
                             }

                             virtual QVariant getParameter(const QString& name) const {
                                 QReadLocker lock(&m_paramLock);
                                 return m_parameters.value(name);
                             }

                             virtual void setParameters(const QVariantMap &params) {
                                 for (auto it = params.begin(); it != params.end(); ++it) {
                                     setParameter(it.key(), it.value());
                                 }
                             }

                             virtual QVariantMap parameters() const {
                                 QReadLocker lock(&m_paramLock);
                                 return m_parameters;
                             }

                             // State management
                             virtual void reset() {}  ///< Reset effect state (clear buffers, etc.)

                             /**
                              * @brief Create a deep copy of the effect
                              * @return Unique pointer to cloned effect
                              */
                             virtual std::unique_ptr<AudioEffect> clone() const = 0;

    signals:
        void enabledChanged(bool enabled);
        void parameterChanged(const QString& name, const QVariant& value);

    protected:
        std::atomic<bool> m_enabled{true};          ///< Effect enabled state
        mutable QReadWriteLock m_paramLock;         ///< Parameter access lock
        QVariantMap m_parameters;                   ///< Current parameter values
    };

    // ============================================================================
    // DSP Utility Functions and Structures
    // ============================================================================

    namespace DSPUtils {
        /**
         * @brief Convert decibels to linear gain
         * @param db Decibel value
         * @return Linear gain multiplier
         */
        inline float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

        /**
         * @brief Convert linear gain to decibels
         * @param linear Linear gain value
         * @return Decibel value (-96dB for near-zero)
         */
        inline float linearToDb(float linear) {
            return linear > 0.00001f ? 20.0f * std::log10(linear) : -96.0f;
        }

        /**
         * @brief Biquad filter coefficients and state
         *
         * Implements Direct Form I biquad filter structure.
         * Normalized coefficients for numerical stability.
         */
        struct BiquadCoeffs {
            float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;  ///< Denominator coefficients
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;  ///< Numerator coefficients
            float x1 = 0.0f, x2 = 0.0f;             ///< Input history
            float y1 = 0.0f, y2 = 0.0f;             ///< Output history

            /**
             * @brief Normalize coefficients so a0 = 1.0
             */
            void normalize() {
                b0 /= a0; b1 /= a0; b2 /= a0;
                a1 /= a0; a2 /= a0;
                a0 = 1.0f;
            }

            /**
             * @brief Configure as peaking EQ filter
             * @param freq Center frequency in Hz
             * @param q Quality factor (bandwidth)
             * @param gainDb Gain in decibels
             * @param sampleRate Audio sample rate
             */
            void setPeaking(float freq, float q, float gainDb, int sampleRate) {
                float w0 = 2.0f * M_PI * freq / sampleRate;
                float cosw0 = std::cos(w0);
                float sinw0 = std::sin(w0);
                float alpha = sinw0 / (2.0f * q);
                float A = std::pow(10.0f, gainDb / 40.0f);

                b0 = 1.0f + alpha * A;
                b1 = -2.0f * cosw0;
                b2 = 1.0f - alpha * A;
                a0 = 1.0f + alpha / A;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha / A;
                normalize();
            }

            /**
             * @brief Configure as low-pass filter
             * @param freq Cutoff frequency in Hz
             * @param q Quality factor
             * @param sampleRate Audio sample rate
             */
            void setLowPass(float freq, float q, int sampleRate) {
                float w0 = 2.0f * M_PI * freq / sampleRate;
                float cosw0 = std::cos(w0);
                float sinw0 = std::sin(w0);
                float alpha = sinw0 / (2.0f * q);

                b0 = (1.0f - cosw0) / 2.0f;
                b1 = 1.0f - cosw0;
                b2 = (1.0f - cosw0) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha;
                normalize();
            }

            /**
             * @brief Configure as high-pass filter
             * @param freq Cutoff frequency in Hz
             * @param q Quality factor
             * @param sampleRate Audio sample rate
             */
            void setHighPass(float freq, float q, int sampleRate) {
                float w0 = 2.0f * M_PI * freq / sampleRate;
                float cosw0 = std::cos(w0);
                float sinw0 = std::sin(w0);
                float alpha = sinw0 / (2.0f * q);

                b0 = (1.0f + cosw0) / 2.0f;
                b1 = -(1.0f + cosw0);
                b2 = (1.0f + cosw0) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha;
                normalize();
            }

            /**
             * @brief Process single sample through filter
             * @param input Input sample
             * @return Filtered output sample
             */
            float process(float input) {
                float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1;
                x1 = input;
                y2 = y1;
                y1 = output;
                return output;
            }
        };
    } // namespace DSPUtils

    // ============================================================================
    // Effect Implementations
    // ============================================================================

    // 1. Gain Effect
    class GainEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit GainEffect(float gainDb = 0.0f, QObject *parent = nullptr)
        : AudioEffect(parent), m_gainParam(gainDb, 3.0f) {
            m_parameters["gainDb"] = gainDb;
        }

        QString name() const override { return tr("Gain"); }
        QString description() const override { return tr("Adjust volume in dB"); }
        QString category() const override { return "Level"; }

        void process(EnhancedAudioBuffer &buffer, int sampleRate, EffectContext context) override {
            Q_UNUSED(context)
            if (!m_enabled) return;

            m_gainParam.process(buffer.samples(), sampleRate);
            float gainDb = m_gainParam.getCurrent();

            if (std::abs(gainDb) < 0.01f) return;  // ~0 dB change

            float linear = DSPUtils::dbToLinear(gainDb);
            buffer.applyGain(linear);
        }

        void setGainDb(float gainDb) {
            m_gainParam.setTarget(gainDb);
            setParameter("gainDb", gainDb);
        }

        float gainDb() const { return m_gainParam.getTarget(); }

        void setParameters(const QVariantMap &params) override {
            if (params.contains("gainDb")) setGainDb(params["gainDb"].toFloat());
        }

        QVariantMap parameters() const override {
            QVariantMap p = m_parameters;
            p["gainDb"] = gainDb();
            return p;
        }

        void reset() override { m_gainParam.setImmediate(m_gainParam.getTarget()); }

        std::unique_ptr<AudioEffect> clone() const override {
            auto e = std::make_unique<GainEffect>(gainDb());
            e->setEnabled(m_enabled);
            e->setParameters(m_parameters);
            return e;
        }

    private:
        SmoothedParameter m_gainParam;  ///< Smoothed gain parameter
    };

    // 2. Pan Effect
    class PanEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit PanEffect(float pan = 0.0f, QObject *parent = nullptr)
        : AudioEffect(parent), m_panParam(pan, 3.0f) {
            m_parameters["pan"] = pan;
            updateGains();
        }

        QString name() const override { return tr("Pan"); }
        QString description() const override { return tr("Stereo panning with constant power"); }
        QString category() const override { return "Level"; }

        void process(EnhancedAudioBuffer &buffer, int sampleRate, EffectContext context) override {
            Q_UNUSED(context)
            if (!m_enabled || buffer.channels() < 2) return;

            m_panParam.process(buffer.samples(), sampleRate);
            updateGains();

            float* data = buffer.data();
            int samples = buffer.samples();

            // Apply pan gains to stereo channels
            for (int i = 0; i < samples; i++) {
                data[i * 2] *= m_leftGain;      // Left channel
                data[i * 2 + 1] *= m_rightGain; // Right channel
            }
        }

        void setPan(float pan) {
            pan = std::clamp(pan, -1.0f, 1.0f);  // -1 = full left, +1 = full right
            m_panParam.setTarget(pan);
            setParameter("pan", pan);
        }

        float pan() const { return m_panParam.getTarget(); }

        void setParameters(const QVariantMap &params) override {
            if (params.contains("pan")) setPan(params["pan"].toFloat());
        }

        QVariantMap parameters() const override {
            QVariantMap p = m_parameters;
            p["pan"] = pan();
            return p;
        }

        void reset() override {
            m_panParam.setImmediate(m_panParam.getTarget());
            updateGains();
        }

        std::unique_ptr<AudioEffect> clone() const override {
            auto e = std::make_unique<PanEffect>(pan());
            e->setEnabled(m_enabled);
            return e;
        }

    private:
        /**
         * @brief Update left/right gains based on current pan position
         *
         * Uses constant-power panning law for smooth transitions.
         */
        void updateGains() {
            float p = m_panParam.getCurrent();
            float angle = (p + 1.0f) * M_PI / 4.0f;  // Map -1..1 to 0..π/2
            m_leftGain = std::cos(angle) * 1.414f;   // √2 for normalization
            m_rightGain = std::sin(angle) * 1.414f;
        }

        SmoothedParameter m_panParam;  ///< Smoothed pan position
        float m_leftGain = 0.707f;     ///< Current left channel gain
        float m_rightGain = 0.707f;    ///< Current right channel gain
    };

    // 3. Filter Effect (Unified implementation)
    class FilterEffect : public AudioEffect {
        Q_OBJECT
    public:
        enum FilterType {
            LowPass,     ///< Low-pass filter
            HighPass,    ///< High-pass filter
            BandPass,    ///< Band-pass filter
            Notch,       ///< Notch (band-reject) filter
            LowShelf,    ///< Low shelf filter
            HighShelf,   ///< High shelf filter
            Peak         ///< Peaking EQ filter
        };

        explicit FilterEffect(FilterType type = Peak, QObject *parent = nullptr)
        : AudioEffect(parent), m_type(type) {
            m_parameters["type"] = static_cast<int>(type);
            m_parameters["frequency"] = 1000.0f;
            m_parameters["q"] = 0.707f;
            m_parameters["gain"] = 0.0f;
        }

        QString name() const override {
            switch (m_type) {
                case LowPass: return tr("Low Pass");
                case HighPass: return tr("High Pass");
                case BandPass: return tr("Band Pass");
                case Notch: return tr("Notch");
                case LowShelf: return tr("Low Shelf");
                case HighShelf: return tr("High Shelf");
                case Peak: return tr("Peak EQ");
                default: return tr("Filter");
            }
        }

        QString description() const override { return tr("Frequency filter with adjustable parameters"); }
        QString category() const override { return "EQ/Filter"; }

        void process(EnhancedAudioBuffer &buffer, int sampleRate, EffectContext context) override {
            Q_UNUSED(context)
            if (!m_enabled) return;

            updateCoefficients(sampleRate);

            int channels = buffer.channels();
            if (static_cast<int>(m_states.size()) != channels) {
                m_states.resize(channels);
            }

            float* data = buffer.data();
            int samples = buffer.samples();

            // Process each channel independently
            for (int ch = 0; ch < channels; ch++) {
                auto& coeffs = m_coeffs[ch];
                auto& state = m_states[ch];

                for (int i = 0; i < samples; i++) {
                    float input = data[i * channels + ch];
                    float output = coeffs.b0 * input + coeffs.b1 * state.x1 + coeffs.b2 * state.x2
                    - coeffs.a1 * state.y1 - coeffs.a2 * state.y2;
                    state.x2 = state.x1;
                    state.x1 = input;
                    state.y2 = state.y1;
                    state.y1 = output;
                    data[i * channels + ch] = output;
                }
            }
        }

        void setType(FilterType type) {
            m_type = type;
            setParameter("type", static_cast<int>(type));
            m_coeffs.clear(); // Force recalculation
        }

        void setFrequency(float freq) {
            m_frequency = std::max(20.0f, std::min(freq, 20000.0f));
            setParameter("frequency", m_frequency);
            m_coeffs.clear();
        }

        void setQ(float q) {
            m_q = std::max(0.1f, std::min(q, 10.0f));
            setParameter("q", m_q);
            m_coeffs.clear();
        }

        void setGain(float gainDb) {
            m_gainDb = std::clamp(gainDb, -24.0f, 24.0f);
            setParameter("gain", m_gainDb);
            m_coeffs.clear();
        }

        FilterType type() const { return m_type; }
        float frequency() const { return m_frequency; }
        float q() const { return m_q; }
        float gain() const { return m_gainDb; }

        void setParameters(const QVariantMap &params) override {
            if (params.contains("type")) setType(static_cast<FilterType>(params["type"].toInt()));
            if (params.contains("frequency")) setFrequency(params["frequency"].toFloat());
            if (params.contains("q")) setQ(params["q"].toFloat());
            if (params.contains("gain")) setGain(params["gain"].toFloat());
        }

        QVariantMap parameters() const override {
            QVariantMap p = m_parameters;
            p["type"] = static_cast<int>(m_type);
            p["frequency"] = m_frequency;
            p["q"] = m_q;
            p["gain"] = m_gainDb;
            return p;
        }

        void reset() override {
            for (auto& state : m_states) {
                state = FilterState{};
            }
            m_coeffs.clear();
        }

        std::unique_ptr<AudioEffect> clone() const override {
            auto e = std::make_unique<FilterEffect>(m_type);
            e->setFrequency(m_frequency);
            e->setQ(m_q);
            e->setGain(m_gainDb);
            e->setEnabled(m_enabled);
            return e;
        }

    private:
        struct FilterState {
            float x1 = 0.0f, x2 = 0.0f;  ///< Input history
            float y1 = 0.0f, y2 = 0.0f;  ///< Output history
        };

        void updateCoefficients(int sampleRate) {
            if (sampleRate == m_lastSampleRate && !m_coeffs.empty()) return;
            m_lastSampleRate = sampleRate;

            // Create coefficients for one channel
            DSPUtils::BiquadCoeffs baseCoeffs;

            switch (m_type) {
                case Peak:
                    baseCoeffs.setPeaking(m_frequency, m_q, m_gainDb, sampleRate);
                    break;
                case LowPass:
                    baseCoeffs.setLowPass(m_frequency, m_q, sampleRate);
                    break;
                case HighPass:
                    baseCoeffs.setHighPass(m_frequency, m_q, sampleRate);
                    break;
                default:
                    // For now, default to low-pass for unimplemented types
                    baseCoeffs.setLowPass(m_frequency, m_q, sampleRate);
                    break;
            }

            // Ensure we have coefficients for all channels
            m_coeffs.resize(1, baseCoeffs); // Same coefficients for all channels
        }

        FilterType m_type = Peak;                      ///< Current filter type
        float m_frequency = 1000.0f;                   ///< Center/cutoff frequency
        float m_q = 0.707f;                           ///< Quality factor
        float m_gainDb = 0.0f;                        ///< Gain in dB (for peaking/shelf)
        std::vector<DSPUtils::BiquadCoeffs> m_coeffs; ///< Filter coefficients per channel
        std::vector<FilterState> m_states;            ///< Filter state per channel
        int m_lastSampleRate = 0;                     ///< Cached sample rate
    };

    // 4. Compressor Effect
    class CompressorEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit CompressorEffect(QObject *parent = nullptr) : AudioEffect(parent) {
            m_parameters["threshold"] = -20.0f;
            m_parameters["ratio"] = 4.0f;
            m_parameters["attack"] = 10.0f;
            m_parameters["release"] = 100.0f;
            m_parameters["makeupGain"] = 0.0f;
            updateCoeffs(48000);
        }

        QString name() const override { return tr("Compressor"); }
        QString description() const override { return tr("Dynamic range compression with adjustable parameters"); }
        QString category() const override { return "Dynamics"; }

        void process(EnhancedAudioBuffer &buffer, int sampleRate, EffectContext context) override {
            Q_UNUSED(context)
            if (!m_enabled) return;

            updateCoeffs(sampleRate);

            float makeupGain = DSPUtils::dbToLinear(m_makeupGainDb);
            float thresholdLinear = DSPUtils::dbToLinear(m_thresholdDb);

            float* data = buffer.data();
            int totalSamples = buffer.totalSamples();

            for (int i = 0; i < totalSamples; i++) {
                float input = data[i];
                float inputLevel = std::abs(input);

                // Envelope follower with different attack/release times
                if (inputLevel > m_envelope) {
                    m_envelope = m_attackCoeff * (m_envelope - inputLevel) + inputLevel;
                } else {
                    m_envelope = m_releaseCoeff * (m_envelope - inputLevel) + inputLevel;
                }

                // Calculate gain reduction based on threshold and ratio
                float gain = 1.0f;
                if (m_envelope > thresholdLinear) {
                    float dbOver = DSPUtils::linearToDb(m_envelope / thresholdLinear);
                    float dbGainReduction = dbOver * (1.0f - 1.0f / m_ratio);
                    gain = DSPUtils::dbToLinear(-dbGainReduction);
                }

                data[i] = input * gain * makeupGain;
            }
        }

        void setThreshold(float db) { m_thresholdDb = db; setParameter("threshold", db); }
        void setRatio(float ratio) { m_ratio = std::max(1.0f, ratio); setParameter("ratio", m_ratio); }
        void setAttack(float ms) { m_attackMs = std::max(0.1f, ms); m_coeffsValid = false; setParameter("attack", m_attackMs); }
        void setRelease(float ms) { m_releaseMs = std::max(1.0f, ms); m_coeffsValid = false; setParameter("release", m_releaseMs); }
        void setMakeupGain(float db) { m_makeupGainDb = db; setParameter("makeupGain", db); }

        void setParameters(const QVariantMap &params) override {
            if (params.contains("threshold")) setThreshold(params["threshold"].toFloat());
            if (params.contains("ratio")) setRatio(params["ratio"].toFloat());
            if (params.contains("attack")) setAttack(params["attack"].toFloat());
            if (params.contains("release")) setRelease(params["release"].toFloat());
            if (params.contains("makeupGain")) setMakeupGain(params["makeupGain"].toFloat());
        }

        QVariantMap parameters() const override {
            QVariantMap p = m_parameters;
            p["threshold"] = m_thresholdDb;
            p["ratio"] = m_ratio;
            p["attack"] = m_attackMs;
            p["release"] = m_releaseMs;
            p["makeupGain"] = m_makeupGainDb;
            return p;
        }

        void reset() override { m_envelope = 0.0f; m_coeffsValid = false; }

        std::unique_ptr<AudioEffect> clone() const override {
            auto e = std::make_unique<CompressorEffect>();
            e->setParameters(parameters());
            e->setEnabled(m_enabled);
            return e;
        }

    private:
        void updateCoeffs(int sampleRate) {
            if (m_coeffsValid && sampleRate == m_lastSampleRate) return;

            m_lastSampleRate = sampleRate;

            // Convert milliseconds to samples and calculate smoothing coefficients
            float attackSamples = m_attackMs * sampleRate / 1000.0f;
            float releaseSamples = m_releaseMs * sampleRate / 1000.0f;

            m_attackCoeff = std::exp(-1.0f / std::max(1.0f, attackSamples));
            m_releaseCoeff = std::exp(-1.0f / std::max(1.0f, releaseSamples));
            m_coeffsValid = true;
        }

        float m_thresholdDb = -20.0f;     ///< Compression threshold in dB
        float m_ratio = 4.0f;            ///< Compression ratio (4:1, etc.)
        float m_makeupGainDb = 0.0f;     ///< Output gain compensation
        float m_attackMs = 10.0f;        ///< Attack time in milliseconds
        float m_releaseMs = 100.0f;      ///< Release time in milliseconds

        float m_envelope = 0.0f;         ///< Current envelope follower value
        float m_attackCoeff = 0.0f;      ///< Pre-calculated attack coefficient
        float m_releaseCoeff = 0.0f;     ///< Pre-calculated release coefficient
        bool m_coeffsValid = false;      ///< Flag indicating coefficients are up-to-date
        int m_lastSampleRate = 0;        ///< Last sample rate used for coefficient calculation
    };

    // 5. Normalize Effect (Offline only)
    class NormalizeEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit NormalizeEffect(float targetDb = -1.0f, QObject *parent = nullptr)
        : AudioEffect(parent), m_targetDb(targetDb) {
            m_parameters["targetDb"] = targetDb;
        }

        QString name() const override { return tr("Normalize"); }
        QString description() const override { return tr("Normalize audio to target dB level"); }
        QString category() const override { return "Level"; }

        bool supportsRealtime() const override { return false; }
        bool supportsPreview() const override { return true; }

        void process(EnhancedAudioBuffer &buffer, int sampleRate, EffectContext context) override {
            Q_UNUSED(sampleRate) Q_UNUSED(context)
            if (!m_enabled) return;

            float peak = buffer.peakLevel();
            if (peak < 0.00001f) return;

            float targetLinear = DSPUtils::dbToLinear(m_targetDb);
            float gain = targetLinear / peak;
            buffer.applyGain(gain);
        }

        void processOffline(EnhancedAudioBuffer &buffer, const Selection& selection, int sampleRate) override {
            if (selection.isEmpty()) {
                process(buffer, sampleRate, EffectContext::Offline);
                return;
            }

            // Find peak in selection only
            float peak = 0.0f;
            qint64 start = selection.start;
            qint64 end = std::min(selection.end, buffer.frames());
            int channels = buffer.channels();

            for (qint64 f = start; f < end; f++) {
                for (int ch = 0; ch < channels; ch++) {
                    peak = std::max(peak, std::abs(buffer.sampleAt(ch, f)));
                }
            }

            if (peak < 0.00001f) return;

            float targetLinear = DSPUtils::dbToLinear(m_targetDb);
            float gain = targetLinear / peak;

            // Apply gain to selection only
            for (qint64 f = start; f < end; f++) {
                for (int ch = 0; ch < channels; ch++) {
                    buffer.setSample(ch, f, buffer.sampleAt(ch, f) * gain);
                }
            }
        }

        void setTargetDb(float db) { m_targetDb = db; setParameter("targetDb", db); }

        void setParameters(const QVariantMap &params) override {
            if (params.contains("targetDb")) setTargetDb(params["targetDb"].toFloat());
        }

        QVariantMap parameters() const override {
            QVariantMap p = m_parameters;
            p["targetDb"] = m_targetDb;
            return p;
        }

        std::unique_ptr<AudioEffect> clone() const override {
            auto e = std::make_unique<NormalizeEffect>(m_targetDb);
            e->setEnabled(m_enabled);
            return e;
        }

    private:
        float m_targetDb = -1.0f;  ///< Target peak level in dBFS
    };

    // 6. Fade Effect (Offline only)
    class FadeEffect : public AudioEffect {
        Q_OBJECT
    public:
        enum FadeType {
            FadeIn,     ///< Fade from silence to full volume
            FadeOut,    ///< Fade from full volume to silence
            FadeCustom  ///< Custom fade shape (reserved for future)
        };

        explicit FadeEffect(FadeType type = FadeIn, QObject *parent = nullptr)
        : AudioEffect(parent), m_type(type) {
            m_parameters["type"] = type;
            m_parameters["curve"] = 1.0f;
        }

        QString name() const override {
            switch (m_type) {
                case FadeIn: return tr("Fade In");
                case FadeOut: return tr("Fade Out");
                default: return tr("Fade");
            }
        }

        QString description() const override {
            switch (m_type) {
                case FadeIn: return tr("Fade in from silence");
                case FadeOut: return tr("Fade out to silence");
                default: return tr("Apply fade with adjustable curve");
            }
        }

        QString category() const override { return "Level"; }

        bool supportsRealtime() const override { return false; }
        bool supportsPreview() const override { return true; }

        void process(EnhancedAudioBuffer &buffer, int sampleRate, EffectContext context) override {
            Q_UNUSED(sampleRate) Q_UNUSED(context)
            if (!m_enabled) return;

            int samples = buffer.samples();
            int channels = buffer.channels();

            for (int f = 0; f < samples; f++) {
                float t = f / static_cast<float>(samples);
                float factor = (m_type == FadeIn) ? t : (1.0f - t);
                factor = std::pow(factor, m_curve);

                for (int ch = 0; ch < channels; ch++) {
                    buffer.setSample(ch, f, buffer.sampleAt(ch, f) * factor);
                }
            }
        }

        void processOffline(EnhancedAudioBuffer &buffer, const Selection& selection, int sampleRate) override {
            if (selection.isEmpty()) {
                process(buffer, sampleRate, EffectContext::Offline);
                return;
            }

            qint64 start = selection.start;
            qint64 len = selection.length();
            qint64 end = std::min(selection.end, buffer.frames());
            int channels = buffer.channels();

            for (qint64 f = start; f < end; f++) {
                float t = (f - start) / static_cast<float>(len);
                float factor = (m_type == FadeIn) ? t : (1.0f - t);
                factor = std::pow(factor, m_curve);

                for (int ch = 0; ch < channels; ch++) {
                    buffer.setSample(ch, f, buffer.sampleAt(ch, f) * factor);
                }
            }
        }

        void setFadeType(FadeType type) { m_type = type; setParameter("type", type); }
        void setCurve(float curve) { m_curve = std::max(0.1f, std::min(curve, 10.0f)); setParameter("curve", curve); }

        void setParameters(const QVariantMap &params) override {
            if (params.contains("type")) setFadeType(static_cast<FadeType>(params["type"].toInt()));
            if (params.contains("curve")) setCurve(params["curve"].toFloat());
        }

        QVariantMap parameters() const override {
            QVariantMap p = m_parameters;
            p["type"] = m_type;
            p["curve"] = m_curve;
            return p;
        }

        std::unique_ptr<AudioEffect> clone() const override {
            auto e = std::make_unique<FadeEffect>(m_type);
            e->setCurve(m_curve);
            e->setEnabled(m_enabled);
            return e;
        }

    private:
        FadeType m_type;          ///< Type of fade (in/out)
        float m_curve = 1.0f;     ///< Fade curve exponent (1.0 = linear)
    };

    // 7. Reverse Effect (Offline only)
    class ReverseEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit ReverseEffect(QObject *parent = nullptr) : AudioEffect(parent) {}

        QString name() const override { return tr("Reverse"); }
        QString description() const override { return tr("Reverse audio playback direction"); }
        QString category() const override { return "Time"; }

        bool supportsRealtime() const override { return false; }
        bool supportsPreview() const override { return false; }

        void process(EnhancedAudioBuffer &buffer, int sampleRate, EffectContext context) override {
            Q_UNUSED(sampleRate) Q_UNUSED(context)
            if (!m_enabled) return;

            int samples = buffer.samples();
            int channels = buffer.channels();
            int mid = samples / 2;

            // Reverse by swapping samples symmetrically around the center
            for (int i = 0; i < mid; i++) {
                int f1 = i;
                int f2 = samples - 1 - i;

                for (int ch = 0; ch < channels; ch++) {
                    float temp = buffer.sampleAt(ch, f1);
                    buffer.setSample(ch, f1, buffer.sampleAt(ch, f2));
                    buffer.setSample(ch, f2, temp);
                }
            }
        }

        void processOffline(EnhancedAudioBuffer &buffer, const Selection& selection, int sampleRate) override {
            if (selection.isEmpty()) {
                process(buffer, sampleRate, EffectContext::Offline);
                return;
            }

            qint64 start = selection.start;
            qint64 len = selection.length();
            qint64 end = std::min(selection.end, buffer.frames());
            int channels = buffer.channels();
            int mid = len / 2;

            for (int i = 0; i < mid; i++) {
                int f1 = static_cast<int>(start + i);
                int f2 = static_cast<int>(end - 1 - i);

                for (int ch = 0; ch < channels; ch++) {
                    float temp = buffer.sampleAt(ch, f1);
                    buffer.setSample(ch, f1, buffer.sampleAt(ch, f2));
                    buffer.setSample(ch, f2, temp);
                }
            }
        }

        std::unique_ptr<AudioEffect> clone() const override {
            return std::make_unique<ReverseEffect>();
        }
    };

    // 8. Silence Effect
    class SilenceEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit SilenceEffect(QObject *parent = nullptr) : AudioEffect(parent) {}

        QString name() const override { return tr("Silence"); }
        QString description() const override { return tr("Replace audio with silence"); }
        QString category() const override { return "Level"; }

        void process(EnhancedAudioBuffer &buffer, int sampleRate, EffectContext context) override {
            Q_UNUSED(sampleRate) Q_UNUSED(context)
            if (m_enabled) buffer.zero();
        }

        void processOffline(EnhancedAudioBuffer &buffer, const Selection& selection, int sampleRate) override {
            if (selection.isEmpty()) {
                process(buffer, sampleRate, EffectContext::Offline);
                return;
            }

            qint64 start = selection.start;
            qint64 end = std::min(selection.end, buffer.frames());
            int channels = buffer.channels();

            for (qint64 f = start; f < end; f++) {
                for (int ch = 0; ch < channels; ch++) {
                    buffer.setSample(ch, f, 0.0f);
                }
            }
        }

        std::unique_ptr<AudioEffect> clone() const override {
            return std::make_unique<SilenceEffect>();
        }
    };

    // ============================================================================
    // Effect Chain Management
    // ============================================================================

    /**
     * @brief Manages ordered collection of audio effects
     *
     * Provides sequential processing of effects with automatic latency compensation.
     * Supports real-time and offline processing modes.
     */
    class EffectChain : public QObject {
        Q_OBJECT
    public:
        explicit EffectChain(QObject *parent = nullptr) : QObject(parent) {}

        /**
         * @brief Add effect to end of chain
         * @param effect Shared pointer to effect instance
         */
        void addEffect(std::shared_ptr<AudioEffect> effect) {
            m_effects.append(effect);
            emit effectAdded(m_effects.size() - 1);
            emit chainChanged();
        }

        /**
         * @brief Remove effect at specified index
         * @param index Index of effect to remove
         */
        void removeEffect(int index) {
            if (index >= 0 && index < m_effects.size()) {
                m_effects.removeAt(index);
                emit effectRemoved(index);
                emit chainChanged();
            }
        }

        /**
         * @brief Move effect to different position in chain
         * @param from Original index
         * @param to New index
         */
        void moveEffect(int from, int to) {
            if (from >= 0 && from < m_effects.size() && to >= 0 && to < m_effects.size()) {
                m_effects.move(from, to);
                emit effectMoved(from, to);
                emit chainChanged();
            }
        }

        /**
         * @brief Remove all effects from chain
         */
        void clear() {
            m_effects.clear();
            emit chainChanged();
        }

        int count() const { return m_effects.size(); }
        bool isEmpty() const { return m_effects.isEmpty(); }

        /**
         * @brief Get effect at specified index
         * @param index Effect index
         * @return Shared pointer to effect, or nullptr if invalid
         */
        std::shared_ptr<AudioEffect> effectAt(int index) const {
            return (index >= 0 && index < m_effects.size()) ? m_effects[index] : nullptr;
        }

        /**
         * @brief Process audio buffer through effect chain
         * @param buffer Audio buffer to process
         * @param sampleRate Audio sample rate
         * @param context Processing context
         */
        void process(EnhancedAudioBuffer &buffer, int sampleRate, EffectContext context = EffectContext::Realtime) {
            for (auto& effect : m_effects) {
                if (effect && effect->isEnabled()) {
                    effect->process(buffer, sampleRate, context);
                }
            }
        }

        /**
         * @brief Process selection through effect chain (offline mode)
         * @param buffer Audio buffer containing selection
         * @param selection Time range to process
         * @param sampleRate Audio sample rate
         */
        void processOffline(EnhancedAudioBuffer &buffer, const Selection& selection, int sampleRate) {
            for (auto& effect : m_effects) {
                if (effect && effect->isEnabled()) {
                    effect->processOffline(buffer, selection, sampleRate);
                }
            }
        }

        /**
         * @brief Calculate total latency of enabled effects
         * @return Total latency in samples
         */
        int totalLatency() const {
            int latency = 0;
            for (auto& effect : m_effects) {
                if (effect && effect->isEnabled()) {
                    latency += effect->latencySamples();
                }
            }
            return latency;
        }

        /**
         * @brief Reset all effects in chain (clear buffers, etc.)
         */
        void resetAll() {
            for (auto& effect : m_effects) {
                if (effect) effect->reset();
            }
        }

        /**
         * @brief Serialize effect chain state to variant map
         * @return Serialized chain data
         */
        QVariantMap serialize() const {
            QVariantMap result;
            QVariantList effects;
            for (auto& effect : m_effects) {
                if (effect) {
                    QVariantMap e;
                    e["name"] = effect->name();
                    e["enabled"] = effect->isEnabled();
                    e["parameters"] = effect->parameters();
                    effects.append(e);
                }
            }
            result["effects"] = effects;
            return result;
        }

    signals:
        void effectAdded(int index);
        void effectRemoved(int index);
        void effectMoved(int from, int to);
        void chainChanged();

    private:
        QList<std::shared_ptr<AudioEffect>> m_effects;  ///< Ordered list of effects
    };

} // namespace Aegis

// Register types for Qt's meta-object system
Q_DECLARE_METATYPE(Aegis::EffectContext)
Q_DECLARE_METATYPE(Aegis::FilterEffect::FilterType)
Q_DECLARE_METATYPE(Aegis::FadeEffect::FadeType)
