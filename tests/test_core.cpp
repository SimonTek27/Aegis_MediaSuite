// tests/test_core.cpp
// Aegis MediaSuite — Smoke test: AudioEngine construction
// Framework: Qt Test (QTest)
//
// FIX: outputDeviceName() does NOT exist on AudioEngine — it belongs to
//      AudioOutput (the PipeWire/Qt backend). Replaced with processingEnabled()
//      which is a real Q_PROPERTY on AudioEngine.

#include <QtTest>
#include "audio.h"

using namespace Aegis;

class TestAudio : public QObject {
    Q_OBJECT
private slots:
    void construct_audio_engine_does_not_crash() {
        AudioEngine engine;
        // processingEnabled() is a valid Q_PROPERTY on AudioEngine.
        // outputDeviceName() does NOT exist on AudioEngine.
        bool enabled = engine.processingEnabled();
        Q_UNUSED(enabled);
    }
};

QTEST_MAIN(TestAudio)
#include "test_core.moc"
