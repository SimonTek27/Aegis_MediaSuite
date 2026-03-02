// audio_output.h - Audio output abstraction with PipeWire/Qt/ALSA backends
// Provides unified interface for all audio output operations
// Part of the audio pillar (Pillar 1)
//
// FIX: Guarded all backend-specific includes behind feature macros so that
// targets which do not have PipeWire dev headers or Qt::Multimedia installed
// can still include this header (and use the abstract AudioOutput interface).
//
// To enable a backend, define the corresponding macro BEFORE including this
// header, or add it to your CMake target:
//   target_compile_definitions(MyTarget PRIVATE AEGIS_ENABLE_PIPEWIRE)
//   target_compile_definitions(MyTarget PRIVATE AEGIS_ENABLE_QT_MULTIMEDIA)
//
// The main aegis_core library target should define these in its CMakeLists.txt
// after checking whether the corresponding libraries are found.

#pragma once

#include <QObject>
#include <QIODevice>
#include <QAudioFormat>
#include <functional>
#include <atomic>
#include <memory>
#include <thread>

// ── Qt Multimedia (QAudioSink) ───────────────────────────────────────────────
// Only include if Qt::Multimedia module is available.
// Tests that don't link Qt6::Multimedia must NOT define QT_MULTIMEDIA_LIB.
#ifdef QT_MULTIMEDIA_LIB
#  include <QAudioSink>
#  include <QAudioDevice>
#  include <QMediaDevices>
#endif

// ── PipeWire ─────────────────────────────────────────────────────────────────
// Only include if PipeWire dev headers are present.
// The build system detects this via pkg_check_modules(PipeWire libpipewire-0.3)
// and defines AEGIS_ENABLE_PIPEWIRE when the package is found.
#ifdef AEGIS_ENABLE_PIPEWIRE
#  include <pipewire/pipewire.h>
#  include <spa/param/audio/format-utils.h>
#endif

namespace Aegis {

    // Audio output backend types
    enum class OutputBackend {
        Auto,           ///< Auto-detect best available
        PipeWire,       ///< Native PipeWire (lowest latency)
        QtMultimedia,   ///< QAudioSink (Qt6, fallback)
        ALSA            ///< Direct ALSA (embedded/low-resource)
    };

    // Output configuration
    struct OutputConfig {
        int sampleRate        = 48000;
        int channels          = 2;
        int bufferSize        = 1024;       ///< Frames per callback
        int latencyTargetMs   = 10;         ///< Target latency in ms
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

        virtual bool initialize(const OutputConfig& config) = 0;
        virtual void shutdown() = 0;
        virtual bool isInitialized() const = 0;

        virtual void write(const float* interleavedData, int frames) = 0;
        virtual void setAudioCallback(std::function<void(float*, int)> callback) = 0;

        virtual int    sampleRate() const  = 0;
        virtual int    channels()   const  = 0;
        virtual double latencyMs()  const  = 0;
        virtual OutputBackend backendType() const = 0;

        virtual void   start()     = 0;
        virtual void   stop()      = 0;
        virtual bool   isPlaying() const = 0;

        virtual void   setVolume(double volume) = 0;
        virtual double volume() const = 0;

    signals:
        void state_changed(bool playing);
        void error(const QString& message);
        void stats_updated(quint64 bytesPlayed, quint64 framesPlayed);
        void underrun_detected();
    };

    // =============================================================================
    // PipeWire Backend (only compiled when PipeWire headers are present)
    // =============================================================================

#ifdef AEGIS_ENABLE_PIPEWIRE
    class PipeWireAudioOutput : public AudioOutput {
        Q_OBJECT
    public:
        explicit PipeWireAudioOutput(QObject* parent = nullptr);
        ~PipeWireAudioOutput() override;

        bool initialize(const OutputConfig& config) override;
        void shutdown() override;
        bool isInitialized() const override;

        void write(const float* interleavedData, int frames) override;
        void setAudioCallback(std::function<void(float*, int)> callback) override;

        int    sampleRate() const override;
        int    channels()   const override;
        double latencyMs()  const override;
        OutputBackend backendType() const override { return OutputBackend::PipeWire; }

        void   start()     override;
        void   stop()      override;
        bool   isPlaying() const override;

        void   setVolume(double volume) override;
        double volume()                 const override;

    private:
        struct Private;
        std::unique_ptr<Private> d;

        static void on_process(void* userdata);
        static void on_stream_state_changed(void* userdata,
            pw_stream_state old_state,
            pw_stream_state state,
            const char* error);
    };
#endif // AEGIS_ENABLE_PIPEWIRE

    // =============================================================================
    // Qt Multimedia Backend (only compiled when Qt6::Multimedia is linked)
    // =============================================================================

#ifdef QT_MULTIMEDIA_LIB
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

        int    sampleRate() const override;
        int    channels()   const override;
        double latencyMs()  const override;
        OutputBackend backendType() const override { return OutputBackend::QtMultimedia; }

        void   start()     override;
        void   stop()      override;
        bool   isPlaying() const override;

        void   setVolume(double volume) override;
        double volume()                 const override;

    private:
        struct Private;
        std::unique_ptr<Private> d;

        void on_state_changed(QAudio::State state);
        int  estimate_latency() const;
    };
#endif // QT_MULTIMEDIA_LIB

    // =============================================================================
    // ALSA Backend (always available on Linux, no extra headers needed here)
    // =============================================================================

    // AlsaOutput is declared in audio_output_alsa.h to avoid pulling in ALSA
    // headers into every translation unit that includes audio_output.h.

    // =============================================================================
    // Factory
    // =============================================================================

    class AudioOutputFactory {
    public:
        static std::unique_ptr<AudioOutput> create(
            OutputBackend preferred = OutputBackend::Auto);
        static bool    isBackendAvailable(OutputBackend backend);
        static QString backendName(OutputBackend backend);
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::OutputBackend)
Q_DECLARE_METATYPE(Aegis::OutputConfig)
