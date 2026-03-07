// karaoke.cpp - Karaoke implementation
#include "audio_karaoke.h"
#include "audio.h"
#include "mpv_backend.h"
#include <QDebug>
#include <QUuid>

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
            return m_songs[m_currentSongId].title;
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

    // ─── CdgDecoder ───────────────────────────────────────────────────────────

    CdgDecoder::CdgDecoder(QObject* parent) : QObject(parent) {}

    bool CdgDecoder::load(const QString& cdgPath) {
        m_packets.clear();
        m_packetTimes.clear();

        QFile f(cdgPath);
        if (!f.open(QIODevice::ReadOnly)) return false;

        constexpr double packetDurationSec = 1.0 / 300.0; // 300 packets/sec (CD+G spec)
        int idx = 0;
        while (!f.atEnd()) {
            CdgPacket pkt{};
            if (f.read(reinterpret_cast<char*>(&pkt), 24) != 24) break;
            m_packets.append(pkt);
            m_packetTimes.append(idx * packetDurationSec);
            ++idx;
        }
        return !m_packets.isEmpty();
    }

    QImage CdgDecoder::frameAtTime(double timeSeconds) {
        // Find the last packet at or before timeSeconds
        int lastIdx = -1;
        for (int i = 0; i < m_packetTimes.size(); ++i) {
            if (m_packetTimes[i] <= timeSeconds) lastIdx = i;
            else break;
        }
        Q_UNUSED(lastIdx)
        // Stub: return empty frame (full CD+G renderer is out of scope)
        return QImage(300, 216, QImage::Format_ARGB32);
    }

    // ─── LyricsRenderer ───────────────────────────────────────────────────────

    LyricsRenderer::LyricsRenderer(QObject* parent) : QObject(parent) {}

    bool LyricsRenderer::loadLrcFile(const QString& path) {
        m_lines.clear();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

        static const QRegularExpression timeTag(QStringLiteral(R"(\[(\d+):(\d+)\.(\d+)\])"));
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine();
            auto it = timeTag.globalMatch(line);
            while (it.hasNext()) {
                auto m = it.next();
                double t = m.captured(1).toInt() * 60.0
                         + m.captured(2).toDouble()
                         + m.captured(3).toDouble() / 100.0;
                QString text = line.mid(m.capturedEnd()).trimmed();
                m_lines.append({t, text});
            }
        }
        std::sort(m_lines.begin(), m_lines.end(), [](const LyricLine& a, const LyricLine& b){
            return a.time < b.time;
        });
        return !m_lines.isEmpty();
    }

    QString LyricsRenderer::lineAtTime(double timeSeconds) const {
        QString result;
        for (const auto& l : m_lines) {
            if (l.time <= timeSeconds) result = l.text;
            else break;
        }
        return result;
    }

    // ─── KaraokeController singer/queue management ───────────────────────────

    QString KaraokeController::addSinger(const QString& name, const QString& displayName) {
        KaraokeSinger s;
        s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.name = name;
        s.displayName = displayName.isEmpty() ? name : displayName;
        s.rotationIndex = m_singers.size();
        m_singers.insert(s.id, s);
        emit singerChanged();
        return s.id;
    }
    void KaraokeController::removeSinger(const QString& singerId) {
        m_singers.remove(singerId);
        emit singerChanged();
    }
    void KaraokeController::moveSinger(const QString& singerId, int newPosition) {
        Q_UNUSED(singerId) Q_UNUSED(newPosition)
        emit rotationChanged();
    }
    QVariantList KaraokeController::singers() const {
        QVariantList result;
        for (const auto& s : m_singers) {
            QVariantMap m;
            m[QStringLiteral("id")]          = s.id;
            m[QStringLiteral("name")]        = s.name;
            m[QStringLiteral("displayName")] = s.displayName;
            result.append(m);
        }
        return result;
    }
    QString KaraokeController::queueSong(const QString& songId, const QString& singerId, int keyChange) {
        KaraokeQueueItem item;
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        item.songId = songId;
        item.singerId = singerId;
        item.keyChange = keyChange;
        m_queue.append(item);
        emit queueChanged();
        return item.id;
    }
    void KaraokeController::removeFromQueue(const QString& queueId) {
        m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),
            [&](const KaraokeQueueItem& i){ return i.id == queueId; }),
            m_queue.end());
        emit queueChanged();
    }
    QVariantList KaraokeController::queue() const {
        QVariantList result;
        for (const auto& item : m_queue) {
            QVariantMap m;
            m[QStringLiteral("id")]       = item.id;
            m[QStringLiteral("songId")]   = item.songId;
            m[QStringLiteral("singerId")] = item.singerId;
            m[QStringLiteral("keyChange")]= item.keyChange;
            result.append(m);
        }
        return result;
    }
    void KaraokeController::scanLibrary(const QString& path) { Q_UNUSED(path) }
    QVariantList KaraokeController::searchSongs(const QString&, int) { return {}; }
    void KaraokeController::importOpenKJDatabase(const QString&) {}
    void KaraokeController::setLyricsFont(const QString&, int)  {}
    void KaraokeController::setLyricsColor(const QColor&)       {}

    void KaraokeController::onCdgFrameReady(const QImage& frame) {
        emit frameReady(frame);
    }

} // namespace Aegis
