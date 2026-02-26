// tests/test_video_editor.cpp
// Aegis MediaSuite — Unit tests: Video Editor
// Framework: Qt Test (QTest)
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "videoeditor.h"

using namespace Aegis;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static ProjectProfile defaultProfile() {
    ProjectProfile p;
    p.width     = 1920;
    p.height    = 1080;
    p.fps       = 30;
    p.sampleRate= 48000;
    return p;
}

static ExportSettings h264Settings() {
    ExportSettings s;
    s.videoCodec   = "libx264";
    s.audioCodec   = "aac";
    s.container    = "mp4";
    s.width        = 1920;
    s.height       = 1080;
    s.fps          = 30;
    s.videoBitrate = 8000000;
    s.audioBitrate = 192000;
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test class
// ─────────────────────────────────────────────────────────────────────────────
class TestVideoEditor : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        m_editor = std::make_unique<VideoEditor>(nullptr);
        QVERIFY(m_editor != nullptr);
    }

    void cleanupTestCase() {
        m_editor.reset();
    }

    void init() {
        // Each test starts with a fresh project
        m_editor->newProject("TestProject", defaultProfile());
    }

    void cleanup() {
        m_editor->closeProject();
    }

    // ── Project lifecycle ──────────────────────────────────────────────────

    void testNewProject_hasProject() {
        QVERIFY(m_editor->hasProject());
    }

    void testNewProject_name() {
        QCOMPARE(m_editor->projectName(), QString("TestProject"));
    }

    void testNewProject_notModified() {
        QVERIFY(!m_editor->isModified());
    }

    void testNewProject_profile() {
        ProjectProfile p = m_editor->profile();
        QCOMPARE(p.width,  1920);
        QCOMPARE(p.height, 1080);
        QCOMPARE(p.fps,    30);
    }

    void testCloseProject_noProject() {
        m_editor->closeProject();
        QVERIFY(!m_editor->hasProject());
    }

    void testSaveAndOpenProject() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = dir.filePath("test.aegisvid");

        QVERIFY(m_editor->saveProject(path));

        m_editor->closeProject();
        QVERIFY(m_editor->openProject(path));
        QVERIFY(m_editor->hasProject());
        QCOMPARE(m_editor->projectName(), QString("TestProject"));
    }

    void testOpenNonExistentProject_returnsFalse() {
        bool ok = m_editor->openProject("/nonexistent/path/project.aegisvid");
        QVERIFY(!ok);
    }

    // ── Track management ───────────────────────────────────────────────────

    void testAddVideoTrack() {
        Track* t = m_editor->addVideoTrack("Video 1");
        QVERIFY(t != nullptr);
        QCOMPARE(t->type(), Track::Type::Video);
        QCOMPARE(t->name(), QString("Video 1"));
    }

    void testAddAudioTrack() {
        Track* t = m_editor->addAudioTrack("Audio 1");
        QVERIFY(t != nullptr);
        QCOMPARE(t->type(), Track::Type::Audio);
    }

    void testAddVideoTrack_marksModified() {
        m_editor->addVideoTrack("V");
        QVERIFY(m_editor->isModified());
    }

    void testRemoveTrack() {
        Track* t = m_editor->addVideoTrack("To Remove");
        QVERIFY(t != nullptr);
        int before = m_editor->videoTracks().size();
        m_editor->removeTrack(t);
        QCOMPARE(m_editor->videoTracks().size(), before - 1);
    }

    void testTrackMute() {
        Track* t = m_editor->addVideoTrack("Mute Test");
        QVERIFY(t != nullptr);
        t->setMuted(true);
        QVERIFY(t->isMuted());
        t->setMuted(false);
        QVERIFY(!t->isMuted());
    }

    void testTrackLock() {
        Track* t = m_editor->addVideoTrack("Lock Test");
        QVERIFY(t != nullptr);
        t->setLocked(true);
        QVERIFY(t->isLocked());
    }

    void testMultipleVideoTracks() {
        m_editor->addVideoTrack("V1");
        m_editor->addVideoTrack("V2");
        m_editor->addVideoTrack("V3");
        QCOMPARE(m_editor->videoTracks().size(), 3);
    }

    // ── Clip management ────────────────────────────────────────────────────

    void testImportClip_validFile() {
        // Create a temporary dummy video file (empty is fine for import metadata test)
        QTemporaryFile tmp;
        tmp.setFileTemplate(tmp.fileTemplate() + ".mp4");
        QVERIFY(tmp.open());

        Track* track = m_editor->addVideoTrack("V");
        Timecode pos{0, 30};
        auto clip = m_editor->importClip(QUrl::fromLocalFile(tmp.fileName()), pos);
        QVERIFY(clip != nullptr);
    }

    void testImportClip_invalidFile_returnsNull() {
        Track* track = m_editor->addVideoTrack("V");
        Timecode pos{0, 30};
        auto clip = m_editor->importClip(QUrl("file:///nonexistent/clip.mp4"), pos);
        QVERIFY(clip == nullptr);
    }

    void testClipPosition() {
        QTemporaryFile tmp;
        tmp.setFileTemplate(tmp.fileTemplate() + ".mp4");
        QVERIFY(tmp.open());

        m_editor->addVideoTrack("V");
        Timecode pos{90, 30};  // 3 seconds in
        auto clip = m_editor->importClip(QUrl::fromLocalFile(tmp.fileName()), pos);
        if (clip) {
            QCOMPARE(clip->position().frames, pos.frames);
        }
    }

    void testSplitClip() {
        QTemporaryFile tmp;
        tmp.setFileTemplate(tmp.fileTemplate() + ".mp4");
        QVERIFY(tmp.open());

        m_editor->addVideoTrack("V");
        Timecode pos{0, 30};
        auto clip = m_editor->importClip(QUrl::fromLocalFile(tmp.fileName()), pos);
        if (!clip) QSKIP("Clip import not supported for empty file");

        int trackClipsBefore = m_editor->videoTracks().first()->clips().size();
        Timecode splitAt{60, 30};  // 2 seconds
        m_editor->splitClip(clip, splitAt);
        int trackClipsAfter = m_editor->videoTracks().first()->clips().size();
        QCOMPARE(trackClipsAfter, trackClipsBefore + 1);
    }

    // ── Timecode ───────────────────────────────────────────────────────────

    void testTimecode_fromSeconds() {
        Timecode tc = Timecode::fromSeconds(2.5, 30);
        QCOMPARE(tc.frames, 75);  // 2.5 * 30
        QCOMPARE(tc.fps, 30);
    }

    void testTimecode_toSeconds() {
        Timecode tc{90, 30};
        QCOMPARE(tc.toSeconds(), 3.0);
    }

    void testTimecode_fromMicroseconds() {
        Timecode tc = Timecode::fromMicroseconds(3000000, 30);  // 3 seconds
        QCOMPARE(tc.frames, 90);
    }

    void testTimecode_comparison_less() {
        Timecode a{30, 30};
        Timecode b{60, 30};
        QVERIFY(a < b);
        QVERIFY(!(b < a));
    }

    void testTimecode_comparison_equal() {
        Timecode a{45, 30};
        Timecode b{45, 30};
        QVERIFY(a == b);
    }

    void testTimecode_toString_30fps() {
        Timecode tc{1800, 30};  // 60 seconds = 00:01:00:00
        QString s = tc.toString();
        QCOMPARE(s, QString("00:01:00:00"));
    }

    void testTimecode_toString_frames() {
        Timecode tc{45, 30};  // 1.5 seconds = 00:00:01:15
        QCOMPARE(tc.toString(), QString("00:00:01:15"));
    }

    // ── Export settings ────────────────────────────────────────────────────

    void testExportSettings_H264Preset() {
        ExportSettings s = ExportSettings::preset(ExportPreset::YouTube1080p);
        QCOMPARE(s.width,  1920);
        QCOMPARE(s.height, 1080);
        QCOMPARE(s.fps,    30);
        QVERIFY(!s.videoCodec.isEmpty());
    }

    void testExportSettings_4KPreset() {
        ExportSettings s = ExportSettings::preset(ExportPreset::YouTube4K);
        QCOMPARE(s.width,  3840);
        QCOMPARE(s.height, 2160);
    }

    void testExportSettings_FFmpegArgs_containsOutput() {
        ExportSettings s = h264Settings();
        QStringList args = s.toFFmpegArgs("/tmp/in.mp4", "/tmp/out.mp4");
        QVERIFY(!args.isEmpty());
        QVERIFY(args.contains("/tmp/out.mp4"));
    }

    void testExportSettings_FFmpegArgs_containsCodec() {
        ExportSettings s = h264Settings();
        QStringList args = s.toFFmpegArgs("in.mp4", "out.mp4");
        QVERIFY(args.join(" ").contains("libx264"));
    }

    // ── Undo / Redo ────────────────────────────────────────────────────────

    void testUndo_afterAddTrack() {
        QVERIFY(!m_editor->canUndo());
        m_editor->addVideoTrack("Undo Test");
        QVERIFY(m_editor->canUndo());
        m_editor->undo();
        QVERIFY(m_editor->videoTracks().isEmpty());
    }

    void testRedo_afterUndo() {
        m_editor->addVideoTrack("Redo Test");
        m_editor->undo();
        QVERIFY(m_editor->canRedo());
        m_editor->redo();
        QCOMPARE(m_editor->videoTracks().size(), 1);
    }

    void testUndo_stackLimit() {
        // Push 60 operations; stack limit is 50
        for (int i = 0; i < 60; ++i)
            m_editor->addVideoTrack(QString("T%1").arg(i));
        // Should not crash; undo should be available up to the limit
        QVERIFY(m_editor->canUndo());
    }

    // ── Markers ────────────────────────────────────────────────────────────

    void testAddMarker() {
        Timecode pos{30, 30};
        m_editor->addMarker(pos, "Chapter 1", Qt::yellow, "chapter");
        QCOMPARE(m_editor->markers().size(), 1);
        QCOMPARE(m_editor->markers().first().label, QString("Chapter 1"));
    }

    void testRemoveMarker() {
        Timecode pos{60, 30};
        m_editor->addMarker(pos, "Temp");
        m_editor->removeMarker(pos);
        QVERIFY(m_editor->markers().isEmpty());
    }

    void testMultipleMarkers_orderedByPosition() {
        m_editor->addMarker(Timecode{90, 30}, "C");
        m_editor->addMarker(Timecode{30, 30}, "A");
        m_editor->addMarker(Timecode{60, 30}, "B");
        auto markers = m_editor->markers();
        QCOMPARE(markers.size(), 3);
        QVERIFY(markers[0].position.frames <= markers[1].position.frames);
        QVERIFY(markers[1].position.frames <= markers[2].position.frames);
    }

    // ── Signals ────────────────────────────────────────────────────────────

    void testTrackAddedSignal() {
        QSignalSpy spy(m_editor.get(), &VideoEditor::trackAdded);
        m_editor->addVideoTrack("Signal Test");
        QCOMPARE(spy.count(), 1);
    }

    void testModifiedChangedSignal() {
        QSignalSpy spy(m_editor.get(), &VideoEditor::modifiedChanged);
        m_editor->addVideoTrack("Mod Signal");
        QVERIFY(spy.count() >= 1);
    }

    void testExportProgress_emittedDuringExport() {
        // This is a lightweight smoke-test; actual render is mocked by empty project
        QSignalSpy spy(m_editor.get(), &VideoEditor::exportProgress);
        QTemporaryDir dir;
        ExportSettings s = h264Settings();
        Timecode start{0, 30};
        Timecode end{30, 30};
        // May return false for empty project; that's acceptable. We only check no crash.
        m_editor->exportVideo(dir.filePath("out.mp4"), s, start, end);
        // Wait briefly for async progress signals
        QTest::qWait(200);
        // We don't mandate spy.count() > 0 here since project is empty,
        // but the call must not crash.
    }

private:
    std::unique_ptr<VideoEditor> m_editor;
};

QTEST_MAIN(TestVideoEditor)
#include "test_video_editor.moc"
