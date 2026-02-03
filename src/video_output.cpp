// video_output.cpp - Video output implementation with PTS audio synchronization
#include "video_output.h"
#include "audio_output.h"  // For audio clock sync
#include <QDebug>
#include <QThread>
#include <QElapsedTimer>
#include <QtMath>

// Qt Multimedia includes
#include <QVideoSink>
#include <QVideoFrame>

namespace Aegis {

    // =============================================================================
    // VideoOutput Base Implementation
    // =============================================================================

    VideoOutput::VideoOutput(QObject* parent) : QObject(parent) {}

    VideoOutput::~VideoOutput() = default;

    void VideoOutput::setAudioOutput(AudioOutput* audio) {
        m_audioOutput = audio;
        if (audio) {
            // Connect to audio output state changes
            connect(audio, &QObject::destroyed, this, [this]() {
                m_audioOutput = nullptr;
            });
        }
    }

    void VideoOutput::setMasterClock(std::function<qint64()> clock) {
        m_masterClock = clock;
    }

    qint64 VideoOutput::currentMasterClock() const {
        if (m_masterClock) {
            return m_masterClock();
        }
        // Fallback to system clock
        return QDateTime::currentMSecsSinceEpoch() * 1000;
    }

    void VideoOutput::setVideoLatency(qint64 microseconds) {
        m_videoLatency = microseconds;
    }

    void VideoOutput::setAudioLatency(qint64 microseconds) {
        m_audioLatency = microseconds;
    }

    bool VideoOutput::shouldDisplayFrame(const VideoPTS& videoPts, qint64 audioPts) {
        if (!m_audioSyncEnabled || audioPts < 0) {
            return true; // No sync, display immediately
        }

        // Calculate time difference between video and audio
        qint64 videoTime = videoPts.pts + m_videoLatency.load();
        qint64 audioTime = audioPts + m_audioLatency.load();
        qint64 diff = videoTime - audioTime; // Positive = video ahead

        // Thresholds in microseconds
        const qint64 EARLY_THRESHOLD = -40000;  // 40ms early - wait
        const qint64 LATE_THRESHOLD = 80000;    // 80ms late - drop

        if (diff < EARLY_THRESHOLD) {
            // Video is too early, should wait
            emit syncStatus("early");
            return false;
        } else if (diff > LATE_THRESHOLD) {
            // Video is too late, drop frame
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
        qint64 audioTime = audioPts + m_audioLatency.load();
        qint64 diff = videoTime - audioTime;

        // Return delay needed (negative = wait, positive = late)
        return -diff;
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
    , m_ownContext(context == nullptr) {}

    OpenGLVideoOutput::~OpenGLVideoOutput() {
        shutdown();
    }

    bool OpenGLVideoOutput::initialize(const QSize& resolution, VideoBackend backend) {
        Q_UNUSED(backend)

        if (m_initialized) return true;

        // Create OpenGL context if not provided
        if (!m_context) {
            m_context = new QOpenGLContext(this);
            QSurfaceFormat format;
            format.setRenderableType(QSurfaceFormat::OpenGL);
            format.setProfile(QSurfaceFormat::CoreProfile);
            format.setVersion(3, 3);
            format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
            m_context->setFormat(format);
            if (!m_context->create()) {
                emit error("Failed to create OpenGL context");
                return false;
            }
        }

        // Make context current
        if (!m_context->makeCurrent(m_context->surface())) {
            emit error("Failed to make OpenGL context current");
            return false;
        }

        initializeOpenGLFunctions();
        initializeGL();

        setResolution(resolution);
        m_initialized = true;
        return true;
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

        if (m_ownContext && m_context) {
            m_context->deleteLater();
            m_context = nullptr;
        }

        m_initialized = false;
    }

    void OpenGLVideoOutput::initializeGL() {
        // Create default shader program
        createShaderProgram();
        setupGeometry();

        // Create FBO for offscreen rendering
        m_fbo = std::make_unique<QOpenGLFramebufferObject>(m_resolution);
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
    m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                             m_customVertexShader.isEmpty() ? vertexShader : m_customVertexShader);
    m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                             m_customFragmentShader.isEmpty() ? fragmentShader : m_customFragmentShader);
    m_shaderProgram->link();
    }

    void OpenGLVideoOutput::setupGeometry() {
        // Full-screen quad
        float vertices[] = {
            // Position    // TexCoord
            -1.0f, -1.0f,  0.0f, 1.0f,
            1.0f, -1.0f,  1.0f, 1.0f,
            1.0f,  1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 0.0f
        };

        unsigned int indices[] = {0, 1, 2, 0, 2, 3};

        m_vao = std::make_unique<QOpenGLVertexArrayObject>();
        m_vao->create();
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
        // Get current audio PTS if available
        qint64 audioPts = -1;
        if (m_audioOutput) {
            // Query audio output for current presentation time
            // This would be implemented in AudioOutput
            audioPts = QDateTime::currentMSecsSinceEpoch() * 1000; // Placeholder
        }

        presentFrameWithSync(frame, audioPts);
    }

    void OpenGLVideoOutput::presentFrameWithSync(const VideoFrame& frame, qint64 audioPts) {
        if (!m_initialized) return;

        // Check if we should display this frame based on sync
        if (!shouldDisplayFrame(frame.pts, audioPts)) {
            return;
        }

        QMutexLocker locker(&m_frameMutex);
        m_currentFrame = frame;

        // Upload texture if needed
        uploadFrame(frame);

        // Render to FBO
        renderFrame();

        m_framesDisplayed++;
        emit framePresented(frame.pts);
    }

    void OpenGLVideoOutput::uploadFrame(const VideoFrame& frame) {
        if (!m_videoTexture ||
            m_videoTexture->width() != frame.image.width() ||
            m_videoTexture->height() != frame.image.height()) {

            m_videoTexture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
        m_videoTexture->setMinificationFilter(QOpenGLTexture::Linear);
        m_videoTexture->setMagnificationFilter(QOpenGLTexture::Linear);
        m_videoTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
        m_videoTexture->setSize(frame.image.width(), frame.image.height());
        m_videoTexture->setFormat(QOpenGLTexture::RGBA8_UNorm);
        m_videoTexture->allocateStorage();
            }

            if (!frame.image.isNull()) {
                QImage glImage = frame.image.convertToFormat(QImage::Format_RGBA8888);
                m_videoTexture->bind();
                m_videoTexture->setData(0, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, glImage.constBits());
            }
    }

    void OpenGLVideoOutput::renderFrame() {
        if (!m_fbo || !m_shaderProgram) return;

        m_fbo->bind();

        glClear(GL_COLOR_BUFFER_BIT);

        m_shaderProgram->bind();
        m_vao->bind();

        if (m_videoTexture) {
            m_videoTexture->bind(0);
            m_shaderProgram->setUniformValue("videoTexture", 0);
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

        if (m_fbo) {
            m_fbo.reset();
            m_fbo = std::make_unique<QOpenGLFramebufferObject>(m_resolution);
        }

        emit resolutionChanged(res);
    }

    void OpenGLVideoOutput::setVsyncEnabled(bool enabled) {
        m_vsyncEnabled = enabled;
        // Implementation depends on platform-specific swap control
        emit vsyncEnabledChanged(enabled);
    }

    void OpenGLVideoOutput::setEffectShader(const QString& vertexShader, const QString& fragmentShader) {
        m_customVertexShader = vertexShader;
        m_customFragmentShader = fragmentShader;
        m_shadersDirty = true;

        if (m_initialized) {
            createShaderProgram();
        }
    }

    void OpenGLVideoOutput::clearEffectShader() {
        m_customVertexShader.clear();
        m_customFragmentShader.clear();
        m_shadersDirty = true;

        if (m_initialized) {
            createShaderProgram();
        }
    }

    QImage OpenGLVideoOutput::captureFrame() {
        QMutexLocker locker(&m_frameMutex);
        if (!m_fbo) return QImage();
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
        QSize resolution;
        double frameRate = 30.0;
        bool initialized = false;
    };

    QtVideoOutput::QtVideoOutput(QObject* parent)
    : VideoOutput(parent)
    , m_impl(std::make_unique<Impl>()) {}

    QtVideoOutput::~QtVideoOutput() = default;

    bool QtVideoOutput::initialize(const QSize& resolution, VideoBackend backend) {
        Q_UNUSED(backend)
        m_impl->resolution = resolution;
        m_impl->videoSink = new QVideoSink(this);
        m_impl->initialized = true;
        return true;
    }

    void QtVideoOutput::shutdown() {
        delete m_impl->videoSink;
        m_impl->videoSink = nullptr;
        m_impl->initialized = false;
    }

    bool QtVideoOutput::isInitialized() const {
        return m_impl->initialized;
    }

    void QtVideoOutput::presentFrame(const VideoFrame& frame) {
        if (!m_impl->videoSink || frame.image.isNull()) return;

        QVideoFrame qtFrame(frame.image);
        m_impl->videoSink->setVideoFrame(qtFrame);
    }

    void QtVideoOutput::presentFrameWithSync(const VideoFrame& frame, qint64 audioPts) {
        // Qt Multimedia handles sync internally, but we can check
        if (shouldDisplayFrame(frame.pts, audioPts)) {
            presentFrame(frame);
        }
    }

    QSize QtVideoOutput::resolution() const {
        return m_impl->resolution;
    }

    void QtVideoOutput::setResolution(const QSize& res) {
        m_impl->resolution = res;
    }

    double QtVideoOutput::frameRate() const {
        return m_impl->frameRate;
    }

    void QtVideoOutput::setFrameRate(double fps) {
        m_impl->frameRate = fps;
    }

    bool QtVideoOutput::vsyncEnabled() const {
        return true; // Qt Multimedia manages this
    }

    void QtVideoOutput::setVsyncEnabled(bool enabled) {
        Q_UNUSED(enabled)
    }

    QImage QtVideoOutput::captureFrame() {
        // Not directly supported with QVideoSink
        return QImage();
    }

    void* QtVideoOutput::videoSink() const {
        return m_impl->videoSink;
    }

    // =============================================================================
    // VideoOutputFactory Implementation
    // =============================================================================

    std::unique_ptr<VideoOutput> VideoOutputFactory::create(VideoBackend backend, QOpenGLContext* context) {
        switch (backend) {
            case VideoBackend::OpenGL:
                return std::make_unique<OpenGLVideoOutput>(context);
            case VideoBackend::QtMultimedia:
                return std::make_unique<QtVideoOutput>();
            case VideoBackend::CPU:
            case VideoBackend::Null:
            default:
                return std::make_unique<QtVideoOutput>(); // Fallback
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
            case VideoBackend::OpenGL: return "OpenGL";
            case VideoBackend::QtMultimedia: return "Qt Multimedia";
            case VideoBackend::CPU: return "Software";
            case VideoBackend::Null: return "Null";
            default: return "Unknown";
        }
    }

} // namespace Aegis
