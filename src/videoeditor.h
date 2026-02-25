// videoeditor.h - Professional Video Editor with Full Audio Platform Integration
// Three-Pillar Architecture: audio, audio_effects, mpv_backend
// Video Pipeline: video_output, video_effects, compositor
//


#pragma once

#include "audio.h"
#include "audio_effects.h"
#include "audio_output.h"
#include "mpv_backend.h"
#include "video_output.h"
#include "video_effects.h"
#include <QObject>
#include <QUrl>
#include <QImage>
#include <QSize>
#include <QColor>
#include <QMutex>
#include <QReadWriteLock>
#include <QThreadPool>
#include <QFutureWatcher>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QStack>
#include <memory>
#include <vector>
#include <atomic>
#include <chrono>

namespace Aegis {

    // Forward declarations
    struct mpv_handle;
    struct mpv_render_context;

    // =============================================================================
    // Time & Frame Types (Frame-accurate editing)
    // =============================================================================

    using FrameTime = qint64;  // Frame count for precise editing
    using Timebase = std::ratio<1, 1000000>; // Microseconds

    struct Timecode {
        FrameTime frames = 0;
        int fps = 30;

        double toSeconds() const { return frames / static_cast<double>(fps); }
        qint64 toMicroseconds() const {
            return static_cast<qint64>(toSeconds() * 1000000);
        }
        qint64 toMilliseconds() const {
            return static_cast<qint64>(toSeconds() * 1000);
        }

        static Timecode fromSeconds(double seconds, int fps = 30) {
            return Timecode{static_cast<FrameTime>(seconds * fps), fps};
        }
        static Timecode fromMicroseconds(qint64 us, int fps = 30) {
            return fromSeconds(us / 1000000.0, fps);
        }

        QString toString() const;
        QString toTimeString() const; // "HH:MM:SS:FF"
        bool isValid() const { return fps > 0; }

        bool operator==(const Timecode& other) const {
            return frames == other.frames && fps == other.fps;
        }
        bool operator!=(const Timecode& other) const {
            return !(*this == other);
        }
        bool operator<(const Timecode& other) const {
            return frames < other.frames;
        }
        bool operator>(const Timecode& other) const {
            return frames > other.frames;
        }
        Timecode operator+(const Timecode& other) const {
            return Timecode{frames + other.frames, fps};
        }
        Timecode operator-(const Timecode& other) const {
            return Timecode{frames - other.frames, fps};
        }
    };

    // =============================================================================
    // Project Configuration
    // =============================================================================

    struct ProjectProfile {
        int width = 1920;
        int height = 1080;
        int fps = 30;
        int sampleRate = 48000;
        int audioChannels = 2;
        QString colorSpace = "bt709";
        QString pixelFormat = "yuv420p";

        bool isValid() const {
            return width > 0 && height > 0 && fps > 0 && sampleRate > 0;
        }

        QSize resolution() const { return QSize(width, height); }
        double aspectRatio() const { return width / static_cast<double>(height); }
        QString resolutionString() const {
            return QString("%1x%2@%3fps").arg(width).arg(height).arg(fps);
        }

        QJsonObject toJson() const;
        static ProjectProfile fromJson(const QJsonObject& json);
    };

    // =============================================================================
    // Export Profiles (YouTube, Vimeo, ProRes, etc.)
    // =============================================================================

    enum class ExportPreset {
        Custom,
        YouTube1080p, YouTube4K, YouTubeShorts,
        Vimeo1080p, Vimeo4K,
        ProRes422, ProRes4444,
        H264High, H265High,
        WebOptimized, SocialMedia,
        ArchiveMaster
    };

    struct ExportSettings {
        int width = 1920;
        int height = 1080;
        int fps = 30;
        int videoBitrate = 8000000; // bps
        int audioBitrate = 320000;  // bps
        QString videoCodec = "libx264";
        QString audioCodec = "aac";
        QString container = "mp4";
        QString preset = "medium";
        bool useHardwareEncoding = false;
        bool twoPass = false;

        static ExportSettings preset(ExportPreset preset);
        QStringList toFFmpegArgs(const QString& input, const QString& output) const;
    };

    // =============================================================================
    // Media Clip (Video/Audio/Image on Timeline)
    // =============================================================================

    class MediaClip : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString id READ id CONSTANT)
        Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
        Q_PROPERTY(QUrl source READ source CONSTANT)
        Q_PROPERTY(Timecode position READ position WRITE setPosition NOTIFY positionChanged)
        Q_PROPERTY(Timecode duration READ duration NOTIFY durationChanged)
        Q_PROPERTY(Timecode inPoint READ inPoint WRITE setInPoint NOTIFY inPointChanged)
        Q_PROPERTY(Timecode outPoint READ outPoint WRITE setOutPoint NOTIFY outPointChanged)
        Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
        Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY mediaInfoChanged)
        Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY mediaInfoChanged)
        Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)

    public:
        enum class Type { Video, Audio, Image, ImageSequence, Title, Color, Solid };
        Q_ENUM(Type)

        explicit MediaClip(const QString& id, const QUrl& source, Type type,
                           QObject* parent = nullptr);

        QString id() const { return m_id; }
        QString name() const { return m_name; }
        void setName(const QString& name);
        QUrl source() const { return m_source; }
        Type type() const { return m_type; }

        // Timeline position (where clip starts on track)
        Timecode position() const { return m_position; }
        void setPosition(const Timecode& pos);

        // Source trimming (in/out points in source media)
        Timecode inPoint() const { return m_inPoint; }
        Timecode outPoint() const { return m_outPoint; }
        void setInPoint(const Timecode& pt);
        void setOutPoint(const Timecode& pt);
        Timecode sourceDuration() const { return m_sourceDuration; }

        // Effective duration (accounts for speed)
        Timecode duration() const;
        void updateDuration();

        // Speed/timing
        double speed() const { return m_speed; }
        void setSpeed(double speed);

        // Track assignment
        int trackIndex() const { return m_trackIndex; }
        void setTrackIndex(int idx);

        // Media properties
        bool hasVideo() const { return m_hasVideo; }
        bool hasAudio() const { return m_hasAudio; }
        bool isMuted() const { return m_muted; }
        void setMuted(bool muted);
        QSize videoSize() const { return m_videoSize; }
        int sourceFps() const { return m_sourceFps; }

        // Audio volume (per-clip)
        float volume() const { return m_volume; }
        void setVolume(float vol);

        // Effects stack (Pillar 2: audio_effects for audio, video_effects for video)
        AudioEffectChain* audioEffects() { return m_audioEffects.get(); }
        VideoEffectChain* videoEffects() { return m_videoEffects.get(); }

        // Frame mapping
        Timecode mapToSource(const Timecode& timelineTime) const;
        Timecode mapFromSource(const Timecode& sourceTime) const;

        // Thumbnails
        QImage thumbnail(const Timecode& atTime, const QSize& size = QSize(160, 90));

        // Load media info (probe file)
        bool loadMediaInfo(MpvBackend* backend);

        // Serialization
        QJsonObject toJson() const;
        static std::shared_ptr<MediaClip> fromJson(const QJsonObject& json, QObject* parent = nullptr);

    signals:
        void nameChanged();
        void positionChanged();
        void durationChanged();
        void inPointChanged();
        void outPointChanged();
        void speedChanged();
        void mediaInfoChanged();
        void mutedChanged();
        void error(const QString& message);

    private:
        QString m_id;
        QUrl m_source;
        Type m_type;
        QString m_name;

        Timecode m_position{0, 30};
        Timecode m_inPoint{0, 30};
        Timecode m_outPoint{0, 30};
        Timecode m_sourceDuration{0, 30};
        double m_speed = 1.0;
        int m_trackIndex = -1;

        bool m_hasVideo = false;
        bool m_hasAudio = false;
        bool m_muted = false;
        QSize m_videoSize;
        int m_sourceFps = 30;
        float m_volume = 1.0f;

        std::unique_ptr<AudioEffectChain> m_audioEffects;
        std::unique_ptr<VideoEffectChain> m_videoEffects;

        mutable QMutex m_mutex;
    };

    // =============================================================================
    // Track (Video or Audio track on timeline)
    // =============================================================================

    class Track : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString id READ id CONSTANT)
        Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
        Q_PROPERTY(Type type READ type CONSTANT)
        Q_PROPERTY(int index READ index WRITE setIndex NOTIFY indexChanged)
        Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
        Q_PROPERTY(bool soloed READ isSoloed WRITE setSoloed NOTIFY soloedChanged)
        Q_PROPERTY(bool locked READ isLocked WRITE setLocked NOTIFY lockedChanged)
        Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
        Q_PROPERTY(float pan READ pan WRITE setPan NOTIFY panChanged)

    public:
        enum class Type { Video, Audio };
        Q_ENUM(Type)

        explicit Track(const QString& id, Type type, const QString& name,
                       QObject* parent = nullptr);

        QString id() const { return m_id; }
        QString name() const { return m_name; }
        void setName(const QString& name);

        Type type() const { return m_type; }

        int index() const { return m_index; }
        void setIndex(int idx);

        // Track controls
        bool isMuted() const { return m_muted; }
        void setMuted(bool muted);
        bool isSoloed() const { return m_soloed; }
        void setSoloed(bool soloed);
        bool isLocked() const { return m_locked; }
        void setLocked(bool locked);

        // Audio mixing
        float volume() const { return m_volume; }
        void setVolume(float vol);
        float pan() const { return m_pan; }
        void setPan(float pan); // -1.0 (L) to 1.0 (R)

        // Effects (Pillar 2)
        AudioEffectChain* audioEffects() { return m_audioEffects.get(); }
        VideoEffectChain* videoEffects() { return m_videoEffects.get(); }

        // Clip management
        void addClip(std::shared_ptr<MediaClip> clip);
        void removeClip(std::shared_ptr<MediaClip> clip);
        void moveClip(std::shared_ptr<MediaClip> clip, const Timecode& newPosition);
        std::shared_ptr<MediaClip> clipAt(const Timecode& position) const;
        std::vector<std::shared_ptr<MediaClip>> clipsInRange(const Timecode& start,
                                                             const Timecode& end) const;
        QList<std::shared_ptr<MediaClip>> clips() const;

        Timecode duration() const;
        bool hasOverlap(const Timecode& start, const Timecode& end,
                        std::shared_ptr<MediaClip> exclude = nullptr) const;
        Timecode snapPosition(const Timecode& position, const Timecode& threshold = Timecode{5, 30}) const;

    signals:
        void nameChanged();
        void indexChanged();
        void mutedChanged();
        void soloedChanged();
        void lockedChanged();
        void volumeChanged();
        void panChanged();
        void clipAdded(std::shared_ptr<MediaClip> clip);
        void clipRemoved(std::shared_ptr<MediaClip> clip);
        void clipsChanged();

    private:
        QString m_id;
        Type m_type;
        QString m_name;
        int m_index = 0;
        bool m_muted = false;
        bool m_soloed = false;
        bool m_locked = false;
        float m_volume = 1.0f;
        float m_pan = 0.0f;

        std::vector<std::shared_ptr<MediaClip>> m_clips;
        std::unique_ptr<AudioEffectChain> m_audioEffects;
        std::unique_ptr<VideoEffectChain> m_videoEffects;

        mutable QReadWriteLock m_lock;
    };

    // =============================================================================
    // Marker (for timeline navigation)
    // =============================================================================

    struct TimelineMarker {
        Timecode position;
        QString label;
        QColor color;
        QString type; // "chapter", "cue", "edit", "comment"

        QJsonObject toJson() const;
        static TimelineMarker fromJson(const QJsonObject& json);
    };

    // =============================================================================
    // Compositor (Multi-track video compositing)
    // =============================================================================

    class VideoCompositor : public QObject, protected QOpenGLFunctions {
        Q_OBJECT
    public:
        explicit VideoCompositor(const ProjectProfile& profile, QObject* parent = nullptr);
        ~VideoCompositor();

        bool initialize();
        void shutdown();
        bool isInitialized() const { return m_initialized; }

        // Composite frame from tracks at given time
        QOpenGLFramebufferObject* compositeFrame(const Timecode& time,
                                                 const std::vector<Track*>& videoTracks,
                                                 const std::vector<Track*>& audioTracks);

        // Render to image (for export/thumbnails)
        QImage renderImage(const Timecode& time,
                           const std::vector<Track*>& videoTracks,
                           const QSize& targetSize);

        // Direct OpenGL access
        QOpenGLFramebufferObject* outputFBO() const { return m_outputFBO.get(); }
        quint64 outputTextureId() const {
            return m_outputFBO ? m_outputFBO->texture() : 0;
        }

        // Blend modes
        enum class BlendMode {
            Normal, Add, Multiply, Screen, Overlay,
            SoftLight, HardLight, Difference, Exclusion
        };
        void setBlendMode(BlendMode mode);

    signals:
        void frameComposited(const Timecode& time);
        void error(const QString& message);

    private:
        void initializeShaders();
        void createGeometry();
        void renderClip(std::shared_ptr<MediaClip> clip, const Timecode& time,
                        QOpenGLFramebufferObject* target);
        void blendTracks(const std::vector<Track*>& tracks, const Timecode& time);

        ProjectProfile m_profile;
        bool m_initialized = false;
        BlendMode m_blendMode = BlendMode::Normal;

        // OpenGL resources
        std::unique_ptr<QOpenGLShaderProgram> m_blendShader;
        std::unique_ptr<QOpenGLShaderProgram> m_transformShader;
        std::unique_ptr<QOpenGLVertexArrayObject> m_vao;
        std::unique_ptr<QOpenGLBuffer> m_vbo;
        std::unique_ptr<QOpenGLFramebufferObject> m_outputFBO;
        std::unique_ptr<QOpenGLFramebufferObject> m_tempFBO;

        // Texture cache for clips
        QHash<QString, std::unique_ptr<QOpenGLTexture>> m_textureCache;
    };

    // =============================================================================
    // Transport Control (Playback state machine)
    // =============================================================================

    class TransportController : public QObject {
        Q_OBJECT
        Q_PROPERTY(State state READ state NOTIFY stateChanged)
        Q_PROPERTY(Timecode position READ position WRITE setPosition NOTIFY positionChanged)
        Q_PROPERTY(Timecode duration READ duration NOTIFY durationChanged)
        Q_PROPERTY(double fps READ fps NOTIFY fpsChanged)
        Q_PROPERTY(bool looping READ isLooping WRITE setLooping NOTIFY loopingChanged)

    public:
        enum class State { Stopped, Playing, Paused, Scrubbing, Rendering };
        Q_ENUM(State)

        explicit TransportController(QObject* parent = nullptr);

        // Control
        void play();
        void pause();
        void stop();
        void togglePlayPause();
        void seek(const Timecode& position);
        void frameStep(int frames);

        State state() const { return m_state; }
        Timecode position() const { return m_position; }
        Timecode duration() const { return m_duration; }
        double fps() const { return m_position.fps; }

        void setPosition(const Timecode& pos);
        void setDuration(const Timecode& dur);
        void setFps(int fps);
        void setLooping(bool loop);
        bool isLooping() const { return m_looping; }

        // Loop region
        void setLoopRegion(const Timecode& start, const Timecode& end);
        Timecode loopStart() const { return m_loopStart; }
        Timecode loopEnd() const { return m_loopEnd; }

        // Audio clock sync
        void updateFromAudioClock(qint64 microseconds);

    signals:
        void stateChanged(State state);
        void positionChanged(const Timecode& position);
        void durationChanged(const Timecode& duration);
        void fpsChanged(double fps);
        void loopingChanged(bool looping);
        void frameReady(const Timecode& position);

    private slots:
        void onPlaybackTimer();

    private:
        State m_state = State::Stopped;
        Timecode m_position{0, 30};
        Timecode m_duration{0, 30};
        bool m_looping = false;
        Timecode m_loopStart{0, 30};
        Timecode m_loopEnd{0, 30};

        QTimer* m_playbackTimer = nullptr;
        qint64 m_playbackStartTime = 0;
        Timecode m_playbackStartPosition{0, 30};
    };

    // =============================================================================
    // [FIX Bug #5] TimelineProxy - QObject aggregator exposed to QML as "timeline"
    //
    // VideoEditor non ha un oggetto "timeline" separato — questo proxy
    // aggrega le informazioni necessarie alla UI in un unico QObject
    // accessibile tramite videoEditor.timeline in QML.
    // =============================================================================

    class VideoEditor; // Forward declaration

    class TimelineProxy : public QObject {
        Q_OBJECT
        // Playback position (frame number)
        Q_PROPERTY(FrameTime playhead READ playhead NOTIFY playheadChanged)
        // Total timeline duration in frames
        Q_PROPERTY(FrameTime duration READ duration NOTIFY durationChanged)
        // Project profile (contains fps, width, height, etc.)
        Q_PROPERTY(Aegis::ProjectProfile profile READ profile NOTIFY profileChanged)
        // Track lists
        Q_PROPERTY(QList<QObject*> videoTracks READ videoTracksAsObjects NOTIFY tracksChanged)
        Q_PROPERTY(QList<QObject*> audioTracks READ audioTracksAsObjects NOTIFY tracksChanged)
        Q_PROPERTY(int videoTrackCount READ videoTrackCount NOTIFY tracksChanged)
        Q_PROPERTY(int audioTrackCount READ audioTrackCount NOTIFY tracksChanged)
        // Selection region
        Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
        Q_PROPERTY(FrameTime selectionStart READ selectionStart NOTIFY selectionChanged)
        Q_PROPERTY(FrameTime selectionEnd READ selectionEnd NOTIFY selectionChanged)

    public:
        explicit TimelineProxy(VideoEditor* editor, QObject* parent = nullptr);

        FrameTime playhead() const;
        FrameTime duration() const;
        ProjectProfile profile() const;

        QList<QObject*> videoTracksAsObjects() const;
        QList<QObject*> audioTracksAsObjects() const;
        int videoTrackCount() const;
        int audioTrackCount() const;

        bool hasSelection() const { return m_hasSelection; }
        FrameTime selectionStart() const { return m_selectionStart; }
        FrameTime selectionEnd() const { return m_selectionEnd; }

        // QML-invokable operations delegated to VideoEditor
        Q_INVOKABLE void addVideoTrack();
        Q_INVOKABLE void addAudioTrack();
        Q_INVOKABLE void removeClip(QObject* clip);
        Q_INVOKABLE void splitClip(QObject* clip, FrameTime atFrame);
        Q_INVOKABLE void rippleDelete(FrameTime start, FrameTime end);
        Q_INVOKABLE void insertClip(QObject* clip, QObject* track, FrameTime position);

        // Snap frame to nearest clip edge or grid
        Q_INVOKABLE FrameTime snap(FrameTime frame, int gridSize = 10) const;

        // Called by VideoEditor to push updates
        void notifyPlayheadChanged();
        void notifyDurationChanged();
        void notifyProfileChanged();
        void notifyTracksChanged();
        void setSelection(FrameTime start, FrameTime end);
        void clearSelection();

    signals:
        void playheadChanged();
        void durationChanged();
        void profileChanged();
        void tracksChanged();
        void selectionChanged();

    private:
        VideoEditor* m_editor;  // Non-owning pointer (VideoEditor owns TimelineProxy)
        bool m_hasSelection = false;
        FrameTime m_selectionStart = 0;
        FrameTime m_selectionEnd = 0;
    };

    // =============================================================================
    // VideoEditor - Main controller (Three Pillars)
    // =============================================================================

    class VideoEditor : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
        Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
        Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
        Q_PROPERTY(ProjectProfile profile READ profile WRITE setProfile NOTIFY profileChanged)
        Q_PROPERTY(TransportController::State state READ state NOTIFY stateChanged)
        Q_PROPERTY(Timecode position READ position NOTIFY positionChanged)
        Q_PROPERTY(Timecode duration READ duration NOTIFY durationChanged)
        Q_PROPERTY(bool modified READ isModified NOTIFY modifiedChanged)

        // [FIX Bug #3] Convenience properties for QML — derived from profile/transport
        Q_PROPERTY(bool playing READ isPlaying NOTIFY stateChanged)
        Q_PROPERTY(QSize resolution READ resolution NOTIFY profileChanged)
        Q_PROPERTY(int fps READ fps NOTIFY profileChanged)

        // [FIX Bug #5] Timeline proxy object for QML timeline panel
        Q_PROPERTY(Aegis::TimelineProxy* timeline READ timeline NOTIFY projectChanged)

    public:
        explicit VideoEditor(QObject* parent = nullptr);
        ~VideoEditor() override;

        // ============== Initialization (Three Pillars) ==============
        void initializeAudio(AudioEngine* engine, AudioOutput* output);
        void initializeVideo(std::unique_ptr<VideoOutput> output = nullptr);

        // Pillar access
        AudioEngine* audioEngine() const { return m_audioEngine; }
        AudioOutput* audioOutput() const { return m_audioOutput; }
        VideoOutput* videoOutput() const { return m_videoOutput.get(); }
        MpvBackend* mpvBackend() const { return m_mpvBackend.get(); }
        VideoCompositor* compositor() const { return m_compositor.get(); }

        // ============== Project Management ==============
        Q_INVOKABLE bool newProject(const QString& name, const ProjectProfile& profile = ProjectProfile{});
        Q_INVOKABLE bool openProject(const QString& path);
        Q_INVOKABLE bool saveProject(const QString& path = QString());
        Q_INVOKABLE void closeProject();

        bool hasProject() const { return m_project != nullptr; }
        QString projectPath() const { return m_projectPath; }
        QString projectName() const { return m_projectName; }
        bool isModified() const { return m_modified; }

        ProjectProfile profile() const { return m_profile; }
        void setProfile(const ProjectProfile& profile);

        // [FIX Bug #3] Convenience getters used by Q_PROPERTY above
        bool isPlaying() const {
            return m_transport && m_transport->state() == TransportController::State::Playing;
        }
        QSize resolution() const { return m_profile.resolution(); }
        int fps() const { return m_profile.fps; }

        // [FIX Bug #5] Timeline proxy accessor
        TimelineProxy* timeline() const { return m_timeline.get(); }

        // ============== Track Management ==============
        Track* addVideoTrack(const QString& name = QString());
        Track* addAudioTrack(const QString& name = QString());
        void removeTrack(Track* track);
        void moveTrack(int fromIndex, int toIndex);

        QList<Track*> videoTracks() const;
        QList<Track*> audioTracks() const;
        QList<Track*> allTracks() const;
        Track* trackAt(int index) const;

        // ============== Clip Operations ==============
        Q_INVOKABLE std::shared_ptr<MediaClip> importMedia(const QUrl& url,
                                                           Track* targetTrack = nullptr,
                                                           const Timecode& position = Timecode{0, 30});
        Q_INVOKABLE void removeClip(std::shared_ptr<MediaClip> clip);
        Q_INVOKABLE void moveClip(std::shared_ptr<MediaClip> clip,
                                  Track* targetTrack,
                                  const Timecode& newPosition);
        Q_INVOKABLE void trimClip(std::shared_ptr<MediaClip> clip,
                                  const Timecode& newInPoint,
                                  const Timecode& newOutPoint);
        Q_INVOKABLE void splitClip(std::shared_ptr<MediaClip> clip, const Timecode& atTime);
        Q_INVOKABLE void deleteRange(const Timecode& start, const Timecode& end);
        Q_INVOKABLE void rippleDelete(const Timecode& start, const Timecode& end);

        // ============== Playback Control ==============
        TransportController* transport() const { return m_transport.get(); }
        Q_INVOKABLE void play();
        Q_INVOKABLE void pause();
        Q_INVOKABLE void stop();
        Q_INVOKABLE void togglePlayPause();
        Q_INVOKABLE void seek(const Timecode& position);
        Q_INVOKABLE void frameStep(int frames);

        TransportController::State state() const { return m_transport->state(); }
        Timecode position() const { return m_transport->position(); }
        Timecode duration() const;

        // ============== Preview/Monitoring ==============
        Q_INVOKABLE void setPreviewSize(const QSize& size);
        QImage currentFrame() const { return m_currentFrame; }
        quint64 currentTextureId() const;

        // ============== Export/Render ==============
        Q_INVOKABLE bool exportVideo(const QString& outputPath,
                                     const ExportSettings& settings,
                                     const Timecode& start = Timecode{0, 30},
                                     const Timecode& end = Timecode{0, 30});
        Q_INVOKABLE void startPreviewExport(const QString& outputPath);
        void cancelExport();

        // ============== Audio-Video Integration ==============
        void connectAudioReactiveEffects();
        void setAudioSyncEnabled(bool enabled);
        bool isAudioSyncEnabled() const { return m_audioSyncEnabled; }
        void updateVideoFromAudioClock(qint64 audioMicroseconds);

        // ============== Undo/Redo ==============
        Q_INVOKABLE void undo();
        Q_INVOKABLE void redo();
        Q_INVOKABLE bool canUndo() const;
        Q_INVOKABLE bool canRedo() const;
        void pushUndoCommand(const QString& description,
                             std::function<void()> undo,
                             std::function<void()> redo);

        // ============== Markers ==============
        void addMarker(const Timecode& position, const QString& label,
                       const QColor& color = Qt::yellow, const QString& type = "cue");
        void removeMarker(const Timecode& position);
        QList<TimelineMarker> markers() const;

        // ============== Internal helper (called by TimelineProxy) ==============
        void addVideoTrackFromProxy();
        void addAudioTrackFromProxy();

    signals:
        void projectChanged();
        void profileChanged();
        void stateChanged(TransportController::State state);
        void positionChanged(const Timecode& position);
        void durationChanged(const Timecode& duration);
        void modifiedChanged();
        void trackAdded(Track* track);
        void trackRemoved(Track* track);
        void clipImported(std::shared_ptr<MediaClip> clip);
        void frameReady(const QImage& frame, const Timecode& position);
        void exportProgress(double percent);
        void exportFinished(bool success, const QString& path);
        void warning(const QString& message);
        void error(const QString& message);
        void statusMessage(const QString& message);

    private slots:
        void onTransportPositionChanged(const Timecode& position);
        void onTransportStateChanged(TransportController::State state);
        void onMpvFrameReady();
        void onAudioPositionChanged(qint64 microseconds);

    private:
        void initializePillars();
        void renderFrame(const Timecode& position);
        void compositeAndDisplay(const Timecode& position);
        void syncAudioToVideo(const Timecode& videoPosition);
        void setModified(bool modified);

        // Project data
        struct ProjectData {
            QList<std::unique_ptr<Track>> tracks;
            QList<TimelineMarker> markers;
        };
        std::unique_ptr<ProjectData> m_project;
        QString m_projectPath;
        QString m_projectName;
        ProjectProfile m_profile;
        bool m_modified = false;

        // Three Pillars
        AudioEngine* m_audioEngine = nullptr;      // Pillar 1
        AudioOutput* m_audioOutput = nullptr;      // Pillar 1 extension
        std::unique_ptr<MpvBackend> m_mpvBackend;  // Pillar 3

        // Video system
        std::unique_ptr<VideoOutput> m_videoOutput;
        std::unique_ptr<VideoCompositor> m_compositor;
        std::unique_ptr<TransportController> m_transport;

        // [FIX Bug #5] Timeline proxy
        std::unique_ptr<TimelineProxy> m_timeline;

        // State
        QImage m_currentFrame;
        QSize m_previewSize{960, 540};
        bool m_audioSyncEnabled = true;
        std::atomic<bool> m_exporting{false};

        // Undo/Redo
        struct UndoCommand {
            QString description;
            std::function<void()> undo;
            std::function<void()> redo;
        };
        QStack<UndoCommand> m_undoStack;
        QStack<UndoCommand> m_redoStack;
        int m_maxUndoLevels = 50;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::FrameTime)
Q_DECLARE_METATYPE(Aegis::Timecode)
Q_DECLARE_METATYPE(Aegis::ProjectProfile)
Q_DECLARE_METATYPE(Aegis::ExportSettings)
Q_DECLARE_METATYPE(Aegis::ExportPreset)
Q_DECLARE_METATYPE(Aegis::MediaClip*)
Q_DECLARE_METATYPE(Aegis::Track*)
Q_DECLARE_METATYPE(Aegis::VideoCompositor::BlendMode)
Q_DECLARE_METATYPE(Aegis::TimelineProxy*)
