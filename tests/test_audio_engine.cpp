// tests/test_audio_engine.cpp
// Aegis MediaSuite — Unit tests: Audio Engine
// Framework: Qt Test (QTest)
// Build:  qmake / cmake with Qt6::Test
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <cmath>
#include <numeric>

#include "audio.h"
#include "audio_effects.h"

using namespace Aegis;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Generate a pure sine wave into a float buffer.
static std::vector<float> makeSine(float freq, int sampleRate, int frames,
                                   float amplitude = 1.0f) {
    std::vector<float> buf(frames);
    for (int i = 0; i < frames; ++i)
        buf[i] = amplitude * std::sin(2.0f * float(M_PI) * freq * i / sampleRate);
    return buf;
}

/// Generate a stereo-interleaved sine wave.
static std::vector<float> makeStereoSine(float freqL, float freqR,
                                          int sampleRate, int frames) {
    std::vector<float> buf(frames * 2);
    for (int i = 0; i < frames; ++i) {
        buf[i * 2]     = std::sin(2.0f * float(M_PI) * freqL * i / sampleRate);
        buf[i * 2 + 1] = std::sin(2.0f * float(M_PI) * freqR * i / sampleRate);
    }
    return buf;
}

/// RMS of a mono float buffer.
static double rms(const std::vector<float>& buf) {
    double sum = 0.0;
    for (float s : buf) sum += double(s) * double(s);
    return std::sqrt(sum / buf.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test class
// ─────────────────────────────────────────────────────────────────────────────
class TestAudioEngine : public QObject {
    Q_OBJECT

private slots:

    // ── Lifecycle ──────────────────────────────────────────────────────────

    void initTestCase() {
        m_engine = std::make_unique<AudioEngine>(nullptr);
        QVERIFY(m_engine != nullptr);
    }

    void cleanupTestCase() {
        m_engine.reset();
    }

    // ── Volume ─────────────────────────────────────────────────────────────

    void testSetVolume_inRange() {
        m_engine->setVolume(75.0);
        QCOMPARE(m_engine->volume(), 75.0);
    }

    void testSetVolume_clampHigh() {
        m_engine->setVolume(150.0);
        QVERIFY(m_engine->volume() <= 100.0);
    }

    void testSetVolume_clampLow() {
        m_engine->setVolume(-10.0);
        QVERIFY(m_engine->volume() >= 0.0);
    }

    void testSetVolume_zero() {
        m_engine->setVolume(0.0);
        QCOMPARE(m_engine->volume(), 0.0);
    }

    // ── FFT / Spectrum ─────────────────────────────────────────────────────

    void testSpectrum_emptyBufferReturnsEmpty() {
        auto result = m_engine->calculateSpectrum(nullptr, 0, 32);
        QVERIFY(result.isEmpty());
    }

    void testSpectrum_returnsBandCount() {
        const int sampleRate = 48000;
        const int bands      = 32;
        auto sine = makeSine(1000.0f, sampleRate, 4096);
        auto result = m_engine->calculateSpectrum(sine.data(), sine.size(), bands);
        QCOMPARE(result.size(), bands);
    }

    void testSpectrum_valuesNormalized() {
        const int sampleRate = 48000;
        auto sine = makeSine(440.0f, sampleRate, 4096);
        auto result = m_engine->calculateSpectrum(sine.data(), sine.size(), 32);
        for (double v : result) {
            QVERIFY2(v >= 0.0 && v <= 1.0,
                     qPrintable(QString("Spectrum value %1 out of [0,1]").arg(v)));
        }
    }

    void testSpectrum_1kHzPeakInMidBand() {
        // A 1 kHz sine should have energy in the mid-frequency bands (not the extremes)
        const int sampleRate = 48000;
        const int bands      = 32;
        auto sine = makeSine(1000.0f, sampleRate, 8192);
        auto result = m_engine->calculateSpectrum(sine.data(), sine.size(), bands);

        // Find band with peak energy
        int peakBand = 0;
        for (int i = 1; i < result.size(); ++i)
            if (result[i] > result[peakBand]) peakBand = i;

        // 1 kHz in a log-scaled 32-band spectrum should land in bands 12–22
        QVERIFY2(peakBand >= 10 && peakBand <= 24,
                 qPrintable(QString("Peak band %1 unexpected for 1 kHz").arg(peakBand)));
    }

    // ── Loudness (EBU R128) ────────────────────────────────────────────────

    void testLoudness_silenceIsMinusInfinity() {
        const int sampleRate = 48000;
        std::vector<float> silence(sampleRate * 2, 0.0f);  // 2 seconds
        m_engine->processForLoudness(silence.data(), silence.size(), sampleRate, 2);

        double momentary = m_engine->momentaryLoudness();
        QVERIFY2(momentary <= -60.0,
                 qPrintable(QString("Silence momentary loudness: %1").arg(momentary)));
    }

    void testLoudness_fullScaleSineIsLoud() {
        const int sampleRate = 48000;
        auto sine = makeStereoSine(1000.0f, 1000.0f, sampleRate, sampleRate); // 1 sec stereo
        m_engine->processForLoudness(sine.data(), sine.size(), sampleRate, 2);

        double momentary = m_engine->momentaryLoudness();
        // Full-scale sine at 1 kHz should be around -3 LUFS
        QVERIFY2(momentary > -20.0,
                 qPrintable(QString("Full-scale loudness too low: %1").arg(momentary)));
    }

    // ── Sample-rate conversion ─────────────────────────────────────────────

    void testSampleRate_default() {
        QCOMPARE(m_engine->sampleRate(), 48000);
    }

    void testSampleRate_setSupportedRate() {
        m_engine->setSampleRate(44100);
        QCOMPARE(m_engine->sampleRate(), 44100);
        m_engine->setSampleRate(48000); // restore
    }

    void testSampleRate_rejectInvalid() {
        int prev = m_engine->sampleRate();
        m_engine->setSampleRate(0);
        QCOMPARE(m_engine->sampleRate(), prev);  // must not change
    }

    // ── Effects processing ─────────────────────────────────────────────────

    void testGainEffect_unity() {
        auto effect = std::make_unique<GainEffect>(1.0f);
        const int frames = 512;
        EnhancedAudioBuffer buf(frames, 2, 48000);
        // Fill with 0.5 amplitude
        for (int i = 0; i < frames * 2; ++i) buf.data()[i] = 0.5f;

        effect->process(buf, 48000, EffectContext::RealTime);

        // Unity gain must not change amplitude
        for (int i = 0; i < frames * 2; ++i)
            QVERIFY(qAbs(buf.data()[i] - 0.5f) < 1e-5f);
    }

    void testGainEffect_doubleAmplitude() {
        auto effect = std::make_unique<GainEffect>(2.0f);
        const int frames = 256;
        EnhancedAudioBuffer buf(frames, 1, 48000);
        for (int i = 0; i < frames; ++i) buf.data()[i] = 0.3f;

        effect->process(buf, 48000, EffectContext::RealTime);

        for (int i = 0; i < frames; ++i)
            QVERIFY(qAbs(buf.data()[i] - 0.6f) < 1e-4f);
    }

    void testPanEffect_center() {
        auto effect = std::make_unique<PanEffect>(0.0f);  // center pan
        const int frames = 128;
        EnhancedAudioBuffer buf(frames, 2, 48000);
        for (int i = 0; i < frames * 2; ++i) buf.data()[i] = 1.0f;

        effect->process(buf, 48000, EffectContext::RealTime);

        // Center pan: left and right should be approximately equal
        for (int i = 0; i < frames; ++i) {
            float L = buf.data()[i * 2];
            float R = buf.data()[i * 2 + 1];
            QVERIFY(qAbs(L - R) < 0.05f);
        }
    }

    void testPanEffect_hardLeft() {
        auto effect = std::make_unique<PanEffect>(-1.0f);
        const int frames = 128;
        EnhancedAudioBuffer buf(frames, 2, 48000);
        for (int i = 0; i < frames * 2; ++i) buf.data()[i] = 1.0f;

        effect->process(buf, 48000, EffectContext::RealTime);

        for (int i = 0; i < frames; ++i) {
            float R = buf.data()[i * 2 + 1];
            QVERIFY2(R < 0.05f, "Hard-left pan should silence right channel");
        }
    }

    void testFadeInEffect() {
        auto effect = std::make_unique<FadeEffect>(FadeEffect::FadeIn);
        const int frames = 1024;
        EnhancedAudioBuffer buf(frames, 1, 48000);
        for (int i = 0; i < frames; ++i) buf.data()[i] = 1.0f;

        effect->process(buf, 48000, EffectContext::Offline);

        // First sample should be near 0, last near 1
        QVERIFY(buf.data()[0] < 0.05f);
        QVERIFY(buf.data()[frames - 1] > 0.95f);
    }

    void testFadeOutEffect() {
        auto effect = std::make_unique<FadeEffect>(FadeEffect::FadeOut);
        const int frames = 1024;
        EnhancedAudioBuffer buf(frames, 1, 48000);
        for (int i = 0; i < frames; ++i) buf.data()[i] = 1.0f;

        effect->process(buf, 48000, EffectContext::Offline);

        QVERIFY(buf.data()[0] > 0.95f);
        QVERIFY(buf.data()[frames - 1] < 0.05f);
    }

    void testEffectDisabled_passesThrough() {
        auto effect = std::make_unique<GainEffect>(0.0f);  // would silence
        effect->setEnabled(false);
        const int frames = 64;
        EnhancedAudioBuffer buf(frames, 1, 48000);
        for (int i = 0; i < frames; ++i) buf.data()[i] = 0.7f;

        effect->process(buf, 48000, EffectContext::RealTime);

        // Disabled effect must pass audio unchanged
        for (int i = 0; i < frames; ++i)
            QVERIFY(qAbs(buf.data()[i] - 0.7f) < 1e-5f);
    }

    // ── Signal emission ────────────────────────────────────────────────────

    void testSpectrumUpdatedSignalEmitted() {
        QSignalSpy spy(m_engine.get(), &AudioEngine::spectrumUpdated);
        auto sine = makeSine(440.0f, 48000, 4096);
        m_engine->calculateSpectrum(sine.data(), sine.size(), 32);
        // The implementation may emit asynchronously; wait up to 200 ms
        spy.wait(200);
        QVERIFY(spy.count() >= 1);
    }

private:
    std::unique_ptr<AudioEngine> m_engine;
};

QTEST_MAIN(TestAudioEngine)
#include "test_audio_engine.moc"
