// mpv_backend.h - Concrete implementation of IAudioBackend using libmpv
#pragma once

#include "core.h"          // for IAudioBackend, PlaybackState, TrackMetadata
#include "raii_wrappers.h"
#include <QTimer>
#include <QObject>

namespace Aegis {

    class MpvBackend : public QObject, public IAudioBackend {
        Q_OBJECT
    public:
        explicit MpvBackend(QObject* parent = nullptr);
        ~MpvBackend() override;

        // ── IAudioBackend interface ──────────────────────────────────────────
        // Signatures must exactly match IAudioBackend (core.h).
        // play/pause/stop/setVolume return void; errors are logged internally.
        // seek takes qint64 milliseconds, matching IAudioBackend::seek(qint64).

        bool    open(const QUrl& url) override;
        void    close() override;

        void    play()                     override;
        void    pause()                    override;
        void    stop()                     override;

        void    seek(qint64 positionMs)    override;
        qint64  position() const           override;
        qint64  duration() const           override;

        void    setVolume(double volume)   override;
        double  volume()  const            override;

        PlaybackState state()    const override { return m_state; }
        TrackMetadata metadata() const override { return m_metadata; }

        bool    isSeekable() const         override;

        // ── MPV-specific extras ─────────────────────────────────────────────
        bool hasVideo() const { return m_hasVideo; }
        void setAudioCallback(std::function<void(const QByteArray&, int)> cb) {
            m_audioCallback = std::move(cb);
        }

        // Convenience wrapper for C++ callers that have a local file path
        bool load(const QString& path) { return open(QUrl::fromLocalFile(path)); }

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
        void error(const QString& message);

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

        // Helper: run a Result<void> and emit error signal on failure
        void runOrLog(const char* context, Result<void> result);

        MpvHandle m_mpv;
        QTimer m_posTimer;
        std::atomic<PlaybackState> m_state{PlaybackState::Stopped};
        std::atomic<double> m_position{0.0};   // seconds (mpv native)
        std::atomic<double> m_duration{0.0};   // seconds (mpv native)
        std::atomic<bool>   m_hasVideo{false};
        TrackMetadata m_metadata;
        std::function<void(const QByteArray&, int)> m_audioCallback;
        double m_volume{1.0};
    };

} // namespace Aegis
