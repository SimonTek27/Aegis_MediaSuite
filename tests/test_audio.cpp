#include <QtTest>
#include "audio.h"

using namespace Aegis;

class TestAudio : public QObject {
    Q_OBJECT
private slots:
    void construct_audio_engine_does_not_crash() {
        AudioEngine engine;
        QVERIFY(engine.outputDeviceName().size() >= 0); // just touch API
    }
};

QTEST_MAIN(TestAudio)
#include "test_audio.moc"
