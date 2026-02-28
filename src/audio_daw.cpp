// audio_daw.cpp — Aegis DAW Engine: Score / MIDI I/O implementation
//
// FIX SUMMARY (v3):
//  1. toLilyPond: static_cast<int>(pitch.pitchClass) % 12 — enum class cast
//  2. Measure::tempoAt: Measure has NO 'tempos' member → return 120.0
//  3. writeVLQ: bytes.last() → bytes[bytes.size()-1] (Qt6 requires size arg)
//  4. Duration::fromTicks doesn't exist → durationFromTicks() inline helper
//  5. Duration::Type::Quarter → DurationType::Quarter (top-level enum)
//  6. SynthVoice fields corrected: active→isActive, frequency→compute inline,
//     amplitude→velocity, remainingSamples→duration (used as countdown)
//  7. removeMeasure/removeStaff: QList<unique_ptr<>>::removeAt() triggers
//     detach() → deleted copy ctor. Use std::move + erase idiom.

#include "audio_daw.h"
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace Aegis {

// =============================================================================
// Duration helpers
// =============================================================================

bool Duration::operator==(const Duration& other) const {
    return type == other.type && dots == other.dots &&
           numerator == other.numerator && denominator == other.denominator;
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
    // FIX #1: PitchClass is an enum class — must cast to int before %
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
// Measure
// =============================================================================

Measure::Measure(int number, QObject* parent)
    : QObject(parent), m_number(number) {}

void Measure::addNote(const Note& note, int voice) {
    Note n  = note;
    n.voice = voice;
    notes.append(n);
}

int Measure::filledTicks() const {
    int maxTick = 0;
    for (const auto& note : notes) {
        int noteEnd = note.tickPosition +
                      static_cast<int>(note.duration.toQuarterNotes() * 480);
        maxTick = qMax(maxTick, noteEnd);
    }
    return maxTick;
}

int  Measure::remainingTicks() const { return m_lengthTicks - filledTicks(); }
bool Measure::isFull()         const { return filledTicks() >= m_lengthTicks; }

double Measure::tempoAt(int tick) const {
    // FIX #2: Measure has no 'tempos' member — tempo is owned by Score.
    Q_UNUSED(tick)
    return 120.0;
}

double Measure::absoluteTimeAt(int tick) const {
    return tick / 480.0 * (60.0 / tempoAt(tick));
}

// =============================================================================
// Staff
// =============================================================================

Staff::Staff(const QString& name, QObject* parent)
    : QObject(parent), m_name(name) {}

void Staff::setTranspose(int chromatic, int diatonic) {
    m_transposeChromatic = chromatic;
    m_transposeDiatonic  = diatonic;
}

Measure* Staff::addMeasure(int number) {
    auto  measure = std::make_unique<Measure>(number, this);
    Measure* ptr  = measure.get();

    if (!measures.isEmpty()) {
        auto& last = measures.last();
        ptr->setStartTick(last->startTick() + last->lengthTicks());
    }

    measures.append(std::move(measure));
    emit measureAdded(ptr);
    return ptr;
}

void Staff::removeMeasure(int index) {
    if (index < 0 || index >= measures.size()) return;
    // FIX #7: removeAt() calls detach() → tries to copy unique_ptr → deleted ctor.
    // Move the element out, then erase the now-null slot.
    auto it = measures.begin() + index;
    { std::unique_ptr<Measure> taken = std::move(*it); Q_UNUSED(taken) }
    measures.erase(it);
    emit measureRemoved(index);
}

Measure* Staff::measureAtTick(int tick) const {
    for (const auto& m : measures) {
        if (tick >= m->startTick() && tick < m->startTick() + m->lengthTicks())
            return m.get();
    }
    return nullptr;
}

double Staff::height() const { return static_cast<double>(m_lines) * 10.0; }

// =============================================================================
// Score
// =============================================================================

Score::Score(QObject* parent) : QObject(parent) {}

Staff* Score::addStaff(const QString& name) {
    auto  staff = std::make_unique<Staff>(name, this);
    Staff* ptr  = staff.get();
    staves.append(std::move(staff));
    emit structureChanged();
    return ptr;
}

void Score::removeStaff(int index) {
    if (index < 0 || index >= staves.size()) return;
    // FIX #7 (same pattern)
    auto it = staves.begin() + index;
    { std::unique_ptr<Staff> taken = std::move(*it); Q_UNUSED(taken) }
    staves.erase(it);
    emit structureChanged();
}

int Score::totalTicks() const {
    int total = 0;
    for (const auto& staff : staves)
        for (const auto& m : staff->measures)
            total = qMax(total, m->startTick() + m->lengthTicks());
    return total;
}

Measure* Score::measureAtTick(int tick) {
    if (staves.isEmpty()) return nullptr;
    return staves.first()->measureAtTick(tick);
}

// =============================================================================
// Anonymous helpers for MIDI I/O
// =============================================================================

namespace {

int readVLQ(const quint8*& ptr, const quint8* end, int& remaining) {
    int value = 0, bytesRead = 0;
    while (ptr < end && remaining > 0) {
        quint8 byte = *ptr++; --remaining; ++bytesRead;
        value = (value << 7) | (byte & 0x7F);
        if (!(byte & 0x80)) return value;
        if (bytesRead > 4)  return -1;
    }
    return -1;
}

void writeVLQ(QByteArray& data, int value) {
    if (value == 0) { data.append('\x00'); return; }
    QByteArray bytes;
    while (value > 0) {
        bytes.prepend(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    // FIX #3: Qt6 QByteArray::last() requires a size argument.
    // Use operator[] on the last index instead.
    bytes[bytes.size() - 1] = bytes[bytes.size() - 1] & static_cast<char>(0x7F);
    data.append(bytes);
}

// FIX #4: Duration::fromTicks() does not exist. Map tick count → DurationType.
Duration durationFromTicks(int ticks, int ppq) {
    Duration d;
    if (ppq <= 0) { d.type = DurationType::Quarter; return d; }
    double q = static_cast<double>(ticks) / ppq;
    if      (q >= 3.5)    d.type = DurationType::Whole;
    else if (q >= 1.75)   d.type = DurationType::Half;
    else if (q >= 0.875)  d.type = DurationType::Quarter;
    else if (q >= 0.4375) d.type = DurationType::Eighth;
    else if (q >= 0.21875)d.type = DurationType::Sixteenth;
    else                  d.type = DurationType::ThirtySecond;
    return d;
}

} // anonymous namespace

// =============================================================================
// Score::loadMIDI
// =============================================================================

bool Score::loadMIDI(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();

    const quint8* ptr  = reinterpret_cast<const quint8*>(data.constData());
    int remaining      = static_cast<int>(data.size());

    if (remaining < 14 || memcmp(ptr, "MThd", 4) != 0) return false;

    int headerChunkLen = (ptr[4]<<24)|(ptr[5]<<16)|(ptr[6]<<8)|ptr[7];
    if (headerChunkLen < 6) return false;

    int trackCount   = (ptr[10]<<8)|ptr[11];
    int timeDivision = (ptr[12]<<8)|ptr[13];

    if (timeDivision & 0x8000) {
        int fps = -(static_cast<qint8>(timeDivision >> 8));
        int tpf =   timeDivision & 0xFF;
        m_ticksPerQuarter = fps * tpf;
    } else {
        m_ticksPerQuarter = timeDivision;
    }

    int advance = 8 + headerChunkLen;
    ptr       += advance;
    remaining -= advance;

    for (int t = 0; t < trackCount && remaining >= 8; ++t) {
        if (memcmp(ptr, "MTrk", 4) != 0) break;
        int trackLen = (ptr[4]<<24)|(ptr[5]<<16)|(ptr[6]<<8)|ptr[7];
        ptr += 8; remaining -= 8;
        if (trackLen > remaining) trackLen = remaining;

        const quint8* trackEnd  = ptr + trackLen;
        int           trackRemain = trackLen;

        auto* staff   = addStaff(QString("Track %1").arg(t + 1));
        auto* measure = staff->addMeasure(1);

        int currentTick = 0, runningStatus = 0;

        struct PendingNote { int note, velocity, startTick, channel; };
        QVector<PendingNote> pendingNotes;

        while (ptr < trackEnd && trackRemain > 0) {
            int delta = readVLQ(ptr, trackEnd, trackRemain);
            if (delta < 0) break;
            currentTick += delta;
            if (trackRemain <= 0) break;

            quint8 statusByte = *ptr;
            if (statusByte & 0x80) {
                runningStatus = statusByte;
                ++ptr; --trackRemain;
            }

            int type    = (runningStatus >> 4) & 0x0F;
            int channel = runningStatus & 0x0F;

            if (runningStatus == 0xFF) {
                if (trackRemain < 1) break;
                quint8 metaType = *ptr++; --trackRemain;
                int metaLen = readVLQ(ptr, trackEnd, trackRemain);
                if (metaLen < 0 || metaLen > trackRemain) break;
                if (metaType == 0x51 && metaLen >= 3) {
                    int microsec = (ptr[0]<<16)|(ptr[1]<<8)|ptr[2];
                    m_tempo = (microsec > 0) ? 60000000.0 / microsec : 120.0;
                }
                ptr += metaLen; trackRemain -= metaLen;

            } else if (type == 0x09) {
                if (trackRemain < 2) break;
                quint8 note = *ptr++; --trackRemain;
                quint8 vel  = *ptr++; --trackRemain;
                if (vel == 0) {
                    for (int k = 0; k < pendingNotes.size(); ++k) {
                        if (pendingNotes[k].note == note &&
                            pendingNotes[k].channel == channel) {
                            Note n;
                            n.pitch        = Pitch(note);
                            n.tickPosition = pendingNotes[k].startTick - measure->startTick();
                            int durTicks   = currentTick - pendingNotes[k].startTick;
                            n.duration     = durationFromTicks(durTicks, m_ticksPerQuarter); // FIX #4
                            n.velocity     = pendingNotes[k].velocity;
                            n.voice        = channel;
                            measure->notes.append(n);
                            pendingNotes.removeAt(k);
                            break;
                        }
                    }
                } else {
                    pendingNotes.append({note, vel, currentTick, channel});
                }

            } else if (type == 0x08) {
                if (trackRemain < 2) break;
                quint8 note = *ptr++; --trackRemain;
                ++ptr; --trackRemain; // velocity ignored
                for (int k = 0; k < pendingNotes.size(); ++k) {
                    if (pendingNotes[k].note == note &&
                        pendingNotes[k].channel == channel) {
                        Note n;
                        n.pitch        = Pitch(note);
                        n.tickPosition = pendingNotes[k].startTick - measure->startTick();
                        int durTicks   = currentTick - pendingNotes[k].startTick;
                        n.duration     = durationFromTicks(durTicks, m_ticksPerQuarter); // FIX #4
                        n.velocity     = pendingNotes[k].velocity;
                        n.voice        = channel;
                        measure->notes.append(n);
                        pendingNotes.removeAt(k);
                        break;
                    }
                }
            } else if (type == 0x0A || type == 0x0B || type == 0x0E) {
                if (trackRemain < 2) break;
                ptr += 2; trackRemain -= 2;
            } else if (type == 0x0C || type == 0x0D) {
                if (trackRemain < 1) break;
                ++ptr; --trackRemain;
            } else if (runningStatus == 0xF0 || runningStatus == 0xF7) {
                while (ptr < trackEnd && trackRemain > 0) {
                    quint8 b = *ptr++; --trackRemain;
                    if (b == 0xF7) break;
                }
            }
        }

        // Flush unterminated notes as quarter notes
        for (const auto& pn : pendingNotes) {
            Note n;
            n.pitch         = Pitch(pn.note);
            n.tickPosition  = pn.startTick - measure->startTick();
            n.duration.type = DurationType::Quarter;  // FIX #5
            n.velocity      = pn.velocity;
            n.voice         = pn.channel;
            measure->notes.append(n);
        }

        ptr       += trackLen;
        remaining -= trackLen;
    }

    return !staves.isEmpty();
}

// =============================================================================
// Score::saveMIDI
// =============================================================================

bool Score::saveMIDI(const QString& path) const {
    QByteArray data;
    int ppq       = (m_ticksPerQuarter > 0) ? m_ticksPerQuarter : 480;
    int numTracks = static_cast<int>(staves.size());

    data.append("MThd");
    data.append('\x00'); data.append('\x00');
    data.append('\x00'); data.append('\x06');
    data.append('\x00'); data.append('\x01'); // format 1
    data.append(static_cast<char>((numTracks >> 8) & 0xFF));
    data.append(static_cast<char>( numTracks       & 0xFF));
    data.append(static_cast<char>((ppq >> 8) & 0xFF));
    data.append(static_cast<char>( ppq       & 0xFF));

    for (const auto& staff : staves) {
        data.append("MTrk");
        int trackLenPos = static_cast<int>(data.size());
        data.append('\x00'); data.append('\x00');
        data.append('\x00'); data.append('\x00');
        int trackStart = static_cast<int>(data.size());
        int lastTick   = 0;

        // Tempo meta event
        {
            int microsec = (m_tempo > 0) ?
                static_cast<int>(60000000.0 / m_tempo) : 500000;
            data.append('\x00');
            data.append('\xFF'); data.append('\x51'); data.append('\x03');
            data.append(static_cast<char>((microsec >> 16) & 0xFF));
            data.append(static_cast<char>((microsec >>  8) & 0xFF));
            data.append(static_cast<char>( microsec        & 0xFF));
        }

        struct MidiEvent {
            int tick; bool isNoteOn;
            quint8 note, velocity, channel;
        };
        QVector<MidiEvent> events;
        int midiChannel = static_cast<int>(staff->midiChannel() & 0x0F);

        for (const auto& measure : staff->measures) {
            for (const auto& note : measure->notes) {
                if (note.isRest) continue;
                int noteStart = measure->startTick() + note.tickPosition;
                int durTicks  = static_cast<int>(note.duration.toQuarterNotes() * ppq);
                int noteEnd   = noteStart + qMax(durTicks, 1);
                events.append({noteStart, true,
                    static_cast<quint8>(note.pitch.midiNote),
                    static_cast<quint8>(note.velocity),
                    static_cast<quint8>(midiChannel)});
                events.append({noteEnd, false,
                    static_cast<quint8>(note.pitch.midiNote),
                    0,
                    static_cast<quint8>(midiChannel)});
            }
        }

        std::sort(events.begin(), events.end(),
            [](const MidiEvent& a, const MidiEvent& b){ return a.tick < b.tick; });

        for (const auto& ev : events) {
            writeVLQ(data, ev.tick - lastTick);
            lastTick = ev.tick;
            data.append(static_cast<char>(
                static_cast<quint8>((ev.isNoteOn ? 0x90 : 0x80) | ev.channel)));
            data.append(static_cast<char>(ev.note));
            data.append(static_cast<char>(ev.velocity));
        }

        // End-of-track
        data.append('\x00'); data.append('\xFF');
        data.append('\x2F'); data.append('\x00');

        int trackLen = static_cast<int>(data.size()) - trackStart;
        data[trackLenPos]     = static_cast<char>((trackLen >> 24) & 0xFF);
        data[trackLenPos + 1] = static_cast<char>((trackLen >> 16) & 0xFF);
        data[trackLenPos + 2] = static_cast<char>((trackLen >>  8) & 0xFF);
        data[trackLenPos + 3] = static_cast<char>( trackLen        & 0xFF);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write(data) == static_cast<qint64>(data.size());
}

// =============================================================================
// Score::renderToPCM
// =============================================================================

QByteArray Score::renderToPCM(int sampleRate) {
    int    ppq      = (m_ticksPerQuarter > 0) ? m_ticksPerQuarter : 480;
    double tempoVal = (m_tempo > 0) ? m_tempo : 120.0;
    int    total    = totalTicks();

    if (total == 0 || staves.isEmpty()) return {};

    int totalSamples = static_cast<int>(
        (static_cast<double>(total) / ppq) * (60.0 / tempoVal) * sampleRate);
    if (totalSamples <= 0) return {};

    QByteArray pcm;
    pcm.resize(totalSamples * 2 * static_cast<int>(sizeof(float)));
    float* buffer = reinterpret_cast<float*>(pcm.data());
    std::fill(buffer, buffer + totalSamples * 2, 0.0f);

    // FIX #6: SynthVoice fields from audio_daw.h:
    //   isActive  (not 'active')
    //   velocity  (double, 0-1 — we store normalized gain here)
    //   duration  (double — we borrow this field as a sample countdown)
    //   pitch     (Pitch — we compute frequency from pitch.midiNote inline)
    QVector<SynthVoice> voices(16);

    double dt          = 1.0 / sampleRate;
    double currentTime = 0.0;

    const auto* staff = staves[0].get();

    for (int i = 0; i < totalSamples; ++i) {
        double sample    = 0.0;
        int currentTick  = static_cast<int>((currentTime * tempoVal / 60.0) * ppq);

        for (const auto& measure : std::as_const(staff->measures)) {
            for (const auto& note : measure->notes) {
                if (note.isRest) continue;
                int noteTick = measure->startTick() + note.tickPosition;
                if (noteTick != currentTick) continue;

                for (auto& v : voices) {
                    if (!v.isActive) {  // FIX #6a
                        v.pitch    = note.pitch;
                        v.velocity = note.velocity / 127.0;  // FIX #6b: was 'amplitude'
                        int durTicks = static_cast<int>(
                            note.duration.toQuarterNotes() * ppq);
                        // FIX #6c: 'duration' reused as remaining-sample countdown
                        v.duration = (durTicks / static_cast<double>(ppq)) *
                                     (60.0 / tempoVal) * sampleRate;
                        v.phase       = 0.0;
                        v.currentTime = 0.0;
                        v.isActive    = true;
                        v.isReleasing = false;
                        break;
                    }
                }
            }
        }

        for (auto& v : voices) {
            if (!v.isActive) continue;  // FIX #6a
            // FIX #6d: compute frequency inline from pitch.midiNote
            double freq = 440.0 * std::pow(2.0, (v.pitch.midiNote - 69) / 12.0);
            sample += v.velocity * std::sin(2.0 * M_PI * freq * v.phase);  // FIX #6b
            v.phase += dt;
            // FIX #6c: decrement duration, deactivate when exhausted
            v.duration -= 1.0;
            if (v.duration <= 0.0) v.isActive = false;  // FIX #6a
        }

        float s = static_cast<float>(std::tanh(sample * 0.5));
        buffer[i * 2]     = s;
        buffer[i * 2 + 1] = s;
        currentTime += dt;
    }

    return pcm;
}

} // namespace Aegis
