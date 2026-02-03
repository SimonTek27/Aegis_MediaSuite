#include <QtTest>
#include "streaming.h"

using namespace Aegis;

class TestStreaming : public QObject {
    Q_OBJECT
private slots:
    void yt_availability_call_does_not_crash() {
        Streaming s;
        (void)s.ytAvailable();
    }
};

QTEST_MAIN(TestStreaming)
#include "test_streaming.moc"
