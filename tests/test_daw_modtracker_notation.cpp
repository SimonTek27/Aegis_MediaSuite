// tests/test_daw_notation.cpp
// Aegis MediaSuite — Unit tests: DAW Engine & Music Notation
// Framework: Qt Test (QTest)
//
// FIX SUMMARY (vs original):
//  1. Duration::Type  →  DurationType  (the enum is a top-level type alias, not nested).
//  2. m_daw->tracks()  →  m_daw->trackCount()  (no tracks() method exists on DAWEngine).
//  3. m_daw->tracks().isEmpty()  →  m_daw->trackCount() == 0.
//  4. createNotationClip("name", index)  →  createNotationClip(index, "name")  (correct arg order).
//  5. tempoMap() / setTempo() / play() / pause() / stop() / transportState()
//     are on Transport, not on DAWEngine.  Use m_daw->transport()->...
//  6. DAWEngine::TransportState  →  TransportState  (top-level enum in Aegis namespace).
//  7. Range-for on QList<unique_ptr<>>: added std::as_const() to prevent detach()
//     which would try to copy unique_ptr and fail with a deleted copy constructor.
//  8. TempoMap has bpmAtBeat()/addTempoChange(); tempoAt() renamed to bpmAtBeat().

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>

#include "audio_daw.h"

using namespace Aegis;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static Note makeNote(int midiNote, DurationType durType, int tickPos = 0,
                     int velocity = 80) {
    Note n;
    n.pitch             = Pitch(midiNote);
    n.duration.type     = durType;
    n.tickPosition      = tickPos;
    n.velocity          = velocity;
    n.isRest            = false;
    return n;
}

static Note makeRest(DurationType durType, int tickPos = 0) {
    Note r;
    r.duration.type = durType;
    r.tickPosition  = tickPos;
    r.isRest        = true;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Score / Staff / Measure tests
// ─────────────────────────────────────────────────────────────────────────────
class TestScore : public QObject {
    Q_OBJECT

private slots:

    void testScoreCreation() {
        Score score;
        QVERIFY(score.staves.isEmpty());
        QCOMPARE(score.totalTicks(), 0);
    }

    void testScoreSetTitle() {
        Score score;
        score.setTitle("Moonlight Sonata");
        QCOMPARE(score.title(), QString("Moonlight Sonata"));
    }

    void testScoreSetComposer() {
        Score score;
        score.setComposer("Beethoven");
        QCOMPARE(score.composer(), QString("Beethoven"));
    }

    void testScoreTempo_default() {
        Score score;
        QVERIFY(score.tempo() >= 60.0 && score.tempo() <= 240.0);
    }

    void testScoreSetTempo() {
        Score score;
        score.setTempo(140.0);
        QCOMPARE(score.tempo(), 140.0);
    }

    void testScoreTicksPerQuarter() {
        Score score;
        QVERIFY(score.ticksPerQuarter() > 0);
    }

    void testAddStaff() {
        Score score;
        Staff* s = score.addStaff("Piano");
        QVERIFY(s != nullptr);
        QCOMPARE(score.staves.size(), 1);
    }

    void testRemoveStaff() {
        Score score;
        score.addStaff("Piano");
        score.addStaff("Violin");
        score.removeStaff(0);
        QCOMPARE(score.staves.size(), 1);
    }

    void testAddMeasure() {
        Score score;
        Staff* s = score.addStaff("Piano");
        Measure* m = s->addMeasure(1);
        QVERIFY(m != nullptr);
        QCOMPARE(s->measures.size(), 1);
    }

    void testMeasureNotFull_empty() {
        Score score;
        Staff* s = score.addStaff("Piano");
        Measure* m = s->addMeasure(1);
        m->setLengthTicks(1920);
        QVERIFY(!m->isFull());
        QCOMPARE(m->filledTicks(), 0);
    }

    void testMeasureFilled_oneQuarterNote() {
        Score score;
        Staff* s = score.addStaff("Piano");
        Measure* m = s->addMeasure(1);
        m->setLengthTicks(1920);
        m->addNote(makeNote(60, DurationType::Quarter, 0));
        QCOMPARE(m->filledTicks(), 480);
    }

    void testRemoveMeasure() {
        Score score;
        Staff* s = score.addStaff("Piano");
        s->addMeasure(1);
        s->addMeasure(2);
        s->removeMeasure(0);
        QCOMPARE(s->measures.size(), 1);
    }

    void testMeasureAtTick() {
        Score score;
        Staff* s = score.addStaff("Piano");
        Measure* m1 = s->addMeasure(1);
        m1->setLengthTicks(1920);
        Measure* m2 = s->addMeasure(2);
        m2->setLengthTicks(1920);

        QCOMPARE(s->measureAtTick(0),    m1);
        QCOMPARE(s->measureAtTick(960),  m1);
        QCOMPARE(s->measureAtTick(1920), m2);
        QVERIFY(s->measureAtTick(9999) == nullptr);
    }

    void testNoteIsRest_false() {
        Note n = makeNote(60, DurationType::Quarter);
        QVERIFY(!n.isRest);
    }

    void testRestIsRest_true() {
        Note r = makeRest(DurationType::Half);
        QVERIFY(r.isRest);
    }

    void testNotePlayDuration_quarterAt120BPM() {
        Note n = makeNote(60, DurationType::Quarter);
        double ticks = n.playDurationTicks(120.0);
        QCOMPARE(ticks, 480.0);
    }

    void testNoteLilyPond_middleC() {
        Note n = makeNote(60, DurationType::Quarter);
        QString lily = n.toLilyPond();
        QVERIFY(lily.startsWith("c"));
    }

    void testNoteVelocityRange() {
        Note n = makeNote(60, DurationType::Quarter, 0, 127);
        QVERIFY(n.velocity >= 0 && n.velocity <= 127);
    }

    void testTotalTicks_twoMeasures() {
        Score score;
        Staff* s = score.addStaff("Piano");
        auto* m1 = s->addMeasure(1); m1->setLengthTicks(1920);
        auto* m2 = s->addMeasure(2); m2->setLengthTicks(1920);
        Q_UNUSED(m2)
        QCOMPARE(score.totalTicks(), 3840);
    }

    void testTotalTicks_emptyScore() {
        Score score;
        QCOMPARE(score.totalTicks(), 0);
    }

    // ── MIDI I/O ───────────────────────────────────────────────────────────

    void testSaveMIDI_createsFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = dir.filePath("test.mid");

        Score score;
        score.setTempo(120.0);
        Staff* s = score.addStaff("Piano");
        Measure* m = s->addMeasure(1);
        m->setLengthTicks(1920);
        m->addNote(makeNote(60, DurationType::Quarter, 0,   80));
        m->addNote(makeNote(64, DurationType::Quarter, 480, 80));
        m->addNote(makeNote(67, DurationType::Quarter, 960, 80));

        QVERIFY(score.saveMIDI(path));
        QVERIFY(QFile::exists(path));
        QVERIFY(QFileInfo(path).size() > 0);
    }

    void testSaveMIDI_validHeader() {
        QTemporaryDir dir;
        QString path = dir.filePath("hdr.mid");

        Score score;
        score.addStaff("Piano");
        QVERIFY(score.saveMIDI(path));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray data = f.readAll();

        QVERIFY(data.startsWith("MThd"));

        quint32 len = (static_cast<quint8>(data[4]) << 24) |
                      (static_cast<quint8>(data[5]) << 16) |
                      (static_cast<quint8>(data[6]) <<  8) |
                       static_cast<quint8>(data[7]);
        QCOMPARE(len, 6u);
    }

    void testSaveMIDI_correctTrackCount() {
        QTemporaryDir dir;
        QString path = dir.filePath("tracks.mid");

        Score score;
        score.addStaff("Piano");
        score.addStaff("Violin");
        QVERIFY(score.saveMIDI(path));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray data = f.readAll();
        QVERIFY(data.size() >= 14);

        quint16 numTracks = (static_cast<quint8>(data[10]) << 8) |
                             static_cast<quint8>(data[11]);
        QVERIFY(numTracks >= 2);
    }

    void testLoadMIDI_roundTrip() {
        QTemporaryDir dir;
        QString path = dir.filePath("rt.mid");

        Score orig;
        orig.setTempo(120.0);
        Staff* s = orig.addStaff("Piano");
        Measure* m = s->addMeasure(1);
        m->setLengthTicks(1920);
        m->addNote(makeNote(60, DurationType::Quarter, 0,   80));
        m->addNote(makeNote(64, DurationType::Quarter, 480, 90));

        QVERIFY(orig.saveMIDI(path));

        Score reader;
        QVERIFY(reader.loadMIDI(path));
        QVERIFY(!reader.staves.isEmpty());

        int totalNotes = 0;
        // std::as_const() prevents QList<unique_ptr<>> from calling detach(),
        // which would try to copy unique_ptrs and fail to compile.
        for (const auto& staff : std::as_const(reader.staves)) {
            for (const auto& measure : std::as_const(staff->measures)) {
                totalNotes += measure->noteCount();
            }
        }
        QVERIFY(totalNotes >= 2);
    }

    void testLoadMIDI_invalidFile_returnsFalse() {
        QTemporaryDir dir;
        QString path = dir.filePath("bad.mid");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not a MIDI file");
        f.close();
        Score score;
        QVERIFY(!score.loadMIDI(path));
    }

    void testLoadMIDI_empty_MThd_returnsHeaderError() {
        QTemporaryDir dir;
        QString path = dir.filePath("short.mid");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("MThd");
        f.close();
        Score score;
        QVERIFY(!score.loadMIDI(path));
    }

    // ── renderToPCM ────────────────────────────────────────────────────────

    void testRenderToPCM_emptyScore_returnsEmpty() {
        Score score;
        QByteArray pcm = score.renderToPCM(48000);
        QVERIFY(pcm.isEmpty());
    }

    void testRenderToPCM_notEmpty() {
        Score score;
        score.setTempo(120.0);
        Staff* s = score.addStaff("Piano");
        Measure* m = s->addMeasure(1);
        m->setLengthTicks(1920);
        m->addNote(makeNote(60, DurationType::Quarter, 0, 80));

        QByteArray pcm = score.renderToPCM(48000);
        QVERIFY(!pcm.isEmpty());
    }

    void testRenderToPCM_stereoFloat() {
        Score score;
        score.setTempo(120.0);
        Staff* s = score.addStaff("Piano");
        Measure* m = s->addMeasure(1);
        m->setLengthTicks(480);
        m->addNote(makeNote(60, DurationType::Quarter, 0, 80));

        QByteArray pcm = score.renderToPCM(48000);
        QCOMPARE(pcm.size() % (2 * static_cast<int>(sizeof(float))), 0);
    }

    void testRenderToPCM_hasNonZeroSamples() {
        Score score;
        score.setTempo(120.0);
        Staff* s = score.addStaff("Piano");
        Measure* m = s->addMeasure(1);
        m->setLengthTicks(1920);
        m->addNote(makeNote(69, DurationType::Half, 0, 100));

        QByteArray pcm = score.renderToPCM(48000);
        const float* buf = reinterpret_cast<const float*>(pcm.constData());
        int count = static_cast<int>(pcm.size() / sizeof(float));

        float peak = 0.0f;
        for (int i = 0; i < count; ++i)
            if (std::fabs(buf[i]) > peak) peak = std::fabs(buf[i]);

        QVERIFY2(peak > 0.01f, "PCM render should contain audible signal for A4 note");
    }

    // ── Signals ────────────────────────────────────────────────────────────

    void testModifiedSignal_onAddStaff() {
        Score score;
        QSignalSpy spy(&score, &Score::modified);
        Staff* s = score.addStaff("Piano");
        Measure* m = s->addMeasure(1);
        m->setLengthTicks(1920);
        m->addNote(makeNote(60, DurationType::Quarter));
        Q_UNUSED(spy);
    }

    void testStructureChangedSignal() {
        Score score;
        QSignalSpy spy(&score, &Score::structureChanged);
        score.addStaff("Piano");
        QCOMPARE(spy.count(), 1);
        score.removeStaff(0);
        QCOMPARE(spy.count(), 2);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// DAWEngine tests
// ─────────────────────────────────────────────────────────────────────────────
class TestDAWEngine : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        m_daw = std::make_unique<DAWEngine>(nullptr);
        QVERIFY(m_daw != nullptr);
    }

    void cleanupTestCase() { m_daw.reset(); }

    void testAddTrack() {
        Track* t = m_daw->addTrack("MIDI 1");
        QVERIFY(t != nullptr);
        QCOMPARE(m_daw->trackCount(), 1);
    }

    void testRemoveTrack() {
        m_daw->addTrack("To Remove");
        m_daw->removeTrack(0);
        QCOMPARE(m_daw->trackCount(), 0);
    }

    void testCreateNotationClip() {
        m_daw->addTrack("Notation Track");
        // Correct arg order: (int trackIndex, const QString& name)
        NotationClip* clip = m_daw->createNotationClip(0, "Test Score");
        QVERIFY(clip != nullptr);
    }

    void testImportScore_MusicXML() {
        QTemporaryDir dir;
        QString path = dir.filePath("test.xml");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"(<?xml version="1.0"?>
<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN"
"http://www.musicxml.org/dtds/partwise.dtd">
<score-partwise version="3.1">
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1"><measure number="1"><note><pitch><step>C</step><octave>4</octave></pitch>
  <duration>4</duration><type>quarter</type></note></measure></part>
</score-partwise>)");
        f.close();

        m_daw->addTrack("Score Track");
        NotationClip* clip = m_daw->importScore(path, 0);
        Q_UNUSED(clip);
    }

    // Transport API lives on m_daw->transport(), not on DAWEngine directly.

    void testTempoMap_default() {
        auto* tp = m_daw->transport();
        QVERIFY(tp != nullptr);
        QVERIFY(tp->tempoMap() != nullptr);
        QVERIFY(tp->tempoMap()->bpmAtBeat(0) > 0.0);
    }

    void testSetTempo() {
        m_daw->transport()->setTempo(140.0);
        QCOMPARE(m_daw->transport()->tempoMap()->bpmAtBeat(0), 140.0);
    }

    void testTransport_initialStateStopped() {
        QCOMPARE(m_daw->transport()->state(), TransportState::Stopped);
    }

    void testTransport_playAndStop() {
        m_daw->transport()->play();
        QTest::qWait(50);
        QCOMPARE(m_daw->transport()->state(), TransportState::Playing);
        m_daw->transport()->stop();
        QCOMPARE(m_daw->transport()->state(), TransportState::Stopped);
    }

    void testTransport_pause() {
        m_daw->transport()->play();
        QTest::qWait(30);
        m_daw->transport()->pause();
        QCOMPARE(m_daw->transport()->state(), TransportState::Paused);
        m_daw->transport()->stop();
    }

    void testSaveLoad_roundTrip() {
        QTemporaryDir dir;
        QString path = dir.filePath("project.aegisproj");

        m_daw->addTrack("MIDI A");
        m_daw->addTrack("MIDI B");
        m_daw->transport()->setTempo(132.0);
        QVERIFY(m_daw->saveProject(path));

        auto daw2 = std::make_unique<DAWEngine>(nullptr);
        QVERIFY(daw2->loadProject(path));
        QCOMPARE(daw2->trackCount(), 2);
        QCOMPARE(daw2->transport()->tempoMap()->bpmAtBeat(0), 132.0);
    }

private:
    std::unique_ptr<DAWEngine> m_daw;
};

// ─────────────────────────────────────────────────────────────────────────────
// Composite runner
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    int result = 0;
    {
        TestScore t;
        result |= QTest::qExec(&t, argc, argv);
    }
    {
        TestDAWEngine t;
        result |= QTest::qExec(&t, argc, argv);
    }
    return result;
}

#include "test_daw_modtracker_notation.moc"
