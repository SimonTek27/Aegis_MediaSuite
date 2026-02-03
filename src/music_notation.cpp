// music_notation.cpp - Music Notation Implementation
#include "music_notation.h"
#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>

namespace Aegis {

    // =============================================================================
    // Pitch Implementation
    // =============================================================================

    void Pitch::fromMidi(int midi) {
        midiNote = midi;
        pitchClass = static_cast<PitchClass>(midi % 12);
        octave = (midi / 12) - 1;
        accidental = Accidental::None;  // Default, may need recalculation based on key
    }

    void Pitch::toMidi() {
        int pc = static_cast<int>(pitchClass);
        if (accidental == Accidental::Sharp || accidental == Accidental::DoubleSharp) {
            // Adjust for sharps
        }
        midiNote = (octave + 1) * 12 + pc;
    }

    QString Pitch::toString() const {
        static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return QString("%1%2").arg(names[static_cast<int>(pitchClass)]).arg(octave);
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
            if (isTuplet()) {
                base *= static_cast<double>(denominator) / numerator;
            }
            return base;
    }

    double Duration::toSeconds(double tempoBpm) const {
        double quarters = toQuarterNotes();
        double secondsPerBeat = 60.0 / tempoBpm;
        return quarters * secondsPerBeat;
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
        notes.append(n);

        // Sort by tick position
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
        // Linear interpolation for now
        return static_cast<int>((tick / static_cast<double>(m_lengthTicks)) * width);
    }

    int Measure::pixelToTick(double x) const {
        return static_cast<int>((x / width) * m_lengthTicks);
    }

    int Measure::filledTicks() const {
        int maxTick = 0;
        for (const auto& note : notes) {
            int endTick = note.tickPosition + static_cast<int>(note.duration.toQuarterNotes() * 480);
            maxTick = qMax(maxTick, endTick);
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
        double tempo = 120.0;
        for (const auto& t : tempos) {
            // Find applicable tempo marking
            // Simplified: just return last tempo before tick
        }
        return tempo;
    }

    double Measure::absoluteTimeAt(int tick) const {
        // Calculate cumulative time from score start
        // This requires knowing previous measures
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
        measures.append(std::move(measure));
        emit measureAdded(ptr);
        return ptr;
    }

    void Staff::removeMeasure(int index) {
        if (index >= 0 && index < measures.size()) {
            measures.removeAt(index);
            emit measureRemoved(index);
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
        return (m_lines - 1) * EngravingSettings::defaults().spatium + 50.0;  // Extra space
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
        if (staves.isEmpty()) return nullptr;
        // Use first staff as reference
        for (auto& m : staves.first()->measures) {
            if (tick >= m->startTick() && tick < m->startTick() + m->lengthTicks()) {
                return m.get();
            }
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
    // File I/O - MusicXML
    // =============================================================================

    bool Score::loadMusicXML(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return false;

        QXmlStreamReader xml(&file);

        staves.clear();

        while (!xml.atEnd()) {
            xml.readNext();

            if (xml.isStartElement()) {
                if (xml.name() == QStringLiteral("part")) {
                    // New staff/part
                    QString partName = xml.attributes().value("id").toString();
                    Staff* staff = addStaff(partName);

                    // Parse measures
                    while (!(xml.isEndElement() && xml.name() == QStringLiteral("part"))) {
                        xml.readNext();
                        if (xml.isStartElement() && xml.name() == QStringLiteral("measure")) {
                            int measureNum = xml.attributes().value("number").toInt();
                            Measure* measure = staff->addMeasure(measureNum);

                            // Parse measure content
                            int currentTick = 0;
                            while (!(xml.isEndElement() && xml.name() == QStringLiteral("measure"))) {
                                xml.readNext();

                                if (xml.isStartElement() && xml.name() == QStringLiteral("note")) {
                                    Note note;
                                    // Parse pitch
                                    while (!(xml.isEndElement() && xml.name() == QStringLiteral("note"))) {
                                        xml.readNext();
                                        if (xml.isStartElement()) {
                                            if (xml.name() == QStringLiteral("pitch")) {
                                                int step = 0, alter = 0, octave = 4;
                                                while (!(xml.isEndElement() && xml.name() == QStringLiteral("pitch"))) {
                                                    xml.readNext();
                                                    if (xml.name() == QStringLiteral("step")) {
                                                        QString s = xml.readElementText();
                                                        step = QString("CDEFGAB").indexOf(s);
                                                    } else if (xml.name() == QStringLiteral("alter")) {
                                                        alter = xml.readElementText().toInt();
                                                    } else if (xml.name() == QStringLiteral("octave")) {
                                                        octave = xml.readElementText().toInt();
                                                    }
                                                }
                                                // Convert to MIDI and create Pitch
                                                int midi = (octave + 1) * 12 + step + alter;
                                                note.pitch = Pitch(midi);
                                                if (alter > 0) note.pitch.accidental = Accidental::Sharp;
                                                else if (alter < 0) note.pitch.accidental = Accidental::Flat;

                                            } else if (xml.name() == QStringLiteral("duration")) {
                                                int divisions = xml.readElementText().toInt();
                                                // Convert divisions to Duration
                                                // Simplified: assume divisions per quarter = 480
                                                note.duration = Duration();
                                                note.duration.type = NoteDuration::Quarter; // Simplified

                                            } else if (xml.name() == QStringLiteral("rest")) {
                                                note.isRest = true;
                                            }
                                        }
                                    }
                                    note.tickPosition = currentTick;
                                    note.measure = measure;
                                    currentTick += static_cast<int>(note.duration.toQuarterNotes() * m_ticksPerQuarter);
                                    measure->notes.append(note);
                                }
                            }
                        }
                    }
                } else if (xml.name() == QStringLiteral("work-title")) {
                    m_title = xml.readElementText();
                } else if (xml.name() == QStringLiteral("creator")) {
                    QString type = xml.attributes().value("type").toString();
                    if (type == "composer") m_composer = xml.readElementText();
                    else if (type == "lyricist") m_lyricist = xml.readElementText();
                }
            }
        }

        emit modified();
        return !xml.hasError();
    }

    bool Score::saveMusicXML(const QString& path) const {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;

        QXmlStreamWriter xml(&file);
        xml.setAutoFormatting(true);

        xml.writeStartDocument();
        xml.writeDTD("<!DOCTYPE score-partwise PUBLIC '-//Recordare//DTD MusicXML 4.0 Partwise//EN' 'http://www.musicxml.org/dtds/partwise.dtd'>");

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
                    xml.writeStartElement("attributes");
                    xml.writeTextElement("divisions", QString::number(m_ticksPerQuarter));

                    xml.writeStartElement("time");
                    xml.writeTextElement("beats", QString::number(m_defaultTimeSig.numerator));
                    xml.writeTextElement("beat-type", QString::number(m_defaultTimeSig.denominator));
                    xml.writeEndElement();

                    xml.writeStartElement("clef");
                    xml.writeTextElement("sign", "G");  // Simplified
                    xml.writeTextElement("line", "2");
                    xml.writeEndElement();

                    xml.writeEndElement();
                }

                // Notes
                for (const auto& note : measure->notes) {
                    xml.writeStartElement("note");

                    if (note.isRest) {
                        xml.writeEmptyElement("rest");
                    } else {
                        xml.writeStartElement("pitch");
                        static const char* steps = "CDEFGAB";
                        xml.writeTextElement("step", QString(steps[note.pitch.pitchClass % 7])); // Simplified
                        xml.writeTextElement("octave", QString::number(note.pitch.octave));
                        xml.writeEndElement();
                    }

                    int divisions = static_cast<int>(note.duration.toQuarterNotes() * m_ticksPerQuarter);
                    xml.writeTextElement("duration", QString::number(divisions));

                    xml.writeEndElement();
                }

                xml.writeEndElement(); // measure
            }

            xml.writeEndElement(); // part
        }

        xml.writeEndElement(); // score-partwise
        xml.writeEndDocument();

        return true;
    }

    // =============================================================================
    // MIDI Import/Export
    // =============================================================================

    bool Score::loadMIDI(const QString& path) {
        // Simplified MIDI import - would use proper MIDI library
        // This is a placeholder for integration with existing MIDI infrastructure

        // Clear existing
        staves.clear();

        // Create single staff for all MIDI tracks or separate staves per track
        Staff* staff = addStaff("MIDI Import");

        // Would parse MIDI file and create notes
        // For now, create a placeholder measure

        Measure* m = staff->addMeasure(1);
        m->timeSigs.append(m_defaultTimeSig);

        // Add some example notes to demonstrate functionality
        Note note1(Pitch(60), Duration()); // Middle C quarter note
        m->addNote(note1, 0);

        Note note2(Pitch(64), Duration());
        note2.tickPosition = 480;
        m->addNote(note2, 0);

        emit modified();
        return true;
    }

    bool Score::saveMIDI(const QString& path) const {
        // Export to MIDI format
        // Would create MIDI events from all notes in all staves

        // Track 0: Tempo, time signature, key signature
        // Track 1+: Notes per staff

        return true; // Placeholder
    }

    // =============================================================================
    // Rendering
    // ====================================================================

    EngravingSettings& EngravingSettings::defaults() {
        static EngravingSettings instance;
        return instance;
    }

    ScoreRenderer::ScoreRenderer(Score* score) : m_score(score) {
        // Initialize SMuFL codes
        if (s_smuflCodes.isEmpty()) {
            s_smuflCodes["noteheadBlack"] = 0xE0A4;
            s_smuflCodes["noteheadHalf"] = 0xE0A3;
            s_smuflCodes["noteheadWhole"] = 0xE0A2;
            s_smuflCodes["gClef"] = 0xE050;
            s_smuflCodes["fClef"] = 0xE062;
            s_smuflCodes["sharp"] = 0xE262;
            s_smuflCodes["flat"] = 0xE260;
            s_smuflCodes["natural"] = 0xE261;
        }
    }

    void ScoreRenderer::render(QPainter* painter, const QRectF& rect, int startStaff, int staffCount) {
        if (!m_score || m_score->staves.isEmpty()) return;

        painter->setRenderHint(QPainter::Antialiasing);

        int endStaff = staffCount < 0 ? m_score->staves.size() : qMin(startStaff + staffCount, m_score->staves.size());

        double y = rect.top();

        for (int i = startStaff; i < endStaff; ++i) {
            Staff* staff = m_score->staves[i].get();

            // Draw staff lines
            painter->setPen(QPen(Qt::black, 1.0));
            double lineSpacing = m_settings.spatium;
            double staffWidth = rect.width();

            for (int line = 0; line < staff->lines(); ++line) {
                double ly = y + line * lineSpacing;
                painter->drawLine(QPointF(rect.left(), ly), QPointF(rect.left() + staffWidth, ly));
            }

            // Draw measures
            double x = rect.left() + 50;  // Clef width
            for (auto& measure : staff->measures) {
                double mWidth = measureWidth(measure.get());

                // Draw barlines
                painter->drawLine(QPointF(x + mWidth, y), QPointF(x + mWidth, y + (staff->lines() - 1) * lineSpacing));

                // Draw notes
                for (const auto& note : measure->notes) {
                    if (note.isRest) {
                        renderRest(painter, note, QPointF(x + measure->tickToPixel(note.tickPosition), y));
                    } else {
                        renderNote(painter, note, QPointF(x + measure->tickToPixel(note.tickPosition), y));
                    }
                }

                x += mWidth;
            }

            y += staff->height() + m_score->staffDistance();
        }
    }

    void ScoreRenderer::renderNote(QPainter* painter, const Note& note, const QPointF& pos) {
        painter->setFont(m_settings.musicFont);

        // Calculate staff line position
        // Middle C (C4) is typically on the first ledger line below treble staff
        // Staff lines from bottom: E4, G4, B4, D5, F5
        int line = note.staffLine;

        double noteY = pos.y() + (10 - line) * (m_settings.spatium / 2.0);
        double noteX = pos.x();

        // Draw notehead
        QString noteChar = QChar(s_smuflCodes["noteheadBlack"]);
        if (note.duration.type == NoteDuration::Half) {
            noteChar = QChar(s_smuflCodes["noteheadHalf"]);
        } else if (note.duration.type == NoteDuration::Whole) {
            noteChar = QChar(s_smuflCodes["noteheadWhole"]);
        }

        painter->drawText(QPointF(noteX, noteY), noteChar);

        // Draw stem
        if (note.duration.type != NoteDuration::Whole) {
            double stemX = note.stemUp ? noteX + m_settings.noteHeadWidth * 0.9 : noteX;
            double stemY1 = noteY - m_settings.spatium * 0.5;
            double stemY2 = note.stemUp ? stemY1 - m_settings.stemLength : stemY1 + m_settings.stemLength;

            painter->setPen(QPen(Qt::black, m_settings.stemWidth));
            painter->drawLine(QPointF(stemX, stemY1), QPointF(stemX, stemY2));
        }

        // Draw accidental if needed
        if (note.pitch.accidental != Accidental::None) {
            QString accChar;
            switch (note.pitch.accidental) {
                case Accidental::Sharp: accChar = QChar(s_smuflCodes["sharp"]); break;
                case Accidental::Flat: accChar = QChar(s_smuflCodes["flat"]); break;
                case Accidental::Natural: accChar = QChar(s_smuflCodes["natural"]); break;
                default: break;
            }
            if (!accChar.isEmpty()) {
                painter->drawText(QPointF(noteX - m_settings.spatium * 1.5, noteY), accChar);
            }
        }
    }

    void ScoreRenderer::renderRest(QPainter* painter, const Note& rest, const QPointF& pos) {
        // Draw rest symbols based on duration
        painter->setFont(m_settings.musicFont);
        // Simplified: draw a rectangle for rests
        double h = m_settings.spatium * 0.5;
        double y = pos.y() + m_settings.spatium * 2;  // Center on staff

        painter->fillRect(QRectF(pos.x(), y, m_settings.spatium * 0.8, h), Qt::black);
    }

    void ScoreRenderer::renderClef(QPainter* painter, const Clef& clef, const QPointF& pos) {
        painter->setFont(m_settings.musicFont);
        QString clefChar;
        switch (clef.type) {
            case ClefType::Treble: clefChar = QChar(s_smuflCodes["gClef"]); break;
            case ClefType::Bass: clefChar = QChar(s_smuflCodes["fClef"]); break;
            default: clefChar = QChar(s_smuflCodes["gClef"]); break;
        }
        painter->drawText(pos, clefChar);
    }

    void ScoreRenderer::renderKeySig(QPainter* painter, const KeySig& key, const QPointF& pos) {
        // Draw sharps or flats based on key signature
        painter->setFont(m_settings.musicFont);

        int accidentals = key.accidentals();
        QString accChar = key.isSharpKey() ? QChar(s_smuflCodes["sharp"]) : QChar(s_smuflCodes["flat"]);

        for (int i = 0; i < qAbs(accidentals); ++i) {
            double x = pos.x() + i * m_settings.spatium * 0.8;
            // Position based on key signature pattern
            double y = pos.y() + i * m_settings.spatium * 0.5;
            painter->drawText(QPointF(x, y), accChar);
        }
    }

    double ScoreRenderer::measureWidth(Measure* measure) const {
        if (!measure) return 100.0;

        // Calculate based on content
        double baseWidth = 80.0;  // Minimum width
        double contentWidth = 0;

        for (const auto& note : measure->notes) {
            contentWidth = qMax(contentWidth, measure->tickToPixel(note.tickPosition) + 50.0);
        }

        return qMax(baseWidth, contentWidth);
    }

    void ScoreRenderer::doLayout() {
        // Full score layout
        // Calculate optimal spacing, line breaks, page breaks
        for (auto& staff : m_score->staves) {
            for (auto& measure : staff->measures) {
                layoutMeasure(measure.get());
            }
        }
    }

    void ScoreRenderer::layoutMeasure(Measure* measure) {
        if (!measure) return;

        // Calculate note positions
        for (auto& note : measure->notes) {
            // Staff line calculation based on pitch
            // Middle C (C4, MIDI 60) is on first ledger line below staff
            // Staff lines (treble): E4(64), G4(67), B4(71), D5(74), F5(77)
            if (!note.isRest) {
                int midi = note.pitch.midiNote;
                // Simplified line calculation for treble clef
                note.staffLine = (77 - midi) / 2;  // Approximate
            }
        }
    }

    QMap<QString, uint> ScoreRenderer::s_smuflCodes;

} // namespace Aegis
