// audio_daw.h - Digital Audio Workstation Engine with Integrated Notation
// Part of Aegis Multimedia Suite - Pillar 1 (Audio)
// Unified architecture: Audio, MIDI, and Notation clips coexist in same timeline

#pragma once

#include "audio.h"
#include "audio_effects.h"
#include "audio_output.h"
#include "music_notation.h"  // New: notation data model
#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QMutex>
#include <QReadWriteLock>
#include <QUndoStack>
#include <memory>
#include <functional>
#include <atomic>
#include <optional>

// Forward declarations
class QTimer;

namespace Aegis {

    // =============================================================================
    // Tempo and Time Signature Map (Unified for all clip types)
    // =============================================================================

    struct TempoMap {
        struct TempoChange {
            double beatPosition;  // In quarter notes from start
            double bpm;
            bool rampToNext = false;  // Gradual tempo change
        };

        struct TimeSignatureChange {
            double beatPosition;
            int numerator = 4;
            int denominator = 4;
        };

        QVector<TempoChange> tempoChanges;
        QVector<TimeSignatureChange> timeSigChanges;

        // Default tempo if no changes
        double defaultBpm = 120.0;

        // Time conversions
        double beatsToSeconds(double beats) const;
        double secondsToBeats(double seconds) const;
        double bpmAtBeat(double beat) const;
        double bpmAtTime(double seconds) const;

        // Beat/bar conversion
        struct BarBeat {
            int bar;
            int beat;
            double fractionalBeat;
        };
        BarBeat beatsToBarBeat(double beats) const;
        double barBeatToBeats(int bar, int beat, double fraction = 0.0) const;

        void addTempoChange(double beat, double bpm, bool ramp = false);
        void addTimeSignature(double beat, int num, int den);

        void clear() {
            tempoChanges.clear();
            timeSigChanges.clear();
            defaultBpm = 120.0;
        }
    };

    // =============================================================================
    // Transport Control (Unified for Audio/MIDI/Notation)
    // =============================================================================

    enum class TransportState {
        Stopped,
        Playing,
        Paused,
        Recording
    };

    class DAWTransport : public QObject {
        Q_OBJECT
        Q_PROPERTY(TransportState state READ state NOTIFY stateChanged)
        Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
        Q_PROPERTY(double tempo READ tempo WRITE setTempo NOTIFY tempoChanged)
        Q_PROPERTY(bool looping READ isLooping WRITE setLooping NOTIFY loopingChanged)
        Q_PROPERTY(double loopStart READ loopStart WRITE setLoopStart NOTIFY loopChanged)
        Q_PROPERTY(double loopEnd READ loopEnd WRITE setLoopEnd NOTIFY loopChanged)

    public:
        explicit DAWTransport(QObject* parent = nullptr);

        // State control
        void play();
        void stop();
        void pause();
        void togglePlay();
        void record();

        TransportState state() const { return m_state; }

        // Position (in seconds, but snaps to beat grid)
        double position() const;
        void setPosition(double seconds);
        void seekToBeat(double beat);
        void seekToBarBeat(int bar, int beat, double fraction = 0.0);

        // Tempo
        double tempo() const;
        void setTempo(double bpm);
        TempoMap* tempoMap() { return &m_tempoMap; }
        const TempoMap* tempoMap() const { return &m_tempoMap; }

        // Looping
        bool isLooping() const { return m_looping; }
        void setLooping(bool loop) { m_looping = loop; emit loopingChanged(); }
        double loopStart() const { return m_loopStart; }
        double loopEnd() const { return m_loopEnd; }
        void setLoopStart(double seconds);
        void setLoopEnd(double seconds);
        void setLoopRange(double start, double end);

        // Time conversion utilities
        double beatToTime(double beat) const;
        double timeToBeat(double time) const;
        double snapToGrid(double time, int subdivisions = 16) const;

        // Metronome
        bool metronomeEnabled() const { return m_metronomeEnabled; }
        void setMetronomeEnabled(bool enabled) { m_metronomeEnabled = enabled; }
        double metronomeVolume() const { return m_metronomeVolume; }
        void setMetronomeVolume(double vol) { m_metronomeVolume = vol; }
        void click();  // Trigger metronome click

        // Punch-in/out for recording
        void setPunchIn(double time) { m_punchIn = time; }
        void setPunchOut(double time) { m_punchOut = time; }

    signals:
        void stateChanged(TransportState state);
        void positionChanged(double position);
        void tempoChanged(double tempo);
        void loopingChanged();
        void loopChanged();
        void measurePassed(int measure);
        void beatPassed(int beat);
        void aboutToLoop();

    private:
        TransportState m_state = TransportState::Stopped;
        std::atomic<double> m_position{0.0};
        double m_tempo = 120.0;
        TempoMap m_tempoMap;

        bool m_looping = false;
        double m_loopStart = 0.0;
        double m_loopEnd = -1.0;  // -1 = no loop

        bool m_metronomeEnabled = false;
        double m_metronomeVolume = 0.5;
        double m_lastClickTime = -1.0;

        double m_punchIn = -1.0;
        double m_punchOut = -1.0;

        QMutex m_positionMutex;
    };

    // =============================================================================
    // Clip Types - Unified base class
    // =============================================================================

    enum class ClipType {
        Audio,      // Waveform data
        MIDI,       // MIDI events
        Notation,   // Structured notation (Score-based)
        Automation  // Parameter automation
    };

    class Clip : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
        Q_PROPERTY(ClipType type READ type CONSTANT)
        Q_PROPERTY(double startTime READ startTime WRITE setStartTime NOTIFY positionChanged)
        Q_PROPERTY(double duration READ duration WRITE setDuration NOTIFY durationChanged)
        Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY stateChanged)
        Q_PROPERTY(bool soloed READ isSoloed WRITE setSoloed NOTIFY stateChanged)
        Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY appearanceChanged)

    public:
        Clip(ClipType type, QObject* parent = nullptr);
        virtual ~Clip() = default;

        // Basic properties
        ClipType type() const { return m_type; }

        QString name() const { return m_name; }
        void setName(const QString& name) { m_name = name; emit nameChanged(); }

        double startTime() const { return m_startTime; }
        void setStartTime(double time) { m_startTime = time; emit positionChanged(); }

        double duration() const { return m_duration; }
        void setDuration(double dur) { m_duration = dur; emit durationChanged(); }

        double endTime() const { return m_startTime + m_duration; }

        bool isMuted() const { return m_muted; }
        void setMuted(bool mute) { m_muted = mute; emit stateChanged(); }

        bool isSoloed() const { return m_soloed; }
        void setSoloed(bool solo) { m_soloed = solo; emit stateChanged(); }

        QColor color() const { return m_color; }
        void setColor(const QColor& c) { m_color = c; emit appearanceChanged(); }

        // Fade in/out
        double fadeIn() const { return m_fadeIn; }
        double fadeOut() const { return m_fadeOut; }
        void setFadeIn(double seconds);
        void setFadeOut(double seconds);

        // Stretch/pitch shift
        double timeStretch() const { return m_timeStretch; }
        double pitchShift() const { return m_pitchShift; }
        void setTimeStretch(double ratio);
        void setPitchShift(double semitones);

        // Virtual methods for subclasses
        virtual bool isEmpty() const = 0;
        virtual void trimStart(double newStart);
        virtual void trimEnd(double newEnd);
        virtual void split(double time, Clip** outLeft = nullptr, Clip** outRight = nullptr);
        virtual Clip* duplicate() const = 0;

        // Playback preparation
        virtual void preparePlayback(double startTime, double duration) {}
        virtual void cleanupPlayback() {}

        // Snap to grid
        void snapToGrid(const TempoMap& tempoMap, int subdivisions = 16);

    signals:
        void nameChanged();
        void positionChanged();
        void durationChanged();
        void stateChanged();
        void appearanceChanged();
        void modified();

    protected:
        ClipType m_type;
        QString m_name;
        double m_startTime = 0.0;
        double m_duration = 0.0;
        bool m_muted = false;
        bool m_soloed = false;
        QColor m_color = QColor(100, 150, 200);

        double m_fadeIn = 0.0;
        double m_fadeOut = 0.0;
        double m_timeStretch = 1.0;
        double m_pitchShift = 0.0;

        mutable QReadWriteLock m_lock;
    };

    // =============================================================================
    // Audio Clip (Existing)
    // =============================================================================

    class AudioClip : public Clip {
        Q_OBJECT
    public:
        explicit AudioClip(QObject* parent = nullptr);

        // File I/O
        bool loadFromFile(const QString& path);
        bool saveToFile(const QString& path) const;

        // Audio data access
        bool isEmpty() const override { return m_audioData.isEmpty(); }
        const QVector<float>& audioData() const { return m_audioData; }
        QVector<float>& audioData() { return m_audioData; }
        void setAudioData(QVector<float>&& data, int sampleRate);

        int sampleRate() const { return m_sampleRate; }
        int channels() const { return m_channels; }

        // Peak data for display
        QVector<float> peakData(double pixelsPerSecond) const;

        // Waveform editing
        void reverse();
        void normalize(double targetDb = -1.0);
        void applyGain(double db);
        void fade(double startDb, double endDb, double startTime, double endTime);

        Clip* duplicate() const override;

        // Real-time access for playback
        void getSamples(double clipTime, int frames, float* output, int channels);

    private:
        QVector<float> m_audioData;
        int m_sampleRate = 48000;
        int m_channels = 2;
        mutable QVector<float> m_peakCache;
        mutable double m_peakCacheZoom = 0.0;
    };

    // =============================================================================
    // MIDI Clip (Existing)
    // =============================================================================

    struct MidiEvent {
        enum Type { NoteOn, NoteOff, ControlChange, PitchBend, ProgramChange, PolyPressure };
        Type type;
        double time;        // Seconds from clip start
        int channel = 0;
        int note = 0;       // For note events
        int velocity = 0;   // For note events
        int controller = 0; // For CC
        int value = 0;      // For CC/velocity
    };

    class MidiClip : public Clip {
        Q_OBJECT
    public:
        explicit MidiClip(QObject* parent = nullptr);

        bool isEmpty() const override { return m_events.isEmpty(); }

        // MIDI data
        void addEvent(const MidiEvent& event);
        void removeEvent(int index);
        void clearEvents() { m_events.clear(); }
        const QVector<MidiEvent>& events() const { return m_events; }
        QVector<MidiEvent> eventsInRange(double start, double end) const;

        // Note editing
        void addNote(int note, double start, double duration, int velocity = 80, int channel = 0);
        void removeNote(int note, double start);
        void transpose(int semitones);
        void quantize(double grid, double strength = 1.0);

        Clip* duplicate() const override;

        // Real-time access
        QVector<MidiEvent> getEventsForTimeRange(double clipStart, double clipEnd) const;

    private:
        QVector<MidiEvent> m_events;
    };

    // =============================================================================
    // Notation Clip - NEW: Integrates music notation into DAW
    // =============================================================================

    enum class NotationDisplayMode {
        Score,      // Traditional notation
        PianoRoll,  // MIDI-like view
        Both        // Split view
    };

    class NotationClip : public Clip {
        Q_OBJECT
    public:
        explicit NotationClip(QObject* parent = nullptr);

        // Core notation data (reuses music_notation.h types)
        Score* score() const { return m_score.get(); }
        void setScore(std::unique_ptr<Score> score);
        void createEmptyScore(const QString& title = "Untitled");

        // Staff-to-track mapping
        void setStaffIndex(int index) { m_staffIndex = index; }
        int staffIndex() const { return m_staffIndex; }

        // Time mapping (notation uses beats/ticks, clips use seconds)
        double ticksPerQuarter() const { return m_score ? m_score->ticksPerQuarter() : 480.0; }
        double beatToTime(double beat) const;
        double timeToBeat(double time) const;
        int timeToTick(double time) const;
        double tickToTime(int tick) const;

        // Rendering options
        NotationDisplayMode displayMode() const { return m_displayMode; }
        void setDisplayMode(NotationDisplayMode mode) { m_displayMode = mode; }

        // Synthesis settings
        void setSynthesisEnabled(bool enabled) { m_synthesisEnabled = enabled; }
        bool synthesisEnabled() const { return m_synthesisEnabled; }
        void setInstrumentId(int id) { m_instrumentId = id; }
        int instrumentId() const { return m_instrumentId; }

        // Conversion
        void toMidiClip(MidiClip* midiClip) const;  // Export to MIDI
        void fromMidiClip(const MidiClip* midiClip); // Import from MIDI

        // Clip interface
        bool isEmpty() const override;
        Clip* duplicate() const override;
        void preparePlayback(double startTime, double duration) override;
        void cleanupPlayback() override;

        // Real-time rendering
        void renderAudio(double clipTime, int frames, float* output, int channels, int sampleRate);
        QVector<MidiEvent> renderMidi(double clipTime, double duration) const;

        // Score access at specific time
        Measure* measureAtTime(double clipTime) const;
        QVector<Note*> notesAtTime(double clipTime) const;
        Note* noteAtPosition(double clipTime, int voice = 0) const;

    signals:
        void scoreModified();
        void renderingModeChanged();

    private:
        std::unique_ptr<Score> m_score;
        int m_staffIndex = 0;  // Which staff in the score this clip represents

        NotationDisplayMode m_displayMode = NotationDisplayMode::Score;
        bool m_synthesisEnabled = true;
        int m_instrumentId = 0;  // For synthesis

        // Real-time synthesis state
        struct SynthesisState {
            double phase = 0.0;
            QVector<SynthVoice> activeVoices;
            double lastRenderTime = -1.0;
        };
        std::unique_ptr<SynthesisState> m_synthState;

        // Simple synthesis for playback (placeholder for proper sampler)
        struct SimpleVoice {
            int midiNote;
            double startTime;
            double duration;
            double phase = 0.0;
            double velocity = 0.8;
            bool active = false;
        };
        QVector<SimpleVoice> m_voices;

        void initializeSynthesis();
        double noteFrequency(int midiNote) const;
    };

    // =============================================================================
    // Automation Clip (Existing)
    // =============================================================================

    class AutomationClip : public Clip {
        Q_OBJECT
    public:
        explicit AutomationClip(QObject* parent = nullptr);

        bool isEmpty() const override { return m_points.isEmpty(); }

        // Control points
        struct Point {
            double time;
            double value;
            enum Type { Linear, Smooth, Step } type = Linear;
        };

        void addPoint(const Point& point);
        void removePoint(int index);
        void clearPoints() { m_points.clear(); }
        const QVector<Point>& points() const { return m_points; }

        // Evaluation
        double valueAt(double time) const;
        void setTargetParameter(QObject* object, const QString& propertyName);

        Clip* duplicate() const override;

    private:
        QVector<Point> m_points;
        QObject* m_targetObject = nullptr;
        QString m_targetProperty;
    };

    // =============================================================================
    // Track - Can contain any clip type
    // =============================================================================

    class Track : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
        Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY stateChanged)
        Q_PROPERTY(bool soloed READ isSoloed WRITE setSoloed NOTIFY stateChanged)
        Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
        Q_PROPERTY(double pan READ pan WRITE setPan NOTIFY panChanged)
        Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY appearanceChanged)

    public:
        explicit Track(const QString& name, QObject* parent = nullptr);
        virtual ~Track();

        // Properties
        QString name() const { return m_name; }
        void setName(const QString& name) { m_name = name; emit nameChanged(); }

        bool isMuted() const { return m_muted; }
        void setMuted(bool mute) { m_muted = mute; emit stateChanged(); }

        bool isSoloed() const { return m_soloed; }
        void setSoloed(bool solo) { m_soloed = solo; emit stateChanged(); }

        double volume() const { return m_volume; }
        void setVolume(double vol) { m_volume = vol; emit volumeChanged(); }

        double pan() const { return m_pan; }
        void setPan(double pan) { m_pan = qBound(-1.0, pan, 1.0); emit panChanged(); }

        QColor color() const { return m_color; }
        void setColor(const QColor& c) { m_color = c; emit appearanceChanged(); }

        // Clip management
        void addClip(std::unique_ptr<Clip> clip);
        void removeClip(Clip* clip);
        void removeClipAt(int index);
        Clip* clipAt(int index) const;
        int clipCount() const { return m_clips.size(); }
        const QVector<std::unique_ptr<Clip>>& clips() const { return m_clips; }

        // Find clips at time
        QVector<Clip*> clipsAt(double time) const;
        Clip* clipAt(double time, ClipType type = static_cast<ClipType>(-1)) const;

        // Specific clip types
        QVector<AudioClip*> audioClips() const;
        QVector<MidiClip*> midiClips() const;
        QVector<NotationClip*> notationClips() const;

        // Effects chain (Pillar 2)
        EffectChain* effectChain() { return &m_effects; }
        const EffectChain* effectChain() const { return &m_effects; }

        // MIDI/Notation specific
        int midiChannel() const { return m_midiChannel; }
        void setMidiChannel(int ch) { m_midiChannel = ch; }
        int midiProgram() const { return m_midiProgram; }
        void setMidiProgram(int prog) { m_midiProgram = prog; }

        // Audio I/O
        void setInputBus(const QString& bus) { m_inputBus = bus; }
        QString inputBus() const { return m_inputBus; }
        void setOutputBus(const QString& bus) { m_outputBus = bus; }
        QString outputBus() const { return m_outputBus; }

        // Real-time processing
        void processAudio(double position, int frames, float* buffer, int channels, int sampleRate);
        void processMidi(double position, double duration, QVector<MidiEvent>& events);

    signals:
        void nameChanged();
        void stateChanged();
        void volumeChanged();
        void panChanged();
        void appearanceChanged();
        void clipAdded(Clip* clip);
        void clipRemoved(Clip* clip);
        void clipChanged(Clip* clip);

    private:
        QString m_name;
        bool m_muted = false;
        bool m_soloed = false;
        double m_volume = 1.0;
        double m_pan = 0.0;
        QColor m_color;

        QVector<std::unique_ptr<Clip>> m_clips;
        EffectChain m_effects;

        // MIDI/Notation settings
        int m_midiChannel = 0;
        int m_midiProgram = 0;

        // Audio routing
        QString m_inputBus;
        QString m_outputBus = "master";

        mutable QReadWriteLock m_lock;
    };

    // =============================================================================
    // Master Track and Mix Buses
    // =============================================================================

    class MasterTrack : public Track {
        Q_OBJECT
    public:
        explicit MasterTrack(QObject* parent = nullptr);

        // Master-specific processing
        void processMaster(double position, int frames,
                           const QMap<QString, float*>& busBuffers,
                           float* output, int channels, int sampleRate);

        // Metering
        float currentLoudness() const { return m_currentLoudness; }
        float peakLevel() const { return m_peakLevel; }

    private:
        float m_currentLoudness = 0.0f;
        float m_peakLevel = 0.0f;
    };

    // =============================================================================
    // DAW Engine - Main controller
    // =============================================================================

    class DAWEngine : public QObject {
        Q_OBJECT
    public:
        explicit DAWEngine(AudioEngine* audioEngine, QObject* parent = nullptr);
        ~DAWEngine();

        // Audio system integration
        AudioEngine* audioEngine() const { return m_audioEngine; }
        AudioOutput* audioOutput() const { return m_audioOutput; }

        // Transport
        DAWTransport* transport() { return &m_transport; }

        // Tracks
        Track* addTrack(const QString& name = QString());
        void removeTrack(int index);
        void removeTrack(Track* track);
        Track* trackAt(int index) const;
        int trackCount() const { return m_tracks.size(); }
        const QVector<std::unique_ptr<Track>>& tracks() const { return m_tracks; }

        Track* masterTrack() const { return m_masterTrack.get(); }

        // Buses
        void createBus(const QString& name);
        void removeBus(const QString& name);
        QStringList busNames() const;

        // Notation-specific methods
        NotationClip* importScore(const QString& path, int trackIndex = -1);
        NotationClip* createNotationTrack(const QString& name = "Notation");
        void notationToMidi(NotationClip* notation, MidiClip* midi);
        void midiToNotation(MidiClip* midi, NotationClip* notation);

        // Playback control
        void startPlayback();
        void stopPlayback();
        void startRecording();

        // Export
        bool exportMix(const QString& path, const QString& format = "WAV");
        bool exportStem(const QString& trackName, const QString& path);
        bool exportNotationPDF(const QString& path);

        // State
        bool isModified() const { return m_modified; }
        void setModified(bool mod) { m_modified = mod; }

        // Serialization
        bool saveProject(const QString& path);
        bool loadProject(const QString& path);

    signals:
        void trackAdded(Track* track);
        void trackRemoved(int index);
        void playbackStarted();
        void playbackStopped();
        void recordingStarted();
        void recordingStopped();
        void modifiedChanged(bool modified);
        void audioCallbackError(const QString& error);

    private slots:
        void onTransportPositionChanged(double pos);
        void onTransportStateChanged(TransportState state);
        void processAudioCallback(float* buffer, int frames);

    private:
        void initializeAudio();
        void shutdownAudio();
        void processTrack(Track* track, double position, int frames,
                          float* buffer, int channels, int sampleRate);
        void applyMasterEffects(float* buffer, int frames, int channels, int sampleRate);

        AudioEngine* m_audioEngine;
        AudioOutput* m_audioOutput = nullptr;
        DAWTransport m_transport;

        QVector<std::unique_ptr<Track>> m_tracks;
        std::unique_ptr<MasterTrack> m_masterTrack;
        QMap<QString, std::unique_ptr<Track>> m_buses;

        // Tempo map sync
        void syncTempoToNotation(NotationClip* clip);
        void syncNotationToTempo(NotationClip* clip);

        // Recording
        bool m_recording = false;
        QMap<Track*, std::unique_ptr<AudioClip>> m_recordingClips;

        bool m_modified = false;
        QMutex m_engineLock;

        // Thread-local mix buffer
        QVector<float> m_mixBuffer;
    };

    // =============================================================================
    // Undo Commands
    // =============================================================================

    class AddTrackCommand : public QUndoCommand {
    public:
        AddTrackCommand(DAWEngine* engine, const QString& name, QUndoCommand* parent = nullptr);
        void undo() override;
        void redo() override;
    private:
        DAWEngine* m_engine;
        QString m_name;
        Track* m_track = nullptr;
        int m_index = -1;
    };

    class RemoveTrackCommand : public QUndoCommand {
        // ...
    };

    class AddClipCommand : public QUndoCommand {
    public:
        AddClipCommand(Track* track, std::unique_ptr<Clip> clip, QUndoCommand* parent = nullptr);
        void undo() override;
        void redo() override;
    private:
        Track* m_track;
        std::unique_ptr<Clip> m_clip;
        int m_index = -1;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::TransportState)
Q_DECLARE_METATYPE(Aegis::ClipType)
Q_DECLARE_METATYPE(Aegis::NotationDisplayMode)
