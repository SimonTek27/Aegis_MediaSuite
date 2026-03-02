// karaoke.cpp - Karaoke implementation
#include "karaoke.h"
#include "audio.h"
#include "mpv_backend.h"
#include <QDebug>

namespace Aegis {

    KaraokeController::KaraokeController(AudioEngine* engine, MpvBackend* backend, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_backend(backend)
    , m_cdgDecoder(std::make_unique<CdgDecoder>(this))
    , m_lyricsRenderer(std::make_unique<LyricsRenderer>(this))
    {
        initDatabase();

        // Connect to MpvBackend signals (Pillar 3)
        if (m_backend) {
            connect(m_backend, &MpvBackend::positionChanged,
                    this, &KaraokeController::onPlaybackPositionChanged);
            connect(m_backend, &MpvBackend::finished,
                    this, &KaraokeController::onPlaybackFinished);
        }
    }

    KaraokeController::~KaraokeController() = default;

    KaraokeProcessor* KaraokeController::karaokeProcessor() const {
        return m_engine ? m_engine->karaokeProcessor() : nullptr;
    }

    void KaraokeController::startKaraoke() {
        if (m_active || !m_engine) return;

        m_active = true;

        // Enable karaoke DSP mode in AudioEngine (Pillar 1)
        m_engine->setKaraokeEnabled(true);

        auto* kproc = m_engine->karaokeProcessor();
        if (kproc) {
            kproc->setVocalSuppressionEnabled(true);
            kproc->setMusicVolume(0.8);
            kproc->setVocalVolume(0.0);
            kproc->setEchoLevel(0.3);
        }

        emit activeChanged();
        processQueue();
    }

    void KaraokeController::stopKaraoke() {
        if (!m_active) return;

        m_active = false;

        // Disable karaoke DSP in AudioEngine (Pillar 1)
        if (m_engine) {
            m_engine->setKaraokeEnabled(false);
        }

        // Stop playback via MpvBackend (Pillar 3)
        if (m_backend) {
            m_backend->stop();
        }

        emit activeChanged();
    }

    void KaraokeController::togglePause() {
        if (!m_backend) return;

        if (m_paused) {
            m_backend->play();
        } else {
            m_backend->pause();
        }
        m_paused = !m_paused;
        emit playbackChanged();
    }

    void KaraokeController::setKeyChange(int semitones) {
        auto* kproc = karaokeProcessor();
        if (kproc) {
            kproc->setKeyChange(semitones);
        }
    }

    void KaraokeController::setVocalVolume(double volume) {
        auto* kproc = karaokeProcessor();
        if (kproc) {
            kproc->setVocalVolume(volume);
        }
    }

    void KaraokeController::setMusicVolume(double volume) {
        auto* kproc = karaokeProcessor();
        if (kproc) {
            kproc->setMusicVolume(volume);
        }
    }

    void KaraokeController::setEchoLevel(double level) {
        auto* kproc = karaokeProcessor();
        if (kproc) {
            kproc->setEchoLevel(level);
        }
    }

    void KaraokeController::setVocalSuppression(bool enabled) {
        auto* kproc = karaokeProcessor();
        if (kproc) {
            kproc->setVocalSuppressionEnabled(enabled);
        }
        emit vocalSuppressionChanged();
    }

    bool KaraokeController::vocalSuppression() const {
        auto* kproc = karaokeProcessor();
        return kproc ? kproc->vocalSuppressionEnabled() : false;
    }

    void KaraokeController::processQueue() {
        if (m_queue.isEmpty() || !m_active) return;

        auto &item = m_queue.first();
        item.isPlaying = true;

        if (!m_songs.contains(item.songId)) {
            emit error("Song not found: " + item.songId);
            return;
        }

        const KaraokeSong &song = m_songs[item.songId];
        m_currentSongId = item.songId;
        m_currentSingerId = item.singerId;
        m_currentQueueId = item.id;

        // Configure audio effects for this song (Pillar 1)
        if (auto* kproc = karaokeProcessor()) {
            kproc->setKeyChange(item.keyChange);
        }

        // Load CDG graphics if available
        if (!song.cdgPath.isEmpty()) {
            m_cdgDecoder->load(song.cdgPath);
        }

        // Load and play audio via MpvBackend (Pillar 3)
        if (m_backend) {
            QString audioPath = song.audioPath.isEmpty() ? song.filePath : song.audioPath;
            m_backend->load(audioPath);
            m_backend->play();
        }

        emit songStarted(item.songId, item.singerId);
    }

    void KaraokeController::onPlaybackPositionChanged(double pos) {
        m_position = pos;
        emit positionChanged();

        // Sync CDG frames
        if (m_cdgDecoder) {
            QImage frame = m_cdgDecoder->frameAtTime(pos);
            if (!frame.isNull()) emit frameReady(frame);
        }

        // Check for song end
        if (m_backend && pos >= m_backend->duration() - 0.5) {
            onPlaybackFinished();
        }
    }

    void KaraokeController::onPlaybackFinished() {
        // Mark current song complete
        if (!m_queue.isEmpty()) {
            m_queue.first().isCompleted = true;
        }

        // Move to next song
        if (m_queue.size() > 1) {
            m_queue.removeFirst();
            advanceRotation();
            processQueue();
        } else {
            m_queue.clear();
            stopKaraoke();
        }

        emit queueChanged();
    }

    // ... (remaining methods: nextSong, singer management, queue management,
    // database functions, etc. - unchanged logic, just ensure they don't
    // bypass the pillar architecture)

    void KaraokeController::advanceRotation() {
        // Update rotation number
        if (!m_singers.isEmpty()) {
            m_rotationNumber++;
            emit rotationChanged();
        }
    }

    QString KaraokeController::currentSinger() const {
        if (!m_currentSingerId.isEmpty() && m_singers.contains(m_currentSingerId)) {
            return m_singers[m_currentSingerId].displayName;
        }
        return QString();
    }

    QString KaraokeController::currentSong() const {
        if (!m_currentSongId.isEmpty() && m_songs.contains(m_currentSongId)) {
            return m_songs[m_currentSongId].displayTitle();
        }
        return QString();
    }

    void KaraokeController::nextSong() {
        onPlaybackFinished();
    }

    void KaraokeController::initDatabase() {
        // ... database initialization ...
    }

    // ... (rest of implementation: addSinger, removeSinger, queueSong,
    // removeFromQueue, scanLibrary, searchSongs, etc.) ...

} // namespace Aegis
