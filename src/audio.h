// audio.h - Core Audio Engine and Processing System
// Provides real-time audio processing, effects, analysis, and module playback
// Uses EnhancedAudioBuffer for consistent audio data management
// Supports both real-time streaming and offline processing

#pragma once

#include <QObject>
#include <QVector>
#include <QVariantList>
#include <QMutex>
#include <QReadWriteLock>
#include <QTimer>
#include <QElapsedTimer>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <rubberband/RubberBandStretcher.h>
#include <ebur128.h>
#include <fftw3.h>

class AudioOutput;
struct fftwf_plan_s;
typedef float fftwf_complex[2];

namespace openmpt {
    class module;
}

namespace Aegis {

    // ============================================================================
    // Audio Data Structures
    // ============================================================================

    /**
     * @brief Time range selection for audio editing
     *
     * Represents a selected region of audio for offline processing.
     * All values are in sample frames relative to the audio buffer.
     */
    struct Selection {
        qint64 start = 0;      ///< Start frame (inclusive)
        qint64 end = 0;        ///< End frame (exclusive)

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

    /**
     * @brief Biquad filter implementation for equalization
     *
     * Second-order IIR filter with configurable coefficients.
     * Supports peaking, low-pass, high-pass, and shelf filters.
     */
    struct BiQuad {
        // Filter coefficients (Direct Form II)
        double b0{1.0}, b1{0.0}, b2{0.0};  ///< Numerator coefficients
        double a1{0.0}, a2{0.0};          ///< Denominator coefficients

        // State history for Direct Form II implementation
        double x1{0.0}, x2{0.0};  ///< Previous input samples
        double y1{0.0}, y2{0.0};  ///< Previous output samples

        /**
         * @brief Reset filter state (clear history)
         */
        void reset() { x1 = x2 = y1 = y2 = 0.0; }

        /**
         * @brief Process a single sample through the filter
         * @param in Input sample
         * @return Filtered output sample
         */
        double process(double in) {
            double out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = out;
            return out;
        }

        /**
         * @brief Configure as peaking EQ filter
         * @param freq Center frequency in Hz
         * @param sampleRate Audio sample rate
         * @param gainDb Gain in decibels (+/-)
         * @param q Quality factor (bandwidth)
         */
        void configurePeak(double freq, double sampleRate, double gainDb, double q = 1.41) {
            if (freq <= 0 || freq >= sampleRate / 2) return;

            double A = std::pow(10.0, gainDb / 40.0);      // Voltage gain
            double w0 = 2.0 * M_PI * freq / sampleRate;    // Angular frequency
            double cosw0 = std::cos(w0);
            double sinw0 = std::sin(w0);
            double alpha = sinw0 / (2.0 * q);             // Bandwidth parameter

            // Calculate coefficients for peaking EQ
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

    /**
     * @brief 10-band parametric equalizer processor
     *
     * Provides standard ISO frequency bands with independent gain control.
     * Uses BiQuad filters for each band, processed in parallel-sum architecture.
     */
    class EqualizerProcessor {
    public:
        static constexpr int Bands = 10;  ///< Number of frequency bands

        /// ISO standard 1/3-octave center frequencies (31.25Hz to 16kHz)
        static constexpr std::array<double, Bands> DefaultFrequencies = {
            31.25, 62.5, 125.0, 250.0, 500.0,
            1000.0, 2000.0, 4000.0, 8000.0, 16000.0
        };

        EqualizerProcessor();

        /**
         * @brief Process interleaved stereo audio through equalizer
         * @param data Pointer to interleaved stereo samples (L,R,L,R...)
         * @param samples Number of sample frames (stereo pairs)
         * @param sampleRate Audio sample rate in Hz
         */
        void process(float* data, int samples, int sampleRate);

        /**
         * @brief Set gain for specific frequency band
         * @param band Band index (0-9)
         * @param gainDb Gain in decibels (+/- 24dB typically)
         */
        void setGain(int band, double gainDb);

        /**
         * @brief Set gains for all bands simultaneously
         * @param gains Vector of 10 gain values in dB
         */
        void setGains(const QVector<double>& gains);

        /**
         * @brief Enable or disable equalizer processing
         * @param enabled True to enable, false to bypass
         */
        void setEnabled(bool enabled) { m_enabled.store(enabled); }

        /**
         * @brief Check if equalizer is enabled
         * @return True if equalizer is active
         */
        bool enabled() const { return m_enabled.load(); }

    private:
        /// Separate filter banks for left and right channels
        std::array<BiQuad, Bands> m_leftFilters;
        std::array<BiQuad, Bands> m_rightFilters;

        /// Current gain settings for each band (thread-safe updates)
        std::array<std::atomic<double>, Bands> m_gains;

        /// Processing enabled flag
        std::atomic<bool> m_enabled{false};

        /// Current sample rate for coefficient calculation
        std::atomic<int> m_sampleRate{48000};

        /// Mutex for filter coefficient updates (prevents audio glitches)
        std::mutex m_coefficientMutex;

        /**
         * @brief Update filter coefficients based on current gains and sample rate
         */
        void updateCoefficients();
    };

    // ============================================================================
    // Karaoke Processor
    // ============================================================================

    /**
     * @brief Real-time karaoke effects processor
     *
     * Provides pitch shifting, vocal suppression, echo effects, and volume control
     * for karaoke applications. Uses Rubber Band Library for high-quality pitch shifting.
     */
    class KaraokeProcessor {
    public:
        KaraokeProcessor();
        ~KaraokeProcessor();

        /**
         * @brief Set pitch shift in semitones
         * @param semitones Number of semitones to shift (-12 to +12)
         */
        void setKeyChange(int semitones);

        /**
         * @brief Enable or disable vocal suppression (center channel removal)
         * @param enabled True to remove vocals, false to keep them
         */
        void setVocalSuppressionEnabled(bool enabled);

        /**
         * @brief Set music volume (non-vocal elements)
         * @param volume Linear volume scale (0.0 to 2.0)
         */
        void setMusicVolume(double volume);

        /**
         * @brief Set vocal volume (center channel)
         * @param volume Linear volume scale (0.0 to 2.0)
         */
        void setVocalVolume(double volume);

        /**
         * @brief Set echo/reverb level
         * @param level Echo mix level (0.0 = dry, 1.0 = wet)
         */
        void setEchoLevel(double level);

        /**
         * @brief Enable or disable all karaoke processing
         * @param enabled True to enable processing
         */
        void setEnabled(bool enabled);

        /**
         * @brief Check if karaoke processor is enabled
         * @return True if processor is active
         */
        bool enabled() const { return m_enabled.load(); }

        /**
         * @brief Process interleaved stereo audio
         * @param interleavedData Pointer to interleaved stereo samples
         * @param frames Number of stereo frames
         * @param sampleRate Audio sample rate in Hz
         */
        void process(float* interleavedData, int frames, int sampleRate);

    private:
        /**
         * @brief Update pitch shifter ratio based on current semitone shift
         * @param sampleRate Current audio sample rate
         */
        void updatePitchRatio(int sampleRate);

        /**
         * @brief Apply vocal suppression using center channel extraction
         * @param data Interleaved stereo audio buffer
         * @param frames Number of stereo frames
         */
        void applyVocalSuppression(float* data, int frames);

        /**
         * @brief Apply echo/reverb effect using feedback delay line
         * @param data Interleaved stereo audio buffer
         * @param frames Number of stereo frames
         */
        void applyEcho(float* data, int frames);

        // Processing parameters (all atomic for thread-safe updates)
        std::atomic<int> m_keySemitones{0};            ///< Pitch shift in semitones
        std::atomic<bool> m_vocalSuppression{true};    ///< Vocal removal enabled
        std::atomic<double> m_musicVolume{1.0};        ///< Instrument volume
        std::atomic<double> m_vocalVolume{0.0};        ///< Vocal volume
        std::atomic<double> m_echoLevel{0.0};          ///< Echo mix level
        std::atomic<bool> m_enabled{false};            ///< Processor enabled

        // Pitch shifting (protected by read-write lock for real-time safety)
        mutable std::shared_mutex m_stretcherMutex;
        std::unique_ptr<RubberBand::RubberBandStretcher> m_stretcher;

        // Echo buffer (feedback delay line)
        std::vector<float> m_echoBuffer;
        std::atomic<size_t> m_echoPos{0};
        static constexpr size_t MaxEchoDelay = 48000;  ///< Maximum 1 second at 48kHz

        std::atomic<int> m_currentSampleRate{48000};   ///< Current sample rate
    };

    // ============================================================================
    // Crossfader
    // ============================================================================

    /**
     * @brief Smooth crossfade between audio sources
     *
     * Supports multiple fade curves for different applications.
     * Handles both manual progress updates and timed automatic fades.
     */
    class Crossfader {
    public:
        /// Fade curve types for different perceptual characteristics
        enum class Curve {
            Linear,      ///< Linear gain changes (simple)
            EqualPower,  ///< Constant power crossfade (best for audio)
            Exponential  ///< Exponential fade (musical)
        };

        /// Current state of the crossfade operation
        enum class State {
            Idle,        ///< No active fade
            FadingOut,   ///< Fading out current source
            FadingIn,    ///< Fading in new source
            Complete     ///< Fade completed successfully
        };

        Crossfader();

        /**
         * @brief Start timed crossfade operation
         * @param durationMs Fade duration in milliseconds
         * @param curve Fade curve type
         */
        void start(double durationMs, Curve curve = Curve::EqualPower);

        /**
         * @brief Stop crossfade immediately
         */
        void stop();

        /**
         * @brief Get current gain for fading out source
         * @return Gain multiplier (1.0 = full, 0.0 = silent)
         */
        double currentGain() const;

        /**
         * @brief Get current fade state
         * @return Current crossfade state
         */
        State state() const { return m_state.load(); }

        /**
         * @brief Get fade progress percentage
         * @return Progress from 0.0 to 1.0
         */
        double progress() const;

        /**
         * @brief Apply crossfade between two audio buffers
         * @param fadeOut Buffer containing outgoing audio
         * @param fadeIn Buffer containing incoming audio
         * @param position Crossfade position in samples
         */
        void applyCrossfade(float* fadeOut, float* fadeIn, qint64 position, qint64 length);

    private:
        /// Calculate gain based on curve type and progress
        double calculateGain(double progress, bool isFadeIn) const;

        std::atomic<State> m_state{State::Idle};  ///< Current fade state
        std::atomic<double> m_progress{0.0};      ///< Current progress (0-1)
        double m_duration{3000.0};               ///< Total fade duration (ms)
        Curve m_curve{Curve::EqualPower};        ///< Selected fade curve
        qint64 m_startTime{0};                   ///< Fade start timestamp (ms)
    };

    // ============================================================================
    // Module Tracker Playback
    // ============================================================================

    /**
     * @brief MOD/S3M/XM/IT module file playback engine
     *
     * Uses libopenmpt for high-quality module playback with real-time rendering.
     * Provides pattern/row position tracking and module metadata extraction.
     */
    class ModTrackerPlayback : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
        Q_PROPERTY(double position READ position NOTIFY positionChanged)
        Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
        Q_PROPERTY(QString title READ title NOTIFY metadataChanged)
        Q_PROPERTY(QString artist READ artist NOTIFY metadataChanged)

    public:
        explicit ModTrackerPlayback(QObject *parent = nullptr);
        ~ModTrackerPlayback();

        /**
         * @brief Load module file from disk
         * @param path Path to module file
         * @return True if loaded successfully
         */
        bool load(const QString& path);

        /**
         * @brief Check if file is a supported module format
         * @param path Path to check
         * @return True if supported module format
         */
        bool isTrackerFile(const QString& path) const;

        /**
         * @brief Get list of supported file extensions
         * @return List of extensions (e.g., ".mod", ".xm", ".it")
         */
        QStringList supportedExtensions() const;

        /**
         * @brief Unload current module
         */
        void unload();

        /**
         * @brief Check if module is loaded
         * @return True if module is loaded
         */
        bool isLoaded() const { return m_module != nullptr; }

        // Playback control
        void play();
        void pause();
        void stop();
        void seek(double seconds);

        /**
         * @brief Check if playback is active
         * @return True if playing
         */
        bool isPlaying() const { return m_playing.load(); }

        // Playback information
        double position() const { return m_position.load(); }
        double duration() const { return m_duration.load(); }
        QString title() const;
        QString artist() const;

        // Pattern/row information (for visualization)
        int getNumPatterns() const;
        int getNumChannels() const;
        int getCurrentPattern() const;
        int getCurrentRow() const;

        // Volume control
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
        /**
         * @brief Timer callback for audio chunk generation
         */
        void generateAudioChunk();

    private:
        /**
         * @brief Update playback position from module renderer
         */
        void updatePosition();

        /**
         * @brief Render audio from module into buffer
         * @param buffer Destination buffer
         * @param frames Number of frames to render
         */
        void renderAudio(float* buffer, int frames);

        std::unique_ptr<openmpt::module> m_module;  ///< OpenMPT module instance
        QTimer m_generationTimer;                   ///< Audio generation timer

        // Playback state (atomic for thread safety)
        std::atomic<bool> m_playing{false};
        std::atomic<double> m_position{0.0};
        std::atomic<double> m_duration{0.0};
        std::atomic<double> m_volume{1.0};

        // Audio rendering buffers
        std::vector<float> m_renderBuffer;

        // Audio format constants
        static constexpr int SAMPLE_RATE = 48000;
        static constexpr int CHANNELS = 2;
        static constexpr int CHUNK_FRAMES = 512;    ///< Render chunk size
    };

    // ============================================================================
    // Audio Source Types
    // ============================================================================

    /**
     * @brief Audio source types supported by the engine
     */
    enum class AudioSource {
        ExternalPCM,    ///< External PCM data (streaming, files)
        TrackerModule   ///< Module tracker playback
    };

    // ============================================================================
    // Main Audio Engine
    // ============================================================================

    /**
     * @brief Core audio processing and analysis engine
     *
     * Integrates all audio processing components: effects, analysis,
     * module playback, and real-time processing. Provides a unified
     * interface for both real-time and offline audio manipulation.
     */
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

        // Spectrum analysis (for visualization)
        QVariantList spectrumData() const;

        // Loudness measurement (EBU R128 compliant)
        double momentaryLoudness() const { return m_momentary.load(); }
        double shortTermLoudness() const { return m_shortTerm.load(); }
        double integratedLoudness();

        // Processing control
        void setProcessingEnabled(bool enabled);
        bool processingEnabled() const { return m_processingEnabled.load(); }

        // Karaoke mode
        void setKaraokeEnabled(bool enabled);
        bool karaokeEnabled() const { return m_karaokeEnabled.load(); }

        // Component access
        KaraokeProcessor* karaokeProcessor() { return &m_karaoke; }
        EqualizerProcessor* equalizer() { return &m_equalizer; }
        Crossfader* crossfader() { return &m_crossfader; }

        // PCM audio processing
        void processPCM(const QByteArray &data, int sampleRate, int channels = 2);

        /**
         * @brief Process audio buffer with all enabled effects
         * @param interleavedData Interleaved audio samples
         * @param frames Number of sample frames
         * @param sampleRate Audio sample rate in Hz
         * @param channels Number of channels (1=mono, 2=stereo)
         */
        void processBuffer(float* interleavedData, int frames, int sampleRate, int channels);

        // Module tracker integration
        bool loadTrackerModule(const QString& path);
        void unloadTracker();
        bool isTrackerMode() const { return m_currentSource == AudioSource::TrackerModule; }
        ModTrackerPlayback* tracker() { return m_trackerPlayback.get(); }
        bool isTrackerFile(const QString& path) const;

        // Tracker playback control
        void playTracker();
        void pauseTracker();
        void stopTracker();
        void seekTracker(double seconds);

        // Error handling
        QString lastError() const { return m_lastError; }
        void clearError() { m_lastError.clear(); }

    signals:
        void spectrumUpdated();                  ///< New spectrum data available
        void loudnessUpdated();                  ///< Loudness measurements updated
        void processingChanged(bool enabled);    ///< Processing enabled state changed
        void karaokeChanged(bool enabled);       ///< Karaoke mode changed
        void trackerModeChanged(bool trackerMode); ///< Audio source changed
        void errorOccurred(const QString& message); ///< Processing error occurred

        // Audio output management
        void setAudioOutput(std::unique_ptr<AudioOutput> output);
        AudioOutput* audioOutput() const { return m_output.get(); }

        // Pipeline processing for output
        void processOutput(float* buffer, int frames);

        // Spectrum analysis delegation
        QVector<float> calculateSpectrum(const float* data, int samples, int

    private:
        std::unique_ptr<AudioOutput> m_output;  // PipeWire/Qt/etc.

        // Processing callback for output
        std::function<void(float*, int)> m_outputCallback;

        /**
         * @brief Calculate FFT spectrum for visualization
         * @param mono Mono mix of input audio
         */
        void calculateFFT(const std::vector<float> &mono);

        /**
         * @brief Calculate loudness metrics (EBU R128)
         * @param data Audio samples
         * @param frames Number of frames
         */
        void calculateLoudness(const float* data, size_t frames);

        /**
         * @brief Convert interleaved audio to mono for analysis
         * @param data Interleaved audio
         * @param frames Number of frames
         * @param channels Number of channels
         * @return Mono audio vector
         */
        std::vector<float> toMono(const float* data, size_t frames, int channels) const;

        // FFT processing for spectrum analysis
        int m_fftSize = 4096;                                   ///< FFT window size
        int m_bands = 32;                                       ///< Number of spectrum bands
        std::unique_ptr<float, void(*)(void*)> m_fftIn;         ///< FFT input buffer
        std::unique_ptr<fftwf_complex[], void(*)(void*)> m_fftOut; ///< FFT output buffer
        fftwf_plan_s* m_fftPlan = nullptr;                      ///< FFTW plan

        QVector<double> m_spectrum;                             ///< Current spectrum data
        std::vector<double> m_fftWindow;                        ///< Hann window for FFT

        // Loudness analysis (EBU R128)
        struct EburStateDeleter {
            void operator()(ebur128_state* p) const;
        };
        std::unique_ptr<ebur128_state, EburStateDeleter> m_eburState;
        std::atomic<double> m_momentary{-70.0};                 ///< Momentary loudness
        std::atomic<double> m_shortTerm{-70.0};                 ///< Short-term loudness
        double m_integrated{-70.0};                             ///< Integrated loudness

        // Processing components
        EqualizerProcessor m_equalizer;                         ///< 10-band equalizer
        Crossfader m_crossfader;                                ///< Crossfade utility
        KaraokeProcessor m_karaoke;                             ///< Karaoke effects

        // Processing state
        std::atomic<bool> m_processingEnabled{false};           ///< Global processing enabled
        std::atomic<bool> m_karaokeEnabled{false};              ///< Karaoke mode enabled

        // Thread synchronization
        mutable QMutex m_mutex;                                 ///< General mutex for state changes

        // Module tracker playback
        std::unique_ptr<ModTrackerPlayback> m_trackerPlayback;  ///< Tracker playback engine
        AudioSource m_currentSource{AudioSource::ExternalPCM};  ///< Current audio source

        // Error handling
        QString m_lastError;                                    ///< Last error message
    };

} // namespace Aegis

// Register types for Qt's meta-object system
Q_DECLARE_METATYPE(Aegis::AudioSource)
Q_DECLARE_METATYPE(Aegis::Selection)
