#include <QtTest>
#include "converter.h"

using namespace Aegis;

class TestConverter : public QObject {
    Q_OBJECT
private slots:
    void default_job_is_not_active() {
        MediaConverter c;
        QVERIFY(!c.isBusy());
    }
};

QTEST_MAIN(TestConverter)
#include "test_utils_converter.moc"
