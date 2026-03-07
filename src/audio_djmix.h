// djmix.h - DJ Mixer
#pragma once

#include "audio.h"
#include "audio_effects.h"
#include "mpv_backend.h"
#include <QObject>
#include <QReadWriteLock>
#include <memory>
#include <atomic>
#include <rubberband/RubberBandStretcher.h>

namespace Aegis {

    // ============================================================================
    // DJClip - Uses AudioClip base with time-stretching
    // ============================================================================

    class DJClip : public QObject {
        Q_OBJECT
    public:
        explicit DJClip(const QString &sourcePath, QObject* parent = nullptr);
        ~DJClip();

        bool load();
        void unload();

        // Audio access - feeds into AudioEngine
        bool readSamples(int64_t startSample, int count, float *output);

        // DJ-specific controls
        void setTempoRatio(double ratio);
        double tempoRatio() const { return m_tempoRatio; }

        void setPitchShift(double semitones);
        double pitchShift() const { return m_pitchShift; }

        // Beat analysis
        void analyzeBpm();
        double bpm() const { return m_bpm; }

        // Properties
        QString sourcePath() const { return m_sourcePath; }
        int64_t totalSamples() const { return m_totalSamples; }
        int sampleRate() const { return m_sampleRate; }
        int channels() const { return m_channels; }
        double duration() const;

        // For AudioEngine integration
        std::shared_ptr<EnhancedAudioBuffer> getBuffer() const { return m_buffer; }

    signals:
        void positionChanged(int64_t sample);
        void nearEnd();
        void bpmDetected(double bpm);

    private:
        void initializeStretcher();
        bool ensureStretcherPrimed();

        QString m_sourcePath;
        std::shared_ptr<EnhancedAudioBuffer> m_buffer;
        std::unique_ptr<::RubberBand::RubberBandStretcher> m_stretcher;

        // State
        std::atomic<double> m_tempoRatio{1.0};
        std::atomic<double> m_pitchShift{0.0};
        std::atomic<double> m_bpm{0.0};

        int64_t m_totalSamples = 0;
        int m_sampleRate = 48000;
        int m_channels = 2;

        // Stretcher state
        std::vector<float> m_stretchInput;
        std::vector<float> m_stretchOutput;
        int64_t m_stretchInputPosition = 0;
        bool m_stretcherPrimed = false;
    };

    // ============================================================================
    // DJDeck - Individual deck controller
    // ============================================================================

    class DJDeck : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString trackName READ trackName NOTIFY trackChanged)
        Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
        Q_PROPERTY(double duration READ duration NOTIFY trackChanged)
        Q_PROPERTY(double bpm READ bpm NOTIFY bpmChanged)
        Q_PROPERTY(double pitch READ pitch WRITE setPitch NOTIFY pitchChanged)
        Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY stateChanged)
        Q_PROPERTY(bool isCueing READ isCueing NOTIFY cueStateChanged)

    public:
        /**
         * @brief Construct deck with explicit pillar dependencies
         * @param deckId Unique deck identifier
         * @param engine AudioEngine for DSP processing (Pillar 1)
         * @param effects EffectChain for deck processing (Pillar 2)
         * @param backend MpvBackend for media decoding (Pillar 3)
         * @param parent QObject parent
         */
        explicit DJDeck(int deckId, AudioEngine* engine,
                        std::shared_ptr<EffectChain> effects,
                        MpvBackend* backend, QObject* parent = nullptr);
        ~DJDeck();

        // Track loading
        bool loadTrack(const QString& filePath);
        void unloadTrack();
        bool isLoaded() const;

        QString trackName() const { return m_trackName; }
        double duration() const;

        // Transport control
        void play();
        void pause();
        void stop();
        void cue(bool enable);

        bool isPlaying() const;
        bool isCueing() const;

        // Position control
        double position() const;
        void setPosition(double seconds);
        void setCuePoint(double seconds);
        void jumpToCue();

        // Pitch control (-0.5 to +0.5 for ±50%)
        double pitch() const { return m_pitch; }
        void setPitch(double pitchPercent);

        // BPM
        double bpm() const { return m_currentClip ? m_currentClip->bpm() : 0.0; }

        void syncToBpm(double targetBpm);

        // Nudge (temporary pitch bend)
        void nudgeForward();
        void nudgeBackward();

        // EQ controls (0.0-2.0, 1.0 = unity) - delegate to EffectChain (Pillar 2)
        void setEqLow(float gain);
        void setEqMid(float gain);
        void setEqHigh(float gain);

        // Waveform for UI
        QVector<float> getWaveformData(int width = 800);

        // Beat sync
        double beatPhase() const;
        void quantizeToBeat();

        // Direct access to pillars
        AudioEngine* engine() const { return m_engine; }
        EffectChain* effects() const { return m_effects.get(); }
        MpvBackend* backend() const { return m_backend; }

    signals:
        void trackChanged();
        void positionChanged();
        void stateChanged();
        void cueStateChanged();
        void bpmChanged();
        void pitchChanged();
        void trackFinished();
        void beatSync(int beatNumber);

    private:
        void updateTempoFromPitch();
        void onBackendPositionChanged(double pos);

        int m_deckId;

        // Pillar dependencies
        AudioEngine* m_engine;                    // Pillar 1 (borrowed)
        std::shared_ptr<EffectChain> m_effects;   // Pillar 2 (shared)
        MpvBackend* m_backend;                    // Pillar 3 (borrowed)

        // Track data
        std::shared_ptr<DJClip> m_currentClip;
        QString m_trackName;

        // State
        std::atomic<double> m_cuePoint{0.0};
        std::atomic<double> m_pitch{0.0};
        std::atomic<bool> m_nudging{false};
        std::atomic<bool> m_cueing{false};
        std::atomic<bool> m_playing{false};

        // EQ effect references from EffectChain
        std::shared_ptr<FilterEffect> m_eqLow;
        std::shared_ptr<FilterEffect> m_eqMid;
        std::shared_ptr<FilterEffect> m_eqHigh;
    };

    // ============================================================================
    // DJMixer - Main mixer controller using all three pillars
    // ============================================================================

    class DJMixer : public QObject {
        Q_OBJECT
        Q_PROPERTY(double crossfader READ crossfader WRITE setCrossfader NOTIFY crossfaderChanged)
        Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY masterVolumeChanged)
        Q_PROPERTY(double masterBpm READ masterBpm WRITE setMasterBpm NOTIFY masterBpmChanged)
        Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY stateChanged)

    public:
        /**
         * @brief Construct mixer with full pillar initialization
         * @param parent QObject parent
         */
        explicit DJMixer(QObject* parent = nullptr);
        ~DJMixer();

        // Deck access
        DJDeck* deckA() const;
        DJDeck* deckB() const;
        DJDeck* deck(int index) const;

        // Mixer controls
        double crossfader() const; // 0.0 = A, 0.5 = center, 1.0 = B
        void setCrossfader(double value);

        double masterVolume() const;
        void setMasterVolume(double volume);

        double masterBpm() const;
        void setMasterBpm(double bpm);

        bool isPlaying() const;

        // Transport
        void play();
        void pause();
        void stop();

        // Recording
        bool startRecording(const QString& filePath);
        void stopRecording();
        bool isRecording() const;

        // Audio I/O integration - delegates to AudioEngine (Pillar 1)
        void processAudio(float* masterOut, float* cueOut, int frames);

        // Direct pillar access
        AudioEngine* audioEngine() const { return m_engine.get(); }
        EffectChain* masterEffects() const { return m_masterEffects.get(); }

    signals:
        void crossfaderChanged();
        void masterVolumeChanged();
        void masterBpmChanged();
        void stateChanged();
        void recordingStarted();
        void recordingStopped();
        void audioProcessed(const float* master, const float* cue, int frames);

    private:
        void setupMixer();
        void updateCrossfader();
        void applyMasterEffects(float* buffer, int frames);

        // Pillar 1: Audio engine (owned)
        std::unique_ptr<AudioEngine> m_engine;

        // Pillar 2: Effect chains
        std::shared_ptr<EffectChain> m_deckAEffects;
        std::shared_ptr<EffectChain> m_deckBEffects;
        std::shared_ptr<EffectChain> m_masterEffects;

        // Pillar 3: MpvBackend instances for each deck
        std::unique_ptr<MpvBackend> m_backendA;
        std::unique_ptr<MpvBackend> m_backendB;

        // Decks
        std::unique_ptr<DJDeck> m_deckA;
        std::unique_ptr<DJDeck> m_deckB;

        // State
        std::atomic<double> m_crossfader{0.5};
        std::atomic<double> m_masterBpm{128.0};
        std::atomic<bool> m_recording{false};

        // Recording
        SNDFILE* m_recordFile = nullptr;
    };

} // namespace Aegis
