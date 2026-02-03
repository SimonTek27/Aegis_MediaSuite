// music_notation.h - Music Notation Data Model and Rendering
// Part of Aegis Multimedia Suite - Notation Pillar
// Provides complete music notation representation compatible with MusicXML
// Integrates with AudioEngine for playback (Pillar 1) and AudioEffects (Pillar 2)

#pragma once

#include "audio.h"
#include "audio_effects.h"
#include <QObject>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QString>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QRectF>
#include <QTransform>
#include <memory>
#include <functional>
#include <cmath>

namespace Aegis {

    // =============================================================================
    // Music Theory Fundamentals
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
        CSharp_Major, F_Major, Bb_Major, Eb_Major, Ab_Major, Db_Major, Gb_Major, Cb_Major,
        // Minor keys (relative to major)
        A_Minor = 0, E_Minor, B_Minor, FSharp_Minor, CSharp_Minor, GSharp_Minor, DSharp_Minor,
        ASharp_Minor, D_Minor, G_Minor, C_Minor, F_Minor, Bb_Minor, Eb_Minor, Ab_Minor
    };

    enum class TimeSignatureType {
        Simple, Compound, Irrational, CommonTime, CutTime
    };

    enum class NoteDuration {
        Maxima = 0, Long, Breve, Whole, Half, Quarter, Eighth,
        Sixteenth, ThirtySecond, SixtyFourth, HundredTwentyEighth, TwoHundredFiftySixth
    };

    enum class NoteHeadType {
        Normal, Cross, Diamond, Triangle, Slash, XCircle, Do, Re, Mi,
        Fa, So, La, Ti, Rectangle, Oval
    };

    enum class BeamMode {
        Auto, Begin, Continue, End, ForwardHook, BackwardHook, NoBeam
    };

    enum class TieType { None, Start, Stop, Continue };
    enum class SlurType { None, Start, Stop };
    enum class Articulation {
        None, Staccato, Tenuto, Accent, Marcato, Staccatissimo,
        Fermata, FermataShort, FermataLong, BreathMark, Caesura
    };

    enum class Dynamic {
        PPPP, PPP, PP, P, MP, MF, F, FF, FFF, FFFF,
        SF, SFP, SFPP, FP, RF, RFZ, SFZ, SFFZ, FZ, N
    };

    enum class Ornament {
        None, Trill, Turn, Mordent, InvertedMordent, Pralltriller,
        UpPrall, DownPrall, LinePrall, Shake, Schleifer
    };

    enum class HairpinType { None, Crescendo, Decrescendo, Diminuendo };

    // =============================================================================
    // Pitch Representation
    // =============================================================================

    struct Pitch {
        int midiNote = 60;  // Middle C = 60
        PitchClass pitchClass = PitchClass::C;
        int octave = 4;
        Accidental accidental = Accidental::None;
        bool showAccidental = false;  // Force display even if in key signature

        Pitch() = default;
        Pitch(int midi) : midiNote(midi) { fromMidi(midi); }
        Pitch(PitchClass pc, int oct, Accidental acc = Accidental::None)
        : pitchClass(pc), octave(oct), accidental(acc) {
            toMidi();
        }

        void fromMidi(int midi);
        void toMidi();
        int compare(const Pitch& other) const;
        QString toString() const;
        static Pitch fromString(const QString& str);

        bool operator==(const Pitch& other) const { return midiNote == other.midiNote; }
        bool operator<(const Pitch& other) const { return midiNote < other.midiNote; }
    };

    // =============================================================================
    // Duration and Timing
    // =============================================================================

    struct Duration {
        NoteDuration type = NoteDuration::Quarter;
        int dots = 0;
        int numerator = 1;      // For tuplets
        int denominator = 1;    // For tuplets (3/2 = triplet)

        double toQuarterNotes() const;
        double toSeconds(double tempoBpm) const;
        QString toString() const;
        static Duration fromString(const QString& str);

        bool isTuplet() const { return numerator != 1 || denominator != 1; }
        bool operator==(const Duration& other) const;
    };

    // =============================================================================
    // Time Signature
    // =============================================================================

    struct TimeSignature {
        int numerator = 4;
        int denominator = 4;
        TimeSignatureType type = TimeSignatureType::Simple;

        double beatsPerMeasure() const { return numerator; }
        double beatUnit() const { return 4.0 / denominator; }  // In quarter notes
        bool isCompound() const;
        QString toString() const { return QString("%1/%2").arg(numerator).arg(denominator); }
    };

    // =============================================================================
    // Musical Elements
    // =============================================================================

    class Measure;
    class Staff;
    class Score;

    struct Note {
        Pitch pitch;
        Duration duration;
        NoteHeadType headType = NoteHeadType::Normal;
        bool isRest = false;
        bool isChord = false;       // Part of a chord (not the primary note)
        int voice = 0;              // 0-3 for four voices
        int velocity = 80;          // MIDI velocity 0-127

        // Visual
        bool stemUp = true;
        int staffLine = 0;          // Calculated line position

        // Notation
        TieType tie = TieType::None;
        QVector<Articulation> articulations;
        Ornament ornament = Ornament::None;
        QVector<SlurType> slurs;    // One per slur ID

        // Playback
        double playbackDurationMultiplier = 1.0;  // For staccato, tenuto, etc.

        // Position
        Measure* measure = nullptr;
        int tickPosition = 0;       // Position in ticks (480 PPQ)

        // Chord members
        QVector<Pitch> chordPitches;

        bool isGraceNote = false;
        Duration graceDuration;     // Duration to steal from main note

        Note() = default;
        explicit Note(const Pitch& p, const Duration& d) : pitch(p), duration(d) {}

        double playDurationTicks(double tempo) const;
        QString toLilyPond() const;
    };

    struct Chord {
        QVector<Note> notes;
        Duration duration;
        bool arpeggiate = false;
        bool arpeggiateUp = true;

        Pitch lowestNote() const;
        Pitch highestNote() const;
    };

    struct Rest : public Note {
        Rest(const Duration& d) {
            isRest = true;
            duration = d;
        }
    };

    struct Tuplet {
        int id = 0;
        int numerator = 3;      // 3 notes
        int denominator = 2;    // In the time of 2
        NoteDuration baseDuration;
        QVector<Note*> notes;
        bool bracket = true;
        int numberVisible = 1;  // 0 = none, 1 = number only, 2 = number + ratio
    };

    struct Clef {
        ClefType type = ClefType::Treble;
        int staffLine = 2;      // Which line (0-based from bottom)
        int octaveChange = 0;   // +1 = 8va, -1 = 8vb

        int pitchOffset() const;  // MIDI note number at bottom line
        QString toString() const;
    };

    struct KeySig {
        KeySignature key = KeySignature::C_Major;
        bool showNaturals = false;  // Show cancelling naturals

        int accidentals() const;     // -7 to +7
        bool isSharpKey() const;
        bool isFlatKey() const;
        Accidental accidentalForPitch(PitchClass pc) const;
    };

    struct Barline {
        enum Type {
            Single, Double, EndRepeat, BeginRepeat, EndBeginRepeat,
            Final, Dashed, Dotted, Tick, Short
        } type = Single;

        int repeatCount = 2;        // For repeats
        QString voltaText;          // "1., 2." etc.
    };

    struct Tempo {
        NoteDuration beatUnit = NoteDuration::Quarter;
        int bpm = 120;
        bool showMetronome = true;
        QString text;               // "Allegro", "Andante", etc.
        double exactBpm() const;
    };

    struct DynamicMark {
        Dynamic dynamic = Dynamic::MF;
        bool isExpressionText = false;  // "cresc.", "dim." etc.
        QString customText;
        int velocity() const;
    };

    struct Hairpin {
        HairpinType type = HairpinType::Crescendo;
        int startTick = 0;
        int endTick = 0;
        double startDynamic = 0.5;   // 0-1 mapped to velocity
        double endDynamic = 0.8;
    };

    struct Lyric {
        QString text;
        int verse = 0;
        enum Syllabic { Single, Begin, Middle, End } syllabic = Single;
        bool melisma = false;
    };

    // =============================================================================
    // Measure - Container for musical events
    // =============================================================================

    class Measure : public QObject {
        Q_OBJECT
    public:
        explicit Measure(int number, QObject* parent = nullptr);

        int measureNumber() const { return m_number; }
        int startTick() const { return m_startTick; }
        int lengthTicks() const { return m_lengthTicks; }

        // Content
        QVector<Note> notes;
        QVector<Clef> clefs;
        QVector<KeySig> keySigs;
        QVector<TimeSignature> timeSigs;
        QVector<Barline> barlines;
        QVector<Tempo> tempos;
        QVector<DynamicMark> dynamics;
        QVector<Lyric> lyrics;
        QVector<Tuplet> tuplets;
        QVector<Hairpin> hairpins;

        // Layout
        double width = 100.0;       // Display width in points
        double xPosition = 0.0;     // Absolute position in score

        // Methods
        void addNote(const Note& note, int voice = 0);
        void removeNote(int index);
        Note* noteAtTick(int tick, int voice = 0);
        QVector<Note*> notesInRange(int startTick, int endTick, int voice = -1);

        bool isPickup() const { return m_isPickup; }
        void setPickup(bool pickup) { m_isPickup = pickup; }

        int tickToPixel(int tick) const;
        int pixelToTick(double x) const;

        // Duration calculation
        int filledTicks() const;
        int remainingTicks() const;
        bool isFull() const;

        // Playback
        double tempoAt(int tick) const;
        double absoluteTimeAt(int tick) const;  // Seconds from score start

    signals:
        void noteAdded(const Note& note);
        void noteRemoved(int index);
        void modified();

    private:
        int m_number = 0;
        int m_startTick = 0;
        int m_lengthTicks = 1920;   // 4/4 at 480 PPQ
        bool m_isPickup = false;
    };

    // =============================================================================
    // Staff - Single instrument line
    // =============================================================================

    class Staff : public QObject {
        Q_OBJECT
    public:
        explicit Staff(const QString& name, QObject* parent = nullptr);

        QString name() const { return m_name; }
        QString shortName() const { return m_shortName; }
        void setName(const QString& name) { m_name = name; }
        void setShortName(const QString& name) { m_shortName = name; }

        // Properties
        int lines() const { return m_lines; }
        void setLines(int lines) { m_lines = lines; }

        Clef defaultClef() const { return m_defaultClef; }
        void setDefaultClef(const Clef& clef) { m_defaultClef = clef; }

        // MIDI
        int midiChannel() const { return m_midiChannel; }
        int midiProgram() const { return m_midiProgram; }
        void setMidiChannel(int ch) { m_midiChannel = ch; }
        void setMidiProgram(int prog) { m_midiProgram = prog; }

        // Transposition
        int transposeChromatic() const { return m_transposeChromatic; }
        int transposeDiatonic() const { return m_transposeDiatonic; }
        void setTranspose(int chromatic, int diatonic);

        // Measures
        QVector<std::unique_ptr<Measure>> measures;
        Measure* addMeasure(int number);
        void removeMeasure(int index);
        Measure* measureAtTick(int tick);

        // Braces/brackets
        bool showBrace = false;
        bool showBracket = false;
        QVector<Staff*> bracketedWith;  // Grand staff grouping

        // Layout
        double yPosition = 0.0;
        double height() const;

    signals:
        void measureAdded(Measure* measure);
        void measureRemoved(int index);

    private:
        QString m_name;
        QString m_shortName;
        int m_lines = 5;
        Clef m_defaultClef;
        int m_midiChannel = 0;
        int m_midiProgram = 0;      // General MIDI program
        int m_transposeChromatic = 0;
        int m_transposeDiatonic = 0;
    };

    // =============================================================================
    // Score - Complete musical composition
    // =============================================================================

    class Score : public QObject {
        Q_OBJECT
    public:
        explicit Score(QObject* parent = nullptr);

        // Metadata
        QString title() const { return m_title; }
        QString composer() const { return m_composer; }
        QString lyricist() const { return m_lyricist; }
        QString copyright() const { return m_copyright; }

        void setTitle(const QString& title) { m_title = title; emit metadataChanged(); }
        void setComposer(const QString& comp) { m_composer = comp; emit metadataChanged(); }
        void setLyricist(const QString& lyr) { m_lyricist = lyr; emit metadataChanged(); }
        void setCopyright(const QString& copy) { m_copyright = copy; emit metadataChanged(); }

        // Staves
        QVector<std::unique_ptr<Staff>> staves;
        Staff* addStaff(const QString& name);
        void removeStaff(int index);
        Staff* staffAtY(double y) const;

        // Global properties
        TimeSignature defaultTimeSignature() const { return m_defaultTimeSig; }
        void setDefaultTimeSignature(const TimeSignature& ts) { m_defaultTimeSig = ts; }

        int ticksPerQuarter() const { return m_ticksPerQuarter; }  // PPQ - usually 480
        void setTicksPerQuarter(int ticks) { m_ticksPerQuarter = ticks; }

        // Layout
        double pageWidth() const { return m_pageWidth; }
        double pageHeight() const { return m_pageHeight; }
        double staffDistance() const { return m_staffDistance; }
        void setPageSize(double w, double h) { m_pageWidth = w; m_pageHeight = h; }

        // Navigation
        int totalTicks() const;
        Measure* measureAtTick(int tick);
        Note* noteAtTick(int tick, int staffIdx = 0, int voice = 0);

        // File I/O
        bool loadMusicXML(const QString& path);
        bool saveMusicXML(const QString& path) const;
        bool loadMIDI(const QString& path);
        bool saveMIDI(const QString& path) const;
        bool loadMuseScore(const QString& path);  // .mscz/.mscx
        bool saveLilyPond(const QString& path) const;

        // Audio generation
        QByteArray renderToPCM(int sampleRate = 48000);  // For preview
        void setAudioEngine(AudioEngine* engine) { m_audioEngine = engine; }

        // Export
        bool exportPDF(const QString& path, const QRectF& pageRect);
        bool exportPNG(const QString& path, int dpi = 300);
        bool exportSVG(const QString& path);

    signals:
        void modified();
        void metadataChanged();
        void structureChanged();  // Staves/measures added/removed

    private:
        QString m_title;
        QString m_composer;
        QString m_lyricist;
        QString m_copyright;

        TimeSignature m_defaultTimeSig;
        int m_ticksPerQuarter = 480;

        // Page layout (in points, 72 points = 1 inch)
        double m_pageWidth = 1224;   // 8.5 * 144 (HiDPI)
        double m_pageHeight = 1584;  // 11 * 144
        double m_staffDistance = 80.0;

        AudioEngine* m_audioEngine = nullptr;

        // Undo/redo
        struct EditAction {
            enum Type { AddNote, RemoveNote, AddMeasure, RemoveMeasure,
                ChangePitch, ChangeDuration, AddStaff, RemoveStaff } type;
                QVariant data;
                std::function<void()> undo;
                std::function<void()> redo;
        };
        QVector<EditAction> m_undoStack;
        int m_undoIndex = 0;
    };

    // =============================================================================
    // Engraving/Rendering
    // =============================================================================

    class EngravingSettings {
    public:
        // Spacing
        double spatium = 20.0;  // Staff space - base unit for all spacing
        double noteHeadWidth = 1.1 * 20.0;
        double stemWidth = 0.13 * 20.0;
        double stemLength = 3.5 * 20.0;
        double beamWidth = 0.5 * 20.0;

        // Fonts
        QFont musicFont{"Bravura", 24};      // SMuFL font for symbols
        QFont textFont{"Times New Roman", 12};
        QFont lyricsFont{"Times New Roman", 11};

        // Colors
        QColor noteColor{Qt::black};
        QColor selectionColor{100, 150, 255, 100};
        QColor playbackColor{255, 100, 100, 150};

        static EngravingSettings& defaults();
    };

    class ScoreRenderer {
    public:
        explicit ScoreRenderer(Score* score);

        void render(QPainter* painter, const QRectF& rect, int startStaff = 0, int staffCount = -1);
        void renderMeasure(QPainter* painter, Measure* measure, const QPointF& pos);
        void renderNote(QPainter* painter, const Note& note, const QPointF& pos);
        void renderRest(QPainter* painter, const Note& rest, const QPointF& pos);
        void renderClef(QPainter* painter, const Clef& clef, const QPointF& pos);
        void renderKeySig(QPainter* painter, const KeySig& key, const QPointF& pos);
        void renderTimeSig(QPainter* painter, const TimeSignature& ts, const QPointF& pos);

        // Hit testing
        Note* noteAt(const QPointF& pos);
        Measure* measureAt(const QPointF& pos);
        Staff* staffAt(const QPointF& pos);

        // Layout
        void doLayout();
        void layoutMeasure(Measure* measure);
        double measureWidth(Measure* measure) const;

    private:
        Score* m_score;
        EngravingSettings m_settings;

        // SMuFL code points for musical symbols
        static QMap<QString, uint> s_smuflCodes;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::Pitch)
Q_DECLARE_METATYPE(Aegis::Duration)
Q_DECLARE_METATYPE(Aegis::Note)
