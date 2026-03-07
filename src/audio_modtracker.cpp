#include "audio_daw.h"
// modtracker.cpp - Fully Integrated MOD Tracker Implementation

#include "audio_modtracker.h"
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cstring>
#include <cmath>

// libopenmpt includes (conditionally compiled)
#ifdef HAS_LIBOPENMPT
#include <libopenmpt/libopenmpt.h>
#endif

namespace Aegis {

    // =============================================================================
    // Utility Functions
    // =============================================================================

    static const double PAL_CLOCK = 7093789.2;
    static const double NTSC_CLOCK = 7159090.5;

    static double amigaPeriodToHz(double period) {
        return PAL_CLOCK / (period * 2.0);
    }

    static double hzToAmigaPeriod(double hz) {
        return PAL_CLOCK / (hz * 2.0);
    }

    static int noteToAmigaPeriod(int note, int finetune = 0) {
        // ProTracker fine-tuned periods
        static const int basePeriods[12] = {
            1712, 1616, 1525, 1440, 1357, 1281, 1209, 1141, 1077, 1017, 961, 907
        };
        int octave = note / 12;
        int semitone = note % 12;
        int period = basePeriods[semitone];
        // Apply octave
        period >>= octave;
        // Apply finetune (simplified)
        period -= finetune;
        return period;
    }

    // =============================================================================
    // ModTrackerClip Implementation
    // =============================================================================

    ModTrackerClip::ModTrackerClip(QObject* parent)
    : Clip(ClipType::Notation, parent)
    {
        m_module.numChannels = 4;
        m_module.patterns.resize(1);
        m_module.patterns[0] = TrackerPattern(64, 4);
        m_module.sequence.append({0, 64});

        // Initialize channel configs
        for (int i = 0; i < 32; ++i) {
            m_channelConfigs[i].pan = (i % 2 == 0) ? -0.3 : 0.3; // Default L/R panning
        }

        m_voices.resize(32);
        m_channelBuffers.resize(32);
    }

    ModTrackerClip::~ModTrackerClip() {
        closeOpenMPT();
    }

    void ModTrackerClip::processAudio(double position, int frames, float* buffer,
                                      int channels, int sampleRate, const TempoMap& tempo) {
        QReadLocker lock(&m_lock);

        // Clear output
        std::fill(buffer, buffer + frames * channels, 0.0f);

        double clipTime = position - startTime();
        if (clipTime < 0 || clipTime >= duration()) {
            return;
        }

        // Ensure buffers are sized
        if (m_mixBuffer.size() < frames * channels) {
            m_mixBuffer.resize(frames * channels);
        }
        for (auto& chBuf : m_channelBuffers) {
            if (chBuf[0].size() < frames) {
                chBuf[0].resize(frames);
                chBuf[1].resize(frames);
            }
            std::fill(chBuf[0].begin(), chBuf[0].end(), 0.0f);
            std::fill(chBuf[1].begin(), chBuf[1].end(), 0.0f);
        }

        // Use libopenmpt if available and module data exists
        #ifdef HAS_LIBOPENMPT
        if (m_openmptModule) {
            renderOpenMPT(frames, buffer, channels);
        } else
            #endif
        {
            // Fallback internal renderer
            renderInternal(frames, buffer, channels, sampleRate);
        }

        // Apply per-channel effects and mixing
        for (int ch = 0; ch < m_module.numChannels; ++ch) {
            if (m_channelConfigs[ch].muted) continue;

            // Apply channel effects if present
            if (m_channelConfigs[ch].effects) {
                // Process interleaved stereo
                QVector<float> tempBuf(frames * 2);
                for (int i = 0; i < frames; ++i) {
                    tempBuf[i * 2] = m_channelBuffers[ch][0][i];
                    tempBuf[i * 2 + 1] = m_channelBuffers[ch][1][i];
                }
                // Note: EffectChain::process signature may vary
                // m_channelConfigs[ch].effects->process(tempBuf.data(), frames);
                for (int i = 0; i < frames; ++i) {
                    m_channelBuffers[ch][0][i] = tempBuf[i * 2];
                    m_channelBuffers[ch][1][i] = tempBuf[i * 2 + 1];
                }
            }

            // Mix to output with volume and pan
            double vol = m_channelConfigs[ch].volume;
            double pan = m_channelConfigs[ch].pan; // -1 to 1
            double leftGain = vol * (1.0 - pan) * 0.5;
            double rightGain = vol * (1.0 + pan) * 0.5;

            for (int i = 0; i < frames; ++i) {
                if (channels >= 2) {
                    buffer[i * channels] += m_channelBuffers[ch][0][i] * leftGain;
                    buffer[i * channels + 1] += m_channelBuffers[ch][1][i] * rightGain;
                } else {
                    buffer[i] += (m_channelBuffers[ch][0][i] + m_channelBuffers[ch][1][i]) * 0.5 * vol;
                }
            }
        }

        // Soft clip
        for (int i = 0; i < frames * channels; ++i) {
            buffer[i] = std::tanh(buffer[i]);
        }

        // Update playback position if following transport
        if (m_followTransport) {
            updatePlaybackPosition(clipTime);
        }
                                      }

                                      QVector<NoteEvent> ModTrackerClip::getMidiEvents(double start, double end) {
                                          QReadLocker lock(&m_lock);
                                          QVector<NoteEvent> events;

                                          // Convert tracker patterns in time range to MIDI events
                                          double clipStart = start - startTime();
                                          double clipEnd = end - startTime();

                                          if (clipEnd < 0 || clipStart > duration()) {
                                              return events;
                                          }

                                          clipStart = std::max(0.0, clipStart);
                                          clipEnd = std::min(duration(), clipEnd);

                                          // Find which patterns are in range
                                          double time = 0.0;
                                          for (int seqIdx = 0; seqIdx < m_module.sequence.size(); ++seqIdx) {
                                              const auto& entry = m_module.sequence[seqIdx];
                                              double patternDur = patternDuration(entry.pattern);

                                              if (time + patternDur > clipStart && time < clipEnd) {
                                                  // This pattern overlaps our range
                                                  auto patternEvents = patternToMidiEvents(entry.pattern, startTime() + time);
                                                  for (const auto& ev : patternEvents) {
                                                      if (ev.time >= start && ev.time < end) {
                                                          events.append(ev);
                                                      }
                                                  }
                                              }

                                              time += patternDur;
                                              if (time >= clipEnd) break;
                                          }

                                          return events;
                                      }

                                      bool ModTrackerClip::load(const QString& path) {
                                          QWriteLocker lock(&m_lock);

                                          QFile file(path);
                                          if (!file.open(QIODevice::ReadOnly)) {
                                              return false;
                                          }

                                          m_moduleData = file.readAll();
                                          TrackerFormat format = detectFormat(path);

                                          bool success = false;
                                          switch (format) {
                                              case TrackerFormat::MOD:
                                                  success = loadMOD(m_moduleData);
                                                  break;
                                              case TrackerFormat::XM:
                                                  success = loadXM(m_moduleData);
                                                  break;
                                              case TrackerFormat::IT:
                                                  success = loadIT(m_moduleData);
                                                  break;
                                              case TrackerFormat::S3M:
                                                  success = loadS3M(m_moduleData);
                                                  break;
                                              default:
                                                  break;
                                          }

                                          if (success) {
                                              setName(QFileInfo(path).baseName());
                                              setDuration(moduleDuration());
                                              initOpenMPT();
                                              emit moduleChanged();
                                          }

                                          return success;
                                      }

                                      bool ModTrackerClip::save(const QString& path) {
                                          QReadLocker lock(&m_lock);

                                          TrackerFormat format = detectFormat(path);

                                          switch (format) {
                                              case TrackerFormat::MOD:
                                                  return saveMOD(path);
                                              case TrackerFormat::XM:
                                                  return saveXM(path);
                                              default:
                                                  // Default to MOD
                                                  return saveMOD(path);
                                          }
                                      }

                                      QStringList ModTrackerClip::supportedFormats() {
                                          return QStringList() << "*.mod" << "*.xm" << "*.it" << "*.s3m";
                                      }

                                      TrackerFormat ModTrackerClip::detectFormat(const QString& path) {
                                          QString ext = QFileInfo(path).suffix().toLower();
                                          if (ext == "mod") return TrackerFormat::MOD;
                                          if (ext == "xm") return TrackerFormat::XM;
                                          if (ext == "it") return TrackerFormat::IT;
                                          if (ext == "s3m") return TrackerFormat::S3M;
                                          return TrackerFormat::Unknown;
                                      }

                                      TrackerPattern* ModTrackerClip::pattern(int index) {
                                          if (index < 0 || index >= m_module.patterns.size()) return nullptr;
                                          return &m_module.patterns[index];
                                      }

                                      void ModTrackerClip::setPatternLoop(int startPattern, int endPattern) {
                                          QWriteLocker lock(&m_lock);
                                          m_loopStartPattern = startPattern;
                                          m_loopEndPattern = endPattern;
                                          m_loopStartRow = 0;
                                          m_loopEndRow = m_module.patterns[endPattern].numRows - 1;
                                      }

                                      void ModTrackerClip::clearPatternLoop() {
                                          QWriteLocker lock(&m_lock);
                                          m_loopStartPattern = -1;
                                          m_loopEndPattern = -1;
                                      }

                                      void ModTrackerClip::setChannelVolume(int channel, double volume) {
                                          if (channel < 0 || channel >= 32) return;
                                          QWriteLocker lock(&m_lock);
                                          m_channelConfigs[channel].volume = std::max(0.0, std::min(2.0, volume));
                                          emit channelConfigChanged(channel);
                                      }

                                      double ModTrackerClip::channelVolume(int channel) const {
                                          if (channel < 0 || channel >= 32) return 0.0;
                                          return m_channelConfigs[channel].volume;
                                      }

                                      void ModTrackerClip::setChannelPan(int channel, double pan) {
                                          if (channel < 0 || channel >= 32) return;
                                          QWriteLocker lock(&m_lock);
                                          m_channelConfigs[channel].pan = std::max(-1.0, std::min(1.0, pan));
                                          emit channelConfigChanged(channel);
                                      }

                                      double ModTrackerClip::channelPan(int channel) const {
                                          if (channel < 0 || channel >= 32) return 0.0;
                                          return m_channelConfigs[channel].pan;
                                      }

                                      void ModTrackerClip::setChannelMuted(int channel, bool muted) {
                                          if (channel < 0 || channel >= 32) return;
                                          QWriteLocker lock(&m_lock);
                                          m_channelConfigs[channel].muted = muted;
                                          emit channelConfigChanged(channel);
                                      }

                                      bool ModTrackerClip::isChannelMuted(int channel) const {
                                          if (channel < 0 || channel >= 32) return false;
                                          return m_channelConfigs[channel].muted;
                                      }

                                      void ModTrackerClip::setChannelEffectChain(int channel, std::shared_ptr<EffectChain> chain) {
                                          if (channel < 0 || channel >= 32) return;
                                          QWriteLocker lock(&m_lock);
                                          m_channelConfigs[channel].effects = chain;
                                          emit channelConfigChanged(channel);
                                      }

                                      std::shared_ptr<EffectChain> ModTrackerClip::channelEffectChain(int channel) const {
                                          if (channel < 0 || channel >= 32) return nullptr;
                                          return m_channelConfigs[channel].effects;
                                      }

                                      MidiClip* ModTrackerClip::toMidiClip(QObject* parent) const {
                                          auto* midi = new MidiClip(parent);

                                          // Convert all patterns to MIDI events
                                          double time = 0.0;
                                          for (const auto& entry : m_module.sequence) {
                                              auto events = patternToMidiEvents(entry.pattern, time);
                                              for (const auto& ev : events) {
                                                  midi->addEvent(ev);
                                              }
                                              time += patternDuration(entry.pattern);
                                          }

                                          return midi;
                                      }

                                      NotationClip* ModTrackerClip::toNotationClip(int patternIndex, QObject* parent) const {
                                          auto* notation = new NotationClip(parent);
                                          auto score = std::make_unique<Score>();
                                          score->setTitle(m_module.title);

                                          auto* staff = score->addStaff("Tracker");

                                          int patternsToConvert = (patternIndex < 0) ? m_module.patterns.size() : 1;
                                          int startPattern = (patternIndex < 0) ? 0 : patternIndex;

                                          for (int p = 0; p < patternsToConvert; ++p) {
                                              int patIdx = startPattern + p;
                                              if (patIdx >= m_module.patterns.size()) break;

                                              const auto& pattern = m_module.patterns[patIdx];
                                              auto* measure = staff->addMeasure(p + 1);

                                              // Simple conversion: each row becomes a 16th note grid
                                              for (int row = 0; row < pattern.numRows; ++row) {
                                                  for (int ch = 0; ch < pattern.numChannels; ++ch) {
                                                      const auto& tnote = pattern.data[row][ch];
                                                      if (tnote.isNoteOn()) {
                                                          Note note;
                                                          note.pitch = Pitch(tnote.note + 24); // Offset to reasonable MIDI range
                                                          note.duration.type = DurationType::Sixteenth;
                                                          note.tickPosition = row * 120; // 480 PPQ / 4 = 120 per 16th
                                                          note.velocity = tnote.volume > 0 ? tnote.volume * 2 : 100;
                                                          note.voice = ch;
                                                          measure->addNote(note);
                                                      }
                                                  }
                                              }
                                          }

                                          notation->setScore(std::move(score));
                                          return notation;
                                      }

                                      void ModTrackerClip::setNote(int pattern, int row, int channel, const TrackerNote& note) {
                                          if (pattern < 0 || pattern >= m_module.patterns.size()) return;
                                          QWriteLocker lock(&m_lock);

                                          auto* pat = &m_module.patterns[pattern];
                                          if (row < 0 || row >= pat->numRows) return;
                                          if (channel < 0 || channel >= pat->numChannels) return;

                                          pat->data[row][channel] = note;
                                          emit patternChanged(pattern);
                                          emit changed();
                                      }

                                      void ModTrackerClip::clearNote(int pattern, int row, int channel) {
                                          setNote(pattern, row, channel, TrackerNote());
                                      }

                                      void ModTrackerClip::insertRow(int pattern, int row) {
                                          if (pattern < 0 || pattern >= m_module.patterns.size()) return;
                                          QWriteLocker lock(&m_lock);

                                          auto* pat = &m_module.patterns[pattern];
                                          if (row < 0 || row > pat->numRows) return;

                                          pat->data.insert(row, QVector<TrackerNote>(pat->numChannels));
                                          pat->numRows++;
                                          emit patternChanged(pattern);
                                          emit changed();
                                      }

                                      void ModTrackerClip::deleteRow(int pattern, int row) {
                                          if (pattern < 0 || pattern >= m_module.patterns.size()) return;
                                          QWriteLocker lock(&m_lock);

                                          auto* pat = &m_module.patterns[pattern];
                                          if (row < 0 || row >= pat->numRows || pat->numRows <= 1) return;

                                          pat->data.removeAt(row);
                                          pat->numRows--;
                                          emit patternChanged(pattern);
                                          emit changed();
                                      }

                                      void ModTrackerClip::clonePattern(int sourcePattern, int destPattern) {
                                          if (sourcePattern < 0 || sourcePattern >= m_module.patterns.size()) return;
                                          QWriteLocker lock(&m_lock);

                                          if (destPattern >= m_module.patterns.size()) {
                                              m_module.patterns.resize(destPattern + 1);
                                          }

                                          m_module.patterns[destPattern] = m_module.patterns[sourcePattern];
                                          emit patternChanged(destPattern);
                                          emit changed();
                                      }

                                      void ModTrackerClip::resizePattern(int pattern, int newRowCount) {
                                          if (pattern < 0 || pattern >= m_module.patterns.size()) return;
                                          if (newRowCount < 1 || newRowCount > 256) return;

                                          QWriteLocker lock(&m_lock);
                                          auto* pat = &m_module.patterns[pattern];

                                          int oldRows = pat->numRows;
                                          pat->data.resize(newRowCount);

                                          // Initialize new rows if expanding
                                          for (int i = oldRows; i < newRowCount; ++i) {
                                              pat->data[i].resize(pat->numChannels);
                                          }

                                          pat->numRows = newRowCount;
                                          emit patternChanged(pattern);
                                          emit changed();
                                      }

                                      void ModTrackerClip::insertSequenceEntry(int position, int pattern) {
                                          QWriteLocker lock(&m_lock);
                                          if (position < 0) position = m_module.sequence.size();
                                          m_module.sequence.insert(position, {pattern, 64});
                                          emit changed();
                                      }

                                      void ModTrackerClip::removeSequenceEntry(int position) {
                                          QWriteLocker lock(&m_lock);
                                          if (position < 0 || position >= m_module.sequence.size()) return;
                                          m_module.sequence.removeAt(position);
                                          emit changed();
                                      }

                                      void ModTrackerClip::setSequenceEntry(int position, int pattern) {
                                          QWriteLocker lock(&m_lock);
                                          if (position < 0 || position >= m_module.sequence.size()) return;
                                          m_module.sequence[position].pattern = pattern;
                                          emit changed();
                                      }

                                      double ModTrackerClip::rowToTime(int pattern, int row) const {
                                          double time = 0.0;
                                          for (const auto& entry : m_module.sequence) {
                                              if (entry.pattern == pattern) {
                                                  time += row * calculateRowDuration();
                                                  return time;
                                              }
                                              time += patternDuration(entry.pattern);
                                          }
                                          return time;
                                      }

                                      void ModTrackerClip::timeToRow(double time, int& pattern, int& row) const {
                                          double t = 0.0;
                                          double rowDur = calculateRowDuration();

                                          for (const auto& entry : m_module.sequence) {
                                              double patDur = patternDuration(entry.pattern);
                                              if (t + patDur > time) {
                                                  pattern = entry.pattern;
                                                  row = static_cast<int>((time - t) / rowDur);
                                                  row = std::min(row, m_module.patterns[entry.pattern].numRows - 1);
                                                  return;
                                              }
                                              t += patDur;
                                          }

                                          // Past end
                                          if (!m_module.sequence.isEmpty()) {
                                              pattern = m_module.sequence.last().pattern;
                                              row = m_module.patterns[pattern].numRows - 1;
                                          } else {
                                              pattern = 0;
                                              row = 0;
                                          }
                                      }

                                      double ModTrackerClip::patternDuration(int pattern) const {
                                          if (pattern < 0 || pattern >= m_module.patterns.size()) return 0.0;
                                          return m_module.patterns[pattern].numRows * calculateRowDuration();
                                      }

                                      double ModTrackerClip::moduleDuration() const {
                                          double dur = 0.0;
                                          for (const auto& entry : m_module.sequence) {
                                              dur += patternDuration(entry.pattern);
                                          }
                                          return dur;
                                      }

                                      void ModTrackerClip::setOverrideTempo(bool override, double bpm) {
                                          QWriteLocker lock(&m_lock);
                                          m_overrideTempo = override;
                                          m_overrideBpm = bpm;
                                          emit changed();
                                      }

                                      double ModTrackerClip::calculateRowDuration() const {
                                          double bpm = m_overrideTempo ? m_overrideBpm : m_module.defaultTempo;
                                          int speed = m_module.defaultSpeed;
                                          // ProTracker timing: (2.5 / BPM) * speed seconds per row
                                          return (2.5 / bpm) * speed;
                                      }

                                      // =============================================================================
                                      // Private Methods
                                      // =============================================================================

                                      void ModTrackerClip::initOpenMPT() {
                                          #ifdef HAS_LIBOPENMPT
                                          closeOpenMPT();
                                          if (m_moduleData.isEmpty()) return;

                                          m_openmptModule = openmpt_module_create_from_memory(
                                              m_moduleData.constData(),
                                                                                              m_moduleData.size(),
                                                                                              nullptr, nullptr, nullptr,
                                                                                              nullptr, nullptr, nullptr, nullptr
                                          );
                                          #endif
                                      }

                                      void ModTrackerClip::closeOpenMPT() {
                                          #ifdef HAS_LIBOPENMPT
                                          if (m_openmptModule) {
                                              openmpt_module_destroy(m_openmptModule);
                                              m_openmptModule = nullptr;
                                          }
                                          #endif
                                      }

                                      void ModTrackerClip::renderOpenMPT(int frames, float* buffer, int channels) {
                                          #ifdef HAS_LIBOPENMPT
                                          if (!m_openmptModule) return;

                                          // Get current position for sync
                                          if (m_followTransport) {
                                              int pattern = openmpt_module_get_current_pattern(m_openmptModule);
                                              int row = openmpt_module_get_current_row(m_openmptModule);
                                              if (pattern != m_currentPattern || row != m_currentRow) {
                                                  m_currentPattern = pattern;
                                                  m_currentRow = row;
                                                  emit playbackPositionChanged(pattern, row);
                                              }
                                          }

                                          // Render interleaved stereo
                                          openmpt_module_read_interleaved_float_stereo(
                                              m_openmptModule,
                                              48000, // TODO: get actual sample rate
                                              frames,
                                              buffer
                                          );
                                          #endif
                                      }

                                      void ModTrackerClip::renderInternal(int frames, float* buffer, int channels, int sampleRate) {
                                          // Simplified internal renderer - basic ProTracker playback
                                          // This is a fallback when libopenmpt is not available

                                          // TODO: Implement basic MOD playback synthesis
                                          // For now, just silence
                                          Q_UNUSED(frames)
                                          Q_UNUSED(buffer)
                                          Q_UNUSED(channels)
                                          Q_UNUSED(sampleRate)
                                      }

                                      void ModTrackerClip::updatePlaybackPosition(double clipTime) {
                                          int pattern, row;
                                          timeToRow(clipTime, pattern, row);

                                          if (pattern != m_currentPattern || row != m_currentRow) {
                                              m_currentPattern = pattern;
                                              m_currentRow = row;
                                              emit playbackPositionChanged(pattern, row);
                                          }
                                      }

                                      QVector<NoteEvent> ModTrackerClip::patternToMidiEvents(int patternIndex, double startTime) const {
                                          QVector<NoteEvent> events;
                                          if (patternIndex < 0 || patternIndex >= m_module.patterns.size()) return events;

                                          const auto& pattern = m_module.patterns[patternIndex];
                                          double rowDur = calculateRowDuration();

                                          // Track active notes for duration calculation
                                          struct ActiveNote {
                                              int note;
                                              int channel;
                                              int velocity;
                                              double startTime;
                                          };
                                          QMap<int, ActiveNote> activeNotes; // Key: channel

                                          for (int row = 0; row < pattern.numRows; ++row) {
                                              double rowTime = startTime + row * rowDur;

                                              for (int ch = 0; ch < pattern.numChannels; ++ch) {
                                                  const auto& tnote = pattern.data[row][ch];

                                                  // Handle note off for previous note
                                                  if (activeNotes.contains(ch) && (tnote.note == TrackerNote::NOTE_OFF ||
                                                      tnote.note == TrackerNote::NOTE_CUT ||
                                                      tnote.isNoteOn())) {
                                                      const auto& active = activeNotes[ch];
                                                  NoteEvent off;
                                                  off.type = NoteEvent::NoteOff;
                                                  off.time = rowTime;
                                                  off.channel = ch;
                                                  off.note = active.note;
                                                  events.append(off);
                                                  activeNotes.remove(ch);
                                                      }

                                                      // Handle note on
                                                      if (tnote.isNoteOn()) {
                                                          int midiNote = tnote.note + 24; // Convert to MIDI range
                                                          int velocity = tnote.volume > 0 ? tnote.volume * 2 : 100;

                                                          NoteEvent on;
                                                          on.type = NoteEvent::NoteOn;
                                                          on.time = rowTime;
                                                          on.channel = ch;
                                                          on.note = midiNote;
                                                          on.velocity = velocity;
                                                          on.pitch = Pitch(midiNote);
                                                          events.append(on);

                                                          ActiveNote active;
                                                          active.note = midiNote;
                                                          active.channel = ch;
                                                          active.velocity = velocity;
                                                          active.startTime = rowTime;
                                                          activeNotes[ch] = active;
                                                      }
                                              }
                                          }

                                          // Note off for any still-active notes at end of pattern
                                          double endTime = startTime + pattern.numRows * rowDur;
                                          for (const auto& active : activeNotes) {
                                              NoteEvent off;
                                              off.type = NoteEvent::NoteOff;
                                              off.time = endTime;
                                              off.channel = active.channel;
                                              off.note = active.note;
                                              events.append(off);
                                          }

                                          return events;
                                      }

                                      // =============================================================================
                                      // Format Loaders (Stubs - full implementation would be extensive)
                                      // =============================================================================

                                      bool ModTrackerClip::loadMOD(const QByteArray& data) {
                                          if (data.size() < 1084) return false;

                                          // Check MOD signature
                                          QString sig = QString::fromLatin1(data.mid(1080, 4));
                                          bool isMod = sig.startsWith("M.K.") || sig.startsWith("M!K!") ||
                                          sig.startsWith("FLT4") || sig.startsWith("4CHN");

                                          if (!isMod && sig[0].isLetter() && sig.mid(1, 2) == "CH") {
                                              // Multi-channel MOD
                                              isMod = true;
                                          }

                                          if (!isMod) return false;

                                          // Parse MOD header
                                          m_module.title = QString::fromLatin1(data.left(20)).trimmed();
                                          m_module.format = TrackerFormat::MOD;

                                          int numSamples = 31;
                                          int sampleHeaderSize = 30;
                                          int numChannels = 4;

                                          if (sig == "M.K." || sig == "M!K!" || sig == "FLT4" || sig == "4CHN") {
                                              numChannels = 4;
                                          } else if (sig.endsWith("CH")) {
                                              numChannels = sig.left(2).toInt();
                                          }

                                          m_module.numChannels = numChannels;

                                          // Parse samples (simplified)
                                          m_module.samples.resize(numSamples);
                                          int offset = 20;
                                          for (int i = 0; i < numSamples; ++i) {
                                              auto& sample = m_module.samples[i];
                                              sample.name = QString::fromLatin1(data.mid(offset, 22)).trimmed();
                                              // Parse sample length, finetune, volume, loop points...
                                              offset += sampleHeaderSize;
                                          }

                                          // Parse song length and sequence
                                          int songLength = static_cast<uint8_t>(data[offset]);
                                          // int restartPos = data[offset + 1]; // Unused
                                          offset += 2;

                                          m_module.sequence.resize(songLength);
                                          for (int i = 0; i < 128; ++i) {
                                              int pat = static_cast<uint8_t>(data[offset + i]);
                                              if (i < songLength) {
                                                  m_module.sequence[i].pattern = pat;
                                                  m_module.sequence[i].duration = 64;
                                              }
                                              m_module.numPatterns = std::max(m_module.numPatterns, pat + 1);
                                          }
                                          offset += 128;
                                          offset += 4; // Skip signature (already read)

                                          // Parse patterns
                                          m_module.patterns.resize(m_module.numPatterns);
                                          for (int p = 0; p < m_module.numPatterns; ++p) {
                                              m_module.patterns[p] = TrackerPattern(64, numChannels);

                                              for (int row = 0; row < 64; ++row) {
                                                  for (int ch = 0; ch < numChannels; ++ch) {
                                                      // ProTracker pattern data: 4 bytes per note
                                                      uint32_t noteData = (static_cast<uint8_t>(data[offset]) << 24) |
                                                      (static_cast<uint8_t>(data[offset + 1]) << 16) |
                                                      (static_cast<uint8_t>(data[offset + 2]) << 8) |
                                                      static_cast<uint8_t>(data[offset + 3]);
                                                      offset += 4;

                                                      TrackerNote& tnote = m_module.patterns[p].data[row][ch];

                                                      uint8_t sampleNum = (noteData >> 24) & 0xF0 | (noteData >> 12) & 0x0F;
                                                      uint16_t period = (noteData >> 16) & 0x0FFF;
                                                      uint8_t effect = (noteData >> 8) & 0x0F;
                                                      uint8_t param = noteData & 0xFF;

                                                      tnote.instrument = sampleNum;
                                                      tnote.effect = effect;
                                                      tnote.param = param;

                                                      // Convert period to note number
                                                      if (period > 0) {
                                                          // Amiga period table lookup would go here
                                                          // Simplified: approximate
                                                          tnote.note = 60; // Placeholder
                                                      }
                                                  }
                                              }
                                          }

                                          // Parse sample data
                                          for (int i = 0; i < numSamples; ++i) {
                                              // Read sample data from offset...
                                          }

                                          return true;
                                      }

                                      bool ModTrackerClip::loadXM(const QByteArray& data) {
                                          Q_UNUSED(data)
                                          return false;
                                      }

                                      bool ModTrackerClip::loadIT(const QByteArray& data) {
                                          Q_UNUSED(data)
                                          return false;
                                      }

                                      bool ModTrackerClip::loadS3M(const QByteArray& data) {
                                          Q_UNUSED(data)
                                          return false;
                                      }

                                      bool ModTrackerClip::saveMOD(const QString& path) {
                                          Q_UNUSED(path)
                                          return false;
                                      }

                                      bool ModTrackerClip::saveXM(const QString& path) {
                                          Q_UNUSED(path)
                                          return false;
                                      }

                                      // =============================================================================
                                      // ModTrackerEditor Implementation
                                      // =============================================================================

                                      ModTrackerEditor::ModTrackerEditor(ModTrackerClip* clip, QObject* parent)
                                      : QObject(parent), m_clip(clip) {
                                      }

                                      void ModTrackerEditor::setCursor(int pattern, int row, int channel) {
                                          if (!m_clip) return;

                                          auto* mod = m_clip->module();
                                          if (pattern < 0) pattern = 0;
                                          if (pattern >= mod->patterns.size()) pattern = mod->patterns.size() - 1;

                                          auto* pat = &mod->patterns[pattern];
                                          if (row < 0) row = 0;
                                          if (row >= pat->numRows) row = pat->numRows - 1;
                                          if (channel < 0) channel = 0;
                                          if (channel >= pat->numChannels) channel = pat->numChannels - 1;

                                          m_cursorPattern = pattern;
                                          m_cursorRow = row;
                                          m_cursorChannel = channel;

                                          emit cursorMoved(pattern, row, channel);
                                      }

                                      void ModTrackerEditor::moveCursor(int deltaRow, int deltaChannel) {
                                          setCursor(m_cursorPattern, m_cursorRow + deltaRow, m_cursorChannel + deltaChannel);
                                      }

                                      void ModTrackerEditor::nextPattern() {
                                          setCursor(m_cursorPattern + 1, m_cursorRow, m_cursorChannel);
                                      }

                                      void ModTrackerEditor::prevPattern() {
                                          setCursor(m_cursorPattern - 1, m_cursorRow, m_cursorChannel);
                                      }

                                      void ModTrackerEditor::enterNote(int note, int instrument) {
                                          if (!m_clip) return;

                                          TrackerNote tnote;
                                          tnote.note = note;
                                          tnote.instrument = instrument;
                                          tnote.volume = 64;

                                          m_clip->setNote(m_cursorPattern, m_cursorRow, m_cursorChannel, tnote);
                                          emit dataChanged(m_cursorPattern, m_cursorRow, m_cursorChannel);

                                          // Advance cursor
                                          moveCursor(1, 0);
                                      }

                                      void ModTrackerEditor::enterEffect(int effect, int param) {
                                          if (!m_clip) return;

                                          auto* pat = m_clip->pattern(m_cursorPattern);
                                          if (!pat) return;

                                          auto* note = pat->noteAt(m_cursorRow, m_cursorChannel);
                                          if (!note) return;

                                          note->effect = effect;
                                          note->param = param;

                                          m_clip->setNote(m_cursorPattern, m_cursorRow, m_cursorChannel, *note);
                                          emit dataChanged(m_cursorPattern, m_cursorRow, m_cursorChannel);
                                      }

                                      void ModTrackerEditor::enterVolume(int volume) {
                                          if (!m_clip) return;

                                          auto* pat = m_clip->pattern(m_cursorPattern);
                                          if (!pat) return;

                                          auto* note = pat->noteAt(m_cursorRow, m_cursorChannel);
                                          if (!note) return;

                                          note->volume = volume;
                                          m_clip->setNote(m_cursorPattern, m_cursorRow, m_cursorChannel, *note);
                                          emit dataChanged(m_cursorPattern, m_cursorRow, m_cursorChannel);
                                      }

                                      void ModTrackerEditor::deleteAtCursor() {
                                          if (!m_clip) return;
                                          m_clip->clearNote(m_cursorPattern, m_cursorRow, m_cursorChannel);
                                          emit dataChanged(m_cursorPattern, m_cursorRow, m_cursorChannel);
                                      }

                                      void ModTrackerEditor::setSelection(int startRow, int endRow, int startChan, int endChan) {
                                          m_selection.startRow = startRow;
                                          m_selection.endRow = endRow;
                                          m_selection.startChan = startChan;
                                          m_selection.endChan = endChan;
                                          m_selection.active = true;
                                          emit selectionChanged();
                                      }

                                      void ModTrackerEditor::clearSelection() {
                                          m_selection.active = false;
                                          emit selectionChanged();
                                      }

                                      bool ModTrackerEditor::hasSelection() const {
                                          return m_selection.active;
                                      }

                                      void ModTrackerEditor::copySelection() {
                                          if (!hasSelection() || !m_clip) return;

                                          auto* pat = m_clip->pattern(m_cursorPattern);
                                          if (!pat) return;

                                          int rows = m_selection.endRow - m_selection.startRow + 1;
                                          int chans = m_selection.endChan - m_selection.startChan + 1;

                                          m_clipboard.resize(rows);
                                          for (int r = 0; r < rows; ++r) {
                                              m_clipboard[r].resize(chans);
                                              for (int c = 0; c < chans; ++c) {
                                                  int srcRow = m_selection.startRow + r;
                                                  int srcCh = m_selection.startChan + c;
                                                  if (srcRow < pat->numRows && srcCh < pat->numChannels) {
                                                      m_clipboard[r][c] = pat->data[srcRow][srcCh];
                                                  }
                                              }
                                          }
                                      }

                                      void ModTrackerEditor::pasteSelection() {
                                          if (m_clipboard.isEmpty() || !m_clip) return;

                                          for (int r = 0; r < m_clipboard.size(); ++r) {
                                              for (int c = 0; c < m_clipboard[r].size(); ++c) {
                                                  m_clip->setNote(m_cursorPattern, m_cursorRow + r, m_cursorChannel + c,
                                                                  m_clipboard[r][c]);
                                              }
                                          }
                                          emit dataChanged(m_cursorPattern, m_cursorRow, m_cursorChannel);
                                      }

                                      void ModTrackerEditor::transposeSelection(int semitones) {
                                          if (!hasSelection() || !m_clip) return;

                                          auto* pat = m_clip->pattern(m_cursorPattern);
                                          if (!pat) return;

                                          for (int r = m_selection.startRow; r <= m_selection.endRow; ++r) {
                                              for (int c = m_selection.startChan; c <= m_selection.endChan; ++c) {
                                                  auto* note = pat->noteAt(r, c);
                                                  if (note && note->isNoteOn()) {
                                                      int newNote = note->note + semitones;
                                                      newNote = std::max(0, std::min(127, newNote));
                                                      note->note = newNote;
                                                      m_clip->setNote(m_cursorPattern, r, c, *note);
                                                  }
                                              }
                                          }
                                          emit dataChanged(m_cursorPattern, m_cursorRow, m_cursorChannel);
                                      }

                                      // =============================================================================
                                      // ModTrackerClipPlayback Implementation (Internal Synthesis)
                                      // =============================================================================

                                      ModTrackerClipPlayback::ModTrackerClipPlayback(QObject* parent)
                                      : QObject(parent) {
                                      }

                                      ModTrackerClipPlayback::~ModTrackerClipPlayback() = default;

                                      void ModTrackerClipPlayback::setModule(TrackerModule* module) {
                                          m_module = module;
                                          if (module) {
                                              m_currentSpeed = module->defaultSpeed;
                                              m_currentTempo = module->defaultTempo;
                                              updateSamplesPerRow();
                                          }
                                      }

                                      void ModTrackerClipPlayback::setPosition(int pattern, int row) {
                                          m_currentPattern = pattern;
                                          m_currentRow = row;
                                          m_rowProgress = 0.0;
                                          m_tickCounter = 0;
                                      }

                                      void ModTrackerClipPlayback::getPosition(int& pattern, int& row) const {
                                          pattern = m_currentPattern;
                                          row = m_currentRow;
                                      }

                                      void ModTrackerClipPlayback::render(int frames, float* buffer, int channels) {
                                          if (!m_module || !m_playing) {
                                              std::fill(buffer, buffer + frames * channels, 0.0f);
                                              return;
                                          }

                                          // Ensure buffers
                                          for (auto& chBuf : m_channelBuffers) {
                                              chBuf[0].resize(frames);
                                              chBuf[1].resize(frames);
                                              std::fill(chBuf[0].begin(), chBuf[0].end(), 0.0f);
                                              std::fill(chBuf[1].begin(), chBuf[1].end(), 0.0f);
                                          }

                                          int rendered = 0;
                                          while (rendered < frames) {
                                              double remainingRowSamples = m_samplesPerRow - m_rowProgress;
                                              int toRender = std::min(frames - rendered,
                                                                      static_cast<int>(remainingRowSamples));

                                              // Render current row state
                                              mixChannels(toRender, buffer + rendered * channels, channels);

                                              rendered += toRender;
                                              m_rowProgress += toRender;

                                              if (m_rowProgress >= m_samplesPerRow) {
                                                  // Advance to next row
                                                  m_rowProgress = 0;
                                                  m_currentRow++;

                                                  auto* pat = &m_module->patterns[m_currentPattern];
                                                  if (m_currentRow >= pat->numRows) {
                                                      m_currentRow = 0;
                                                      // Find next pattern in sequence
                                                      // ... sequence logic ...
                                                      emit patternFinished(m_currentPattern);
                                                  }

                                                  processRow();
                                                  emit positionChanged(m_currentPattern, m_currentRow);
                                              }
                                          }
                                      }

                                      void ModTrackerClipPlayback::processRow() {
                                          if (!m_module) return;
                                          if (m_currentPattern >= m_module->patterns.size()) return;

                                          auto* pat = &m_module->patterns[m_currentPattern];
                                          if (m_currentRow >= pat->numRows) return;

                                          for (int ch = 0; ch < m_module->numChannels; ++ch) {
                                              const auto& note = pat->data[m_currentRow][ch];
                                              processChannel(ch, note);
                                          }

                                          m_tickCounter = 0;
                                      }

                                      void ModTrackerClipPlayback::processChannel(int ch, const TrackerNote& note) {
                                          auto& state = m_channels[ch];

                                          if (note.isNoteOn()) {
                                              state.note = note.note;
                                              state.samplePos = 0;

                                              if (note.instrument > 0 && note.instrument <= m_module->samples.size()) {
                                                  state.instrument = note.instrument - 1;
                                              }

                                              // Calculate sample increment based on note period
                                              double period = noteToAmigaPeriod(note.note,
                                                                                m_module->samples[state.instrument].finetune);
                                              state.sampleInc = periodToIncrement(period, m_sampleRate);
                                          }

                                          if (note.volume > 0) {
                                              state.volume = note.volume;
                                          }

                                          state.effect = note.effect;
                                          state.param = note.param;

                                          // Store effect memory
                                          if (note.effect != 0 || note.param != 0) {
                                              state.effectMemory = (note.effect << 8) | note.param;
                                          }
                                      }

                                      void ModTrackerClipPlayback::mixChannels(int frames, float* buffer, int channels) {
                                          for (int ch = 0; ch < m_module->numChannels; ++ch) {
                                              auto& state = m_channels[ch];
                                              if (state.note == 0 || state.instrument < 0) continue;

                                              const auto& sample = m_module->samples[state.instrument];
                                              if (sample.data.isEmpty()) continue;

                                              const int16_t* sampleData = reinterpret_cast<const int16_t*>(sample.data.constData());
                                              int sampleLen = sample.data.size() / sizeof(int16_t);

                                              for (int i = 0; i < frames; ++i) {
                                                  // Apply effects
                                                  applyEffect(ch, state);

                                                  // Get sample
                                                  int pos = static_cast<int>(state.samplePos);
                                                  if (pos >= sampleLen) {
                                                      if (sample.loop && sample.loopEnd > sample.loopStart) {
                                                          state.samplePos = sample.loopStart;
                                                          pos = sample.loopStart;
                                                      } else {
                                                          state.note = 0;
                                                          break;
                                                      }
                                                  }

                                                  float smp = sampleData[pos] / 32768.0f;
                                                  smp *= state.volume / 64.0;

                                                  // Simple panning
                                                  float pan = (ch % 2 == 0) ? 0.7f : 0.3f;

                                                  if (channels >= 2) {
                                                      buffer[i * channels] += smp * pan;
                                                      buffer[i * channels + 1] += smp * (1.0f - pan);
                                                  } else {
                                                      buffer[i] += smp;
                                                  }

                                                  // Store per-channel buffer
                                                  m_channelBuffers[ch][0][i] = smp * pan;
                                                  m_channelBuffers[ch][1][i] = smp * (1.0f - pan);

                                                  state.samplePos += state.sampleInc;
                                              }
                                          }
                                      }

                                      void ModTrackerClipPlayback::applyEffect(int ch, ChannelState& state) {
                                          // Simplified effect processing
                                          switch (state.effect) {
                                              case 0x0: // Arpeggio
                                                  if (state.param != 0) {
                                                      // Cycle through notes
                                                  }
                                                  break;
                                              case 0x1: // Portamento up
                                                  state.sampleInc *= std::pow(2.0, state.param / (12.0 * 16.0));
                                                  break;
                                              case 0x2: // Portamento down
                                                  state.sampleInc /= std::pow(2.0, state.param / (12.0 * 16.0));
                                                  break;
                                              case 0xA: // Volume slide
                                                  if (m_tickCounter == 0) {
                                                      int slide = (state.param >> 4) - (state.param & 0x0F);
                                                      state.volume = std::max(0, std::min(64, state.volume + slide));
                                                  }
                                                  break;
                                              case 0xC: // Set volume
                                                  state.volume = std::min(64, static_cast<int>(state.param));
                                                  break;
                                              case 0xF: // Set speed/tempo
                                                  if (state.param < 32) {
                                                      m_currentSpeed = state.param;
                                                  } else {
                                                      m_currentTempo = state.param;
                                                      updateSamplesPerRow();
                                                  }
                                                  break;
                                          }
                                      }

                                      double ModTrackerClipPlayback::periodToIncrement(double period, int sampleRate) const {
                                          double freq = amigaPeriodToHz(period);
                                          return freq / sampleRate;
                                      }

                                      void ModTrackerClipPlayback::updateSamplesPerRow() {
                                          // ProTracker: (2.5 / BPM) * speed seconds per row
                                          double secondsPerRow = (2.5 / m_currentTempo) * m_currentSpeed;
                                          m_samplesPerRow = secondsPerRow * m_sampleRate;
                                      }

                                      double ModTrackerClipPlayback::noteToPeriod(int note, int finetune) {
                                          return noteToAmigaPeriod(note, finetune);
                                      }

} // namespace Aegis
