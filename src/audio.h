// audio.h - Core Audio Engine and Processing System

#pragma once

#include <QObject>
#include <QVector>
#include <QVariantList>
#include <QMutex>
#include <QReadWriteLock>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <rubberband/RubberBandStretcher.h>
#include <ebur128.h>
#include <fftw3.h>
#include <libopenmpt/libopenmpt.hpp>

// Forward declaration for AudioOutput
class AudioOutput;
enum class TransportState;
struct fftwf_plan_s;
typedef float fftwf_complex[2];

namespace openmpt {
    class module;
}

namespace Aegis {

    class AudioEngine;

    struct AudioFormat {
        int sampleRate = 44100;
        int channels = 2;
        int bitsPerSample = 16;
        bool isFloat = false;
    };

    // ============================================================================
    // Audio Data Structures
    // ============================================================================

    struct Selection {
        qint64 start = 0;
        qint64 end = 0;

        bool isEmpty() const { return start == end; }
        bool isValid() const { return start >= 0 && end >= start; }
        qint64 length() const { return end - start; }

        void clear() { start = end = 0; }
        void set(qint64 s, qint64 e) { start = s; end = e; }

        bool contains(qint64 position) const {
            return position >= start && position < end;
        }

        Selection intersection(const Selection& other) const {
            qint64 s = std::max(start, other.start);
            qint64 e = std::min(end, other.end);
            if (s < e) return {s, e};
            return {0, 0};
        }
    };

    struct BiQuad {
        double b0{1.0}, b1{0.0}, b2{0.0};
        double a1{0.0}, a2{0.0};

        double x1{0.0}, x2{0.0};
        double y1{0.0}, y2{0.0};

        void reset() { x1 = x2 = y1 = y2 = 0.0; }

        double process(double in) {
            double out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = out;
            return out;
        }

        void configurePeak(double freq, double sampleRate, double gainDb, double q = 1.41) {
            if (freq <= 0 || freq >= sampleRate / 2) return;

            double A = std::pow(10.0, gainDb / 40.0);
            double w0 = 2.0 * M_PI * freq / sampleRate;
            double cosw0 = std::cos(w0);
            double sinw0 = std::sin(w0);
            double alpha = sinw0 / (2.0 * q);

            double a0 = 1.0 + alpha / A;
            b0 = (1.0 + alpha * A) / a0;
            b1 = (-2.0 * cosw0) / a0;
            b2 = (1.0 - alpha * A) / a0;
            a1 = (-2.0 * cosw0) / a0;
            a2 = (1.0 - alpha / A) / a0;
        }
    };

    // ============================================================================
    // Equalizer Processor
    // ============================================================================

    class EqualizerProcessor {
    public:
        static constexpr int Bands = 10;

        static constexpr std::array<double, Bands> DefaultFrequencies = {
            31.25, 62.5, 125.0, 250.0, 500.0,
            1000.0, 2000.0, 4000.0, 8000.0, 16000.0
        };

        EqualizerProcessor();

        void process(float* data, int samples, int sampleRate);
        void setGain(int band, double gainDb);
        void setGains(const QVector<double>& gains);
        void setEnabled(bool enabled) { m_enabled.store(enabled); }
        bool enabled() const { return m_enabled.load(); }

    private:
        std::array<BiQuad, Bands> m_leftFilters;
        std::array<BiQuad, Bands> m_rightFilters;
        std::array<std::atomic<double>, Bands> m_gains;
        std::atomic<bool> m_enabled{false};
        std::atomic<int> m_sampleRate{48000};
        std::mutex m_coefficientMutex;

        void updateCoefficients();
    };

    // ============================================================================
    // Karaoke Processor - With PIMPL forward declaration
    // ============================================================================

    class KaraokeProcessor {
    public:
        // Forward declaration of private implementation
        class Private;

        KaraokeProcessor();
        ~KaraokeProcessor();

        void setKeyChange(int semitones);
        void setVocalSuppressionEnabled(bool enabled);
        void setMusicVolume(double volume);
        void setVocalVolume(double volume);
        void setEchoLevel(double level);
        void setEnabled(bool enabled);
        bool enabled() const;
        bool vocalSuppressionEnabled() const;

        void process(float* interleavedData, int frames, int sampleRate);

    private:
        std::unique_ptr<Private> d;
    };

    // ============================================================================
    // Crossfader
    // ============================================================================

    class Crossfader {
    public:
        enum class Curve {
            Linear,
            EqualPower,
            Exponential
        };

        enum class State {
            Idle,
            FadingOut,
            FadingIn,
            Complete
        };

        Crossfader();

        void start(double durationMs, Curve curve = Curve::EqualPower);
        void stop();
        double currentGain() const;
        State state() const { return m_state.load(); }
        double progress() const;

    private:
        std::atomic<State> m_state{State::Idle};
        std::atomic<double> m_progress{0.0};
        double m_duration{3000.0};
        Curve m_curve{Curve::EqualPower};
        qint64 m_startTime{0};
    };

    // ============================================================================
    // Module Tracker Playback
    // ============================================================================

    class ModTrackerPlayback : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
        Q_PROPERTY(double position READ position NOTIFY positionChanged)
        Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
        Q_PROPERTY(QString title READ title NOTIFY metadataChanged)
        Q_PROPERTY(QString artist READ artist NOTIFY metadataChanged)

    public:
        explicit ModTrackerPlayback(AudioEngine* engine = nullptr);
        ~ModTrackerPlayback();

        bool load(const QString& path);
        bool isTrackerFile(const QString& path) const;
        QStringList supportedExtensions() const;
        void unload();
        bool isLoaded() const { return m_module != nullptr; }

        void play();
        void pause();
        void stop();
        void seek(double seconds);

        bool isPlaying() const { return m_playing.load(); }
        double position() const { return m_position.load(); }
        double duration() const { return m_duration.load(); }
        QString title() const;
        QString artist() const;

        int getNumPatterns() const;
        int getNumChannels() const;
        int getCurrentPattern() const;
        int getCurrentRow() const;

        void setVolume(double volume) { m_volume.store(volume); }
        double volume() const { return m_volume.load(); }

    signals:
        void playingChanged();
        void positionChanged();
        void durationChanged();
        void metadataChanged();
        void finished();
        void error(const QString& message);

    private slots:
        void generateAudioChunk();

    private:
        std::unique_ptr<openmpt::module> m_module;
        QTimer m_generationTimer;
        std::atomic<bool> m_playing{false};
        std::atomic<double> m_position{0.0};
        std::atomic<double> m_duration{0.0};
        std::atomic<double> m_volume{1.0};
        std::vector<float> m_renderBuffer;
        static constexpr int SAMPLE_RATE = 48000;
        static constexpr int CHANNELS = 2;
        static constexpr int CHUNK_FRAMES = 512;
        AudioEngine* m_engine = nullptr;
    };

    // ============================================================================
    // Audio Source Types
    // ============================================================================

    enum class AudioSource {
        ExternalPCM,
        TrackerModule
    };

    // ============================================================================
    // Main Audio Engine
    // ============================================================================

    class AudioEngine : public QObject {
        Q_OBJECT
        Q_PROPERTY(QVariantList spectrumData READ spectrumData NOTIFY spectrumUpdated)
        Q_PROPERTY(double momentaryLoudness READ momentaryLoudness NOTIFY loudnessUpdated)
        Q_PROPERTY(double shortTermLoudness READ shortTermLoudness NOTIFY loudnessUpdated)
        Q_PROPERTY(bool processingEnabled READ processingEnabled WRITE setProcessingEnabled NOTIFY processingChanged)
        Q_PROPERTY(bool karaokeEnabled READ karaokeEnabled WRITE setKaraokeEnabled NOTIFY karaokeChanged)
        Q_PROPERTY(bool trackerMode READ isTrackerMode NOTIFY trackerModeChanged)

    public:
        explicit AudioEngine(QObject *parent = nullptr);
        ~AudioEngine() override;

        QVariantList spectrumData() const;

        double momentaryLoudness() const { return m_momentary.load(); }
        double shortTermLoudness() const { return m_shortTerm.load(); }
        double integratedLoudness();

        void setProcessingEnabled(bool enabled);
        bool processingEnabled() const { return m_processingEnabled.load(); }

        void setKaraokeEnabled(bool enabled);
        bool karaokeEnabled() const { return m_karaokeEnabled.load(); }

        KaraokeProcessor* karaokeProcessor() { return &m_karaoke; }
        EqualizerProcessor* equalizer() { return &m_equalizer; }
        Crossfader* crossfader() { return &m_crossfader; }

        void processPCM(const QByteArray &data, int sampleRate, int channels = 2);
        void processBuffer(float* interleavedData, int frames, int sampleRate, int channels);

        bool loadTrackerModule(const QString& path);
        void unloadTracker();
        bool isTrackerMode() const { return m_currentSource == AudioSource::TrackerModule; }
        ModTrackerPlayback* tracker() { return m_trackerPlayback.get(); }
        bool isTrackerFile(const QString& path) const;

        void playTracker();
        void pauseTracker();
        void stopTracker();
        void seekTracker(double seconds);

        QString lastError() const { return m_lastError; }
        void clearError() { m_lastError.clear(); }

        void setAudioOutput(std::unique_ptr<AudioOutput> output);
        AudioOutput* audioOutput() const { return m_output.get(); }

        void processOutput(float* buffer, int frames);

        QVector<float> calculateSpectrum(const float* data, int samples, int channels);

    signals:
        void spectrumUpdated();
        void loudnessUpdated();
        void processingFinished();
        void processingChanged(bool enabled);
        void karaokeChanged(bool enabled);
        void trackerModeChanged(bool trackerMode);
        void errorOccurred(const QString& message);
        void positionChanged(double position);
        void stateChanged(TransportState state);
        void finished();
        void error(const QString &message);

    private:
        std::unique_ptr<AudioOutput> m_output;
        std::function<void(float*, int)> m_outputCallback;

        void calculateFFT(const std::vector<float> &mono);
        void calculateLoudness(const float* data, size_t frames);
        std::vector<float> toMono(const float* data, size_t frames, int channels) const;

        int m_fftSize = 4096;
        int m_bands = 32;
        std::unique_ptr<float, void(*)(void*)> m_fftIn;
        std::unique_ptr<fftwf_complex[], void(*)(void*)> m_fftOut;
        fftwf_plan_s* m_fftPlan = nullptr;
        QVector<double> m_spectrum;
        std::vector<double> m_fftWindow;

        struct EburStateDeleter {
            void operator()(ebur128_state* p) const;
        };
        std::unique_ptr<ebur128_state, EburStateDeleter> m_eburState;
        std::atomic<double> m_momentary{-70.0};
        std::atomic<double> m_shortTerm{-70.0};
        double m_integrated{-70.0};

        EqualizerProcessor m_equalizer;
        Crossfader m_crossfader;
        KaraokeProcessor m_karaoke;

        std::atomic<bool> m_processingEnabled{false};
        std::atomic<bool> m_karaokeEnabled{false};

        mutable QMutex m_mutex;

        std::unique_ptr<ModTrackerPlayback> m_trackerPlayback;
        AudioSource m_currentSource{AudioSource::ExternalPCM};

        QString m_lastError;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::AudioSource)
Q_DECLARE_METATYPE(Aegis::Selection)
