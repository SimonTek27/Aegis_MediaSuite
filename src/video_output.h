// video_output.h - Video output abstraction with audio synchronization
// Part of Aegis Multimedia Suite
// Integrates with audio platform via PTS (Presentation Timestamp) synchronization

#pragma once

#include <QObject>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QSize>
#include <QImage>
#include <QMutex>
#include <QTimer>
#include <QDateTime>
#include <QOpenGLFunctions>
#include <atomic>
#include <memory>
#include <functional>

// Forward declarations for MPV
struct mpv_handle;
struct mpv_render_context;

namespace Aegis {

    // Forward declarations from audio platform
    class AudioOutput;
    class AudioEngine;

    // =============================================================================
    // Presentation Timestamp (PTS) Synchronization
    // =============================================================================

    struct VideoPTS {
        qint64 pts;              // Presentation timestamp in microseconds
        qint64 duration;         // Frame duration in microseconds
        double timeBase;         // Time base for conversion

        static VideoPTS fromMicroseconds(qint64 us) {
            return VideoPTS{us, 0, 1000000.0};
        }

        static VideoPTS fromMilliseconds(qint64 ms) {
            return VideoPTS{ms * 1000, 0, 1000000.0};
        }

        double toSeconds() const {
            return pts / timeBase;
        }

        bool operator==(const VideoPTS& other) const {
            return pts == other.pts;
        }

        bool operator<(const VideoPTS& other) const {
            return pts < other.pts;
        }
    };

    // =============================================================================
    // Video Frame with Synchronization Data
    // =============================================================================

    struct VideoFrame {
        QImage image;                    // CPU-side image data
        std::unique_ptr<QOpenGLTexture> texture;  // GPU texture (if uploaded)
        VideoPTS pts;                    // Presentation timestamp
        VideoPTS audioPts;               // Corresponding audio PTS for sync
        QSize sourceSize;                // Original video size
        bool isHardwareDecoded = false;
        bool hasAlpha = false;

        // Metadata
        int frameNumber = 0;
        double displayTime = 0.0;        // Calculated display time
    };

    // =============================================================================
    // Video Output Backend Types
    // =============================================================================

    enum class VideoBackend {
        OpenGL,         // OpenGL-based rendering (recommended)
        QtMultimedia,   // Qt6 QVideoSink integration
        CPU,            // Software rendering fallback
        Null            // No output (headless)
    };

    // =============================================================================
    // Abstract Video Output Base Class
    // =============================================================================

    class VideoOutput : public QObject {
        Q_OBJECT
        Q_PROPERTY(QSize resolution READ resolution WRITE setResolution NOTIFY resolutionChanged)
        Q_PROPERTY(double frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged)
        Q_PROPERTY(bool vsyncEnabled READ vsyncEnabled WRITE setVsyncEnabled NOTIFY vsyncEnabledChanged)

    public:
        explicit VideoOutput(QObject* parent = nullptr);
        virtual ~VideoOutput();

        // Core interface
        virtual bool initialize(const QSize& resolution, VideoBackend backend = VideoBackend::OpenGL) = 0;
        virtual void shutdown() = 0;
        virtual bool isInitialized() const = 0;

        // Frame presentation with PTS synchronization
        virtual void presentFrame(const VideoFrame& frame) = 0;
        virtual void presentFrameWithSync(const VideoFrame& frame, qint64 audioPts) = 0;

        // Properties
        virtual QSize resolution() const = 0;
        virtual void setResolution(const QSize& res) = 0;
        virtual double frameRate() const = 0;
        virtual void setFrameRate(double fps) = 0;
        virtual bool vsyncEnabled() const = 0;
        virtual void setVsyncEnabled(bool enabled) = 0;

        // Audio synchronization
        void setAudioOutput(AudioOutput* audio);  // Link to audio for sync
        AudioOutput* audioOutput() const { return m_audioOutput; }

        // PTS-based synchronization
        void setMasterClock(std::function<qint64()> clock);  // External clock source
        qint64 currentMasterClock() const;

        // Sync control
        void enableAudioSync(bool enable) { m_audioSyncEnabled = enable; }
        bool isAudioSyncEnabled() const { return m_audioSyncEnabled; }

        // Latency compensation
        void setVideoLatency(qint64 microseconds);  // Display pipeline latency
        void setAudioLatency(qint64 microseconds);  // Audio pipeline latency

        // Capture
        virtual QImage captureFrame() = 0;
        virtual quint64 captureTextureId() const { return 0; }

    signals:
        void resolutionChanged(const QSize& size);
        void frameRateChanged(double fps);
        void vsyncEnabledChanged(bool enabled);
        void framePresented(const VideoPTS& pts);
        void syncStatus(const QString& status);  // "sync", "late", "early", "drop"
        void error(const QString& message);

    protected:
        // Synchronization logic
        bool shouldDisplayFrame(const VideoPTS& videoPts, qint64 audioPts);
        qint64 calculateDelay(const VideoPTS& videoPts, qint64 audioPts);
        void handleFrameDrop(const VideoPTS& pts);

        AudioOutput* m_audioOutput = nullptr;
        std::function<qint64()> m_masterClock;
        std::atomic<bool> m_audioSyncEnabled{true};
        std::atomic<qint64> m_videoLatency{0};   // μs
        std::atomic<qint64> m_audioLatency{0};   // μs

        // Sync statistics
        std::atomic<int> m_framesDisplayed{0};
        std::atomic<int> m_framesDropped{0};
        std::atomic<double> m_averageDelay{0.0}; // ms
    };

    // =============================================================================
    // OpenGL Video Output (Primary Implementation)
    // =============================================================================

    class OpenGLVideoOutput : public VideoOutput, protected QOpenGLFunctions {
        Q_OBJECT
    public:
        explicit OpenGLVideoOutput(QOpenGLContext* context = nullptr, QObject* parent = nullptr);
        ~OpenGLVideoOutput() override;

        bool initialize(const QSize& resolution, VideoBackend backend = VideoBackend::OpenGL) override;
        void shutdown() override;
        bool isInitialized() const override { return m_initialized; }

        void presentFrame(const VideoFrame& frame) override;
        void presentFrameWithSync(const VideoFrame& frame, qint64 audioPts) override;

        QSize resolution() const override { return m_resolution; }
        void setResolution(const QSize& res) override;
        double frameRate() const override { return m_frameRate; }
        void setFrameRate(double fps) override { m_frameRate = fps; }
        bool vsyncEnabled() const override { return m_vsyncEnabled; }
        void setVsyncEnabled(bool enabled) override;

        QImage captureFrame() override;
        quint64 captureTextureId() const override { return m_fbo ? m_fbo->texture() : 0; }

        // Direct OpenGL access for advanced use
        QOpenGLFramebufferObject* fbo() const { return m_fbo.get(); }
        void bindFBO();
        void releaseFBO();

        // Shader effects pipeline (for video_effects integration)
        void setEffectShader(const QString& vertexShader, const QString& fragmentShader);
        void clearEffectShader();

    signals:
        void frameRendered(quint64 textureId, const QSize& size);

    private:
        void initializeGL();
        void createShaderProgram();
        void uploadFrame(const VideoFrame& frame);
        void renderFrame();
        void setupGeometry();

        QOpenGLContext* m_context = nullptr;
        bool m_ownContext = false;
        bool m_initialized = false;

        // OpenGL resources
        std::unique_ptr<QOpenGLFramebufferObject> m_fbo;
        std::unique_ptr<QOpenGLShaderProgram> m_shaderProgram;
        std::unique_ptr<QOpenGLVertexArrayObject> m_vao;
        std::unique_ptr<QOpenGLBuffer> m_vbo;
        std::unique_ptr<QOpenGLBuffer> m_ibo;
        std::unique_ptr<QOpenGLTexture> m_videoTexture;

        // State
        QSize m_resolution{1920, 1080};
        double m_frameRate = 30.0;
        bool m_vsyncEnabled = true;
        VideoFrame m_currentFrame;
        QMutex m_frameMutex;

        // Effect shaders
        QString m_customVertexShader;
        QString m_customFragmentShader;
        bool m_shadersDirty = true;
    };

    // =============================================================================
    // Qt Multimedia Video Output (Fallback)
    // =============================================================================

    class QtVideoOutput : public VideoOutput {
        Q_OBJECT
    public:
        explicit QtVideoOutput(QObject* parent = nullptr);
        ~QtVideoOutput() override;

        bool initialize(const QSize& resolution, VideoBackend backend = VideoBackend::QtMultimedia) override;
        void shutdown() override;
        bool isInitialized() const override;

        void presentFrame(const VideoFrame& frame) override;
        void presentFrameWithSync(const VideoFrame& frame, qint64 audioPts) override;

        QSize resolution() const override;
        void setResolution(const QSize& res) override;
        double frameRate() const override;
        void setFrameRate(double fps) override;
        bool vsyncEnabled() const override;
        void setVsyncEnabled(bool enabled) override;

        QImage captureFrame() override;

        // Qt Multimedia specific
        void* videoSink() const;  // Returns QVideoSink* (opaque for header)

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };

    // =============================================================================
    // Video Output Factory
    // =============================================================================

    class VideoOutputFactory {
    public:
        static std::unique_ptr<VideoOutput> create(VideoBackend backend = VideoBackend::OpenGL,
                                                   QOpenGLContext* context = nullptr);
        static bool isBackendAvailable(VideoBackend backend);
        static QString backendName(VideoBackend backend);
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::VideoPTS)
Q_DECLARE_METATYPE(Aegis::VideoFrame)
Q_DECLARE_METATYPE(Aegis::VideoBackend)
