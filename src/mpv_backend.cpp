// mpv_backend.cpp
#include "mpv_backend.h"
#include <QDebug>

namespace Aegis {

    MpvBackend::MpvBackend(QObject* parent) : AudioBackend(parent) {
        initMpv();

        m_posTimer.setInterval(100);
        connect(&m_posTimer, &QTimer::timeout, [this]() {
            if (m_state == PlaybackState::Playing && m_mpv) {
                double pos = 0;
                mpv_get_property(m_mpv.get(), "time-pos", MPV_FORMAT_DOUBLE, &pos);
                if (pos != m_position) {
                    m_position = pos;
                    emit positionChanged(pos);
                }
            }
        });
    }

    MpvBackend::~MpvBackend() = default;

    void MpvBackend::initMpv() {
        m_mpv.reset(mpv_create());
        if (!m_mpv) throw std::runtime_error("Failed to create mpv instance");

        mpv_set_option_string(m_mpv.get(), "vo", "libmpv");
        mpv_set_option_string(m_mpv.get(), "hwdec", "auto");
        mpv_set_option_string(m_mpv.get(), "audio-display", "no");

        // Enable audio data export for visualization
        mpv_set_option_string(m_mpv.get(), "audio-buffer", "0.1"); // Small buffer for low latency

        mpv_observe_property(m_mpv.get(), 0, "duration", MPV_FORMAT_DOUBLE);
        mpv_observe_property(m_mpv.get(), 0, "core-idle", MPV_FORMAT_FLAG);
        mpv_set_wakeup_callback(m_mpv.get(), mpvWakeup, this);

        if (mpv_initialize(m_mpv.get()) < 0) {
            throw std::runtime_error("Failed to initialize mpv");
        }

        m_posTimer.start();
    }

    void MpvBackend::mpvWakeup(void* ctx) {
        auto *obj = static_cast<MpvBackend *>(ctx);
        QMetaObject::invokeMethod(obj, "handleEvent", Qt::QueuedConnection);
    }

    void MpvBackend::handleEvent() {
        while (m_mpv) {
            mpv_event* event = mpv_wait_event(m_mpv.get(), 0);
            if (event->event_id == MPV_EVENT_NONE) break;

            switch (event->event_id) {
                case MPV_EVENT_PROPERTY_CHANGE: {
                    auto* prop = static_cast<mpv_event_property*>(event->data);
                    if (strcmp(prop->name, "duration") == 0 && prop->data) {
                        m_duration = *static_cast<double*>(prop->data);
                        emit durationChanged(m_duration);
                    }
                    break;
                }
                case MPV_EVENT_END_FILE:
                    emit finished();
                    m_state = PlaybackState::Stopped;
                    emit stateChanged(m_state);
                    break;
                case MPV_EVENT_PLAYBACK_RESTART:
                    m_state = PlaybackState::Playing;
                    emit stateChanged(m_state);
                    break;
                case MPV_EVENT_PAUSE:
                    m_state = PlaybackState::Paused;
                    emit stateChanged(m_state);
                    break;
                default:
                    break;
            }
        }
    }

    void MpvBackend::load(const QString& path) {
        if (!m_mpv) return;
        const char* cmd[] = {"loadfile", path.toUtf8().constData(), nullptr};
        mpv_command(m_mpv.get(), cmd);
        updateMetadata();
    }

    void MpvBackend::updateMetadata() {
        if (!m_mpv) return;

        char* title = nullptr;
        mpv_get_property(m_mpv.get(), "media-title", MPV_FORMAT_STRING, &title);
        if (title) {
            m_metadata.title = QString::fromUtf8(title);
            mpv_free(title);
        }

        char* format = nullptr;
        mpv_get_property(m_mpv.get(), "video-format", MPV_FORMAT_STRING, &format);
        m_hasVideo = (format != nullptr);
        if (format) mpv_free(format);

        emit metadataChanged(m_metadata);
    }

    void MpvBackend::play() {
        if (m_mpv) {
            mpv_set_property_string(m_mpv.get(), "pause", "no");
            m_state = PlaybackState::Playing;
        }
    }

    void MpvBackend::pause() {
        if (m_mpv) {
            mpv_set_property_string(m_mpv.get(), "pause", "yes");
            m_state = PlaybackState::Paused;
        }
    }

    void MpvBackend::stop() {
        if (m_mpv) {
            const char* cmd[] = {"stop", nullptr};
            mpv_command(m_mpv.get(), cmd);
            m_state = PlaybackState::Stopped;
            m_position = 0;
        }
    }

    void MpvBackend::seek(double position) {
        if (m_mpv) {
            mpv_set_property(m_mpv.get(), "time-pos", MPV_FORMAT_DOUBLE, &position);
        }
    }

    void MpvBackend::setVolume(double volume) {
        if (m_mpv) {
            double norm = std::clamp(volume / 100.0, 0.0, 1.0);
            mpv_set_property(m_mpv.get(), "volume", MPV_FORMAT_DOUBLE, &norm);
        }
    }

    bool MpvBackendFactory::isAvailable() const {
        // Check if libmpv is available by attempting to create a handle
        mpv_handle* test = mpv_create();
        if (test) {
            mpv_destroy(test);
            return true;
        }
        return false;
    }

} // namespace Aegis
