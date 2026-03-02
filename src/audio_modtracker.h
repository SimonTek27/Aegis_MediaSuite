// modtracker.h - Fully Integrated MOD Tracker for Aegis DAW
// Pillar 1 (Audio) + Pillar 2 (Effects) + DAW Engine Integration

#pragma once

#include "audio_daw.h"
#include <QObject>
#include <QVector>
#include <QString>
#include <QByteArray>
#include <QMap>
#include <memory>
#include <array>

// Forward declarations for libopenmpt
struct openmpt_module;

namespace Aegis {

    // =============================================================================
    // Tracker Types
    // =============================================================================

    enum class TrackerFormat {
        MOD,    // ProTracker
        XM,     // FastTracker II
        IT,     // Impulse Tracker
        S3M,    // Scream Tracker 3
        Unknown
    };

    struct TrackerNote {
        static constexpr int NO_NOTE = 0;
        static constexpr int NOTE_CUT = 254;
        static constexpr int NOTE_OFF = 255;

        uint8_t note = 0;        // 0=none, 1-128=notes, 254=cut, 255=off
        uint8_t instrument = 0;  // 0-128
        uint8_t volume = 0;      // 0-64 (0=none)
        uint8_t effect = 0;      // 0-36 (ProTracker effects)
        uint8_t param = 0;       // Effect parameter

        bool isEmpty() const {
            return note == 0 && instrument == 0 && volume == 0 && effect == 0 && param == 0;
        }
        bool isNoteOn() const { return note > 0 && note < 128; }
    };

    struct TrackerPattern {
        int numRows = 64;
        int numChannels = 4;
        QVector<QVector<TrackerNote>> data; // [row][channel]

        TrackerPattern(int rows = 64, int chans = 4)
        : numRows(rows), numChannels(chans) {
            data.resize(rows);
            for (auto& row : data) row.resize(chans);
        }

        TrackerNote* noteAt(int row, int channel) {
            if (row < 0 || row >= numRows) return nullptr;
            if (channel < 0 || channel >= numChannels) return nullptr;
            return &data[row][channel];
        }
    };

    struct TrackerSample {
        QString name;
        QByteArray data;
        int sampleRate = 8363;
        int loopStart = 0;
        int loopEnd = 0;
        bool loop = false;
        int volume = 64;
        int finetune = 0;
        double panning = 0.5; // 0.0=left, 1.0=right
    };

    struct TrackerInstrument {
        QString name;
        int numSamples = 0;
        QVector<int> sampleMap; // Note to sample mapping
        int defaultVolume = 64;
        int defaultPan = 128;
    };

    struct TrackerSequenceEntry {
        int pattern = 0;
        int duration = 64; // In rows
    };

    struct TrackerModule {
        QString title;
        TrackerFormat format = TrackerFormat::MOD;
        int numChannels = 4;
        int numPatterns = 0;
        int numInstruments = 0;
        int numSamples = 0;
        int defaultTempo = 125; // BPM
        int defaultSpeed = 6;   // Ticks per row

        QVector<TrackerPattern> patterns;
        QVector<TrackerSample> samples;
        QVector<TrackerInstrument> instruments;
        QVector<TrackerSequenceEntry> sequence;
        QMap<int, int> patternToSequenceIndex; // For quick lookup
    };

    // =============================================================================
    // ModTrackerClip - First-class DAW Clip
    // =============================================================================

    class ModTrackerClip : public Clip {
        Q_OBJECT
    public:
        explicit ModTrackerClip(QObject* parent = nullptr);
        ~ModTrackerClip();

        // Clip interface implementation
        void processAudio(double position, int frames, float* buffer,
                          int channels, int sampleRate, const TempoMap& tempo) override;
                          QVector<NoteEvent> getMidiEvents(double start, double end) override;

                          // Tracker-specific
                          TrackerModule* module() { return &m_module; }
                          const TrackerModule* module() const { return &m_module; }

                          // Load/save
                          bool load(const QString& path);
                          bool save(const QString& path);
                          bool importFromMemory(const QByteArray& data, const QString& format);

                          // Format support
                          static QStringList supportedFormats();
                          static TrackerFormat detectFormat(const QString& path);

                          // Pattern access
                          TrackerPattern* pattern(int index);
                          int currentPattern() const { return m_currentPattern; }
                          int currentRow() const { return m_currentRow; }

                          // Playback control (synced with DAW transport)
                          void setFollowTransport(bool follow) { m_followTransport = follow; }
                          bool followsTransport() const { return m_followTransport; }

                          void setPatternLoop(int startPattern, int endPattern);
                          void clearPatternLoop();
                          bool hasPatternLoop() const { return m_loopStartPattern >= 0; }

                          // Per-channel configuration
                          void setChannelVolume(int channel, double volume);
                          double channelVolume(int channel) const;
                          void setChannelPan(int channel, double pan);
                          double channelPan(int channel) const;
                          void setChannelMuted(int channel, bool muted);
                          bool isChannelMuted(int channel) const;

                          // Effects per channel (Pillar 2)
                          void setChannelEffectChain(int channel, std::shared_ptr<EffectChain> chain);
                          std::shared_ptr<EffectChain> channelEffectChain(int channel) const;

                          // Conversion to other clip types
                          MidiClip* toMidiClip(QObject* parent = nullptr) const;
                          NotationClip* toNotationClip(int patternIndex = -1, QObject* parent = nullptr) const;

                          // Render to audio (freeze)
                          AudioClip* freeze(Track* targetTrack, double startTime, double duration);

                          // Editor state
                          void setEditPattern(int pattern) { m_editPattern = pattern; }
                          int editPattern() const { return m_editPattern; }
                          void setEditChannel(int channel) { m_editChannel = channel; }
                          int editChannel() const { return m_editChannel; }

                          // Pattern editing
                          void setNote(int pattern, int row, int channel, const TrackerNote& note);
                          void clearNote(int pattern, int row, int channel);
                          void insertRow(int pattern, int row);
                          void deleteRow(int pattern, int row);
                          void clonePattern(int sourcePattern, int destPattern);
                          void resizePattern(int pattern, int newRowCount);

                          // Sequence editing
                          void insertSequenceEntry(int position, int pattern);
                          void removeSequenceEntry(int position);
                          void setSequenceEntry(int position, int pattern);

                          // Timing calculations
                          double rowToTime(int pattern, int row) const;
                          void timeToRow(double time, int& pattern, int& row) const;
                          double patternDuration(int pattern) const;
                          double moduleDuration() const;

                          // Tempo handling
                          void setOverrideTempo(bool override, double bpm = 125.0);
                          bool tempoOverridden() const { return m_overrideTempo; }
                          double overrideBpm() const { return m_overrideBpm; }

                          // Internal synthesis fallback
                          double calculateRowDuration() const;

    signals:
        void patternChanged(int pattern);
        void rowChanged(int row);
        void moduleChanged();
        void channelConfigChanged(int channel);
        void playbackPositionChanged(int pattern, int row);

    private:
        TrackerModule m_module;

        // Playback state
        int m_currentPattern = 0;
        int m_currentRow = 0;
        bool m_followTransport = true;

        // Looping
        int m_loopStartPattern = -1;
        int m_loopEndPattern = -1;
        int m_loopStartRow = 0;
        int m_loopEndRow = 63;

        // Editor state
        int m_editPattern = 0;
        int m_editChannel = 0;

        // Per-channel config
        struct ChannelConfig {
            double volume = 1.0;
            double pan = 0.0; // -1 to 1
            bool muted = false;
            std::shared_ptr<EffectChain> effects;
        };
        std::array<ChannelConfig, 32> m_channelConfigs; // Max 32 channels

        // Tempo override
        bool m_overrideTempo = false;
        double m_overrideBpm = 125.0;

        // libopenmpt handle
        openmpt_module* m_openmptModule = nullptr;
        QByteArray m_moduleData; // Keep data alive for libopenmpt

        // Internal audio buffer for processing
        mutable QVector<float> m_mixBuffer;
        mutable QVector<std::array<QVector<float>, 2>> m_channelBuffers; // Per-channel stereo

        // Synthesis fallback when libopenmpt not available
        struct Voice {
            bool active = false;
            int sampleIndex = -1;
            double position = 0.0;
            double increment = 1.0;
            double volume = 1.0;
            double pan = 0.5;
            int channel = 0;
        };
        QVector<Voice> m_voices;

        void initOpenMPT();
        void closeOpenMPT();
        void renderOpenMPT(int frames, float* buffer, int channels);
        void renderInternal(int frames, float* buffer, int channels, int sampleRate);
        void updatePlaybackPosition(double clipTime);

        // Pattern to MIDI conversion helper
        QVector<NoteEvent> patternToMidiEvents(int patternIndex, double startTime) const;

        // Format loaders
        bool loadMOD(const QByteArray& data);
        bool loadXM(const QByteArray& data);
        bool loadIT(const QByteArray& data);
        bool loadS3M(const QByteArray& data);
        bool saveMOD(const QString& path);
        bool saveXM(const QString& path);
    };

    // =============================================================================
    // ModTrackerEditor - UI Controller (separate from playback)
    // =============================================================================

    class ModTrackerEditor : public QObject {
        Q_OBJECT
    public:
        explicit ModTrackerEditor(ModTrackerClip* clip, QObject* parent = nullptr);

        // Navigation
        void setCursor(int pattern, int row, int channel);
        void moveCursor(int deltaRow, int deltaChannel);
        void nextPattern();
        void prevPattern();

        // Editing
        void enterNote(int note, int instrument = 0);
        void enterEffect(int effect, int param);
        void enterVolume(int volume);
        void deleteAtCursor();
        void cutNote();
        void paste(const TrackerNote& note);

        // Playback (for preview, not transport)
        void playNote(int note, int instrument);
        void stopNote(int channel);
        void playRow();
        void playPattern();

        // Selection
        void setSelection(int startRow, int endRow, int startChan, int endChan);
        void clearSelection();
        bool hasSelection() const;
        void copySelection();
        void pasteSelection();
        void deleteSelection();
        void transposeSelection(int semitones);

        // Getters
        int cursorPattern() const { return m_cursorPattern; }
        int cursorRow() const { return m_cursorRow; }
        int cursorChannel() const { return m_cursorChannel; }
        ModTrackerClip* clip() const { return m_clip; }

    signals:
        void cursorMoved(int pattern, int row, int channel);
        void selectionChanged();
        void dataChanged(int pattern, int row, int channel);

    private:
        ModTrackerClip* m_clip;
        int m_cursorPattern = 0;
        int m_cursorRow = 0;
        int m_cursorChannel = 0;

        struct Selection {
            int startRow = 0, endRow = 0;
            int startChan = 0, endChan = 0;
            bool active = false;
        } m_selection;

        QVector<QVector<TrackerNote>> m_clipboard;
    };

    // =============================================================================
    // ModTrackerPlayback - Internal playback engine (used by ModTrackerClip)
    // =============================================================================

    class ModTrackerPlayback : public QObject {
        Q_OBJECT
    public:
        explicit ModTrackerPlayback(QObject* parent = nullptr);
        ~ModTrackerPlayback();

        void setModule(TrackerModule* module);
        void setSampleRate(int rate) { m_sampleRate = rate; }

        // Position control
        void setPosition(int pattern, int row);
        void getPosition(int& pattern, int& row) const;

        // Rendering
        void render(int frames, float* buffer, int channels);

        // State
        bool isPlaying() const { return m_playing; }
        void setPlaying(bool play) { m_playing = play; }

        // Per-channel access for mixing
        float* channelBuffer(int channel) { return m_channelBuffers[channel][0].data(); }

    signals:
        void positionChanged(int pattern, int row);
        void patternFinished(int pattern);

    private:
        TrackerModule* m_module = nullptr;
        int m_sampleRate = 48000;
        int m_currentPattern = 0;
        int m_currentRow = 0;
        double m_rowProgress = 0.0; // Fraction of current row
        bool m_playing = false;

        // Timing
        double m_samplesPerRow = 0.0;
        int m_currentSpeed = 6;
        int m_currentTempo = 125;
        int m_tickCounter = 0;

        // Channel state
        struct ChannelState {
            int note = 0;
            int instrument = 0;
            int volume = 64;
            int pan = 128;
            double period = 0.0;
            double samplePos = 0.0;
            double sampleInc = 0.0;
            int effect = 0;
            int param = 0;
            int effectMemory = 0;
            // Effect state
            int portaSpeed = 0;
            int vibratoSpeed = 0;
            int vibratoDepth = 0;
            int tremoloSpeed = 0;
            int tremoloDepth = 0;
            int volumeSlide = 0;
            int arpeggioNotes[3] = {0, 0, 0};
            int arpeggioIndex = 0;
        };
        std::array<ChannelState, 32> m_channels;

        // Buffers
        std::array<std::array<QVector<float>, 2>, 32> m_channelBuffers;

        void processRow();
        void processTick();
        void processChannel(int ch, const TrackerNote& note);
        void mixChannels(int frames, float* buffer, int channels);
        void applyEffect(int ch, ChannelState& state);
        double periodToIncrement(double period, int sampleRate) const;
        void updateSamplesPerRow();
        static double noteToPeriod(int note, int finetune = 0);
    };

    // =============================================================================
    // DAWEngine Extensions (add these methods to DAWEngine class in audio_daw.h)
    // =============================================================================

    /*
     c lass DAWEngine {                        *
public:
    ModTrackerClip* createTrackerClip(int trackIndex = -1,
    const QString& name = "Tracker",
    int channels = 4);
    ModTrackerClip* importTrackerModule(const QString& path,
    int trackIndex = -1);
    bool renderTrackerToAudio(ModTrackerClip* clip,
    const QString& outputPath,
    const AudioRenderSettings& settings = {});
    Track* createTrackerTrack(const QString& name = "Tracker Track");
};
*/

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::TrackerNote)
Q_DECLARE_METATYPE(Aegis::TrackerPattern)
