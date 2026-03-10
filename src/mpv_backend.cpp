// mpv_backend.cpp - Production MPV backend with full error handling

#include "mpv_backend.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <clocale>

namespace Aegis {

// ============================================================================
// Helpers
// ============================================================================

// Log a Result<void> on failure and optionally emit error signal.
// Used by void-returning IAudioBackend overrides.
void MpvBackend::runOrLog(const char* context, Result<void> result) {
    if (result.isError()) {
        QString msg = QString("[MpvBackend::%1] %2").arg(context, result.error());
        qWarning().noquote() << msg;
        emit error(msg);
    }
}

// ============================================================================
// Construction / Destruction
// ============================================================================

MpvBackend::MpvBackend(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<PlaybackState>();
    initialize();
}

MpvBackend::~MpvBackend() {
    // Use internal stop helper to avoid virtual dispatch during destruction
    if (m_mpv) {
        m_mpv.withExclusiveLock([this](mpv_handle* handle) {
            mpv_set_wakeup_callback(handle, nullptr, nullptr);
            const char* cmd[] = {"stop", nullptr};
            mpv_command(handle, cmd);
            return Result<void>::success();
        });
    }
}

// ============================================================================
// IAudioBackend – open / close
// ============================================================================

bool MpvBackend::open(const QUrl& url) {
    qInfo() << "MpvBackend::open:" << url.toString();

    bool ok = false;
    m_mpv.withExclusiveLock([this, &url, &ok](mpv_handle* handle) {
        QByteArray path = url.isLocalFile()
                          ? url.toLocalFile().toUtf8()
                          : url.toString().toUtf8();
        const char* cmd[] = {"loadfile", path.constData(), nullptr};
        int result = mpv_command(handle, cmd);
        if (result < 0) {
            qWarning() << "MpvBackend::open: failed:" << mpv_error_string(result);
            ok = false;
        } else {
            m_state.store(PlaybackState::Buffering);
            emit stateChanged(PlaybackState::Buffering);
            ok = true;
        }
        return Result<void>::success();
    });
    return ok;
}

void MpvBackend::close() {
    m_mpv.withExclusiveLock([this](mpv_handle* handle) {
        const char* cmd[] = {"stop", nullptr};
        mpv_command(handle, cmd);
        m_state.store(PlaybackState::Stopped);
        m_position.store(0.0);
        m_duration.store(0.0);
        emit stateChanged(PlaybackState::Stopped);
        return Result<void>::success();
    });
}

// ============================================================================
// IAudioBackend – transport (void-returning, errors logged internally)
// ============================================================================

void MpvBackend::play() {
    runOrLog("play", setMpvProperty("pause", "no"));
}

void MpvBackend::pause() {
    runOrLog("pause", setMpvProperty("pause", "yes"));
}

void MpvBackend::stop() {
    runOrLog("stop", m_mpv.withExclusiveLock([this](mpv_handle* handle) {
        const char* cmd[] = {"stop", nullptr};
        int result = mpv_command(handle, cmd);
        if (result < 0) {
            return Result<void>::error(QString("mpv_command stop: %1")
                                       .arg(mpv_error_string(result)));
        }
        m_position.store(0.0);
        m_state.store(PlaybackState::Stopped);
        emit stateChanged(PlaybackState::Stopped);
        return Result<void>::success();
    }));
}

// IAudioBackend::seek takes milliseconds; mpv "time-pos" is in seconds.
void MpvBackend::seek(qint64 positionMs) {
    double posSec = static_cast<double>(positionMs) / 1000.0;
    runOrLog("seek", setMpvProperty("time-pos", posSec));
}

qint64 MpvBackend::position() const {
    // m_position stores seconds; convert to milliseconds for the interface
    return static_cast<qint64>(m_position.load() * 1000.0);
}

qint64 MpvBackend::duration() const {
    return static_cast<qint64>(m_duration.load() * 1000.0);
}

void MpvBackend::setVolume(double volume) {
    m_volume = std::clamp(volume, 0.0, 1.0);
    // mpv "volume" property uses 0–100 scale
    runOrLog("setVolume", setMpvProperty("volume", m_volume * 100.0));
}

double MpvBackend::volume() const {
    return m_volume;
}

bool MpvBackend::isSeekable() const {
    bool seekable = false;
    m_mpv.withLock([&seekable](mpv_handle* handle) {
        int val = 0;
        if (mpv_get_property(handle, "seekable", MPV_FORMAT_FLAG, &val) >= 0) {
            seekable = (val != 0);
        }
        return Result<void>::success();
    });
    return seekable;
}

// ============================================================================
// Initialisation helpers
// ============================================================================

Result<void> MpvBackend::initialize() {
    qDebug() << "Initializing MPV backend";

    auto result = createMpvInstance();
    if (result.isError()) return result;

    result = configureMpv();
    if (result.isError()) return result;

    result = setupEventHandling();
    if (result.isError()) return result;

    qInfo() << "MPV backend initialized successfully";
    return Result<void>::success();
}

Result<void> MpvBackend::createMpvInstance() {
    mpv_handle* handle = mpv_create();
    if (!handle)
        return Result<void>::error("Failed to create mpv instance");
    m_mpv = MpvHandle(handle);
    return Result<void>::success();
}

Result<void> MpvBackend::configureMpv() {
    return m_mpv.withExclusiveLock([this](mpv_handle* handle) {
        setOption(handle, "vo",             "libmpv");
        setOption(handle, "hwdec",          "auto");
        setOption(handle, "audio-display",  "no");
        setOption(handle, "audio-buffer",   "0.1");
        setOption(handle, "cache",          "yes");
        setOption(handle, "cache-secs",     "10");
        setOption(handle, "audio-format",   "float");
        setOption(handle, "audio-channels", "2");
        setOption(handle, "audio-samplerate", "48000");

        if (mpv_initialize(handle) < 0)
            return Result<void>::error("Failed to initialize mpv");

        mpv_observe_property(handle, 0, "duration",     MPV_FORMAT_DOUBLE);
        mpv_observe_property(handle, 0, "time-pos",     MPV_FORMAT_DOUBLE);
        mpv_observe_property(handle, 0, "core-idle",    MPV_FORMAT_FLAG);
        mpv_observe_property(handle, 0, "pause",        MPV_FORMAT_FLAG);
        mpv_observe_property(handle, 0, "eof-reached",  MPV_FORMAT_FLAG);

        return Result<void>::success();
    });
}

void MpvBackend::setOption(mpv_handle* handle, const char* key, const char* value) {
    int result = mpv_set_option_string(handle, key, value);
    if (result < 0) {
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

// ============================================================================
// Event loop
// ============================================================================

void MpvBackend::handleEvent() {
    handleEvents();
}

void MpvBackend::handleEvents() {
    m_mpv.withLock([this](mpv_handle* handle) {
        while (true) {
            mpv_event* event = mpv_wait_event(handle, 0);
            if (event->event_id == MPV_EVENT_NONE) break;
            processEvent(event);
        }
        return Result<void>::success();
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
            qInfo() << "File loaded successfully";
            updateMetadata();
            emit durationChanged(m_duration.load());
            break;
        case MPV_EVENT_START_FILE:      // 6
            m_state.store(PlaybackState::Buffering);
            emit stateChanged(PlaybackState::Buffering);
            break;
        case MPV_EVENT_PLAYBACK_RESTART: // 21
            m_state.store(PlaybackState::Playing);
            emit stateChanged(PlaybackState::Playing);
            break;
        case MPV_EVENT_LOG_MESSAGE: {
            auto* log = static_cast<mpv_event_log_message*>(event->data);
            handleLogMessage(log);
            break;
        }
        case MPV_EVENT_SEEK:            // 20
        case MPV_EVENT_AUDIO_RECONFIG:  // 18
        case MPV_EVENT_VIDEO_RECONFIG:  // 17
        case MPV_EVENT_CLIENT_MESSAGE:  // 16
        case 11: // MPV_EVENT_IDLE
        case 24: // MPV_EVENT_QUEUE_OVERFLOW
            break;
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
    } else if (strcmp(prop->name, "time-pos") == 0) {
        double pos = *static_cast<double*>(prop->data);
        if (pos >= 0) {
            m_position.store(pos);
            emit positionChanged(pos);
        }
    } else if (strcmp(prop->name, "eof-reached") == 0) {
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
    QString text   = QString::fromUtf8(log->text).trimmed();
    if (text.isEmpty()) return;

    switch (log->log_level) {
        case MPV_LOG_LEVEL_FATAL:
        case MPV_LOG_LEVEL_ERROR:
            qCritical() << QString("[%1] %2").arg(prefix, text);
            break;
        case MPV_LOG_LEVEL_WARN:
            qWarning()  << QString("[%1] %2").arg(prefix, text);
            break;
        case MPV_LOG_LEVEL_INFO:
            qInfo()     << QString("[%1] %2").arg(prefix, text);
            break;
        default:
            qDebug()    << QString("[%1] %2").arg(prefix, text);
            break;
    }
}

// ============================================================================
// Internal property helpers
// ============================================================================

Result<void> MpvBackend::setMpvProperty(const char* name, const char* value) {
    return m_mpv.withExclusiveLock([name, value](mpv_handle* handle) {
        int result = mpv_set_property_string(handle, name, value);
        if (result < 0) {
            return Result<void>::error(
                QString("Failed to set %1: %2").arg(name, mpv_error_string(result)));
        }
        return Result<void>::success();
    });
}

Result<void> MpvBackend::setMpvProperty(const char* name, double value) {
    return m_mpv.withExclusiveLock([name, value](mpv_handle* handle) {
        double mutableValue = value;
        int result = mpv_set_property(handle, name, MPV_FORMAT_DOUBLE, &mutableValue);
        if (result < 0) {
            return Result<void>::error(
                QString("Failed to set %1: %2").arg(name, mpv_error_string(result)));
        }
        return Result<void>::success();
    });
}

// ============================================================================
// Metadata
// ============================================================================

void MpvBackend::updateMetadata() {
    m_mpv.withLock([this](mpv_handle* handle) {
        TrackMetadata metadata;

        char* title = nullptr;
        if (mpv_get_property(handle, "media-title", MPV_FORMAT_STRING, &title) >= 0) {
            metadata.title = QString::fromUtf8(title);
            mpv_free(title);
        }

        // Detect video stream
        char* fmt = nullptr;
        if (mpv_get_property(handle, "video-format", MPV_FORMAT_STRING, &fmt) >= 0) {
            m_hasVideo = true;
            mpv_free(fmt);
        }

        mpv_node tags;
        if (mpv_get_property(handle, "metadata", MPV_FORMAT_NODE, &tags) >= 0) {
            if (tags.format == MPV_FORMAT_NODE_MAP) {
                for (int i = 0; i < tags.u.list->num; ++i) {
                    if (tags.u.list->values[i].format != MPV_FORMAT_STRING) continue;
                    QString key   = QString::fromUtf8(tags.u.list->keys[i]).toLower();
                    QString value = QString::fromUtf8(tags.u.list->values[i].u.string);

                    if      (key == "artist") metadata.artist = value;
                    else if (key == "album")  metadata.album  = value;
                    else if (key == "genre")  metadata.genre  = value;
                    else if (key == "date")   metadata.year   = value.toInt();
                }
            }
            mpv_free_node_contents(&tags);
        }

        m_metadata = metadata;
        emit metadataChanged(metadata);
        return Result<void>::success();
    });
}

// ============================================================================
// Static factory helpers
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
    caps.supportsVideo            = true;
    caps.supportsAudio            = true;
    caps.supportsStreaming        = true;
    caps.supportsHardwareDecoding = true;
    caps.maxChannels              = 8;
    caps.supportedCodecs          = {"h264", "hevc", "vp9", "aac", "mp3", "flac"};
    return caps;
}

} // namespace Aegis
