// converter.h - Media converter using all three pillars
#pragma once
#include <QObject>
#include <QThread>
#include <QProcess>
#include <QVariantMap>
#include <memory>

namespace Aegis {

    class AudioEngine;       // Pillar 1
    class EffectChain;       // Pillar 2
    class MpvBackend;        // Pillar 3

    enum class ConvertPreset {
        AudioMP3, AudioFLAC, AudioAAC, AudioOGG,
        VideoMP4, VideoWebM, VideoMKV,
        AudioOnly, Mobile, Web
    };

    struct ConvertJob {
        QString inputPath;
        QString outputPath;
        ConvertPreset preset;
        QVariantMap options; // bitrate, resolution, etc.
        std::shared_ptr<EffectChain> effects; // Pillar 2: Processing chain
    };

    /**
     * @brief Converter using MpvBackend for transcoding
     *
     * Architecture:
     * - Pillar 1 (audio): AudioEngine for analysis, loudness measurement
     * - Pillar 2 (audio_effects): EffectChain for processing during transcode
     * - Pillar 3 (mpv_backend): MpvBackend for decode/encode
     */
    class Converter : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool converting READ converting NOTIFY convertingChanged)
        Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
        Q_PROPERTY(QString status READ status NOTIFY statusChanged)

    public:
        /**
         * @brief Construct with pillar dependencies
         * @param engine AudioEngine for analysis (Pillar 1)
         * @param parent QObject parent
         */
        explicit Converter(AudioEngine* engine, QObject *parent = nullptr);
        ~Converter();

        // QML accessible API
        Q_INVOKABLE void convertFile(const QString &input, const QString &output,
                                     ConvertPreset preset,
                                     std::shared_ptr<EffectChain> effects = nullptr);
        Q_INVOKABLE void convertBatch(const QStringList &inputs, const QString &outputDir,
                                      ConvertPreset preset);
        Q_INVOKABLE void cancel();

        bool converting() const;
        int progress() const { return m_progress; }
        QString status() const { return m_status; }

    signals:
        void convertingChanged();
        void progressChanged();
        void statusChanged();
        void conversionFinished(bool success, QString message);

    private:
        void processJob(const ConvertJob& job);
        QString buildMpvArgs(const ConvertJob& job);

        // Dependencies
        AudioEngine* m_engine;  // Pillar 1 (borrowed)

        // State
        std::unique_ptr<MpvBackend> m_backend;  // Pillar 3 (created per job)
        int m_progress = 0;
        QString m_status;
        bool m_converting = false;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::ConvertJob)
Q_DECLARE_METATYPE(Aegis::ConvertPreset)
