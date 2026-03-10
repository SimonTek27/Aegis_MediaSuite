// Utility tests for DiscLabelMaker basic API

#include <QtTest/QtTest>

#include "../src/disc_labelmaker.h"

using namespace Aegis;

class TestUtilsLabelMaker : public QObject {
    Q_OBJECT

private slots:
    void testDefaultTemplate() {
        DiscLabelMaker maker;
        auto geom = maker.geometry();
        QVERIFY(geom.size.width() > 0.0);
        QVERIFY(geom.size.height() > 0.0);
    }

    void testAvailableTemplatesNotEmpty() {
        DiscLabelMaker maker;
        auto list = maker.availableTemplates();
        QVERIFY(!list.isEmpty());
    }
};

QTEST_MAIN(TestUtilsLabelMaker)
#include "test_utils_labelmaker.moc"
