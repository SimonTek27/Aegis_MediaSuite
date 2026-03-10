// core.h - Core interfaces and types for Aegis MediaSuite
// Provides both the low-level backend interfaces (used by mpv_backend) and
// the high-level Core class used by the main application and QML.
#pragma once

#include "raii_wrappers.h"

namespace Aegis {

// ============================================================================
// PlaybackState - Unified playback state machine
// ============================================================================

enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Buffering,
    Error
};

// ============================================================================
// TrackMetadata - Unified track information
// ============================================================================

struct TrackMetadata {
    QString title;
    QString artist;
    QString album;
    QString genre;
    int     year        { 0 };
    int     trackNumber { 0 };
    int     duration    { 0 };   // seconds
    int     sampleRate  { 0 };
    int     bitRate     { 0 };
    int     channels    { 0 };
    bool    hasVideo    { false };
    bool    isTracker   { false };
    int     patterns    { 0 };
    QUrl    url;
    QString albumArtPath;

    bool isValid() const { return !url.isEmpty(); }
};

// ============================================================================
// IAudioBackend - Abstract audio backend interface
// ============================================================================

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    virtual bool    open(const QUrl& url)  = 0;
    virtual void    close()                = 0;

    virtual void    play()                 = 0;
    virtual void    pause()                = 0;
    virtual void    stop()                 = 0;

    virtual void    seek(qint64 positionMs) = 0;
    virtual qint64  position() const        = 0;
    virtual qint64  duration() const        = 0;

    virtual void    setVolume(double volume) = 0;   // 0.0 – 1.0
    virtual double  volume() const           = 0;

    virtual PlaybackState state() const      = 0;
    virtual TrackMetadata metadata() const   = 0;

    virtual bool    isSeekable() const       = 0;
};

// Forward declarations for high-level interfaces
class IVideoBackend;
class IAudioEngine;

// ============================================================================
// Core playlist item
// ============================================================================

class PlaylistItem {
public:
    enum class Type { File, Url, Stream };

    PlaylistItem(const QUrl& url = QUrl(), Type type = Type::File);

    QUrl url() const { return m_url; }
    QString title() const { return m_title; }
    std::optional<double> duration() const { return m_duration; }

    void setDuration(double d) { m_duration = d; }

private:
    QUrl m_url;
    QString m_title;
    Type m_type{Type::File};
    std::optional<double> m_duration;
};

// ============================================================================
// Core - Main playback / playlist coordinator
// ============================================================================

class Core : public QObject {
    Q_OBJECT

public:
    explicit Core(std::unique_ptr<IAudioBackend> audioBackend,
                  std::unique_ptr<IVideoBackend> videoBackend,
                  std::unique_ptr<IAudioEngine> audioEngine,
                  QObject* parent = nullptr);
    ~Core() override = default;

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void load(const QUrl& url);
    Q_INVOKABLE PlaybackState state() const;

    Q_INVOKABLE void enqueue(const QUrl& url);
    Q_INVOKABLE void clearPlaylist();

    Q_INVOKABLE double position() const { return m_position.load(); }
    Q_INVOKABLE double duration() const { return m_duration.load(); }

    Q_INVOKABLE QString currentTitle() const;
    Q_INVOKABLE QVariantList playlistVariant() const;

    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE double volume() const { return m_volume.load(); }

    Q_INVOKABLE bool hasVideo() const;

signals:
    void error(const QString& message);
    void playlistChanged();
    void currentTrackChanged(const PlaylistItem& item);
    void hasVideoChanged(bool hasVideo);
    void positionChanged(double position);

public slots:
    void dequeue(int index);

private:
    void loadTrackImpl(int index);
    void updateMetadata();

    void onBackendPositionChanged(double pos);
    void onAudioBackendFinished();

    std::unique_ptr<IAudioBackend> m_audioBackend;
    std::unique_ptr<IVideoBackend> m_videoBackend;
    std::unique_ptr<IAudioEngine>  m_audioEngine;

    std::vector<PlaylistItem> m_playlist;
    std::atomic<int>    m_currentPlaylistIndex{-1};
    std::atomic<double> m_position{0.0};
    std::atomic<double> m_duration{0.0};
    std::atomic<double> m_volume{1.0};
};

// ============================================================================
// High-level backend interfaces (used by main.cpp adapters)
// ============================================================================

class IVideoBackend {
public:
    virtual ~IVideoBackend() = default;

    virtual Result<void> load(const QUrl& url) = 0;
    virtual Result<void> play() = 0;
    virtual Result<void> pause() = 0;
    virtual Result<void> stop() = 0;
    virtual Result<void> seek(double position) = 0;

    virtual QImage currentFrame() const = 0;
    virtual QSize  videoSize() const = 0;
    virtual bool   hasVideo() const = 0;
};

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;

    virtual Result<void> processBuffer(float* buffer, int frames, int sampleRate, int channels) = 0;
    virtual Result<std::vector<float>> analyzeSpectrum(int size) = 0;
    virtual double rmsLevel() const = 0;
    virtual double peakLevel() const = 0;
    virtual void   setBpm(double bpm) = 0;
    virtual double bpm() const = 0;
};

} // namespace Aegis
