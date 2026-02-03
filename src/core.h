// core.h - Main playback engine core interface
#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QTimer>
#include <memory>
#include <vector>
#include <atomic>
#include <optional>

namespace Aegis {

    // Forward declarations to reduce header dependencies
    class AudioEngine;
    class VideoEngine;
    class Library;

    /**
     * @brief Playback state enumeration
     *
     * Represents the current state of media playback.
     * Used for state machines and UI feedback.
     */
    enum class PlaybackState {
        Stopped,    ///< No media is loaded or playing
        Playing,    ///< Media is actively playing
        Paused,     ///< Playback is paused
        Buffering   ///< Media is buffering data
    };

    /**
     * @brief Main playback controller and state manager
     *
     * Central orchestrator for all media playback operations.
     * Manages playlist, audio/video synchronization, and state transitions.
     * Provides QML-friendly interface with properties and signals.
     */
    class Core : public QObject {
        Q_OBJECT
        // QML-accessible properties with automatic change notifications
        Q_PROPERTY(PlaybackState state READ state NOTIFY stateChanged)
        Q_PROPERTY(double position READ position NOTIFY positionChanged)
        Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
        Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
        Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
        Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentTrackChanged)
        Q_PROPERTY(QVariantList playlist READ playlist NOTIFY playlistChanged)

    public:
        /**
         * @brief Constructs the Core playback engine
         * @param library Shared pointer to media library database
         * @param parent Parent QObject for memory management
         */
        explicit Core(std::shared_ptr<Library> library, QObject *parent = nullptr);

        /**
         * @brief Destructor with proper cleanup
         */
        ~Core() override;

        // Disable copying to prevent multiple instances controlling same resources
        Core(const Core&) = delete;
        Core& operator=(const Core&) = delete;

        // ================ Playback Control API ================

        /**
         * @brief Load a single media file for playback
         * @param url URL or local file path to media resource
         *
         * Clears existing playlist and loads the specified URL as single item.
         * Does not start playback automatically.
         */
        Q_INVOKABLE void load(const QUrl &url);

        /**
         * @brief Start or resume playback
         *
         * If no track is loaded, starts playback from first playlist item.
         * Resumes from paused position if applicable.
         */
        Q_INVOKABLE void play();

        /**
         * @brief Pause current playback
         *
         * Pauses at current position without unloading media.
         * State can be restored with play().
         */
        Q_INVOKABLE void pause();

        /**
         * @brief Toggle between play and pause states
         */
        Q_INVOKABLE void playPause();

        /**
         * @brief Stop playback completely
         *
         * Stops playback and resets position to beginning.
         * Media remains loaded in engine.
         */
        Q_INVOKABLE void stop();

        /**
         * @brief Seek to specific position in current track
         * @param position Time in seconds from start of track
         *
         * Position is clamped to valid range [0, duration].
         * Updates both audio and video engines synchronously.
         */
        Q_INVOKABLE void seek(double position);

        /**
         * @brief QML-compatible alias for seek()
         */
        Q_INVOKABLE void setPosition(double position) { seek(position); }

        /**
         * @brief Set playback volume level
         * @param volume Volume level from 0.0 (mute) to 100.0 (max)
         *
         * Volume is applied to audio engine only.
         * Value is automatically clamped to valid range.
         */
        Q_INVOKABLE void setVolume(double volume);

        // ================ Playlist Management API ================

        /**
         * @brief Add media URL to end of playlist
         * @param url URL or local file path to add
         */
        Q_INVOKABLE void enqueue(const QUrl &url);

        /**
         * @brief Remove item from playlist at specified index
         * @param index Zero-based position in playlist
         *
         * Adjusts current index if removal affects current playback.
         */
        Q_INVOKABLE void dequeue(int index);

        /**
         * @brief Remove all items from playlist
         */
        Q_INVOKABLE void clearPlaylist();

        /**
         * @brief Advance to next track in playlist
         *
         * Handles repeat modes and end-of-playlist behavior.
         */
        Q_INVOKABLE void next();

        /**
         * @brief Return to previous track in playlist
         *
         * Does nothing if at beginning of playlist.
         */
        Q_INVOKABLE void previous();

        /**
         * @brief Jump directly to specific playlist index
         * @param index Zero-based position in playlist
         *
         * Stops current playback and loads specified track.
         */
        Q_INVOKABLE void setPlaylistIndex(int index);

        // ================ State Query Methods ================

        /**
         * @brief Get current playback state
         * @return Current PlaybackState enumeration value
         */
        PlaybackState state() const { return m_state.load(); }

        /**
         * @brief Get current playback position
         * @return Current position in seconds
         */
        double position() const { return m_position.load(); }

        /**
         * @brief Get duration of currently loaded track
         * @return Duration in seconds, 0.0 if no media loaded
         */
        double duration() const { return m_duration.load(); }

        /**
         * @brief Get current volume level
         * @return Volume from 0.0 to 100.0
         */
        double volume() const { return m_volume.load(); }

        /**
         * @brief Check if current media contains video stream
         * @return True if video track is available
         */
        bool hasVideo() const;

        /**
         * @brief Get title of currently playing track
         * @return Track title or empty string if no track loaded
         */
        QString currentTitle() const;

        /**
         * @brief Get playlist data in QML-compatible format
         * @return List of playlist items as QVariantMap objects
         */
        QVariantList playlist() const;

        // ================ Engine Access (Advanced Usage) ================

        /**
         * @brief Get raw pointer to audio engine
         * @return AudioEngine instance for low-level control
         *
         * Warning: Direct engine manipulation may bypass Core state management.
         */
        AudioEngine* audioEngine() { return m_audio.get(); }

        /**
         * @brief Get raw pointer to video engine
         * @return VideoEngine instance for low-level control
         */
        VideoEngine* videoEngine() { return m_video.get(); }

    signals:
        // ================ State Change Notifications ================

        /**
         * @brief Emitted when playback state changes
         * @param state New playback state
         */
        void stateChanged(PlaybackState state);

        /**
         * @brief Emitted when playback position changes
         * @param position New position in seconds
         *
         * Throttled for UI updates but precise for seeking operations.
         */
        void positionChanged(double position);

        /**
         * @brief Emitted when track duration is known or changes
         * @param duration Duration in seconds
         */
        void durationChanged(double duration);

        /**
         * @brief Emitted when volume level changes
         * @param volume New volume level (0.0-100.0)
         */
        void volumeChanged(double volume);

        /**
         * @brief Emitted when current track changes
         * @param title Title of new track
         */
        void currentTrackChanged(const QString &title);

        /**
         * @brief Emitted when video availability changes
         * @param hasVideo True if video stream is available
         */
        void hasVideoChanged(bool hasVideo);

        /**
         * @brief Emitted when playlist contents change
         */
        void playlistChanged();

        /**
         * @brief Emitted when playback error occurs
         * @param message Human-readable error description
         */
        void error(const QString &message);

        /**
         * @brief Emitted when end of stream is reached
         *
         * Signals that playback has naturally completed.
         */
        void endOfStream();

    private slots:
        // ================ Backend Event Handlers ================

        /**
         * @brief Handle state changes from audio/video engines
         * @param state New engine state
         */
        void onBackendStateChanged(PlaybackState state);

        /**
         * @brief Handle position updates from backend engines
         * @param pos Current position in seconds
         */
        void onBackendPositionChanged(double pos);

        /**
         * @brief Handle duration updates from backend engines
         * @param dur Track duration in seconds
         */
        void onBackendDurationChanged(double dur);

        /**
         * @brief Handle track completion from backend engines
         *
         * Triggers auto-advance to next track or end-of-stream.
         */
        void onBackendFinished();

    private:
        // ================ Private Helper Methods ================

        /**
         * @brief Load and prepare track from playlist
         * @param index Playlist position to load
         */
        void loadTrack(int index);

        /**
         * @brief Update metadata for current track
         *
         * Queries library database and backend engines for metadata.
         */
        void updateMetadata();

        // ================ Data Structures ================

        /**
         * @brief Internal representation of playlist item
         */
        struct PlaylistItem {
            QUrl url;                       ///< Media resource location
            QString title;                  ///< Display title (from metadata or filename)
            std::optional<double> duration; ///< Track duration if known
        };

        // ================ Member Variables ================

        std::vector<PlaylistItem> m_playlist;       ///< Ordered list of media items
        std::atomic<int> m_currentIndex{-1};        ///< Currently playing item index

        // Playback state (atomic for thread-safe access)
        std::atomic<PlaybackState> m_state{PlaybackState::Stopped};
        std::atomic<double> m_position{0.0};        ///< Current playback position (seconds)
        std::atomic<double> m_duration{0.0};        ///< Current track duration (seconds)
        std::atomic<double> m_volume{100.0};        ///< Current volume level (0-100)

        // Engine components (managed with unique_ptr for automatic cleanup)
        std::unique_ptr<AudioEngine> m_audio;       ///< Audio decoding and output
        std::unique_ptr<VideoEngine> m_video;       ///< Video decoding and rendering
        std::shared_ptr<Library> m_library;         ///< Media metadata database

        // UI update timer for smooth position display
        QTimer m_positionTimer;                     ///< 60Hz timer for UI updates
    };

} // namespace Aegis

// Register enums for QML type system
Q_DECLARE_METATYPE(Aegis::PlaybackState)
