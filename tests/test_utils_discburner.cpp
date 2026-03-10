// Utility tests for CDBurner and BurnJob

#include <QtTest/QtTest>

#include "../src/discburner.h"

using namespace Aegis;

class TestUtilsDiscBurner : public QObject {
    Q_OBJECT

private slots:
    void testBurnJobDefaults() {
        BurnJob job;
        QCOMPARE(job.type, BurnType::DataCD);
        QCOMPARE(job.device, QString("/dev/sr0"));
        QCOMPARE(job.volumeLabel, QString("Aegis"));
        QVERIFY(job.sourceFiles.isEmpty());
    }

    void testCapabilitiesWriteFlag() {
        BurnerCapabilities caps;
        QVERIFY(!caps.canWrite());
        caps.canWriteCDR = true;
        QVERIFY(caps.canWrite());
    }
};

QTEST_MAIN(TestUtilsDiscBurner)
#include "test_utils_discburner.moc"
