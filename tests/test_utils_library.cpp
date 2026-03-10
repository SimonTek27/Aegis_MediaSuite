// Utility tests for Library high-level API

#include <QtTest/QtTest>

#include "../src/library.h"

using namespace Aegis;

class TestUtilsLibrary : public QObject {
    Q_OBJECT

private slots:
    void testLibraryTrackDefaults() {
        LibraryTrack t;
        QVERIFY(!t.isValid());
        QCOMPARE(t.id, -1);
        QVERIFY(t.path.isEmpty());
    }

    void testStatisticsOnEmptyDb() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString dbPath = dir.filePath("library.db");
        Library lib(dbPath);
        auto stats = lib.statistics();
        QVERIFY(stats.contains("trackCount"));
    }
};

QTEST_MAIN(TestUtilsLibrary)
#include "test_utils_library.moc"
