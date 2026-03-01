// audio_effects.h - Complete Audio Effects Processing System
// Provides modular audio effects with real-time and offline processing support.
// Includes enhanced buffer management, DSP utilities, and effect chain management.

#pragma once

#include "audio_daw.h"
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
     * @brief Smoothly interpolates parameter changes to prevent audio artifacts.
     *
     * Uses exponential smoothing to gradually change parameter values over time.
     * Essential for real-time parameter automation without clicks or pops.
     */
    class SmoothedParameter {
    public:
        explicit SmoothedParameter(float initial = 0.0f, float smoothingTimeMs = 5.0f)
            : m_target(initial), m_current(initial), m_smoothingTimeMs(smoothingTimeMs) {}

        void setTarget(float value)    { m_target.store(value); }
        void setImmediate(float value) { m_target.store(value); m_current = value; }

        float getCurrent() const { return m_current; }
        float getTarget()  const { return m_target.load(); }

        bool isSettled() const { return std::abs(m_current - m_target.load()) < 0.0001f; }

        void process(int samples, int sampleRate) {
            float target = m_target.load();
            if (std::abs(m_current - target) < 0.00001f) return;

            if (m_smoothingCoeff == 0.0f) {
                float samplesForTime = m_smoothingTimeMs * (sampleRate / 1000.0f);
                m_smoothingCoeff = std::exp(-1.0f / samplesForTime);
            }

            float coeff = std::pow(m_smoothingCoeff, samples);
            m_current = target + (m_current - target) * coeff;
        }

    private:
        std::atomic<float> m_target;
        float m_current       = 0.0f;
        float m_smoothingCoeff = 0.0f;
        float m_smoothingTimeMs;
    };

    // ============================================================================
    // Enhanced Audio Buffer with SIMD Optimization
    // ============================================================================

    /**
     * @brief High-performance audio buffer with SSE2 optimizations.
     *
     * Stores interleaved multi-channel PCM data. All indices are frame-based
     * unless explicitly named "sample" (a sample = one channel value).
     */
    class EnhancedAudioBuffer {
    public:
        EnhancedAudioBuffer() = default;
        explicit EnhancedAudioBuffer(qint64 frames, int channels = 2) { resize(frames, channels); }

        // ---- Data access ----
        float*       data()       { return m_data.data(); }
        const float* data() const { return m_data.data(); }

        float* channelData(int channel) {
            return (channel >= 0 && channel < m_channels) ? m_data.data() + channel : nullptr;
        }
        const float* channelData(int channel) const {
            return (channel >= 0 && channel < m_channels) ? m_data.data() + channel : nullptr;
        }

        float sampleAt(int channel, qint64 frame) const {
            if (channel < 0 || channel >= m_channels || frame < 0 || frame >= m_frames)
                return 0.0f;
            return m_data[frame * m_channels + channel];
        }
        void setSample(int channel, qint64 frame, float value) {
            if (channel < 0 || channel >= m_channels || frame < 0 || frame >= m_frames)
                return;
            m_data[frame * m_channels + channel] = value;
        }

        // ---- Info ----
        int    samples()      const { return static_cast<int>(m_frames); }
        qint64 frames()       const { return m_frames; }
        int    channels()     const { return m_channels; }
        qint64 totalSamples() const { return m_frames * m_channels; }
        bool   isEmpty()      const { return m_frames == 0; }

        // ---- Resize / clear ----
        void resize(qint64 frames, int channels) {
            m_frames   = frames;
            m_channels = channels;
            m_data.resize(static_cast<size_t>(frames) * channels, 0.0f);
        }
        void clear() { m_data.clear(); m_frames = 0; }
        void fill(float value) { std::fill(m_data.begin(), m_data.end(), value); }
        void zero() { fill(0.0f); }

        // ---- Buffer operations ----

        void copyFrom(const EnhancedAudioBuffer& src, int srcOffset, int dstOffset, int count) {
            int srcStart = srcOffset * src.m_channels;
            int dstStart = dstOffset * m_channels;
            qint64 maxCopy = std::min<qint64>(src.totalSamples() - srcStart,
                                               totalSamples() - dstStart);
            int copyCount = std::min(count * src.m_channels, static_cast<int>(maxCopy));
            if (copyCount > 0)
                std::memcpy(m_data.data() + dstStart, src.data() + srcStart,
                            static_cast<size_t>(copyCount) * sizeof(float));
        }

        EnhancedAudioBuffer slice(qint64 startFrame, qint64 count) const {
            if (startFrame < 0) startFrame = 0;
            if (startFrame >= m_frames) return EnhancedAudioBuffer();
            if (startFrame + count > m_frames) count = m_frames - startFrame;
            EnhancedAudioBuffer result(count, m_channels);
            result.copyFrom(*this, static_cast<int>(startFrame), 0, static_cast<int>(count));
            return result;
        }

        void insert(qint64 pos, const EnhancedAudioBuffer& other) {
            if (other.isEmpty()) return;
            if (pos < 0) pos = 0;
            if (pos > m_frames) pos = m_frames;

            qint64 oldFrames = m_frames;
            resize(m_frames + other.m_frames, m_channels);

            if (pos < oldFrames)
                std::memmove(m_data.data() + (pos + other.m_frames) * m_channels,
                             m_data.data() + pos * m_channels,
                             static_cast<size_t>(oldFrames - pos) * m_channels * sizeof(float));

            std::memcpy(m_data.data() + pos * m_channels,
                        other.m_data.data(),
                        other.m_data.size() * sizeof(float));
        }

        void remove(qint64 start, qint64 count) {
            if (start < 0 || start >= m_frames || count <= 0) return;
            if (start + count > m_frames) count = m_frames - start;
            qint64 remaining = m_frames - start - count;
            if (remaining > 0)
                std::memmove(m_data.data() + start * m_channels,
                             m_data.data() + (start + count) * m_channels,
                             static_cast<size_t>(remaining) * m_channels * sizeof(float));
            resize(m_frames - count, m_channels);
        }

        void append(const EnhancedAudioBuffer& other) {
            if (other.isEmpty()) return;
            if (isEmpty()) { *this = other; return; }
            insert(m_frames, other);
        }

        // ---- Mixing (SIMD-optimized) ----

        void mix(const EnhancedAudioBuffer& other, float gain = 1.0f, int dstOffset = 0) {
            int count = static_cast<int>(
                std::min<qint64>(other.totalSamples(),
                                 totalSamples() - static_cast<qint64>(dstOffset) * m_channels));
            if (count <= 0) return;

            const float* src = other.data();
            float*       dst = m_data.data() + dstOffset * m_channels;

            int i = 0;
#ifdef __SSE2__
            __m128 gainVec = _mm_set1_ps(gain);
            for (; i <= count - 4; i += 4) {
                __m128 s = _mm_loadu_ps(src + i);
                __m128 d = _mm_loadu_ps(dst + i);
                _mm_storeu_ps(dst + i, _mm_add_ps(d, _mm_mul_ps(s, gainVec)));
            }
#endif
            for (; i < count; i++) dst[i] += src[i] * gain;
        }

        void applyGain(float gain) {
            if (gain == 1.0f) return;
            int total = static_cast<int>(totalSamples());
            int i = 0;
#ifdef __SSE2__
            __m128 g = _mm_set1_ps(gain);
            for (; i <= total - 4; i += 4) {
                __m128 v = _mm_loadu_ps(m_data.data() + i);
                _mm_storeu_ps(m_data.data() + i, _mm_mul_ps(v, g));
            }
#endif
            for (; i < total; i++) m_data[i] *= gain;
        }

        void applyGainRamp(qint64 startFrame, qint64 endFrame, float startGain, float endGain) {
            if (startFrame < 0) startFrame = 0;
            if (endFrame > m_frames) endFrame = m_frames;
            qint64 len = endFrame - startFrame;
            if (len <= 0) return;
            for (qint64 f = 0; f < len; f++) {
                float t    = f / static_cast<float>(len);
                float gain = startGain + (endGain - startGain) * t;
                for (int ch = 0; ch < m_channels; ch++)
                    m_data[(startFrame + f) * m_channels + ch] *= gain;
            }
        }

        // ---- Analysis ----

        float peakLevel(int channel = -1) const {
            if (m_data.empty()) return 0.0f;
            float peak = 0.0f;
            if (channel < 0) {
                for (float s : m_data) peak = std::max(peak, std::abs(s));
            } else if (channel < m_channels) {
                for (qint64 i = 0; i < m_frames; ++i)
                    peak = std::max(peak, std::abs(sampleAt(channel, i)));
            }
            return peak;
        }

        float rmsLevel(int channel = -1) const {
            if (m_frames == 0) return 0.0f;
            double sum = 0.0;
            if (channel < 0) {
                for (float s : m_data) sum += s * s;
                return std::sqrt(static_cast<float>(sum / m_data.size()));
            } else if (channel < m_channels) {
                for (qint64 i = 0; i < m_frames; ++i) {
                    float s = sampleAt(channel, i);
                    sum += s * s;
                }
                return std::sqrt(static_cast<float>(sum / m_frames));
            }
            return 0.0f;
        }

        float dcOffset(int channel) const {
            if (m_frames == 0 || channel < 0 || channel >= m_channels) return 0.0f;
            double sum = 0.0;
            for (qint64 i = 0; i < m_frames; ++i) sum += sampleAt(channel, i);
            return static_cast<float>(sum / m_frames);
        }

    private:
        std::vector<float> m_data;
        qint64 m_frames   = 0;
        int    m_channels = 2;
    };

    // ============================================================================
    // Effect Context
    // ============================================================================

    enum class EffectContext {
        Realtime,   ///< Low latency, no lookahead, CPU-optimized
        Offline,    ///< Full file access, lookahead allowed, highest quality
        Preview     ///< Fast, approximate processing for UI feedback
    };

    // ============================================================================
    // AudioEffect Base Class
    // ============================================================================

    /**
     * @brief Abstract base for all audio effects.
     *
     * Subclasses implement process() for real-time operation.
     * Override processOffline() if selection-based processing differs.
     */
    class AudioEffect : public QObject {
        Q_OBJECT
    public:
        explicit AudioEffect(QObject* parent = nullptr) : QObject(parent) {}
        virtual ~AudioEffect() = default;

        // Metadata
        virtual QString name()        const = 0;
        virtual QString description() const = 0;
        virtual QString category()    const { return "General"; }

        // Context support flags
        virtual bool supportsRealtime() const { return true; }
        virtual bool supportsOffline()  const { return true; }
        virtual bool supportsPreview()  const { return true; }

        // Processing
        virtual void process(EnhancedAudioBuffer& buffer, int sampleRate,
                             EffectContext context = EffectContext::Realtime) = 0;

        virtual void processOffline(EnhancedAudioBuffer& buffer,
                                    const Selection& selection, int sampleRate) {
            if (selection.isEmpty() || !selection.isValid()) {
                process(buffer, sampleRate, EffectContext::Offline);
                return;
            }
            EnhancedAudioBuffer selected = buffer.slice(selection.start, selection.length());
            process(selected, sampleRate, EffectContext::Offline);
            buffer.copyFrom(selected, 0,
                            static_cast<int>(selection.start),
                            static_cast<int>(selection.length()));
        }

        // State
        bool isEnabled() const { return m_enabled; }
        void setEnabled(bool enabled) {
            m_enabled = enabled;
            emit enabledChanged(enabled);
        }

        // Latency info
        virtual int latencySamples() const { return 0; }
        virtual int tailSamples()    const { return 0; }

        // Parameters
        virtual void setParameter(const QString& name, const QVariant& value) {
            QWriteLocker lock(&m_paramLock);
            m_parameters[name] = value;
            emit parameterChanged(name, value);
        }
        virtual QVariant getParameter(const QString& name) const {
            QReadLocker lock(&m_paramLock);
            return m_parameters.value(name);
        }
        virtual void setParameters(const QVariantMap& params) {
            for (auto it = params.begin(); it != params.end(); ++it)
                setParameter(it.key(), it.value());
        }
        virtual QVariantMap parameters() const {
            QReadLocker lock(&m_paramLock);
            return m_parameters;
        }

        virtual void reset() {}

        virtual std::unique_ptr<AudioEffect> clone() const = 0;

    signals:
        void enabledChanged(bool enabled);
        void parameterChanged(const QString& name, const QVariant& value);

    protected:
        std::atomic<bool>   m_enabled{true};
        mutable QReadWriteLock m_paramLock;
        QVariantMap         m_parameters;
    };

    // ============================================================================
    // DSP Utilities
    // ============================================================================

    namespace DSPUtils {

        inline float dbToLinear(float db)      { return std::pow(10.0f, db / 20.0f); }
        inline float linearToDb(float linear)  {
            return linear > 0.00001f ? 20.0f * std::log10(linear) : -96.0f;
        }

        /**
         * @brief Biquad filter with Direct Form I structure.
         *
         * Coefficients are normalized (a0 = 1) after calling any set*() method.
         */
        struct BiquadCoeffs {
            float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
            float x1 = 0.0f, x2 = 0.0f;
            float y1 = 0.0f, y2 = 0.0f;

            void normalize() {
                b0 /= a0; b1 /= a0; b2 /= a0;
                a1 /= a0; a2 /= a0;
                a0 = 1.0f;
            }

            void setPeaking(float freq, float q, float gainDb, int sampleRate) {
                float w0    = 2.0f * static_cast<float>(M_PI) * freq / sampleRate;
                float cosw0 = std::cos(w0);
                float sinw0 = std::sin(w0);
                float alpha = sinw0 / (2.0f * q);
                float A     = std::pow(10.0f, gainDb / 40.0f);
                b0 = 1.0f + alpha * A;
                b1 = -2.0f * cosw0;
                b2 = 1.0f - alpha * A;
                a0 = 1.0f + alpha / A;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha / A;
                normalize();
            }

            void setLowPass(float freq, float q, int sampleRate) {
                float w0    = 2.0f * static_cast<float>(M_PI) * freq / sampleRate;
                float cosw0 = std::cos(w0);
                float sinw0 = std::sin(w0);
                float alpha = sinw0 / (2.0f * q);
                b0 = (1.0f - cosw0) / 2.0f;
                b1 =  1.0f - cosw0;
                b2 = (1.0f - cosw0) / 2.0f;
                a0 =  1.0f + alpha;
                a1 = -2.0f * cosw0;
                a2 =  1.0f - alpha;
                normalize();
            }

            void setHighPass(float freq, float q, int sampleRate) {
                float w0    = 2.0f * static_cast<float>(M_PI) * freq / sampleRate;
                float cosw0 = std::cos(w0);
                float sinw0 = std::sin(w0);
                float alpha = sinw0 / (2.0f * q);
                b0 =  (1.0f + cosw0) / 2.0f;
                b1 = -(1.0f + cosw0);
                b2 =  (1.0f + cosw0) / 2.0f;
                a0 =   1.0f + alpha;
                a1 =  -2.0f * cosw0;
                a2 =   1.0f - alpha;
                normalize();
            }

            float process(float input) {
                float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = input;
                y2 = y1; y1 = output;
                return output;
            }
        };

    } // namespace DSPUtils

    // ============================================================================
    // 1. Gain Effect
    // ============================================================================

    class GainEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit GainEffect(float gainDb = 0.0f, QObject* parent = nullptr)
            : AudioEffect(parent), m_gainParam(gainDb, 3.0f) {
            m_parameters["gainDb"] = gainDb;
        }

        QString name()        const override { return tr("Gain"); }
        QString description() const override { return tr("Adjust volume in dB"); }
        QString category()    const override { return "Level"; }

        void process(EnhancedAudioBuffer& buffer, int sampleRate,
                     EffectContext context = EffectContext::Realtime) override {
            Q_UNUSED(context)
            if (!m_enabled) return;
            m_gainParam.process(buffer.samples(), sampleRate);
            float gainDb = m_gainParam.getCurrent();
            if (std::abs(gainDb) < 0.01f) return;
            buffer.applyGain(DSPUtils::dbToLinear(gainDb));
        }

        void setGainDb(float gainDb) {
            m_gainParam.setTarget(gainDb);
            setParameter("gainDb", gainDb);
        }
        float gainDb() const { return m_gainParam.getTarget(); }

        void setParameters(const QVariantMap& params) override {
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
            return e;
        }

    private:
        SmoothedParameter m_gainParam;
    };

    // ============================================================================
    // 2. Pan Effect
    // ============================================================================

    class PanEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit PanEffect(float pan = 0.0f, QObject* parent = nullptr)
            : AudioEffect(parent), m_panParam(pan, 3.0f) {
            m_parameters["pan"] = pan;
            updateGains();
        }

        QString name()        const override { return tr("Pan"); }
        QString description() const override { return tr("Stereo panning with constant power"); }
        QString category()    const override { return "Level"; }

        void process(EnhancedAudioBuffer& buffer, int sampleRate,
                     EffectContext context = EffectContext::Realtime) override {
            Q_UNUSED(context)
            if (!m_enabled || buffer.channels() < 2) return;
            m_panParam.process(buffer.samples(), sampleRate);
            updateGains();
            float* data    = buffer.data();
            int    samples = buffer.samples();
            for (int i = 0; i < samples; i++) {
                data[i * 2]     *= m_leftGain;
                data[i * 2 + 1] *= m_rightGain;
            }
        }

        void setPan(float pan) {
            pan = std::clamp(pan, -1.0f, 1.0f);
            m_panParam.setTarget(pan);
            setParameter("pan", pan);
        }
        float pan() const { return m_panParam.getTarget(); }

        void setParameters(const QVariantMap& params) override {
            if (params.contains("pan")) setPan(params["pan"].toFloat());
        }
        QVariantMap parameters() const override {
            QVariantMap p = m_parameters;
            p["pan"] = pan();
            return p;
        }

        void reset() override { m_panParam.setImmediate(m_panParam.getTarget()); updateGains(); }

        std::unique_ptr<AudioEffect> clone() const override {
            auto e = std::make_unique<PanEffect>(pan());
            e->setEnabled(m_enabled);
            return e;
        }

    private:
        void updateGains() {
            float p     = m_panParam.getCurrent();
            float angle = (p + 1.0f) * static_cast<float>(M_PI) / 4.0f;
            m_leftGain  = std::cos(angle) * 1.414f;
            m_rightGain = std::sin(angle) * 1.414f;
        }

        SmoothedParameter m_panParam;
        float m_leftGain  = 0.707f;
        float m_rightGain = 0.707f;
    };

    // ============================================================================
    // 3. Filter Effect
    // ============================================================================

    class FilterEffect : public AudioEffect {
        Q_OBJECT
    public:
        enum FilterType { LowPass, HighPass, BandPass, Notch, LowShelf, HighShelf, Peak };

        explicit FilterEffect(FilterType type = Peak, QObject* parent = nullptr)
            : AudioEffect(parent), m_type(type) {
            m_parameters["type"]      = static_cast<int>(type);
            m_parameters["frequency"] = 1000.0f;
            m_parameters["q"]         = 0.707f;
            m_parameters["gain"]      = 0.0f;
        }

        QString name() const override {
            switch (m_type) {
                case LowPass:  return tr("Low Pass");
                case HighPass: return tr("High Pass");
                case BandPass: return tr("Band Pass");
                case Notch:    return tr("Notch");
                case LowShelf: return tr("Low Shelf");
                case HighShelf:return tr("High Shelf");
                case Peak:     return tr("Peak EQ");
                default:       return tr("Filter");
            }
        }
        QString description() const override { return tr("Frequency filter with adjustable parameters"); }
        QString category()    const override { return "EQ/Filter"; }

        void process(EnhancedAudioBuffer& buffer, int sampleRate,
                     EffectContext context = EffectContext::Realtime) override {
            Q_UNUSED(context)
            if (!m_enabled) return;
            updateCoefficients(sampleRate);

            int channels = buffer.channels();
            if (static_cast<int>(m_states.size()) != channels)
                m_states.resize(channels);

            float* data    = buffer.data();
            int    samples = buffer.samples();

            for (int ch = 0; ch < channels; ch++) {
                // Use single set of coefficients for all channels
                DSPUtils::BiquadCoeffs& coeffs = m_coeffs[0];
                FilterState&            state  = m_states[ch];
                for (int i = 0; i < samples; i++) {
                    float input  = data[i * channels + ch];
                    float output = coeffs.b0 * input
                                 + coeffs.b1 * state.x1
                                 + coeffs.b2 * state.x2
                                 - coeffs.a1 * state.y1
                                 - coeffs.a2 * state.y2;
                    state.x2 = state.x1; state.x1 = input;
                    state.y2 = state.y1; state.y1 = output;
                    data[i * channels + ch] = output;
                }
            }
        }

        void setType(FilterType type)    { m_type = type; setParameter("type", static_cast<int>(type)); m_coeffs.clear(); }
        void setFrequency(float freq)    { m_frequency = std::clamp(freq, 20.0f, 20000.0f); setParameter("frequency", m_frequency); m_coeffs.clear(); }
        void setQ(float q)               { m_q = std::clamp(q, 0.1f, 10.0f);               setParameter("q", m_q);               m_coeffs.clear(); }
        void setGain(float gainDb)       { m_gainDb = std::clamp(gainDb, -24.0f, 24.0f);   setParameter("gain", m_gainDb);        m_coeffs.clear(); }

        FilterType type()      const { return m_type; }
        float      frequency() const { return m_frequency; }
        float      q()         const { return m_q; }
        float      gain()      const { return m_gainDb; }

        void setParameters(const QVariantMap& params) override {
            if (params.contains("type"))      setType(static_cast<FilterType>(params["type"].toInt()));
            if (params.contains("frequency")) setFrequency(params["frequency"].toFloat());
            if (params.contains("q"))         setQ(params["q"].toFloat());
            if (params.contains("gain"))      setGain(params["gain"].toFloat());
        }
        QVariantMap parameters() const override {
            QVariantMap p = m_parameters;
            p["type"]      = static_cast<int>(m_type);
            p["frequency"] = m_frequency;
            p["q"]         = m_q;
            p["gain"]      = m_gainDb;
            return p;
        }

        void reset() override { for (auto& s : m_states) s = FilterState{}; m_coeffs.clear(); }

        std::unique_ptr<AudioEffect> clone() const override {
            auto e = std::make_unique<FilterEffect>(m_type);
            e->setFrequency(m_frequency);
            e->setQ(m_q);
            e->setGain(m_gainDb);
            e->setEnabled(m_enabled);
            return e;
        }

    private:
        struct FilterState { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

        void updateCoefficients(int sampleRate) {
            if (sampleRate == m_lastSampleRate && !m_coeffs.empty()) return;
            m_lastSampleRate = sampleRate;
            DSPUtils::BiquadCoeffs base;
            switch (m_type) {
                case Peak:     base.setPeaking(m_frequency, m_q, m_gainDb, sampleRate); break;
                case HighPass: base.setHighPass(m_frequency, m_q, sampleRate); break;
                default:       base.setLowPass(m_frequency, m_q, sampleRate);  break;
            }
            m_coeffs.assign(1, base);
        }

        FilterType m_type      = Peak;
        float m_frequency      = 1000.0f;
        float m_q              = 0.707f;
        float m_gainDb         = 0.0f;
        std::vector<DSPUtils::BiquadCoeffs> m_coeffs;
        std::vector<FilterState>            m_states;
        int m_lastSampleRate   = 0;
    };

    // ============================================================================
    // 4. Compressor Effect
    // ============================================================================

    class CompressorEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit CompressorEffect(QObject* parent = nullptr) : AudioEffect(parent) {
            m_parameters["threshold"] = m_thresholdDb;
            m_parameters["ratio"]     = m_ratio;
            m_parameters["attack"]    = m_attackMs;
            m_parameters["release"]   = m_releaseMs;
            m_parameters["makeupGain"]= m_makeupGainDb;
            updateCoeffs(48000);
        }

        QString name()        const override { return tr("Compressor"); }
        QString description() const override { return tr("Dynamic range compression"); }
        QString category()    const override { return "Dynamics"; }

        void process(EnhancedAudioBuffer& buffer, int sampleRate,
                     EffectContext context = EffectContext::Realtime) override {
            Q_UNUSED(context)
            if (!m_enabled) return;
            updateCoeffs(sampleRate);

            float makeupGain      = DSPUtils::dbToLinear(m_makeupGainDb);
            float thresholdLinear = DSPUtils::dbToLinear(m_thresholdDb);
            float* data           = buffer.data();
            int    total          = static_cast<int>(buffer.totalSamples());

            for (int i = 0; i < total; i++) {
                float input      = data[i];
                float inputLevel = std::abs(input);

                m_envelope = (inputLevel > m_envelope)
                    ? m_attackCoeff  * (m_envelope - inputLevel) + inputLevel
                    : m_releaseCoeff * (m_envelope - inputLevel) + inputLevel;

                float gain = 1.0f;
                if (m_envelope > thresholdLinear) {
                    float dbOver         = DSPUtils::linearToDb(m_envelope / thresholdLinear);
                    float dbGainReduction= dbOver * (1.0f - 1.0f / m_ratio);
                    gain                 = DSPUtils::dbToLinear(-dbGainReduction);
                }
                data[i] = input * gain * makeupGain;
            }
        }

        void setThreshold(float db)   { m_thresholdDb  = db;                             setParameter("threshold",  db); }
        void setRatio(float ratio)    { m_ratio         = std::max(1.0f, ratio);          setParameter("ratio",      m_ratio); }
        void setAttack(float ms)      { m_attackMs      = std::max(0.1f, ms); m_coeffsValid = false; setParameter("attack",     m_attackMs); }
        void setRelease(float ms)     { m_releaseMs     = std::max(1.0f, ms); m_coeffsValid = false; setParameter("release",    m_releaseMs); }
        void setMakeupGain(float db)  { m_makeupGainDb  = db;                             setParameter("makeupGain", db); }

        void setParameters(const QVariantMap& params) override {
            if (params.contains("threshold"))  setThreshold(params["threshold"].toFloat());
            if (params.contains("ratio"))      setRatio(params["ratio"].toFloat());
            if (params.contains("attack"))     setAttack(params["attack"].toFloat());
            if (params.contains("release"))    setRelease(params["release"].toFloat());
            if (params.contains("makeupGain")) setMakeupGain(params["makeupGain"].toFloat());
        }
        QVariantMap parameters() const override {
            QVariantMap p = m_parameters;
            p["threshold"]  = m_thresholdDb;
            p["ratio"]      = m_ratio;
            p["attack"]     = m_attackMs;
            p["release"]    = m_releaseMs;
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
            float attackSamples  = m_attackMs  * sampleRate / 1000.0f;
            float releaseSamples = m_releaseMs * sampleRate / 1000.0f;
            m_attackCoeff  = std::exp(-1.0f / std::max(1.0f, attackSamples));
            m_releaseCoeff = std::exp(-1.0f / std::max(1.0f, releaseSamples));
            m_coeffsValid  = true;
        }

        float m_thresholdDb  = -20.0f;
        float m_ratio        =   4.0f;
        float m_makeupGainDb =   0.0f;
        float m_attackMs     =  10.0f;
        float m_releaseMs    = 100.0f;
        float m_envelope     =   0.0f;
        float m_attackCoeff  =   0.0f;
        float m_releaseCoeff =   0.0f;
        bool  m_coeffsValid  = false;
        int   m_lastSampleRate = 0;
    };

    // ============================================================================
    // 5. Normalize Effect (Offline only)
    // ============================================================================

    class NormalizeEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit NormalizeEffect(float targetDb = -1.0f, QObject* parent = nullptr)
            : AudioEffect(parent), m_targetDb(targetDb) {
            m_parameters["targetDb"] = targetDb;
        }

        QString name()        const override { return tr("Normalize"); }
        QString description() const override { return tr("Normalize audio to target dB level"); }
        QString category()    const override { return "Level"; }

        bool supportsRealtime() const override { return false; }

        void process(EnhancedAudioBuffer& buffer, int sampleRate,
                     EffectContext context = EffectContext::Realtime) override {
            Q_UNUSED(sampleRate) Q_UNUSED(context)
            if (!m_enabled) return;
            float peak = buffer.peakLevel();
            if (peak < 0.00001f) return;
            buffer.applyGain(DSPUtils::dbToLinear(m_targetDb) / peak);
        }

        void processOffline(EnhancedAudioBuffer& buffer,
                            const Selection& selection, int sampleRate) override {
            if (selection.isEmpty()) { process(buffer, sampleRate, EffectContext::Offline); return; }
            float peak = 0.0f;
            qint64 end = std::min(selection.end, buffer.frames());
            for (qint64 f = selection.start; f < end; f++)
                for (int ch = 0; ch < buffer.channels(); ch++)
                    peak = std::max(peak, std::abs(buffer.sampleAt(ch, f)));
            if (peak < 0.00001f) return;
            float gain = DSPUtils::dbToLinear(m_targetDb) / peak;
            for (qint64 f = selection.start; f < end; f++)
                for (int ch = 0; ch < buffer.channels(); ch++)
                    buffer.setSample(ch, f, buffer.sampleAt(ch, f) * gain);
        }

        void setTargetDb(float db) { m_targetDb = db; setParameter("targetDb", db); }
        void setParameters(const QVariantMap& params) override {
            if (params.contains("targetDb")) setTargetDb(params["targetDb"].toFloat());
        }
        QVariantMap parameters() const override {
            QVariantMap p = m_parameters; p["targetDb"] = m_targetDb; return p;
        }
        std::unique_ptr<AudioEffect> clone() const override {
            auto e = std::make_unique<NormalizeEffect>(m_targetDb);
            e->setEnabled(m_enabled);
            return e;
        }

    private:
        float m_targetDb = -1.0f;
    };

    // ============================================================================
    // 6. Fade Effect (Offline only)
    // ============================================================================

    class FadeEffect : public AudioEffect {
        Q_OBJECT
    public:
        enum FadeType { FadeIn, FadeOut, FadeCustom };

        explicit FadeEffect(FadeType type = FadeIn, QObject* parent = nullptr)
            : AudioEffect(parent), m_type(type) {
            m_parameters["type"]  = type;
            m_parameters["curve"] = 1.0f;
        }

        QString name() const override {
            return m_type == FadeIn ? tr("Fade In") : tr("Fade Out");
        }
        QString description() const override {
            return m_type == FadeIn ? tr("Fade in from silence") : tr("Fade out to silence");
        }
        QString category()    const override { return "Level"; }
        bool supportsRealtime() const override { return false; }

        void process(EnhancedAudioBuffer& buffer, int sampleRate,
                     EffectContext context = EffectContext::Realtime) override {
            Q_UNUSED(sampleRate) Q_UNUSED(context)
            if (!m_enabled) return;
            int samples  = buffer.samples();
            int channels = buffer.channels();
            for (int f = 0; f < samples; f++) {
                float t      = f / static_cast<float>(samples);
                float factor = std::pow(m_type == FadeIn ? t : (1.0f - t), m_curve);
                for (int ch = 0; ch < channels; ch++)
                    buffer.setSample(ch, f, buffer.sampleAt(ch, f) * factor);
            }
        }

        void processOffline(EnhancedAudioBuffer& buffer,
                            const Selection& selection, int sampleRate) override {
            if (selection.isEmpty()) { process(buffer, sampleRate, EffectContext::Offline); return; }
            qint64 start    = selection.start;
            qint64 len      = selection.length();
            qint64 end      = std::min(selection.end, buffer.frames());
            int    channels = buffer.channels();
            for (qint64 f = start; f < end; f++) {
                float t      = (f - start) / static_cast<float>(len);
                float factor = std::pow(m_type == FadeIn ? t : (1.0f - t), m_curve);
                for (int ch = 0; ch < channels; ch++)
                    buffer.setSample(ch, f, buffer.sampleAt(ch, f) * factor);
            }
        }

        void setFadeType(FadeType type) { m_type = type; setParameter("type", type); }
        void setCurve(float curve)      { m_curve = std::clamp(curve, 0.1f, 10.0f); setParameter("curve", curve); }

        void setParameters(const QVariantMap& params) override {
            if (params.contains("type"))  setFadeType(static_cast<FadeType>(params["type"].toInt()));
            if (params.contains("curve")) setCurve(params["curve"].toFloat());
        }
        QVariantMap parameters() const override {
            QVariantMap p = m_parameters; p["type"] = m_type; p["curve"] = m_curve; return p;
        }
        std::unique_ptr<AudioEffect> clone() const override {
            auto e = std::make_unique<FadeEffect>(m_type);
            e->setCurve(m_curve); e->setEnabled(m_enabled);
            return e;
        }

    private:
        FadeType m_type;
        float    m_curve = 1.0f;
    };

    // ============================================================================
    // 7. Reverse Effect (Offline only)
    // ============================================================================

    class ReverseEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit ReverseEffect(QObject* parent = nullptr) : AudioEffect(parent) {}

        QString name()        const override { return tr("Reverse"); }
        QString description() const override { return tr("Reverse audio playback direction"); }
        QString category()    const override { return "Time"; }
        bool supportsRealtime() const override { return false; }
        bool supportsPreview()  const override { return false; }

        void process(EnhancedAudioBuffer& buffer, int sampleRate,
                     EffectContext context = EffectContext::Realtime) override {
            Q_UNUSED(sampleRate) Q_UNUSED(context)
            if (!m_enabled) return;
            int samples  = buffer.samples();
            int channels = buffer.channels();
            for (int i = 0; i < samples / 2; i++) {
                int j = samples - 1 - i;
                for (int ch = 0; ch < channels; ch++) {
                    float tmp = buffer.sampleAt(ch, i);
                    buffer.setSample(ch, i, buffer.sampleAt(ch, j));
                    buffer.setSample(ch, j, tmp);
                }
            }
        }

        void processOffline(EnhancedAudioBuffer& buffer,
                            const Selection& selection, int sampleRate) override {
            if (selection.isEmpty()) { process(buffer, sampleRate, EffectContext::Offline); return; }
            qint64 start    = selection.start;
            qint64 end      = std::min(selection.end, buffer.frames());
            qint64 len      = end - start;
            int    channels = buffer.channels();
            for (qint64 i = 0; i < len / 2; i++) {
                qint64 f1 = start + i;
                qint64 f2 = end - 1 - i;
                for (int ch = 0; ch < channels; ch++) {
                    float tmp = buffer.sampleAt(ch, f1);
                    buffer.setSample(ch, f1, buffer.sampleAt(ch, f2));
                    buffer.setSample(ch, f2, tmp);
                }
            }
        }

        std::unique_ptr<AudioEffect> clone() const override {
            return std::make_unique<ReverseEffect>();
        }
    };

    // ============================================================================
    // 8. Silence Effect
    // ============================================================================

    class SilenceEffect : public AudioEffect {
        Q_OBJECT
    public:
        explicit SilenceEffect(QObject* parent = nullptr) : AudioEffect(parent) {}

        QString name()        const override { return tr("Silence"); }
        QString description() const override { return tr("Replace audio with silence"); }
        QString category()    const override { return "Level"; }

        void process(EnhancedAudioBuffer& buffer, int sampleRate,
                     EffectContext context = EffectContext::Realtime) override {
            Q_UNUSED(sampleRate) Q_UNUSED(context)
            if (m_enabled) buffer.zero();
        }

        void processOffline(EnhancedAudioBuffer& buffer,
                            const Selection& selection, int sampleRate) override {
            if (selection.isEmpty()) { process(buffer, sampleRate, EffectContext::Offline); return; }
            qint64 end = std::min(selection.end, buffer.frames());
            for (qint64 f = selection.start; f < end; f++)
                for (int ch = 0; ch < buffer.channels(); ch++)
                    buffer.setSample(ch, f, 0.0f);
        }

        std::unique_ptr<AudioEffect> clone() const override {
            return std::make_unique<SilenceEffect>();
        }
    };

    // ============================================================================
    // Effect Chain
    // ============================================================================

    /**
     * @brief Manages an ordered collection of audio effects.
     *
     * This is the single authoritative definition of EffectChain in the codebase.
     * The abstract placeholder that was in audio_daw.h has been removed.
     */
    class EffectChain : public QObject {
        Q_OBJECT
    public:
        explicit EffectChain(QObject* parent = nullptr) : QObject(parent) {}

        void addEffect(std::shared_ptr<AudioEffect> effect) {
            m_effects.append(effect);
            emit effectAdded(m_effects.size() - 1);
            emit chainChanged();
        }

        void removeEffect(int index) {
            if (index >= 0 && index < m_effects.size()) {
                m_effects.removeAt(index);
                emit effectRemoved(index);
                emit chainChanged();
            }
        }

        void moveEffect(int from, int to) {
            if (from >= 0 && from < m_effects.size() &&
                to   >= 0 && to   < m_effects.size()) {
                m_effects.move(from, to);
                emit effectMoved(from, to);
                emit chainChanged();
            }
        }

        void clear() { m_effects.clear(); emit chainChanged(); }

        int  count()   const { return m_effects.size(); }
        bool isEmpty() const { return m_effects.isEmpty(); }

        std::shared_ptr<AudioEffect> effectAt(int index) const {
            return (index >= 0 && index < m_effects.size()) ? m_effects[index] : nullptr;
        }

        void process(EnhancedAudioBuffer& buffer, int sampleRate,
                     EffectContext context = EffectContext::Realtime) {
            for (auto& effect : m_effects)
                if (effect && effect->isEnabled())
                    effect->process(buffer, sampleRate, context);
        }

        void processOffline(EnhancedAudioBuffer& buffer,
                            const Selection& selection, int sampleRate) {
            for (auto& effect : m_effects)
                if (effect && effect->isEnabled())
                    effect->processOffline(buffer, selection, sampleRate);
        }

        int totalLatency() const {
            int latency = 0;
            for (auto& effect : m_effects)
                if (effect && effect->isEnabled())
                    latency += effect->latencySamples();
            return latency;
        }

        void resetAll() {
            for (auto& effect : m_effects)
                if (effect) effect->reset();
        }

        QVariantMap serialize() const {
            QVariantList effects;
            for (auto& effect : m_effects) {
                if (effect) {
                    QVariantMap e;
                    e["name"]       = effect->name();
                    e["enabled"]    = effect->isEnabled();
                    e["parameters"] = effect->parameters();
                    effects.append(e);
                }
            }
            QVariantMap result;
            result["effects"] = effects;
            return result;
        }

    signals:
        void effectAdded(int index);
        void effectRemoved(int index);
        void effectMoved(int from, int to);
        void chainChanged();

    private:
        QList<std::shared_ptr<AudioEffect>> m_effects;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::EffectContext)
Q_DECLARE_METATYPE(Aegis::FilterEffect::FilterType)
Q_DECLARE_METATYPE(Aegis::FadeEffect::FadeType)
