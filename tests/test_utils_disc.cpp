// Utility tests for Disc/DiscRipper

#include <QtTest/QtTest>

#include "../src/disc.h"

using namespace Aegis;

class TestUtilsDisc : public QObject {
    Q_OBJECT

private slots:
    void testDiscInfoDefaults() {
        DiscInfo info;
        QCOMPARE(info.totalTracks, 0);
        QVERIFY(info.tracks.isEmpty());
        QVERIFY(!info.isAudioCD());
    }

    void testDiscTrackDefaults() {
        DiscTrack t;
        QCOMPARE(t.number, 0);
        QCOMPARE(t.channels, 2);
        QCOMPARE(t.sampleRate, 44100);
        QVERIFY(t.title.isEmpty());
    }

    void testDiscLoggerOutput() {
        DiscLogger logger("TestDisc");
        logger.info("info");
        logger.debug("debug");
        logger.warning("warn");
        logger.error("err");
        QVERIFY(true); // just ensure no crash
    }
};

QTEST_MAIN(TestUtilsDisc)
#include "test_utils_disc.moc"
