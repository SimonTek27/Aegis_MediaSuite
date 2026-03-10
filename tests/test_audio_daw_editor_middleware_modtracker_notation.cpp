// Updated tests for Score/Staff/Measure and DAW/Notation/Tracker integration
// This file focuses on:
//  - basic Score / Staff / Measure API
//  - DAWEngine + NotationClip + ModTrackerClip integration

#include <QtTest/QtTest>

#include "../src/audio_daw.h"

using namespace Aegis;

// ─────────────────────────────────────────────────────────────────────────────
// Score / Staff / Measure tests
// ─────────────────────────────────────────────────────────────────────────────
class TestScore : public QObject {
    Q_OBJECT

private slots:
    void testScoreCreation() {
        Score score;
        QVERIFY(score.staves().empty());
        QCOMPARE(score.totalTicks(), 0);
    }

    void testAddStaffAndMeasure() {
        Score score;
        Staff* s = score.addStaff("Piano");
        QVERIFY(s != nullptr);
        QCOMPARE(static_cast<int>(score.staves().size()), 1);

        Measure* m = s->addMeasure(1);
        QVERIFY(m != nullptr);
        QCOMPARE(static_cast<int>(s->measures().size()), 1);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// DAW / Notation / Tracker integration tests
// ─────────────────────────────────────────────────────────────────────────────
class TestDawNotationTracker : public QObject {
    Q_OBJECT

private slots:
    void testAddTrackAndNotationClip() {
        DAWEngine daw;
        Track* track = daw.addTrack("Notation Track");
        QVERIFY(track != nullptr);

        NotationClip* clip = daw.createNotationClip();
        QVERIFY(clip != nullptr);

        // Attach to track via addClip
        track->addClip(std::unique_ptr<Clip>(clip));
        QCOMPARE(track->clipCount(), 1);
    }

    void testCreateTrackerClip() {
        DAWEngine daw;
        Track* track = daw.createTrackerTrack("Tracker");
        QVERIFY(track != nullptr);

        ModTrackerClip* tclip = daw.createTrackerClip();
        QVERIFY(tclip != nullptr);

        track->addClip(std::unique_ptr<Clip>(tclip));
        QCOMPARE(track->clipCount(), 1);
    }

    void testProcessAudioDoesNotCrash() {
        DAWEngine daw;
        daw.addTrack("Audio");

        const int frames = 512;
        QVector<float> buffer(frames * 2);
        daw.processAudio(buffer.data(), frames, 2, 48000);
        // No QVERIFY here: test passes if it doesn't crash
    }
};

QTEST_MAIN(TestScore)
#include "test_audio_daw_editor_middleware_modtracker_notation.moc"
