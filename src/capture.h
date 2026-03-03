// capture.h - Universal multimedia capture subsystem for Aegis
//
// Supports:
// - Screen/Window capture (XDG Desktop Portal / PipeWire)
// - Webcam and capture cards (V4L2)
// - DVB (Digital Video Broadcasting) tuners
// - IP cameras (RTSP/ONVIF)
// - IPTV (HLS, MPEG-TS)
// - Audio capture (PulseAudio/PipeWire)
// - Screenshots (single frame capture)
//
// Integrates with OpenScreen project for zoom keyframes and backgrounds
//

#pragma once

#include <QObject>
#include <QProcess>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QVariantMap>
#include <QVariantList>
#include <QDateTime>
#include <QColor>
#include <QList>
#include <QMap>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <limits>
#include <memory>

// Forward declarations for GStreamer (used in implementation)
typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;
typedef struct _GstMessage GstMessage;

// OpenScreen-compatible zoom keyframe
struct ZoomKeyframe {
    qint64 timestampMs = 0;   ///< Position in the recording (ms from start)
    double depth       = 2.0; ///< Zoom multiplier (1.0 = no zoom, 2.0 = 2×, …)
    double x           = 0.5; ///< Normalised horizontal centre [0,1]
    double y           = 0.5; ///< Normalised vertical   centre [0,1]
    int    durationMs  = 500; ///< Transition duration in ms
};

// Background descriptor (OpenScreen background panel)
enum class BackgroundKind { Wallpaper, SolidColor, Gradient, Image };

struct BackgroundConfig {
    BackgroundKind kind  = BackgroundKind::SolidColor;
    QColor         color = Qt::black;         // for SolidColor
    QColor         gradientFrom = Qt::black;  // for Gradient
    QColor         gradientTo   = Qt::gray;   // for Gradient
    QString        imagePath;                  // for Wallpaper / Image
};

// Export options (mirrors OpenScreen export dialog)
struct CaptureExportOptions {
    bool    exportGif    = false;  ///< GIF export (OpenScreen v1.1)
    bool    exportMp4    = true;
    int     width        = 0;     ///< 0 = source native
    int     height       = 0;
    double  aspectRatio  = 0.0;   ///< 0 = keep source
    int     bitrate      = 2000;  ///< kbps, 0 = default
    int     fps          = 30;    ///< 0 = source native
    QString outputDir;            ///< Empty = Movies/Aegis default
    QString outputFormat = "mp4"; ///< mp4, mkv, avi, gif, etc.
    QString audioCodec   = "aac"; ///< aac, mp3, opus, copy (passthrough)
    QString videoCodec   = "h264"; ///< h264, h265, vp9, copy (passthrough)
};

// Source types supported by the capture subsystem
enum class CaptureSourceType {
    Unknown,
    Screen,           // Entire screen
    Window,           // Specific window
    Webcam,           // USB/Integrated camera
    CaptureCard,      // HDMI/SDI capture devices
    DVB,              // Digital TV tuner (DVB-T/S/C)
    IPCamera,         // Network camera (RTSP/ONVIF)
    IPTV,             // Internet streaming (HLS, MPEG-TS)
    AudioInput,       // Microphone/line-in
    AudioMonitor      // Desktop audio (loopback)
};

// Audio stream information
struct AudioStreamInfo {
    QString id;                // PulseAudio source name, ALSA device, etc.
    QString name;              // Human-readable name
    int channels = 2;          // 1=mono, 2=stereo, 6=5.1, etc.
    int sampleRate = 48000;    // Hz
    bool isDefault = false;
    QStringList codecs;        // Supported encodings
};

// Video stream information
struct VideoStreamInfo {
    QString id;                // Device path, URL, etc.
    QString name;              // Human-readable name
    QList<QSize> resolutions;  // Supported resolutions
    QList<int> fpsList;        // Supported framerates
    QStringList codecs;        // Supported video codecs
    bool hasAudio = false;     // Whether source includes audio
    AudioStreamInfo audioInfo; // Audio info if present
};

// Complete source information
struct CaptureSourceInfo {
    QString id;                 // Unique identifier
    QString name;               // Display name
    CaptureSourceType type;      // Source category
    QString backend;            // Which backend handles this (portal/v4l2/dvb/etc.)
    QVariantMap capabilities;    // Backend-specific capabilities
    QList<VideoStreamInfo> videoStreams;  // Available video streams
    QList<AudioStreamInfo> audioStreams;  // Available audio streams
    QMap<QString, QString> metadata;      // Additional info (vendor, model, etc.)
    bool isAvailable = true;    // Whether device is currently accessible
};

// Recording state and progress
struct RecordingStats {
    qint64 durationMs = 0;      // Recording duration
    qint64 bytesWritten = 0;    // Data written
    int currentFps = 0;         // Current encoding FPS
    int droppedFrames = 0;      // Frames dropped by encoder
    double bitrate = 0;         // Current bitrate (kbps)
};

// Main capture class
class Capture : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(RecordingStats stats READ stats NOTIFY statsUpdated)

public:
    explicit Capture(QObject *parent = nullptr);
    ~Capture() override;

    // Basic state
    bool recording() const { return m_recording; }
    RecordingStats stats() const { return m_stats; }

    // Source enumeration (discover available capture devices)
    Q_INVOKABLE QList<CaptureSourceInfo> listSources(CaptureSourceType type = CaptureSourceType::Unknown);
    Q_INVOKABLE CaptureSourceInfo getSourceInfo(const QString &sourceId);

    // Capture control
    Q_INVOKABLE bool startCapture(const QString &sourceId, const QString &audioSourceId = QString());
    Q_INVOKABLE bool startCaptureWithOptions(const QString &sourceId,
                                             const CaptureExportOptions &options,
                                             const QString &audioSourceId = QString());
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void pauseRecording();
    Q_INVOKABLE void resumeRecording();

    // Screenshot (single frame)
    Q_INVOKABLE bool captureScreenshot(const QString &sourceId,
                                       const QString &outputPath = QString(),
                                       const QSize &size = QSize());

    // Audio-only recording
    Q_INVOKABLE bool recordAudio(const QString &audioSourceId,
                                 const QString &outputPath = QString(),
                                 int durationSeconds = 0); // 0 = indefinite

    // IP camera/streaming specific
    Q_INVOKABLE bool addIPCamera(const QString &name, const QUrl &url,
                                 const QString &username = QString(),
                                 const QString &password = QString());
    Q_INVOKABLE bool removeIPCamera(const QString &cameraId);
    Q_INVOKABLE QList<CaptureSourceInfo> listIPCameras();

    // DVB specific
    Q_INVOKABLE QList<CaptureSourceInfo> scanDVBTChannels(const QString &adapter = QString());
    Q_INVOKABLE bool tuneDVBChannel(const QString &channelId);

    // OpenScreen-compatible features
    Q_INVOKABLE void addZoomKeyframe(qint64 timestampMs,
                                     double depth = 2.0,
                                     double cx    = 0.5,
                                     double cy    = 0.5,
                                     int    durationMs = 500);
    Q_INVOKABLE void removeZoomKeyframe(qint64 timestampMs);
    Q_INVOKABLE void setZoomKeyframes(const QList<ZoomKeyframe>& keyframes);
    const QList<ZoomKeyframe>& zoomKeyframes() const { return m_zoomKeyframes; }

    Q_INVOKABLE void setBackground(const BackgroundConfig& bg);
    const BackgroundConfig& background() const { return m_background; }

    Q_INVOKABLE void setExportOptions(const CaptureExportOptions& opts);
    const CaptureExportOptions& exportOptions() const { return m_exportOptions; }

    Q_INVOKABLE void setCursorOverlayEnabled(bool enabled);
    bool cursorOverlayEnabled() const { return m_cursorOverlayEnabled; }

signals:
    // State changes
    void recordingChanged();
    void statsUpdated(const RecordingStats &stats);

    // Errors and warnings
    void error(const QString &message);
    void warning(const QString &message);

    // Source availability
    void sourceAdded(const CaptureSourceInfo &source);
    void sourceRemoved(const QString &sourceId);
    void sourceChanged(const QString &sourceId);

    // Recording events
    void recordingStarted(const QString &outputPath);
    void recordingPaused();
    void recordingResumed();
    void recordingFinished(const QString &outputPath, const RecordingStats &finalStats);

    // Portal/XDP integration
    void streamReady(const QString &pipeline, uint nodeId);

    // OpenScreen integration
    void zoomKeyframesChanged();

private slots:
    // Portal response handler
    void onPortalResponse(uint code, const QVariantMap &results);

    // GStreamer bus messages
    void onGstBusMessage(GstMessage *msg);

    // Process handlers (FFmpeg fallback)
    void onProcessFinished(int code, QProcess::ExitStatus status);
    void onProcessOutput();
    void onProcessError();

private:
    // Portal handshake (Wayland screen capture)
    enum class PortalStep { Idle, CreateSession, SelectSources, Start };
    bool sendCreateSession();
    bool sendSelectSources();
    bool sendStart();
    QString nextRequestPath();
    QString buildGStreamerPipeline(uint nodeId) const;

    // Backend implementations
    QList<CaptureSourceInfo> enumerateV4L2Sources();
    QList<CaptureSourceInfo> enumerateDVBSources();
    QList<CaptureSourceInfo> enumeratePulseAudioSources();
    QList<CaptureSourceInfo> enumerateIPCameras();
    QList<CaptureSourceInfo> enumeratePortalSources();

    // GStreamer pipeline management
    bool initializeGStreamer();
    void shutdownGStreamer();
    bool buildPipelineForSource(const CaptureSourceInfo &source,
                                const QString &audioSourceId = QString());
    void updatePipelineElements();

    // Recording control
    void startGstRecording();
    void stopGstRecording();
    void pauseGstRecording();
    void resumeGstRecording();

    // Helpers
    QString generateOutputPath(const QString &extension = "mp4");
    void updateStats();

    // Members
    // Core state
    bool m_recording = false;
    bool m_paused = false;
    RecordingStats m_stats;
    CaptureExportOptions m_exportOptions;

    // Current capture source
    CaptureSourceInfo m_currentSource;
    QString m_currentAudioSource;
    QString m_outputPath;

    // GStreamer elements
    GstElement *m_pipeline = nullptr;
    GstBus *m_bus = nullptr;
    uint m_busWatchId = 0;

    // FFmpeg fallback (for systems without GStreamer)
    QProcess *m_ffmpegProcess = nullptr;

    // Portal state (Wayland screen capture)
    QDBusInterface *m_portal = nullptr;
    PortalStep m_portalStep = PortalStep::Idle;
    QString m_sessionHandle;
    QString m_sessionToken;
    uint m_requestSerial = 0;

    // OpenScreen state
    QList<ZoomKeyframe> m_zoomKeyframes;
    BackgroundConfig m_background;
    bool m_cursorOverlayEnabled = false;

    // Source cache
    QMap<QString, CaptureSourceInfo> m_sourceCache;
    QTimer *m_statsTimer = nullptr;
};
