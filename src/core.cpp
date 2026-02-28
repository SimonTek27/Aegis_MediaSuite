// core.cpp - Main playback engine implementation
//
// FIX SUMMARY:
//  AudioEngine does NOT have play/pause/stop/seek/setVolume/load/position.
//  Those methods belong to AudioBackend (implemented by MpvBackend).
//  Core now holds both:
//    m_audio  : AudioEngine  — effects, FFT, EBU R128 analysis
//    m_backend: MpvBackend   — actual media decoding & transport control
//  The position-polling lambda now reads m_backend->position() instead of
//  the non-existent m_audio->position().

#include "core.h"
#include "audio.h"
#include "video.h"
#include "mpv_backend.h"
#include "library.h"

#include <QDebug>
#include <algorithm>

namespace Aegis {

    // ── Constructor ───────────────────────────────────────────────────────────

    Core::Core(std::shared_ptr<Library> library, QObject *parent)
    : QObject(parent)
    , m_library(std::move(library))
    {
        // Pillar 1: AudioEngine — handles effects/analysis, NOT transport
        m_audio   = std::make_unique<AudioEngine>(this);

        // Pillar 3: MpvBackend — actual decoding and transport (play/pause/seek/…)
        m_backend = std::make_unique<MpvBackend>(this);

        // Video engine (unchanged)
        m_video = std::make_unique<VideoEngine>(this);

        // Forward backend signals
        connect(m_backend.get(), &AudioBackend::stateChanged,
                this, &Core::onBackendStateChanged);
        connect(m_backend.get(), &AudioBackend::finished,
                this, &Core::onBackendFinished);
        connect(m_backend.get(), &AudioBackend::durationChanged,
                this, &Core::onBackendDurationChanged);
        connect(m_backend.get(), &AudioBackend::metadataChanged,
                this, [this](const TrackMetadata& md) {
                    Q_UNUSED(md)
                    updateMetadata();
                });

        // Forward AudioEngine analysis errors
        connect(m_audio.get(), &AudioEngine::error,
                this, [this](const QString &msg) { emit error(msg); });

        // 60 Hz position polling — read from MpvBackend, not AudioEngine
        m_positionTimer.setInterval(16);
        connect(&m_positionTimer, &QTimer::timeout, [this]() {
            if (m_state.load() == PlaybackState::Playing) {
                m_position.store(m_backend->position());   // ← was m_audio->position()
                emit positionChanged(m_position.load());
            }
        });

        qDebug() << "Core playback engine initialized";
    }

    Core::~Core()
    {
        stop();
        m_positionTimer.stop();
        qDebug() << "Core playback engine destroyed";
    }

    // ── Playback controls ─────────────────────────────────────────────────────

    void Core::play()
    {
        if (m_playlist.empty()) {
            qDebug() << "Playback attempted with empty playlist";
            return;
        }
        if (m_currentIndex.load() < 0)
            setPlaylistIndex(0);

        m_backend->play();          // ← was m_audio->play()
        m_video->play();
        m_positionTimer.start();

        qDebug() << "Playback started for track:" << m_currentIndex.load();
    }

    void Core::pause()
    {
        m_backend->pause();         // ← was m_audio->pause()
        m_video->pause();
        m_positionTimer.stop();

        m_state.store(PlaybackState::Paused);
        emit stateChanged(PlaybackState::Paused);

        qDebug() << "Playback paused at position:" << m_position.load();
    }

    void Core::playPause()
    {
        if (m_state.load() == PlaybackState::Playing)
            pause();
        else
            play();
    }

    void Core::stop()
    {
        m_backend->stop();          // ← was m_audio->stop()
        m_video->stop();
        m_positionTimer.stop();

        m_position.store(0.0);
        emit positionChanged(0.0);

        qDebug() << "Playback stopped";
    }

    void Core::seek(double position)
    {
        position = std::clamp(position, 0.0, m_duration.load());

        m_backend->seek(position);  // ← was m_audio->seek()
        m_video->seek(position);

        m_position.store(position);
        emit positionChanged(position);

        qDebug() << "Seek to position:" << position << "seconds";
    }

    void Core::setVolume(double volume)
    {
        volume = std::clamp(volume, 0.0, 100.0);
        m_volume.store(volume);

        // Volume lives on the backend, not on AudioEngine
        m_backend->setVolume(volume);   // ← was m_audio->setVolume()

        emit volumeChanged(volume);
        qDebug() << "Volume set to:" << volume;
    }

    // ── URL loading ───────────────────────────────────────────────────────────

    void Core::load(const QUrl &url)
    {
        if (!url.isValid()) {
            emit error("Invalid URL provided");
            return;
        }
        m_playlist.clear();
        enqueue(url);
        setPlaylistIndex(0);
        qDebug() << "Loaded media:" << url.toString();
    }

    // ── Playlist management ───────────────────────────────────────────────────

    void Core::enqueue(const QUrl &url)
    {
        PlaylistItem item;
        item.url   = url;
        item.title = url.fileName();
        m_playlist.push_back(std::move(item));
        emit playlistChanged();
        qDebug() << "Enqueued media:" << url.toString();
    }

    void Core::dequeue(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_playlist.size())) {
            qWarning() << "Attempted to dequeue invalid index:" << index;
            return;
        }
        m_playlist.erase(m_playlist.begin() + index);
        if (index == m_currentIndex.load()) {
            stop();
        } else if (index < m_currentIndex.load()) {
            m_currentIndex.store(m_currentIndex.load() - 1);
        }
        emit playlistChanged();
    }

    void Core::setPlaylistIndex(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_playlist.size()))
            return;
        loadTrack(index);
    }

    // ── Getters ───────────────────────────────────────────────────────────────

    QString Core::currentTitle() const
    {
        int idx = m_currentIndex.load();
        if (idx >= 0 && idx < static_cast<int>(m_playlist.size()))
            return m_playlist[static_cast<size_t>(idx)].title;
        return {};
    }

    QVariantList Core::playlist() const
    {
        QVariantList list;
        for (const auto &item : m_playlist) {
            QVariantMap map;
            map["url"]      = item.url;
            map["title"]    = item.title;
            map["duration"] = item.duration.value_or(0.0);
            list.append(map);
        }
        return list;
    }

    // ── Private helpers ───────────────────────────────────────────────────────

    void Core::loadTrack(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_playlist.size()))
            return;

        m_currentIndex.store(index);
        const auto &item = m_playlist[static_cast<size_t>(index)];

        try {
            // Load via MpvBackend (not AudioEngine)
            m_backend->load(item.url.toLocalFile());   // ← was m_audio->load()
            m_video->load(item.url);

            // Apply current volume to backend
            m_backend->setVolume(m_volume.load());     // ← was m_audio->setVolume()

            emit currentTrackChanged(item.title);
            emit hasVideoChanged(hasVideo());
            updateMetadata();

            qDebug() << "Loaded track" << index << ":" << item.url.toString();

        } catch (const std::exception &e) {
            QString msg = QString("Failed to load track: %1").arg(e.what());
            qCritical() << msg;
            emit error(msg);
            onBackendFinished();
        }
    }

    void Core::updateMetadata()
    {
        // Metadata comes from MpvBackend, not AudioEngine
        int idx = m_currentIndex.load();
        if (idx >= 0 && idx < static_cast<int>(m_playlist.size())) {
            emit currentTrackChanged(m_playlist[static_cast<size_t>(idx)].title);
        }
    }

    // ── Backend signal handlers ───────────────────────────────────────────────

    void Core::onBackendStateChanged(PlaybackState state)
    {
        m_state.store(state);
        emit stateChanged(state);

        if (state == PlaybackState::Playing)
            m_positionTimer.start();
        else
            m_positionTimer.stop();
    }

    void Core::onBackendPositionChanged(double pos)
    {
        m_position.store(pos);
        emit positionChanged(pos);
    }

    void Core::onBackendDurationChanged(double dur)
    {
        m_duration.store(dur);
        emit durationChanged(dur);

        int idx = m_currentIndex.load();
        if (idx >= 0 && idx < static_cast<int>(m_playlist.size()))
            m_playlist[static_cast<size_t>(idx)].duration = dur;
    }

    void Core::onBackendFinished()
    {
        int next = m_currentIndex.load() + 1;
        if (next < static_cast<int>(m_playlist.size())) {
            setPlaylistIndex(next);
            play();
        } else {
            stop();
            emit playlistFinished();
        }
    }

    bool Core::hasVideo() const
    {
        return m_backend ? m_backend->hasVideo() : false;
    }

} // namespace Aegis
