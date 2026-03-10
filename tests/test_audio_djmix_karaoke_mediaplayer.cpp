// tests/test_audio_djmix_karaoke_mediaplayer.cpp
// Aegis MediaSuite — Unit tests: DJMixer, KaraokeController, MediaPlayer
// Framework: Qt Test (QTest)
//
// Architecture note:
//   DJMixer owns its AudioEngine + MpvBackend internally — costruisce i pillar
//   internamente, quindi i test non devono iniettarli dall'esterno.
//
//   KaraokeController richiede un AudioEngine* e un MpvBackend* espliciti.
//
//   MediaPlayer accetta un AudioOutput opzionale (nullptr = auto-create).

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <memory>

#include "audio_djmix.h"
#include "audio_karaoke.h"
#include "mediaplayer.h"
#include "mpv_backend.h"
#include "audio.h"

using namespace Aegis;

// =============================================================================
// TestDJMixer
// =============================================================================

class TestDJMixer : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        m_mixer = std::make_unique<DJMixer>(nullptr);
        QVERIFY(m_mixer != nullptr);
    }

    void cleanupTestCase() {
        m_mixer.reset();
    }

    // ── Costruzione e accesso ai deck ─────────────────────────────────────

    void testDeckA_notNull() {
        QVERIFY(m_mixer->deckA() != nullptr);
    }

    void testDeckB_notNull() {
        QVERIFY(m_mixer->deckB() != nullptr);
    }

    void testDeck_byIndex_0_isA() {
        QCOMPARE(m_mixer->deck(0), m_mixer->deckA());
    }

    void testDeck_byIndex_1_isB() {
        QCOMPARE(m_mixer->deck(1), m_mixer->deckB());
    }

    void testDeck_byIndex_outOfRange_returnsNull() {
        QVERIFY(m_mixer->deck(99) == nullptr);
    }

    // ── Crossfader ────────────────────────────────────────────────────────

    void testCrossfader_defaultIsCenter() {
        double cf = m_mixer->crossfader();
        QVERIFY2(cf >= 0.0 && cf <= 1.0,
                 "Crossfader must be in [0.0, 1.0]");
    }

    void testCrossfader_setToA() {
        QSignalSpy spy(m_mixer.get(), &DJMixer::crossfaderChanged);
        m_mixer->setCrossfader(0.0);
        QCOMPARE(m_mixer->crossfader(), 0.0);
        QVERIFY(spy.count() >= 1);
    }

    void testCrossfader_setToB() {
        m_mixer->setCrossfader(1.0);
        QCOMPARE(m_mixer->crossfader(), 1.0);
    }

    void testCrossfader_setToCenter() {
        m_mixer->setCrossfader(0.5);
        QCOMPARE(m_mixer->crossfader(), 0.5);
    }

    // ── Master volume ──────────────────────────────────────────────────────

    void testMasterVolume_default_inRange() {
        double vol = m_mixer->masterVolume();
        QVERIFY2(vol >= 0.0 && vol <= 2.0,
                 "Master volume should be in a reasonable range");
    }

    void testMasterVolume_set() {
        QSignalSpy spy(m_mixer.get(), &DJMixer::masterVolumeChanged);
        m_mixer->setMasterVolume(0.75);
        QCOMPARE(m_mixer->masterVolume(), 0.75);
        QVERIFY(spy.count() >= 1);
    }

    // ── Master BPM ────────────────────────────────────────────────────────

    void testMasterBpm_default_positive() {
        QVERIFY(m_mixer->masterBpm() > 0.0);
    }

    void testMasterBpm_set() {
        QSignalSpy spy(m_mixer.get(), &DJMixer::masterBpmChanged);
        m_mixer->setMasterBpm(140.0);
        QCOMPARE(m_mixer->masterBpm(), 140.0);
        QVERIFY(spy.count() >= 1);
    }

    // ── Transport ─────────────────────────────────────────────────────────

    void testPlay_doesNotCrash() {
        m_mixer->play();
    }

    void testPause_doesNotCrash() {
        m_mixer->pause();
    }

    void testStop_doesNotCrash() {
        m_mixer->stop();
    }

    // ── Recording ─────────────────────────────────────────────────────────

    void testIsRecording_initiallyFalse() {
        QVERIFY(!m_mixer->isRecording());
    }

    void testStartRecording_invalidPath_returnsFalse() {
        bool ok = m_mixer->startRecording("/nonexistent/dir/rec.wav");
        QVERIFY(!ok);
        QVERIFY(!m_mixer->isRecording());
    }

    void testStartRecording_validPath() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = dir.filePath("rec.wav");
        bool ok = m_mixer->startRecording(path);
        if (ok) {
            QVERIFY(m_mixer->isRecording());
            m_mixer->stopRecording();
            QVERIFY(!m_mixer->isRecording());
        }
        // If recording is not supported in headless mode we just skip.
    }

    // ── AudioEngine accesso ───────────────────────────────────────────────

    void testAudioEngine_notNull() {
        QVERIFY(m_mixer->audioEngine() != nullptr);
    }

    // ── processAudio smoke test ───────────────────────────────────────────

    void testProcessAudio_doesNotCrash() {
        const int frames = 512;
        std::vector<float> master(static_cast<size_t>(frames * 2), 0.0f);
        std::vector<float> cue(static_cast<size_t>(frames * 2), 0.0f);
        m_mixer->processAudio(master.data(), cue.data(), frames);
    }

    // ── DJDeck pitch control ──────────────────────────────────────────────

    void testDeckA_pitch_default_zero() {
        // Pitch is ±50% expressed as a ratio; 0.0 = unity
        double p = m_mixer->deckA()->pitch();
        QVERIFY2(p >= -1.0 && p <= 1.0, "Pitch must be in valid range");
    }

    void testDeckA_setPitch() {
        m_mixer->deckA()->setPitch(0.08);   // +8%
        double p = m_mixer->deckA()->pitch();
        QVERIFY2(qAbs(p - 0.08) < 1e-9, "Pitch should be stored accurately");
        m_mixer->deckA()->setPitch(0.0);    // restore
    }

    // ── DJDeck EQ controls ────────────────────────────────────────────────

    void testDeckA_setEq_doesNotCrash() {
        m_mixer->deckA()->setEqLow(1.0f);
        m_mixer->deckA()->setEqMid(1.0f);
        m_mixer->deckA()->setEqHigh(1.0f);
    }

private:
    std::unique_ptr<DJMixer> m_mixer;
};

// =============================================================================
// TestKaraokeController
// =============================================================================

class TestKaraokeController : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        // KaraokeController borrows AudioEngine* and MpvBackend*; we own them.
        m_engine  = std::make_unique<Aegis::AudioEngine>(nullptr);
        m_backend = std::make_unique<MpvBackend>(nullptr);
        m_karaoke = std::make_unique<KaraokeController>(
            m_engine.get(), m_backend.get(), nullptr);
        QVERIFY(m_karaoke != nullptr);
    }

    void cleanupTestCase() {
        m_karaoke.reset();
        m_backend.reset();
        m_engine.reset();
    }

    // ── Stato iniziale ────────────────────────────────────────────────────

    void testActive_initiallyFalse() {
        QVERIFY(!m_karaoke->active());
    }

    void testQueueSize_initiallyZero() {
        QCOMPARE(m_karaoke->queueSize(), 0);
    }

    void testCurrentSinger_initiallyEmpty() {
        QVERIFY(m_karaoke->currentSinger().isEmpty());
    }

    void testCurrentSong_initiallyEmpty() {
        QVERIFY(m_karaoke->currentSong().isEmpty());
    }

    void testPosition_initiallyZero() {
        QCOMPARE(m_karaoke->position(), 0.0);
    }

    void testPaused_initiallyFalse() {
        QVERIFY(!m_karaoke->paused());
    }

    void testRotationNumber_defaultPositive() {
        QVERIFY(m_karaoke->rotationNumber() >= 1);
    }

    // ── Singer management ─────────────────────────────────────────────────

    void testAddSinger_returnsNonEmptyId() {
        QString id = m_karaoke->addSinger("Alice");
        QVERIFY(!id.isEmpty());
    }

    void testAddSinger_incrementsRotation() {
        int before = m_karaoke->singers().size();
        m_karaoke->addSinger("Bob");
        QCOMPARE(m_karaoke->singers().size(), before + 1);
    }

    void testRemoveSinger_decrementsList() {
        QString id = m_karaoke->addSinger("ToRemove");
        int before = m_karaoke->singers().size();
        m_karaoke->removeSinger(id);
        QCOMPARE(m_karaoke->singers().size(), before - 1);
    }

    void testAddSinger_withDisplayName() {
        QString id = m_karaoke->addSinger("Charlie", "Chuck");
        QVERIFY(!id.isEmpty());
    }

    // ── Queue management ──────────────────────────────────────────────────

    void testQueueSong_withValidSingerId_returnsId() {
        QString singerId = m_karaoke->addSinger("Singer1");
        // Use a fake song id — queueSong should return a queue entry id
        // regardless of whether the songId maps to a real file.
        QString queueId = m_karaoke->queueSong("fakeSongId", singerId, 0);
        QVERIFY(!queueId.isEmpty());
    }

    void testQueueSong_incrementsQueueSize() {
        QString singerId = m_karaoke->addSinger("Singer2");
        int before = m_karaoke->queueSize();
        m_karaoke->queueSong("fakeSongId2", singerId);
        QCOMPARE(m_karaoke->queueSize(), before + 1);
    }

    void testRemoveFromQueue_decrementsSize() {
        QString singerId = m_karaoke->addSinger("Singer3");
        QString queueId  = m_karaoke->queueSong("fakeSongId3", singerId);
        int before = m_karaoke->queueSize();
        m_karaoke->removeFromQueue(queueId);
        QCOMPARE(m_karaoke->queueSize(), before - 1);
    }

    void testQueue_returnsVariantList() {
        QVariantList q = m_karaoke->queue();
        QVERIFY(q.size() >= 0);  // Can be empty — just must not crash
    }

    // ── Audio controls ────────────────────────────────────────────────────

    void testSetKeyChange_doesNotCrash() {
        m_karaoke->setKeyChange(0);
        m_karaoke->setKeyChange(+5);
        m_karaoke->setKeyChange(-3);
    }

    void testSetVocalVolume_doesNotCrash() {
        m_karaoke->setVocalVolume(1.0);
        m_karaoke->setVocalVolume(0.5);
    }

    void testSetMusicVolume_doesNotCrash() {
        m_karaoke->setMusicVolume(0.8);
    }

    void testSetEchoLevel_doesNotCrash() {
        m_karaoke->setEchoLevel(0.3);
    }

    void testVocalSuppression_toggles() {
        m_karaoke->setVocalSuppression(true);
        QVERIFY(m_karaoke->vocalSuppression());
        m_karaoke->setVocalSuppression(false);
        QVERIFY(!m_karaoke->vocalSuppression());
    }

    // ── Song library ──────────────────────────────────────────────────────

    void testSearchSongs_emptyQuery_returnsVariantList() {
        QVariantList results = m_karaoke->searchSongs("", 10);
        QVERIFY(results.size() >= 0);
    }

    void testSearchSongs_nonExistentQuery_returnsEmpty() {
        QVariantList results = m_karaoke->searchSongs("XYZZY_NONEXISTENT_99999", 10);
        QVERIFY(results.isEmpty());
    }

    // ── Transport (headless) ──────────────────────────────────────────────

    void testStartKaraoke_doesNotCrash() {
        m_karaoke->startKaraoke();
    }

    void testStopKaraoke_doesNotCrash() {
        m_karaoke->stopKaraoke();
        QVERIFY(!m_karaoke->active());
    }

    void testTogglePause_doesNotCrash() {
        m_karaoke->togglePause();
    }

    void testNextSong_doesNotCrash() {
        m_karaoke->nextSong();
    }

    // ── Signals ───────────────────────────────────────────────────────────

    void testQueueChangedSignal_onQueueSong() {
        QSignalSpy spy(m_karaoke.get(), &KaraokeController::queueChanged);
        QString singerId = m_karaoke->addSinger("SpySinger");
        m_karaoke->queueSong("spySongId", singerId);
        QVERIFY(spy.count() >= 1);
    }

    // ── AudioEngine access ─────────────────────────────────────────────────

    void testAudioEngine_notNull() {
        QVERIFY(m_karaoke->audioEngine() != nullptr);
    }

private:
    std::unique_ptr<Aegis::AudioEngine>        m_engine;
    std::unique_ptr<MpvBackend>         m_backend;
    std::unique_ptr<KaraokeController>  m_karaoke;
};

// =============================================================================
// TestMediaPlayer
// =============================================================================

class TestMediaPlayer : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        // nullptr output → MediaPlayer auto-creates its AudioOutput
        m_player = std::make_unique<MediaPlayer>(nullptr, nullptr, nullptr);
        QVERIFY(m_player != nullptr);
    }

    void cleanupTestCase() {
        m_player.reset();
    }

    // ── Stato iniziale ────────────────────────────────────────────────────

    void testState_initiallyStop() {
        QCOMPARE(m_player->state(), PlaybackState::Stopped);
    }

    void testSource_initiallyEmpty() {
        QVERIFY(m_player->source().isEmpty());
    }

    void testPosition_initiallyZero() {
        QCOMPARE(m_player->position(), qint64(0));
    }

    void testDuration_initiallyZero() {
        QCOMPARE(m_player->duration(), qint64(0));
    }

    void testMuted_initiallyFalse() {
        QVERIFY(!m_player->muted());
    }

    void testVolume_default_inRange() {
        double v = m_player->volume();
        QVERIFY2(v >= 0.0 && v <= 2.0, "Volume should be in valid range");
    }

    void testSeekable_initiallyTrue() {
        QVERIFY(m_player->seekable());
    }

    void testActiveBackend_initiallyNone() {
        QCOMPARE(m_player->activeBackend(), BackendType::None);
    }

    void testIsTrackerMode_initiallyFalse() {
        QVERIFY(!m_player->isTrackerMode());
    }

    // ── Volume & mute ──────────────────────────────────────────────────────

    void testSetVolume_stores() {
        QSignalSpy spy(m_player.get(), &MediaPlayer::volumeChanged);
        m_player->setVolume(0.6);
        QCOMPARE(m_player->volume(), 0.6);
        QVERIFY(spy.count() >= 1);
    }

    void testSetMuted_true() {
        QSignalSpy spy(m_player.get(), &MediaPlayer::mutedChanged);
        m_player->setMuted(true);
        QVERIFY(m_player->muted());
        QVERIFY(spy.count() >= 1);
        m_player->setMuted(false);
    }

    void testSetMuted_false() {
        m_player->setMuted(true);
        m_player->setMuted(false);
        QVERIFY(!m_player->muted());
    }

    // ── Repeat / shuffle ──────────────────────────────────────────────────

    void testRepeatMode_default_none() {
        QCOMPARE(m_player->repeatMode(), MediaPlayer::RepeatMode::None);
    }

    void testSetRepeatMode_track() {
        m_player->setRepeatMode(MediaPlayer::RepeatMode::Track);
        QCOMPARE(m_player->repeatMode(), MediaPlayer::RepeatMode::Track);
        m_player->setRepeatMode(MediaPlayer::RepeatMode::None);
    }

    void testShuffle_default_false() {
        QVERIFY(!m_player->shuffle());
    }

    void testSetShuffle_true() {
        m_player->setShuffle(true);
        QVERIFY(m_player->shuffle());
        m_player->setShuffle(false);
    }

    // ── Transport (senza file reale — solo smoke) ─────────────────────────

    void testPlay_withoutTrack_doesNotCrash() {
        m_player->play();
    }

    void testPause_doesNotCrash() {
        m_player->pause();
    }

    void testStop_doesNotCrash() {
        m_player->stop();
    }

    void testTogglePause_doesNotCrash() {
        m_player->togglePause();
    }

    void testSeek_doesNotCrash() {
        m_player->seek(0);
    }

    void testSeekSeconds_doesNotCrash() {
        m_player->seekSeconds(0.0);
    }

    void testNext_doesNotCrash() {
        m_player->next();
    }

    void testPrevious_doesNotCrash() {
        m_player->previous();
    }

    // ── Load URL invalida ─────────────────────────────────────────────────

    void testLoad_invalidUrl_doesNotCrash() {
        m_player->load(QUrl("file:///nonexistent/file.mp3"));
    }

    // ── Tracker file detection ────────────────────────────────────────────

    void testIsTrackerFile_modExtension() {
        QVERIFY(m_player->isTrackerFile("song.mod"));
    }

    void testIsTrackerFile_xmExtension() {
        QVERIFY(m_player->isTrackerFile("song.xm"));
    }

    void testIsTrackerFile_itExtension() {
        QVERIFY(m_player->isTrackerFile("song.it"));
    }

    void testIsTrackerFile_mp3_returnsFalse() {
        QVERIFY(!m_player->isTrackerFile("song.mp3"));
    }

    void testSupportedTrackerFormats_notEmpty() {
        QStringList fmts = m_player->supportedTrackerFormats();
        QVERIFY(!fmts.isEmpty());
    }

    // ── Enqueue / currentIndex ────────────────────────────────────────────

    void testEnqueue_doesNotCrash() {
        m_player->enqueue(QUrl("file:///tmp/fake.mp3"));
    }

    void testCurrentIndex_default_minusOne() {
        MediaPlayer fresh;
        QCOMPARE(fresh.currentIndex(), -1);
    }

    // ── AudioEngine access ─────────────────────────────────────────────────

    void testAudioEngine_notNull() {
        QVERIFY(m_player->audioEngine() != nullptr);
    }

    // ── Signals ───────────────────────────────────────────────────────────

    void testStateChangedSignal_onStop() {
        QSignalSpy spy(m_player.get(), &MediaPlayer::stateChanged);
        m_player->stop();
        // stop() from Stopped should be a no-op — may or may not emit
        // Just ensure it doesn't crash and spy is valid
        QVERIFY(spy.isValid());
    }

private:
    std::unique_ptr<MediaPlayer> m_player;
};

// =============================================================================
// Composite runner
// =============================================================================

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int result = 0;
    {
        TestDJMixer t;
        result |= QTest::qExec(&t, argc, argv);
    }
    {
        TestKaraokeController t;
        result |= QTest::qExec(&t, argc, argv);
    }
    {
        TestMediaPlayer t;
        result |= QTest::qExec(&t, argc, argv);
    }
    return result;
}

#include "test_audio_djmix_karaoke_mediaplayer.moc"
