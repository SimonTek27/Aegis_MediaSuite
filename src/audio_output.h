// audio_output.h - Audio output abstraction with PipeWire/Qt/ALSA backends
// Provides unified interface for all audio output operations
// Part of the audio pillar (Pillar 1)

#pragma once

#include <QObject>
#include <QIODevice>
#include <QAudioSink>
#include <QAudioFormat>
#include <functional>
#include <atomic>
#include <memory>
#include <thread>

// Forward declarations for PipeWire
struct pw_main_loop;
struct pw_context;
struct pw_stream;
struct spa_hook;
struct pw_buffer;

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

namespace Aegis {

    // Audio output backend types
    enum class OutputBackend {
        Auto,       // Auto-detect best available
        PipeWire,   // Native PipeWire (lowest latency, recommended)
        QtMultimedia, // QAudioSink (Qt6, fallback)
        ALSA        // Direct ALSA (embedded/low-resource systems)
    };

    // Output configuration
    struct OutputConfig {
        int sampleRate = 48000;
        int channels = 2;
        int bufferSize = 1024;  // Frames per callback
        int latencyTargetMs = 10;  // Target latency in ms
        bool realtimePriority = true;
        OutputBackend preferredBackend = OutputBackend::Auto;
    };

    // =============================================================================
    // Abstract Base Class
    // =============================================================================

    class AudioOutput : public QObject {
        Q_OBJECT
    public:
        explicit AudioOutput(QObject* parent = nullptr) : QObject(parent) {}
        virtual ~AudioOutput() = default;

        // Core interface
        virtual bool initialize(const OutputConfig& config) = 0;
        virtual void shutdown() = 0;
        virtual bool isInitialized() const = 0;

        // Audio data input
        virtual void write(const float* interleavedData, int frames) = 0;
        virtual void setAudioCallback(std::function<void(float*, int)> callback) = 0;

        // Properties
        virtual int sampleRate() const = 0;
        virtual int channels() const = 0;
        virtual double latencyMs() const = 0;
        virtual OutputBackend backendType() const = 0;

        // Control
        virtual void start() = 0;
        virtual void stop() = 0;
        virtual bool isPlaying() const = 0;

        // Volume (0.0 - 1.0)
        virtual void setVolume(double volume) = 0;
        virtual double volume() const = 0;

    signals:
        void stateChanged(bool playing);
        void underrunDetected();
        void error(const QString& message);
        void latencyChanged(double latencyMs);

    protected:
        OutputConfig m_config;
        std::atomic<double> m_volume{1.0};
        std::atomic<bool> m_playing{false};
    };

    // =============================================================================
    // PipeWire Backend (Primary - Lowest Latency)
    // =============================================================================

    class PipeWireOutput : public AudioOutput {
        Q_OBJECT
    public:
        explicit PipeWireOutput(QObject* parent = nullptr);
        ~PipeWireOutput() override;

        bool initialize(const OutputConfig& config) override;
        void shutdown() override;
        bool isInitialized() const override { return m_stream != nullptr; }

        void write(const float* interleavedData, int frames) override;
        void setAudioCallback(std::function<void(float*, int)> callback) override;

        int sampleRate() const override;
        int channels() const override;
        double latencyMs() const override;
        OutputBackend backendType() const override { return OutputBackend::PipeWire; }

        void start() override;
        void stop() override;
        bool isPlaying() const override;

        void setVolume(double volume) override;
        double volume() const override;

        // PipeWire-specific
        QString nodeName() const;
        void setNodeName(const QString& name);

    private:
        static void onProcess(void* userdata);
        static void onParamChanged(void* userdata, uint32_t id, const struct spa_pod* param);
        static void onStateChanged(void* userdata, enum pw_stream_state old,
                                   enum pw_stream_state state, const char* error);

        void processBuffer(struct pw_buffer* buffer);
        void updateLatency();

        // PipeWire objects
        struct pw_main_loop* m_loop = nullptr;
        struct pw_context* m_context = nullptr;
        struct pw_stream* m_stream = nullptr;
        struct spa_hook* m_streamListener = nullptr;

        // Threading
        std::thread m_loopThread;
        std::atomic<bool> m_running{false};

        // Audio callback (pull mode)
        std::function<void(float*, int)> m_audioCallback;
        std::atomic<double> m_currentLatency{0.0};
        QString m_nodeName = "Aegis Audio";

        // Format
        struct spa_audio_info_raw m_format;
    };

    // =============================================================================
    // Qt Multimedia Backend (Fallback - Cross-platform)
    // =============================================================================

    class QtAudioOutput : public AudioOutput {
        Q_OBJECT
    public:
        explicit QtAudioOutput(QObject* parent = nullptr);
        ~QtAudioOutput() override;

        bool initialize(const OutputConfig& config) override;
        void shutdown() override;
        bool isInitialized() const override;

        void write(const float* interleavedData, int frames) override;
        void setAudioCallback(std::function<void(float*, int)> callback) override;

        int sampleRate() const override;
        int channels() const override;
        double latencyMs() const override;
        OutputBackend backendType() const override { return OutputBackend::QtMultimedia; }

        void start() override;
        void stop() override;
        bool isPlaying() const override;

        void setVolume(double volume) override;
        double volume() const override;

    private:
        QAudioSink* m_sink = nullptr;
        QIODevice* m_device = nullptr;
        QByteArray m_buffer;

        std::function<void(float*, int)> m_callback;
        QAudioFormat m_format;
        int m_bufferSize = 0;
    };

    // =============================================================================
    // Factory
    // =============================================================================

    class AudioOutputFactory {
    public:
        static std::unique_ptr<AudioOutput> create(OutputBackend preferred = OutputBackend::Auto);
        static bool isBackendAvailable(OutputBackend backend);
        static QString backendName(OutputBackend backend);
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::OutputBackend)
Q_DECLARE_METATYPE(Aegis::OutputConfig)
