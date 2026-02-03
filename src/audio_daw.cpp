// audio_daw.cpp - DAW implementation
#include "audio_daw.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtConcurrent>
#include <sndfile.h>

namespace Aegis {

    // =============================================================================
    // DAWTransport Implementation
    // =============================================================================

    DAWTransport::DAWTransport(QObject* parent) : QObject(parent) {
        m_positionTimer.setInterval(50); // 20Hz update
        connect(&m_positionTimer, &QTimer::timeout, [this]() {
            if (m_playing.load()) {
                double advance = 0.05; // 50ms
                setPosition(m_position.load() + advance);
            }
        });
    }

    void DAWTransport::play() {
        if (m_playing.load()) return;
        m_playing = true;
        m_paused = false;
        m_positionTimer.start();
        emit playingChanged(true);
    }

    void DAWTransport::stop() {
        bool wasPlaying = m_playing.load();
        m_playing = false;
        m_recording = false;
        m_paused = false;
        m_positionTimer.stop();
        setPosition(0.0);
        if (wasPlaying) emit playingChanged(false);
        emit recordingChanged(false);
    }

    void DAWTransport::pause() {
        if (!m_playing.load()) return;
        m_paused = true;
        m_playing = false;
        m_positionTimer.stop();
        emit playingChanged(false);
    }

    void DAWTransport::record() {
        if (!m_playing.load()) {
            play();
        }
        m_recording = !m_recording.load();
        emit recordingChanged(m_recording.load());
    }

    void DAWTransport::togglePlay() {
        if (m_playing.load()) pause();
        else play();
    }

    void DAWTransport::toggleRecord() {
        record();
    }

    void DAWTransport::nudge(int beats) {
        double seconds = beatsToSeconds(beats);
        setPosition(m_position.load() + seconds);
    }

    void DAWTransport::setPosition(double seconds) {
        seconds = std::max(0.0, seconds);
        if (m_loopEnabled && seconds > m_loopEnd) {
            seconds = m_loopStart;
        }
        m_position = seconds;
        emit positionChanged(seconds);
    }

    void DAWTransport::setTempo(double bpm) {
        m_tempo = std::clamp(bpm, 20.0, 999.0);
        emit tempoChanged(m_tempo.load());
    }

    void DAWTransport::setTimeSignature(int num, int denom) {
        m_numerator = num;
        m_denominator = denom;
        emit timeSignatureChanged();
    }

    void DAWTransport::setLoopStart(double seconds) {
        m_loopStart = seconds;
        emit loopChanged();
    }

    void DAWTransport::setLoopEnd(double seconds) {
        m_loopEnd = seconds;
        emit loopChanged();
    }

    void DAWTransport::setLoopEnabled(bool enabled) {
        m_loopEnabled = enabled;
        emit loopChanged();
    }

    double DAWTransport::beatsToSeconds(double beats) const {
        return beats * 60.0 / m_tempo.load();
    }

    double DAWTransport::secondsToBeats(double seconds) const {
        return seconds * m_tempo.load() / 60.0;
    }

    QString DAWTransport::timeToString(double seconds) const {
        double beats = secondsToBeats(seconds);
        int bars = static_cast<int>(beats / m_numerator) + 1;
        int beat = static_cast<int>(beats) % m_numerator + 1;
        int ticks = static_cast<int>((beats - static_cast<int>(beats)) * 960);
        return QString("%1:%2:%3").arg(bars).arg(beat).arg(ticks, 3, 10, QChar('0'));
    }

    double DAWTransport::stringToTime(const QString& str) const {
        QStringList parts = str.split(':');
        if (parts.size() != 3) return 0.0;

        int bars = parts[0].toInt() - 1;
        int beat = parts[1].toInt() - 1;
        int ticks = parts[2].toInt();

        double beats = bars * m_numerator + beat + ticks / 960.0;
        return beatsToSeconds(beats);
    }

    // =============================================================================
    // DAWEngine Implementation
    // =============================================================================

    DAWEngine::DAWEngine(std::unique_ptr<AudioOutput> output, QObject* parent)
    : QObject(parent)
    , m_output(std::move(output))
    , m_engine(std::make_unique<AudioEngine>(this))
    , m_pluginHost(std::make_unique<PluginHost>(this))
    , m_masterChain(std::make_shared<EffectChain>())
    , m_transport(new DAWTransport(this))
    {
        // Initialize audio output
        if (!m_output) {
            OutputConfig config;
            config.sampleRate = 48000;
            config.channels = 2;
            config.bufferSize = 512;
            config.latencyTargetMs = 10;
            config.preferredBackend = OutputBackend::Auto;

            m_output = AudioOutputFactory::create(OutputBackend::Auto);
            if (m_output) {
                m_output->initialize(config);
            }
        }

        // Connect transport
        connect(m_transport, &DAWTransport::positionChanged,
                this, &DAWEngine::onTransportPositionChanged);
        connect(m_transport, &DAWTransport::playingChanged,
                this, &DAWEngine::onTransportPlayingChanged);

        // Setup audio callback
        if (m_output) {
            m_output->setAudioCallback([this](float* buffer, int frames) {
                processAudio(buffer, frames);
            });
        }

        // Metering timer
        m_meterTimer.setInterval(50);
        connect(&m_meterTimer, &QTimer::timeout, this, &DAWEngine::updateTrackMeters);

        // Add default master track
        auto master = std::make_shared<Track>();
        master->id = "master";
        master->name = "Master";
        master->type = TrackType::Master;
        master->effectChain = m_masterChain;
        m_tracks.insert("master", master);
        m_trackOrder.append("master");
    }

    DAWEngine::~DAWEngine() {
        if (m_output) {
            m_output->stop();
        }
    }

    void DAWEngine::newProject(const QString& name) {
        closeProject();
        m_projectPath.clear();

        // Clear tracks except master
        m_trackOrder.clear();
        m_tracks.clear();

        auto master = std::make_shared<Track>();
        master->id = "master";
        master->name = "Master";
        master->type = TrackType::Master;
        master->effectChain = m_masterChain;
        m_tracks.insert("master", master);
        m_trackOrder.append("master");

        m_transport->setPosition(0.0);
        m_duration = 300.0;
        setModified(false);
    }

    QString DAWEngine::addTrack(TrackType type, const QString& name) {
        QWriteLocker lock(&m_trackLock);

        auto track = std::make_shared<Track>();
        track->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        track->name = name.isEmpty() ? QString("Track %1").arg(m_tracks.size()) : name;
        track->type = type;
        track->effectChain = std::make_shared<EffectChain>();

        // Add default EQ to audio tracks
        if (type == TrackType::Audio || type == TrackType::Master) {
            auto eq = std::make_shared<EQEffect>();
            track->effectChain->addEffect(eq);
        }

        QString id = track->id;
        m_tracks.insert(id, track);
        m_trackOrder.insert(m_trackOrder.size() - 1, id); // Insert before master

        emit tracksChanged();
        setModified(true);
        return id;
    }

    void DAWEngine::removeTrack(const QString& trackId) {
        if (trackId == "master") return; // Can't remove master

        QWriteLocker lock(&m_trackLock);
        m_tracks.remove(trackId);
        m_trackOrder.removeOne(trackId);

        emit tracksChanged();
        setModified(true);
    }

    Track* DAWEngine::getTrack(const QString& trackId) {
        QReadLocker lock(&m_trackLock);
        auto it = m_tracks.find(trackId);
        if (it != m_tracks.end()) {
            return it.value().get();
        }
        return nullptr;
    }

    QString DAWEngine::addAudioClip(const QString& trackId,
                                    const QString& audioPath,
                                    double position) {
        Track* track = getTrack(trackId);
        if (!track || track->type != TrackType::Audio) return QString();

        AudioClip clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip.name = QFileInfo(audioPath).baseName();
        clip.sourcePath = audioPath;
        clip.startTime = position;
        clip.gain = 0.0f; // 0 dB

        // Load audio to get duration and cache waveform
        SF_INFO info;
        SNDFILE* file = sf_open(audioPath.toUtf8().constData(), SFM_READ, &info);
        if (file) {
            clip.duration = static_cast<double>(info.frames) / info.samplerate;

            // Generate waveform cache
            int cacheSize = static_cast<int>(clip.duration * clip.waveformResolution);
            clip.waveformCache.resize(cacheSize);

            // Simple peak detection per segment
            QVector<float> buffer(info.frames * info.channels);
            sf_readf_float(file, buffer.data(), info.frames);

            for (int i = 0; i < cacheSize; i++) {
                int startFrame = i * info.frames / cacheSize;
                int endFrame = (i + 1) * info.frames / cacheSize;
                float peak = 0.0f;
                for (int f = startFrame; f < endFrame && f < info.frames; f++) {
                    for (int ch = 0; ch < info.channels; ch++) {
                        float sample = std::abs(buffer[f * info.channels + ch]);
                        peak = std::max(peak, sample);
                    }
                }
                clip.waveformCache[i] = peak;
            }

            sf_close(file);
        }

        track->audioClips.append(clip);
        m_duration = std::max(m_duration, position + clip.duration);

        emit clipAdded(trackId, clip.id);
        emit durationChanged();
        setModified(true);
        return clip.id;
                                    }

                                    void DAWEngine::processAudio(float* outputBuffer, int frames) {
                                        if (!m_transport->playing() && !m_processState.rendering) {
                                            std::fill(outputBuffer, outputBuffer + frames * 2, 0.0f);
                                            return;
                                        }

                                        double currentTime = m_transport->position();
                                        int channels = 2;

                                        // Clear output
                                        std::fill(outputBuffer, outputBuffer + frames * channels, 0.0f);

                                        // Process each track
                                        QReadLocker lock(&m_trackLock);
                                        for (const QString& trackId : m_trackOrder) {
                                            auto it = m_tracks.find(trackId);
                                            if (it == m_tracks.end()) continue;

                                            Track* track = it.value().get();
                                            if (track->muted || track->type == TrackType::Master) continue;

                                            // Allocate track buffer
                                            QVector<float> trackBuffer(frames * channels, 0.0f);

                                            // Process track audio
                                            processTrack(track, currentTime, frames, trackBuffer.data());

                                            // Mix to master
                                            float volume = std::pow(10.0f, track->volume / 20.0f); // dB to linear
                                            for (int i = 0; i < frames * channels; i++) {
                                                outputBuffer[i] += trackBuffer[i] * volume;
                                            }
                                        }

                                        // Process master chain
                                        m_masterChain->processBlock(outputBuffer, frames,
                                                                    m_output->sampleRate(), channels);

                                        // Update transport position
                                        double advance = static_cast<double>(frames) / m_output->sampleRate();
                                        m_transport->setPosition(currentTime + advance);
                                    }

                                    void DAWEngine::processTrack(Track* track, double position, int frames, float* buffer) {
                                        // Process audio clips
                                        for (const AudioClip& clip : track->audioClips) {
                                            if (clip.muted) continue;
                                            double clipEnd = clip.startTime + clip.duration;
                                            if (position + static_cast<double>(frames) / m_output->sampleRate() < clip.startTime ||
                                                position > clipEnd) continue;

                                            processClip(clip, position, frames, buffer);
                                        }

                                        // Process plugins
                                        for (auto& plugin : track->plugins) {
                                            if (plugin.bypassed || !plugin.enabled) continue;

                                            // Process plugin
                                            QVector<float> temp(frames * track->channelCount);
                                            std::copy(buffer, buffer + frames * track->channelCount, temp.data());
                                            plugin.processFunc(temp.data(), buffer, frames, track->channelCount);

                                            // Apply dry/wet
                                            if (plugin.dryWet < 1.0f) {
                                                for (int i = 0; i < frames * track->channelCount; i++) {
                                                    buffer[i] = temp[i] * (1.0f - plugin.dryWet) + buffer[i] * plugin.dryWet;
                                                }
                                            }
                                        }

                                        // Apply track effects chain (Pillar 2)
                                        track->effectChain->processBlock(buffer, frames,
                                                                         m_output->sampleRate(),
                                                                         track->channelCount);

                                        // Apply pan
                                        if (track->channelCount == 2) {
                                            float pan = track->pan; // -1.0 (L) to 1.0 (R)
                                            float leftGain = pan <= 0.0f ? 1.0f : 1.0f - pan;
                                            float rightGain = pan >= 0.0f ? 1.0f : 1.0f + pan;

                                            for (int i = 0; i < frames; i++) {
                                                buffer[i * 2] *= leftGain;
                                                buffer[i * 2 + 1] *= rightGain;
                                            }
                                        }
                                    }

                                    void DAWEngine::processClip(const AudioClip& clip, double position, int frames, float* buffer) {
                                        // Calculate overlap
                                        double clipStart = clip.startTime;
                                        double clipEnd = clip.startTime + clip.duration;
                                        double bufferStart = position;
                                        double bufferEnd = position + static_cast<double>(frames) / m_output->sampleRate();

                                        if (bufferEnd <= clipStart || bufferStart >= clipEnd) return;

                                        // Determine read range
                                        double readStart = std::max(0.0, bufferStart - clipStart + clip.offset);
                                        double readDuration = std::min(bufferEnd, clipEnd) - std::max(bufferStart, clipStart);
                                        int readFrames = static_cast<int>(readDuration * m_output->sampleRate());
                                        int bufferOffset = static_cast<int>(std::max(0.0, clipStart - bufferStart) * m_output->sampleRate());

                                        if (readFrames <= 0 || bufferOffset >= frames) return;

                                        // Read audio file (simplified - should cache and resample)
                                        SF_INFO info;
                                        SNDFILE* file = sf_open(clip.sourcePath.toUtf8().constData(), SFM_READ, &info);
                                        if (!file) return;

                                        int startFrame = static_cast<int>(readStart * info.samplerate);
                                        sf_seek(file, startFrame, SEEK_SET);

                                        QVector<float> temp(readFrames * info.channels);
                                        int framesRead = sf_readf_float(file, temp.data(), readFrames);
                                        sf_close(file);

                                        // Apply fade
                                        float fadeInSamples = clip.fadeIn * info.samplerate;
                                        float fadeOutSamples = clip.fadeOut * info.samplerate;
                                        float clipGain = std::pow(10.0f, clip.gain / 20.0f);

                                        // Mix into buffer (with gain and fades)
                                        for (int i = 0; i < framesRead && (bufferOffset + i) < frames; i++) {
                                            float gain = clipGain;

                                            // Fade in
                                            if (i < fadeInSamples) {
                                                gain *= i / fadeInSamples;
                                            }
                                            // Fade out
                                            double clipPos = readStart + static_cast<double>(i) / info.samplerate;
                                            double timeToEnd = clip.duration - clipPos;
                                            if (timeToEnd < clip.fadeOut) {
                                                gain *= timeToEnd / clip.fadeOut;
                                            }

                                            // Mix (stereo for now)
                                            for (int ch = 0; ch < 2 && ch < info.channels; ch++) {
                                                int srcIdx = i * info.channels + ch;
                                                int dstIdx = (bufferOffset + i) * 2 + ch;
                                                buffer[dstIdx] += temp[srcIdx] * gain;
                                            }
                                        }
                                    }

                                    void DAWEngine::onTransportPlayingChanged(bool playing) {
                                        if (playing) {
                                            m_output->start();
                                            m_meterTimer.start();
                                        } else {
                                            m_output->stop();
                                            m_meterTimer.stop();
                                        }
                                    }

                                    void DAWEngine::updateTrackMeters() {
                                        QReadLocker lock(&m_trackLock);
                                        for (auto& [id, track] : m_tracks) {
                                            // Update peak/RMS from processing
                                            emit trackMeterUpdated(id, track->peakLevel, track->rmsLevel);
                                            track->peakLevel *= 0.9f; // Decay
                                        }
                                    }

                                    bool DAWEngine::exportProject(const QString& outputPath,
                                                                  const QString& format,
                                                                  double start,
                                                                  double end) {
                                        if (end < 0) end = m_duration;

                                        SF_INFO info;
                                        info.samplerate = m_output->sampleRate();
                                        info.channels = 2;
                                        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

                                        if (format == "FLAC") {
                                            info.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
                                        } else if (format == "OGG") {
                                            info.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
                                        }

                                        SNDFILE* file = sf_open(outputPath.toUtf8().constData(), SFM_WRITE, &info);
                                        if (!file) return false;

                                        // Render offline
                                        const int bufferFrames = 4096;
                                        QVector<float> buffer(bufferFrames * 2);

                                        m_processState.rendering = true;
                                        double originalPos = m_transport->position();

                                        for (double pos = start; pos < end; pos += static_cast<double>(bufferFrames) / info.samplerate) {
                                            m_transport->setPosition(pos);
                                            processAudio(buffer.data(), bufferFrames);
                                            sf_writef_float(file, buffer.data(), bufferFrames);

                                            int progress = static_cast<int>((pos - start) / (end - start) * 100);
                                            // emit renderProgress(progress);
                                        }

                                        sf_close(file);
                                        m_processState.rendering = false;
                                        m_transport->setPosition(originalPos);

                                        return true;
                                                                  }

                                                                  void DAWEngine::setModified(bool modified) {
                                                                      if (m_modified != modified) {
                                                                          m_modified = modified;
                                                                          emit modifiedChanged();
                                                                      }
                                                                  }

                                                                  // ... (other methods implementation)

} // namespace Aegis
