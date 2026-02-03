// audio_daw.h - Professional DAW functionality with plugin hosting
// Part of Aegis Multimedia Suite
// Uses all three pillars: audio, audio_effects, mpv_backend

#pragma once

#include "audio.h"
#include "audio_effects.h"
#include "audio_output.h"
#include "mpv_backend.h"
#include <QObject>
#include <QVector>
#include <QHash>
#include <QReadWriteLock>
#include <QUuid>
#include <QFutureWatcher>
#include <memory>
#include <functional>
#include <atomic>

// Forward declarations for plugin hosting
struct LilvPlugin;
struct LilvInstance;
typedef struct LV2_Descriptor LV2_Descriptor;

namespace Steinberg {
    namespace Vst {
        class IComponent;
        class IEditController;
        class IAudioProcessor;
    }
}

namespace Aegis {

    // =============================================================================
    // DAW Data Structures
    // =============================================================================

    enum class TrackType {
        Audio,      // Audio track with clips
        MIDI,       // MIDI/instrument track
        Aux,        // Auxiliary bus
        Master      // Master output bus
    };

    enum class AutomationType {
        Volume,     // dB
        Pan,        // -1.0 to 1.0
        Mute,       // boolean
        Solo,       // boolean
        PluginParam, // Generic plugin parameter
        SendLevel   // Send level to aux
    };

    struct AutomationPoint {
        double time;        // seconds
        double value;       // normalized 0.0-1.0 or actual value
        double curve;       // -1.0 to 1.0 (curve shape)
    };

    struct AutomationLane {
        AutomationType type;
        int pluginIndex;    // For PluginParam
        int paramIndex;     // For PluginParam
        QVector<AutomationPoint> points;
        bool enabled = true;
    };

    struct AudioClip {
        QString id;
        QString name;
        QString sourcePath;     // Path to audio file or reference to buffer
        double startTime;       // Position on timeline (seconds)
        double duration;        // Clip duration (seconds)
        double offset;          // Offset into source (seconds)
        double fadeIn;          // Fade in duration
        double fadeOut;         // Fade out duration
        float gain;             // Clip gain (dB)
        bool muted = false;

        // Cached waveform data
        QVector<float> waveformCache;
        int waveformResolution = 100; // samples per pixel
    };

    struct MIDINote {
        int pitch;          // MIDI note number (0-127)
        double startTime;   // seconds
        double duration;    // seconds
        float velocity;     // 0.0-1.0
        int channel = 0;
    };

    struct MIDIClip {
        QString id;
        QString name;
        double startTime;
        double duration;
        QVector<MIDINote> notes;
        bool loop = false;
    };

    struct PluginInstance {
        QString id;
        QString name;
        QString path;           // Plugin file path
        QString format;         // "VST3", "LV2", "CLAP", "LADSPA"

        // Plugin state
        bool enabled = true;
        bool bypassed = false;
        float dryWet = 1.0f;    // 0.0 = dry, 1.0 = wet

        // Parameters
        QHash<int, float> parameters; // paramId -> value

        // Native handles (opaque)
        void* nativeHandle = nullptr;
        void* nativeUI = nullptr;

        // Audio processing function
        std::function<void(float*, float*, int, int)> processFunc;
    };

    struct Track {
        QString id;
        QString name;
        TrackType type;
        int channelCount = 2;

        // Routing
        QString inputBus;       // "hardware", "bus:name", "track:id"
        QString outputBus;      // "master", "bus:name", "track:id"
        QVector<QString> sends; // Send destinations

        // Clips
        QVector<AudioClip> audioClips;
        QVector<MIDIClip> midiClips;

        // Processing chain - Pillar 2 integration
        std::shared_ptr<EffectChain> effectChain;
        QVector<PluginInstance> plugins;

        // Automation
        QVector<AutomationLane> automation;

        // State
        float volume = 0.0f;    // dB
        float pan = 0.0f;       // -1.0 to 1.0
        bool muted = false;
        bool soloed = false;
        bool armed = false;     // Record arm
        bool monitoring = false; // Input monitoring

        // Metering
        float peakLevel = 0.0f;
        float rmsLevel = 0.0f;
    };

    struct TimelineSelection {
        double startTime = 0.0;
        double endTime = 0.0;
        QVector<QString> trackIds;
        bool isRange = true;
    };

    // =============================================================================
    // Transport Control
    // =============================================================================

    class DAWTransport : public QObject {
        Q_OBJECT
        Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
        Q_PROPERTY(double tempo READ tempo WRITE setTempo NOTIFY tempoChanged)
        Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
        Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
        Q_PROPERTY(double loopStart READ loopStart WRITE setLoopStart NOTIFY loopChanged)
        Q_PROPERTY(double loopEnd READ loopEnd WRITE setLoopEnd NOTIFY loopChanged)
        Q_PROPERTY(bool loopEnabled READ loopEnabled WRITE setLoopEnabled NOTIFY loopChanged)

    public:
        explicit DAWTransport(QObject* parent = nullptr);

        // Control
        void play();
        void stop();
        void pause();
        void record();
        void togglePlay();
        void toggleRecord();
        void nudge(int beats);  // Move playhead by beats

        // Position
        double position() const { return m_position.load(); }
        void setPosition(double seconds);

        // Tempo
        double tempo() const { return m_tempo.load(); }
        void setTempo(double bpm);

        // Time signature
        int numerator() const { return m_numerator; }
        int denominator() const { return m_denominator; }
        void setTimeSignature(int num, int denom);

        // Loop
        double loopStart() const { return m_loopStart; }
        double loopEnd() const { return m_loopEnd; }
        bool loopEnabled() const { return m_loopEnabled; }
        void setLoopStart(double seconds);
        void setLoopEnd(double seconds);
        void setLoopEnabled(bool enabled);

        // State
        bool playing() const { return m_playing.load(); }
        bool recording() const { return m_recording.load(); }

        // Utility
        double beatsToSeconds(double beats) const;
        double secondsToBeats(double seconds) const;
        QString timeToString(double seconds) const; // "bars:beats:ticks"
        double stringToTime(const QString& str) const;

    signals:
        void positionChanged(double position);
        void tempoChanged(double tempo);
        void playingChanged(bool playing);
        void recordingChanged(bool recording);
        void loopChanged();
        void timeSignatureChanged();

    private:
        std::atomic<double> m_position{0.0};
        std::atomic<double> m_tempo{120.0};
        std::atomic<bool> m_playing{false};
        std::atomic<bool> m_recording{false};
        std::atomic<bool> m_paused{false};

        int m_numerator = 4;
        int m_denominator = 4;
        double m_loopStart = 0.0;
        double m_loopEnd = 0.0;
        bool m_loopEnabled = false;

        QTimer m_positionTimer;
    };

    // =============================================================================
    // Plugin Host - VST3/LV2/CLAP support
    // =============================================================================

    class PluginHost : public QObject {
        Q_OBJECT
    public:
        explicit PluginHost(QObject* parent = nullptr);
        ~PluginHost();

        // Scanning
        void scanPluginDirectories();
        QVector<QString> availablePlugins() const;
        QHash<QString, QString> pluginInfo(const QString& pluginId) const;

        // Loading
        std::shared_ptr<PluginInstance> loadPlugin(const QString& path,
                                                   const QString& format,
                                                   int sampleRate,
                                                   int bufferSize);
        bool unloadPlugin(std::shared_ptr<PluginInstance> plugin);

        // Processing
        void processPlugin(std::shared_ptr<PluginInstance> plugin,
                           const float* input, float* output,
                           int frames, int channels);

        // UI
        void* createEditor(std::shared_ptr<PluginInstance> plugin, void* parentWindow);
        void destroyEditor(std::shared_ptr<PluginInstance> plugin);
        void updateEditor(std::shared_ptr<PluginInstance> plugin);

    signals:
        void pluginScanned(const QString& pluginId, const QString& name);
        void scanProgress(int percent);
        void scanFinished();

    private:
        void scanLV2Plugins();
        void scanVST3Plugins();
        void scanCLAPPlugins();

        // LV2
        struct LilvWorld* m_lilvWorld = nullptr;

        // VST3
        struct VST3Module {
            void* module;
            QString path;
        };
        QVector<VST3Module> m_vst3Modules;

        QHash<QString, QHash<QString, QString>> m_pluginDatabase;
    };

    // =============================================================================
    // Main DAW Engine
    // =============================================================================

    class DAWEngine : public QObject {
        Q_OBJECT
        Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
        Q_PROPERTY(int trackCount READ trackCount NOTIFY tracksChanged)
        Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)

    public:
        /**
         * @brief Construct DAW with full pillar integration
         * @param output AudioOutput for playback (Pillar 1 extension)
         * @param parent QObject parent
         */
        explicit DAWEngine(std::unique_ptr<AudioOutput> output = nullptr,
                           QObject* parent = nullptr);
        ~DAWEngine();

        // ============== Project Management ==============
        Q_INVOKABLE void newProject(const QString& name);
        Q_INVOKABLE bool loadProject(const QString& path);
        Q_INVOKABLE bool saveProject(const QString& path = QString());
        Q_INVOKABLE void closeProject();
        QString projectPath() const;
        bool modified() const { return m_modified; }

        // ============== Track Management ==============
        Q_INVOKABLE QString addTrack(TrackType type, const QString& name = QString());
        Q_INVOKABLE void removeTrack(const QString& trackId);
        Q_INVOKABLE void renameTrack(const QString& trackId, const QString& name);
        Q_INVOKABLE void reorderTrack(const QString& trackId, int newIndex);
        Q_INVOKABLE QVector<QString> trackIds() const;
        Track* getTrack(const QString& trackId);

        // ============== Clip Operations ==============
        Q_INVOKABLE QString addAudioClip(const QString& trackId,
                                         const QString& audioPath,
                                         double position);
        Q_INVOKABLE QString addMIDIClip(const QString& trackId,
                                        double position,
                                        double duration);
        Q_INVOKABLE void removeClip(const QString& trackId, const QString& clipId);
        Q_INVOKABLE void moveClip(const QString& trackId,
                                  const QString& clipId,
                                  double newPosition);
        Q_INVOKABLE void resizeClip(const QString& trackId,
                                    const QString& clipId,
                                    double newDuration,
                                    bool stretch = false);
        Q_INVOKABLE void splitClip(const QString& trackId,
                                   const QString& clipId,
                                   double splitTime);

        // ============== Plugin Management ==============
        Q_INVOKABLE bool addPlugin(const QString& trackId,
                                   const QString& pluginPath,
                                   int index = -1);
        Q_INVOKABLE void removePlugin(const QString& trackId, int index);
        Q_INVOKABLE void movePlugin(const QString& trackId, int fromIndex, int toIndex);
        Q_INVOKABLE void setPluginEnabled(const QString& trackId, int index, bool enabled);
        Q_INVOKABLE void setPluginParameter(const QString& trackId,
                                            int pluginIndex,
                                            int paramIndex,
                                            float value);

        // ============== Automation ==============
        Q_INVOKABLE void addAutomationPoint(const QString& trackId,
                                            AutomationType type,
                                            double time,
                                            double value);
        Q_INVOKABLE void clearAutomation(const QString& trackId, AutomationType type);

        // ============== Audio I/O ==============
        Q_INVOKABLE void setTrackInput(const QString& trackId, const QString& input);
        Q_INVOKABLE void setTrackOutput(const QString& trackId, const QString& output);
        Q_INVOKABLE void addSend(const QString& trackId, const QString& destinationId);
        Q_INVOKABLE void removeSend(const QString& trackId, int sendIndex);

        // ============== Transport ==============
        DAWTransport* transport() const { return m_transport; }

        // ============== Rendering ==============
        Q_INVOKABLE bool exportProject(const QString& outputPath,
                                       const QString& format = "WAV",
                                       double start = 0.0,
                                       double end = -1.0);
        Q_INVOKABLE void startRendering();
        Q_INVOKABLE void stopRendering();
        bool isRendering() const;

        // ============== Analysis ==============
        Q_INVOKABLE QVector<float> analyzeTrack(const QString& trackId,
                                                double start,
                                                double end,
                                                const QString& analysisType);

        // ============== Pillar Access ==============
        AudioEngine* audioEngine() const { return m_engine.get(); }
        AudioOutput* audioOutput() const { return m_output.get(); }
        PluginHost* pluginHost() const { return m_pluginHost.get(); }
        EffectChain* masterChain() const { return m_masterChain.get(); }

    signals:
        void durationChanged();
        void tracksChanged();
        void modifiedChanged();
        void projectLoaded(const QString& path);
        void projectSaved(const QString& path);
        void clipAdded(const QString& trackId, const QString& clipId);
        void clipRemoved(const QString& trackId, const QString& clipId);
        void clipModified(const QString& trackId, const QString& clipId);
        void trackMeterUpdated(const QString& trackId, float peak, float rms);
        void error(const QString& message);

    private slots:
        void onTransportPositionChanged(double pos);
        void onTransportPlayingChanged(bool playing);
        void processAudio(float* outputBuffer, int frames);
        void updateTrackMeters();

    private:
        void processTrack(Track* track, double position, int frames, float* buffer);
        void processClip(const AudioClip& clip, double position, int frames, float* buffer);
        void applyAutomation(Track* track, double time);
        float interpolateAutomation(const AutomationLane& lane, double time);
        void setModified(bool modified);
        void buildProcessGraph();

        // Dependencies - Three Pillars
        std::unique_ptr<AudioEngine> m_engine;           // Pillar 1
        std::unique_ptr<AudioOutput> m_output;           // Audio output
        std::unique_ptr<PluginHost> m_pluginHost;        // Plugin hosting
        std::shared_ptr<EffectChain> m_masterChain;      // Master bus processing

        // Transport
        DAWTransport* m_transport;

        // Project data
        QHash<QString, std::shared_ptr<Track>> m_tracks;
        QVector<QString> m_trackOrder;
        QString m_projectPath;
        bool m_modified = false;
        double m_duration = 300.0;  // 5 minutes default

        // Processing state
        struct ProcessState {
            double currentTime = 0.0;
            int bufferSize = 1024;
            int sampleRate = 48000;
            bool rendering = false;
        } m_processState;

        // Threading
        QReadWriteLock m_trackLock;
        QTimer m_meterTimer;

        // Rendering
        QFutureWatcher<bool> m_renderWatcher;
    };

    // =============================================================================
    // Undo/Redo System
    // =============================================================================

    class DAWUndoManager : public QObject {
        Q_OBJECT
    public:
        explicit DAWUndoManager(DAWEngine* engine, QObject* parent = nullptr);

        void pushCommand(std::function<void()> undo, std::function<void()> redo,
                         const QString& description);

        Q_INVOKABLE void undo();
        Q_INVOKABLE void redo();
        Q_INVOKABLE bool canUndo() const;
        Q_INVOKABLE bool canRedo() const;
        Q_INVOKABLE QString undoDescription() const;
        Q_INVOKABLE QString redoDescription() const;
        Q_INVOKABLE void clear();

    signals:
        void undoStateChanged();

    private:
        struct Command {
            std::function<void()> undo;
            std::function<void()> redo;
            QString description;
        };

        DAWEngine* m_engine;
        QVector<Command> m_stack;
        int m_index = 0;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::TrackType)
Q_DECLARE_METATYPE(Aegis::AutomationType)
Q_DECLARE_METATYPE(Aegis::TimelineSelection)
