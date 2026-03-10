// Utility tests for Platform helpers and adaptors

#include <QtTest/QtTest>

#include "../src/platform.h"

class TestUtilsPlatform : public QObject {
    Q_OBJECT

private slots:
    void testMprisIdentity() {
        QObject dummyPlayer;
        MprisAdaptor adaptor(&dummyPlayer, nullptr);
        QCOMPARE(adaptor.identity(), QString("Aegis Media Player"));
    }

    void testAdminAdaptorProperties() {
        // We can't construct real Core/Library here without full runtime,
        // so only verify that the adaptor can be instantiated with nullptrs
        AegisAdminAdaptor adaptor(nullptr, nullptr, nullptr);
        // calling getters should not crash
        adaptor.trackCount();
        adaptor.playing();
        adaptor.volume();
        adaptor.currentFile();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestUtilsPlatform)
#include "test_utils_platform.moc"
