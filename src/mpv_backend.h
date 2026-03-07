// mpv_backend.h - Concrete implementation
#pragma once

#include "core.h"          // for AudioBackend, PlaybackState, TrackMetadata
#include "raii_wrappers.h"
#include <QTimer>

namespace Aegis {

    class MpvBackend : public IAudioBackend {
        Q_OBJECT
    public:
        explicit MpvBackend(QObject* parent = nullptr);
        ~MpvBackend() override;

        Result<void> load(const QString& path) override;
        Result<void> play() override;
        Result<void> pause() override;
        Result<void> stop() override;
        Result<void> seek(double position) override;
        Result<void> setVolume(double volume) override;

        PlaybackState state() const override { return m_state; }
        double position() const override { return m_position; }
        double duration() const override { return m_duration; }
        TrackMetadata metadata() const override { return m_metadata; }
        bool hasVideo() const override { return m_hasVideo; }

        void setAudioCallback(std::function<void(const QByteArray&, int)> cb) override {
            m_audioCallback = std::move(cb);
        }

    // BackendType concept interface
    static QString name() { return QStringLiteral("mpv"); }
    static bool isAvailable();

    struct Capabilities {
        bool supportsVideo{true};
        bool supportsAudio{true};
        bool supportsStreaming{true};
        bool supportsHardwareDecoding{true};
        int maxChannels{8};
        QStringList supportedCodecs;
    };
    static Capabilities capabilities();

signals:
        void positionChanged(double position);
        void durationChanged(double duration);
        void finished();
        void stateChanged(Aegis::PlaybackState state);
        void metadataChanged(const Aegis::TrackMetadata& metadata);

    private slots:
        void handleEvent();

    private:
        Result<void> initialize();
        Result<void> createMpvInstance();
        Result<void> configureMpv();
        Result<void> setupEventHandling();
        void setOption(mpv_handle* handle, const char* key, const char* value);
        void handleEvents();
        void processEvent(mpv_event* event);
        void handlePropertyChange(mpv_event_property* prop);
        void handleEndFile(mpv_event_end_file* endFile);
        void handleLogMessage(mpv_event_log_message* log);
        Result<void> setMpvProperty(const char* name, const char* value);
        Result<void> setMpvProperty(const char* name, double value);
        void initMpv();
        static void mpvWakeup(void* ctx);
        void updateMetadata();

        MpvHandle m_mpv;
        QTimer m_posTimer;
        std::atomic<PlaybackState> m_state{PlaybackState::Stopped};
        std::atomic<double> m_position{0.0};
        std::atomic<double> m_duration{0.0};
        std::atomic<bool> m_hasVideo{false};
        TrackMetadata m_metadata;
        std::function<void(const QByteArray&, int)> m_audioCallback;
    };



} // namespace Aegis
