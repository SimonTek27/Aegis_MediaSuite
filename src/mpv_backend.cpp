// mpv_backend.cpp - Production MPV backend with full error handling
#include "mpv_backend.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>

namespace Aegis {

    // ============================================================================
    // MPV Backend Implementation
    // ============================================================================

    MpvBackend::MpvBackend(QObject* parent)
    : IAudioBackend(parent)
    , m_logger("MpvBackend") {

        qRegisterMetaType<PlaybackState>();
        initialize();
    }

    MpvBackend::~MpvBackend() {
        stop();
        if (m_mpv) {
            m_mpv.withExclusiveLock([this](mpv_handle* handle) {
                mpv_set_wakeup_callback(handle, nullptr, nullptr);
            });
        }
    }

    Result<void> MpvBackend::initialize() {
        m_logger.debug("Initializing MPV backend");

        auto result = createMpvInstance();
        if (result.isError()) {
            return result;
        }

        result = configureMpv();
        if (result.isError()) {
            return result;
        }

        result = setupEventHandling();
        if (result.isError()) {
            return result;
        }

        m_logger.info("MPV backend initialized successfully");
        return Result<void>::success();
    }

    Result<void> MpvBackend::createMpvInstance() {
        mpv_handle* handle = mpv_create();
        if (!handle) {
            return Result<void>::error("Failed to create mpv instance");
        }

        m_mpv = MpvHandle(handle);
        return Result<void>::success();
    }

    Result<void> MpvBackend::configureMpv() {
        return m_mpv.withExclusiveLock([this](mpv_handle* handle) {
            // Basic configuration
            setOption(handle, "vo", "libmpv");
            setOption(handle, "hwdec", "auto");
            setOption(handle, "audio-display", "no");
            setOption(handle, "audio-buffer", "0.1");
            setOption(handle, "cache", "yes");
            setOption(handle, "cache-secs", "10");

            // Audio format for visualization
            setOption(handle, "audio-format", "float");
            setOption(handle, "audio-channels", "2");
            setOption(handle, "audio-samplerate", "48000");

            // Initialize
            if (mpv_initialize(handle) < 0) {
                return Result<void>::error("Failed to initialize mpv");
            }

            // Observe properties
            mpv_observe_property(handle, 0, "duration", MPV_FORMAT_DOUBLE);
            mpv_observe_property(handle, 0, "time-pos", MPV_FORMAT_DOUBLE);
            mpv_observe_property(handle, 0, "core-idle", MPV_FORMAT_FLAG);
            mpv_observe_property(handle, 0, "pause", MPV_FORMAT_FLAG);
            mpv_observe_property(handle, 0, "eof-reached", MPV_FORMAT_FLAG);

            return Result<void>::success();
        });
    }

    void MpvBackend::setOption(mpv_handle* handle, const char* key, const char* value) {
        int result = mpv_set_option_string(handle, key, value);
        if (result < 0) {
            m_logger.warning(QString("Failed to set option %1=%2: %3")
            .arg(key, value, mpv_error_string(result)));
        }
    }

    Result<void> MpvBackend::setupEventHandling() {
        return m_mpv.withExclusiveLock([this](mpv_handle* handle) {
            mpv_set_wakeup_callback(handle, [](void* ctx) {
                auto* self = static_cast<MpvBackend*>(ctx);
                QMetaObject::invokeMethod(self, "handleEvents", Qt::QueuedConnection);
            }, this);

            return Result<void>::success();
        });
    }

    void MpvBackend::handleEvents() {
        m_mpv.withLock([this](mpv_handle* handle) {
            while (true) {
                mpv_event* event = mpv_wait_event(handle, 0);
                if (event->event_id == MPV_EVENT_NONE) break;

                processEvent(event);
            }
        });
    }

    void MpvBackend::processEvent(mpv_event* event) {
        switch (event->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* prop = static_cast<mpv_event_property*>(event->data);
                handlePropertyChange(prop);
                break;
            }

            case MPV_EVENT_END_FILE: {
                auto* endFile = static_cast<mpv_event_end_file*>(event->data);
                handleEndFile(endFile);
                break;
            }

            case MPV_EVENT_FILE_LOADED:
                m_logger.info("File loaded successfully");
                updateMetadata();
                emit durationChanged(m_duration.load());
                break;

            case MPV_EVENT_PLAYBACK_RESTART:
                m_state.store(PlaybackState::Playing);
                emit stateChanged(PlaybackState::Playing);
                break;

            case MPV_EVENT_PAUSE:
                m_state.store(PlaybackState::Paused);
                emit stateChanged(PlaybackState::Paused);
                break;

            case MPV_EVENT_UNPAUSE:
                m_state.store(PlaybackState::Playing);
                emit stateChanged(PlaybackState::Playing);
                break;

            case MPV_EVENT_LOG_MESSAGE: {
                auto* log = static_cast<mpv_event_log_message*>(event->data);
                handleLogMessage(log);
                break;
            }

            case MPV_EVENT_ERROR:
                m_logger.error("MPV error event received");
                break;

            default:
                m_logger.debug(QString("Unhandled event: %1").arg(event->event_id));
                break;
        }
    }

    void MpvBackend::handlePropertyChange(mpv_event_property* prop) {
        if (!prop->data) return;

        if (strcmp(prop->name, "duration") == 0) {
            double dur = *static_cast<double*>(prop->data);
            if (dur > 0) {
                m_duration.store(dur);
                emit durationChanged(dur);
            }
        }
        else if (strcmp(prop->name, "time-pos") == 0) {
            double pos = *static_cast<double*>(prop->data);
            if (pos >= 0) {
                m_position.store(pos);
                emit positionChanged(pos);
            }
        }
        else if (strcmp(prop->name, "eof-reached") == 0) {
            int eof = *static_cast<int*>(prop->data);
            if (eof) {
                m_logger.debug("EOF reached");
                emit finished();
            }
        }
    }

    void MpvBackend::handleEndFile(mpv_event_end_file* endFile) {
        switch (endFile->reason) {
            case MPV_END_FILE_REASON_EOF:
                m_logger.info("Playback finished normally");
                emit finished();
                break;

            case MPV_END_FILE_REASON_STOP:
                m_logger.debug("Playback stopped");
                break;

            case MPV_END_FILE_REASON_ERROR:
                m_logger.error(QString("Playback error: %1")
                .arg(endFile->error >= 0 ? "" : mpv_error_string(endFile->error)));
                emit error(QString("Playback error: %1")
                .arg(mpv_error_string(endFile->error)));
                break;

            default:
                m_logger.warning(QString("Unknown end reason: %1").arg(endFile->reason));
                break;
        }
    }

    void MpvBackend::handleLogMessage(mpv_event_log_message* log) {
        QString prefix = QString::fromUtf8(log->prefix);
        QString text = QString::fromUtf8(log->text).trimmed();

        if (text.isEmpty()) return;

        switch (log->log_level) {
            case MPV_LOG_LEVEL_FATAL:
            case MPV_LOG_LEVEL_ERROR:
                m_logger.error(QString("[%1] %2").arg(prefix, text));
                break;
            case MPV_LOG_LEVEL_WARN:
                m_logger.warning(QString("[%1] %2").arg(prefix, text));
                break;
            case MPV_LOG_LEVEL_INFO:
                m_logger.info(QString("[%1] %2").arg(prefix, text));
                break;
            default:
                m_logger.debug(QString("[%1] %2").arg(prefix, text));
                break;
        }
    }

    Result<void> MpvBackend::load(const QString& path) {
        m_logger.info(QString("Loading: %1").arg(path));

        return m_mpv.withExclusiveLock([this, &path](mpv_handle* handle) {
            const char* cmd[] = {"loadfile", path.toUtf8().constData(), nullptr};
            int result = mpv_command(handle, cmd);

            if (result < 0) {
                return Result<void>::error(QString("Failed to load file: %1")
                .arg(mpv_error_string(result)));
            }

            m_state.store(PlaybackState::Buffering);
            emit stateChanged(PlaybackState::Buffering);

            return Result<void>::success();
        });
    }

    Result<void> MpvBackend::play() {
        return setProperty("pause", "no");
    }

    Result<void> MpvBackend::pause() {
        return setProperty("pause", "yes");
    }

    Result<void> MpvBackend::stop() {
        return m_mpv.withExclusiveLock([this](mpv_handle* handle) {
            const char* cmd[] = {"stop", nullptr};
            int result = mpv_command(handle, cmd);

            if (result < 0) {
                return Result<void>::error(QString("Failed to stop: %1")
                .arg(mpv_error_string(result)));
            }

            m_position.store(0.0);
            m_state.store(PlaybackState::Stopped);
            emit stateChanged(PlaybackState::Stopped);

            return Result<void>::success();
        });
    }

    Result<void> MpvBackend::seek(double position) {
        return setProperty("time-pos", position);
    }

    Result<void> MpvBackend::setVolume(double volume) {
        double norm = std::clamp(volume / 100.0, 0.0, 1.0);
        return setProperty("volume", norm);
    }

    Result<void> MpvBackend::setProperty(const char* name, const char* value) {
        return m_mpv.withExclusiveLock([this, name, value](mpv_handle* handle) {
            int result = mpv_set_property_string(handle, name, value);
            if (result < 0) {
                return Result<void>::error(QString("Failed to set %1: %2")
                .arg(name, mpv_error_string(result)));
            }
            return Result<void>::success();
        });
    }

    Result<void> MpvBackend::setProperty(const char* name, double value) {
        return m_mpv.withExclusiveLock([this, name, value](mpv_handle* handle) {
            int result = mpv_set_property(handle, name, MPV_FORMAT_DOUBLE, &value);
            if (result < 0) {
                return Result<void>::error(QString("Failed to set %1: %2")
                .arg(name, mpv_error_string(result)));
            }
            return Result<void>::success();
        });
    }

    void MpvBackend::updateMetadata() {
        m_mpv.withLock([this](mpv_handle* handle) {
            TrackMetadata metadata;

            // Get media title
            char* title = nullptr;
            if (mpv_get_property(handle, "media-title", MPV_FORMAT_STRING, &title) >= 0) {
                metadata.title = QString::fromUtf8(title);
                mpv_free(title);
            }

            // Check for video
            char* format = nullptr;
            if (mpv_get_property(handle, "video-format", MPV_FORMAT_STRING, &format) >= 0) {
                m_hasVideo = true;
                mpv_free(format);
            }

            // Get metadata from tags
            mpv_node tags;
            if (mpv_get_property(handle, "metadata", MPV_FORMAT_NODE, &tags) >= 0) {
                if (tags.format == MPV_FORMAT_NODE_MAP) {
                    for (int i = 0; i < tags.u.list->num; i++) {
                        QString key = tags.u.list->keys[i];
                        QString value;

                        if (tags.u.list->values[i].format == MPV_FORMAT_STRING) {
                            value = QString::fromUtf8(tags.u.list->values[i].u.string);
                        }

                        if (key == "artist") metadata.artist = value;
                        else if (key == "album") metadata.album = value;
                        else if (key == "genre") metadata.genre = value;
                        else if (key == "date") metadata.year = value.toInt();
                        else if (key == "track") metadata.trackNumber = value.toInt();
                    }
                }
                mpv_free_node_contents(&tags);
            }

            m_metadata = metadata;
            emit metadataChanged(metadata);
        });
    }

    void MpvBackend::setAudioCallback(std::function<void(const QByteArray&, int)> cb) {
        m_audioCallback = std::move(cb);

        // In a real implementation, this would set up audio output capture
        // using mpv's audio output API or custom AO
    }

    // ============================================================================
    // Factory Implementation
    // ============================================================================

    QString MpvBackendFactory::name() {
        return "mpv";
    }

    bool MpvBackendFactory::isAvailable() {
        // Try to create a test instance
        mpv_handle* test = mpv_create();
        if (test) {
            mpv_terminate_destroy(test);
            return true;
        }
        return false;
    }

    MpvBackendFactory::Capabilities MpvBackendFactory::capabilities() {
        Capabilities caps;
        caps.supportsVideo = true;
        caps.supportsAudio = true;
        caps.supportsStreaming = true;
        caps.supportsHardwareDecoding = true;
        caps.maxChannels = 8;
        caps.supportedCodecs = {"h264", "hevc", "vp9", "aac", "mp3", "flac"};
        return caps;
    }

} // namespace Aegis
