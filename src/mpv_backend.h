// mpv_backend.h - Concrete implementation
#pragma once

#include "core.h"          // for AudioBackend, PlaybackState, TrackMetadata
#include "raii_wrappers.h"
#include <QTimer>

namespace Aegis {

    class MpvBackend : public AudioBackend {
        Q_OBJECT
    public:
        explicit MpvBackend(QObject* parent = nullptr);
        ~MpvBackend() override;

        void load(const QString& path) override;
        void play() override;
        void pause() override;
        void stop() override;
        void seek(double position) override;
        void setVolume(double volume) override;

        PlaybackState state() const override { return m_state; }
        double position() const override { return m_position; }
        double duration() const override { return m_duration; }
        TrackMetadata metadata() const override { return m_metadata; }
        bool hasVideo() const override { return m_hasVideo; }

        void setAudioCallback(std::function<void(const QByteArray&, int)> cb) override {
            m_audioCallback = std::move(cb);
        }

    private slots:
        void handleEvent();

    private:
        void initMpv();
        static void mpvWakeup(void* ctx);
        void updateMetadata();

        MpvHandlePtr m_mpv;
        QTimer m_posTimer;
        std::atomic<PlaybackState> m_state{PlaybackState::Stopped};
        std::atomic<double> m_position{0.0};
        std::atomic<double> m_duration{0.0};
        std::atomic<bool> m_hasVideo{false};
        TrackMetadata m_metadata;
        std::function<void(const QByteArray&, int)> m_audioCallback;
    };

    class MpvBackendFactory : public BackendFactory {
    public:
        QString name() const override { return QStringLiteral("mpv"); }
        std::unique_ptr<AudioBackend> create(QObject* parent) const override {
            return std::make_unique<MpvBackend>(parent);
        }
        bool isAvailable() const override;
    };

} // namespace Aegis
