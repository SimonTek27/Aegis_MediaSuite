// core.cpp - Main playback engine implementation
#include "core.h"
#include "audio.h"
#include "video.h"
#include "library.h"

#include <QDebug>
#include <algorithm> // For std::clamp

namespace Aegis {

    // ================ Constructor & Destructor ================

    /**
     * @brief Core constructor - Initializes playback engine
     * @param library Shared library database
     * @param parent Parent QObject
     */
    Core::Core(std::shared_ptr<Library> library, QObject *parent)
    : QObject(parent)
    , m_library(std::move(library))
    {
        // Initialize engine components
        m_audio = std::make_unique<AudioEngine>(this);
        m_video = std::make_unique<VideoEngine>(this);

        // Connect audio engine signals to internal handlers
        // These connections ensure state synchronization between engines
        connect(m_audio.get(), &AudioEngine::positionChanged,
                this, &Core::onBackendPositionChanged);
        connect(m_audio.get(), &AudioEngine::stateChanged,
                this, &Core::onBackendStateChanged);
        connect(m_audio.get(), &AudioEngine::finished,
                this, &Core::onBackendFinished);
        connect(m_audio.get(), &AudioEngine::error,
                this, [this](const QString &msg) { emit error(msg); });

        // Setup position update timer for smooth UI feedback
        // 16ms interval ≈ 60fps for responsive progress bars
        m_positionTimer.setInterval(16);
        connect(&m_positionTimer, &QTimer::timeout, [this]() {
            // Only emit updates during active playback to reduce CPU usage
            if (m_state.load() == PlaybackState::Playing) {
                m_position.store(m_audio->position());
                emit positionChanged(m_position.load());
            }
        });

        qDebug() << "Core playback engine initialized";
    }

    /**
     * @brief Core destructor - Cleanup resources
     */
    Core::~Core()
    {
        // Stop all playback before destruction
        stop();
        m_positionTimer.stop();

        // Unique pointers automatically clean up engines
        qDebug() << "Core playback engine destroyed";
    }

    // ================ Playback Control Implementation ================

    /**
     * @brief Load single media file, clearing existing playlist
     * @param url Media URL to load
     */
    void Core::load(const QUrl &url)
    {
        // Validate input URL
        if (!url.isValid()) {
            emit error("Invalid URL provided");
            return;
        }

        // Clear existing playlist and load single item
        m_playlist.clear();
        enqueue(url);
        setPlaylistIndex(0);

        qDebug() << "Loaded media:" << url.toString();
    }

    /**
     * @brief Start or resume playback
     */
    void Core::play()
    {
        // Cannot play without playlist items
        if (m_playlist.empty()) {
            qDebug() << "Playback attempted with empty playlist";
            return;
        }

        // Ensure we have a valid current index
        if (m_currentIndex.load() < 0) {
            setPlaylistIndex(0);
        }

        // Start both audio and video engines
        m_audio->play();
        m_video->play();

        // Begin UI update timer
        m_positionTimer.start();

        qDebug() << "Playback started for track:" << m_currentIndex.load();
    }

    /**
     * @brief Pause current playback
     */
    void Core::pause()
    {
        // Pause both engines
        m_audio->pause();
        m_video->pause();

        // Stop UI updates while paused
        m_positionTimer.stop();

        // Update internal state
        m_state.store(PlaybackState::Paused);
        emit stateChanged(PlaybackState::Paused);

        qDebug() << "Playback paused at position:" << m_position.load();
    }

    /**
     * @brief Toggle between play and pause states
     */
    void Core::playPause()
    {
        if (m_state.load() == PlaybackState::Playing) {
            pause();
        } else {
            play();
        }
    }

    /**
     * @brief Stop playback completely
     */
    void Core::stop()
    {
        // Stop both engines
        m_audio->stop();
        m_video->stop();

        // Stop UI updates
        m_positionTimer.stop();

        // Reset position and notify UI
        m_position.store(0.0);
        emit positionChanged(0.0);

        qDebug() << "Playback stopped";
    }

    /**
     * @brief Seek to specific position in current track
     * @param position Time in seconds
     */
    void Core::seek(double position)
    {
        // Clamp position to valid range [0, duration]
        position = std::clamp(position, 0.0, m_duration.load());

        // Seek both engines synchronously
        m_audio->seek(position);
        m_video->seek(position);

        // Update internal position and notify UI
        m_position.store(position);
        emit positionChanged(position);

        qDebug() << "Seek to position:" << position << "seconds";
    }

    /**
     * @brief Set playback volume level
     * @param volume Volume from 0.0 to 100.0
     */
    void Core::setVolume(double volume)
    {
        // Clamp volume to valid range
        volume = std::clamp(volume, 0.0, 100.0);

        // Store and apply to audio engine
        m_volume.store(volume);
        m_audio->setVolume(volume);

        // Notify UI of volume change
        emit volumeChanged(volume);

        qDebug() << "Volume set to:" << volume;
    }

    // ================ Playlist Management Implementation ================

    /**
     * @brief Add media URL to playlist
     * @param url Media URL to add
     */
    void Core::enqueue(const QUrl &url)
    {
        PlaylistItem item;
        item.url = url;

        // Use filename as temporary title until metadata is loaded
        item.title = url.fileName();

        // Add to playlist vector
        m_playlist.push_back(std::move(item));

        // Notify UI of playlist change
        emit playlistChanged();

        qDebug() << "Enqueued media:" << url.toString();
    }

    /**
     * @brief Remove item from playlist
     * @param index Zero-based position to remove
     */
    void Core::dequeue(int index)
    {
        // Validate index bounds
        if (index < 0 || index >= static_cast<int>(m_playlist.size())) {
            qWarning() << "Attempted to dequeue invalid index:" << index;
            return;
        }

        // Remove item from vector
        m_playlist.erase(m_playlist.begin() + index);

        // Adjust current index if necessary
        if (index == m_currentIndex.load()) {
            // If removing current item, stop playback
            stop();
        } else if (index < m_currentIndex.load()) {
            // If removing before current item, decrement current index
            m_currentIndex.store(m_currentIndex.load() - 1);
        }

        // Notify UI of playlist change
        emit playlistChanged();

        qDebug() << "Dequeued item at index:" << index;
    }

    /**
     * @brief Clear entire playlist
     */
    void Core::clearPlaylist()
    {
        // Stop any active playback
        stop();

        // Clear playlist vector
        m_playlist.clear();

        // Reset current index
        m_currentIndex.store(-1);

        // Notify UI
        emit playlistChanged();

        qDebug() << "Playlist cleared";
    }

    /**
     * @brief Advance to next track in playlist
     */
    void Core::next()
    {
        int nextIdx = m_currentIndex.load() + 1;

        if (nextIdx < static_cast<int>(m_playlist.size())) {
            // Valid next track exists
            setPlaylistIndex(nextIdx);
        } else {
            // End of playlist reached
            stop();
            emit endOfStream();

            qDebug() << "End of playlist reached";
        }
    }

    /**
     * @brief Return to previous track
     */
    void Core::previous()
    {
        int prevIdx = m_currentIndex.load() - 1;

        if (prevIdx >= 0) {
            // Valid previous track exists
            setPlaylistIndex(prevIdx);
        } else {
            qDebug() << "Already at beginning of playlist";
        }
    }

    /**
     * @brief Jump to specific playlist index
     * @param index Target playlist position
     */
    void Core::setPlaylistIndex(int index)
    {
        // Validate index bounds
        if (index < 0 || index >= static_cast<int>(m_playlist.size())) {
            qWarning() << "Invalid playlist index:" << index;
            return;
        }

        // Stop current playback
        stop();

        // Load and play the new track
        loadTrack(index);
        play();

        qDebug() << "Playlist index set to:" << index;
    }

    // ================ State Query Implementation ================

    /**
     * @brief Check if current media contains video
     * @return True if video stream is available
     */
    bool Core::hasVideo() const
    {
        return m_video && m_video->hasVideo();
    }

    /**
     * @brief Get title of currently playing track
     * @return Track title or empty string
     */
    QString Core::currentTitle() const
    {
        int idx = m_currentIndex.load();
        if (idx >= 0 && idx < static_cast<int>(m_playlist.size())) {
            return m_playlist[idx].title;
        }
        return QString();
    }

    /**
     * @brief Get playlist in QML-compatible format
     * @return List of playlist items as QVariantMap
     */
    QVariantList Core::playlist() const
    {
        QVariantList list;

        for (const auto &item : m_playlist) {
            QVariantMap map;
            map["url"] = item.url;
            map["title"] = item.title;
            map["duration"] = item.duration.value_or(0.0);
            list.append(map);
        }

        return list;
    }

    // ================ Private Helper Methods ================

    /**
     * @brief Load and prepare track from playlist
     * @param index Playlist position to load
     */
    void Core::loadTrack(int index)
    {
        // Validate index
        if (index < 0 || index >= static_cast<int>(m_playlist.size())) {
            return;
        }

        // Update current index
        m_currentIndex.store(index);
        const auto &item = m_playlist[index];

        try {
            // Load media into both engines
            m_audio->load(item.url);
            m_video->load(item.url);

            // Restore saved volume setting
            m_audio->setVolume(m_volume.load());

            // Notify UI of track change
            emit currentTrackChanged(item.title);
            emit hasVideoChanged(hasVideo());

            // Trigger metadata update from library
            updateMetadata();

            qDebug() << "Loaded track" << index << ":" << item.url.toString();

        } catch (const std::exception &e) {
            // Handle load failure
            QString errorMsg = QString("Failed to load track: %1").arg(e.what());
            qCritical() << errorMsg;

            emit error(errorMsg);
            onBackendFinished(); // Auto-skip to next track
        }
    }

    /**
     * @brief Update metadata for current track
     */
    void Core::updateMetadata()
    {
        // TODO: Implement metadata extraction from:
        // 1. Audio engine metadata
        // 2. Video engine metadata
        // 3. Library database
        // 4. Online sources (MusicBrainz, etc.)

        int idx = m_currentIndex.load();
        if (idx >= 0 && idx < static_cast<int>(m_playlist.size())) {
            // Example: Query library for metadata
            // auto metadata = m_library->getMetadata(m_playlist[idx].url);
            // m_playlist[idx].title = metadata.title;
            // m_playlist[idx].artist = metadata.artist;

            emit currentTrackChanged(m_playlist[idx].title);
        }
    }

    // ================ Backend Event Handlers ================

    /**
     * @brief Handle state changes from audio/video engines
     * @param state New engine state
     */
    void Core::onBackendStateChanged(PlaybackState state)
    {
        // Update internal state
        m_state.store(state);

        // Forward to UI
        emit stateChanged(state);

        qDebug() << "Backend state changed to:" << static_cast<int>(state);
    }

    /**
     * @brief Handle position updates from backend
     * @param pos Current position in seconds
     */
    void Core::onBackendPositionChanged(double pos)
    {
        // Update internal position
        m_position.store(pos);

        // Emit immediately for precise seeking feedback
        // Timer handles throttled updates during playback
        if (m_positionTimer.isActive()) {
            emit positionChanged(pos);
        }
    }

    /**
     * @brief Handle duration updates from backend
     * @param dur Track duration in seconds
     */
    void Core::onBackendDurationChanged(double dur)
    {
        // Update internal duration
        m_duration.store(dur);

        // Update playlist item duration if applicable
        int idx = m_currentIndex.load();
        if (idx >= 0 && idx < static_cast<int>(m_playlist.size())) {
            m_playlist[idx].duration = dur;
        }

        // Forward to UI
        emit durationChanged(dur);

        qDebug() << "Track duration:" << dur << "seconds";
    }

    /**
     * @brief Handle track completion from backend
     */
    void Core::onBackendFinished()
    {
        // Auto-advance to next track if available
        if (m_currentIndex.load() < static_cast<int>(m_playlist.size()) - 1) {
            next();
        } else {
            // End of playlist reached
            stop();
            emit endOfStream();

            qDebug() << "Playlist completed";
        }
    }

} // namespace Aegis
