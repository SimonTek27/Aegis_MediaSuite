// audio_daw.cpp — Aegis DAW Engine: Score / MIDI I/O implementation

#include "audio_daw.h"
// audio_modtracker.h provides the full definition of ModTrackerClip,
// required by static_cast<ModTrackerClip*> in Track::trackerClips().
#include "audio_modtracker.h"
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <memory>

namespace Aegis {

    // =============================================================================
    // Duration helpers
    // =============================================================================

    double Duration::toQuarterNotes() const {
        double base = 0.0;
        switch (type) {
            case DurationType::Whole: base = 4.0; break;
            case DurationType::Half: base = 2.0; break;
            case DurationType::Quarter: base = 1.0; break;
            case DurationType::Eighth: base = 0.5; break;
            case DurationType::Sixteenth: base = 0.25; break;
            case DurationType::ThirtySecond: base = 0.125; break;
            case DurationType::SixtyFourth: base = 0.0625; break;
            default: base = 1.0;
        }

        // Apply dots
        double dotted = base;
        for (int i = 0; i < dots; ++i) {
            dotted += base / (2 << i);
        }

        // Apply tuplet
        return dotted * (static_cast<double>(numerator) / denominator);
    }

    double Duration::toSeconds(double tempoBpm) const {
        double quarterSeconds = 60.0 / tempoBpm;
        return toQuarterNotes() * quarterSeconds;
    }

    QString Duration::toString() const {
        QString result;
        switch (type) {
            case DurationType::Whole: result = "1"; break;
            case DurationType::Half: result = "2"; break;
            case DurationType::Quarter: result = "4"; break;
            case DurationType::Eighth: result = "8"; break;
            case DurationType::Sixteenth: result = "16"; break;
            case DurationType::ThirtySecond: result = "32"; break;
            case DurationType::SixtyFourth: result = "64"; break;
            default: result = "4";
        }

        for (int i = 0; i < dots; ++i) {
            result += ".";
        }

        if (isTuplet()) {
            result = QString("%1/%2").arg(numerator).arg(denominator) + result;
        }

        return result;
    }

    Duration Duration::fromString(const QString& str) {
        Duration d;
        // Simple parsing - would need more robust implementation
        if (str.contains("1")) d.type = DurationType::Whole;
        else if (str.contains("2")) d.type = DurationType::Half;
        else if (str.contains("4")) d.type = DurationType::Quarter;
        else if (str.contains("8")) d.type = DurationType::Eighth;
        else if (str.contains("16")) d.type = DurationType::Sixteenth;

        d.dots = str.count('.');
        return d;
    }

    bool Duration::operator==(const Duration& other) const {
        return type == other.type && dots == other.dots &&
        numerator == other.numerator && denominator == other.denominator;
    }

    // =============================================================================
    // Pitch helpers
    // =============================================================================

    void Pitch::fromMidi(int midi) {
        midiNote = std::clamp(midi, 0, 127);
        int pc = midiNote % 12;
        pitchClass = static_cast<PitchClass>(pc);
        octave = (midiNote / 12) - 1;
        accidental = Accidental::Natural;
    }

    void Pitch::toMidi() {
        int pc = static_cast<int>(pitchClass);
        midiNote = (octave + 1) * 12 + pc;

        // Apply accidental
        switch (accidental) {
            case Accidental::Sharp: midiNote += 1; break;
            case Accidental::Flat: midiNote -= 1; break;
            case Accidental::DoubleSharp: midiNote += 2; break;
            case Accidental::DoubleFlat: midiNote -= 2; break;
            default: break;
        }

        midiNote = std::clamp(midiNote, 0, 127);
    }

    int Pitch::compare(const Pitch& other) const {
        return midiNote - other.midiNote;
    }

    QString Pitch::toString() const {
        static const char* names[] = {"C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"};
            return QString("%1%2").arg(names[static_cast<int>(pitchClass)]).arg(octave);
    }

    Pitch Pitch::fromString(const QString& str) {
        Pitch p;
        // Simple parsing - would need more robust implementation
        QString note = str;

        // Crea QRegularExpression localmente
        QRegularExpression re("[0-9]");
        note.remove(re);

        int oct = str.mid(note.length()).toInt();

        if (note == "C") p.pitchClass = PitchClass::C;
        else if (note == "C#") p.pitchClass = PitchClass::CSharp;
        else if (note == "D") p.pitchClass = PitchClass::D;
        else if (note == "D#") p.pitchClass = PitchClass::DSharp;
        else if (note == "E") p.pitchClass = PitchClass::E;
        else if (note == "F") p.pitchClass = PitchClass::F;
        else if (note == "F#") p.pitchClass = PitchClass::FSharp;
        else if (note == "G") p.pitchClass = PitchClass::G;
        else if (note == "G#") p.pitchClass = PitchClass::GSharp;
        else if (note == "A") p.pitchClass = PitchClass::A;
        else if (note == "A#") p.pitchClass = PitchClass::ASharp;
        else if (note == "B") p.pitchClass = PitchClass::B;

        p.octave = oct;
        p.toMidi();
        return p;
    }

    // =============================================================================
    // KeySig helpers
    // =============================================================================

    int KeySig::accidentals() const {
        int val = static_cast<int>(key);
        if (val <= 7) return val;  // Sharp keys
        return 7 - (val - 7);       // Flat keys
    }

    bool KeySig::isSharpKey() const {
        return static_cast<int>(key) <= 7;
    }

    bool KeySig::isFlatKey() const {
        return static_cast<int>(key) > 7;
    }

    Accidental KeySig::accidentalForPitch(PitchClass pc) const {
        static const QMap<int, QVector<PitchClass>> sharpKeys = {
            {0, {}},  // C major
            {1, {PitchClass::F}},  // G major
            {2, {PitchClass::F, PitchClass::C}},  // D major
            {3, {PitchClass::F, PitchClass::C, PitchClass::G}},  // A major
            {4, {PitchClass::F, PitchClass::C, PitchClass::G, PitchClass::D}},  // E major
            {5, {PitchClass::F, PitchClass::C, PitchClass::G, PitchClass::D, PitchClass::A}},  // B major
            {6, {PitchClass::F, PitchClass::C, PitchClass::G, PitchClass::D, PitchClass::A, PitchClass::E}},  // F# major
            {7, {PitchClass::F, PitchClass::C, PitchClass::G, PitchClass::D, PitchClass::A, PitchClass::E, PitchClass::B}}  // C# major
        };

        static const QMap<int, QVector<PitchClass>> flatKeys = {
            {7, {PitchClass::B}},  // F major
            {6, {PitchClass::B, PitchClass::E}},  // Bb major
            {5, {PitchClass::B, PitchClass::E, PitchClass::A}},  // Eb major
            {4, {PitchClass::B, PitchClass::E, PitchClass::A, PitchClass::D}},  // Ab major
            {3, {PitchClass::B, PitchClass::E, PitchClass::A, PitchClass::D, PitchClass::G}},  // Db major
            {2, {PitchClass::B, PitchClass::E, PitchClass::A, PitchClass::D, PitchClass::G, PitchClass::C}},  // Gb major
            {1, {PitchClass::B, PitchClass::E, PitchClass::A, PitchClass::D, PitchClass::G, PitchClass::C, PitchClass::F}}  // Cb major
        };

        int acc = accidentals();
        if (isSharpKey() && acc > 0) {
            const auto& list = sharpKeys[acc];
            if (list.contains(pc)) return Accidental::Sharp;
        } else if (isFlatKey() && acc > 0) {
            const auto& list = flatKeys[acc];
            if (list.contains(pc)) return Accidental::Flat;
        }

        return Accidental::Natural;
    }

    // =============================================================================
    // Clef helpers
    // =============================================================================

    int Clef::pitchOffset() const {
        switch (type) {
            case ClefType::Treble: return 2;  // G clef on second line
            case ClefType::Bass: return 6;    // F clef on fourth line
            case ClefType::Alto: return 4;     // C clef on third line
            case ClefType::Tenor: return 4;     // C clef on fourth line
            default: return 2;
        }
    }

    QString Clef::toString() const {
        switch (type) {
            case ClefType::Treble: return "treble";
            case ClefType::Bass: return "bass";
            case ClefType::Alto: return "alto";
            case ClefType::Tenor: return "tenor";
            default: return "treble";
        }
    }

    // =============================================================================
    // Note helpers
    // =============================================================================

    double Note::playDurationTicks(double /*tempo*/) const {
        return duration.toQuarterNotes() * 480.0 * playbackDurationMultiplier;
    }

    QString Note::toLilyPond() const {
        if (isRest)
            return QString("r%1").arg(duration.toString());

        static const char* pitchNames[] = {
            "c","cis","d","dis","e","f","fis","g","gis","a","ais","b"
        };

        int semitone = static_cast<int>(pitch.pitchClass) % 12;
        if (semitone < 0) semitone += 12;
        QString result = pitchNames[semitone];

        int octDiff = pitch.octave - 3;
        if      (octDiff > 0) for (int i = 0; i < octDiff;  ++i) result += '\'';
        else if (octDiff < 0) for (int i = 0; i < -octDiff; ++i) result += ',';

        result += duration.toString()
        .replace("quarter","4").replace("eighth","8")
        .replace("half","2").replace("whole","1")
        .replace("16th","16").replace("32nd","32")
        .replace("64th","64");
        return result;
    }

    // =============================================================================
    // SynthVoice
    // =============================================================================

    void SynthVoice::start(const Pitch& p, double vel, double dur, double now) {
        pitch = p;
        velocity = vel;
        startTime = now;
        duration = dur;
        currentTime = 0.0;
        phase = 0.0;
        isActive = true;
        isReleasing = false;
        noteId++;
    }

    void SynthVoice::release() {
        if (isActive && !isReleasing) {
            isReleasing = true;
            releaseStartLevel = envelope();
        }
    }

    double SynthVoice::envelope() const {
        if (!isActive) return 0.0;

        if (isReleasing) {
            double releaseTime = this->releaseTime;
            if (releaseTime <= 0.0) return 0.0;
            double releasePos = currentTime - (duration - releaseTime);
            return std::max(0.0, releaseStartLevel * (1.0 - releasePos / releaseTime));
        }

        if (currentTime < attack) {
            return currentTime / attack;  // Linear attack
        }

        if (currentTime < attack + decay) {
            double decayPos = (currentTime - attack) / decay;
            return 1.0 - (1.0 - sustain) * decayPos;
        }

        if (currentTime < duration - releaseTime) {
            return sustain;
        }

        return sustain;
    }

    double SynthVoice::nextSample() {
        if (!isActive) return 0.0;

        double freq = pitch.frequency();
        double value = 0.0;

        switch (waveform) {
            case Waveform::Sine:
                value = std::sin(2.0 * M_PI * freq * phase);
                break;
            case Waveform::Square:
                value = (std::sin(2.0 * M_PI * freq * phase) > 0) ? 1.0 : -1.0;
                break;
            case Waveform::Saw:
                value = 2.0 * (phase * freq - std::floor(phase * freq + 0.5));
                break;
            case Waveform::Triangle:
                value = 2.0 * std::abs(2.0 * (phase * freq - std::floor(phase * freq + 0.5))) - 1.0;
                break;
            default:
                value = 0.0;
        }

        phase += 1.0 / sampleRate;
        currentTime += 1.0 / sampleRate;

        if (currentTime >= duration) {
            isActive = false;
        }

        return value * envelope() * velocity;
    }

    // =============================================================================
    // TempoMap
    // =============================================================================

    double TempoMap::beatsToSeconds(double beats) const {
        double time = 0.0;
        double lastBeat = 0.0;
        double currentBpm = defaultBpm;

        for (const auto& change : tempoChanges) {
            if (change.beatPosition > beats) break;

            time += (change.beatPosition - lastBeat) * (60.0 / currentBpm);
            lastBeat = change.beatPosition;
            currentBpm = change.bpm;
        }

        time += (beats - lastBeat) * (60.0 / currentBpm);
        return time;
    }

    double TempoMap::secondsToBeats(double seconds) const {
        double beats = 0.0;
        double time = 0.0;
        double currentBpm = defaultBpm;

        for (const auto& change : tempoChanges) {
            double changeTime = change.beatPosition * (60.0 / currentBpm);
            if (changeTime > seconds) break;

            beats += (change.beatPosition - beats) * (60.0 / currentBpm);
            time = changeTime;
            currentBpm = change.bpm;
        }

        beats += (seconds - time) * (currentBpm / 60.0);
        return beats;
    }

    double TempoMap::bpmAtBeat(double beat) const {
        double currentBpm = defaultBpm;

        for (const auto& change : tempoChanges) {
            if (change.beatPosition > beat) break;
            currentBpm = change.bpm;
        }

        return currentBpm;
    }

    int TempoMap::timeToTick(double seconds) const {
        double beats = secondsToBeats(seconds);
        return static_cast<int>(beats * ticksPerQuarter);
    }

    double TempoMap::tickToTime(int tick) const {
        double beats = static_cast<double>(tick) / ticksPerQuarter;
        return beatsToSeconds(beats);
    }

    void TempoMap::addTempoChange(double beat, double bpm, bool ramp) {
        TempoChange change;
        change.beatPosition = beat;
        change.bpm = bpm;
        change.ramp = ramp;

        tempoChanges.append(change);
        std::sort(tempoChanges.begin(), tempoChanges.end(),
                  [](const TempoChange& a, const TempoChange& b) {
                      return a.beatPosition < b.beatPosition;
                  });
    }

    void TempoMap::clear() {
        tempoChanges.clear();
    }

    // =============================================================================
    // Measure Private Implementation
    // =============================================================================

    class Measure::Private {
    public:
        int number = 0;
        int startTick = 0;
        int lengthTicks = 1920;
        QVector<Note> notes;
        QVector<Clef> clefs;
        QVector<KeySig> keySigs;
        QVector<TimeSignature> timeSigs;
        QVector<Barline> barlines;

        // Cache for fast lookup
        mutable QHash<int, QVector<Note*>> noteCache;
        mutable bool cacheValid = false;
    };

    Measure::Measure(int number, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
        d->number = number;
    }

    Measure::~Measure() = default;

    int Measure::measureNumber() const { return d->number; }
    int Measure::startTick() const { return d->startTick; }
    int Measure::lengthTicks() const { return d->lengthTicks; }
    void Measure::setStartTick(int tick) { d->startTick = tick; }
    void Measure::setLengthTicks(int ticks) { d->lengthTicks = ticks; }

    const QVector<Note>& Measure::notes() const { return d->notes; }
    QVector<Note>& Measure::notes() { return d->notes; }

    void Measure::addNote(const Note& note, int voice) {
        Note n = note;
        n.voice = voice;
        n.measure = this;
        d->notes.append(n);
        d->cacheValid = false;
        emit noteAdded(n);
        emit modified();
    }

    void Measure::removeNote(int index) {
        if (index >= 0 && index < d->notes.size()) {
            d->notes.removeAt(index);
            d->cacheValid = false;
            emit noteRemoved(index);
            emit modified();
        }
    }

    Note* Measure::noteAtTick(int tick, int voice) {
        if (!d->cacheValid) {
            rebuildNoteCache();
        }

        auto it = d->noteCache.find(tick);
        if (it != d->noteCache.end()) {
            for (Note* note : it.value()) {
                if (voice < 0 || note->voice == voice) {
                    return note;
                }
            }
        }
        return nullptr;
    }

    QVector<Note*> Measure::notesInRange(int startTick, int endTick, int voice) {
        QVector<Note*> result;

        for (auto& note : d->notes) {
            if (note.tickPosition >= startTick &&
                note.tickPosition < endTick &&
                (voice < 0 || note.voice == voice)) {
                result.append(&note);
                }
        }

        return result;
    }

    void Measure::rebuildNoteCache() const {
        d->noteCache.clear();
        for (const auto& note : d->notes) {
            d->noteCache[note.tickPosition].append(const_cast<Note*>(&note));
        }
        d->cacheValid = true;
    }

    int Measure::tickToPixel(int tick) const {
        double ratio = width / lengthTicks();
        return static_cast<int>(xPosition + tick * ratio);
    }

    int Measure::pixelToTick(double x) const {
        if (x < xPosition) return 0;
        if (x > xPosition + width) return lengthTicks();
        double ratio = width / lengthTicks();
        return static_cast<int>((x - xPosition) / ratio);
    }

    int Measure::filledTicks() const {
        int maxTick = 0;
        for (const auto& note : d->notes) {
            int noteEnd = note.tickPosition +
            static_cast<int>(note.duration.toQuarterNotes() * 480);
            maxTick = qMax(maxTick, noteEnd);
        }
        return maxTick;
    }

    int Measure::remainingTicks() const {
        return d->lengthTicks - filledTicks();
    }

    bool Measure::isFull() const {
        return filledTicks() >= d->lengthTicks;
    }

    double Measure::tempoAt(int tick) const {
        Q_UNUSED(tick)
        // Tempo is owned by Score, not Measure
        return 120.0;
    }

    double Measure::absoluteTimeAt(int tick) const {
        return tick / 480.0 * (60.0 / tempoAt(tick));
    }

    // =============================================================================
    // Staff Private Implementation
    // =============================================================================

    class Staff::Private {
    public:
        QString name;
        int lines = 5;
        Clef defaultClef;
        int midiChannel = 0;
        int midiProgram = 0;
        int transposeChromatic = 0;
        int transposeDiatonic = 0;

        // Use unique_ptr for automatic cleanup with std::vector
        std::vector<std::unique_ptr<Measure>> measures;

        // Cache for quick measure lookup
        mutable QMap<int, Measure*> measureByTick;
        mutable bool cacheValid = false;
    };

    Staff::Staff(const QString& name, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
        d->name = name;
    }

    Staff::~Staff() = default;

    QString Staff::name() const { return d->name; }
    void Staff::setName(const QString& name) { d->name = name; }

    int Staff::lines() const { return d->lines; }
    void Staff::setLines(int lines) { d->lines = lines; }

    Clef Staff::defaultClef() const { return d->defaultClef; }
    void Staff::setDefaultClef(const Clef& clef) { d->defaultClef = clef; }

    int Staff::midiChannel() const { return d->midiChannel; }
    int Staff::midiProgram() const { return d->midiProgram; }
    void Staff::setMidiChannel(int ch) { d->midiChannel = ch; }
    void Staff::setMidiProgram(int prog) { d->midiProgram = prog; }

    int Staff::transposeChromatic() const { return d->transposeChromatic; }
    int Staff::transposeDiatonic() const { return d->transposeDiatonic; }

    void Staff::setTranspose(int chromatic, int diatonic) {
        d->transposeChromatic = chromatic;
        d->transposeDiatonic = diatonic;
    }

    Measure* Staff::addMeasure(int number) {
        auto measure = std::make_unique<Measure>(number, this);
        Measure* ptr = measure.get();

        if (!d->measures.empty()) {
            auto* last = d->measures.back().get();
            measure->setStartTick(last->startTick() + last->lengthTicks());
        }

        d->measures.push_back(std::move(measure));
        d->cacheValid = false;
        emit measureAdded(ptr);
        return ptr;
    }

    void Staff::removeMeasure(int index) {
        if (index < 0 || index >= static_cast<int>(d->measures.size())) return;

        d->measures.erase(d->measures.begin() + index);
        d->cacheValid = false;
        emit measureRemoved(index);
    }

    Measure* Staff::measureAtTick(int tick) {
        if (!d->cacheValid) {
            rebuildCache();
        }

        auto it = d->measureByTick.lowerBound(tick);
        if (it != d->measureByTick.begin()) {
            --it;
            if (tick >= it.value()->startTick() &&
                tick < it.value()->startTick() + it.value()->lengthTicks()) {
                return it.value();
                }
        }
        return nullptr;
    }

    void Staff::rebuildCache() const {
        d->measureByTick.clear();
        for (const auto& measure : d->measures) {
            d->measureByTick[measure->startTick()] = measure.get();
        }
        d->cacheValid = true;
    }

    double Staff::height() const {
        return static_cast<double>(d->lines) * 10.0;
    }

    // =============================================================================
    // Score Private Implementation
    // =============================================================================

    class Score::Private {
    public:
        QString title;
        QString composer;
        QString lyricist;
        QString copyright;
        TimeSignature defaultTimeSig;
        int ticksPerQuarter = 480;
        double tempo = 120.0;
        double pageWidth = 1224;
        double pageHeight = 1584;
        double staffDistance = 80.0;

        // Use unique_ptr for automatic cleanup with std::vector
        std::vector<std::unique_ptr<Staff>> staves;

        // Cache for quick lookup
        mutable QMap<int, Measure*> measureByTick;
        mutable bool cacheValid = false;
    };

    Score::Score(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
    }

    Score::~Score() = default;

    QString Score::title() const { return d->title; }
    QString Score::composer() const { return d->composer; }
    QString Score::lyricist() const { return d->lyricist; }
    QString Score::copyright() const { return d->copyright; }

    void Score::setTitle(const QString& title) { d->title = title; emit metadataChanged(); }
    void Score::setComposer(const QString& comp) { d->composer = comp; emit metadataChanged(); }
    void Score::setLyricist(const QString& lyr) { d->lyricist = lyr; emit metadataChanged(); }
    void Score::setCopyright(const QString& copy) { d->copyright = copy; emit metadataChanged(); }

    Staff* Score::addStaff(const QString& name) {
        auto staff = std::make_unique<Staff>(name, this);
        Staff* ptr = staff.get();
        d->staves.push_back(std::move(staff));
        d->cacheValid = false;
        emit structureChanged();
        return ptr;
    }

    void Score::removeStaff(int index) {
        if (index < 0 || index >= static_cast<int>(d->staves.size())) return;

        d->staves.erase(d->staves.begin() + index);
        d->cacheValid = false;
        emit structureChanged();
    }

    Staff* Score::staffAtY(double y) const {
        double currentY = 0.0;
        for (const auto& staff : d->staves) {
            if (y >= currentY && y <= currentY + staff->height()) {
                return staff.get();
            }
            currentY += staff->height() + d->staffDistance;
        }
        return d->staves.empty() ? nullptr : d->staves[0].get();
    }

    TimeSignature Score::defaultTimeSignature() const { return d->defaultTimeSig; }
    void Score::setDefaultTimeSignature(const TimeSignature& ts) { d->defaultTimeSig = ts; }

    int Score::ticksPerQuarter() const { return d->ticksPerQuarter; }
    void Score::setTicksPerQuarter(int ticks) { d->ticksPerQuarter = ticks; }

    double Score::tempo() const { return d->tempo; }
    void Score::setTempo(double t) { d->tempo = t; }

    double Score::pageWidth() const { return d->pageWidth; }
    double Score::pageHeight() const { return d->pageHeight; }
    double Score::staffDistance() const { return d->staffDistance; }
    void Score::setPageSize(double w, double h) { d->pageWidth = w; d->pageHeight = h; }

    int Score::totalTicks() const {
        int total = 0;
        for (const auto& staff : d->staves) {
            for (const auto& measure : staff->measures()) {
                total = qMax(total, measure->startTick() + measure->lengthTicks());
            }
        }
        return total;
    }

    Measure* Score::measureAtTick(int tick) {
        if (d->staves.empty()) return nullptr;

        if (!d->cacheValid) {
            rebuildCache();
        }

        auto it = d->measureByTick.lowerBound(tick);
        if (it != d->measureByTick.begin()) {
            --it;
            if (tick >= it.value()->startTick() &&
                tick < it.value()->startTick() + it.value()->lengthTicks()) {
                return it.value();
                }
        }
        return nullptr;
    }

    Note* Score::noteAtTick(int tick, int staffIdx, int voice) {
        if (staffIdx < 0 || staffIdx >= static_cast<int>(d->staves.size())) return nullptr;

        Staff* staff = d->staves[staffIdx].get();
        Measure* measure = staff->measureAtTick(tick);
        if (!measure) return nullptr;

        return measure->noteAtTick(tick - measure->startTick(), voice);
    }

    void Score::rebuildCache() const {
        d->measureByTick.clear();
        if (!d->staves.empty()) {
            for (const auto& measure : d->staves[0]->measures()) {
                d->measureByTick[measure->startTick()] = measure.get();
            }
        }
        d->cacheValid = true;
    }

    // =============================================================================
    // Safe MIDI I/O with Error Handling
    // =============================================================================

    namespace {
        class MidiFileReader {
        private:
            const quint8* m_data;
            const quint8* m_end;
            size_t m_size;
            size_t m_position = 0;

        public:
            explicit MidiFileReader(const QByteArray& data)
            : m_data(reinterpret_cast<const quint8*>(data.constData()))
            , m_end(m_data + data.size())
            , m_size(data.size()) {
            }

            bool readVLQ(int& value) {
                value = 0;
                int bytesRead = 0;

                while (m_position < m_size && bytesRead < 4) {
                    quint8 byte = m_data[m_position++];
                    value = (value << 7) | (byte & 0x7F);
                    bytesRead++;

                    if (!(byte & 0x80)) {
                        return true;
                    }
                }

                return false;  // Invalid VLQ or overflow
            }

            bool readBytes(void* dest, size_t count) {
                if (m_position + count > m_size) {
                    return false;
                }

                memcpy(dest, m_data + m_position, count);
                m_position += count;
                return true;
            }

            template<typename T>
            bool readBE(T& value) {
                static_assert(std::is_integral<T>::value, "Integral type required");

                if (m_position + sizeof(T) > m_size) {
                    return false;
                }

                value = 0;
                for (size_t i = 0; i < sizeof(T); ++i) {
                    value = (value << 8) | m_data[m_position + i];
                }
                m_position += sizeof(T);
                return true;
            }

            bool skip(size_t count) {
                if (m_position + count > m_size) {
                    return false;
                }
                m_position += count;
                return true;
            }

            size_t remaining() const {
                return m_size - m_position;
            }

            const quint8* current() const {
                return m_data + m_position;
            }

            size_t position() const { return m_position; }
        };

        class MidiFileWriter {
        private:
            QByteArray& m_data;

        public:
            explicit MidiFileWriter(QByteArray& data) : m_data(data) {}

            void writeVLQ(int value) {
                if (value == 0) {
                    m_data.append('\x00');
                    return;
                }

                QByteArray bytes;
                while (value > 0) {
                    bytes.prepend(static_cast<char>((value & 0x7F) | 0x80));
                    value >>= 7;
                }

                if (!bytes.isEmpty()) {
                    bytes[bytes.size() - 1] &= 0x7F;
                    m_data.append(bytes);
                }
            }

            template<typename T>
            void writeBE(T value) {
                static_assert(std::is_integral<T>::value, "Integral type required");

                for (int i = sizeof(T) - 1; i >= 0; --i) {
                    m_data.append(static_cast<char>((value >> (i * 8)) & 0xFF));
                }
            }

            void writeBytes(const void* data, size_t count) {
                m_data.append(static_cast<const char*>(data), static_cast<int>(count));
            }
        };

        // Safe duration conversion with bounds checking
        Duration durationFromTicks(int ticks, int ppq) {
            Duration d;

            if (ppq <= 0) {
                d.type = DurationType::Quarter;
                return d;
            }

            // Calculate quarter note equivalent
            double quarters = static_cast<double>(ticks) / ppq;

            // Map to nearest standard duration
            const struct {
                double quarters;
                DurationType type;
            } durationMap[] = {
                {4.0, DurationType::Whole},
                {2.0, DurationType::Half},
                {1.0, DurationType::Quarter},
                {0.5, DurationType::Eighth},
                {0.25, DurationType::Sixteenth},
                {0.125, DurationType::ThirtySecond},
                {0.0625, DurationType::SixtyFourth}
            };

            double minDiff = std::numeric_limits<double>::max();
            for (const auto& entry : durationMap) {
                double diff = std::abs(quarters - entry.quarters);
                if (diff < minDiff) {
                    minDiff = diff;
                    d.type = entry.type;
                }
            }

            // Detect dotted notes
            for (const auto& entry : durationMap) {
                double dotted = entry.quarters * 1.5;
                if (std::abs(quarters - dotted) < 0.01) {
                    d.type = entry.type;
                    d.dots = 1;
                    break;
                }
            }

            return d;
        }
    }

    // =============================================================================
    // Score::loadMIDI - With comprehensive error handling
    // =============================================================================

    bool Score::loadMIDI(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            emit error(tr("Cannot open file: %1").arg(file.errorString()));
            return false;
        }

        QByteArray data = file.readAll();
        if (data.size() < 14) {
            emit error(tr("File too small to be valid MIDI"));
            return false;
        }

        MidiFileReader reader(data);

        // Read header
        char headerId[4];
        if (!reader.readBytes(headerId, 4) || memcmp(headerId, "MThd", 4) != 0) {
            emit error(tr("Invalid MIDI header"));
            return false;
        }

        uint32_t headerLen;
        if (!reader.readBE(headerLen) || headerLen < 6) {
            emit error(tr("Invalid header length"));
            return false;
        }

        uint16_t format, trackCount, timeDivision;
        if (!reader.readBE(format) || !reader.readBE(trackCount) || !reader.readBE(timeDivision)) {
            emit error(tr("Failed to read header data"));
            return false;
        }

        // Parse time division
        if (timeDivision & 0x8000) {
            int fps = -(static_cast<qint8>(timeDivision >> 8));
            int tpf = timeDivision & 0xFF;
            d->ticksPerQuarter = fps * tpf;
        } else {
            d->ticksPerQuarter = timeDivision;
        }

        // Skip remaining header if any
        if (headerLen > 6) {
            reader.skip(headerLen - 6);
        }

        // Read tracks
        for (int t = 0; t < trackCount && reader.remaining() >= 8; ++t) {
            char trackId[4];
            if (!reader.readBytes(trackId, 4) || memcmp(trackId, "MTrk", 4) != 0) {
                emit error(tr("Invalid track header"));
                return false;
            }

            uint32_t trackLen;
            if (!reader.readBE(trackLen) || trackLen > reader.remaining()) {
                emit error(tr("Invalid track length"));
                return false;
            }

            // Create staff and measure
            auto* staff = addStaff(QString("Track %1").arg(t + 1));
            auto* measure = staff->addMeasure(1);

            int currentTick = 0;
            int runningStatus = 0;
            size_t trackEnd = reader.position() + trackLen;

            struct PendingNote {
                int note;
                int velocity;
                int startTick;
                int channel;
            };
            QVector<PendingNote> pendingNotes;

            while (reader.position() < trackEnd && reader.remaining() > 0) {
                int delta;
                if (!reader.readVLQ(delta)) {
                    emit error(tr("Invalid delta time"));
                    break;
                }
                currentTick += delta;

                if (reader.remaining() == 0) break;

                quint8 statusByte;
                if (!reader.readBytes(&statusByte, 1)) break;

                if (statusByte & 0x80) {
                    runningStatus = statusByte;
                } else {
                    // Running status - step back
                    reader.skip(-1);
                    statusByte = static_cast<quint8>(runningStatus);
                }

                int type = (statusByte >> 4) & 0x0F;
                int channel = statusByte & 0x0F;

                switch (type) {
                    case 0x08:  // Note Off
                    case 0x09: { // Note On
                        quint8 note, velocity;
                        if (!reader.readBytes(&note, 1) || !reader.readBytes(&velocity, 1)) {
                            break;
                        }

                        if (type == 0x09 && velocity > 0) {
                            // Note On
                            pendingNotes.append({note, velocity, currentTick, channel});
                        } else {
                            // Note Off
                            for (int k = 0; k < pendingNotes.size(); ++k) {
                                if (pendingNotes[k].note == note &&
                                    pendingNotes[k].channel == channel) {
                                    Note n;
                                n.pitch = Pitch(note);
                                n.tickPosition = pendingNotes[k].startTick - measure->startTick();
                                int durTicks = currentTick - pendingNotes[k].startTick;
                                n.duration = durationFromTicks(durTicks, d->ticksPerQuarter);
                                n.velocity = pendingNotes[k].velocity;
                                n.voice = channel;
                                measure->addNote(n);
                                pendingNotes.removeAt(k);
                                break;
                                    }
                            }
                        }
                        break;
                    }

                    case 0x0A:  // Poly Pressure
                    case 0x0B:  // Control Change
                    case 0x0E:  // Pitch Bend
                        reader.skip(2);
                        break;

                    case 0x0C:  // Program Change
                    case 0x0D:  // Channel Pressure
                        reader.skip(1);
                        break;

                    case 0x0F: { // System Exclusive
                        if (statusByte == 0xFF) { // Meta event
                            quint8 metaType;
                            if (!reader.readBytes(&metaType, 1)) break;

                            int metaLen;
                            if (!reader.readVLQ(metaLen) || metaLen > static_cast<int>(reader.remaining())) {
                                break;
                            }

                            if (metaType == 0x51 && metaLen >= 3) { // Tempo
                                quint8 tempoData[3];
                                if (reader.readBytes(tempoData, 3)) {
                                    int microsec = (tempoData[0] << 16) |
                                    (tempoData[1] << 8) |
                                    tempoData[2];
                                    d->tempo = (microsec > 0) ? 60000000.0 / microsec : 120.0;
                                }
                            } else {
                                reader.skip(metaLen);
                            }
                        } else {
                            // Skip sysex data
                            while (reader.position() < trackEnd) {
                                quint8 b;
                                if (!reader.readBytes(&b, 1)) break;
                                if (b == 0xF7) break;
                            }
                        }
                        break;
                    }
                }
            }

            // Flush pending notes as quarter notes
            for (const auto& pn : pendingNotes) {
                Note n;
                n.pitch = Pitch(pn.note);
                n.tickPosition = pn.startTick - measure->startTick();
                n.duration.type = DurationType::Quarter;
                n.velocity = pn.velocity;
                n.voice = pn.channel;
                measure->addNote(n);
            }

            reader.skip(trackEnd - reader.position());
        }

        emit structureChanged();
        return true;
    }

    // =============================================================================
    // Score::saveMIDI - With comprehensive validation
    // =============================================================================

    bool Score::saveMIDI(const QString& path) const {
        if (d->staves.empty()) {
            const_cast<Score*>(this)->emit error(tr("No tracks to save"));
            return false;
        }

        QByteArray data;
        MidiFileWriter writer(data);

        // Header
        writer.writeBytes("MThd", 4);
        writer.writeBE<uint32_t>(6);
        writer.writeBE<uint16_t>(1);  // Format 1
        writer.writeBE<uint16_t>(static_cast<uint16_t>(d->staves.size()));
        writer.writeBE<uint16_t>(static_cast<uint16_t>(d->ticksPerQuarter));

        // Tracks
        for (const auto& staff : d->staves) {
            writer.writeBytes("MTrk", 4);

            // Placeholder for track length
            size_t trackLenPos = data.size();
            writer.writeBE<uint32_t>(0);

            size_t trackStart = data.size();
            int lastTick = 0;

            // Tempo meta event
            writer.writeVLQ(0);
            writer.writeBE<uint8_t>(0xFF);
            writer.writeBE<uint8_t>(0x51);
            writer.writeBE<uint8_t>(3);

            int microsec = (d->tempo > 0) ?
            static_cast<int>(60000000.0 / d->tempo) : 500000;
            writer.writeBE<uint8_t>((microsec >> 16) & 0xFF);
            writer.writeBE<uint8_t>((microsec >> 8) & 0xFF);
            writer.writeBE<uint8_t>(microsec & 0xFF);

            // Collect events
            struct MidiEvent {
                int tick;
                bool isNoteOn;
                quint8 note;
                quint8 velocity;
                quint8 channel;

                bool operator<(const MidiEvent& other) const {
                    return tick < other.tick;
                }
            };

            QVector<MidiEvent> events;
            int midiChannel = staff->midiChannel() & 0x0F;

            for (const auto& measure : staff->measures()) {
                for (const auto& note : measure->notes()) {
                    if (note.isRest) continue;

                    int noteStart = measure->startTick() + note.tickPosition;
                    int durTicks = static_cast<int>(note.duration.toQuarterNotes() * d->ticksPerQuarter);
                    int noteEnd = noteStart + qMax(durTicks, 1);

                    MidiEvent onEvent;
                    onEvent.tick = noteStart;
                    onEvent.isNoteOn = true;
                    onEvent.note = static_cast<quint8>(note.pitch.midiNote);
                    onEvent.velocity = static_cast<quint8>(note.velocity);
                    onEvent.channel = static_cast<quint8>(midiChannel);
                    events.append(onEvent);

                    MidiEvent offEvent;
                    offEvent.tick = noteEnd;
                    offEvent.isNoteOn = false;
                    offEvent.note = static_cast<quint8>(note.pitch.midiNote);
                    offEvent.velocity = 0;
                    offEvent.channel = static_cast<quint8>(midiChannel);
                    events.append(offEvent);
                }
            }

            std::sort(events.begin(), events.end());

            // Write events
            for (const auto& ev : events) {
                writer.writeVLQ(ev.tick - lastTick);
                lastTick = ev.tick;

                writer.writeBE<uint8_t>((ev.isNoteOn ? 0x90 : 0x80) | ev.channel);
                writer.writeBE<uint8_t>(ev.note);
                writer.writeBE<uint8_t>(ev.velocity);
            }

            // End of track
            writer.writeVLQ(0);
            writer.writeBE<uint8_t>(0xFF);
            writer.writeBE<uint8_t>(0x2F);
            writer.writeBE<uint8_t>(0);

            // Write track length
            uint32_t trackLen = static_cast<uint32_t>(data.size() - trackStart);
            memcpy(data.data() + trackLenPos, &trackLen, 4);

            // Convert to big-endian if necessary
            if (QSysInfo::ByteOrder == QSysInfo::LittleEndian) {
                std::reverse(data.data() + trackLenPos, data.data() + trackLenPos + 4);
            }
        }

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            const_cast<Score*>(this)->emit error(tr("Cannot open file for writing: %1").arg(file.errorString()));
            return false;
        }

        return file.write(data) == data.size();
    }

    // =============================================================================
    // Score::renderToPCM (simplified stub)
    // =============================================================================

    QByteArray Score::renderToPCM(int sampleRate) {
        Q_UNUSED(sampleRate)
        return QByteArray();
    }

    // =============================================================================
    // EngravingSettings
    // =============================================================================

    EngravingSettings& EngravingSettings::defaults() {
        static EngravingSettings instance;
        return instance;
    }

    // =============================================================================
    // ScoreRenderer (simplified stub)
    // =============================================================================

    ScoreRenderer::ScoreRenderer(Score* score) : m_score(score) {}

    void ScoreRenderer::render(QPainter* painter, const QRectF& rect, int startStaff, int staffCount) {
        Q_UNUSED(painter) Q_UNUSED(rect) Q_UNUSED(startStaff) Q_UNUSED(staffCount)
        // Implementation would draw the score
    }

    void ScoreRenderer::doLayout() {
        // Implementation would layout measures
    }

    double ScoreRenderer::calculateNoteY(const Note& note) const {
        Q_UNUSED(note)
        return 0.0;
    }

    // =============================================================================
    // Clip
    // =============================================================================

    Clip::Clip(ClipType type, QObject* parent) : QObject(parent), m_type(type) {}

    void Clip::snapToGrid(const TempoMap& tempoMap, int subdivisions) {
        Q_UNUSED(tempoMap) Q_UNUSED(subdivisions)
        // Implementation would snap clip to grid
    }

    // =============================================================================
    // NotationClip
    // =============================================================================

    NotationClip::NotationClip(QObject* parent) : Clip(ClipType::Notation, parent) {}

    void NotationClip::setScore(std::unique_ptr<Score> score) {
        m_score = std::move(score);
        emit scoreChanged();
    }

    void NotationClip::createEmptyScore(const QString& title) {
        auto score = std::make_unique<Score>();
        score->setTitle(title);
        setScore(std::move(score));
    }

    bool NotationClip::isEmpty() const {
        return !m_score || m_score->staves().empty();
    }

    void NotationClip::processAudio(double position, int frames, float* buffer,
                                    int channels, int sampleRate, const TempoMap& tempo) {
        Q_UNUSED(position) Q_UNUSED(frames) Q_UNUSED(buffer)
        Q_UNUSED(channels) Q_UNUSED(sampleRate) Q_UNUSED(tempo)
        // Implementation would render audio from score
                                    }

                                    QVector<NoteEvent> NotationClip::getMidiEvents(double start, double end) {
                                        Q_UNUSED(start) Q_UNUSED(end)
                                        return QVector<NoteEvent>();
                                    }

                                    QVector<NoteEvent> NotationClip::renderToMidi() const {
                                        return QVector<NoteEvent>();
                                    }

                                    // =============================================================================
                                    // MidiClip
                                    // =============================================================================

                                    MidiClip::MidiClip(QObject* parent) : Clip(ClipType::MIDI, parent) {}

                                    void MidiClip::addEvent(const NoteEvent& event) {
                                        m_events.append(event);
                                    }

                                    void MidiClip::addNote(const Pitch& pitch, double start, const Duration& dur, int vel, int ch) {
                                        NoteEvent on, off;
                                        on.type = NoteEvent::NoteOn;
                                        on.time = start;
                                        on.channel = ch;
                                        on.note = pitch.midiNote;
                                        on.velocity = vel;
                                        on.pitch = pitch;

                                        off.type = NoteEvent::NoteOff;
                                        off.time = start + dur.toSeconds(120.0);
                                        off.channel = ch;
                                        off.note = pitch.midiNote;
                                        off.velocity = 0;

                                        addEvent(on);
                                        addEvent(off);
                                    }

                                    void MidiClip::removeEvent(int index) {
                                        if (index >= 0 && index < m_events.size()) {
                                            m_events.removeAt(index);
                                        }
                                    }

                                    void MidiClip::processAudio(double position, int frames, float* buffer,
                                                                int channels, int sampleRate, const TempoMap& tempo) {
                                        Q_UNUSED(position) Q_UNUSED(frames) Q_UNUSED(buffer)
                                        Q_UNUSED(channels) Q_UNUSED(sampleRate) Q_UNUSED(tempo)
                                        // Implementation would synthesize audio from MIDI
                                                                }

                                                                QVector<NoteEvent> MidiClip::getMidiEvents(double start, double end) {
                                                                    QVector<NoteEvent> result;
                                                                    for (const auto& ev : m_events) {
                                                                        if (ev.time >= start && ev.time < end) {
                                                                            result.append(ev);
                                                                        }
                                                                    }
                                                                    return result;
                                                                }

                                                                // =============================================================================
                                                                // AudioClip
                                                                // =============================================================================

                                                                AudioClip::AudioClip(QObject* parent) : Clip(ClipType::Audio, parent) {}

                                                                bool AudioClip::load(const QString& path) {
                                                                    Q_UNUSED(path)
                                                                    return false;
                                                                }

                                                                bool AudioClip::loadFromMemory(const QByteArray& data, int sampleRate, int channels) {
                                                                    Q_UNUSED(data) Q_UNUSED(sampleRate) Q_UNUSED(channels)
                                                                    return false;
                                                                }

                                                                void AudioClip::processAudio(double position, int frames, float* buffer,
                                                                                             int channels, int sampleRate, const TempoMap& tempo) {
                                                                    Q_UNUSED(position) Q_UNUSED(frames) Q_UNUSED(buffer)
                                                                    Q_UNUSED(channels) Q_UNUSED(sampleRate) Q_UNUSED(tempo)
                                                                    // Implementation would play audio from file
                                                                                             }

                                                                                             QVector<NoteEvent> AudioClip::getMidiEvents(double start, double end) {
                                                                                                 Q_UNUSED(start) Q_UNUSED(end)
                                                                                                 return QVector<NoteEvent>();
                                                                                             }

                                                                                             // =============================================================================
                                                                                             // Track
                                                                                             // =============================================================================

                                                                                             Track::Track(const QString& name, QObject* parent) : QObject(parent), m_name(name) {}

                                                                                             void Track::addClip(std::unique_ptr<Clip> clip) {
                                                                                                 m_clips.push_back(std::move(clip));
                                                                                                 emit clipAdded(m_clips.back().get());
                                                                                             }

                                                                                             void Track::removeClip(Clip* clip) {
                                                                                                 auto it = std::find_if(m_clips.begin(), m_clips.end(),
                                                                                                                        [clip](const auto& ptr) { return ptr.get() == clip; });
                                                                                                 if (it != m_clips.end()) {
                                                                                                     m_clips.erase(it);
                                                                                                     emit clipRemoved(clip);
                                                                                                 }
                                                                                             }

                                                                                             Clip* Track::clipAt(int index) const {
                                                                                                 if (index >= 0 && index < static_cast<int>(m_clips.size())) {
                                                                                                     return m_clips[index].get();
                                                                                                 }
                                                                                                 return nullptr;
                                                                                             }

                                                                                             QVector<Clip*> Track::clipsAt(double time) const {
                                                                                                 QVector<Clip*> result;
                                                                                                 for (const auto& clip : m_clips) {
                                                                                                     if (time >= clip->startTime() && time < clip->endTime()) {
                                                                                                         result.append(clip.get());
                                                                                                     }
                                                                                                 }
                                                                                                 return result;
                                                                                             }

                                                                                             QVector<NotationClip*> Track::notationClips() const {
                                                                                                 QVector<NotationClip*> result;
                                                                                                 for (const auto& clip : m_clips) {
                                                                                                     if (clip->type() == ClipType::Notation) {
                                                                                                         result.append(static_cast<NotationClip*>(clip.get()));
                                                                                                     }
                                                                                                 }
                                                                                                 return result;
                                                                                             }

                                                                                             QVector<ModTrackerClip*> Track::trackerClips() const {
                                                                                                 QVector<ModTrackerClip*> result;
                                                                                                 for (const auto& clip : m_clips) {
                                                                                                     if (clip->type() == ClipType::Tracker) {
                                                                                                         result.append(static_cast<ModTrackerClip*>(clip.get()));
                                                                                                     }
                                                                                                 }
                                                                                                 return result;
                                                                                             }

                                                                                             void Track::processAudio(double position, int frames, float* buffer,
                                                                                                                      int channels, int sampleRate, const TempoMap& tempo) {
                                                                                                 QWriteLocker lock(&m_lock);

                                                                                                 std::fill(buffer, buffer + frames * channels, 0.0f);

                                                                                                 for (const auto& clip : m_clips) {
                                                                                                     if (clip->isMuted()) continue;

                                                                                                     if (position >= clip->startTime() && position < clip->endTime()) {
                                                                                                         clip->processAudio(position, frames, buffer, channels, sampleRate, tempo);
                                                                                                     }
                                                                                                 }

                                                                                                 // Apply track volume/pan
                                                                                                 if (m_volume != 1.0 || m_pan != 0.0) {
                                                                                                     float leftGain = static_cast<float>(m_volume) * (1.0f - static_cast<float>(m_pan)) * 0.5f;
                                                                                                     float rightGain = static_cast<float>(m_volume) * (1.0f + static_cast<float>(m_pan)) * 0.5f;

                                                                                                     for (int i = 0; i < frames; ++i) {
                                                                                                         if (channels >= 2) {
                                                                                                             buffer[i * channels] *= leftGain;
                                                                                                             buffer[i * channels + 1] *= rightGain;
                                                                                                         } else {
                                                                                                             buffer[i] *= static_cast<float>(m_volume);
                                                                                                         }
                                                                                                     }
                                                                                                 }
                                                                                                                      }

                                                                                                                      void Track::collectMidiEvents(double position, double duration, QVector<NoteEvent>& events) {
                                                                                                                          QReadLocker lock(&m_lock);

                                                                                                                          double endTime = position + duration;

                                                                                                                          for (const auto& clip : m_clips) {
                                                                                                                              if (clip->isMuted()) continue;

                                                                                                                              double clipStart = std::max(position, clip->startTime());
                                                                                                                              double clipEnd = std::min(endTime, clip->endTime());

                                                                                                                              if (clipStart < clipEnd) {
                                                                                                                                  auto clipEvents = clip->getMidiEvents(clipStart, clipEnd);
                                                                                                                                  events.append(clipEvents);
                                                                                                                              }
                                                                                                                          }
                                                                                                                      }

                                                                                                                      // =============================================================================
                                                                                                                      // Transport
                                                                                                                      // =============================================================================

                                                                                                                      Transport::Transport(QObject* parent) : QObject(parent) {}

                                                                                                                      void Transport::setPosition(double pos) {
                                                                                                                          QMutexLocker lock(&m_positionMutex);
                                                                                                                          m_position = pos;
                                                                                                                          emit positionChanged(pos);
                                                                                                                      }

                                                                                                                      void Transport::play() {
                                                                                                                          m_state = TransportState::Playing;
                                                                                                                          emit stateChanged(m_state);
                                                                                                                      }

                                                                                                                      void Transport::stop() {
                                                                                                                          m_state = TransportState::Stopped;
                                                                                                                          m_position = 0.0;
                                                                                                                          emit stateChanged(m_state);
                                                                                                                          emit positionChanged(0.0);
                                                                                                                      }

                                                                                                                      void Transport::pause() {
                                                                                                                          m_state = TransportState::Paused;
                                                                                                                          emit stateChanged(m_state);
                                                                                                                      }

                                                                                                                      void Transport::togglePlay() {
                                                                                                                          if (m_state == TransportState::Playing) {
                                                                                                                              pause();
                                                                                                                          } else {
                                                                                                                              play();
                                                                                                                          }
                                                                                                                      }

                                                                                                                      void Transport::setTempo(double bpm) {
                                                                                                                          m_tempo = bpm;
                                                                                                                          emit tempoChanged(bpm);
                                                                                                                      }

                                                                                                                      void Transport::seekToBeat(double beat) {
                                                                                                                          setPosition(m_tempoMap.beatsToSeconds(beat));
                                                                                                                      }

                                                                                                                      double Transport::beatToTime(double beat) const {
                                                                                                                          return m_tempoMap.beatsToSeconds(beat);
                                                                                                                      }

                                                                                                                      double Transport::timeToBeat(double time) const {
                                                                                                                          return m_tempoMap.secondsToBeats(time);
                                                                                                                      }

                                                                                                                      void Transport::setLoopRange(double start, double end) {
                                                                                                                          m_loopStart = start;
                                                                                                                          m_loopEnd = end;
                                                                                                                          m_looping = (start >= 0 && end > start);
                                                                                                                      }

                                                                                                                      // =============================================================================
                                                                                                                      // DAWEngine
                                                                                                                      // =============================================================================

                                                                                                                      DAWEngine::DAWEngine(QObject* parent) : QObject(parent) {}

                                                                                                                      Track* DAWEngine::addTrack(const QString& name) {
                                                                                                                          auto track = std::make_unique<Track>(name.isEmpty() ? tr("Track %1").arg(m_tracks.size() + 1) : name, this);
                                                                                                                          Track* ptr = track.get();
                                                                                                                          m_tracks.push_back(std::move(track));
                                                                                                                          emit trackAdded(ptr);
                                                                                                                          return ptr;
                                                                                                                      }

                                                                                                                      void DAWEngine::removeTrack(int index) {
                                                                                                                          if (index >= 0 && index < static_cast<int>(m_tracks.size())) {
                                                                                                                              m_tracks.erase(m_tracks.begin() + index);
                                                                                                                              emit trackRemoved(index);
                                                                                                                          }
                                                                                                                      }

                                                                                                                      Track* DAWEngine::trackAt(int index) const {
                                                                                                                          if (index >= 0 && index < static_cast<int>(m_tracks.size())) {
                                                                                                                              return m_tracks[index].get();
                                                                                                                          }
                                                                                                                          return nullptr;
                                                                                                                      }

                                                                                                                      NotationClip* DAWEngine::createNotationClip(int trackIndex, const QString& name) {
                                                                                                                          auto clip = std::make_unique<NotationClip>(this);
                                                                                                                          clip->setName(name);
                                                                                                                          clip->createEmptyScore();

                                                                                                                          NotationClip* ptr = clip.get();

                                                                                                                          if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size())) {
                                                                                                                              m_tracks[trackIndex]->addClip(std::move(clip));
                                                                                                                          }

                                                                                                                          return ptr;
                                                                                                                      }

                                                                                                                      NotationClip* DAWEngine::importScore(const QString& path, int trackIndex) {
                                                                                                                          auto clip = std::make_unique<NotationClip>(this);
                                                                                                                          auto score = std::make_unique<Score>();

                                                                                                                          if (path.endsWith(".xml", Qt::CaseInsensitive) ||
                                                                                                                              path.endsWith(".musicxml", Qt::CaseInsensitive)) {
                                                                                                                              if (!score->loadMusicXML(path)) {
                                                                                                                                  return nullptr;
                                                                                                                              }
                                                                                                                              } else if (path.endsWith(".mid", Qt::CaseInsensitive) ||
                                                                                                                                  path.endsWith(".midi", Qt::CaseInsensitive)) {
                                                                                                                                  if (!score->loadMIDI(path)) {
                                                                                                                                      return nullptr;
                                                                                                                                  }
                                                                                                                                  } else {
                                                                                                                                      return nullptr;
                                                                                                                                  }

                                                                                                                                  clip->setScore(std::move(score));
                                                                                                                              clip->setName(QFileInfo(path).baseName());

                                                                                                                              NotationClip* ptr = clip.get();

                                                                                                                              if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size())) {
                                                                                                                                  m_tracks[trackIndex]->addClip(std::move(clip));
                                                                                                                              }

                                                                                                                              return ptr;
                                                                                                                      }

                                                                                                                      void DAWEngine::processAudio(float* buffer, int frames, int channels, int sampleRate) {
                                                                                                                          double position = m_transport.position();

                                                                                                                          // Clear mix buffer
                                                                                                                          if (m_mixBuffer.size() < frames * channels) {
                                                                                                                              m_mixBuffer.resize(frames * channels);
                                                                                                                          }
                                                                                                                          std::fill(m_mixBuffer.begin(), m_mixBuffer.end(), 0.0f);

                                                                                                                          // Process each track
                                                                                                                          for (const auto& track : m_tracks) {
                                                                                                                              if (track->isMuted()) continue;

                                                                                                                              std::vector<float> trackBuffer(frames * channels);
                                                                                                                              track->processAudio(position, frames, trackBuffer.data(), channels, sampleRate, *m_transport.tempoMap());

                                                                                                                              // Mix to master
                                                                                                                              for (int i = 0; i < frames * channels; ++i) {
                                                                                                                                  m_mixBuffer[i] += trackBuffer[i];
                                                                                                                              }
                                                                                                                          }

                                                                                                                          // Copy to output buffer
                                                                                                                          std::copy(m_mixBuffer.begin(), m_mixBuffer.end(), buffer);

                                                                                                                          // Soft clip
                                                                                                                          for (int i = 0; i < frames * channels; ++i) {
                                                                                                                              buffer[i] = std::tanh(buffer[i]);
                                                                                                                          }
                                                                                                                      }

                                                                                                                      bool DAWEngine::saveProject(const QString& path) {
                                                                                                                          Q_UNUSED(path)
                                                                                                                          return false;
                                                                                                                      }

                                                                                                                      bool DAWEngine::loadProject(const QString& path) {
                                                                                                                          Q_UNUSED(path)
                                                                                                                          return false;
                                                                                                                      }

} // namespace Aegis
