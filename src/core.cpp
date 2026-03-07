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



    // ── Playback controls ─────────────────────────────────────────────────────







    // ── URL loading ───────────────────────────────────────────────────────────


    // ── Playlist management ───────────────────────────────────────────────────


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
            m_audioBackend->load(item.url().toLocalFile());
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

} // namespace Aegis
