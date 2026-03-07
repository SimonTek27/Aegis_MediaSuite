// djmix.cpp - DJ Mixer implementation using all three pillars
#include "audio_djmix.h"
#include <QFileInfo>
#include <QDebug>
#include <QtMath>
#include <algorithm>
#include <rubberband/RubberBandStretcher.h>
#include <sndfile.h>

namespace Aegis {

    // ============================================================================
    // DJClip Implementation
    // ============================================================================

    DJClip::DJClip(const QString &sourcePath, QObject* parent)
    : QObject(parent), m_sourcePath(sourcePath) {}

    DJClip::~DJClip() {
        unload();
    }

    bool DJClip::load() {
        // Use MpvBackend or direct file loading to populate buffer
        // For now, using EnhancedAudioBuffer directly
        m_buffer = std::make_shared<EnhancedAudioBuffer>();

        // Load via audio file parser (libsndfile, etc.)
        SF_INFO info;
        SNDFILE* file = sf_open(m_sourcePath.toUtf8().constData(), SFM_READ, &info);
        if (!file) return false;

        m_sampleRate = info.samplerate;
        m_channels = info.channels;
        m_totalSamples = info.frames;

        m_buffer->resize(m_totalSamples, m_channels);

        // Read samples
        sf_readf_float(file, m_buffer->data(), m_totalSamples);
        sf_close(file);

        // Initialize time-stretching if needed
        initializeStretcher();

        // Analyze BPM
        analyzeBpm();

        return true;
    }

    void DJClip::unload() {
        m_stretcher.reset();
        m_buffer.reset();
    }

    void DJClip::initializeStretcher() {
        m_stretcher = std::make_unique<RubberBand::RubberBandStretcher>(
            m_sampleRate, m_channels,
            RubberBand::RubberBandStretcher::OptionProcessRealTime |
            RubberBand::RubberBandStretcher::OptionEngineFaster
        );

        m_stretcher->setTimeRatio(m_tempoRatio);
        m_stretcher->setPitchScale(std::pow(2.0, m_pitchShift / 12.0));

        m_stretchInput.resize(4096 * m_channels);
        m_stretchOutput.resize(4096 * m_channels);
        m_stretcherPrimed = false;
    }

    bool DJClip::ensureStretcherPrimed() {
        if (m_stretcherPrimed || !m_stretcher) return true;

        const int primeFrames = m_stretcher->getPreferredStartPad();
        std::vector<float> silence(primeFrames * m_channels, 0.0f);

        // build per-channel input pointers for RubberBand
        std::vector<const float*> inPtrs(m_channels);
        for (int ch = 0; ch < m_channels; ++ch) {
            inPtrs[ch] = silence.data() + ch * primeFrames;
        }

        m_stretcher->process(inPtrs.data(), primeFrames, false);

        int delay = m_stretcher->getStartDelay();
        if (delay > 0 && m_stretcher->available() >= delay) {
            std::vector<float> discard(delay * m_channels);

            std::vector<float*> outPtrs(m_channels);
            for (int ch = 0; ch < m_channels; ++ch) {
                outPtrs[ch] = discard.data() + ch * delay;
            }

            m_stretcher->retrieve(outPtrs.data(), delay);
        }

        m_stretcherPrimed = true;
        return true;
    }

    bool DJClip::readSamples(int64_t startSample, int count, float *output) {
        if (!m_buffer) return false;

        if (!m_stretcher || (m_tempoRatio == 1.0 && m_pitchShift == 0.0)) {
            // No stretching - direct copy
            m_buffer->copyFrom(*m_buffer, static_cast<int>(startSample), 0, count);
            std::memcpy(output, m_buffer->data(), count * m_channels * sizeof(float));
            return true;
        }

        // Time-stretching mode
        ensureStretcherPrimed();

        int64_t sourcePos = static_cast<int64_t>(startSample / m_tempoRatio);
        int outputGenerated = 0;

        while (outputGenerated < count) {
            int available = m_stretcher->available();

            if (available > 0) {
                int toRetrieve = std::min(available, count - outputGenerated);

                std::vector<float*> outPtrs(m_channels);
                for (int ch = 0; ch < m_channels; ++ch) {
                    outPtrs[ch] = output + (outputGenerated * m_channels) + ch * toRetrieve;
                }

                m_stretcher->retrieve(outPtrs.data(), toRetrieve);
                outputGenerated += toRetrieve;
            } else {
                int inputFrames = std::min(4096, static_cast<int>(m_totalSamples - sourcePos));
                if (inputFrames <= 0) break;

                std::vector<float> input(inputFrames * m_channels);
                m_buffer->copyFrom(*m_buffer, static_cast<int>(sourcePos), 0, inputFrames);

                std::vector<const float*> inPtrs(m_channels);
                for (int ch = 0; ch < m_channels; ++ch) {
                    inPtrs[ch] = input.data() + ch * inputFrames;
                }

                bool final = (sourcePos + inputFrames >= m_totalSamples);
                m_stretcher->process(inPtrs.data(), inputFrames, final);
                sourcePos += inputFrames;
            }
        }

        if (outputGenerated < count) {
            std::memset(output + outputGenerated * m_channels, 0,
                        (count - outputGenerated) * m_channels * sizeof(float));
        }

        return true;
    }

    void DJClip::setTempoRatio(double ratio) {
        m_tempoRatio = ratio;
        if (m_stretcher) {
            m_stretcher->setTimeRatio(ratio);
            m_stretcher->reset();
            m_stretcherPrimed = false;
        }
    }

    void DJClip::setPitchShift(double semitones) {
        m_pitchShift = semitones;
        if (m_stretcher) {
            m_stretcher->setPitchScale(std::pow(2.0, semitones / 12.0));
        }
    }

    void DJClip::analyzeBpm() {
        // Use AudioEngine's analysis capabilities or implement beat detection
        // Simplified: detect peaks and calculate BPM
        m_bpm = 128.0; // Placeholder
        emit bpmDetected(m_bpm);
    }

    double DJClip::duration() const {
        return static_cast<double>(m_totalSamples) / m_sampleRate / m_tempoRatio;
    }

    // ============================================================================
    // DJDeck Implementation
    // ============================================================================

    DJDeck::DJDeck(int deckId, AudioEngine* engine,
                   std::shared_ptr<EffectChain> effects,
                   MpvBackend* backend, QObject* parent)
    : QObject(parent)
    , m_deckId(deckId)
    , m_engine(engine)
    , m_effects(effects)
    , m_backend(backend)
    {
        // Create EQ effects in the chain (Pillar 2)
        m_eqLow = std::make_shared<FilterEffect>(FilterEffect::LowShelf);

        m_eqMid = std::make_shared<FilterEffect>(FilterEffect::Peak);
        m_eqMid->setFrequency(1000.0f);
        m_eqMid->setGain(0.0f);

        m_eqHigh = std::make_shared<FilterEffect>(FilterEffect::HighShelf);
        m_eqHigh->setFrequency(4000.0f);
        m_eqHigh->setGain(0.0f);

        m_effects->addEffect(m_eqLow);
        m_effects->addEffect(m_eqMid);
        m_effects->addEffect(m_eqHigh);
        m_effects->addEffect(std::make_shared<PanEffect>(0.0f));

        // Connect to backend signals (Pillar 3)
        if (m_backend) {
            connect(m_backend, &MpvBackend::positionChanged,
                    this, &DJDeck::onBackendPositionChanged);
            connect(m_backend, &MpvBackend::finished,
                    this, &DJDeck::trackFinished);
        }
    }

bool DJDeck::loadTrack(const QString& filePath) {
        unloadTrack();

        m_currentClip = std::make_shared<DJClip>(filePath);
        if (!m_currentClip->load()) {
            m_currentClip.reset();
            return false;
        }

        // Load in MpvBackend (Pillar 3)
        if (m_backend) {
            m_backend->load(filePath);
        }

        QFileInfo fi(filePath);
        m_trackName = fi.baseName();

        emit trackChanged();
        return true;
    }

    void DJDeck::unloadTrack() {
        stop();
        m_currentClip.reset();
        m_trackName.clear();
        emit trackChanged();
    }

    void DJDeck::play() {
        if (m_backend) {
            m_backend->play();
            m_playing = true;
            emit stateChanged();
        }
    }

    void DJDeck::pause() {
        if (m_backend) {
            m_backend->pause();
            m_playing = false;
            emit stateChanged();
        }
    }

    void DJDeck::stop() {
        if (m_backend) {
            m_backend->stop();
            m_playing = false;
            emit stateChanged();
        }
    }

    void DJDeck::cue(bool enable) {
        m_cueing = enable;
        if (enable && m_backend) {
            // Jump to cue point
            m_backend->seek(m_cuePoint);
        }
        emit cueStateChanged();
    }

    double DJDeck::position() const {
        return m_backend ? m_backend->position() : 0.0;
    }

    void DJDeck::setPosition(double seconds) {
        if (m_backend) {
            m_backend->seek(seconds);
        }
    }

    void DJDeck::setPitch(double pitchPercent) {
        m_pitch = std::clamp(pitchPercent, -0.5, 0.5);
        updateTempoFromPitch();
        emit pitchChanged();
    }

    void DJDeck::updateTempoFromPitch() {
        if (!m_currentClip) return;
        double ratio = 1.0 + m_pitch;
        m_currentClip->setTempoRatio(ratio);
    }

    void DJDeck::setEqLow(float gain) {
        if (m_eqLow) {
            float db = (gain - 1.0f) * 24.0f;  // 0-2 -> -12 to +12 dB
            m_eqLow->setGain(db);
        }
    }

    void DJDeck::setEqMid(float gain) {
        if (m_eqMid) {
            float db = (gain - 1.0f) * 24.0f;
            m_eqMid->setGain(db);
        }
    }

    void DJDeck::setEqHigh(float gain) {
        if (m_eqHigh) {
            float db = (gain - 1.0f) * 24.0f;
            m_eqHigh->setGain(db);
        }
    }

    void DJDeck::onBackendPositionChanged(double pos) {
        emit positionChanged();
    }

    // ============================================================================
    // DJMixer Implementation
    // ============================================================================

    DJMixer::DJMixer(QObject* parent) : QObject(parent) {
        setupMixer();
    }

    DJMixer::~DJMixer() = default;

    DJDeck::~DJDeck() = default;

    void DJMixer::setupMixer() {
        // Pillar 1: Create AudioEngine
        m_engine = std::make_unique<AudioEngine>(this);

        // Pillar 2: Create effect chains for each deck and master
        m_deckAEffects = std::make_shared<EffectChain>();
        m_deckBEffects = std::make_shared<EffectChain>();
        m_masterEffects = std::make_shared<EffectChain>();

        // Add master compressor/limiter
        auto compressor = std::make_shared<CompressorEffect>();
        compressor->setThreshold(-6.0f);
        compressor->setRatio(4.0f);
        compressor->setMakeupGain(3.0f);
        m_masterEffects->addEffect(compressor);

        // Pillar 3: Create MpvBackend instances for each deck
        m_backendA = std::make_unique<MpvBackend>(this);
        m_backendB = std::make_unique<MpvBackend>(this);

        // Connect backend audio to engine
        m_backendA->setAudioCallback([this](const QByteArray& data, int sampleRate) {
            // Process through deck A effects then into AudioEngine
            // This would involve converting QByteArray to EnhancedAudioBuffer
            // and running through m_deckAEffects, then feeding to m_engine
            Q_UNUSED(data)
            Q_UNUSED(sampleRate)
        });

        // Create decks
        m_deckA = std::make_unique<DJDeck>(0, m_engine.get(), m_deckAEffects, m_backendA.get(), this);
        m_deckB = std::make_unique<DJDeck>(1, m_engine.get(), m_deckBEffects, m_backendB.get(), this);

        // Default crossfader to center
        updateCrossfader();
    }

    DJDeck* DJMixer::deckA() const { return m_deckA.get(); }
    DJDeck* DJMixer::deckB() const { return m_deckB.get(); }
    DJDeck* DJMixer::deck(int index) const {
        return (index == 0) ? m_deckA.get() : (index == 1) ? m_deckB.get() : nullptr;
    }

    double DJMixer::crossfader() const {
        return m_crossfader.load();
    }

    void DJMixer::setCrossfader(double value) {
        m_crossfader = std::clamp(value, 0.0, 1.0);
        updateCrossfader();
        emit crossfaderChanged();
    }

    void DJMixer::updateCrossfader() {
        double xf = m_crossfader.load();

        // Equal power crossfade: xf=0 -> A=1, B=0; xf=0.5 -> A=0.707, B=0.707
        double gainA = std::cos(xf * M_PI / 2.0) * 1.414;
        double gainB = std::sin(xf * M_PI / 2.0) * 1.414;

        // Apply through deck effect chain gain stages
        // This would adjust gain effects in m_deckAEffects/m_deckBEffects
    }

    double DJMixer::masterVolume() const {
        // Return AudioEngine's master volume
        return 1.0; // Placeholder
    }

    void DJMixer::setMasterVolume(double volume) {
        // Set AudioEngine's master volume
        Q_UNUSED(volume)
        emit masterVolumeChanged();
    }

    double DJMixer::masterBpm() const {
        return m_masterBpm.load();
    }

    void DJMixer::setMasterBpm(double bpm) {
        m_masterBpm = bpm;
        emit masterBpmChanged();
    }

    bool DJMixer::isPlaying() const {
        return m_engine ? true : false; // Check engine playing state
    }

    void DJMixer::play() {
        if (m_deckA) m_deckA->play();
        emit stateChanged();
    }

    void DJMixer::pause() {
        if (m_deckA) m_deckA->pause();
        if (m_deckB) m_deckB->pause();
        emit stateChanged();
    }

    void DJMixer::stop() {
        if (m_deckA) m_deckA->stop();
        if (m_deckB) m_deckB->stop();
        emit stateChanged();
    }

    bool DJMixer::startRecording(const QString& filePath) {
        if (m_recording) return false;

        SF_INFO info{};
        info.samplerate = 48000;
        info.channels = 2;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

        m_recordFile = sf_open(filePath.toUtf8().constData(), SFM_WRITE, &info);
        if (!m_recordFile) return false;

        m_recording = true;
        emit recordingStarted();
        return true;
    }

    void DJMixer::stopRecording() {
        if (!m_recording) return;
        m_recording = false;
        if (m_recordFile) {
            sf_close(m_recordFile);
            m_recordFile = nullptr;
        }
        emit recordingStopped();
    }

    bool DJMixer::isRecording() const {
        return m_recording;
    }

    void DJMixer::processAudio(float* masterOut, float* cueOut, int frames) {
        // This would be called by the audio output callback
        // 1. Get audio from each deck's MpvBackend
        // 2. Process through each deck's EffectChain
        // 3. Mix according to crossfader
        // 4. Process through master EffectChain
        // 5. Output to masterOut/cueOut

        if (m_recording && m_recordFile) {
            sf_writef_float(m_recordFile, masterOut, frames);
        }

        emit audioProcessed(masterOut, cueOut, frames);
    }

    // ─── DJDeck missing property getters ─────────────────────────────────────

    double DJDeck::duration() const {
        return m_backend ? m_backend->duration() : 0.0;
    }
    bool DJDeck::isPlaying() const { return m_playing.load(); }
    bool DJDeck::isCueing()  const { return m_cueing.load();  }

} // namespace Aegis
