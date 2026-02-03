// audio_daw.cpp - DAW Engine Implementation with Notation Integration
#include "audio_daw.h"
#include <algorithm>
#include <cmath>
#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDateTime>

namespace Aegis {

    // =============================================================================
    // TempoMap Implementation
    // =============================================================================

    double TempoMap::beatsToSeconds(double beats) const {
        if (tempoChanges.isEmpty()) {
            return beats * 60.0 / defaultBpm;
        }

        // Find applicable tempo section
        double seconds = 0.0;
        double lastBeat = 0.0;
        double lastBpm = defaultBpm;

        for (const auto& change : tempoChanges) {
            if (change.beatPosition >= beats) break;

            double beatDelta = change.beatPosition - lastBeat;
            seconds += beatDelta * 60.0 / lastBpm;

            lastBeat = change.beatPosition;
            lastBpm = change.bpm;
        }

        double remainingBeats = beats - lastBeat;
        seconds += remainingBeats * 60.0 / lastBpm;

        return seconds;
    }

    double TempoMap::secondsToBeats(double seconds) const {
        if (tempoChanges.isEmpty()) {
            return seconds * defaultBpm / 60.0;
        }

        // Reverse calculation
        double beats = 0.0;
        double lastTime = 0.0;
        double lastBpm = defaultBpm;

        for (const auto& change : tempoChanges) {
            double sectionTime = (change.beatPosition - beats) * 60.0 / lastBpm;
            if (lastTime + sectionTime >= seconds) {
                return beats + (seconds - lastTime) * lastBpm / 60.0;
            }

            lastTime += sectionTime;
            beats = change.beatPosition;
            lastBpm = change.bpm;
        }

        return beats + (seconds - lastTime) * lastBpm / 60.0;
    }

    double TempoMap::bpmAtBeat(double beat) const {
        double bpm = defaultBpm;
        for (const auto& change : tempoChanges) {
            if (change.beatPosition <= beat) {
                bpm = change.bpm;
            }
        }
        return bpm;
    }

    void TempoMap::addTempoChange(double beat, double bpm, bool ramp) {
        TempoChange change{beat, bpm, ramp};
        // Insert sorted
        auto it = std::upper_bound(tempoChanges.begin(), tempoChanges.end(), change,
                                   [](const TempoChange& a, const TempoChange& b) { return a.beatPosition < b.beatPosition; });
        tempoChanges.insert(it, change);
    }

    void TempoMap::addTimeSignature(double beat, int num, int den) {
        TimeSignatureChange ts{beat, num, den};
        auto it = std::upper_bound(timeSigChanges.begin(), timeSigChanges.end(), ts,
                                   [](const TimeSignatureChange& a, const TimeSignatureChange& b) { return a.beatPosition < b.beatPosition; });
        timeSigChanges.insert(it, ts);
    }

    // =============================================================================
    // DAWTransport Implementation
    // =============================================================================

    DAWTransport::DAWTransport(QObject* parent) : QObject(parent) {}

    double DAWTransport::position() const {
        QMutexLocker lock(&m_positionMutex);
        return m_position;
    }

    void DAWTransport::setPosition(double seconds) {
        {
            QMutexLocker lock(&m_positionMutex);
            m_position = seconds;
        }
        emit positionChanged(seconds);
    }

    void DAWTransport::seekToBeat(double beat) {
        setPosition(m_tempoMap.beatsToSeconds(beat));
    }

    void DAWTransport::play() {
        if (m_state == TransportState::Playing) return;
        m_state = TransportState::Playing;
        emit stateChanged(m_state);
    }

    void DAWTransport::stop() {
        if (m_state == TransportState::Stopped) return;
        m_state = TransportState::Stopped;
        setPosition(0.0);
        emit stateChanged(m_state);
    }

    void DAWTransport::pause() {
        if (m_state != TransportState::Playing) return;
        m_state = TransportState::Paused;
        emit stateChanged(m_state);
    }

    void DAWTransport::togglePlay() {
        if (m_state == TransportState::Playing) pause();
        else play();
    }

    void DAWTransport::setLoopStart(double seconds) {
        m_loopStart = seconds;
        emit loopChanged();
    }

    void DAWTransport::setLoopEnd(double seconds) {
        m_loopEnd = seconds;
        emit loopChanged();
    }

    void DAWTransport::click() {
        // Would trigger metronome sound
        m_lastClickTime = position();
    }

    double DAWTransport::beatToTime(double beat) const {
        return m_tempoMap.beatsToSeconds(beat);
    }

    double DAWTransport::timeToBeat(double time) const {
        return m_tempoMap.secondsToBeats(time);
    }

    // =============================================================================
    // Clip Base Implementation
    // =============================================================================

    Clip::Clip(ClipType type, QObject* parent)
    : QObject(parent), m_type(type) {}

    void Clip::trimStart(double newStart) {
        if (newStart > startTime() + duration()) return;
        double end = endTime();
        setStartTime(newStart);
        setDuration(end - newStart);
    }

    void Clip::trimEnd(double newEnd) {
        if (newEnd < startTime()) return;
        setDuration(newEnd - startTime());
    }

    void Clip::snapToGrid(const TempoMap& tempoMap, int subdivisions) {
        double beat = tempoMap.secondsToBeats(startTime());
        double gridBeats = 4.0 / subdivisions;  // Quarter note / subdivisions
        double snappedBeat = std::round(beat / gridBeats) * gridBeats;
        setStartTime(tempoMap.beatsToSeconds(snappedBeat));
    }

    // =============================================================================
    // AudioClip Implementation
    // =============================================================================

    AudioClip::AudioClip(QObject* parent) : Clip(ClipType::Audio, parent) {}

    bool AudioClip::loadFromFile(const QString& path) {
        // Would use SF_READ or similar
        // For now, placeholder
        m_name = QFileInfo(path).fileName();
        return true;
    }

    void AudioClip::getSamples(double clipTime, int frames, float* output, int channels) {
        QReadLocker lock(&m_lock);

        if (m_audioData.isEmpty()) {
            std::fill(output, output + frames * channels, 0.0f);
            return;
        }

        int startSample = static_cast<int>(clipTime * m_sampleRate) * m_channels;
        int available = (m_audioData.size() - startSample) / m_channels;
        int toCopy = std::min(frames, available);

        // Deinterleave and copy
        for (int i = 0; i < toCopy; ++i) {
            for (int ch = 0; ch < channels; ++ch) {
                int srcCh = std::min(ch, m_channels - 1);
                output[i * channels + ch] = m_audioData[(startSample + i) * m_channels + srcCh];
            }
        }

        // Zero pad if needed
        if (toCopy < frames) {
            std::fill(output + toCopy * channels, output + frames * channels, 0.0f);
        }
    }

    Clip* AudioClip::duplicate() const {
        auto* copy = new AudioClip(parent());
        copy->m_name = m_name;
        copy->m_startTime = m_startTime;
        copy->m_duration = m_duration;
        copy->m_audioData = m_audioData;
        copy->m_sampleRate = m_sampleRate;
        copy->m_channels = m_channels;
        return copy;
    }

    // =============================================================================
    // MidiClip Implementation
    // =============================================================================

    MidiClip::MidiClip(QObject* parent) : Clip(ClipType::MIDI, parent) {}

    void MidiClip::addEvent(const MidiEvent& event) {
        QWriteLocker lock(&m_lock);
        m_events.append(event);
        // Keep sorted by time
        std::sort(m_events.begin(), m_events.end(),
                  [](const MidiEvent& a, const MidiEvent& b) { return a.time < b.time; });
        emit modified();
    }

    void MidiClip::addNote(int note, double start, double duration, int velocity, int channel) {
        addEvent({MidiEvent::NoteOn, start, channel, note, velocity});
        addEvent({MidiEvent::NoteOff, start + duration, channel, note, 0});
    }

    QVector<MidiEvent> MidiClip::getEventsForTimeRange(double clipStart, double clipEnd) const {
        QReadLocker lock(&m_lock);
        QVector<MidiEvent> result;
        for (const auto& ev : m_events) {
            if (ev.time >= clipStart && ev.time < clipEnd) {
                result.append(ev);
            }
        }
        return result;
    }

    Clip* MidiClip::duplicate() const {
        auto* copy = new MidiClip(parent());
        copy->m_name = m_name;
        copy->m_startTime = m_startTime;
        copy->m_duration = m_duration;
        copy->m_events = m_events;
        return copy;
    }

    // =============================================================================
    // NotationClip Implementation - NEW
    // =============================================================================

    NotationClip::NotationClip(QObject* parent)
    : Clip(ClipType::Notation, parent) {}

    void NotationClip::setScore(std::unique_ptr<Score> score) {
        QWriteLocker lock(&m_lock);
        m_score = std::move(score);
        if (m_score) {
            // Calculate duration from score
            double totalBeats = m_score->totalTicks() / ticksPerQuarter();
            setDuration(totalBeats * 60.0 / 120.0);  // At 120 BPM initially
        }
        emit scoreModified();
        emit modified();
    }

    void NotationClip::createEmptyScore(const QString& title) {
        auto score = std::make_unique<Score>();
        score->setTitle(title);

        Staff* staff = score->addStaff("Staff 1");
        staff->addMeasure(1);

        setScore(std::move(score));
    }

    bool NotationClip::isEmpty() const {
        QReadLocker lock(&m_lock);
        return !m_score || m_score->staves.isEmpty();
    }

    double NotationClip::beatToTime(double beat) const {
        // Use tempo from score or default
        return beat * 60.0 / 120.0;
    }

    double NotationClip::timeToBeat(double time) const {
        return time * 120.0 / 60.0;
    }

    int NotationClip::timeToTick(double time) const {
        return static_cast<int>(timeToBeat(time) * ticksPerQuarter());
    }

    double NotationClip::tickToTime(int tick) const {
        return beatToTime(tick / ticksPerQuarter());
    }

    void NotationClip::toMidiClip(MidiClip* midiClip) const {
        QReadLocker lock(&m_lock);
        if (!m_score) return;

        midiClip->clearEvents();

        // Convert notation notes to MIDI events
        Staff* staff = m_score->staves.value(m_staffIndex).get();
        if (!staff) return;

        for (const auto& measure : staff->measures) {
            double measureStartBeat = measure->startTick() / ticksPerQuarter();

            for (const auto& note : measure->notes) {
                if (note.isRest) continue;

                double startBeat = measureStartBeat + (note.tickPosition / ticksPerQuarter());
                double durBeats = note.duration.toQuarterNotes();

                midiClip->addNote(note.pitch.midiNote,
                                  beatToTime(startBeat),
                                  beatToTime(durBeats),
                                  note.velocity,
                                  note.voice);
            }
        }
    }

    void NotationClip::fromMidiClip(const MidiClip* midiClip) {
        // Create score from MIDI data
        // This is complex - would need quantization and notation analysis
        createEmptyScore();

        for (const auto& ev : midiClip->events()) {
            if (ev.type == MidiEvent::NoteOn && ev.velocity > 0) {
                // Find matching note-off
                // Add to score
            }
        }
    }

    void NotationClip::preparePlayback(double startTime, double duration) {
        initializeSynthesis();
        m_synthState->lastRenderTime = -1.0;
    }

    void NotationClip::cleanupPlayback() {
        m_synthState.reset();
    }

    void NotationClip::initializeSynthesis() {
        if (!m_synthState) {
            m_synthState = std::make_unique<SynthesisState>();
            m_voices.clear();
        }
    }

    double NotationClip::noteFrequency(int midiNote) const {
        return 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
    }

    void NotationClip::renderAudio(double clipTime, int frames, float* output, int channels, int sampleRate) {
        QReadLocker lock(&m_lock);
        if (!m_score) {
            std::fill(output, output + frames * channels, 0.0f);
            return;
        }

        initializeSynthesis();

        double dt = 1.0 / sampleRate;
        double currentTime = clipTime;

        // Clear output
        std::fill(output, output + frames * channels, 0.0f);

        // Get notes active in this time range
        Staff* staff = m_score->staves.value(m_staffIndex).get();
        if (!staff) return;

        // Simple synthesis: sine waves for each active note
        // In production, this would use proper sampler or plugin instrument

        for (int i = 0; i < frames; ++i) {
            double sample = 0.0;
            int currentTick = timeToTick(currentTime);

            // Find notes at this time
            for (const auto& measure : staff->measures) {
                if (currentTick < measure->startTick() || currentTick >= measure->startTick() + measure->lengthTicks()) {
                    continue;
                }

                int localTick = currentTick - measure->startTick();
                for (const auto& note : measure->notes) {
                    if (note.isRest) continue;
                    if (note.tickPosition > localTick) continue;

                    int noteEndTick = note.tickPosition + static_cast<int>(note.duration.toQuarterNotes() * ticksPerQuarter());
                    if (localTick >= noteEndTick) continue;

                    // Note is active - synthesize
                    double freq = noteFrequency(note.pitch.midiNote);
                    double vel = note.velocity / 127.0;

                    // Simple envelope
                    double noteTime = (localTick - note.tickPosition) / ticksPerQuarter() * (60.0/120.0);
                    double envelope = 1.0;
                    if (noteTime < 0.01) envelope = noteTime / 0.01;  // Attack
                    else if (noteTime > note.duration.toQuarterNotes() * (60.0/120.0) - 0.1) {
                        envelope = std::max(0.0, (note.duration.toQuarterNotes() * (60.0/120.0) - noteTime) / 0.1);
                    }

                    m_synthState->phase += 2.0 * M_PI * freq * dt;
                    while (m_synthState->phase > 2.0 * M_PI) m_synthState->phase -= 2.0 * M_PI;

                    sample += std::sin(m_synthState->phase) * vel * envelope * 0.3;
                }
            }

            // Soft clip
            sample = std::tanh(sample);

            for (int ch = 0; ch < channels; ++ch) {
                output[i * channels + ch] += static_cast<float>(sample);
            }

            currentTime += dt;
        }
    }

    QVector<MidiEvent> NotationClip::renderMidi(double clipTime, double duration) const {
        QVector<MidiEvent> events;
        if (!m_score) return events;

        Staff* staff = m_score->staves.value(m_staffIndex).get();
        if (!staff) return events;

        int startTick = timeToTick(clipTime);
        int endTick = timeToTick(clipTime + duration);

        for (const auto& measure : staff->measures) {
            for (const auto& note : measure->notes) {
                if (note.isRest) continue;

                int noteStart = measure->startTick() + note.tickPosition;
                int noteEnd = noteStart + static_cast<int>(note.duration.toQuarterNotes() * ticksPerQuarter());

                if (noteStart >= startTick && noteStart < endTick) {
                    events.append({MidiEvent::NoteOn, tickToTime(noteStart), note.voice,
                        note.pitch.midiNote, note.velocity});
                }
                if (noteEnd >= startTick && noteEnd < endTick) {
                    events.append({MidiEvent::NoteOff, tickToTime(noteEnd), note.voice,
                        note.pitch.midiNote, 0});
                }
            }
        }

        return events;
    }

    Measure* NotationClip::measureAtTime(double clipTime) const {
        if (!m_score) return nullptr;
        int tick = timeToTick(clipTime);
        return m_score->measureAtTick(tick);
    }

    QVector<Note*> NotationClip::notesAtTime(double clipTime) const {
        QVector<Note*> result;
        Measure* m = measureAtTime(clipTime);
        if (!m) return result;

        int localTick = timeToTick(clipTime) - m->startTick();
        for (auto& note : m->notes) {
            int noteEnd = note.tickPosition + static_cast<int>(note.duration.toQuarterNotes() * ticksPerQuarter());
            if (localTick >= note.tickPosition && localTick < noteEnd) {
                result.append(&note);
            }
        }
        return result;
    }

    Clip* NotationClip::duplicate() const {
        auto* copy = new NotationClip(parent());
        copy->m_name = m_name;
        copy->m_startTime = m_startTime;
        copy->m_duration = m_duration;
        if (m_score) {
            // Deep copy score
            copy->m_score = std::make_unique<Score>();
            // Copy score data...
        }
        return copy;
    }

    // =============================================================================
    // Track Implementation
    // =============================================================================

    Track::Track(const QString& name, QObject* parent)
    : QObject(parent), m_name(name) {}

    Track::~Track() = default;

    void Track::addClip(std::unique_ptr<Clip> clip) {
        QWriteLocker lock(&m_lock);
        clip->setParent(this);
        m_clips.append(std::move(clip));
        emit clipAdded(m_clips.last().get());
    }

    void Track::removeClip(Clip* clip) {
        QWriteLocker lock(&m_lock);
        auto it = std::find_if(m_clips.begin(), m_clips.end(),
                               [clip](const std::unique_ptr<Clip>& ptr) { return ptr.get() == clip; });
        if (it != m_clips.end()) {
            int index = std::distance(m_clips.begin(), it);
            m_clips.erase(it);
            emit clipRemoved(clip);
        }
    }

    Clip* Track::clipAt(int index) const {
        QReadLocker lock(&m_lock);
        if (index < 0 || index >= m_clips.size()) return nullptr;
        return m_clips[index].get();
    }

    QVector<Clip*> Track::clipsAt(double time) const {
        QReadLocker lock(&m_lock);
        QVector<Clip*> result;
        for (const auto& clip : m_clips) {
            if (time >= clip->startTime() && time < clip->endTime()) {
                result.append(clip.get());
            }
        }
        return result;
    }

    QVector<NotationClip*> Track::notationClips() const {
        QReadLocker lock(&m_lock);
        QVector<NotationClip*> result;
        for (const auto& clip : m_clips) {
            if (clip->type() == ClipType::Notation) {
                result.append(static_cast<NotationClip*>(clip.get()));
            }
        }
        return result;
    }

    void Track::processAudio(double position, int frames, float* buffer, int channels, int sampleRate) {
        QReadLocker lock(&m_lock);
        if (m_muted) {
            std::fill(buffer, buffer + frames * channels, 0.0f);
            return;
        }

        // Mix all clips
        std::fill(buffer, buffer + frames * channels, 0.0f);

        for (const auto& clip : m_clips) {
            if (clip->isMuted()) continue;

            double clipTime = position - clip->startTime();
            if (clipTime < 0 || clipTime >= clip->duration()) continue;

            QVector<float> clipBuffer(frames * channels);

            switch (clip->type()) {
                case ClipType::Audio: {
                    auto* audio = static_cast<AudioClip*>(clip.get());
                    audio->getSamples(clipTime, frames, clipBuffer.data(), channels);
                    break;
                }
                case ClipType::Notation: {
                    auto* notation = static_cast<NotationClip*>(clip.get());
                    if (notation->synthesisEnabled()) {
                        notation->renderAudio(clipTime, frames, clipBuffer.data(), channels, sampleRate);
                    }
                    break;
                }
                case ClipType::MIDI: {
                    // MIDI clips don't produce audio directly - would need instrument
                    break;
                }
                default:
                    break;
            }

            // Mix with volume/pan
            for (int i = 0; i < frames * channels; ++i) {
                float panGain = (channels == 2) ? ((i % 2 == 0) ? (1.0f - m_pan) : (1.0f + m_pan)) * 0.5f : 1.0f;
                buffer[i] += clipBuffer[i] * m_volume * panGain;
            }
        }

        // Apply effects chain
        // m_effects.process(buffer, frames, sampleRate, channels);
    }

    void Track::processMidi(double position, double duration, QVector<MidiEvent>& events) {
        QReadLocker lock(&m_lock);
        if (m_muted) return;

        for (const auto& clip : m_clips) {
            if (clip->isMuted()) continue;

            double clipStart = position - clip->startTime();
            if (clipStart < 0 || clipStart >= clip->duration()) continue;

            if (clip->type() == ClipType::MIDI) {
                auto* midi = static_cast<MidiClip*>(clip.get());
                events.append(midi->getEventsForTimeRange(clipStart, clipStart + duration));
            } else if (clip->type() == ClipType::Notation) {
                auto* notation = static_cast<NotationClip*>(clip.get());
                events.append(notation->renderMidi(clipStart, duration));
            }
        }
    }

    // =============================================================================
    // DAWEngine Implementation
    // =============================================================================

    DAWEngine::DAWEngine(AudioEngine* audioEngine, QObject* parent)
    : QObject(parent)
    , m_audioEngine(audioEngine)
    , m_masterTrack(std::make_unique<MasterTrack>()) {

        initializeAudio();

        connect(&m_transport, &DAWTransport::positionChanged,
                this, &DAWEngine::onTransportPositionChanged);
        connect(&m_transport, &DAWTransport::stateChanged,
                this, &DAWEngine::onTransportStateChanged);
    }

    DAWEngine::~DAWEngine() {
        shutdownAudio();
    }

    void DAWEngine::initializeAudio() {
        if (!m_audioEngine) return;

        // Create default output
        m_audioOutput = new AudioOutput(this);
        m_audioOutput->setAudioCallback([this](float* buffer, int frames) {
            processAudioCallback(buffer, frames);
        });

        // Match engine settings
        if (m_audioEngine->m_trackerPlayback) {
            m_audioOutput->setSampleRate(m_audioEngine->m_trackerPlayback->sampleRate());
        }
    }

    void DAWEngine::shutdownAudio() {
        if (m_audioOutput) {
            m_audioOutput->stop();
            delete m_audioOutput;
            m_audioOutput = nullptr;
        }
    }

    Track* DAWEngine::addTrack(const QString& name) {
        auto track = std::make_unique<Track>(name.isEmpty() ? QString("Track %1").arg(m_tracks.size() + 1) : name, this);
        Track* ptr = track.get();
        m_tracks.append(std::move(track));
        emit trackAdded(ptr);
        setModified(true);
        return ptr;
    }

    void DAWEngine::removeTrack(int index) {
        if (index < 0 || index >= m_tracks.size()) return;
        m_tracks.removeAt(index);
        emit trackRemoved(index);
        setModified(true);
    }

    NotationClip* DAWEngine::importScore(const QString& path, int trackIndex) {
        Track* track = (trackIndex >= 0 && trackIndex < m_tracks.size())
        ? m_tracks[trackIndex].get()
        : addTrack("Notation");

        auto clip = std::make_unique<NotationClip>();
        NotationClip* ptr = clip.get();

        // Load score
        if (path.endsWith(".xml") || path.endsWith(".musicxml")) {
            auto score = std::make_unique<Score>();
            if (score->loadMusicXML(path)) {
                clip->setScore(std::move(score));
            }
        } else if (path.endsWith(".mid") || path.endsWith(".midi")) {
            auto score = std::make_unique<Score>();
            if (score->loadMIDI(path)) {
                clip->setScore(std::move(score));
            }
        }

        if (!clip->isEmpty()) {
            track->addClip(std::move(clip));

            // Sync tempo
            if (ptr->score() && !ptr->score()->staves.isEmpty()) {
                // Extract tempo from score if available
            }

            setModified(true);
            return ptr;
        }

        return nullptr;
    }

    NotationClip* DAWEngine::createNotationTrack(const QString& name) {
        Track* track = addTrack(name);
        auto clip = std::make_unique<NotationClip>();
        clip->createEmptyScore(name);
        NotationClip* ptr = clip.get();
        track->addClip(std::move(clip));
        setModified(true);
        return ptr;
    }

    void DAWEngine::notationToMidi(NotationClip* notation, MidiClip* midi) {
        if (!notation || !midi) return;
        notation->toMidiClip(midi);
        setModified(true);
    }

    void DAWEngine::midiToNotation(MidiClip* midi, NotationClip* notation) {
        if (!notation || !midi) return;
        notation->fromMidiClip(midi);
        setModified(true);
    }

    void DAWEngine::startPlayback() {
        if (!m_audioOutput) return;

        // Prepare all clips
        double pos = m_transport.position();
        for (const auto& track : m_tracks) {
            for (const auto& clip : track->clips()) {
                clip->preparePlayback(pos, 1.0);
            }
        }

        m_audioOutput->start();
        m_transport.play();
        emit playbackStarted();
    }

    void DAWEngine::stopPlayback() {
        m_transport.stop();

        if (m_audioOutput) {
            m_audioOutput->stop();
        }

        // Cleanup clips
        for (const auto& track : m_tracks) {
            for (const auto& clip : track->clips()) {
                clip->cleanupPlayback();
            }
        }

        emit playbackStopped();
    }

    void DAWEngine::processAudioCallback(float* buffer, int frames) {
        if (!buffer) return;

        double position = m_transport.position();
        TransportState state = m_transport.state();

        if (state != TransportState::Playing) {
            std::fill(buffer, buffer + frames * 2, 0.0f);
            return;
        }

        int sampleRate = m_audioOutput ? m_audioOutput->sampleRate() : 48000;
        int channels = 2;

        // Ensure mix buffer is large enough
        if (m_mixBuffer.size() < frames * channels) {
            m_mixBuffer.resize(frames * channels);
        }

        // Clear master output
        std::fill(buffer, buffer + frames * channels, 0.0f);

        // Process each track
        for (const auto& track : m_tracks) {
            if (track->isMuted()) continue;

            // Check if any solo is active (this track isn't soloed)
            bool anySolo = false;
            for (const auto& t : m_tracks) {
                if (t->isSoloed()) { anySolo = true; break; }
            }
            if (anySolo && !track->isSoloed()) continue;

            // Process track
            track->processAudio(position, frames, m_mixBuffer.data(), channels, sampleRate);

            // Apply track effects
            track->effectChain()->processBuffer(
                EnhancedAudioBuffer(m_mixBuffer.data(), frames, channels, sampleRate),
                                                sampleRate, EffectContext::Realtime
            );

            // Mix to master
            for (int i = 0; i < frames * channels; ++i) {
                buffer[i] += m_mixBuffer[i];
            }
        }

        // Process MIDI (for external instruments or internal synth)
        // This would go to a MIDI output or internal synth

        // Master processing
        m_masterTrack->processMaster(position, frames, QMap<QString, float*>(), buffer, channels, sampleRate);

        // Update transport position
        double duration = frames / static_cast<double>(sampleRate);
        double newPos = position + duration;

        // Handle looping
        if (m_transport.isLooping() && m_transport.loopEnd() > 0) {
            if (newPos >= m_transport.loopEnd()) {
                newPos = m_transport.loopStart();
                emit m_transport.aboutToLoop();
            }
        }

        // Check end of project
        if (newPos > 600.0) {  // 10 minutes max for safety
            stopPlayback();
            return;
        }

        m_transport.setPosition(newPos);
    }

    void DAWEngine::onTransportPositionChanged(double pos) {
        // Update UI or other components
    }

    void DAWEngine::onTransportStateChanged(TransportState state) {
        // Handle state changes
    }

    void DAWEngine::setModified(bool mod) {
        if (m_modified != mod) {
            m_modified = mod;
            emit modifiedChanged(mod);
        }
    }

    bool DAWEngine::exportMix(const QString& path, const QString& format) {
        // Offline render
        // Similar to processAudioCallback but writing to file instead of audio output
        return true;
    }

    bool DAWEngine::saveProject(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;

        QXmlStreamWriter xml(&file);
        xml.setAutoFormatting(true);
        xml.writeStartDocument();
        xml.writeStartElement("AegisProject");
        xml.writeAttribute("version", "1.0");

        // Transport/tempo
        xml.writeStartElement("Transport");
        xml.writeTextElement("Tempo", QString::number(m_transport.tempo()));
        xml.writeTextElement("Position", QString::number(m_transport.position()));
        xml.writeEndElement();

        // Tracks
        xml.writeStartElement("Tracks");
        for (const auto& track : m_tracks) {
            xml.writeStartElement("Track");
            xml.writeAttribute("name", track->name());
            xml.writeAttribute("muted", track->isMuted() ? "1" : "0");
            xml.writeAttribute("volume", QString::number(track->volume()));

            // Clips
            for (const auto& clip : track->clips()) {
                xml.writeStartElement("Clip");
                xml.writeAttribute("type", QString::number(static_cast<int>(clip->type())));
                xml.writeAttribute("name", clip->name());
                xml.writeAttribute("start", QString::number(clip->startTime()));
                xml.writeAttribute("duration", QString::number(clip->duration()));

                if (clip->type() == ClipType::Notation) {
                    auto* notation = static_cast<NotationClip*>(clip.get());
                    if (notation->score()) {
                        // Save score reference or embed
                        xml.writeTextElement("ScoreFile", notation->score()->title() + ".xml");
                    }
                }

                xml.writeEndElement();
            }

            xml.writeEndElement();
        }
        xml.writeEndElement();

        xml.writeEndElement();
        xml.writeEndDocument();

        setModified(false);
        return true;
    }

    bool DAWEngine::loadProject(const QString& path) {
        // Implementation...
        return true;
    }

} // namespace Aegis
