// audio_daw.cpp — Aegis DAW Engine: Score / MIDI I/O implementation
// Fixed bugs:
//   1. MIDI header length was always written as 0 (wrong arithmetic)
//   2. `size` variable was recalculated incorrectly after each track loop
//   3. Undefined Behaviour: raw pointer compared to smart-pointer via staves.first()
//   4. `size` not decremented while reading variable-length delta-time bytes
//   5. renderToPCM: staves.first() called without isEmpty() guard

#include "audio_daw.h"
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <algorithm>
#include <cstring>

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
    if (isRest) {
        return QString("r%1").arg(duration.toString());
    }

    static const char* pitchNames[] = {
        "c","cis","d","dis","e","f","fis","g","gis","a","ais","b"
    };
    QString result = pitchNames[pitch.pitchClass % 12];

    int octDiff = pitch.octave - 3;
    if (octDiff > 0) {
        for (int i = 0; i < octDiff; ++i) result += '\'';
    } else if (octDiff < 0) {
        for (int i = 0; i < -octDiff; ++i) result += ',';
    }

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
    Note n = note;
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

int Measure::remainingTicks() const { return m_lengthTicks - filledTicks(); }
bool Measure::isFull() const       { return filledTicks() >= m_lengthTicks; }

double Measure::tempoAt(int tick) const {
    double t = 120.0;
    for (const auto& tempo : tempos) {
        Q_UNUSED(tick)
        t = tempo.bpm;
    }
    return t;
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
    auto measure = std::make_unique<Measure>(number, this);
    Measure* ptr = measure.get();

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
        for (int i = index; i < measures.size(); ++i) {
            // renumber: just a placeholder hook — actual renumber logic lives in subclass
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
// Score
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

// =============================================================================
// MIDI helpers — variable-length quantity
// =============================================================================

namespace {

/// Decode a MIDI variable-length quantity.
/// Returns the decoded value and advances `ptr` past the consumed bytes.
/// Also decrements `remaining` accordingly.
/// Returns -1 on error (malformed or buffer exhausted).
int readVLQ(const quint8*& ptr, const quint8* end, int& remaining) {
    int value = 0;
    int bytesRead = 0;
    while (ptr < end && remaining > 0) {
        quint8 byte = *ptr++;
        --remaining;
        ++bytesRead;
        value = (value << 7) | (byte & 0x7F);
        if (!(byte & 0x80)) {
            return value;  // continuation bit clear → done
        }
        if (bytesRead > 4) {
            // MIDI VLQ max 4 bytes
            return -1;
        }
    }
    return -1;  // ran out of data
}

/// Append a variable-length quantity to a QByteArray.
void writeVLQ(QByteArray& data, int value) {
    if (value == 0) {
        data.append('\x00');
        return;
    }
    QByteArray bytes;
    while (value > 0) {
        bytes.prepend(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    // Clear the continuation bit on the last byte
    bytes.last() &= static_cast<char>(0x7F);
    data.append(bytes);
}

} // anonymous namespace

// =============================================================================
// Score::loadMIDI
// =============================================================================

bool Score::loadMIDI(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QByteArray data = file.readAll();
    const quint8* ptr     = reinterpret_cast<const quint8*>(data.constData());
    const quint8* dataEnd = ptr + data.size();
    int remaining         = data.size();

    // ---- Validate header ----
    if (remaining < 14 || memcmp(ptr, "MThd", 4) != 0) return false;

    // Header chunk length (bytes 4-7); for standard MIDI it must be 6
    int headerChunkLen = (ptr[4] << 24) | (ptr[5] << 16) | (ptr[6] << 8) | ptr[7];
    if (headerChunkLen < 6) return false;   // malformed

    int format      = (ptr[8]  << 8) | ptr[9];
    int trackCount  = (ptr[10] << 8) | ptr[11];
    int timeDivision= (ptr[12] << 8) | ptr[13];

    Q_UNUSED(format)

    // ---- Ticks-per-quarter ----
    if (timeDivision & 0x8000) {
        // SMPTE: convert to approximate PPQ
        int framesPerSec = -(static_cast<qint8>(timeDivision >> 8));
        int ticksPerFrame = timeDivision & 0xFF;
        m_ticksPerQuarter = framesPerSec * ticksPerFrame;
    } else {
        m_ticksPerQuarter = timeDivision;
    }

    // Advance past the 8-byte chunk header + the 6-byte header body
    int advanceBy = 8 + headerChunkLen;
    ptr       += advanceBy;
    remaining -= advanceBy;

    // ---- Parse tracks ----
    for (int t = 0; t < trackCount && remaining >= 8; ++t) {
        if (memcmp(ptr, "MTrk", 4) != 0) break;

        int trackLen = (ptr[4] << 24) | (ptr[5] << 16) | (ptr[6] << 8) | ptr[7];
        ptr       += 8;
        remaining -= 8;

        // Guard: don't read beyond available data
        if (trackLen > remaining) {
            qWarning() << "MIDI track" << t << "claims length" << trackLen
                       << "but only" << remaining << "bytes remain — clamping.";
            trackLen = remaining;
        }

        const quint8* trackEnd    = ptr + trackLen;
        int           trackRemain = trackLen;

        auto* staff   = addStaff(QString("Track %1").arg(t + 1));
        auto* measure = staff->addMeasure(1);

        int currentTick  = 0;
        int runningStatus= 0;

        struct PendingNote {
            int note;
            int velocity;
            int startTick;
            int channel;
        };
        QVector<PendingNote> pendingNotes;

        while (ptr < trackEnd && trackRemain > 0) {
            // ---- FIX #4: decrement trackRemain when reading delta-time ----
            int delta = readVLQ(ptr, trackEnd, trackRemain);
            if (delta < 0) break;
            currentTick += delta;

            if (trackRemain <= 0) break;

            quint8 statusByte = *ptr;

            // Running status: if high bit clear, reuse previous status
            if (statusByte & 0x80) {
                runningStatus = statusByte;
                ++ptr; --trackRemain;
            }

            int type    = (runningStatus >> 4) & 0x0F;
            int channel = runningStatus & 0x0F;

            if (runningStatus == 0xFF) {
                // Meta event
                if (trackRemain < 1) break;
                quint8 metaType = *ptr++; --trackRemain;
                int metaLen = readVLQ(ptr, trackEnd, trackRemain);
                if (metaLen < 0 || metaLen > trackRemain) break;

                if (metaType == 0x51 && metaLen >= 3) {
                    // Tempo
                    int microsec = (ptr[0] << 16) | (ptr[1] << 8) | ptr[2];
                    m_tempo = (microsec > 0) ? 60000000.0 / microsec : 120.0;
                }
                ptr       += metaLen;
                trackRemain -= metaLen;

            } else if (type == 0x09) {
                // Note On
                if (trackRemain < 2) break;
                quint8 note = *ptr++; --trackRemain;
                quint8 vel  = *ptr++; --trackRemain;

                if (vel == 0) {
                    // Velocity 0 = note off
                    for (int k = 0; k < pendingNotes.size(); ++k) {
                        if (pendingNotes[k].note == note && pendingNotes[k].channel == channel) {
                            Note n;
                            n.pitch        = Pitch(note);
                            n.tickPosition = pendingNotes[k].startTick - measure->startTick();
                            int durTicks   = currentTick - pendingNotes[k].startTick;
                            n.duration     = Duration::fromTicks(durTicks, m_ticksPerQuarter);
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
                // Note Off
                if (trackRemain < 2) break;
                quint8 note = *ptr++; --trackRemain;
                /*vel*/       ++ptr; --trackRemain;   // velocity ignored for note-off

                for (int k = 0; k < pendingNotes.size(); ++k) {
                    if (pendingNotes[k].note == note && pendingNotes[k].channel == channel) {
                        Note n;
                        n.pitch        = Pitch(note);
                        n.tickPosition = pendingNotes[k].startTick - measure->startTick();
                        int durTicks   = currentTick - pendingNotes[k].startTick;
                        n.duration     = Duration::fromTicks(durTicks, m_ticksPerQuarter);
                        n.velocity     = pendingNotes[k].velocity;
                        n.voice        = channel;
                        measure->notes.append(n);
                        pendingNotes.removeAt(k);
                        break;
                    }
                }

            } else if (type == 0x0A || type == 0x0B || type == 0x0E) {
                // Poly-aftertouch, Control change, Pitch bend: 2 data bytes
                if (trackRemain < 2) break;
                ptr += 2; trackRemain -= 2;
            } else if (type == 0x0C || type == 0x0D) {
                // Program change, Channel aftertouch: 1 data byte
                if (trackRemain < 1) break;
                ++ptr; --trackRemain;
            } else if (runningStatus == 0xF0 || runningStatus == 0xF7) {
                // SysEx: read until 0xF7
                while (ptr < trackEnd && trackRemain > 0) {
                    quint8 b = *ptr++; --trackRemain;
                    if (b == 0xF7) break;
                }
            } else {
                // Unknown — skip one byte to avoid infinite loop
                if (trackRemain > 0) { ++ptr; --trackRemain; }
            }
        }

        // ---- Flush any unmatched note-ons ----
        for (const auto& pn : pendingNotes) {
            Note n;
            n.pitch        = Pitch(pn.note);
            n.tickPosition = pn.startTick - measure->startTick();
            n.duration.type= Duration::Type::Quarter;
            n.velocity     = pn.velocity;
            n.voice        = pn.channel;
            measure->notes.append(n);
        }

        // ---- FIX #2: advance ptr and remaining correctly after each track ----
        ptr       = trackEnd;
        remaining -= trackLen;  // (not the broken "trackEnd arithmetic" that gave data.size() each time)
    }

    emit modified();
    return true;
}

// =============================================================================
// Score::saveMIDI
// =============================================================================

bool Score::saveMIDI(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QByteArray data;

    // ---- MIDI Header chunk ----
    // Format: MThd <length=6> <format=1> <numTracks> <ticksPerQuarter>
    data.append("MThd", 4);

    // FIX #1: The MIDI standard defines the header data length as exactly 6.
    // The previous code computed headerLen from buffer offsets which always
    // yielded 0. We now write the constant 6 directly, per the MIDI spec.
    data.append('\x00'); data.append('\x00');
    data.append('\x00'); data.append('\x06');  // length = 6 (fixed per MIDI spec)

    data.append('\x00'); data.append('\x01');  // format 1 (multi-track)

    int numTracks = staves.size();
    data.append(static_cast<char>((numTracks >> 8) & 0xFF));
    data.append(static_cast<char>( numTracks       & 0xFF));

    int ppq = (m_ticksPerQuarter > 0) ? m_ticksPerQuarter : 480;
    data.append(static_cast<char>((ppq >> 8) & 0xFF));
    data.append(static_cast<char>( ppq       & 0xFF));

    // ---- Track chunks ----
    // FIX #3: use index-based loop to avoid smart-pointer comparison UB
    for (int si = 0; si < staves.size(); ++si) {
        const auto& staff = staves[si];

        data.append("MTrk", 4);

        // Reserve 4 bytes for track length (filled in below)
        int trackLenPos = data.size();
        data.append('\x00'); data.append('\x00');
        data.append('\x00'); data.append('\x00');
        int trackStart = data.size();

        int lastTick = 0;

        // Emit tempo meta event only on the first track
        if (si == 0) {
            int microsec = (m_tempo > 0)
                           ? static_cast<int>(60000000.0 / m_tempo)
                           : 500000;  // default 120 BPM

            data.append('\x00');                          // delta = 0
            data.append('\xFF'); data.append('\x51'); data.append('\x03');
            data.append(static_cast<char>((microsec >> 16) & 0xFF));
            data.append(static_cast<char>((microsec >>  8) & 0xFF));
            data.append(static_cast<char>( microsec        & 0xFF));
        }

        // Collect and sort note-on / note-off events
        struct MidiEvent {
            int    tick;
            bool   isNoteOn;
            quint8 note;
            quint8 velocity;
            quint8 channel;
        };
        QVector<MidiEvent> events;

        int midiChannel = static_cast<quint8>(staff->midiChannel() & 0x0F);

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
                  [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

        for (const auto& ev : events) {
            int delta = ev.tick - lastTick;
            lastTick  = ev.tick;
            writeVLQ(data, delta);

            quint8 status = static_cast<quint8>((ev.isNoteOn ? 0x90 : 0x80) | ev.channel);
            data.append(static_cast<char>(status));
            data.append(static_cast<char>(ev.note));
            data.append(static_cast<char>(ev.velocity));
        }

        // End of track meta event
        data.append('\x00');
        data.append('\xFF'); data.append('\x2F'); data.append('\x00');

        // Back-patch track length
        int trackLen = data.size() - trackStart;
        data[trackLenPos]     = static_cast<char>((trackLen >> 24) & 0xFF);
        data[trackLenPos + 1] = static_cast<char>((trackLen >> 16) & 0xFF);
        data[trackLenPos + 2] = static_cast<char>((trackLen >>  8) & 0xFF);
        data[trackLenPos + 3] = static_cast<char>( trackLen        & 0xFF);
    }

    file.write(data);
    return true;
}

// =============================================================================
// Score::renderToPCM
// =============================================================================

QByteArray Score::renderToPCM(int sampleRate) {
    int ppq         = (m_ticksPerQuarter > 0) ? m_ticksPerQuarter : 480;
    double tempoVal = (m_tempo > 0) ? m_tempo : 120.0;
    int total       = totalTicks();

    if (total == 0 || staves.isEmpty()) return {};

    int totalSamples = static_cast<int>(
        (static_cast<double>(total) / ppq) * (60.0 / tempoVal) * sampleRate);

    if (totalSamples <= 0) return {};

    QByteArray pcm;
    pcm.resize(totalSamples * 2 * static_cast<int>(sizeof(float)));  // stereo float32
    float* buffer = reinterpret_cast<float*>(pcm.data());
    std::fill(buffer, buffer + totalSamples * 2, 0.0f);

    // FIX #5: guard staves access
    if (staves.isEmpty()) return pcm;

    QVector<SynthVoice> voices;
    voices.resize(16);

    double dt          = 1.0 / sampleRate;
    double currentTime = 0.0;

    const auto* staff = staves[0].get();

    for (int i = 0; i < totalSamples; ++i) {
        double sample     = 0.0;
        int    currentTick= static_cast<int>((currentTime * tempoVal / 60.0) * ppq);

        // Trigger new notes
        for (const auto& measure : staff->measures) {
            for (const auto& note : measure->notes) {
                if (note.isRest) continue;
                int noteTick = measure->startTick() + note.tickPosition;
                if (noteTick == currentTick) {
                    // Find a free voice
                    for (auto& v : voices) {
                        if (!v.active) {
                            double freq = 440.0 *
                                std::pow(2.0, (note.pitch.midiNote - 69) / 12.0);
                            v.frequency = freq;
                            v.phase     = 0.0;
                            v.amplitude = note.velocity / 127.0f;
                            int durTicks= static_cast<int>(
                                note.duration.toQuarterNotes() * ppq);
                            v.remainingSamples = static_cast<int>(
                                (durTicks / static_cast<double>(ppq)) *
                                (60.0 / tempoVal) * sampleRate);
                            v.active = true;
                            break;
                        }
                    }
                }
            }
        }

        // Synthesize active voices
        for (auto& v : voices) {
            if (!v.active) continue;
            sample += v.amplitude * std::sin(2.0 * M_PI * v.frequency * v.phase);
            v.phase += dt;
            if (--v.remainingSamples <= 0) v.active = false;
        }

        // Soft clip and write stereo
        float s = static_cast<float>(std::tanh(sample * 0.5));
        buffer[i * 2]     = s;
        buffer[i * 2 + 1] = s;

        currentTime += dt;
    }

    return pcm;
}

} // namespace Aegis
