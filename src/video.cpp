// video.cpp
#include "video.h"
#include <QDebug>
#include <QOpenGLFramebufferObject>
#include <QOpenGLContext>
#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace Aegis {

    void VideoEngine::MpvDeleter::operator()(mpv_handle *p) const {
        if (p) mpv_terminate_destroy(p);
    }

    void VideoEngine::MpvRenderDeleter::operator()(mpv_render_context *p) const {
        if (p) mpv_render_context_free(p);
    }

    VideoEngine::VideoEngine(QObject *parent)
    : QObject(parent)
    {
        initializeMpv();
    }

    VideoEngine::~VideoEngine() {
        // Ensure GL context is released before destruction
        if (m_renderContext) {
            mpv_render_context_set_update_callback(m_renderContext.get(), nullptr, nullptr);
        }
    }

    void VideoEngine::initializeMpv() {
        m_mpv.reset(mpv_create());
        if (!m_mpv) {
            throw std::runtime_error("Failed to create mpv instance for video");
        }

        // Hardware decoding
        mpv_set_option_string(m_mpv.get(), "hwdec", "auto");
        mpv_set_option_string(m_mpv.get(), "vo", "libmpv");
        mpv_set_option_string(m_mpv.get(), "keepaspect", "yes");

        // Initialize
        if (mpv_initialize(m_mpv.get()) < 0) {
            throw std::runtime_error("Failed to initialize mpv video");
        }

        // Setup render context (GL)
        mpv_opengl_init_params gl_init_params{
            [](void *ctx, const char *name) -> void* {
                auto *gl = static_cast<QOpenGLContext*>(ctx);
                return reinterpret_cast<void*>(gl->getProcAddress(QByteArray(name)));
            },
            QOpenGLContext::currentContext()
        };

        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };

        mpv_render_context *render_ctx = nullptr;
        if (mpv_render_context_create(&render_ctx, m_mpv.get(), params) < 0) {
            throw std::runtime_error("Failed to create mpv render context");
        }
        m_renderContext.reset(render_ctx);

        mpv_render_context_set_update_callback(m_renderContext.get(), onMpvUpdate, this);
    }

    void VideoEngine::load(const QUrl &url) {
        if (!m_mpv) return;

        const QByteArray bytes = url.toString().toUtf8();
        const char *cmd[] = {"loadfile", bytes.constData(), nullptr};
        mpv_command(m_mpv.get(), cmd);

        // Probe for video stream
        char *format = nullptr;
        mpv_get_property(m_mpv.get(), "video-format", MPV_FORMAT_STRING, &format);
        bool hasVid = (format != nullptr);
        if (format) mpv_free(format);

        m_hasVideo.store(hasVid);
        emit hasVideoChanged(hasVid);

        if (hasVid) {
            updateVideoFormat();
        }
    }

    void VideoEngine::play() {
        if (m_mpv) {
            mpv_set_property_string(m_mpv.get(), "pause", "no");
        }
    }

    void VideoEngine::pause() {
        if (m_mpv) {
            mpv_set_property_string(m_mpv.get(), "pause", "yes");
        }
    }

    void VideoEngine::stop() {
        if (m_mpv) {
            const char *cmd[] = {"stop", nullptr};
            mpv_command(m_mpv.get(), cmd);
        }
    }

    void VideoEngine::seek(double position) {
        if (m_mpv) {
            mpv_set_property(m_mpv.get(), "time-pos", MPV_FORMAT_DOUBLE, &position);
        }
    }

    QSize VideoEngine::resolution() const {
        if (!m_mpv) return QSize();
        int64_t w = 0, h = 0;
        mpv_get_property(m_mpv.get(), "width", MPV_FORMAT_INT64, &w);
        mpv_get_property(m_mpv.get(), "height", MPV_FORMAT_INT64, &h);
        return QSize(w, h);
    }

    qint64 VideoEngine::renderTargetId() const {
        // Return FBO texture ID for QML to render
        return static_cast<qint64>(m_fbo);
    }

    void VideoEngine::renderFrame() {
        if (!m_renderContext || !m_hasVideo.load()) return;

        // Render to FBO
        mpv_opengl_fbo mpfbo{
            static_cast<int>(m_fbo),
            m_resolution.width(),
            m_resolution.height(),
            0 // internal format
        };

        int flip_y{1};
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };

        mpv_render_context_render(m_renderContext.get(), params);
    }

    void VideoEngine::updateVideoFormat() {
        auto res = resolution();
        if (res != m_resolution) {
            m_resolution = res;
            emit resolutionChanged(res);

            // Recreate FBO if needed
            // (Actual GL FBO management would be here)
        }
    }

    void VideoEngine::onMpvUpdate(void *ctx) {
        auto *self = static_cast<VideoEngine*>(ctx);
        QMetaObject::invokeMethod(self, &VideoEngine::renderFrame, Qt::QueuedConnection);
    }

    void VideoEngine::handleMpvEvent() {
        // Handle mpv events if needed
    }

} // namespace Aegis
