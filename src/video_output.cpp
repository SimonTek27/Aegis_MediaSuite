// video_output.cpp - Video output implementation with PTS audio synchronization
// Part of Aegis Multimedia Suite
//
// CORRELATION NOTES:
// - Implements video_output.h interfaces
// - Provides OpenGL rendering with proper context management
// - Used by VideoEditor for frame presentation
//

#include "video_output.h"
#include "audio_output.h"
#include <QDebug>
#include <QThread>
#include <QElapsedTimer>
#include <QtMath>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <stdexcept>

namespace Aegis {

    // =============================================================================
    // VideoOutput Base Implementation
    // =============================================================================

    VideoOutput::VideoOutput(QObject* parent) : QObject(parent) {}

    VideoOutput::~VideoOutput() = default;

    void VideoOutput::setAudioOutput(AudioOutput* audio) {
        m_audioOutput = audio;
        if (audio) {
            connect(audio, &QObject::destroyed, this, [this]() {
                m_audioOutput = nullptr;
            });
        }
    }

    void VideoOutput::setMasterClock(std::function<qint64()> clock) {
        m_masterClock = clock;
    }

    qint64 VideoOutput::currentMasterClock() const {
        if (m_masterClock)
            return m_masterClock();
        return QDateTime::currentMSecsSinceEpoch() * 1000;
    }

    void VideoOutput::setVideoLatency(qint64 microseconds) {
        m_videoLatency = microseconds;
    }

    void VideoOutput::setAudioLatency(qint64 microseconds) {
        m_audioLatency = microseconds;
    }

    bool VideoOutput::shouldDisplayFrame(const VideoPTS& videoPts, qint64 audioPts) {
        if (!m_audioSyncEnabled || audioPts < 0)
            return true;

        qint64 videoTime = videoPts.pts + m_videoLatency.load();
        qint64 audioTime = audioPts    + m_audioLatency.load();
        qint64 diff      = videoTime  - audioTime;  // positive = video ahead

        const qint64 EARLY_THRESHOLD = -40000;   // 40 ms early  → wait
        const qint64 LATE_THRESHOLD  =  80000;   // 80 ms late   → drop

        if (diff < EARLY_THRESHOLD) {
            emit syncStatus("early");
            return false;
        }
        if (diff > LATE_THRESHOLD) {
            emit syncStatus("late");
            handleFrameDrop(videoPts);
            return false;
        }

        emit syncStatus("sync");
        return true;
    }

    qint64 VideoOutput::calculateDelay(const VideoPTS& videoPts, qint64 audioPts) {
        if (!m_audioSyncEnabled) return 0;
        qint64 videoTime = videoPts.pts + m_videoLatency.load();
        qint64 audioTime = audioPts    + m_audioLatency.load();
        return -(videoTime - audioTime);
    }

    void VideoOutput::handleFrameDrop(const VideoPTS& pts) {
        m_framesDropped++;
        qDebug() << "Video frame dropped at PTS:" << pts.pts;
    }

    // =============================================================================
    // OpenGLVideoOutput Implementation
    // =============================================================================

    OpenGLVideoOutput::OpenGLVideoOutput(QOpenGLContext* context, QObject* parent)
    : VideoOutput(parent)
    , m_context(context)
    , m_ownContext(context == nullptr)
    {}

    OpenGLVideoOutput::~OpenGLVideoOutput() {
        shutdown();
    }

    bool OpenGLVideoOutput::initialize(const QSize& resolution, VideoBackend backend) {
        Q_UNUSED(backend)
        if (m_initialized) return true;

        try {
            // [Fix #4] Create context + offscreen surface
            if (!m_context) {
                m_context = new QOpenGLContext(this);
                QSurfaceFormat format;
                format.setRenderableType(QSurfaceFormat::OpenGL);
                format.setProfile(QSurfaceFormat::CoreProfile);
                format.setVersion(3, 3);
                format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
                format.setDepthBufferSize(24);
                format.setStencilBufferSize(8);
                m_context->setFormat(format);

                if (!m_context->create()) {
                    emit error("Failed to create OpenGL context");
                    return false;
                }
            }

            // A freshly created context has no surface; attach an offscreen one
            if (!m_context->surface()) {
                m_offscreenSurface = new QOffscreenSurface();
                m_offscreenSurface->setFormat(m_context->format());
                m_offscreenSurface->create();

                if (!m_offscreenSurface->isValid()) {
                    emit error("Failed to create offscreen surface");
                    delete m_offscreenSurface;
                    m_offscreenSurface = nullptr;
                    return false;
                }
            }

            QSurface* surface = m_offscreenSurface
            ? static_cast<QSurface*>(m_offscreenSurface)
            : m_context->surface();

            if (!m_context->makeCurrent(surface)) {
                emit error("Failed to make OpenGL context current");
                return false;
            }

            if (!initializeOpenGLFunctions()) {
                emit error("Failed to initialize OpenGL functions");
                m_context->doneCurrent();
                return false;
            }

            initializeGL();
            setResolution(resolution);
            m_initialized = true;

            m_context->doneCurrent();
            return true;

        } catch (const std::exception& e) {
            emit error(QString("OpenGL initialization exception: %1").arg(e.what()));
            return false;
        }
    }

    void OpenGLVideoOutput::makeCurrent() {
        if (!m_context) return;

        QSurface* surf = m_offscreenSurface
        ? static_cast<QSurface*>(m_offscreenSurface)
        : m_context->surface();

        if (!surf || !surf->isValid()) {
            qWarning() << "OpenGLVideoOutput: Invalid surface for makeCurrent";
            return;
        }

        m_context->makeCurrent(surf);
    }

    void OpenGLVideoOutput::doneCurrent() {
        if (m_context) m_context->doneCurrent();
    }

    void OpenGLVideoOutput::shutdown() {
        if (!m_initialized) return;

        makeCurrent();

        m_videoTexture.reset();
        m_ibo.reset();
        m_vbo.reset();
        m_vao.reset();
        m_shaderProgram.reset();
        m_fbo.reset();

        doneCurrent();

        if (m_offscreenSurface) {
            m_offscreenSurface->destroy();
            delete m_offscreenSurface;
            m_offscreenSurface = nullptr;
        }

        if (m_ownContext && m_context) {
            m_context->deleteLater();
            m_context = nullptr;
        }

        m_initialized = false;
    }

    void OpenGLVideoOutput::initializeGL() {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        createShaderProgram();
        setupGeometry();
        m_fbo = std::make_unique<QOpenGLFramebufferObject>(m_resolution);

        if (!m_fbo->isValid()) {
            qWarning() << "OpenGLVideoOutput: FBO creation failed";
        }
    }

    void OpenGLVideoOutput::createShaderProgram() {
        const char* vertexShader = R"(
            #version 330 core
            layout(location = 0) in vec2 position;
            layout(location = 1) in vec2 texCoord;
            out vec2 vTexCoord;
            void main() {
                gl_Position = vec4(position, 0.0, 1.0);
                vTexCoord = texCoord;
            }
        )";

        const char* fragmentShader = R"(
            #version 330 core
            in vec2 vTexCoord;
            out vec4 fragColor;
            uniform sampler2D videoTexture;
            void main() {
                fragColor = texture(videoTexture, vTexCoord);
            }
        )";

        m_shaderProgram = std::make_unique<QOpenGLShaderProgram>();

        if (!m_shaderProgram->addShaderFromSourceCode(
            QOpenGLShader::Vertex,
            m_customVertexShader.isEmpty() ? vertexShader : m_customVertexShader)) {
            qWarning() << "OpenGLVideoOutput: Vertex shader compilation failed";
            }

            if (!m_shaderProgram->addShaderFromSourceCode(
                QOpenGLShader::Fragment,
                m_customFragmentShader.isEmpty() ? fragmentShader : m_customFragmentShader)) {
                qWarning() << "OpenGLVideoOutput: Fragment shader compilation failed";
                }

                if (!m_shaderProgram->link()) {
                    qWarning() << "OpenGLVideoOutput: Shader program linking failed";
                }

                m_shadersDirty = false;
    }

    void OpenGLVideoOutput::setupGeometry() {
        float vertices[] = {
            // Position    // TexCoord
            -1.0f, -1.0f,  0.0f, 1.0f,
            1.0f, -1.0f,  1.0f, 1.0f,
            1.0f,  1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 0.0f
        };
        unsigned int indices[] = {0, 1, 2, 0, 2, 3};

        m_vao = std::make_unique<QOpenGLVertexArrayObject>();
        if (!m_vao->create()) {
            qWarning() << "OpenGLVideoOutput: VAO creation failed";
            return;
        }
        m_vao->bind();

        m_vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
        m_vbo->create();
        m_vbo->bind();
        m_vbo->allocate(vertices, sizeof(vertices));

        m_ibo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer);
        m_ibo->create();
        m_ibo->bind();
        m_ibo->allocate(indices, sizeof(indices));

        m_shaderProgram->enableAttributeArray(0);
        m_shaderProgram->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
        m_shaderProgram->enableAttributeArray(1);
        m_shaderProgram->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

        m_vao->release();
    }

    void OpenGLVideoOutput::presentFrame(const VideoFrame& frame) {
        qint64 audioPts = m_audioOutput
        ? QDateTime::currentMSecsSinceEpoch() * 1000   // placeholder
        : -1;
        presentFrameWithSync(frame, audioPts);
    }

    void OpenGLVideoOutput::presentFrameWithSync(const VideoFrame& frame, qint64 audioPts) {
        if (!m_initialized) return;
        if (!shouldDisplayFrame(frame.pts, audioPts)) return;

        makeCurrent();

        QMutexLocker locker(&m_frameMutex);
        m_currentFrame = frame;
        uploadFrame(frame);
        renderFrame();

        doneCurrent();

        m_framesDisplayed++;
        emit framePresented(frame.pts);
    }

    // [Fix #5] Always call setData when image is non-null
    void OpenGLVideoOutput::uploadFrame(const VideoFrame& frame) {
        if (frame.image.isNull()) {
            qWarning() << "OpenGLVideoOutput: Attempt to upload null image";
            return;
        }

        bool needsAlloc = !m_videoTexture
        || m_videoTexture->width()  != frame.image.width()
        || m_videoTexture->height() != frame.image.height();

        if (needsAlloc) {
            m_videoTexture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
            m_videoTexture->setMinificationFilter(QOpenGLTexture::Linear);
            m_videoTexture->setMagnificationFilter(QOpenGLTexture::Linear);
            m_videoTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
            m_videoTexture->setSize(frame.image.width(), frame.image.height());
            m_videoTexture->setFormat(QOpenGLTexture::RGBA8_UNorm);
            m_videoTexture->allocateStorage();
        }

        // [Fix #5] Always upload pixel data (even when texture object was reused)
        QImage glImage = frame.image.convertToFormat(QImage::Format_RGBA8888);
        m_videoTexture->bind();
        m_videoTexture->setData(0,
                                QOpenGLTexture::RGBA,
                                QOpenGLTexture::UInt8,
                                glImage.constBits());
    }

    void OpenGLVideoOutput::renderFrame() {
        if (!m_fbo || !m_fbo->isValid() || !m_shaderProgram) {
            qWarning() << "OpenGLVideoOutput: Invalid state for rendering";
            return;
        }

        if (!m_fbo->bind()) {
            qWarning() << "OpenGLVideoOutput: Failed to bind FBO";
            return;
        }

        glViewport(0, 0, m_resolution.width(), m_resolution.height());
        glClear(GL_COLOR_BUFFER_BIT);

        m_shaderProgram->bind();
        m_vao->bind();

        if (m_videoTexture && m_videoTexture->isCreated()) {
            m_videoTexture->bind(0);
            m_shaderProgram->setUniformValue("videoTexture", 0);
        } else {
            qWarning() << "OpenGLVideoOutput: No valid texture to render";
        }

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

        m_vao->release();
        m_shaderProgram->release();
        m_fbo->release();

        emit frameRendered(m_fbo->texture(), m_resolution);
    }

    void OpenGLVideoOutput::setResolution(const QSize& res) {
        if (m_resolution == res) return;
        m_resolution = res;

        makeCurrent();

        if (m_fbo) {
            m_fbo.reset();
            m_fbo = std::make_unique<QOpenGLFramebufferObject>(m_resolution);
        }

        doneCurrent();
        emit resolutionChanged(res);
    }

    void OpenGLVideoOutput::setVsyncEnabled(bool enabled) {
        m_vsyncEnabled = enabled;
        emit vsyncEnabledChanged(enabled);
    }

    void OpenGLVideoOutput::setEffectShader(const QString& vertexShader,
                                            const QString& fragmentShader) {
        m_customVertexShader   = vertexShader;
        m_customFragmentShader = fragmentShader;
        m_shadersDirty = true;
        if (m_initialized) createShaderProgram();
                                            }

                                            void OpenGLVideoOutput::clearEffectShader() {
                                                m_customVertexShader.clear();
                                                m_customFragmentShader.clear();
                                                m_shadersDirty = true;
                                                if (m_initialized) createShaderProgram();
                                            }

                                            QImage OpenGLVideoOutput::captureFrame() {
                                                QMutexLocker locker(&m_frameMutex);
                                                if (!m_fbo || !m_fbo->isValid()) return {};
                                                return m_fbo->toImage();
                                            }

                                            void OpenGLVideoOutput::bindFBO() {
                                                if (m_fbo) m_fbo->bind();
                                            }

                                            void OpenGLVideoOutput::releaseFBO() {
                                                if (m_fbo) m_fbo->release();
                                            }

                                            // =============================================================================
                                            // QtVideoOutput Implementation (PIMPL)
                                            // =============================================================================

                                            class QtVideoOutput::Impl {
                                            public:
                                                QVideoSink* videoSink = nullptr;
                                                QSize       resolution;
                                                double      frameRate  = 30.0;
                                                bool        initialized = false;
                                            };

                                            QtVideoOutput::QtVideoOutput(QObject* parent)
                                            : VideoOutput(parent)
                                            , m_impl(std::make_unique<Impl>())
                                            {}

                                            QtVideoOutput::~QtVideoOutput() = default;

                                            bool QtVideoOutput::initialize(const QSize& resolution, VideoBackend backend) {
                                                Q_UNUSED(backend)
                                                m_impl->resolution  = resolution;
                                                m_impl->videoSink   = new QVideoSink(this);
                                                m_impl->initialized = true;
                                                return true;
                                            }

                                            void QtVideoOutput::shutdown() {
                                                delete m_impl->videoSink;
                                                m_impl->videoSink   = nullptr;
                                                m_impl->initialized = false;
                                            }

                                            bool   QtVideoOutput::isInitialized() const { return m_impl->initialized; }
                                            QSize  QtVideoOutput::resolution()    const { return m_impl->resolution;  }
                                            double QtVideoOutput::frameRate()     const { return m_impl->frameRate;   }
                                            bool   QtVideoOutput::vsyncEnabled()  const { return true; }

                                            void QtVideoOutput::setResolution(const QSize& res) { m_impl->resolution = res; }
                                            void QtVideoOutput::setFrameRate(double fps)        { m_impl->frameRate  = fps; }
                                            void QtVideoOutput::setVsyncEnabled(bool)           {}

                                            void QtVideoOutput::presentFrame(const VideoFrame& frame) {
                                                if (!m_impl->videoSink || frame.image.isNull()) return;
                                                m_impl->videoSink->setVideoFrame(QVideoFrame(frame.image));
                                            }

                                            void QtVideoOutput::presentFrameWithSync(const VideoFrame& frame, qint64 audioPts) {
                                                if (shouldDisplayFrame(frame.pts, audioPts))
                                                    presentFrame(frame);
                                            }

                                            QImage QtVideoOutput::captureFrame() { return {}; }

                                            void* QtVideoOutput::videoSink() const { return m_impl->videoSink; }

                                            // =============================================================================
                                            // VideoOutputFactory
                                            // =============================================================================

                                            std::unique_ptr<VideoOutput> VideoOutputFactory::create(VideoBackend backend,
                                                                                                    QOpenGLContext* context) {
                                                switch (backend) {
                                                    case VideoBackend::OpenGL:
                                                        return std::make_unique<OpenGLVideoOutput>(context);
                                                    case VideoBackend::QtMultimedia:
                                                        return std::make_unique<QtVideoOutput>();
                                                    case VideoBackend::CPU:
                                                    case VideoBackend::Null:
                                                    default:
                                                        return std::make_unique<QtVideoOutput>();
                                                }
                                                                                                    }

                                                                                                    bool VideoOutputFactory::isBackendAvailable(VideoBackend backend) {
                                                                                                        switch (backend) {
                                                                                                            case VideoBackend::OpenGL:
                                                                                                                return QOpenGLContext::openGLModuleType() != QOpenGLContext::LibGLES;
                                                                                                            case VideoBackend::QtMultimedia:
                                                                                                                return true;
                                                                                                            default:
                                                                                                                return false;
                                                                                                        }
                                                                                                    }

                                                                                                    QString VideoOutputFactory::backendName(VideoBackend backend) {
                                                                                                        switch (backend) {
                                                                                                            case VideoBackend::OpenGL:        return "OpenGL";
                                                                                                            case VideoBackend::QtMultimedia:  return "Qt Multimedia";
                                                                                                            case VideoBackend::CPU:           return "Software";
                                                                                                            case VideoBackend::Null:          return "Null";
                                                                                                            default:                          return "Unknown";
                                                                                                        }
                                                                                                    }

} // namespace Aegis
