// mediaplayer.h - Unified media player with audio_output integration
// Uses three-pillar architecture with proper audio output abstraction

#pragma once

#include <QObject>
#include <QUrl>
#include <QTimer>
#include <chrono>
#include <memory>
#include "audio.h"
#include "audio_output.h"  // NEW: Audio output abstraction

// Forward declarations
class Library;
class Playlist;
class Settings;

namespace Aegis {

    enum class PlaybackState {
        Stopped,
        Playing,
        Paused,
        Buffering,
        Error
    };

    enum class BackendType {
        None,
        Mpv,      // Standard audio/video via MPV + AudioOutput
        Tracker   // MOD/XM/IT/S3M via libopenmpt + AudioOutput
    };

    struct TrackMetadata {
        QString title;
        QString artist;
        QString album;
        QString comment;
        int year = 0;
        int duration = 0;     // milliseconds
        int channels = 2;
        int patterns = 0;
        bool hasVideo = false;
        bool isTracker = false;
    };

    /**
     * @brief Unified media player with audio_output integration
     *
     * Architecture:
     * - Pillar 1 (audio): AudioEngine for effects, analysis, tracker playback
     * - Pillar 2 (audio_effects): Effects chain processing
     * - Pillar 3 (mpv_backend): Media decoding
     * - audio_output: PipeWire/Qt abstraction for actual audio output
     *
     * Audio flow: MPV/Tracker -> AudioEngine (effects) -> AudioOutput (PipeWire)
     */
    class MediaPlayer : public QObject {
        Q_OBJECT
        Q_PROPERTY(PlaybackState state READ state NOTIFY stateChanged)
        Q_PROPERTY(QUrl source READ source NOTIFY sourceChanged)
        Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
        Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
        Q_PROPERTY(TrackMetadata metadata READ metadata NOTIFY metadataChanged)
        Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
        Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
        Q_PROPERTY(bool seekable READ seekable NOTIFY seekableChanged)
        Q_PROPERTY(BackendType activeBackend READ activeBackend NOTIFY backendChanged)
        Q_PROPERTY(bool isTrackerMode READ isTrackerMode NOTIFY backendChanged)
        Q_PROPERTY(QString audioBackendName READ audioBackendName NOTIFY audioBackendChanged)

    public:
        /**
         * @brief Construct with explicit audio output dependency
         * @param output AudioOutput for playback (PipeWire/Qt) - can be null to auto-create
         * @param library Media library for metadata
         * @param parent QObject parent
         */
        explicit MediaPlayer(std::unique_ptr<AudioOutput> output = nullptr,
                             std::shared_ptr<Library> library = nullptr,
                             QObject *parent = nullptr);
        ~MediaPlayer();

        // Core playback controls
        Q_INVOKABLE void load(const QUrl &url);
        Q_INVOKABLE void play();
        Q_INVOKABLE void pause();
        Q_INVOKABLE void togglePause();
        Q_INVOKABLE void stop();
        Q_INVOKABLE void seek(qint64 positionMs);
        Q_INVOKABLE void seekSeconds(double seconds);
        Q_INVOKABLE void next();
        Q_INVOKABLE void previous();

        // Playlist integration
        Q_INVOKABLE void setPlaylist(std::shared_ptr<Playlist> playlist);
        Q_INVOKABLE void playAt(int index);

        // Volume & audio
        void setVolume(double volume);
        double volume() const;
        void setMuted(bool muted);
        bool muted() const;

        // DSP/Audio engine access
        AudioEngine* audioEngine() { return m_audioEngine.get(); }

        // Audio output access
        AudioOutput* audioOutput() { return m_output.get(); }
        QString audioBackendName() const {
            return m_output ? AudioOutputFactory::backendName(m_output->backendType()) : "None";
        }

        // State getters
        PlaybackState state() const { return m_state; }
        QUrl source() const { return m_source; }
        qint64 position() const;
        qint64 duration() const;
        TrackMetadata metadata() const { return m_metadata; }
        bool seekable() const { return m_seekable; }
        BackendType activeBackend() const { return m_activeBackend; }
        bool isTrackerMode() const { return m_activeBackend == BackendType::Tracker; }

        // Tracker-specific
        int trackerChannels() const { return m_metadata.channels; }
        int trackerPatterns() const { return m_metadata.patterns; }
        int currentTrackerPattern() const;
        int currentTrackerRow() const;

        // Utility
        Q_INVOKABLE bool isTrackerFile(const QString &path) const;
        Q_INVOKABLE QStringList supportedTrackerFormats() const;

        // Audio output control
        Q_INVOKABLE bool switchAudioBackend(OutputBackend backend);
        Q_INVOKABLE OutputBackend currentAudioBackend() const;

    signals:
        void stateChanged(PlaybackState state);
        void sourceChanged(const QUrl &source);
        void positionChanged(qint64 position);
        void durationChanged(qint64 duration);
        void metadataChanged(const TrackMetadata &metadata);
        void volumeChanged(double volume);
        void mutedChanged(bool muted);
        void seekableChanged(bool seekable);
        void finished();
        void error(const QString &message);
        void backendChanged(BackendType backend);
        void audioBackendChanged();

    private slots:
        // MPV backend handlers
        void onMpvPositionChanged(double position);
        void onMpvDurationChanged(double duration);
        void onMpvStateChanged(int state);
        void onMpvMetadataChanged(const QVariantMap &metadata);
        void onMpvFinished();
        void onMpvError(const QString &message);
        void onMpvAudioData(const QByteArray &data, int sampleRate);

        // Tracker handlers
        void onTrackerPositionChanged();
        void onTrackerFinished();
        void onTrackerError(const QString &message);

        // Audio output handlers
        void onAudioOutputStateChanged(bool playing);
        void onAudioOutputUnderrun();

        // Internal
        void updatePosition();
        void loadNextPlaylistItem();
        void processAudioOutput(float* buffer, int frames);

    private:
        void setState(PlaybackState state);
        void setBackend(BackendType type);
        void setupMpvAudioCallback();
        void syncMetadataFromTracker();
        void syncMetadataFromMpv(const QVariantMap &metadata);
        void resetMetadata();
        void cleanupCurrentPlayback();
        void initializeAudioOutput();

        // Dependencies
        std::shared_ptr<Library> m_library;
        std::shared_ptr<Playlist> m_playlist;
        std::unique_ptr<AudioEngine> m_audioEngine;      // Pillar 1
        std::unique_ptr<MpvBackend> m_mpvBackend;        // Pillar 3
        std::unique_ptr<AudioOutput> m_output;           // Audio output abstraction

        // State
        PlaybackState m_state = PlaybackState::Stopped;
        BackendType m_activeBackend = BackendType::None;
        QUrl m_source;
        TrackMetadata m_metadata;
        bool m_seekable = true;
        double m_volume = 1.0;
        bool m_muted = false;
        bool m_playlistMode = false;
        int m_currentPlaylistIndex = -1;

        // Position tracking
        QTimer m_positionTimer;
        std::chrono::steady_clock::time_point m_lastPositionUpdate;
        qint64 m_trackedPosition = 0;

        // Audio processing buffer (intermediate between MPV and AudioOutput)
        std::vector<float> m_audioBuffer;
        int m_audioSampleRate = 48000;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::PlaybackState)
Q_DECLARE_METATYPE(Aegis::BackendType)
