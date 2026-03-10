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
#include "core.h"

// Forward declarations
namespace Aegis {
    class Library;
    class Playlist;
    class Settings;
}

namespace Aegis {

    class MpvBackend;  // forward declaration

    enum class BackendType {
        None,
        Mpv,      // Standard audio/video via MPV + AudioOutput
        Tracker   // MOD/XM/IT/S3M via libopenmpt + AudioOutput
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
        bool isMuted() const;

        // DSP/Audio engine access (defined in .cpp via pimpl)
        AudioEngine* audioEngine();
        AudioOutput* audioOutput();
        QString audioBackendName() const;

        // State getters (all defined in .cpp via pimpl)
        PlaybackState state() const;
        QUrl source() const;
        qint64 position() const;
        qint64 duration() const;
        TrackMetadata metadata() const;
        bool seekable() const;
        BackendType activeBackend() const;
        bool isTrackerMode() const;

        // Tracker-specific
        int trackerChannels() const;
        int trackerPatterns() const;
        int currentTrackerPattern() const;
        int currentTrackerRow() const;

        // Utility
        Q_INVOKABLE bool isTrackerFile(const QString &path) const;
        Q_INVOKABLE QStringList supportedTrackerFormats() const;

        // Audio output control
        Q_INVOKABLE bool switchAudioBackend(OutputBackend backend);
        Q_INVOKABLE OutputBackend currentAudioBackend() const;

        // Playlist compat
        Q_INVOKABLE void enqueue(const QUrl& url);
        Q_INVOKABLE void setCurrentIndex(int index) { playAt(index); }
        int currentIndex() const;
        TrackMetadata currentMetadata() const;

        // Repeat / Shuffle
        enum class RepeatMode { None, Track, All };
        Q_ENUM(RepeatMode)
        void setRepeatMode(RepeatMode mode);
        RepeatMode repeatMode() const;
        void setShuffle(bool enabled);
        bool shuffle() const;

    signals:
        void currentTrackChanged();
        void playbackFinished();
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
        void onMpvPositionChanged(double position);
        void onMpvDurationChanged(double duration);
        void onMpvStateChanged(int state);
        void onMpvMetadataChanged(const QVariantMap &metadata);
        void onMpvFinished();
        void onMpvError(const QString &message);
        void onMpvAudioData(const QByteArray &data, int sampleRate);

        void onTrackerPositionChanged();
        void onTrackerFinished();
        void onTrackerError(const QString &message);

        void onAudioOutputStateChanged(bool playing);
        void onAudioOutputUnderrun();

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
        void loadTracker(const QString &path);
        void loadMpv(const QUrl &url);

        class Private;
        std::unique_ptr<Private> d;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::BackendType)
