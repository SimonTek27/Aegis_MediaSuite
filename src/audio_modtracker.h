// audio_modtracker.h - ModTracker clip type for the DAW timeline
// Required by audio_daw.cpp (Track::trackerClips())
#pragma once

#include <QString>
#include <QVector>
#include <QObject>
#include <QReadWriteLock>
#include <QByteArray>
#include <memory>

#include "audio_daw.h"   // for Clip, ClipType, NoteEvent, TempoMap

namespace Aegis {

class EffectChain;
class MidiClip;
class NotationClip;
class Score;

#ifdef HAS_LIBOPENMPT
struct openmpt_module;
#endif

// Basic tracker note representation
struct TrackerNote {
    enum SpecialNotes { NOTE_OFF = 97, NOTE_CUT = 98 };

    int note{0};        // 0 = no note, 1-96 = note number, 97=OFF, 98=CUT
    int instrument{0};  // 0 = no instrument
    int volume{0};      // 0-64
    int effect{0};      // effect command
    int param{0};       // effect parameter

    bool isNoteOn() const { return note > 0 && note < NOTE_OFF; }
};

// Pattern data structure
struct TrackerPattern {
    int numRows{64};
    int numChannels{4};
    QVector<QVector<TrackerNote>> data; // [row][channel]

    TrackerPattern(int rows = 64, int channels = 4)
        : numRows(rows), numChannels(channels), data(rows, QVector<TrackerNote>(channels)) {}

    TrackerNote* noteAt(int row, int channel) {
        if (row < 0 || row >= numRows || channel < 0 || channel >= numChannels) return nullptr;
        return &data[row][channel];
    }
};

// Sequence entry
struct TrackerSequenceEntry {
    int pattern{0};
    int rows{64};
    int duration{64};
};

// Sample info (minimal subset used by audio_modtracker.cpp)
struct TrackerSample {
    QString name;
    QByteArray data;
    bool loop{false};
    int loopStart{0};
    int loopEnd{0};
    int finetune{0};
};

// Module
struct TrackerModule {
    QString title;
    int numChannels{4};
    int numPatterns{0};
    int defaultSpeed{6};
    int defaultTempo{125};
    QVector<TrackerPattern> patterns;
    QVector<TrackerSequenceEntry> sequence;
    QVector<TrackerSample> samples;
    enum class Format { MOD, XM, IT, S3M, Unknown } format{Format::Unknown};
};

// Per-channel config
struct TrackerChannelConfig {
    double volume{1.0};   // 0.0 - 2.0
    double pan{0.0};      // -1.0 - 1.0
    bool   muted{false};
    std::shared_ptr<EffectChain> effects;
};

// Playback state per channel for internal renderer
struct ChannelState {
    int note{0};
    int instrument{0};
    int volume{64};
    int effect{0};
    int param{0};
    double samplePos{0.0};
    double sampleInc{0.0};
    int effectMemory{0};
};

// ============================================================================
// ModTrackerClip - full tracker clip, derived from Clip
// ============================================================================

class ModTrackerClip : public Clip {
    Q_OBJECT
public:
    explicit ModTrackerClip(QObject* parent = nullptr);
    ~ModTrackerClip() override;

    void processAudio(double position, int frames, float* buffer,
                      int channels, int sampleRate, const TempoMap& tempo) override;

    QVector<NoteEvent> getMidiEvents(double start, double end) override;

    // Module loading/saving
    bool load(const QString& path);
    bool save(const QString& path);
    static QStringList supportedFormats();

    // Pattern / sequence helpers
    TrackerPattern* pattern(int index);

    void setPatternLoop(int startPattern, int endPattern);
    void clearPatternLoop();

    void setChannelVolume(int channel, double volume);
    double channelVolume(int channel) const;

    void setChannelPan(int channel, double pan);
    double channelPan(int channel) const;

    void setChannelMuted(int channel, bool muted);
    bool isChannelMuted(int channel) const;

    void setChannelEffectChain(int channel, std::shared_ptr<EffectChain> chain);
    std::shared_ptr<EffectChain> channelEffectChain(int channel) const;

    MidiClip* toMidiClip(QObject* parent) const;
    NotationClip* toNotationClip(int patternIndex, QObject* parent) const;

    void setNote(int pattern, int row, int channel, const TrackerNote& note);
    void clearNote(int pattern, int row, int channel);
    void insertRow(int pattern, int row);
    void deleteRow(int pattern, int row);
    void clonePattern(int sourcePattern, int destPattern);
    void resizePattern(int pattern, int newRowCount);

    void insertSequenceEntry(int position, int pattern);
    void removeSequenceEntry(int position);
    void setSequenceEntry(int position, int pattern);

    double rowToTime(int pattern, int row) const;
    void timeToRow(double time, int& pattern, int& row) const;

    double patternDuration(int pattern) const;
    double moduleDuration() const;

    void setOverrideTempo(bool override, double bpm);

    // Access to underlying module (for editors/helpers)
    TrackerModule* module() { return &m_module; }
    const TrackerModule* module() const { return &m_module; }

signals:
    void moduleChanged();
    void patternChanged(int pattern);
    void channelConfigChanged(int channel);
    void playbackPositionChanged(int pattern, int row);

private:
    enum class TrackerFormat { MOD, XM, IT, S3M, Unknown };

    TrackerFormat detectFormat(const QString& path);

    bool loadMOD(const QByteArray& data);
    bool loadXM(const QByteArray& data);
    bool loadIT(const QByteArray& data);
    bool loadS3M(const QByteArray& data);

    bool saveMOD(const QString& path);
    bool saveXM(const QString& path);

    void initOpenMPT();
    void closeOpenMPT();
    void renderOpenMPT(int frames, float* buffer, int channels);
    void renderInternal(int frames, float* buffer, int channels, int sampleRate);

    void updatePlaybackPosition(double clipTime);
    QVector<NoteEvent> patternToMidiEvents(int patternIndex, double startTime) const;

    double calculateRowDuration() const;

    // Internal data
    mutable QReadWriteLock m_lock;

    TrackerModule m_module;
    QVector<TrackerChannelConfig> m_channelConfigs{32};

    QByteArray m_moduleData;

    bool   m_followTransport{false};
    bool   m_overrideTempo{false};
    double m_overrideBpm{0.0};

    // Loop state
    int m_loopStartPattern{-1};
    int m_loopEndPattern{-1};
    int m_loopStartRow{0};
    int m_loopEndRow{0};

    // Playback cursor for UI sync
    int m_currentPattern{0};
    int m_currentRow{0};

    QVector<float> m_mixBuffer;
    QVector<QVector<QVector<float>>> m_channelBuffers; // [channel][LR][frame]

#ifdef HAS_LIBOPENMPT
    std::unique_ptr<openmpt_module, void(*)(openmpt_module*)> m_openmptModule{nullptr, nullptr};
#endif
};

// ============================================================================
// ModTrackerEditor - simple pattern editor for UI interactions
// ============================================================================

class ModTrackerEditor : public QObject {
    Q_OBJECT
public:
    explicit ModTrackerEditor(ModTrackerClip* clip, QObject* parent = nullptr);

    void setCursor(int pattern, int row, int channel);
    void moveCursor(int deltaRow, int deltaChannel);
    void nextPattern();
    void prevPattern();

    void enterNote(int note, int instrument);
    void enterEffect(int effect, int param);
    void enterVolume(int volume);
    void deleteAtCursor();

    void setSelection(int startRow, int endRow, int startChan, int endChan);
    void clearSelection();
    bool hasSelection() const;

    void copySelection();
    void pasteSelection();
    void transposeSelection(int semitones);

signals:
    void cursorMoved(int pattern, int row, int channel);
    void dataChanged(int pattern, int row, int channel);
    void selectionChanged();

private:
    struct Selection {
        int startRow{0};
        int endRow{0};
        int startChan{0};
        int endChan{0};
        bool active{false};
    };

    ModTrackerClip* m_clip{nullptr};
    int m_cursorPattern{0};
    int m_cursorRow{0};
    int m_cursorChannel{0};

    Selection m_selection;
    QVector<QVector<TrackerNote>> m_clipboard;
};

// ============================================================================
// ModTrackerClipPlayback - internal synthesis helper
// ============================================================================

class ModTrackerClipPlayback : public QObject {
    Q_OBJECT
public:
    explicit ModTrackerClipPlayback(QObject* parent = nullptr);
    ~ModTrackerClipPlayback() override;

    void setModule(TrackerModule* module);
    void setPosition(int pattern, int row);
    void getPosition(int& pattern, int& row) const;

    void setSampleRate(int sr) { m_sampleRate = sr; }
    void setPlaying(bool p) { m_playing = p; }

    void render(int frames, float* buffer, int channels);

signals:
    void patternFinished(int pattern);
    void positionChanged(int pattern, int row);

private:
    void processRow();
    void processChannel(int ch, const TrackerNote& note);
    void mixChannels(int frames, float* buffer, int channels);
    void applyEffect(int ch, ChannelState& state);
    double periodToIncrement(double period, int sampleRate) const;
    void updateSamplesPerRow();
    double noteToPeriod(int note, int finetune);

    TrackerModule* m_module{nullptr};
    QVector<ChannelState> m_channels{32};
    QVector<QVector<QVector<float>>> m_channelBuffers; // [channel][LR][frame]

    int m_sampleRate{44100};
    bool m_playing{false};

    int m_currentPattern{0};
    int m_currentRow{0};
    double m_rowProgress{0.0};
    double m_samplesPerRow{0.0};
    int m_currentSpeed{6};
    int m_currentTempo{125};
    int m_tickCounter{0};
};

} // namespace Aegis
