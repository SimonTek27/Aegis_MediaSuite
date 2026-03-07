// mpv_backend.cpp - Production MPV backend with full error handling

#include "mpv_backend.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <clocale>

namespace Aegis {

    // ============================================================================
    // MPV Backend Implementation
    // ============================================================================

    MpvBackend::MpvBackend(QObject* parent)
    : IAudioBackend(parent) {

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
        qDebug() << "Initializing MPV backend";

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

        qInfo() << "MPV backend initialized successfully";
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
            // FIX: Added missing closing parenthesis
            qWarning() << QString("Failed to set option %1=%2: %3")
            .arg(key, value, mpv_error_string(result));
        }
    }

    Result<void> MpvBackend::setupEventHandling() {
        return m_mpv.withExclusiveLock([this](mpv_handle* handle) {
            mpv_set_wakeup_callback(handle, [](void* ctx) {
                auto* self = static_cast<MpvBackend*>(ctx);
                QMetaObject::invokeMethod(self, "handleEvent", Qt::QueuedConnection);
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
        // Canonical libmpv event values (client.h, stable since libmpv 1.x):
        //  1=SHUTDOWN  2=LOG_MESSAGE  5=COMMAND_REPLY  6=START_FILE
        //  7=END_FILE  8=FILE_LOADED  11=IDLE  16=CLIENT_MESSAGE
        //  17=VIDEO_RECONFIG  18=AUDIO_RECONFIG  20=SEEK
        //  21=PLAYBACK_RESTART  22=PROPERTY_CHANGE  24=QUEUE_OVERFLOW  25=HOOK
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
                qInfo() << "File loaded successfully";
                updateMetadata();
                emit durationChanged(m_duration.load());
                break;

            case MPV_EVENT_START_FILE:  // 6 — new file about to load
                m_state.store(PlaybackState::Buffering);
                emit stateChanged(PlaybackState::Buffering);
                break;

            case MPV_EVENT_PLAYBACK_RESTART:  // 21
                m_state.store(PlaybackState::Playing);
                emit stateChanged(PlaybackState::Playing);
                break;

            case MPV_EVENT_LOG_MESSAGE: {
                auto* log = static_cast<mpv_event_log_message*>(event->data);
                handleLogMessage(log);
                break;
            }

            case MPV_EVENT_SEEK:         // 20 — seek in progress, no state change needed
            case MPV_EVENT_AUDIO_RECONFIG: // 18
            case MPV_EVENT_VIDEO_RECONFIG: // 17
            case MPV_EVENT_CLIENT_MESSAGE: // 16
            case 11: // MPV_EVENT_IDLE — player idle, no file queued
            case 24: // MPV_EVENT_QUEUE_OVERFLOW
                break; // benign, no action needed

            case 1: // MPV_EVENT_SHUTDOWN
                m_state.store(PlaybackState::Stopped);
                emit stateChanged(PlaybackState::Stopped);
                break;

            default:
                qDebug() << QString("Unhandled MPV event: %1").arg(event->event_id);
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
                qDebug() << "EOF reached";
                emit finished();
            }
        }
    }

    void MpvBackend::handleEndFile(mpv_event_end_file* endFile) {
        switch (endFile->reason) {
            case MPV_END_FILE_REASON_EOF:
                qInfo() << "Playback finished normally";
                emit finished();
                break;

            case MPV_END_FILE_REASON_STOP:
                qDebug() << "Playback stopped";
                break;

            case MPV_END_FILE_REASON_ERROR:
                // FIX: Added missing closing parenthesis
                qCritical() << QString("Playback error: %1")
                .arg(endFile->error >= 0 ? "" : mpv_error_string(endFile->error));
                emit error(QString("Playback error: %1")
                .arg(mpv_error_string(endFile->error)));
                break;

            default:
                qWarning() << QString("Unknown end reason: %1").arg(endFile->reason);
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
                qCritical() << QString("[%1] %2").arg(prefix, text);
                break;
            case MPV_LOG_LEVEL_WARN:
                qWarning() << QString("[%1] %2").arg(prefix, text);
                break;
            case MPV_LOG_LEVEL_INFO:
                qInfo() << QString("[%1] %2").arg(prefix, text);
                break;
            default:
                qDebug() << QString("[%1] %2").arg(prefix, text);
                break;
        }
    }

    Result<void> MpvBackend::load(const QString& path) {
        qInfo() << QString("Loading: %1").arg(path);

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
        return setMpvProperty("pause", "no");
    }

    Result<void> MpvBackend::pause() {
        return setMpvProperty("pause", "yes");
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
        return setMpvProperty("time-pos", position);
    }

    Result<void> MpvBackend::setVolume(double volume) {
        // mpv "volume" property uses 0–100 scale (not 0–1)
        double clamped = std::clamp(volume, 0.0, 100.0);
        return setMpvProperty("volume", clamped);
    }

    Result<void> MpvBackend::setMpvProperty(const char* name, const char* value) {
        return m_mpv.withExclusiveLock([this, name, value](mpv_handle* handle) {
            int result = mpv_set_property_string(handle, name, value);
            if (result < 0) {
                return Result<void>::error(QString("Failed to set %1: %2")
                .arg(name, mpv_error_string(result)));
            }
            return Result<void>::success();
        });
    }

    Result<void> MpvBackend::setMpvProperty(const char* name, double value) {
        return m_mpv.withExclusiveLock([this, name, value](mpv_handle* handle) {
            // FIX: Cast away const-ness for mpv_set_property which expects void*
            double mutableValue = value;
            int result = mpv_set_property(handle, name, MPV_FORMAT_DOUBLE, &mutableValue);
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
                        else if (key == "genre") metadata.comment = value;  // genre stored in comment
                        else if (key == "date") metadata.year = value.toInt();
                        // trackNumber not in TrackMetadata, skip
                    }
                }
                mpv_free_node_contents(&tags);
            }

            m_metadata = metadata;
            emit metadataChanged(metadata);
        });
    }

    // setAudioCallback defined inline in header

    // ============================================================================
    // Factory Implementation
    // ============================================================================

    bool MpvBackend::isAvailable() {
        std::setlocale(LC_NUMERIC, "C");
        mpv_handle* test = mpv_create();
        if (test) {
            mpv_terminate_destroy(test);
            return true;
        }
        return false;
    }

    MpvBackend::Capabilities MpvBackend::capabilities() {
        Capabilities caps;
        caps.supportsVideo = true;
        caps.supportsAudio = true;
        caps.supportsStreaming = true;
        caps.supportsHardwareDecoding = true;
        caps.maxChannels = 8;
        caps.supportedCodecs = {"h264", "hevc", "vp9", "aac", "mp3", "flac"};
        return caps;
    }

    void MpvBackend::handleEvent() {
        handleEvents();
    }

} // namespace Aegis
