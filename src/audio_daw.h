// audio_daw.h - Digital Audio Workstation Engine with Integrated Notation

#pragma once

#include <QObject>
#include <QVector>
#include <QMap>
#include <QMutex>
#include <QReadWriteLock>
#include <QString>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QRectF>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <memory>
#include <atomic>
#include <cmath>
#include <functional>
#include <algorithm>
#include <vector>

// Forward declarations
class QFileInfo;
class AudioEngine;

namespace Aegis {

    // Forward declarations for tracker integration
    class ModTrackerClip;

    // =============================================================================
    // Core Types (Unified Across Audio/MIDI/Notation)
    // =============================================================================

    enum class ClipType { Audio, MIDI, Notation, Tracker, Automation };
    enum class TransportState { Stopped, Playing, Paused, Recording };

    // =============================================================================
    // Music Theory Types
    // =============================================================================

    enum class PitchClass {
        C = 0, CSharp, D, DSharp, E, F, FSharp, G, GSharp, A, ASharp, B
    };

    enum class Accidental {
        None, Natural, Sharp, Flat, DoubleSharp, DoubleFlat,
        QuarterToneSharp, QuarterToneFlat
    };

    enum class ClefType {
        Treble, Bass, Alto, Tenor, Soprano, MezzoSoprano,
        Baritone, BaritoneC, FrenchViolin, Percussion, Tab
    };

    enum class KeySignature {
        C_Major = 0, G_Major, D_Major, A_Major, E_Major, B_Major, FSharp_Major,
        CSharp_Major, F_Major, Bb_Major, Eb_Major, Ab_Major, Db_Major, Gb_Major, Cb_Major
    };

    enum class TimeSignatureType {
        Simple, Compound, Irrational, CommonTime, CutTime
    };

    enum class DurationType {
        Maxima = 0, Long, Breve, Whole, Half, Quarter, Eighth,
        Sixteenth, ThirtySecond, SixtyFourth, HundredTwentyEighth, TwoHundredFiftySixth,
        DoubleWhole = Breve
    };

    enum class NoteHeadType {
        Normal, Cross, Diamond, Triangle, Slash, XCircle, Do, Re, Mi,
        Fa, So, La, Ti, Rectangle, Oval
    };

    enum class Articulation {
        None, Staccato, Tenuto, Accent, Marcato, Staccatissimo,
        Fermata, FermataShort, FermataLong, BreathMark, Caesura
    };

    enum class Ornament {
        None, Trill, Turn, Mordent, InvertedMordent, Pralltriller,
        UpPrall, DownPrall, LinePrall, Shake, Schleifer
    };

    enum class TieType { None, Start, Stop, Continue };
    enum class SlurType { None, Start, Stop };

    enum class BarlineType {
        Single, Double, EndRepeat, BeginRepeat, EndBeginRepeat,
        Final, Dashed, Dotted, Tick, Short
    };

    enum class Waveform { Sine, Square, Saw, Triangle, Noise, Sample };

    // =============================================================================
    // Duration
    // =============================================================================

    struct Duration {
        DurationType type = DurationType::Quarter;
        int dots = 0;
        int numerator = 1;      // For tuplets
        int denominator = 1;    // For tuplets

        double toQuarterNotes() const;
        double toSeconds(double tempoBpm) const;
        QString toString() const;
        static Duration fromString(const QString& str);
        bool isTuplet() const { return numerator != 1 || denominator != 1; }
        bool operator==(const Duration& other) const;
    };

    // =============================================================================
    // Pitch
    // =============================================================================

    struct Pitch {
        int midiNote = 60;
        PitchClass pitchClass = PitchClass::C;
        int octave = 4;
        Accidental accidental = Accidental::Natural;
        bool showAccidental = false;

        Pitch() = default;
        explicit Pitch(int midi) : midiNote(midi) { fromMidi(midi); }
        Pitch(PitchClass pc, int oct, Accidental acc = Accidental::Natural)
        : pitchClass(pc), octave(oct), accidental(acc) { toMidi(); }

        void fromMidi(int midi);
        void toMidi();
        int compare(const Pitch& other) const;
        QString toString() const;
        static Pitch fromString(const QString& str);
        double frequency() const { return 440.0 * std::pow(2.0, (midiNote - 69) / 12.0); }

        bool operator==(const Pitch& other) const { return midiNote == other.midiNote; }
        bool operator<(const Pitch& other) const { return midiNote < other.midiNote; }
    };

    // =============================================================================
    // Time Signature
    // =============================================================================

    struct TimeSignature {
        int numerator = 4;
        int denominator = 4;
        TimeSignatureType type = TimeSignatureType::Simple;

        double beatsPerMeasure() const { return numerator; }
        double beatUnit() const { return 4.0 / denominator; }
        bool isCompound() const { return type == TimeSignatureType::Compound; }
        QString toString() const { return QString("%1/%2").arg(numerator).arg(denominator); }
    };

    // =============================================================================
    // Key Signature
    // =============================================================================

    struct KeySig {
        KeySignature key = KeySignature::C_Major;
        bool showNaturals = false;

        int accidentals() const;
        bool isSharpKey() const;
        bool isFlatKey() const;
        Accidental accidentalForPitch(PitchClass pc) const;
    };

    // =============================================================================
    // Clef
    // =============================================================================

    struct Clef {
        ClefType type = ClefType::Treble;
        int staffLine = 2;
        int octaveChange = 0;

        int pitchOffset() const;
        QString toString() const;
    };

    // =============================================================================
    // Barline
    // =============================================================================

    struct Barline {
        BarlineType type = BarlineType::Single;
        int repeatCount = 2;
        QString voltaText;
    };

    // =============================================================================
    // Lyric
    // =============================================================================

    struct Lyric {
        QString text;
        int verse = 0;
        enum Syllabic { Single, Begin, Middle, End } syllabic = Single;
        bool melisma = false;
    };

    // =============================================================================
    // Note
    // =============================================================================

    class Measure;

    struct Note {
        Pitch pitch;
        Duration duration;
        NoteHeadType headType = NoteHeadType::Normal;
        bool isRest = false;
        bool isChord = false;
        int voice = 0;
        int velocity = 80;
        bool stemUp = true;
        int staffLine = 0;
        TieType tie = TieType::None;
        QVector<Articulation> articulations;
        Ornament ornament = Ornament::None;
        QVector<SlurType> slurs;
        double playbackDurationMultiplier = 1.0;
        Measure* measure = nullptr;
        int tickPosition = 0;
        QVector<Pitch> chordPitches;
        bool isGraceNote = false;
        Duration graceDuration;
        QVector<Lyric> lyrics;

        Note() = default;
        explicit Note(const Pitch& p, const Duration& d) : pitch(p), duration(d) {}

        double playDurationTicks(double tempo) const;
        QString toLilyPond() const;
    };

    // =============================================================================
    // Synth Voice (Real-time Synthesis)
    // =============================================================================

    struct SynthVoice {
        int noteId = -1;
        Pitch pitch;
        double velocity = 0.8;
        double startTime = 0.0;
        double duration = 0.0;
        double attack = 0.01;
        double decay = 0.1;
        double sustain = 0.7;
        double releaseTime = 0.2;
        Waveform waveform = Waveform::Triangle;
        double phase = 0.0;
        double currentTime = 0.0;
        bool isActive = false;
        bool isReleasing = false;
        double releaseStartLevel = 0.0;
        double sampleRate = 48000.0;

        void start(const Pitch& p, double vel, double dur, double now);
        void release();
        double envelope() const;
        double nextSample();
    };

    // =============================================================================
    // Tempo Map
    // =============================================================================

    struct TempoChange {
        double beatPosition;
        double bpm;
        bool ramp = false;
    };

    class TempoMap {
    public:
        QVector<TempoChange> tempoChanges;
        double defaultBpm = 120.0;
        int ticksPerQuarter = 480;

        double beatsToSeconds(double beats) const;
        double secondsToBeats(double seconds) const;
        double bpmAtBeat(double beat) const;
        int timeToTick(double seconds) const;
        double tickToTime(int tick) const;

        void addTempoChange(double beat, double bpm, bool ramp = false);
        void clear();
    };

    // =============================================================================
    // Note Event (Shared Between MIDI and Notation)
    // =============================================================================

    struct NoteEvent {
        enum Type { NoteOn, NoteOff, ControlChange, PitchBend } type;
        double time;
        int channel = 0;
        int note = 0;
        int velocity = 0;
        Pitch pitch;
        Duration duration;
        bool fromNotation = false;
        int voice = 0;
    };

    // =============================================================================
    // Forward declarations for PIMPL classes
    // =============================================================================

    class Measure;
    class Staff;
    class Score;

    // =============================================================================
    // Measure
    // =============================================================================

    class Measure : public QObject {
        Q_OBJECT

        // Forward declaration of private implementation
        class Private;
        std::unique_ptr<Private> d;

    public:
        explicit Measure(int number, QObject* parent = nullptr);
        ~Measure() override;

        int measureNumber() const;
        int startTick() const;
        int lengthTicks() const;
        void setStartTick(int tick);
        void setLengthTicks(int ticks);

        const QVector<Note>& notes() const;
        QVector<Note>& notes();

        void addNote(const Note& note, int voice = 0);
        void removeNote(int index);
        Note* noteAtTick(int tick, int voice = 0);
        QVector<Note*> notesInRange(int startTick, int endTick, int voice = -1);

        void addTimeSignature(const TimeSignature& ts);
        const QVector<TimeSignature>& timeSignatures() const;

        int tickToPixel(int tick) const;
        int pixelToTick(double x) const;
        int filledTicks() const;
        int remainingTicks() const;
        bool isFull() const;
        double height() const { return 80.0; }
        double tempoAt(int tick) const;
        double absoluteTimeAt(int tick) const;

        double width = 100.0;
        double xPosition = 0.0;

    signals:
        void noteAdded(const Note& note);
        void noteRemoved(int index);
        void modified();

    private:
        void rebuildNoteCache() const;
    };

    // =============================================================================
    // Staff
    // =============================================================================

    class Staff : public QObject {
        Q_OBJECT

        // Forward declaration of private implementation
        class Private;
        std::unique_ptr<Private> d;

    public:
        explicit Staff(const QString& name, QObject* parent = nullptr);
        ~Staff() override;

        QString name() const;
        void setName(const QString& name);

        int lines() const;
        void setLines(int lines);

        Clef defaultClef() const;
        void setDefaultClef(const Clef& clef);

        int midiChannel() const;
        int midiProgram() const;
        void setMidiChannel(int ch);
        void setMidiProgram(int prog);

        int transposeChromatic() const;
        int transposeDiatonic() const;
        void setTranspose(int chromatic, int diatonic);

        // Usa std::vector per unique_ptr
        const std::vector<std::unique_ptr<Measure>>& measures() const;
        std::vector<std::unique_ptr<Measure>>& measures();

        Measure* addMeasure(int number);
        void removeMeasure(int index);
        Measure* measureAtTick(int tick);
        double height() const;

        double yPosition = 0.0;
        bool showBrace = false;
        bool showBracket = false;
        QVector<Staff*> bracketedWith;

    signals:
        void measureAdded(Measure* measure);
        void measureRemoved(int index);

    private:
        void rebuildCache() const;
    };

    // =============================================================================
    // Score
    // =============================================================================

    class Score : public QObject {
        Q_OBJECT

        // Forward declaration of private implementation
        class Private;
        std::unique_ptr<Private> d;

    public:
        explicit Score(QObject* parent = nullptr);
        ~Score() override;

        QString title() const;
        QString composer() const;
        QString lyricist() const;
        QString copyright() const;

        void setTitle(const QString& title);
        void setComposer(const QString& comp);
        void setLyricist(const QString& lyr);
        void setCopyright(const QString& copy);

        // Usa std::vector per unique_ptr
        const std::vector<std::unique_ptr<Staff>>& staves() const;
        std::vector<std::unique_ptr<Staff>>& staves();

        Staff* addStaff(const QString& name);
        void removeStaff(int index);
        Staff* staffAtY(double y) const;

        TimeSignature defaultTimeSignature() const;
        void setDefaultTimeSignature(const TimeSignature& ts);

        int ticksPerQuarter() const;
        void setTicksPerQuarter(int ticks);

        double tempo() const;
        void setTempo(double t);

        double pageWidth() const;
        double pageHeight() const;
        double staffDistance() const;
        void setPageSize(double w, double h);

        int totalTicks() const;
        Measure* measureAtTick(int tick);
        Note* noteAtTick(int tick, int staffIdx = 0, int voice = 0);

        // File I/O
        bool loadMusicXML(const QString& path);
        bool saveMusicXML(const QString& path) const;
        bool loadMIDI(const QString& path);
        bool saveMIDI(const QString& path) const;

        // Audio generation
        QByteArray renderToPCM(int sampleRate = 48000);

    signals:
        void modified();
        void metadataChanged();
        void structureChanged();
        void error(const QString& message);

    private:
        void rebuildCache() const;

        // MusicXML parsing helpers
        Note parseMusicXMLNote(QXmlStreamReader& xml);
        void parseMusicXMLPitch(QXmlStreamReader& xml, Pitch& pitch);
        void parseMusicXMLAttributes(QXmlStreamReader& xml, Measure* measure);
        void parseMusicXMLTime(QXmlStreamReader& xml, Measure* measure);
        void parseMusicXMLKey(QXmlStreamReader& xml, Measure* measure);
        void parseMusicXMLClef(QXmlStreamReader& xml, Measure* measure);
        void parseMusicXMLNotations(QXmlStreamReader& xml, Note& note);
        void parseMusicXMLArticulations(QXmlStreamReader& xml, Note& note);
        void parseMusicXMLOrnaments(QXmlStreamReader& xml, Note& note);
        void parseMusicXMLLyric(QXmlStreamReader& xml, Note& note);
        DurationType parseDurationType(const QString& str);
        Duration durationFromQuarters(double quarters);

        // MusicXML writing helpers
        void writeMusicXMLAttributes(QXmlStreamWriter& xml, const Measure* measure, bool isFirst) const;
        void writeMusicXMLNote(QXmlStreamWriter& xml, const Note& note, const Measure* measure) const;
        void writeMusicXMLBarline(QXmlStreamWriter& xml, const Barline& barline) const;
    };

    // =============================================================================
    // Engraving Settings
    // =============================================================================

    class EngravingSettings {
    public:
        double spatium = 20.0;
        double noteHeadWidth = 22.0;
        double stemWidth = 2.6;
        double stemLength = 70.0;
        double beamWidth = 10.0;
        double lineWidth = 1.0;
        double clefWidth = 50.0;
        double timeSigWidth = 40.0;
        double leftMargin = 10.0;
        double rightMargin = 10.0;

        QFont musicFont{"Bravura", 24};
        QFont textFont{"Times New Roman", 12};
        QFont lyricsFont{"Times New Roman", 11};

        QColor noteColor{Qt::black};
        QColor lineColor{Qt::black};
        QColor textColor{Qt::black};
        QColor selectionColor{100, 150, 255, 100};
        QColor playbackColor{255, 100, 100, 150};

        static EngravingSettings& defaults();
    };

    // =============================================================================
    // Score Renderer
    // =============================================================================

    class ScoreRenderer {
    public:
        explicit ScoreRenderer(Score* score = nullptr);

        void render(QPainter* painter, const QRectF& rect, int startStaff = 0, int staffCount = -1);
        void renderMeasure(QPainter* painter, Measure* measure, const QPointF& pos);
        void renderNote(QPainter* painter, const Note& note, const QPointF& pos);
        void renderRest(QPainter* painter, const Note& rest, const QPointF& pos);
        void renderClef(QPainter* painter, const Clef& clef, const QPointF& pos);
        void renderKeySig(QPainter* painter, const KeySig& key, const QPointF& pos);
        void renderTimeSig(QPainter* painter, const TimeSignature& ts, const QPointF& pos);
        void renderBarline(QPainter* painter, const Barline& barline, const QPointF& pos, double height);

        Note* noteAt(const QPointF& pos);
        Measure* measureAt(const QPointF& pos);
        Staff* staffAt(const QPointF& pos);

        void doLayout();
        void layoutMeasure(Measure* measure);
        double measureWidth(Measure* measure) const;

        // Public so ScoreView can use it for selection drawing
        double calculateNoteY(const Note& note) const;

    private:
        Score* m_score;
        EngravingSettings m_settings;
        static QMap<QString, uint> s_smuflCodes;

        void initializeSmuflCodes();
        void drawStaffLines(QPainter* painter, Staff* staff, const QRectF& rect);
        void drawStem(QPainter* painter, const Note& note, const QPointF& notePos);
        void drawFlag(QPainter* painter, const Note& note, const QPointF& stemEnd);
        void drawAccidental(QPainter* painter, Accidental acc, const QPointF& pos);
        void drawArticulation(QPainter* painter, Articulation art, const QPointF& notePos, bool above);
        void drawLyric(QPainter* painter, const Lyric& lyric, const QPointF& pos);
        double keySigWidth(const KeySig& key) const;
        int midiToStaffLine(int midiNote) const;
    };

    // =============================================================================
    // Clip Base
    // =============================================================================

    class Clip : public QObject {
        Q_OBJECT
    public:
        explicit Clip(ClipType type, QObject* parent = nullptr);
        virtual ~Clip() = default;

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

        QColor color() const { return m_color; }
        void setColor(const QColor& c) { m_color = c; emit appearanceChanged(); }

        virtual void preparePlayback(double startTime, double duration) { Q_UNUSED(startTime) Q_UNUSED(duration) }
        virtual void cleanupPlayback() {}
        virtual void processAudio(double position, int frames, float* buffer,
                                  int channels, int sampleRate, const TempoMap& tempo) = 0;
                                  virtual QVector<NoteEvent> getMidiEvents(double start, double end) = 0;

                                  virtual bool isEmpty() const { return false; }

                                  void snapToGrid(const TempoMap& tempoMap, int subdivisions = 16);

    signals:
        void nameChanged();
        void positionChanged();
        void durationChanged();
        void stateChanged();
        void appearanceChanged();
        void changed();

    protected:
        ClipType m_type;
        QString m_name;
        double m_startTime = 0.0;
        double m_duration = 0.0;
        bool m_muted = false;
        QColor m_color = QColor(100, 150, 200);
        mutable QReadWriteLock m_lock;
    };

    // =============================================================================
    // Notation Clip
    // =============================================================================

    class NotationClip : public Clip {
        Q_OBJECT
    public:
        explicit NotationClip(QObject* parent = nullptr);

        Score* score() const { return m_score.get(); }
        void setScore(std::unique_ptr<Score> score);
        void createEmptyScore(const QString& title = "Untitled");

        bool isEmpty() const override;

        void setStaffIndex(int index) { m_staffIndex = index; }
        int staffIndex() const { return m_staffIndex; }

        void setSynthesisEnabled(bool enabled) { m_synthEnabled = enabled; }
        bool synthesisEnabled() const { return m_synthEnabled; }

        void processAudio(double position, int frames, float* buffer,
                          int channels, int sampleRate, const TempoMap& tempo) override;
                          QVector<NoteEvent> getMidiEvents(double start, double end) override;

                          QVector<NoteEvent> renderToMidi() const;

    signals:
        void scoreChanged();

    private:
        std::unique_ptr<Score> m_score;
        bool m_synthEnabled = true;
        int m_staffIndex = 0;

        struct SynthState {
            QVector<SynthVoice> voices;
            int nextVoiceId = 0;
            int lastProcessedTick = -1;
        };
        std::unique_ptr<SynthState> m_synthState;

        void updateVoices(int currentTick, const TempoMap& tempo, int sampleRate);
    };

    // =============================================================================
    // MIDI Clip
    // =============================================================================

    class MidiClip : public Clip {
        Q_OBJECT
    public:
        explicit MidiClip(QObject* parent = nullptr);

        void addEvent(const NoteEvent& event);
        void addNote(const Pitch& pitch, double start, const Duration& dur, int vel = 80, int ch = 0);
        void removeEvent(int index);
        void clearEvents() { m_events.clear(); }
        const QVector<NoteEvent>& events() const { return m_events; }

        void processAudio(double position, int frames, float* buffer,
                          int channels, int sampleRate, const TempoMap& tempo) override;
                          QVector<NoteEvent> getMidiEvents(double start, double end) override;

    private:
        QVector<NoteEvent> m_events;
    };

    // =============================================================================
    // Audio Clip - Simple Audio File Playback
    // =============================================================================

    class AudioClip : public Clip {
        Q_OBJECT
    public:
        explicit AudioClip(QObject* parent = nullptr);

        bool load(const QString& path);
        bool loadFromMemory(const QByteArray& data, int sampleRate, int channels);

        void processAudio(double position, int frames, float* buffer,
                          int channels, int sampleRate, const TempoMap& tempo) override;
                          QVector<NoteEvent> getMidiEvents(double start, double end) override;

                          int sampleRate() const { return m_sampleRate; }
                          int channels() const { return m_channels; }
                          const QVector<float>& audioData() const { return m_audioData; }

    private:
        QVector<float> m_audioData;
        int m_sampleRate = 48000;
        int m_channels = 2;
        mutable int m_readPosition = 0;
    };

    // =============================================================================
    // Track
    // =============================================================================

    class Track : public QObject {
        Q_OBJECT
    public:
        explicit Track(const QString& name, QObject* parent = nullptr);

        QString name() const { return m_name; }
        void setName(const QString& name) { m_name = name; emit nameChanged(); }

        double volume() const { return m_volume; }
        void setVolume(double vol) { m_volume = vol; emit volumeChanged(); }

        double pan() const { return m_pan; }
        void setPan(double p) { m_pan = std::max(-1.0, std::min(1.0, p)); emit panChanged(); }

        bool isMuted() const { return m_muted; }
        void setMuted(bool m) { m_muted = m; emit stateChanged(); }

        bool isSoloed() const { return m_soloed; }
        void setSoloed(bool s) { m_soloed = s; emit stateChanged(); }

        // Usa std::vector per unique_ptr
        const std::vector<std::unique_ptr<Clip>>& clips() const { return m_clips; }

        void addClip(std::unique_ptr<Clip> clip);
        void removeClip(Clip* clip);
        Clip* clipAt(int index) const;
        int clipCount() const { return static_cast<int>(m_clips.size()); }
        QVector<Clip*> clipsAt(double time) const;
        QVector<NotationClip*> notationClips() const;

        // Tracker integration
        QVector<ModTrackerClip*> trackerClips() const;

        void processAudio(double position, int frames, float* buffer,
                          int channels, int sampleRate, const TempoMap& tempo);
        void collectMidiEvents(double position, double duration, QVector<NoteEvent>& events);

    signals:
        void nameChanged();
        void volumeChanged();
        void panChanged();
        void stateChanged();
        void clipAdded(Clip* clip);
        void clipRemoved(Clip* clip);

    private:
        QString m_name;
        double m_volume = 1.0;
        double m_pan = 0.0;
        bool m_muted = false;
        bool m_soloed = false;
        std::vector<std::unique_ptr<Clip>> m_clips;
        mutable QReadWriteLock m_lock;
    };

    // =============================================================================
    // Transport
    // =============================================================================

    class Transport : public QObject {
        Q_OBJECT
    public:
        explicit Transport(QObject* parent = nullptr);

        double position() const { return m_position.load(); }
        void setPosition(double pos);

        TransportState state() const { return m_state; }
        void play();
        void stop();
        void pause();
        void togglePlay();

        double tempo() const { return m_tempo; }
        void setTempo(double bpm);

        void seekToBeat(double beat);
        double beatToTime(double beat) const;
        double timeToBeat(double time) const;

        TempoMap* tempoMap() { return &m_tempoMap; }
        const TempoMap* tempoMap() const { return &m_tempoMap; }

        bool isLooping() const { return m_looping; }
        void setLooping(bool loop) { m_looping = loop; }
        double loopStart() const { return m_loopStart; }
        double loopEnd() const { return m_loopEnd; }
        void setLoopRange(double start, double end);

    signals:
        void positionChanged(double pos);
        void stateChanged(TransportState state);
        void tempoChanged(double tempo);
        void aboutToLoop();

    private:
        std::atomic<double> m_position{0.0};
        TransportState m_state = TransportState::Stopped;
        double m_tempo = 120.0;
        TempoMap m_tempoMap;
        bool m_looping = false;
        double m_loopStart = 0.0;
        double m_loopEnd = -1.0;
        QMutex m_positionMutex;
    };

    // =============================================================================
    // DAW Engine
    // =============================================================================

    class DAWEngine : public QObject {
        Q_OBJECT
    public:
        explicit DAWEngine(QObject* parent = nullptr);

        Transport* transport() { return &m_transport; }

        Track* addTrack(const QString& name = QString());
        void removeTrack(int index);
        Track* trackAt(int index) const;
        int trackCount() const { return static_cast<int>(m_tracks.size()); }

        NotationClip* createNotationClip(int trackIndex = -1, const QString& name = "Notation");
        NotationClip* importScore(const QString& path, int trackIndex = -1);

        // Tracker integration methods
        ModTrackerClip* createTrackerClip(int trackIndex = -1,
                                          const QString& name = "Tracker",
                                          int channels = 4);
        ModTrackerClip* importTrackerModule(const QString& path,
                                            int trackIndex = -1);
        Track* createTrackerTrack(const QString& name = "Tracker Track");

        void processAudio(float* buffer, int frames, int channels, int sampleRate);

        bool saveProject(const QString& path);
        bool loadProject(const QString& path);

    signals:
        void trackAdded(Track* track);
        void trackRemoved(int index);

    private:
        Transport m_transport;
        std::vector<std::unique_ptr<Track>> m_tracks;
        QVector<float> m_mixBuffer;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::TransportState)
Q_DECLARE_METATYPE(Aegis::ClipType)
