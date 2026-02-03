#include <QtTest>
#include "video.h"

using namespace Aegis;

class TestVideo : public QObject {
    Q_OBJECT
private slots:
    void construct_video_engine_does_not_have_video_initially() {
        VideoEngine v;
        QVERIFY(!v.hasVideo());
    }
};

QTEST_MAIN(TestVideo)
#include "test_video.moc"
