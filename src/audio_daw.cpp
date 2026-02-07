// audio_daw.cpp - DAW Engine Implementation with Notation Integration

#include "audio_daw.h"
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QPainter>
#include <algorithm>

namespace Aegis {

    // =============================================================================
    // Pitch Implementation
    // =============================================================================

    void Pitch::fromMidi(int midi) {
        midiNote = midi;
        pitchClass = static_cast<PitchClass>(midi % 12);
        octave = (midi / 12) - 1;
        accidental = Accidental::Natural;
    }

    void Pitch::toMidi() {
        int pc = static_cast<int>(pitchClass);
        if (accidental == Accidental::Sharp) pc++;
        else if (accidental == Accidental::Flat) pc--;
        else if (accidental == Accidental::DoubleSharp) pc += 2;
        else if (accidental == Accidental::DoubleFlat) pc -= 2;

        while (pc < 0) { pc += 12; octave--; }
        while (pc > 11) { pc -= 12; octave++; }

        midiNote = (octave + 1) * 12 + pc;
    }

    QString Pitch::toString() const {
        static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return QString("%1%2").arg(names[static_cast<int>(pitchClass)]).arg(octave);
    }

    Pitch Pitch::fromString(const QString& str) {
        Pitch p;
        // Parse pitch string like "C#4" or "Bb3"
        if (str.isEmpty()) return p;

        QChar noteChar = str[0].toUpper();
        int basePitch = 0;
        switch (noteChar.unicode()) {
            case 'C': basePitch = 0; break;
            case 'D': basePitch = 2; break;
            case 'E': basePitch = 4; break;
            case 'F': basePitch = 5; break;
            case 'G': basePitch = 7; break;
            case 'A': basePitch = 9; break;
            case 'B': basePitch = 11; break;
            default: return p;
        }

        int pos = 1;
        Accidental acc = Accidental::Natural;

        while (pos < str.length() && (str[pos] == '#' || str[pos] == 'b')) {
            if (str[pos] == '#') {
                basePitch++;
                acc = (acc == Accidental::Sharp) ? Accidental::DoubleSharp : Accidental::Sharp;
            } else {
                basePitch--;
                acc = (acc == Accidental::Flat) ? Accidental::DoubleFlat : Accidental::Flat;
            }
            pos++;
        }

        int oct = 4;
        if (pos < str.length()) {
            bool ok;
            int parsedOct = str.mid(pos).toInt(&ok);
            if (ok) oct = parsedOct;
        }

        p.pitchClass = static_cast<PitchClass>((basePitch + 12) % 12);
        p.octave = oct + (basePitch < 0 ? -1 : basePitch / 12);
        p.accidental = acc;
        p.toMidi();

        return p;
    }

    int Pitch::compare(const Pitch& other) const {
        if (midiNote != other.midiNote) return midiNote - other.midiNote;
        return static_cast<int>(accidental) - static_cast<int>(other.accidental);
    }

    // =============================================================================
    // Duration Implementation
    // =============================================================================

    double Duration::toQuarterNotes() const {
        static const double factors[] = {16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
            0.0625, 0.03125, 0.015625, 0.0078125};
            double base = factors[static_cast<int>(type)];

            // Add dots
            double dotValue = base * 0.5;
            for (int i = 0; i < dots; ++i) {
                base += dotValue;
                dotValue *= 0.5;
            }

            // Apply tuplet
            if (numerator != 1 || denominator != 1) {
                base *= static_cast<double>(numerator) / denominator;
            }

            return base;
    }

    double Duration::toSeconds(double tempoBpm) const {
        double quarters = toQuarterNotes();
        return quarters * (60.0 / tempoBpm);
    }

    QString Duration::toString() const {
        static const char* names[] = {"maxima", "long", "breve", "whole", "half", "quarter",
            "eighth", "16th", "32nd", "64th", "128th", "256th"};
            QString result = names[static_cast<int>(type)];
            for (int i = 0; i < dots; ++i) result += ".";
            if (numerator != 1 || denominator != 1) {
                result += QString(" (%1:%2)").arg(numerator).arg(denominator);
            }
            return result;
    }

    Duration Duration::fromString(const QString& str) {
        Duration d;
        // Parse duration string
        if (str.startsWith("maxima")) d.type = Type::Maxima;
        else if (str.startsWith("long")) d.type = Type::Long;
        else if (str.startsWith("breve")) d.type = Type::Breve;
        else if (str.startsWith("whole")) d.type = Type::Whole;
        else if (str.startsWith("half")) d.type = Type::Half;
        else if (str.startsWith("quarter")) d.type = Type::Quarter;
        else if (str.startsWith("eighth")) d.type = Type::Eighth;
        else if (str.startsWith("16th")) d.type = Type::Sixteenth;
        else if (str.startsWith("32nd")) d.type = Type::ThirtySecond;
        else if (str.startsWith("64th")) d.type = Type::SixtyFourth;
        else if (str.startsWith("128th")) d.type = Type::HundredTwentyEighth;
        else if (str.startsWith("256th")) d.type = Type::TwoHundredFiftySixth;

        // Count dots
        for (int i = 0; i < str.length(); ++i) {
            if (str[i] == '.') d.dots++;
        }

        return d;
    }

    bool Duration::operator==(const Duration& other) const {
        return type == other.type && dots == other.dots &&
        numerator == other.numerator && denominator == other.denominator;
    }

    // =============================================================================
    // Note Implementation
    // =============================================================================

    double Note::playDurationTicks(double tempo) const {
        double baseTicks = duration.toQuarterNotes() * 480.0; // 480 PPQ
        return baseTicks * playbackDurationMultiplier;
    }

    QString Note::toLilyPond() const {
        if (isRest) {
            return QString("r%1").arg(duration.toString());
        }

        static const char* pitchNames[] = {"c", "cis", "d", "dis", "e", "f", "fis", "g", "gis", "a", "ais", "b"};
        QString result = pitchNames[pitch.pitchClass % 12];

        // Octave
        int octDiff = pitch.octave - 3;
        if (octDiff > 0) {
            for (int i = 0; i < octDiff; ++i) result += "'";
        } else if (octDiff < 0) {
            for (int i = 0; i < -octDiff; ++i) result += ",";
        }

        // Duration
        result += duration.toString().replace("quarter", "4").replace("eighth", "8")
        .replace("half", "2").replace("whole", "1").replace("16th", "16")
        .replace("32nd", "32").replace("64th", "64");

        return result;
    }

    // =============================================================================
    // Measure Implementation
    // =============================================================================

    Measure::Measure(int number, QObject* parent)
    : QObject(parent), m_number(number) {}

    void Measure::addNote(const Note& note, int voice) {
        Note n = note;
        n.voice = voice;
        n.measure = this;

        // Calculate tick position if not set
        if (n.tickPosition == 0 && !notes.isEmpty()) {
            int lastTick = 0;
            for (const auto& existing : notes) {
                lastTick = qMax(lastTick, existing.tickPosition +
                static_cast<int>(existing.duration.toQuarterNotes() * 480));
            }
            n.tickPosition = lastTick;
        }

        notes.append(n);

        // Keep sorted by tick position
        std::sort(notes.begin(), notes.end(),
                  [](const Note& a, const Note& b) { return a.tickPosition < b.tickPosition; });

        emit noteAdded(n);
        emit modified();
    }

    void Measure::removeNote(int index) {
        if (index >= 0 && index < notes.size()) {
            notes.removeAt(index);
            emit noteRemoved(index);
            emit modified();
        }
    }

    Note* Measure::noteAtTick(int tick, int voice) {
        for (auto& note : notes) {
            if (note.tickPosition == tick && (voice < 0 || note.voice == voice)) {
                return &note;
            }
        }
        return nullptr;
    }

    QVector<Note*> Measure::notesInRange(int startTick, int endTick, int voice) {
        QVector<Note*> result;
        for (auto& note : notes) {
            if (note.tickPosition >= startTick && note.tickPosition < endTick) {
                if (voice < 0 || note.voice == voice) {
                    result.append(&note);
                }
            }
        }
        return result;
    }

    int Measure::tickToPixel(int tick) const {
        return static_cast<int>((tick / static_cast<double>(m_lengthTicks)) * width);
    }

    int Measure::pixelToTick(double x) const {
        return static_cast<int>((x / width) * m_lengthTicks);
    }

    int Measure::filledTicks() const {
        int maxTick = 0;
        for (const auto& note : notes) {
            int noteEnd = note.tickPosition + static_cast<int>(note.duration.toQuarterNotes() * 480);
            maxTick = qMax(maxTick, noteEnd);
        }
        return maxTick;
    }

    int Measure::remainingTicks() const {
        return m_lengthTicks - filledTicks();
    }

    bool Measure::isFull() const {
        return filledTicks() >= m_lengthTicks;
    }

    double Measure::tempoAt(int tick) const {
        double t = 120.0;
        for (const auto& tempo : tempos) {
            // Simplified: return last tempo
            Q_UNUSED(tick)
            t = tempo.bpm;
        }
        return t;
    }

    double Measure::absoluteTimeAt(int tick) const {
        // This would need cumulative calculation from score start
        return tick / 480.0 * (60.0 / tempoAt(tick));
    }

    // =============================================================================
    // Staff Implementation
    // =============================================================================

    Staff::Staff(const QString& name, QObject* parent)
    : QObject(parent), m_name(name) {}

    void Staff::setTranspose(int chromatic, int diatonic) {
        m_transposeChromatic = chromatic;
        m_transposeDiatonic = diatonic;
    }

    Measure* Staff::addMeasure(int number) {
        auto measure = std::make_unique<Measure>(number, this);
        Measure* ptr = measure.get();

        // Set start tick based on previous measures
        if (!measures.isEmpty()) {
            auto& last = measures.last();
            ptr->setStartTick(last->startTick() + last->lengthTicks());
        }

        measures.append(std::move(measure));
        emit measureAdded(ptr);
        return ptr;
    }

    void Staff::removeMeasure(int index) {
        if (index >= 0 && index < measures.size()) {
            measures.removeAt(index);
            emit measureRemoved(index);

            // Renumber remaining measures
            for (int i = index; i < measures.size(); ++i) {
                // Update measure numbers
            }
        }
    }

    Measure* Staff::measureAtTick(int tick) {
        for (auto& m : measures) {
            if (tick >= m->startTick() && tick < m->startTick() + m->lengthTicks()) {
                return m.get();
            }
        }
        return nullptr;
    }

    double Staff::height() const {
        return (m_lines - 1) * EngravingSettings::defaults().spatium + 50.0;
    }

    // =============================================================================
    // Score Implementation
    // =============================================================================

    Score::Score(QObject* parent) : QObject(parent) {}

    Staff* Score::addStaff(const QString& name) {
        auto staff = std::make_unique<Staff>(name, this);
        Staff* ptr = staff.get();
        staves.append(std::move(staff));
        emit structureChanged();
        return ptr;
    }

    void Score::removeStaff(int index) {
        if (index >= 0 && index < staves.size()) {
            staves.removeAt(index);
            emit structureChanged();
        }
    }

    Staff* Score::staffAtY(double y) const {
        double currentY = 0;
        for (const auto& staff : staves) {
            if (y >= currentY && y < currentY + staff->height()) {
                return staff.get();
            }
            currentY += staff->height() + m_staffDistance;
        }
        return nullptr;
    }

    int Score::totalTicks() const {
        if (staves.isEmpty() || staves.first()->measures.isEmpty()) return 0;
        int total = 0;
        for (const auto& m : staves.first()->measures) {
            total += m->lengthTicks();
        }
        return total;
    }

    Measure* Score::measureAtTick(int tick) {
        for (auto& staff : staves) {
            Measure* m = staff->measureAtTick(tick);
            if (m) return m;
        }
        return nullptr;
    }

    Note* Score::noteAtTick(int tick, int staffIdx, int voice) {
        if (staffIdx < 0 || staffIdx >= staves.size()) return nullptr;
        Measure* m = staves[staffIdx]->measureAtTick(tick);
        if (m) {
            return m->noteAtTick(tick - m->startTick(), voice);
        }
        return nullptr;
    }

    // =============================================================================
    // MusicXML Import/Export
    // =============================================================================

    bool Score::loadMusicXML(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return false;

        QXmlStreamReader xml(&file);
        staves.clear();

        Staff* currentStaff = nullptr;
        Measure* currentMeasure = nullptr;
        int currentTick = 0;

        while (!xml.atEnd()) {
            xml.readNext();

            if (xml.isStartElement()) {
                QString name = xml.name().toString();

                if (name == "part") {
                    QString partName = xml.attributes().value("id").toString();
                    currentStaff = addStaff(partName);
                    currentTick = 0;

                } else if (name == "measure" && currentStaff) {
                    int measureNum = xml.attributes().value("number").toInt();
                    currentMeasure = currentStaff->addMeasure(measureNum);
                    currentTick = 0;

                    // Parse attributes if present
                    while (!(xml.isEndElement() && xml.name() == "measure")) {
                        xml.readNext();

                        if (xml.isStartElement()) {
                            QString elName = xml.name().toString();

                            if (elName == "note") {
                                Note note = parseMusicXMLNote(xml);
                                note.tickPosition = currentTick;
                                note.measure = currentMeasure;
                                currentMeasure->notes.append(note);

                                if (!note.isRest && !note.isChord) {
                                    currentTick += static_cast<int>(note.duration.toQuarterNotes() * 480);
                                }

                            } else if (elName == "attributes") {
                                parseMusicXMLAttributes(xml, currentMeasure);
                            }
                        }
                    }

                } else if (name == "work-title") {
                    m_title = xml.readElementText();

                } else if (name == "creator") {
                    QString type = xml.attributes().value("type").toString();
                    if (type == "composer") m_composer = xml.readElementText();
                    else if (type == "lyricist") m_lyricist = xml.readElementText();
                }
            }
        }

        emit modified();
        return !xml.hasError();
    }

    Note Score::parseMusicXMLNote(QXmlStreamReader& xml) {
        Note note;
        bool isChord = false;

        while (!(xml.isEndElement() && xml.name() == "note")) {
            xml.readNext();
            if (!xml.isStartElement()) continue;

            QString name = xml.name().toString();

            if (name == "pitch") {
                parseMusicXMLPitch(xml, note.pitch);
            } else if (name == "rest") {
                note.isRest = true;
            } else if (name == "duration") {
                int divisions = xml.readElementText().toInt();
                // Convert divisions to Duration (assuming 480 divisions per quarter)
                double quarters = divisions / 480.0;
                note.duration = durationFromQuarters(quarters);
            } else if (name == "chord") {
                isChord = true;
                note.isChord = true;
            } else if (name == "velocity") {
                note.velocity = xml.readElementText().toInt();
            } else if (name == "type") {
                // Override duration type if specified
                QString typeStr = xml.readElementText();
                note.duration.type = parseDurationType(typeStr);
            } else if (name == "dot") {
                note.duration.dots++;
            } else if (name == "notations") {
                parseMusicXMLNotations(xml, note);
            } else if (name == "lyric") {
                parseMusicXMLLyric(xml, note);
            }
        }

        return note;
    }

    void Score::parseMusicXMLPitch(QXmlStreamReader& xml, Pitch& pitch) {
        QString step;
        int alter = 0;
        int octave = 4;

        while (!(xml.isEndElement() && xml.name() == "pitch")) {
            xml.readNext();
            if (!xml.isStartElement()) continue;

            QString name = xml.name().toString();
            if (name == "step") step = xml.readElementText();
            else if (name == "alter") alter = xml.readElementText().toInt();
            else if (name == "octave") octave = xml.readElementText().toInt();
        }

        // Convert step to pitch class
        int basePitch = 0;
        if (step == "C") basePitch = 0;
        else if (step == "D") basePitch = 2;
        else if (step == "E") basePitch = 4;
        else if (step == "F") basePitch = 5;
        else if (step == "G") basePitch = 7;
        else if (step == "A") basePitch = 9;
        else if (step == "B") basePitch = 11;

        basePitch += alter;
        if (basePitch < 0) { basePitch += 12; octave--; }
        else if (basePitch > 11) { basePitch -= 12; octave++; }

        pitch.pitchClass = static_cast<PitchClass>(basePitch);
        pitch.octave = octave - 1; // MusicXML octave vs MIDI octave
        pitch.accidental = (alter > 0) ? Accidental::Sharp :
        (alter < 0) ? Accidental::Flat : Accidental::Natural;
        pitch.toMidi();
    }

    void Score::parseMusicXMLAttributes(QXmlStreamReader& xml, Measure* measure) {
        while (!(xml.isEndElement() && xml.name() == "attributes")) {
            xml.readNext();
            if (!xml.isStartElement()) continue;

            QString name = xml.name().toString();

            if (name == "divisions") {
                int div = xml.readElementText().toInt();
                // Store divisions per quarter note
            } else if (name == "time") {
                parseMusicXMLTime(xml, measure);
            } else if (name == "key") {
                parseMusicXMLKey(xml, measure);
            } else if (name == "clef") {
                parseMusicXMLClef(xml, measure);
            }
        }
    }

    void Score::parseMusicXMLTime(QXmlStreamReader& xml, Measure* measure) {
        int beats = 4;
        int beatType = 4;

        while (!(xml.isEndElement() && xml.name() == "time")) {
            xml.readNext();
            if (!xml.isStartElement()) continue;

            QString name = xml.name().toString();
            if (name == "beats") beats = xml.readElementText().toInt();
            else if (name == "beat-type") beatType = xml.readElementText().toInt();
        }

        TimeSignature ts;
        ts.numerator = beats;
        ts.denominator = beatType;
        measure->timeSigs.append(ts);
        measure->setLengthTicks(beats * (480 * 4 / beatType));
    }

    void Score::parseMusicXMLKey(QXmlStreamReader& xml, Measure* measure) {
        int fifths = 0;

        while (!(xml.isEndElement() && xml.name() == "key")) {
            xml.readNext();
            if (!xml.isStartElement()) continue;

            if (xml.name() == "fifths") {
                fifths = xml.readElementText().toInt();
            }
        }

        KeySig ks;
        // Map fifths to key signature
        ks.key = static_cast<KeySignature>(fifths + 7); // Offset to make C Major = 7
        measure->keySigs.append(ks);
    }

    void Score::parseMusicXMLClef(QXmlStreamReader& xml, Measure* measure) {
        QString sign = "G";
        int line = 2;
        int octaveChange = 0;

        while (!(xml.isEndElement() && xml.name() == "clef")) {
            xml.readNext();
            if (!xml.isStartElement()) continue;

            QString name = xml.name().toString();
            if (name == "sign") sign = xml.readElementText();
            else if (name == "line") line = xml.readElementText().toInt();
            else if (name == "clef-octave-change") octaveChange = xml.readElementText().toInt();
        }

        Clef c;
        if (sign == "G") c.type = ClefType::Treble;
        else if (sign == "F") c.type = ClefType::Bass;
        else if (sign == "C") {
            if (line == 3) c.type = ClefType::Alto;
            else if (line == 4) c.type = ClefType::Tenor;
        }
        c.octaveChange = octaveChange;
        c.staffLine = line - 1;
        measure->clefs.append(c);
    }

    void Score::parseMusicXMLNotations(QXmlStreamReader& xml, Note& note) {
        while (!(xml.isEndElement() && xml.name() == "notations")) {
            xml.readNext();
            if (!xml.isStartElement()) continue;

            QString name = xml.name().toString();

            if (name == "tied") {
                QString type = xml.attributes().value("type").toString();
                if (type == "start") note.tie = TieType::Start;
                else if (type == "stop") note.tie = TieType::Stop;
            } else if (name == "articulations") {
                parseMusicXMLArticulations(xml, note);
            } else if (name == "ornaments") {
                parseMusicXMLOrnaments(xml, note);
            } else if (name == "slur") {
                QString type = xml.attributes().value("type").toString();
                note.slurs.append(type == "start" ? SlurType::Start : SlurType::Stop);
            }
        }
    }

    void Score::parseMusicXMLArticulations(QXmlStreamReader& xml, Note& note) {
        while (!(xml.isEndElement() && xml.name() == "articulations")) {
            xml.readNext();
            if (!xml.isStartElement()) continue;

            QString name = xml.name().toString();
            if (name == "staccato") note.articulations.append(Articulation::Staccato);
            else if (name == "tenuto") note.articulations.append(Articulation::Tenuto);
            else if (name == "accent") note.articulations.append(Articulation::Accent);
            else if (name == "marcato") note.articulations.append(Articulation::Marcato);
            else if (name == "staccatissimo") note.articulations.append(Articulation::Staccatissimo);
        }
    }

    void Score::parseMusicXMLOrnaments(QXmlStreamReader& xml, Note& note) {
        while (!(xml.isEndElement() && xml.name() == "ornaments")) {
            xml.readNext();
            if (!xml.isStartElement()) continue;

            QString name = xml.name().toString();
            if (name == "trill-mark") note.ornament = Ornament::Trill;
            else if (name == "turn") note.ornament = Ornament::Turn;
            else if (name == "mordent") note.ornament = Ornament::Mordent;
            else if (name == "inverted-mordent") note.ornament = Ornament::InvertedMordent;
        }
    }

    void Score::parseMusicXMLLyric(QXmlStreamReader& xml, Note& note) {
        Lyric lyric;
        lyric.verse = xml.attributes().value("number").toInt();

        while (!(xml.isEndElement() && xml.name() == "lyric")) {
            xml.readNext();
            if (!xml.isStartElement()) continue;

            QString name = xml.name().toString();
            if (name == "text") lyric.text = xml.readElementText();
            else if (name == "syllabic") {
                QString s = xml.readElementText();
                if (s == "single") lyric.syllabic = Lyric::Single;
                else if (s == "begin") lyric.syllabic = Lyric::Begin;
                else if (s == "middle") lyric.syllabic = Lyric::Middle;
                else if (s == "end") lyric.syllabic = Lyric::End;
            }
        }

        note.lyrics.append(lyric);
    }

    Duration::Type Score::parseDurationType(const QString& str) {
        if (str == "maxima") return Duration::Type::Maxima;
        if (str == "long") return Duration::Type::Long;
        if (str == "breve") return Duration::Type::Breve;
        if (str == "whole") return Duration::Type::Whole;
        if (str == "half") return Duration::Type::Half;
        if (str == "quarter") return Duration::Type::Quarter;
        if (str == "eighth") return Duration::Type::Eighth;
        if (str == "16th") return Duration::Type::Sixteenth;
        if (str == "32nd") return Duration::Type::ThirtySecond;
        if (str == "64th") return Duration::Type::SixtyFourth;
        if (str == "128th") return Duration::Type::HundredTwentyEighth;
        if (str == "256th") return Duration::Type::TwoHundredFiftySixth;
        return Duration::Type::Quarter;
    }

    Duration Score::durationFromQuarters(double quarters) {
        Duration d;
        // Find closest duration type
        if (quarters >= 3.5) d.type = Duration::Type::DoubleWhole;
        else if (quarters >= 1.75) d.type = Duration::Type::Whole;
        else if (quarters >= 0.875) d.type = Duration::Type::Half;
        else if (quarters >= 0.4375) d.type = Duration::Type::Quarter;
        else if (quarters >= 0.21875) d.type = Duration::Type::Eighth;
        else if (quarters >= 0.109375) d.type = Duration::Type::Sixteenth;
        else d.type = Duration::Type::ThirtySecond;

        // Calculate dots
        double base = d.toQuarterNotes();
        while (base * 1.5 <= quarters && d.dots < 3) {
            d.dots++;
            base = d.toQuarterNotes();
        }

        return d;
    }

    bool Score::saveMusicXML(const QString& path) const {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;

        QXmlStreamWriter xml(&file);
        xml.setAutoFormatting(true);

        xml.writeStartDocument();
        xml.writeDTD("<!DOCTYPE score-partwise PUBLIC '-//Recordare//DTD MusicXML 4.0 Partwise//EN' "
        "'http://www.musicxml.org/dtds/partwise.dtd'>");

        xml.writeStartElement("score-partwise");
        xml.writeAttribute("version", "4.0");

        // Work
        xml.writeStartElement("work");
        xml.writeTextElement("work-title", m_title);
        xml.writeEndElement();

        // Identification
        xml.writeStartElement("identification");
        xml.writeStartElement("creator");
        xml.writeAttribute("type", "composer");
        xml.writeCharacters(m_composer);
        xml.writeEndElement();
        xml.writeStartElement("creator");
        xml.writeAttribute("type", "lyricist");
        xml.writeCharacters(m_lyricist);
        xml.writeEndElement();
        xml.writeEndElement();

        // Part list
        xml.writeStartElement("part-list");
        for (int i = 0; i < staves.size(); ++i) {
            xml.writeStartElement("score-part");
            xml.writeAttribute("id", QString("P%1").arg(i + 1));
            xml.writeTextElement("part-name", staves[i]->name());
            xml.writeEndElement();
        }
        xml.writeEndElement();

        // Parts
        for (int i = 0; i < staves.size(); ++i) {
            xml.writeStartElement("part");
            xml.writeAttribute("id", QString("P%1").arg(i + 1));

            Staff* staff = staves[i].get();
            for (int m = 0; m < staff->measures.size(); ++m) {
                Measure* measure = staff->measures[m].get();
                xml.writeStartElement("measure");
                xml.writeAttribute("number", QString::number(m + 1));

                // Attributes for first measure
                if (m == 0) {
                    writeMusicXMLAttributes(xml, measure, true);
                } else if (measure->timeSigs.size() > 0 || measure->keySigs.size() > 0) {
                    writeMusicXMLAttributes(xml, measure, false);
                }

                // Write notes
                for (const auto& note : measure->notes) {
                    writeMusicXMLNote(xml, note, measure);
                }

                // Barline
                for (const auto& barline : measure->barlines) {
                    writeMusicXMLBarline(xml, barline);
                }

                xml.writeEndElement(); // measure
            }

            xml.writeEndElement(); // part
        }

        xml.writeEndElement(); // score-partwise
        xml.writeEndDocument();

        return true;
    }

    void Score::writeMusicXMLAttributes(QXmlStreamWriter& xml, const Measure* measure, bool isFirst) const {
        xml.writeStartElement("attributes");

        if (isFirst) {
            xml.writeTextElement("divisions", "480");
        }

        // Key signature
        if (!measure->keySigs.isEmpty()) {
            const auto& ks = measure->keySigs.first();
            xml.writeStartElement("key");
            xml.writeTextElement("fifths", QString::number(ks.accidentals()));
            xml.writeEndElement();
        }

        // Time signature
        if (!measure->timeSigs.isEmpty()) {
            const auto& ts = measure->timeSigs.first();
            xml.writeStartElement("time");
            xml.writeTextElement("beats", QString::number(ts.numerator));
            xml.writeTextElement("beat-type", QString::number(ts.denominator));
            xml.writeEndElement();
        }

        // Clef
        if (!measure->clefs.isEmpty()) {
            const auto& clef = measure->clefs.first();
            xml.writeStartElement("clef");
            QString sign = (clef.type == ClefType::Treble || clef.type == ClefType::FrenchViolin) ? "G" :
            (clef.type == ClefType::Bass) ? "F" : "C";
            xml.writeTextElement("sign", sign);
            xml.writeTextElement("line", QString::number(clef.staffLine + 1));
            if (clef.octaveChange != 0) {
                xml.writeTextElement("clef-octave-change", QString::number(clef.octaveChange));
            }
            xml.writeEndElement();
        }

        xml.writeEndElement(); // attributes
    }

    void Score::writeMusicXMLNote(QXmlStreamWriter& xml, const Note& note, const Measure* measure) const {
        xml.writeStartElement("note");

        // Chord
        if (note.isChord) {
            xml.writeEmptyElement("chord");
        }

        if (note.isRest) {
            xml.writeEmptyElement("rest");
        } else {
            xml.writeStartElement("pitch");
            static const char* steps = "CDEFGAB";
            int stepIndex = static_cast<int>(note.pitch.pitchClass) % 7;
            int alter = 0;
            if (note.pitch.accidental == Accidental::Sharp) alter = 1;
            else if (note.pitch.accidental == Accidental::Flat) alter = -1;
            else if (note.pitch.accidental == Accidental::DoubleSharp) alter = 2;
            else if (note.pitch.accidental == Accidental::DoubleFlat) alter = -2;

            xml.writeTextElement("step", QString(steps[stepIndex]));
            if (alter != 0) xml.writeTextElement("alter", QString::number(alter));
            xml.writeTextElement("octave", QString::number(note.pitch.octave + 1));
            xml.writeEndElement();
        }

        int duration = static_cast<int>(note.duration.toQuarterNotes() * 480);
        xml.writeTextElement("duration", QString::number(duration));

        // Type
        static const char* types[] = {"maxima", "long", "breve", "whole", "half", "quarter",
            "eighth", "16th", "32nd", "64th", "128th", "256th"};
            xml.writeTextElement("type", types[static_cast<int>(note.duration.type)]);

            // Dots
            for (int i = 0; i < note.duration.dots; ++i) {
                xml.writeEmptyElement("dot");
            }

            // Notations
            if (note.tie != TieType::None || !note.articulations.isEmpty() ||
                note.ornament != Ornament::None) {
                xml.writeStartElement("notations");

            if (note.tie != TieType::None) {
                xml.writeStartElement("tied");
                xml.writeAttribute("type", note.tie == TieType::Start ? "start" : "stop");
                xml.writeEndElement();
            }

            if (!note.articulations.isEmpty()) {
                xml.writeStartElement("articulations");
                for (auto art : note.articulations) {
                    switch (art) {
                        case Articulation::Staccato: xml.writeEmptyElement("staccato"); break;
                        case Articulation::Tenuto: xml.writeEmptyElement("tenuto"); break;
                        case Articulation::Accent: xml.writeEmptyElement("accent"); break;
                        case Articulation::Marcato: xml.writeEmptyElement("strong-accent"); break;
                        case Articulation::Staccatissimo: xml.writeEmptyElement("staccatissimo"); break;
                        default: break;
                    }
                }
                xml.writeEndElement();
            }

            if (note.ornament != Ornament::None) {
                xml.writeStartElement("ornaments");
                switch (note.ornament) {
                    case Ornament::Trill: xml.writeEmptyElement("trill-mark"); break;
                    case Ornament::Turn: xml.writeEmptyElement("turn"); break;
                    case Ornament::Mordent: xml.writeEmptyElement("mordent"); break;
                    case Ornament::InvertedMordent: xml.writeEmptyElement("inverted-mordent"); break;
                    default: break;
                }
                xml.writeEndElement();
            }

            xml.writeEndElement();
                }

                xml.writeEndElement(); // note
    }

    void Score::writeMusicXMLBarline(QXmlStreamWriter& xml, const Barline& barline) const {
        xml.writeStartElement("barline");
        xml.writeAttribute("location", "right");

        static const char* styles[] = {"regular", "light-light", "light-heavy", "heavy-light",
            "heavy-heavy", "light-heavy", "dashed", "dotted", "tick", "short"};
            xml.writeTextElement("bar-style", styles[static_cast<int>(barline.type)]);

            if (barline.type == Barline::EndRepeat || barline.type == Barline::EndBeginRepeat) {
                xml.writeTextElement("repeat", QString(), {{"direction", "backward"}});
            }
            if (barline.type == Barline::BeginRepeat || barline.type == Barline::EndBeginRepeat) {
                xml.writeTextElement("repeat", QString(), {{"direction", "forward"}});
            }

            xml.writeEndElement();
    }

    // =============================================================================
    // MIDI Import/Export
    // =============================================================================

    bool Score::loadMIDI(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return false;

        QByteArray data = file.readAll();
        const quint8* ptr = reinterpret_cast<const quint8*>(data.constData());
        int size = data.size();

        // Check header
        if (size < 14 || memcmp(ptr, "MThd", 4) != 0) return false;

        int format = (ptr[8] << 8) | ptr[9];
        int trackCount = (ptr[10] << 8) | ptr[11];
        int timeDivision = (ptr[12] << 8) | ptr[13];

        // Calculate ticks per quarter
        if (timeDivision & 0x8000) {
            // SMPTE format - convert to ticks per quarter
            int framesPerSec = -(timeDivision >> 8);
            int ticksPerFrame = timeDivision & 0xFF;
            m_ticksPerQuarter = framesPerSec * ticksPerFrame;
        } else {
            m_ticksPerQuarter = timeDivision;
        }

        ptr += 14;
        size -= 14;

        // Create staff for each track
        for (int t = 0; t < trackCount && size > 0; ++t) {
            if (memcmp(ptr, "MTrk", 4) != 0) break;

            int trackLen = (ptr[4] << 24) | (ptr[5] << 16) | (ptr[6] << 8) | ptr[7];
            ptr += 8;
            size -= 8;

            auto* staff = addStaff(QString("Track %1").arg(t + 1));
            auto* measure = staff->addMeasure(1);

            int currentTick = 0;
            int runningStatus = 0;

            // Track notes being held to match note-off
            struct PendingNote {
                int note;
                int velocity;
                int startTick;
                int channel;
            };
            QVector<PendingNote> pendingNotes;

            const quint8* trackEnd = ptr + trackLen;
            while (ptr < trackEnd && size > 0) {
                // Read variable-length delta time
                int delta = 0;
                while (*ptr & 0x80) {
                    delta = (delta << 7) | (*ptr & 0x7F);
                    ptr++;
                    size--;
                }
                delta = (delta << 7) | *ptr++;
                size--;
                currentTick += delta;

                // Check for new measure needed
                while (currentTick >= measure->startTick() + measure->lengthTicks()) {
                    int nextNum = measure->measureNumber() + 1;
                    measure = staff->addMeasure(nextNum);
                }

                quint8 status = *ptr;
                if (status & 0x80) {
                    runningStatus = status;
                    ptr++;
                    size--;
                } else {
                    status = runningStatus;
                }

                quint8 type = status & 0xF0;
                quint8 channel = status & 0x0F;

                switch (type) {
                    case 0x80: // Note off
                    case 0x90: { // Note on (velocity 0 = note off)
                        quint8 note = *ptr++;
                        quint8 vel = *ptr++;
                        size -= 2;

                        if (type == 0x90 && vel > 0) {
                            // Note on
                            PendingNote pn;
                            pn.note = note;
                            pn.velocity = vel;
                            pn.startTick = currentTick;
                            pn.channel = channel;
                            pendingNotes.append(pn);
                        } else {
                            // Note off - find matching note
                            for (int i = 0; i < pendingNotes.size(); ++i) {
                                if (pendingNotes[i].note == note && pendingNotes[i].channel == channel) {
                                    Note n;
                                    n.pitch = Pitch(note);
                                    n.tickPosition = pendingNotes[i].startTick - measure->startTick();
                                    int durationTicks = currentTick - pendingNotes[i].startTick;
                                    n.duration = durationFromQuarters(durationTicks / 480.0);
                                    n.velocity = pendingNotes[i].velocity;
                                    n.voice = channel;
                                    measure->notes.append(n);
                                    pendingNotes.removeAt(i);
                                    break;
                                }
                            }
                        }
                        break;
                    }
                    case 0xA0: // Polyphonic key pressure
                        ptr += 2; size -= 2;
                        break;
                    case 0xB0: // Control change
                        ptr += 2; size -= 2;
                        break;
                    case 0xC0: // Program change
                    case 0xD0: // Channel pressure
                        ptr++; size--;
                        break;
                    case 0xE0: // Pitch bend
                        ptr += 2; size -= 2;
                        break;
                    case 0xF0: // System exclusive
                        if (status == 0xFF) { // Meta event
                            quint8 metaType = *ptr++;
                            size--;
                            // Read length
                            int len = 0;
                            while (*ptr & 0x80) {
                                len = (len << 7) | (*ptr & 0x7F);
                                ptr++;
                                size--;
                            }
                            len = (len << 7) | *ptr++;
                            size--;

                            if (metaType == 0x51 && len >= 3) { // Tempo
                                int microsecPerQuarter = (ptr[0] << 16) | (ptr[1] << 8) | ptr[2];
                                m_tempo = 60000000.0 / microsecPerQuarter;
                            }
                            ptr += len;
                            size -= len;
                        } else if (status == 0xF0 || status == 0xF7) {
                            // SysEx - skip
                            int len = 0;
                            while (*ptr & 0x80) {
                                len = (len << 7) | (*ptr & 0x7F);
                                ptr++;
                                size--;
                            }
                            len = (len << 7) | *ptr++;
                            size--;
                            ptr += len;
                            size -= len;
                        }
                        break;
                }
            }

            // Clean up any unmatched note-ons
            for (const auto& pn : pendingNotes) {
                Note n;
                n.pitch = Pitch(pn.note);
                n.tickPosition = pn.startTick - measure->startTick();
                n.duration.type = Duration::Type::Quarter; // Default
                n.velocity = pn.velocity;
                n.voice = pn.channel;
                measure->notes.append(n);
            }

            ptr = trackEnd;
            size = (trackEnd - reinterpret_cast<const quint8*>(data.constData())) +
            data.size() - (ptr - reinterpret_cast<const quint8*>(data.constData()));
        }

        emit modified();
        return true;
    }

    bool Score::saveMIDI(const QString& path) const {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;

        // Build MIDI data
        QByteArray data;

        // Header chunk
        data.append("MThd", 4);
        data.append('\x00'); data.append('\x00'); data.append('\x00'); data.append('\x00'); // Length (placeholder)
        int headerStart = data.size() - 4;

        data.append('\x00'); data.append('\x01'); // Format 1 (multiple tracks, single sequence)
        data.append((staves.size() >> 8) & 0xFF); data.append(staves.size() & 0xFF); // Number of tracks
        data.append((m_ticksPerQuarter >> 8) & 0xFF); data.append(m_ticksPerQuarter & 0xFF); // Time division

        // Fix header length
        int headerLen = data.size() - headerStart - 4;
        data[headerStart] = (headerLen >> 24) & 0xFF;
        data[headerStart + 1] = (headerLen >> 16) & 0xFF;
        data[headerStart + 2] = (headerLen >> 8) & 0xFF;
        data[headerStart + 3] = headerLen & 0xFF;

        // Track chunks
        for (const auto& staff : staves) {
            data.append("MTrk", 4);
            int trackLenPos = data.size();
            data.append('\x00'); data.append('\x00'); data.append('\x00'); data.append('\x00'); // Length placeholder

            int trackStart = data.size();
            int lastTick = 0;

            // Tempo event at start
            if (&staff == staves.first()) {
                data.append('\x00'); // Delta time
                data.append('\xFF'); data.append('\x51'); data.append('\x03'); // Tempo meta
                int microsec = 60000000 / (m_tempo > 0 ? int(m_tempo) : 120);
                data.append((microsec >> 16) & 0xFF);
                data.append((microsec >> 8) & 0xFF);
                data.append(microsec & 0xFF);
            }

            // Time signature at start
            if (&staff == staves.first() && !staff->measures.isEmpty()) {
                const auto& m = staff->measures.first();
                if (!m->timeSigs.isEmpty()) {
                    const auto& ts = m->timeSigs.first();
                    data.append('\x00'); // Delta time
                    data.append('\xFF'); data.append('\x58'); data.append('\x04'); // Time sig meta
                    data.append(static_cast<char>(ts.numerator));
                    int denomPower = 0;
                    int d = ts.denominator;
                    while (d > 1) { d >>= 1; denomPower++; }
                    data.append(static_cast<char>(denomPower));
                    data.append('\x18'); // 24 MIDI clocks per metronome click
                    data.append('\x08'); // 32nd notes per quarter
                }
            }

            // Program change
            data.append('\x00'); // Delta
            data.append(0xC0 | (staff->midiChannel() & 0x0F)); // Program change
            data.append(static_cast<char>(staff->midiProgram() & 0x7F));

            // Notes
            for (const auto& measure : staff->measures) {
                int measureStartTick = measure->startTick();

                // Sort events by tick
                struct MidiEvent {
                    int tick;
                    bool isNoteOn;
                    quint8 note;
                    quint8 velocity;
                    quint8 channel;
                };
                QVector<MidiEvent> events;

                for (const auto& note : measure->notes) {
                    if (note.isRest) continue;

                    int noteStart = measureStartTick + note.tickPosition;
                    int noteEnd = noteStart + static_cast<int>(note.duration.toQuarterNotes() * m_ticksPerQuarter);

                    events.append({noteStart, true, static_cast<quint8>(note.pitch.midiNote),
                        static_cast<quint8>(note.velocity), static_cast<quint8>(note.voice & 0x0F)});
                    events.append({noteEnd, false, static_cast<quint8>(note.pitch.midiNote),
                        0, static_cast<quint8>(note.voice & 0x0F)});
                }

                std::sort(events.begin(), events.end(),
                          [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

                // Write events
                for (const auto& ev : events) {
                    int delta = ev.tick - lastTick;
                    lastTick = ev.tick;

                    // Write variable-length delta
                    if (delta == 0) {
                        data.append('\x00');
                    } else {
                        QByteArray deltaBytes;
                        do {
                            deltaBytes.prepend((delta & 0x7F) | 0x80);
                            delta >>= 7;
                        } while (delta > 0);
                        deltaBytes.last() &= 0x7F; // Clear continue bit on last byte
                        data.append(deltaBytes);
                    }

                    quint8 status = (ev.isNoteOn ? 0x90 : 0x80) | (ev.channel & 0x0F);
                    data.append(static_cast<char>(status));
                    data.append(static_cast<char>(ev.note));
                    data.append(static_cast<char>(ev.velocity));
                }
            }

            // End of track
            data.append('\x00'); // Delta
            data.append('\xFF'); data.append('\x2F'); data.append('\x00'); // End of track

            // Fix track length
            int trackLen = data.size() - trackStart;
            data[trackLenPos] = (trackLen >> 24) & 0xFF;
            data[trackLenPos + 1] = (trackLen >> 16) & 0xFF;
            data[trackLenPos + 2] = (trackLen >> 8) & 0xFF;
            data[trackLenPos + 3] = trackLen & 0xFF;
        }

        file.write(data);
        return true;
    }

    // =============================================================================
    // Audio Rendering
    // =============================================================================

    QByteArray Score::renderToPCM(int sampleRate) {
        // Simple offline render
        int totalSamples = (totalTicks() / 480.0) * (60.0 / m_tempo) * sampleRate;

        QByteArray pcm;
        pcm.resize(totalSamples * 2 * sizeof(float)); // Stereo float

        float* buffer = reinterpret_cast<float*>(pcm.data());
        std::fill(buffer, buffer + totalSamples * 2, 0.0f);

        // Simple synthesis (similar to real-time but offline)
        QVector<SynthVoice> voices;
        voices.resize(16);

        double dt = 1.0 / sampleRate;
        double currentTime = 0.0;

        for (int i = 0; i < totalSamples; ++i) {
            double sample = 0.0;
            int currentTick = static_cast<int>((currentTime * m_tempo / 60.0) * 480);

            // Update voices
            if (!staves.isEmpty()) {
                auto* staff = staves[0].get();
                for (const auto& measure : staff->measures) {
                    int measureStart = measure->startTick();
                    if (currentTick < measureStart || currentTick >= measureStart + measure->lengthTicks())
                        continue;

                    int localTick = currentTick - measureStart;

                    for (const auto& note : measure->notes) {
                        if (note.isRest) continue;
                        if (note.tickPosition == localTick) {
                            // Find free voice
                            for (auto& v : voices) {
                                if (!v.isActive) {
                                    v.sampleRate = sampleRate;
                                    v.start(note.pitch, note.velocity / 127.0,
                                            note.duration.toSeconds(m_tempo), currentTime);
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            // Mix voices
            for (auto& v : voices) {
                if (v.isActive) {
                    sample += v.nextSample();
                }
            }

            // Soft clip
            sample = std::tanh(sample);

            buffer[i * 2] = static_cast<float>(sample);
            buffer[i * 2 + 1] = static_cast<float>(sample);

            currentTime += dt;
        }

        return pcm;
    }

    // =============================================================================
    // Engraving and Rendering
    // =============================================================================

    EngravingSettings& EngravingSettings::defaults() {
        static EngravingSettings instance;
        return instance;
    }

    QMap<QString, uint> ScoreRenderer::s_smuflCodes;

    ScoreRenderer::ScoreRenderer(Score* score) : m_score(score) {
        if (s_smuflCodes.isEmpty()) {
            initializeSmuflCodes();
        }
    }

    void ScoreRenderer::initializeSmuflCodes() {
        s_smuflCodes["noteheadBlack"] = 0xE0A4;
        s_smuflCodes["noteheadHalf"] = 0xE0A3;
        s_smuflCodes["noteheadWhole"] = 0xE0A2;
        s_smuflCodes["noteheadDoubleWhole"] = 0xE0A0;
        s_smuflCodes["gClef"] = 0xE050;
        s_smuflCodes["fClef"] = 0xE062;
        s_smuflCodes["cClef"] = 0xE05C;
        s_smuflCodes["sharp"] = 0xE262;
        s_smuflCodes["flat"] = 0xE260;
        s_smuflCodes["natural"] = 0xE261;
        s_smuflCodes["doubleSharp"] = 0xE263;
        s_smuflCodes["doubleFlat"] = 0xE264;
        s_smuflCodes["stem"] = 0xE210;
        s_smuflCodes["restWhole"] = 0xE4E3;
        s_smuflCodes["restHalf"] = 0xE4E4;
        s_smuflCodes["restQuarter"] = 0xE4E5;
        s_smuflCodes["restEighth"] = 0xE4E6;
        s_smuflCodes["rest16th"] = 0xE4E7;
        s_smuflCodes["rest32nd"] = 0xE4E8;
        s_smuflCodes["rest64th"] = 0xE4E9;
        s_smuflCodes["augmentationDot"] = 0xE1E7;
        s_smuflCodes["barlineSingle"] = 0xE030;
        s_smuflCodes["barlineDouble"] = 0xE031;
        s_smuflCodes["barlineFinal"] = 0xE032;
        s_smuflCodes["tie"] = 0xE1FD;
        s_smuflCodes["slur"] = 0xE1FD;
        s_smuflCodes["staccato"] = 0xE4A2;
        s_smuflCodes["accent"] = 0xE4A0;
        s_smuflCodes["tenuto"] = 0xE4A4;
        s_smuflCodes["fermata"] = 0xE4C0;
        s_smuflCodes["trill"] = 0xE566;
    }

    void ScoreRenderer::render(QPainter* painter, const QRectF& rect, int startStaff, int staffCount) {
        if (!m_score || m_score->staves.isEmpty()) return;

        painter->setRenderHint(QPainter::Antialiasing);

        int endStaff = staffCount < 0 ? m_score->staves.size() :
        qMin(startStaff + staffCount, m_score->staves.size());

        double y = rect.top();

        for (int i = startStaff; i < endStaff; ++i) {
            Staff* staff = m_score->staves[i].get();

            // Draw staff lines
            drawStaffLines(painter, staff, QRectF(rect.left(), y, rect.width(), staff->height()));

            // Draw measures
            double x = rect.left() + m_settings.leftMargin;
            for (auto& measure : staff->measures) {
                double mWidth = measureWidth(measure.get());
                QRectF mRect(x, y, mWidth, staff->height());
                renderMeasure(painter, measure.get(), mRect.topLeft());
                x += mWidth;
            }

            y += staff->height() + m_score->staffDistance();
        }
    }

    void ScoreRenderer::drawStaffLines(QPainter* painter, Staff* staff, const QRectF& rect) {
        painter->setPen(QPen(m_settings.lineColor, m_settings.lineWidth));
        double lineSpacing = m_settings.spatium;

        for (int line = 0; line < staff->lines(); ++line) {
            double ly = rect.top() + line * lineSpacing;
            painter->drawLine(QPointF(rect.left(), ly), QPointF(rect.right(), ly));
        }
    }

    void ScoreRenderer::renderMeasure(QPainter* painter, Measure* measure, const QPointF& pos) {
        double x = pos.x();
        double y = pos.y();

        // Draw clef if present
        if (!measure->clefs.isEmpty()) {
            renderClef(painter, measure->clefs.first(), QPointF(x, y));
            x += m_settings.clefWidth;
        }

        // Draw key signature
        if (!measure->keySigs.isEmpty()) {
            renderKeySig(painter, measure->keySigs.first(), QPointF(x, y));
            x += keySigWidth(measure->keySigs.first());
        }

        // Draw time signature
        if (!measure->timeSigs.isEmpty()) {
            renderTimeSig(painter, measure->timeSigs.first(), QPointF(x, y));
            x += m_settings.timeSigWidth;
        }

        // Draw notes
        for (const auto& note : measure->notes) {
            double noteX = x + measure->tickToPixel(note.tickPosition);
            renderNote(painter, note, QPointF(noteX, y));
        }

        // Draw barline at end
        if (!measure->barlines.isEmpty()) {
            double barX = pos.x() + measure->width;
            renderBarline(painter, measure->barlines.first(),
                          QPointF(barX, y), measure->height());
        }
    }

    void ScoreRenderer::renderNote(QPainter* painter, const Note& note, const QPointF& pos) {
        painter->setFont(m_settings.musicFont);

        // Calculate vertical position based on pitch
        double noteY = calculateNoteY(note);

        // Draw notehead
        QString noteChar;
        switch (note.duration.type) {
            case Duration::Type::Whole:
                noteChar = QChar(s_smuflCodes["noteheadWhole"]);
                break;
            case Duration::Type::Half:
                noteChar = QChar(s_smuflCodes["noteheadHalf"]);
                break;
            default:
                noteChar = QChar(s_smuflCodes["noteheadBlack"]);
        }

        double noteX = pos.x();
        painter->drawText(QPointF(noteX, noteY), noteChar);

        // Draw stem
        if (note.duration.type != Duration::Type::Whole &&
            note.duration.type != Duration::Type::Breve) {
            drawStem(painter, note, QPointF(noteX, noteY));
            }

            // Draw accidental
            if (note.pitch.accidental != Accidental::Natural &&
                note.pitch.accidental != Accidental::None) {
                drawAccidental(painter, note.pitch.accidental, QPointF(noteX - m_settings.spatium * 1.5, noteY));
                }

                // Draw dots
                for (int i = 0; i < note.duration.dots; ++i) {
                    double dotX = noteX + m_settings.noteHeadWidth * 1.2 + i * m_settings.spatium * 0.5;
                    painter->drawText(QPointF(dotX, noteY - m_settings.spatium * 0.1),
                                      QChar(s_smuflCodes["augmentationDot"]));
                }

                // Draw articulations
                for (auto art : note.articulations) {
                    drawArticulation(painter, art, QPointF(noteX, noteY), note.stemUp);
                }

                // Draw lyrics
                for (const auto& lyric : note.lyrics) {
                    drawLyric(painter, lyric, QPointF(noteX, noteY + m_settings.spatium * 4));
                }
    }

    void ScoreRenderer::renderRest(QPainter* painter, const Note& rest, const QPointF& pos) {
        painter->setFont(m_settings.musicFont);

        QString restChar;
        switch (rest.duration.type) {
            case Duration::Type::Whole: restChar = QChar(s_smuflCodes["restWhole"]); break;
            case Duration::Type::Half: restChar = QChar(s_smuflCodes["restHalf"]); break;
            case Duration::Type::Quarter: restChar = QChar(s_smuflCodes["restQuarter"]); break;
            case Duration::Type::Eighth: restChar = QChar(s_smuflCodes["restEighth"]); break;
            case Duration::Type::Sixteenth: restChar = QChar(s_smuflCodes["rest16th"]); break;
            case Duration::Type::ThirtySecond: restChar = QChar(s_smuflCodes["rest32nd"]); break;
            default: restChar = QChar(s_smuflCodes["restQuarter"]);
        }

        double restY = pos.y() + m_settings.spatium * 2; // Center on staff
        painter->drawText(QPointF(pos.x(), restY), restChar);

        // Dots for rests
        for (int i = 0; i < rest.duration.dots; ++i) {
            double dotX = pos.x() + m_settings.spatium * 1.5 + i * m_settings.spatium * 0.5;
            painter->drawText(QPointF(dotX, restY), QChar(s_smuflCodes["augmentationDot"]));
        }
    }

    void ScoreRenderer::renderClef(QPainter* painter, const Clef& clef, const QPointF& pos) {
        painter->setFont(m_settings.musicFont);

        QString clefChar;
        switch (clef.type) {
            case ClefType::Treble:
            case ClefType::FrenchViolin:
                clefChar = QChar(s_smuflCodes["gClef"]);
                break;
            case ClefType::Bass:
                clefChar = QChar(s_smuflCodes["fClef"]);
                break;
            case ClefType::Alto:
            case ClefType::Tenor:
                clefChar = QChar(s_smuflCodes["cClef"]);
                break;
            default:
                clefChar = QChar(s_smuflCodes["gClef"]);
        }

        // Adjust position based on clef type and octave change
        double clefY = pos.y() + m_settings.spatium * 2;
        if (clef.octaveChange > 0) clefY -= m_settings.spatium;
        else if (clef.octaveChange < 0) clefY += m_settings.spatium;

        painter->drawText(QPointF(pos.x(), clefY), clefChar);
    }

    void ScoreRenderer::renderKeySig(QPainter* painter, const KeySig& key, const QPointF& pos) {
        painter->setFont(m_settings.musicFont);

        int accidentals = key.accidentals();
        bool isSharp = key.isSharpKey();
        QString accChar = isSharp ? QChar(s_smuflCodes["sharp"]) : QChar(s_smuflCodes["flat"]);

        // Standard positions for accidentals
        static const int sharpPositions[] = {0, 3, -1, 2, 5, 1, 4}; // Lines/spaces for F#, C#, G#, etc.
        static const int flatPositions[] = {4, 1, 5, 2, 6, 3, 0};

        const int* positions = isSharp ? sharpPositions : flatPositions;
        double staffTop = pos.y();

        for (int i = 0; i < qAbs(accidentals); ++i) {
            double x = pos.x() + i * m_settings.spatium * 0.8;
            double y = staffTop + (5 - positions[i]) * (m_settings.spatium * 0.5);
            painter->drawText(QPointF(x, y), accChar);
        }
    }

    void ScoreRenderer::renderTimeSig(QPainter* painter, const TimeSignature& ts, const QPointF& pos) {
        painter->setFont(m_settings.textFont);
        painter->setPen(QPen(m_settings.textColor));

        QString num = QString::number(ts.numerator);
        QString den = QString::number(ts.denominator);

        double x = pos.x();
        double topY = pos.y() + m_settings.spatium * 2;
        double bottomY = pos.y() + m_settings.spatium * 4;

        painter->drawText(QPointF(x, topY), num);
        painter->drawText(QPointF(x, bottomY), den);
    }

    void ScoreRenderer::renderBarline(QPainter* painter, const Barline& barline, const QPointF& pos, double height) {
        painter->setPen(QPen(m_settings.lineColor, m_settings.lineWidth));

        double x = pos.x();
        double y1 = pos.y();
        double y2 = pos.y() + height;

        switch (barline.type) {
            case Barline::Single:
                painter->drawLine(QPointF(x, y1), QPointF(x, y2));
                break;
            case Barline::Double:
                painter->drawLine(QPointF(x, y1), QPointF(x, y2));
                painter->drawLine(QPointF(x + m_settings.spatium * 0.3, y1),
                                  QPointF(x + m_settings.spatium * 0.3, y2));
                break;
            case Barline::Final:
                painter->setPen(QPen(m_settings.lineColor, m_settings.lineWidth * 2));
                painter->drawLine(QPointF(x, y1), QPointF(x, y2));
                painter->setPen(QPen(m_settings.lineColor, m_settings.lineWidth));
                painter->drawLine(QPointF(x + m_settings.spatium * 0.4, y1),
                                  QPointF(x + m_settings.spatium * 0.4, y2));
                break;
            default:
                painter->drawLine(QPointF(x, y1), QPointF(x, y2));
        }
    }

    void ScoreRenderer::drawStem(QPainter* painter, const Note& note, const QPointF& notePos) {
        painter->setPen(QPen(m_settings.lineColor, m_settings.stemWidth));

        double stemX = note.stemUp ?
        notePos.x() + m_settings.noteHeadWidth * 0.9 :
        notePos.x() + m_settings.noteHeadWidth * 0.1;

        double stemY1 = notePos.y() - m_settings.spatium * 0.5;
        double stemY2 = note.stemUp ?
        stemY1 - m_settings.stemLength :
        stemY1 + m_settings.stemLength;

        painter->drawLine(QPointF(stemX, stemY1), QPointF(stemX, stemY2));

        // Draw beam/flag if needed
        if (note.duration.type >= Duration::Type::Eighth) {
            drawFlag(painter, note, QPointF(stemX, stemY2));
        }
    }

    void ScoreRenderer::drawFlag(QPainter* painter, const Note& note, const QPointF& stemEnd) {
        // Simplified flag drawing
        painter->setPen(QPen(m_settings.lineColor, m_settings.stemWidth));

        double flagLen = m_settings.stemLength * 0.3;
        double flagX = note.stemUp ? stemEnd.x() + flagLen : stemEnd.x() - flagLen;
        double flagY = stemEnd.y() + (note.stemUp ? -m_settings.spatium * 0.5 : m_settings.spatium * 0.5);

        QPainterPath path;
        path.moveTo(stemEnd);
        path.quadTo((stemEnd.x() + flagX) / 2, stemEnd.y() + (note.stemUp ? -m_settings.spatium : m_settings.spatium), flagX, flagY);
        painter->drawPath(path);
    }

    void ScoreRenderer::drawAccidental(QPainter* painter, Accidental acc, const QPointF& pos) {
        painter->setFont(m_settings.musicFont);

        QString accChar;
        switch (acc) {
            case Accidental::Sharp: accChar = QChar(s_smuflCodes["sharp"]); break;
            case Accidental::Flat: accChar = QChar(s_smuflCodes["flat"]); break;
            case Accidental::Natural: accChar = QChar(s_smuflCodes["natural"]); break;
            case Accidental::DoubleSharp: accChar = QChar(s_smuflCodes["doubleSharp"]); break;
            case Accidental::DoubleFlat: accChar = QChar(s_smuflCodes["doubleFlat"]); break;
            default: return;
        }

        painter->drawText(pos, accChar);
    }

    void ScoreRenderer::drawArticulation(QPainter* painter, Articulation art, const QPointF& notePos, bool above) {
        painter->setFont(m_settings.musicFont);

        QString artChar;
        switch (art) {
            case Articulation::Staccato: artChar = QChar(s_smuflCodes["staccato"]); break;
            case Articulation::Accent: artChar = QChar(s_smuflCodes["accent"]); break;
            case Articulation::Tenuto: artChar = QChar(s_smuflCodes["tenuto"]); break;
            case Articulation::Marcato: artChar = QChar(s_smuflCodes["accent"]); break; // Use accent for marcato too
            default: return;
        }

        double y = above ? notePos.y() - m_settings.spatium * 2 : notePos.y() + m_settings.spatium * 2;
        painter->drawText(QPointF(notePos.x(), y), artChar);
    }

    void ScoreRenderer::drawLyric(QPainter* painter, const Lyric& lyric, const QPointF& pos) {
        painter->setFont(m_settings.lyricsFont);
        painter->setPen(QPen(m_settings.textColor));

        QString text = lyric.text;
        if (lyric.syllabic == Lyric::Begin) text += "-";
        else if (lyric.syllabic == Lyric::Middle) text = "-" + text + "-";
        else if (lyric.syllabic == Lyric::End) text = "-" + text;

        painter->drawText(pos, text);
    }

    double ScoreRenderer::calculateNoteY(const Note& note) const {
        // Calculate staff line position based on pitch
        // Middle C (C4, MIDI 60) is typically on the first ledger line below staff
        // In treble clef: lines from bottom are E4, G4, B4, D5, F5

        int midi = note.pitch.midiNote;

        // For treble clef, middle line (B4) is MIDI 71
        // Each line/space is one MIDI note number
        int middleLineMidi = 71; // B4
        int linesFromMiddle = midi - middleLineMidi;

        // Middle line is at 2 * spatium from top of staff
        double staffTop = 0; // Relative to staff top
        double middleLineY = staffTop + 2 * m_settings.spatium;

        // Each MIDI number is half a line space
        return middleLineY - linesFromMiddle * (m_settings.spatium * 0.5);
    }

    double ScoreRenderer::measureWidth(Measure* measure) const {
        if (!measure) return 100.0;

        double width = m_settings.leftMargin;

        // Space for clef
        if (!measure->clefs.isEmpty()) width += m_settings.clefWidth;

        // Space for key signature
        if (!measure->keySigs.isEmpty()) {
            width += keySigWidth(measure->keySigs.first());
        }

        // Space for time signature
        if (!measure->timeSigs.isEmpty()) width += m_settings.timeSigWidth;

        // Space for notes
        double contentWidth = 0;
        for (const auto& note : measure->notes) {
            contentWidth = qMax(contentWidth, measure->tickToPixel(note.tickPosition) +
            m_settings.noteHeadWidth * 2);
        }

        width += qMax(contentWidth, measure->width);
        width += m_settings.rightMargin;

        return width;
    }

    double ScoreRenderer::keySigWidth(const KeySig& key) const {
        int acc = qAbs(key.accidentals());
        return acc * m_settings.spatium * 0.8 + m_settings.spatium * 0.5;
    }

    void ScoreRenderer::doLayout() {
        if (!m_score) return;

        for (auto& staff : m_score->staves) {
            double x = 0;
            for (auto& measure : staff->measures) {
                measure->xPosition = x;
                layoutMeasure(measure.get());
                x += measure->width;
            }
        }
    }

    void ScoreRenderer::layoutMeasure(Measure* measure) {
        if (!measure) return;

        // Calculate note positions
        for (auto& note : measure->notes) {
            note.staffLine = midiToStaffLine(note.pitch.midiNote);

            // Determine stem direction based on position
            note.stemUp = note.staffLine < 4; // Below middle line = stem up
        }

        measure->width = measureWidth(measure);
    }

    int ScoreRenderer::midiToStaffLine(int midiNote) const {
        // MIDI 60 = C4
        // In treble clef, lines are E4(64), G4(67), B4(71), D5(74), F5(77)
        // Space below staff (E4 line is line 0 from bottom)

        int relativeToE4 = midiNote - 64;
        // E4 = 0, F4 = 1, G4 = 2, A4 = 3, B4 = 4, C5 = 5, etc.
        // Lines are at 0, 2, 4, 6, 8 (even numbers)
        // Spaces are at 1, 3, 5, 7, 9 (odd numbers)

        return relativeToE4;
    }

    Note* ScoreRenderer::noteAt(const QPointF& pos) {
        // Hit testing - find note at position
        if (!m_score) return nullptr;

        double currentY = 0;
        for (const auto& staff : m_score->staves) {
            if (pos.y() < currentY || pos.y() >= currentY + staff->height()) {
                currentY += staff->height() + m_score->staffDistance();
                continue;
            }

            // Check measures in this staff
            for (const auto& measure : staff->measures) {
                if (pos.x() < measure->xPosition || pos.x() >= measure->xPosition + measure->width)
                    continue;

                for (auto& note : measure->notes) {
                    double noteX = measure->xPosition + measure->tickToPixel(note.tickPosition);
                    double noteY = currentY + calculateNoteY(note);

                    if (qAbs(pos.x() - noteX) < m_settings.noteHeadWidth &&
                        qAbs(pos.y() - noteY) < m_settings.spatium) {
                        return &note;
                        }
                }
            }

            currentY += staff->height() + m_score->staffDistance();
        }

        return nullptr;
    }

    Measure* ScoreRenderer::measureAt(const QPointF& pos) {
        if (!m_score) return nullptr;

        double currentY = 0;
        for (const auto& staff : m_score->staves) {
            if (pos.y() < currentY || pos.y() >= currentY + staff->height()) {
                currentY += staff->height() + m_score->staffDistance();
                continue;
            }

            for (const auto& measure : staff->measures) {
                if (pos.x() >= measure->xPosition && pos.x() < measure->xPosition + measure->width) {
                    return measure.get();
                }
            }
        }

        return nullptr;
    }

    Staff* ScoreRenderer::staffAt(const QPointF& pos) {
        if (!m_score) return nullptr;
        return m_score->staffAtY(pos.y());
    }

    // =============================================================================
    // DAW Engine Implementation
    // =============================================================================

    Clip::Clip(ClipType type, QObject* parent)
    : QObject(parent), m_type(type) {}

    void Clip::snapToGrid(const TempoMap& tempoMap, int subdivisions) {
        double beat = tempoMap.secondsToBeats(m_startTime);
        double gridBeats = 4.0 / subdivisions; // Quarter note / subdivisions
        double snappedBeat = std::round(beat / gridBeats) * gridBeats;
        m_startTime = tempoMap.beatsToSeconds(snappedBeat);
    }

    // =============================================================================
    // Notation Clip Implementation
    // =============================================================================

    NotationClip::NotationClip(QObject* parent)
    : Clip(ClipType::Notation, parent) {}

    void NotationClip::setScore(std::unique_ptr<Score> score) {
        QWriteLocker lock(&m_lock);
        m_score = std::move(score);
        if (m_score) {
            double totalBeats = m_score->totalTicks() / 480.0;
            double tempo = m_score->tempo > 0 ? m_score->tempo : 120.0;
            setDuration(totalBeats * 60.0 / tempo);
        }
        emit scoreChanged();
        emit changed();
    }

    void NotationClip::createEmptyScore(const QString& title) {
        auto s = std::make_unique<Score>();
        s->setTitle(title);
        auto* staff = s->addStaff("Staff 1");
        staff->addMeasure(1);
        setScore(std::move(s));
    }

    bool NotationClip::isEmpty() const {
        QReadLocker lock(&m_lock);
        return !m_score || m_score->staves.isEmpty();
    }

    void NotationClip::processAudio(double position, int frames, float* buffer,
                                    int channels, int sampleRate, const TempoMap& tempo) {
        QReadLocker lock(&m_lock);

        if (!m_score || !m_synthEnabled) {
            std::fill(buffer, buffer + frames * channels, 0.0f);
            return;
        }

        if (!m_synthState) {
            m_synthState = std::make_unique<SynthState>();
        }

        double clipTime = position - startTime();
        if (clipTime < 0 || clipTime >= duration()) {
            std::fill(buffer, buffer + frames * channels, 0.0f);
            return;
        }

        std::fill(buffer, buffer + frames * channels, 0.0f);

        double dt = 1.0 / sampleRate;
        int currentTick = tempo.timeToTick(position) - tempo.timeToTick(startTime());

        for (int i = 0; i < frames; ++i) {
            double sample = 0.0;

            // Update voices and spawn new notes
            updateVoices(currentTick, tempo, sampleRate);

            // Mix all active voices
            for (auto& voice : m_synthState->voices) {
                if (voice.isActive) {
                    sample += voice.nextSample();
                }
            }

            // Soft clip and output
            sample = std::tanh(sample * 0.5) * 0.8;

            for (int ch = 0; ch < channels; ++ch) {
                buffer[i * channels + ch] = static_cast<float>(sample);
            }

            clipTime += dt;
            currentTick = tempo.timeToTick(startTime() + clipTime) - tempo.timeToTick(startTime());
        }
                                    }

                                    void NotationClip::updateVoices(int currentTick, const TempoMap& tempo, int sampleRate) {
                                        if (!m_score || m_staffIndex >= m_score->staves.size()) return;

                                        auto* staff = m_score->staves[m_staffIndex].get();

                                        // Find notes starting or active at this tick
                                        for (const auto& measure : staff->measures) {
                                            int measureStartTick = measure->startTick();
                                            if (currentTick < measureStartTick || currentTick >= measureStartTick + measure->lengthTicks())
                                                continue;

                                            int localTick = currentTick - measureStartTick;

                                            for (const auto& note : measure->notes) {
                                                if (note.isRest) continue;

                                                int noteStart = note.tickPosition;
                                                int noteEnd = noteStart + static_cast<int>(note.duration.toQuarterNotes() * 480);

                                                // Note starts here
                                                if (localTick == noteStart) {
                                                    // Find or steal voice
                                                    SynthVoice* voice = nullptr;
                                                    for (auto& v : m_synthState->voices) {
                                                        if (!v.isActive) {
                                                            voice = &v;
                                                            break;
                                                        }
                                                    }

                                                    // Steal oldest if no free voice
                                                    if (!voice && !m_synthState->voices.isEmpty()) {
                                                        // Find oldest voice
                                                        double oldestTime = m_synthState->voices[0].currentTime;
                                                        int oldestIdx = 0;
                                                        for (int i = 1; i < m_synthState->voices.size(); ++i) {
                                                            if (m_synthState->voices[i].currentTime > oldestTime) {
                                                                oldestTime = m_synthState->voices[i].currentTime;
                                                                oldestIdx = i;
                                                            }
                                                        }
                                                        voice = &m_synthState->voices[oldestIdx];
                                                    }

                                                    if (!voice) {
                                                        m_synthState->voices.resize(16);
                                                        voice = &m_synthState->voices[0];
                                                    }

                                                    // Calculate playback duration with articulation
                                                    double playDur = note.duration.toSeconds(m_score->tempo);
                                                    for (auto art : note.articulations) {
                                                        if (art == Articulation::Staccato) playDur *= 0.5;
                                                        else if (art == Articulation::Tenuto) playDur *= 1.1;
                                                    }

                                                    voice->sampleRate = sampleRate;
                                                    voice->start(note.pitch, note.velocity / 127.0, playDur, 0.0);
                                                    voice->noteId = note.pitch.midiNote;
                                                }
                                            }
                                        }
                                    }

                                    QVector<NoteEvent> NotationClip::renderToMidi() const {
                                        QReadLocker lock(&m_lock);
                                        QVector<NoteEvent> events;
                                        if (!m_score || m_score->staves.isEmpty()) return events;

                                        auto* staff = m_score->staves[0].get();
                                        double tempo = m_score->tempo > 0 ? m_score->tempo : 120.0;

                                        for (const auto& measure : staff->measures) {
                                            int measureStartTick = measure->startTick();

                                            for (const auto& note : measure->notes) {
                                                if (note.isRest) continue;

                                                double startSec = (measureStartTick + note.tickPosition) / 480.0 * (60.0 / tempo);
                                                double durSec = note.duration.toSeconds(tempo);

                                                // Note on
                                                NoteEvent on;
                                                on.type = NoteEvent::NoteOn;
                                                on.time = startSec;
                                                on.channel = note.voice & 0x0F;
                                                on.note = note.pitch.midiNote;
                                                on.velocity = note.velocity;
                                                on.pitch = note.pitch;
                                                on.duration = note.duration;
                                                on.fromNotation = true;
                                                on.voice = note.voice;
                                                events.append(on);

                                                // Note off
                                                NoteEvent off;
                                                off.type = NoteEvent::NoteOff;
                                                off.time = startSec + durSec;
                                                off.channel = note.voice & 0x0F;
                                                off.note = note.pitch.midiNote;
                                                off.velocity = 0;
                                                events.append(off);
                                            }
                                        }

                                        return events;
                                    }

                                    QVector<NoteEvent> NotationClip::getMidiEvents(double start, double end) {
                                        auto allEvents = renderToMidi();
                                        QVector<NoteEvent> result;
                                        for (const auto& ev : allEvents) {
                                            if (ev.time >= start && ev.time < end) {
                                                result.append(ev);
                                            }
                                        }
                                        return result;
                                    }

                                    // =============================================================================
                                    // MIDI Clip Implementation
                                    // =============================================================================

                                    MidiClip::MidiClip(QObject* parent) : Clip(ClipType::MIDI, parent) {}

                                    void MidiClip::addEvent(const NoteEvent& ev) {
                                        QWriteLocker lock(&m_lock);
                                        m_events.append(ev);
                                        std::sort(m_events.begin(), m_events.end(),
                                                  [](const NoteEvent& a, const NoteEvent& b) { return a.time < b.time; });
                                    }

                                    void MidiClip::addNote(const Pitch& pitch, double start, const Duration& dur, int vel, int ch) {
                                        NoteEvent on;
                                        on.type = NoteEvent::NoteOn;
                                        on.time = start;
                                        on.channel = ch & 0x0F;
                                        on.note = pitch.midiNote;
                                        on.velocity = vel;
                                        on.pitch = pitch;
                                        on.duration = dur;
                                        addEvent(on);

                                        NoteEvent off;
                                        off.type = NoteEvent::NoteOff;
                                        off.time = start + dur.toQuarterNotes() * (60.0/120.0);
                                        off.channel = ch & 0x0F;
                                        off.note = pitch.midiNote;
                                        off.velocity = 0;
                                        addEvent(off);
                                    }

                                    void MidiClip::processAudio(double position, int frames, float* buffer,
                                                                int channels, int sampleRate, const TempoMap& tempo) {
                                        Q_UNUSED(position)
                                        Q_UNUSED(frames)
                                        Q_UNUSED(channels)
                                        Q_UNUSED(sampleRate)
                                        Q_UNUSED(tempo)
                                        // MIDI clips don't produce audio directly - they send events to a synth
                                        std::fill(buffer, buffer + frames * channels, 0.0f);
                                                                }

                                                                QVector<NoteEvent> MidiClip::getMidiEvents(double start, double end) {
                                                                    QReadLocker lock(&m_lock);
                                                                    QVector<NoteEvent> result;
                                                                    for (const auto& ev : m_events) {
                                                                        if (ev.time >= start && ev.time < end) result.append(ev);
                                                                    }
                                                                    return result;
                                                                }

                                                                // =============================================================================
                                                                // Track Implementation
                                                                // =============================================================================

                                                                Track::Track(const QString& name, QObject* parent)
                                                                : QObject(parent), m_name(name) {}

                                                                void Track::addClip(std::unique_ptr<Clip> clip) {
                                                                    QWriteLocker lock(&m_lock);
                                                                    clip->setParent(this);
                                                                    m_clips.append(std::move(clip));
                                                                    emit clipAdded(m_clips.last().get());
                                                                }

                                                                void Track::removeClip(Clip* clip) {
                                                                    QWriteLocker lock(&m_lock);
                                                                    auto it = std::find_if(m_clips.begin(), m_clips.end(),
                                                                                           [clip](const auto& p) { return p.get() == clip; });
                                                                    if (it != m_clips.end()) {
                                                                        m_clips.erase(it);
                                                                        emit clipRemoved(clip);
                                                                    }
                                                                }

                                                                QVector<Clip*> Track::clipsAt(double time) const {
                                                                    QReadLocker lock(&m_lock);
                                                                    QVector<Clip*> result;
                                                                    for (const auto& c : m_clips) {
                                                                        if (time >= c->startTime() && time < c->endTime()) result.append(c.get());
                                                                    }
                                                                    return result;
                                                                }

                                                                void Track::processAudio(double position, int frames, float* buffer,
                                                                                         int channels, int sampleRate, const TempoMap& tempo) {
                                                                    QReadLocker lock(&m_lock);
                                                                    if (m_muted) {
                                                                        std::fill(buffer, buffer + frames * channels, 0.0f);
                                                                        return;
                                                                    }

                                                                    QVector<float> mix(frames * channels);
                                                                    std::fill(mix.begin(), mix.end(), 0.0f);

                                                                    for (const auto& clip : m_clips) {
                                                                        if (clip->isMuted()) continue;

                                                                        double clipTime = position - clip->startTime();
                                                                        if (clipTime < 0 || clipTime >= clip->duration()) continue;

                                                                        QVector<float> clipBuf(frames * channels);
                                                                        clip->processAudio(position, frames, clipBuf.data(), channels, sampleRate, tempo);

                                                                        // Mix with volume/pan
                                                                        for (int i = 0; i < frames; ++i) {
                                                                            for (int ch = 0; ch < channels; ++ch) {
                                                                                float panGain = (channels == 2) ?
                                                                                (ch == 0 ? (1.0f - m_pan) : (1.0f + m_pan)) * 0.5f : 1.0f;
                                                                                mix[i * channels + ch] += clipBuf[i * channels + ch] * m_volume * panGain;
                                                                            }
                                                                        }
                                                                    }

                                                                    std::copy(mix.begin(), mix.end(), buffer);
                                                                                         }

                                                                                         void Track::collectMidiEvents(double position, double duration, QVector<NoteEvent>& events) {
                                                                                             QReadLocker lock(&m_lock);
                                                                                             for (const auto& clip : m_clips) {
                                                                                                 if (clip->isMuted()) continue;
                                                                                                 if (position < clip->startTime() || position >= clip->endTime()) continue;

                                                                                                 double clipStart = position - clip->startTime();
                                                                                                 events.append(clip->getMidiEvents(clipStart, clipStart + duration));
                                                                                             }
                                                                                         }

                                                                                         QVector<NotationClip*> Track::notationClips() const {
                                                                                             QVector<NotationClip*> result;
                                                                                             for (const auto& c : m_clips) {
                                                                                                 if (c->type() == ClipType::Notation) {
                                                                                                     result.append(static_cast<NotationClip*>(c.get()));
                                                                                                 }
                                                                                             }
                                                                                             return result;
                                                                                         }

                                                                                         // =============================================================================
                                                                                         // DAW Engine Implementation
                                                                                         // =============================================================================

                                                                                         DAWEngine::DAWEngine(QObject* parent) : QObject(parent) {}

                                                                                         Track* DAWEngine::addTrack(const QString& name) {
                                                                                             auto track = std::make_unique<Track>(name.isEmpty() ?
                                                                                             QString("Track %1").arg(m_tracks.size() + 1) : name, this);
                                                                                             Track* ptr = track.get();
                                                                                             m_tracks.append(std::move(track));
                                                                                             emit trackAdded(ptr);
                                                                                             return ptr;
                                                                                         }

                                                                                         void DAWEngine::removeTrack(int index) {
                                                                                             if (index < 0 || index >= m_tracks.size()) return;
                                                                                             m_tracks.removeAt(index);
                                                                                             emit trackRemoved(index);
                                                                                         }

                                                                                         NotationClip* DAWEngine::createNotationClip(int trackIndex, const QString& name) {
                                                                                             Track* track = (trackIndex >= 0 && trackIndex < m_tracks.size()) ?
                                                                                             m_tracks[trackIndex].get() : addTrack(name);

                                                                                             auto clip = std::make_unique<NotationClip>();
                                                                                             clip->createEmptyScore(name);
                                                                                             NotationClip* ptr = clip.get();
                                                                                             track->addClip(std::move(clip));
                                                                                             return ptr;
                                                                                         }

                                                                                         NotationClip* DAWEngine::importScore(const QString& path, int trackIndex) {
                                                                                             Track* track = (trackIndex >= 0 && trackIndex < m_tracks.size()) ?
                                                                                             m_tracks[trackIndex].get() : addTrack(QFileInfo(path).baseName());

                                                                                             auto clip = std::make_unique<NotationClip>();
                                                                                             auto score = std::make_unique<Score>();

                                                                                             if (path.endsWith(".xml", Qt::CaseInsensitive) ||
                                                                                                 path.endsWith(".musicxml", Qt::CaseInsensitive)) {
                                                                                                 if (score->loadMusicXML(path)) {
                                                                                                     clip->setScore(std::move(score));
                                                                                                 }
                                                                                                 } else if (path.endsWith(".mid", Qt::CaseInsensitive) ||
                                                                                                     path.endsWith(".midi", Qt::CaseInsensitive)) {
                                                                                                     if (score->loadMIDI(path)) {
                                                                                                         clip->setScore(std::move(score));
                                                                                                     }
                                                                                                     }

                                                                                                     if (!clip->isEmpty()) {
                                                                                                         NotationClip* ptr = clip.get();
                                                                                                         track->addClip(std::move(clip));
                                                                                                         return ptr;
                                                                                                     }

                                                                                                     return nullptr;
                                                                                         }

                                                                                         void DAWEngine::processAudio(float* buffer, int frames, int channels, int sampleRate) {
                                                                                             double pos = m_transport.position();

                                                                                             if (m_transport.state() != TransportState::Playing) {
                                                                                                 std::fill(buffer, buffer + frames * channels, 0.0f);
                                                                                                 return;
                                                                                             }

                                                                                             if (m_mixBuffer.size() < frames * channels) {
                                                                                                 m_mixBuffer.resize(frames * channels);
                                                                                             }

                                                                                             std::fill(buffer, buffer + frames * channels, 0.0f);

                                                                                             for (const auto& track : m_tracks) {
                                                                                                 if (track->isMuted()) continue;

                                                                                                 track->processAudio(pos, frames, m_mixBuffer.data(), channels, sampleRate,
                                                                                                                     *m_transport.tempoMap());

                                                                                                 for (int i = 0; i < frames * channels; ++i) {
                                                                                                     buffer[i] += m_mixBuffer[i];
                                                                                                 }
                                                                                             }

                                                                                             // Update position
                                                                                             double duration = frames / double(sampleRate);
                                                                                             m_transport.setPosition(pos + duration);
                                                                                         }

} // namespace Aegis
