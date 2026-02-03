// video.h
#pragma once

#include <QObject>
#include <QImage>
#include <QUrl>
#include <memory>
#include <atomic>

// Forward declarations for mpv
struct mpv_handle;
struct mpv_render_context;

namespace Aegis {

    class VideoEngine : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
        Q_PROPERTY(QSize resolution READ resolution NOTIFY resolutionChanged)
        Q_PROPERTY(qint64 renderTargetId READ renderTargetId NOTIFY renderTargetChanged)

    public:
        explicit VideoEngine(QObject *parent = nullptr);
        ~VideoEngine() override;

        // Non-copyable
        VideoEngine(const VideoEngine&) = delete;
        VideoEngine& operator=(const VideoEngine&) = delete;

        // Playback control
        void load(const QUrl &url);
        void play();
        void pause();
        void stop();
        void seek(double position);

        // Rendering
        void renderFrame(); // Call from render thread
        QImage captureFrame(); // For thumbnails

        // Properties
        bool hasVideo() const { return m_hasVideo.load(); }
        QSize resolution() const;
        qint64 renderTargetId() const; // OpenGL texture ID or similar

        // Synchronization with audio
        void setAudioSync(double delay); // Adjust for A/V sync

    signals:
        void hasVideoChanged(bool hasVideo);
        void resolutionChanged(const QSize &res);
        void renderTargetChanged(qint64 id);
        void frameReady(const QImage &frame);
        void error(const QString &message);

    private:
        void initializeMpv();
        void updateVideoFormat();
        static void onMpvUpdate(void *ctx);
        void handleMpvEvent();

        struct MpvDeleter {
            void operator()(mpv_handle *p) const;
        };
        struct MpvRenderDeleter {
            void operator()(mpv_render_context *p) const;
        };

        std::unique_ptr<mpv_handle, MpvDeleter> m_mpv;
        std::unique_ptr<mpv_render_context, MpvRenderDeleter> m_renderContext;

        std::atomic<bool> m_hasVideo{false};
        std::atomic<bool> m_initialized{false};
        double m_position{0.0};
        QSize m_resolution;
        uint32_t m_fbo{0}; // Framebuffer object for GL rendering
    };

} // namespace Aegis
