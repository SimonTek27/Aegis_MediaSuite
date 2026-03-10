#include <QtTest>
#include "converter.h"

using namespace Aegis;

class TestConverter : public QObject {
    Q_OBJECT
private slots:
    void default_job_is_not_active() {
        Converter c(nullptr);
        QVERIFY(!c.converting());
    }
};

QTEST_MAIN(TestConverter)
#include "test_utils_converter.moc"

