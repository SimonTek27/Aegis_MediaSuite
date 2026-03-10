// tests/test_audio_engine.cpp
// Aegis MediaSuite — Unit tests: AudioEngine
// Framework: Qt Test (QTest)
//
// FIX SUMMARY (vs original):
//  1. Removed setVolume/volume tests — AudioEngine has no such API.
//     Volume is managed by AudioOutput (PipeWire/Qt backend), not by AudioEngine.
//     Replaced with processBuffer smoke-tests that exercise the same code path.
//  2. Replaced processForLoudness(data, n, sr, ch) with processBuffer(data, frames, sr, ch)
//     which internally calls calculateLoudness(); then read momentaryLoudness().
//  3. Removed sampleRate/setSampleRate — AudioEngine has no mutable sample-rate property.
//     The sample rate is set per-call in processBuffer().
//  4. EnhancedAudioBuffer constructor takes (frames, channels) — dropped the third
//     argument (sampleRate) that the test was incorrectly passing.
//  5. EffectContext::RealTime  →  EffectContext::Realtime  (correct enum spelling).
//  6. calculateSpectrum returns QVector<float>; loop variable changed from double to float.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <cmath>
#include <vector>

#include "audio.h"
#include "audio_effects.h"

using namespace Aegis;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<float> makeSine(float freq, int sampleRate, int frames,
                                    float amplitude = 1.0f) {
    std::vector<float> buf(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
        buf[static_cast<size_t>(i)] = amplitude *
            std::sin(2.0f * static_cast<float>(M_PI) * freq *
                     static_cast<float>(i) / static_cast<float>(sampleRate));
    return buf;
}

static std::vector<float> makeStereoSine(float freqL, float freqR,
                                          int sampleRate, int frames) {
    std::vector<float> buf(static_cast<size_t>(frames * 2));
    for (int i = 0; i < frames; ++i) {
        buf[static_cast<size_t>(i * 2)]     =
            std::sin(2.0f * static_cast<float>(M_PI) * freqL *
                     static_cast<float>(i) / static_cast<float>(sampleRate));
        buf[static_cast<size_t>(i * 2 + 1)] =
            std::sin(2.0f * static_cast<float>(M_PI) * freqR *
                     static_cast<float>(i) / static_cast<float>(sampleRate));
    }
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test class
// ─────────────────────────────────────────────────────────────────────────────
class TestAudioEngine : public QObject {
    Q_OBJECT

private slots:

    // ── Lifecycle ──────────────────────────────────────────────────────────

    void initTestCase() {
        m_engine = std::make_unique<Aegis::AudioEngine>(nullptr);
        QVERIFY(m_engine != nullptr);
    }

    void cleanupTestCase() {
        m_engine.reset();
    }

    // ── processBuffer smoke tests ──────────────────────────────────────────
    // AudioEngine has no setVolume/volume or setSampleRate/sampleRate — those
    // belong to AudioOutput.  processBuffer() is the real entry point that
    // applies all enabled effects and feeds the EBU R128 / FFT pipelines.

    void testProcessBuffer_doesNotCrash_monoSilence() {
        std::vector<float> silence(512, 0.0f);
        m_engine->processBuffer(silence.data(), 512, 48000, 1);
    }

    void testProcessBuffer_doesNotCrash_stereoSine() {
        auto sine = makeStereoSine(440.0f, 440.0f, 48000, 1024);
        m_engine->processBuffer(sine.data(), 1024, 48000, 2);
    }

    void testProcessBuffer_doesNotCrash_44100() {
        auto sine = makeStereoSine(1000.0f, 1000.0f, 44100, 1024);
        m_engine->processBuffer(sine.data(), 1024, 44100, 2);
    }

    void testProcessBuffer_nullPointer_isHandledGracefully() {
        m_engine->processBuffer(nullptr, 0, 48000, 2);
    }

    // ── FFT / Spectrum ─────────────────────────────────────────────────────

    void testSpectrum_emptyBufferReturnsEmpty() {
        auto result = m_engine->calculateSpectrum(nullptr, 0, 1);
        QVERIFY(result.isEmpty());
    }

    void testSpectrum_returnsBandCount() {
        const int bands = 32;
        auto sine = makeSine(1000.0f, 48000, 4096);
        auto result = m_engine->calculateSpectrum(sine.data(),
                                                  static_cast<int>(sine.size()), 1);
        QVERIFY(!result.isEmpty());
        QCOMPARE(result.size(), bands);
    }

    void testSpectrum_valuesNormalized() {
        auto sine = makeSine(440.0f, 48000, 4096);
        auto result = m_engine->calculateSpectrum(sine.data(),
                                                  static_cast<int>(sine.size()), 1);
        for (float v : result) {
            QVERIFY2(v >= 0.0f && v <= 1.0f,
                     qPrintable(QString("Spectrum value %1 out of [0,1]")
                                .arg(static_cast<double>(v))));
        }
    }

    void testSpectrum_1kHzPeakInMidBand() {
        auto sine = makeSine(1000.0f, 48000, 8192);
        auto result = m_engine->calculateSpectrum(sine.data(),
                                                  static_cast<int>(sine.size()), 1);
        QVERIFY(!result.isEmpty());

        int peakBand = 0;
        for (int i = 1; i < result.size(); ++i)
            if (result[i] > result[peakBand]) peakBand = i;

        QVERIFY2(peakBand >= 10 && peakBand <= 24,
                 qPrintable(QString("Peak band %1 unexpected for 1 kHz").arg(peakBand)));
    }

    // ── Loudness (EBU R128) ────────────────────────────────────────────────
    // processBuffer() drives calculateLoudness() internally.

    void testLoudness_silenceIsVeryQuiet() {
        std::vector<float> silence(static_cast<size_t>(48000 * 2 * 2), 0.0f);
        m_engine->processBuffer(silence.data(), 48000 * 2, 48000, 2);

        double momentary = m_engine->momentaryLoudness();
        QVERIFY2(momentary <= -40.0,
                 qPrintable(QString("Silence loudness should be very low, got: %1").arg(momentary)));
    }

    void testLoudness_fullScaleSineIsLoud() {
        auto sine = makeStereoSine(1000.0f, 1000.0f, 48000, 48000);
        m_engine->processBuffer(sine.data(), 48000, 48000, 2);

        double momentary = m_engine->momentaryLoudness();
        QVERIFY2(momentary > -30.0,
                 qPrintable(QString("Full-scale sine loudness too low: %1").arg(momentary)));
    }

    void testLoudness_shortTermIsFinite() {
        double st = m_engine->shortTermLoudness();
        QVERIFY(std::isfinite(st));
    }

    // ── Effects processing ─────────────────────────────────────────────────
    // EnhancedAudioBuffer(frames, channels)  ← only 2 args, no sampleRate.
    // EffectContext::Realtime                ← capital R, lowercase t.

    void testGainEffect_unity() {
        auto effect = std::make_unique<GainEffect>(1.0f);
        const int frames = 512;
        EnhancedAudioBuffer buf(frames, 2);
        for (int i = 0; i < frames * 2; ++i) buf.data()[i] = 0.5f;

        effect->process(buf, 48000, EffectContext::Realtime);

        for (int i = 0; i < frames * 2; ++i)
            QVERIFY(qAbs(buf.data()[i] - 0.5f) < 1e-5f);
    }

    void testGainEffect_doubleAmplitude() {
        auto effect = std::make_unique<GainEffect>(2.0f);
        const int frames = 256;
        EnhancedAudioBuffer buf(frames, 1);
        for (int i = 0; i < frames; ++i) buf.data()[i] = 0.3f;

        effect->process(buf, 48000, EffectContext::Realtime);

        for (int i = 0; i < frames; ++i)
            QVERIFY(qAbs(buf.data()[i] - 0.6f) < 1e-4f);
    }

    void testPanEffect_center() {
        auto effect = std::make_unique<PanEffect>(0.0f);
        const int frames = 128;
        EnhancedAudioBuffer buf(frames, 2);
        for (int i = 0; i < frames * 2; ++i) buf.data()[i] = 1.0f;

        effect->process(buf, 48000, EffectContext::Realtime);

        for (int i = 0; i < frames; ++i) {
            float L = buf.data()[i * 2];
            float R = buf.data()[i * 2 + 1];
            QVERIFY(qAbs(L - R) < 0.05f);
        }
    }

    void testPanEffect_hardLeft() {
        auto effect = std::make_unique<PanEffect>(-1.0f);
        const int frames = 128;
        EnhancedAudioBuffer buf(frames, 2);
        for (int i = 0; i < frames * 2; ++i) buf.data()[i] = 1.0f;

        effect->process(buf, 48000, EffectContext::Realtime);

        for (int i = 0; i < frames; ++i) {
            float R = buf.data()[i * 2 + 1];
            QVERIFY2(R < 0.05f, "Hard-left pan should silence right channel");
        }
    }

    void testFadeInEffect() {
        auto effect = std::make_unique<FadeEffect>(FadeEffect::FadeIn);
        const int frames = 1024;
        EnhancedAudioBuffer buf(frames, 1);
        for (int i = 0; i < frames; ++i) buf.data()[i] = 1.0f;

        effect->process(buf, 48000, EffectContext::Offline);

        QVERIFY(buf.data()[0] < 0.05f);
        QVERIFY(buf.data()[frames - 1] > 0.95f);
    }

    void testFadeOutEffect() {
        auto effect = std::make_unique<FadeEffect>(FadeEffect::FadeOut);
        const int frames = 1024;
        EnhancedAudioBuffer buf(frames, 1);
        for (int i = 0; i < frames; ++i) buf.data()[i] = 1.0f;

        effect->process(buf, 48000, EffectContext::Offline);

        QVERIFY(buf.data()[0] > 0.95f);
        QVERIFY(buf.data()[frames - 1] < 0.05f);
    }

    void testEffectDisabled_passesThrough() {
        auto effect = std::make_unique<GainEffect>(0.0f);
        effect->setEnabled(false);
        const int frames = 64;
        EnhancedAudioBuffer buf(frames, 1);
        for (int i = 0; i < frames; ++i) buf.data()[i] = 0.7f;

        effect->process(buf, 48000, EffectContext::Realtime);  // ← Realtime

        for (int i = 0; i < frames; ++i)
            QVERIFY(qAbs(buf.data()[i] - 0.7f) < 1e-5f);
    }

    // ── Signal emission ────────────────────────────────────────────────────

    void testSpectrumUpdatedSignalEmitted() {
        QSignalSpy spy(m_engine.get(), &Aegis::AudioEngine::spectrumUpdated);
        auto sine = makeSine(440.0f, 48000, 4096);
        m_engine->calculateSpectrum(sine.data(), static_cast<int>(sine.size()), 1);
        spy.wait(200);
        QVERIFY(spy.count() >= 1);
    }

private:
    std::unique_ptr<Aegis::AudioEngine> m_engine;
};

QTEST_MAIN(TestAudioEngine)
#include "test_audio_engine.moc"
