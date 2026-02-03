// modtracker.h - MOD Tracker using audio pillar
#pragma once

#include "audio.h"
#include <QObject>
#include <QVector>
#include <QString>
#include <QByteArray>
#include <memory>

namespace Aegis {

    // ... (Note, Pattern, Sample, Instrument, Module structs unchanged) ...

    /**
     * @brief Tracker editor - uses AudioEngine's ModTrackerPlayback
     *
     * Architecture:
     * - Pillar 1 (audio): Playback via ModTrackerPlayback, analysis via AudioEngine
     * - Pillar 2 (audio_effects): Effects chain for tracker playback
     * - Pillar 3 (mpv_backend): Not used (tracker files decoded by libopenmpt)
     */
    class ModTracker : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
        Q_PROPERTY(int currentPattern READ currentPattern WRITE setCurrentPattern NOTIFY currentPatternChanged)
        Q_PROPERTY(int currentRow READ currentRow NOTIFY currentRowChanged)
        Q_PROPERTY(int currentChannel READ currentChannel WRITE setCurrentChannel NOTIFY currentChannelChanged)
        Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
        Q_PROPERTY(int numChannels READ numChannels NOTIFY moduleChanged)
        Q_PROPERTY(QString title READ title NOTIFY moduleChanged)

    public:
        /**
         * @brief Construct with AudioEngine dependency
         * @param engine AudioEngine containing ModTrackerPlayback (Pillar 1)
         * @param parent QObject parent
         */
        explicit ModTracker(AudioEngine* engine, QObject *parent = nullptr);
        ~ModTracker();

        // File operations
        Q_INVOKABLE bool load(const QString &path);
        Q_INVOKABLE bool save(const QString &path);
        Q_INVOKABLE bool saveAs(const QString &path, const QString &format);
        Q_INVOKABLE void newModule(int channels = 4, int patternLength = 64);

        // Playback control - delegates to ModTrackerPlayback (Pillar 1)
        Q_INVOKABLE void play();
        Q_INVOKABLE void playFrom(int pattern, int row);
        Q_INVOKABLE void stop();
        Q_INVOKABLE void setPlayhead(int pattern, int row);

        // Pattern editing
        Q_INVOKABLE void setNote(int pattern, int row, int channel, const Note &note);
        Q_INVOKABLE void clearNote(int pattern, int row, int channel);
        Q_INVOKABLE void setInstrument(int pattern, int row, int channel, int instrument);
        Q_INVOKABLE void setEffect(int pattern, int row, int channel, int effect, int value);
        Q_INVOKABLE void insertRow(int pattern, int row);
        Q_INVOKABLE void deleteRow(int pattern, int row);

        // Song structure
        Q_INVOKABLE void insertPatternInSequence(int position, int pattern);
        Q_INVOKABLE void setPatternInSequence(int position, int pattern);
        Q_INVOKABLE int getPatternInSequence(int position) const;

        // Effects - Pillar 2
        Q_INVOKABLE void setEffectChain(std::shared_ptr<EffectChain> chain);
        Q_INVOKABLE std::shared_ptr<EffectChain> effectChain() const;

        // Getters
        bool modified() const { return m_modified; }
        int currentPattern() const { return m_currentPattern; }
        int currentRow() const { return m_currentRow; }
        int currentChannel() const { return m_currentChannel; }
        bool playing() const;
        int numChannels() const { return m_module ? m_module->numChannels : 4; }
        QString title() const { return m_module ? m_module->title : QString(); }

        Module* getModule() { return m_module.get(); }
        ModTrackerPlayback* playback() const { return m_playback; }

        static QString noteName(int period);

    signals:
        void modifiedChanged();
        void currentPatternChanged();
        void currentRowChanged();
        void currentChannelChanged();
        void playingChanged();
        void moduleChanged();
        void patternDataChanged(int pattern);
        void playbackPositionChanged(int pattern, int row);
        void error(const QString &message);

    private:
        void markModified();
        void syncPlaybackState();

        // Pillar 1 dependency
        AudioEngine* m_engine;  // Borrowed
        ModTrackerPlayback* m_playback;  // From engine

        // Editor data
        std::unique_ptr<Module> m_module;
        bool m_modified = false;

        // Editor state
        int m_currentPattern = 0;
        int m_currentRow = 0;
        int m_currentChannel = 0;

        // Pillar 2: Optional effects
        std::shared_ptr<EffectChain> m_effects;
    };

} // namespace Aegis
