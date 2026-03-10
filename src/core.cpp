// core.cpp - Main playback engine implementation
//
// FIX SUMMARY:
//  AudioEngine does NOT have play/pause/stop/seek/setVolume/load/position.
//  Those methods belong to AudioBackend (implemented by MpvBackend).
//  Core now holds both:
//    m_audio  : AudioEngine  — effects, FFT, EBU R128 analysis
//    m_backend: MpvBackend   — actual media decoding & transport control
//  The position-polling lambda now reads m_audioBackend->position() instead of
//  the non-existent m_audioEngine->position().

#include "core.h"
#include "audio.h"
#include "video.h"
#include "mpv_backend.h"
#include "library.h"

#include <QDebug>
#include <algorithm>

namespace Aegis {

    // ── Constructor ───────────────────────────────────────────────────────────

    Core::Core(std::unique_ptr<IAudioBackend> audioBackend,
               std::unique_ptr<IVideoBackend> videoBackend,
               std::unique_ptr<IAudioEngine>  audioEngine,
               QObject* parent)
        : QObject(parent)
        , m_audioBackend(std::move(audioBackend))
        , m_videoBackend(std::move(videoBackend))
        , m_audioEngine(std::move(audioEngine))
    {
        // Nothing further needed — backends are ready for use immediately.
    }

    // ── Playback controls ─────────────────────────────────────────────────────

    void Core::play()
    {
        if (m_audioBackend)
            m_audioBackend->play();
    }

    void Core::pause()
    {
        if (m_audioBackend)
            m_audioBackend->pause();
    }

    void Core::playPause()
    {
        if (!m_audioBackend) return;
        if (m_audioBackend->state() == PlaybackState::Playing)
            m_audioBackend->pause();
        else
            m_audioBackend->play();
    }

    void Core::stop()
    {
        if (m_audioBackend)
            m_audioBackend->stop();
    }

    void Core::next()
    {
        int next = m_currentPlaylistIndex.load() + 1;
        if (next < static_cast<int>(m_playlist.size()))
            loadTrackImpl(next);
    }

    void Core::previous()
    {
        int prev = m_currentPlaylistIndex.load() - 1;
        if (prev >= 0)
            loadTrackImpl(prev);
    }

    void Core::setVolume(double volume)
    {
        m_volume.store(volume);
        if (m_audioBackend)
            m_audioBackend->setVolume(volume);
    }

    bool Core::hasVideo() const
    {
        if (m_videoBackend)
            return m_videoBackend->hasVideo();
        return false;
    }

    // ── Playlist management ───────────────────────────────────────────────────

    void Core::enqueue(const QUrl& url)
    {
        m_playlist.emplace_back(url, PlaylistItem::Type::File);
        emit playlistChanged();
        // Auto-start if this is the first item
        if (m_currentPlaylistIndex.load() < 0)
            loadTrackImpl(0);
    }

    void Core::clearPlaylist()
    {
        stop();
        m_playlist.clear();
        m_currentPlaylistIndex.store(-1);
        emit playlistChanged();
    }

    void Core::onAudioBackendFinished()
    {
        // Advance to next track, or emit playlist end
        int next = m_currentPlaylistIndex.load() + 1;
        if (next < static_cast<int>(m_playlist.size()))
            loadTrackImpl(next);
        else
            m_currentPlaylistIndex.store(-1);
    }







    void Core::dequeue(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_playlist.size())) {
            qWarning() << "Attempted to dequeue invalid index:" << index;
            return;
        }
        m_playlist.erase(m_playlist.begin() + index);
        if (index == m_currentPlaylistIndex.load()) {
            stop();
            m_currentPlaylistIndex.store(-1);
        } else if (index < m_currentPlaylistIndex.load()) {
            m_currentPlaylistIndex.store(m_currentPlaylistIndex.load() - 1);
        }
        emit playlistChanged();
    }


    // ── Getters ───────────────────────────────────────────────────────────────

    QString Core::currentTitle() const
    {
        int idx = m_currentPlaylistIndex.load();
        if (idx >= 0 && idx < static_cast<int>(m_playlist.size()))
            return m_playlist[static_cast<size_t>(idx)].title();
        return {};
    }

    QVariantList Core::playlistVariant() const
    {
        QVariantList list;
        for (const auto &item : m_playlist) {
            QVariantMap map;
            map["url"]      = item.url();
            map["title"]    = item.title();
            map["duration"] = item.duration().value_or(0.0);
            list.append(map);
        }
        return list;
    }

    // ── Private helpers ───────────────────────────────────────────────────────

    void Core::loadTrackImpl(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_playlist.size()))
            return;

        m_currentPlaylistIndex.store(index);
        const auto &item = m_playlist[static_cast<size_t>(index)];

        try {
            m_audioBackend->open(QUrl::fromLocalFile(item.url().toLocalFile()));
            m_videoBackend->load(item.url());

            m_audioBackend->setVolume(m_volume.load());

            emit currentTrackChanged(item);
            emit hasVideoChanged(hasVideo());
            updateMetadata();

            qDebug() << "Loaded track" << index << ":" << item.url().toString();

        } catch (const std::exception &e) {
            QString msg = QString("Failed to load track: %1").arg(e.what());
            qCritical() << msg;
            emit error(msg);
            onAudioBackendFinished();
        }
    }

    void Core::updateMetadata()
    {
        int idx = m_currentPlaylistIndex.load();
        if (idx >= 0 && idx < static_cast<int>(m_playlist.size())) {
            emit currentTrackChanged(m_playlist[static_cast<size_t>(idx)]);
        }
    }

    // ── Backend signal handlers ───────────────────────────────────────────────


    void Core::onBackendPositionChanged(double pos)
    {
        m_position.store(pos);
        emit positionChanged(pos);
    }




    // ─── PlaylistItem ─────────────────────────────────────────────────────────

    PlaylistItem::PlaylistItem(const QUrl& url, Type type)
        : m_url(url), m_type(type)
    {
        // Try to derive a title from the filename
        m_title = url.fileName();
    }

    void Core::seek(qint64 positionMs)
    {
        if (m_audioBackend)
            m_audioBackend->seek(positionMs);
    }


    void Core::load(const QUrl& url)
    {
        if (m_audioBackend)
            m_audioBackend->open(url);
    }

    PlaybackState Core::state() const
    {
        if (m_audioBackend)
            return m_audioBackend->state();
        return PlaybackState::Stopped;
    }

} // namespace Aegis
