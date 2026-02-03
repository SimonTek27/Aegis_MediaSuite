// audio_effects.cpp - Audio mixing engine implementation

#include "audio.h"
#include "mpv_backend.h"
#include "audio_effects.h"
#include "audio_output.h"
#include "ffmpeg_utils.h"
#include <QtMath>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace Aegis {

// ============================================================================
// AudioBuffer Implementation
// ============================================================================

AudioBuffer::AudioBuffer(int samples, int channels)
    : m_samples(samples)
    , m_channels(channels)
{
    m_data.resize(samples * channels);
}

void AudioBuffer::resize(int samples, int channels) {
    m_samples = samples;
    m_channels = channels;
    m_data.resize(samples * channels);
}

void AudioBuffer::clear() {
    m_data.clear();
    m_samples = 0;
}

void AudioBuffer::fill(float value) {
    m_data.fill(value);
}

float* AudioBuffer::channelData(int channel) {
    if (channel < 0 || channel >= m_channels) return nullptr;
    return m_data.data() + channel;
}

const float* AudioBuffer::channelData(int channel) const {
    if (channel < 0 || channel >= m_channels) return nullptr;
    return m_data.data() + channel;
}

void AudioBuffer::mix(const AudioBuffer &other, float gain, int dstOffset) {
    int count = std::min(other.totalSamples(), totalSamples() - dstOffset * m_channels);
    if (count <= 0) return;
    
    const float *src = other.data();
    float *dst = m_data.data() + dstOffset * m_channels;
    
    for (int i = 0; i < count; i++) {
        dst[i] += src[i] * gain;
    }
}

void AudioBuffer::applyGain(float gain) {
    for (int i = 0; i < m_data.size(); i++) {
        m_data[i] *= gain;
    }
}

void AudioBuffer::applyGainRamp(float startGain, float endGain) {
    int total = totalSamples();
    for (int i = 0; i < total; i++) {
        float t = i / static_cast<float>(total);
        float gain = startGain + (endGain - startGain) * t;
        m_data[i] *= gain;
    }
}

float AudioBuffer::peakLevel(int channel) const {
    if (m_data.isEmpty()) return 0.0f;
    
    float peak = 0.0f;
    if (channel < 0) {
        // All channels
        for (float sample : m_data) {
            peak = std::max(peak, std::abs(sample));
        }
    } else {
        // Specific channel
        for (int i = channel; i < m_data.size(); i += m_channels) {
            peak = std::max(peak, std::abs(m_data[i]));
        }
    }
    return peak;
}

float AudioBuffer::rmsLevel(int channel) const {
    if (m_data.isEmpty()) return 0.0f;
    
    double sum = 0.0;
    int count = 0;
    
    if (channel < 0) {
        for (float sample : m_data) {
            sum += sample * sample;
        }
        count = m_data.size();
    } else {
        for (int i = channel; i < m_data.size(); i += m_channels) {
            sum += m_data[i] * m_data[i];
        }
        count = m_data.size() / m_channels;
    }
    
    return static_cast<float>(std::sqrt(sum / count));
}

void AudioBuffer::copyFrom(const AudioBuffer &src, int srcOffset, int dstOffset, int count) {
    int srcStart = srcOffset * src.m_channels;
    int dstStart = dstOffset * m_channels;
    int copyCount = std::min(count * src.m_channels, 
                             std::min(src.totalSamples() - srcStart, totalSamples() - dstStart));
    
    if (copyCount > 0) {
        std::memcpy(m_data.data() + dstStart, src.data() + srcStart, copyCount * sizeof(float));
    }
}

// ============================================================================
// Gain Effect Implementation
// ============================================================================

GainEffect::GainEffect(float gainDb, QObject *parent)
    : AudioEffect(parent)
    , m_gainDb(gainDb)
{
    m_gainLinear = std::pow(10.0f, gainDb / 20.0f);
}

void GainEffect::setGainDb(float gainDb) {
    m_gainDb = gainDb;
    m_gainLinear = std::pow(10.0f, gainDb / 20.0f);
}

void GainEffect::process(AudioBuffer &buffer, int sampleRate) {
    Q_UNUSED(sampleRate)
    if (!m_enabled || m_gainLinear == 1.0f) return;
    buffer.applyGain(m_gainLinear);
}

QVariantMap GainEffect::parameters() const {
    QVariantMap params;
    params["gainDb"] = m_gainDb;
    return params;
}

void GainEffect::setParameters(const QVariantMap &params) {
    if (params.contains("gainDb")) {
        setGainDb(params["gainDb"].toFloat());
    }
}

// ============================================================================
// Pan Effect Implementation
// ============================================================================

PanEffect::PanEffect(float pan, QObject *parent)
    : AudioEffect(parent)
    , m_pan(pan)
{
    // Calculate gains using constant power panning
    float angle = (pan + 1.0f) * M_PI / 4.0f;
    m_leftGain = std::cos(angle) * 1.414f;
    m_rightGain = std::sin(angle) * 1.414f;
}

void PanEffect::setPan(float pan) {
    m_pan = std::clamp(pan, -1.0f, 1.0f);
    float angle = (m_pan + 1.0f) * M_PI / 4.0f;
    m_leftGain = std::cos(angle) * 1.414f;
    m_rightGain = std::sin(angle) * 1.414f;
}

void PanEffect::process(AudioBuffer &buffer, int sampleRate) {
    Q_UNUSED(sampleRate)
    if (!m_enabled || m_pan == 0.0f || buffer.channels() < 2) return;
    
    float *data = buffer.data();
    int samples = buffer.samples();
    
    for (int i = 0; i < samples; i++) {
        data[i * 2] *= m_leftGain;      // Left
        data[i * 2 + 1] *= m_rightGain; // Right
    }
}

QVariantMap PanEffect::parameters() const {
    QVariantMap params;
    params["pan"] = m_pan;
    return params;
}

void PanEffect::setParameters(const QVariantMap &params) {
    if (params.contains("pan")) {
        setPan(params["pan"].toFloat());
    }
}

// ============================================================================
// Compressor Effect Implementation
// ============================================================================

CompressorEffect::CompressorEffect(QObject *parent)
    : AudioEffect(parent)
{
    setAttack(m_attackMs);
    setRelease(m_releaseMs);
}

void CompressorEffect::setThreshold(float thresholdDb) {
    m_thresholdDb = thresholdDb;
}

void CompressorEffect::setRatio(float ratio) {
    m_ratio = std::max(1.0f, ratio);
}

void CompressorEffect::setAttack(float attackMs) {
    m_attackMs = attackMs;
    // Attack coefficient: time constant for envelope follower
    m_attackCoeff = std::exp(-1.0f / (attackMs * 0.001f * 48000.0f));
}

void CompressorEffect::setRelease(float releaseMs) {
    m_releaseMs = releaseMs;
    m_releaseCoeff = std::exp(-1.0f / (releaseMs * 0.001f * 48000.0f));
}

void CompressorEffect::setMakeupGain(float gainDb) {
    m_makeupGainDb = gainDb;
}

void CompressorEffect::process(AudioBuffer &buffer, int sampleRate) {
    Q_UNUSED(sampleRate)
    if (!m_enabled) return;
    
    float makeupGain = std::pow(10.0f, m_makeupGainDb / 20.0f);
    float thresholdLinear = std::pow(10.0f, m_thresholdDb / 20.0f);
    
    float *data = buffer.data();
    int totalSamples = buffer.totalSamples();
    
    for (int i = 0; i < totalSamples; i++) {
        // Get input level
        float input = data[i];
        float inputLevel = std::abs(input);
        
        // Envelope follower
        if (inputLevel > m_envelope) {
            m_envelope = m_attackCoeff * (m_envelope - inputLevel) + inputLevel;
        } else {
            m_envelope = m_releaseCoeff * (m_envelope - inputLevel) + inputLevel;
        }
        
        // Calculate gain reduction
        float gain = 1.0f;
        if (m_envelope > thresholdLinear) {
            float dbOver = 20.0f * std::log10(m_envelope / thresholdLinear);
            float dbGainReduction = dbOver * (1.0f - 1.0f / m_ratio);
            gain = std::pow(10.0f, -dbGainReduction / 20.0f);
        }
        
        // Apply gain
        data[i] = input * gain * makeupGain;
    }
}

QVariantMap CompressorEffect::parameters() const {
    QVariantMap params;
    params["threshold"] = m_thresholdDb;
    params["ratio"] = m_ratio;
    params["attack"] = m_attackMs;
    params["release"] = m_releaseMs;
    params["makeupGain"] = m_makeupGainDb;
    return params;
}

void CompressorEffect::setParameters(const QVariantMap &params) {
    if (params.contains("threshold")) setThreshold(params["threshold"].toFloat());
    if (params.contains("ratio")) setRatio(params["ratio"].toFloat());
    if (params.contains("attack")) setAttack(params["attack"].toFloat());
    if (params.contains("release")) setRelease(params["release"].toFloat());
    if (params.contains("makeupGain")) setMakeupGain(params["makeupGain"].toFloat());
}

// ============================================================================
// EQ Effect Implementation
// ============================================================================

float EQEffect::Filter::process(float input) {
    float output = (b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
    x2 = x1;
    x1 = input;
    y2 = y1;
    y1 = output;
    return output;
}

void EQEffect::Filter::setPeaking(float freq, float q, float gainDb, int sampleRate) {
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * q);
    float A = std::pow(10.0f, gainDb / 40.0f);
    
    b0 = 1.0f + alpha * A;
    b1 = -2.0f * cosw0;
    b2 = 1.0f - alpha * A;
    a0 = 1.0f + alpha / A;
    a1 = -2.0f * cosw0;
    a2 = 1.0f - alpha / A;
}

EQEffect::EQEffect(QObject *parent)
    : AudioEffect(parent)
{
    updateFilters(48000);
}

void EQEffect::updateFilters(int sampleRate) {
    if (sampleRate == m_lastSampleRate) return;
    m_lastSampleRate = sampleRate;
    
    m_lowFilter.setPeaking(m_lowFreq, 0.7f, m_lowGain, sampleRate);
    m_midFilter.setPeaking(1000.0f, 0.7f, m_midGain, sampleRate);
    m_highFilter.setPeaking(m_highFreq, 0.7f, m_highGain, sampleRate);
}

void EQEffect::setLowGain(float gainDb) {
    m_lowGain = gainDb;
    m_lowFilter.setPeaking(m_lowFreq, 0.7f, m_lowGain, m_lastSampleRate);
}

void EQEffect::setMidGain(float gainDb) {
    m_midGain = gainDb;
    m_midFilter.setPeaking(1000.0f, 0.7f, m_midGain, m_lastSampleRate);
}

void EQEffect::setHighGain(float gainDb) {
    m_highGain = gainDb;
    m_highFilter.setPeaking(m_highFreq, 0.7f, m_highGain, m_lastSampleRate);
}

void EQEffect::setLowFreq(float freq) {
    m_lowFreq = freq;
    m_lowFilter.setPeaking(m_lowFreq, 0.7f, m_lowGain, m_lastSampleRate);
}

void EQEffect::setHighFreq(float freq) {
    m_highFreq = freq;
    m_highFilter.setPeaking(m_highFreq, 0.7f, m_highGain, m_lastSampleRate);
}

void EQEffect::process(AudioBuffer &buffer, int sampleRate) {
    if (!m_enabled) return;
    
    updateFilters(sampleRate);
    
    float *data = buffer.data();
    int samples = buffer.samples();
    int channels = buffer.channels();
    
    for (int i = 0; i < samples; i++) {
        for (int ch = 0; ch < channels; ch++) {
            float sample = data[i * channels + ch];
            sample = m_lowFilter.process(sample);
            sample = m_midFilter.process(sample);
            sample = m_highFilter.process(sample);
            data[i * channels + ch] = sample;
        }
    }
}

QVariantMap EQEffect::parameters() const {
    QVariantMap params;
    params["lowGain"] = m_lowGain;
    params["midGain"] = m_midGain;
    params["highGain"] = m_highGain;
    params["lowFreq"] = m_lowFreq;
    params["highFreq"] = m_highFreq;
    return params;
}

void EQEffect::setParameters(const QVariantMap &params) {
    if (params.contains("lowGain")) setLowGain(params["lowGain"].toFloat());
    if (params.contains("midGain")) setMidGain(params["midGain"].toFloat());
    if (params.contains("highGain")) setHighGain(params["highGain"].toFloat());
    if (params.contains("lowFreq")) setLowFreq(params["lowFreq"].toFloat());
    if (params.contains("highFreq")) setHighFreq(params["highFreq"].toFloat());
}

// ============================================================================
// Reverb Effect Implementation
// ============================================================================

class ReverbEffect::CombFilter {
public:
    void setParams(float size, float damping, int sampleRate) {
        int delayMs = static_cast<int>(size * 100);
        m_delaySamples = delayMs * sampleRate / 1000;
        m_buffer.resize(m_delaySamples);
        m_damping = damping;
    }
    
    float process(float input) {
        float output = m_buffer[m_pos];
        m_store = (output * (1.0f - m_damping)) + (m_store * m_damping);
        m_buffer[m_pos] = input + (m_store * 0.8f);
        
        m_pos++;
        if (m_pos >= m_delaySamples) m_pos = 0;
        
        return output;
    }
    
private:
    QVector<float> m_buffer;
    int m_delaySamples = 0;
    int m_pos = 0;
    float m_damping = 0.5f;
    float m_store = 0.0f;
};

class ReverbEffect::AllPassFilter {
public:
    void setParams(int delayMs, int sampleRate) {
        m_delaySamples = delayMs * sampleRate / 1000;
        m_buffer.resize(m_delaySamples);
    }
    
    float process(float input) {
        float buffered = m_buffer[m_pos];
        float output = buffered - input;
        m_buffer[m_pos] = input + (buffered * 0.5f);
        
        m_pos++;
        if (m_pos >= m_delaySamples) m_pos = 0;
        
        return output;
    }
    
private:
    QVector<float> m_buffer;
    int m_delaySamples = 0;
    int m_pos = 0;
};

ReverbEffect::ReverbEffect(QObject *parent)
    : AudioEffect(parent)
{
    initializeFilters();
}

ReverbEffect::~ReverbEffect() = default;

void ReverbEffect::initializeFilters() {
    m_combFilters = std::make_unique<CombFilter[]>(4);
    m_allPassFilters = std::make_unique<AllPassFilter[]>(2);
}

void ReverbEffect::setRoomSize(float size) {
    m_roomSize = std::clamp(size, 0.0f, 1.0f);
    for (int i = 0; i < 4; i++) {
        m_combFilters[i].setParams(m_roomSize, m_damping, m_sampleRate);
    }
}

void ReverbEffect::setDamping(float damping) {
    m_damping = std::clamp(damping, 0.0f, 1.0f);
    for (int i = 0; i < 4; i++) {
        m_combFilters[i].setParams(m_roomSize, m_damping, m_sampleRate);
    }
}

void ReverbEffect::setWetLevel(float wet) {
    m_wetLevel = std::clamp(wet, 0.0f, 1.0f);
}

void ReverbEffect::process(AudioBuffer &buffer, int sampleRate) {
    if (!m_enabled || m_wetLevel <= 0.0f) return;
    
    if (sampleRate != m_sampleRate) {
        m_sampleRate = sampleRate;
        initializeFilters();
        setRoomSize(m_roomSize);
        setDamping(m_damping);
        m_allPassFilters[0].setParams(25, sampleRate);
        m_allPassFilters[1].setParams(8, sampleRate);
    }
    
    float *data = buffer.data();
    int samples = buffer.samples();
    int channels = buffer.channels();
    
    for (int i = 0; i < samples; i++) {
        // Mix channels for reverb input
        float input = 0.0f;
        for (int ch = 0; ch < channels; ch++) {
            input += data[i * channels + ch];
        }
        input /= channels;
        
        // Process through comb filters
        float combOut = 0.0f;
        for (int c = 0; c < 4; c++) {
            combOut += m_combFilters[c].process(input);
        }
        combOut *= 0.25f;
        
        // Process through all-pass filters
        float reverb = m_allPassFilters[0].process(combOut);
        reverb = m_allPassFilters[1].process(reverb);
        
        // Mix wet/dry
        for (int ch = 0; ch < channels; ch++) {
            float dry = data[i * channels + ch];
            data[i * channels + ch] = dry * (1.0f - m_wetLevel) + reverb * m_wetLevel;
        }
    }
}

QVariantMap ReverbEffect::parameters() const {
    QVariantMap params;
    params["roomSize"] = m_roomSize;
    params["damping"] = m_damping;
    params["wetLevel"] = m_wetLevel;
    return params;
}

void ReverbEffect::setParameters(const QVariantMap &params) {
    if (params.contains("roomSize")) setRoomSize(params["roomSize"].toFloat());
    if (params.contains("damping")) setDamping(params["damping"].toFloat());
    if (params.contains("wetLevel")) setWetLevel(params["wetLevel"].toFloat());
}

// ============================================================================
// AudioTrack Implementation
// ============================================================================

AudioTrack::AudioTrack(const QString &name, QObject *parent)
    : QObject(parent)
    , m_name(name)
{
}

void AudioTrack::addClip(std::shared_ptr<AudioClip> clip, int64_t startSample) {
    QWriteLocker lock(&m_lock);
    
    ClipInstance instance;
    instance.clip = clip;
    instance.startSample = startSample;
    instance.endSample = startSample + (clip ? clip->totalSamples() : 0);
    
    m_clips.append(instance);
    lock.unlock();
    
    emit clipAdded(clip);
}

void AudioTrack::removeClip(std::shared_ptr<AudioClip> clip) {
    QWriteLocker lock(&m_lock);
    
    for (int i = 0; i < m_clips.size(); i++) {
        if (m_clips[i].clip == clip) {
            m_clips.removeAt(i);
            lock.unlock();
            emit clipRemoved(clip);
            return;
        }
    }
}

void AudioTrack::moveClip(std::shared_ptr<AudioClip> clip, int64_t newPosition) {
    QWriteLocker lock(&m_lock);
    
    for (auto &instance : m_clips) {
        if (instance.clip == clip) {
            instance.startSample = newPosition;
            instance.endSample = newPosition + (clip ? clip->totalSamples() : 0);
            break;
        }
    }
}

void AudioTrack::addEffect(std::shared_ptr<AudioEffect> effect) {
    m_effects.append(effect);
    emit effectsChanged();
}

void AudioTrack::removeEffect(int index) {
    if (index >= 0 && index < m_effects.size()) {
        m_effects.removeAt(index);
        emit effectsChanged();
    }
}

void AudioTrack::moveEffect(int from, int to) {
    if (from >= 0 && from < m_effects.size() && to >= 0 && to < m_effects.size()) {
        m_effects.move(from, to);
        emit effectsChanged();
    }
}

QList<std::shared_ptr<AudioEffect>> AudioTrack::effects() const {
    return m_effects;
}

void AudioTrack::readOutput(int64_t startSample, int count, float *output, int outputChannels) {
    if (m_muted) {
        std::memset(output, 0, count * outputChannels * sizeof(float));
        return;
    }
    
    QReadLocker lock(&m_lock);
    
    // Temporary buffer for processing
    AudioBuffer tempBuffer(count, outputChannels);
    tempBuffer.zero();
    
    // Mix all clips at this position
    for (const auto &instance : m_clips) {
        if (!instance.clip) continue;
        
        int64_t clipStart = instance.startSample;
        int64_t clipEnd = instance.endSample;
        
        if (startSample + count < clipStart || startSample >= clipEnd) {
            continue;  // Clip not in range
        }
        
        int64_t clipOffset = startSample - clipStart;
        int readCount = std::min<int>(count, static_cast<int>(clipEnd - startSample));
        
        if (clipOffset < 0) {
            readCount += static_cast<int>(clipOffset);
            clipOffset = 0;
        }
        
        if (readCount > 0) {
            AudioBuffer clipData = instance.clip->readBuffer(clipOffset, readCount);
            tempBuffer.mix(clipData, 1.0f, std::max<int64_t>(0, clipStart - startSample));
        }
    }
    
    lock.unlock();
    
    // Apply track effects
    for (auto &effect : m_effects) {
        if (effect && effect->isEnabled()) {
            effect->process(tempBuffer, 48000);  // TODO: Get actual sample rate
        }
    }
    
    // Apply volume and pan
    float vol = m_volume;
    if (vol != 1.0f) {
        tempBuffer.applyGain(vol);
    }
    
    // Apply pan if stereo
    if (outputChannels >= 2 && m_pan != 0.0f) {
        float angle = (m_pan + 1.0f) * M_PI / 4.0f;
        float leftGain = std::cos(angle) * 1.414f;
        float rightGain = std::sin(angle) * 1.414f;
        
        float *data = tempBuffer.data();
        for (int i = 0; i < count; i++) {
            data[i * 2] *= leftGain;
            data[i * 2 + 1] *= rightGain;
        }
    }
    
    // Copy to output
    std::memcpy(output, tempBuffer.data(), count * outputChannels * sizeof(float));
}

QList<std::shared_ptr<AudioClip>> AudioTrack::clipsAt(int64_t position) const {
    QReadLocker lock(&m_lock);
    
    QList<std::shared_ptr<AudioClip>> result;
    for (const auto &instance : m_clips) {
        if (position >= instance.startSample && position < instance.endSample) {
            result.append(instance.clip);
        }
    }
    return result;
}

// ============================================================================
// AudioMixer Implementation
// ============================================================================

AudioMixer::AudioMixer(QObject *parent)
    : QObject(parent)
{
}

AudioMixer::~AudioMixer() {
    shutdown();
}

bool AudioMixer::initialize(int sampleRate, int channels, int bufferSize) {
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_bufferSize = bufferSize;
    
    m_mixBuffer.resize(bufferSize, channels);
    m_running = true;
    
    return true;
}

void AudioMixer::shutdown() {
    stop();
    m_running = false;
    
    qDeleteAll(m_tracks);
    m_tracks.clear();
}

AudioTrack* AudioMixer::addTrack(const QString &name) {
    auto *track = new AudioTrack(name.isEmpty() ? tr("Track %1").arg(m_tracks.size() + 1) : name, this);
    m_tracks.append(track);
    return track;
}

void AudioMixer::removeTrack(AudioTrack *track) {
    m_tracks.removeAll(track);
    delete track;
}

QList<AudioTrack*> AudioMixer::tracks() const {
    return m_tracks;
}

void AudioMixer::addMasterEffect(std::shared_ptr<AudioEffect> effect) {
    m_masterEffects.append(effect);
}

void AudioMixer::removeMasterEffect(int index) {
    if (index >= 0 && index < m_masterEffects.size()) {
        m_masterEffects.removeAt(index);
    }
}

void AudioMixer::play() {
    if (!m_running) return;
    m_playing = true;
    emit playbackStarted();
}

void AudioMixer::pause() {
    m_playing = false;
    emit playbackPaused();
}

void AudioMixer::stop() {
    m_playing = false;
    m_currentSample = 0;
    emit playbackStopped();
}

void AudioMixer::seek(int64_t samplePosition) {
    m_currentSample = std::max(int64_t(0), samplePosition);
    emit positionChanged(m_currentSample);
}

void AudioMixer::enableScrubbing(bool enable) {
    m_srubbing = enable;
}

void AudioMixer::scrubTo(int64_t samplePosition) {
    m_scrubTarget = samplePosition;
}

void AudioMixer::processMix(float *output, int frames) {
    if (!m_running) return;
    
    // Check for scrub
    if (m_srubbing) {
        m_currentSample = m_scrubTarget;
    }
    
    // Clear output
    std::memset(output, 0, frames * m_channels * sizeof(float));
    
    if (!m_playing && !m_srubbing) {
        return;
    }
    
    // Check if any track is soloed
    bool hasSolo = false;
    for (auto *track : m_tracks) {
        if (track->isSolo()) {
            hasSolo = true;
            break;
        }
    }
    
    // Mix all tracks
    for (auto *track : m_tracks) {
        if (track->isMuted()) continue;
        if (hasSolo && !track->isSolo()) continue;
        
        track->readOutput(m_currentSample, frames, m_mixBuffer.data(), m_channels);
        
        // Add to output
        for (int i = 0; i < frames * m_channels; i++) {
            output[i] += m_mixBuffer.data()[i];
        }
    }
    
    // Apply master effects
    AudioBuffer masterBuffer(frames, m_channels);
    std::memcpy(masterBuffer.data(), output, frames * m_channels * sizeof(float));
    
    for (auto &effect : m_masterEffects) {
        if (effect && effect->isEnabled()) {
            effect->process(masterBuffer, m_sampleRate);
        }
    }
    
    // Apply master volume
    if (m_masterVolume != 1.0f) {
        masterBuffer.applyGain(m_masterVolume);
    }
    
    // Apply limiter
    if (m_limiterEnabled) {
        applyLimiter(masterBuffer.data(), masterBuffer.totalSamples());
    }
    
    // Copy back to output
    std::memcpy(output, masterBuffer.data(), frames * m_channels * sizeof(float));
    
    // Advance position
    if (m_playing && !m_srubbing) {
        m_currentSample += frames;
        emit positionChanged(m_currentSample);
    }
}

void AudioMixer::applyLimiter(float *data, int samples) {
    float thresholdLinear = std::pow(10.0f, m_limiterThreshold / 20.0f);
    
    for (int i = 0; i < samples; i++) {
        float sample = data[i];
        float absSample = std::abs(sample);
        
        if (absSample > thresholdLinear) {
            data[i] = (sample / absSample) * thresholdLinear;
        }
    }
}

bool AudioMixer::startRender(const QString &outputPath, int64_t startSample, int64_t endSample) {
    if (m_rendering) return false;
    
    m_rendering = true;
    m_renderStart = startSample;
    m_renderEnd = endSample;
    m_renderPosition = startSample;
    
    // TODO: Initialize FFmpeg encoder and render loop
    
    return true;
}

void AudioMixer::stopRender() {
    m_rendering = false;
    emit renderFinished();
}

double AudioMixer::renderProgress() const {
    if (!m_rendering || m_renderEnd <= m_renderStart) return 0.0;
    return static_cast<double>(m_renderPosition - m_renderStart) / (m_renderEnd - m_renderStart);
}

// ============================================================================
// AudioClip Implementation (stub - would use FFmpeg)
// ============================================================================

AudioClip::AudioClip(const QString &sourcePath, QObject *parent)
    : QObject(parent)
    , m_sourcePath(sourcePath)
{
}

AudioClip::~AudioClip() = default;

bool AudioClip::load() {
    // TODO: Implement FFmpeg audio loading
    // For now, just probe the file
    MediaInfo info = FFmpegUtils::probeMedia(m_sourcePath);
    if (!info.isValid()) {
        emit error("Failed to load audio file");
        return false;
    }
    
    m_sampleRate = info.audioSampleRate;
    m_channels = info.audioChannels;
    m_duration = info.duration;
    m_totalSamples = static_cast<int64_t>(m_duration * m_sampleRate);
    m_loaded = true;
    
    emit loaded();
    return true;
}

void AudioClip::unload() {
    m_buffer.clear();
    m_loaded = false;
}

bool AudioClip::readSamples(int64_t startSample, int count, float *output) {
    Q_UNUSED(startSample)
    Q_UNUSED(count)
    Q_UNUSED(output)
    // TODO: Implement sample reading from FFmpeg
    return false;
}

AudioBuffer AudioClip::readBuffer(int64_t startSample, int count) {
    AudioBuffer buffer(count, m_channels);
    readSamples(startSample, count, buffer.data());
    return buffer;
}

QVector<float> AudioClip::getWaveformData(int64_t startSample, int samples, int pixels) {
    Q_UNUSED(startSample)
    Q_UNUSED(samples)
    
    // Return placeholder waveform
    QVector<float> data(pixels);
    for (int i = 0; i < pixels; i++) {
        data[i] = static_cast<float>(std::sin(i * 0.1) * 0.5 + 0.5);
    }
    return data;
}

float AudioClip::getPeakInRange(int64_t startSample, int count, int channel) {
    Q_UNUSED(startSample)
    Q_UNUSED(count)
    Q_UNUSED(channel)
    return 0.0f;
}

} // namespace Aegis
