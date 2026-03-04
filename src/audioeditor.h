// audioeditor.h - Audio Editor
// Professional digital audio editing workstation

#pragma once

#include "audio.h"
#include "audio_effects.h"
#include <QObject>
#include <QVector>
#include <QString>
#include <QUrl>
#include <QColor>
#include <QThread>
#include <QMutex>
#include <QReadWriteLock>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <memory>
#include <complex>
#include <functional>
#include <samplerate.h>

// Forward declarations
namespace Aegis {
    class MpvBackend;  // Pillar 3 for preview
    struct AudioFormat;
    class EditAction;
}

namespace Aegis {

    // ... (AudioFormat, AudioBuffer structs unchanged) ...

    // ============================================================================
    // Waveform Display Data
    // ============================================================================

    struct WaveformLevel {
        float min = 0.0f;
        float max = 0.0f;
        float rms = 0.0f;
    };

    class WaveformCache : public QObject {
        Q_OBJECT
    public:
        explicit WaveformCache(QObject* parent = nullptr);
        void build(const EnhancedAudioBuffer& buffer, int pixelsPerSecond = 100);
        void clear();
        QVector<WaveformLevel> getLevels(qint64 startFrame, qint64 endFrame, int width) const;
        bool isValid() const { return !m_levels.isEmpty(); }
        qint64 totalFrames() const { return m_totalFrames; }

    signals:
        void cacheUpdated();

    private:
        QVector<WaveformLevel> m_levels;
        qint64 m_totalFrames = 0;
        int m_cacheResolution = 100;
        mutable QReadWriteLock m_lock;
    };

    // ============================================================================
    // Main Audio Editor Class - Uses all three pillars
    // ============================================================================

    class AudioEditor : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
        Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
        Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
        Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
        Q_PROPERTY(Aegis::AudioFormat format READ format NOTIFY formatChanged)
        Q_PROPERTY(qint64 totalFrames READ totalFrames NOTIFY bufferChanged)
        Q_PROPERTY(double duration READ duration NOTIFY bufferChanged)
        Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
        Q_PROPERTY(Selection selection READ selection WRITE setSelection NOTIFY selectionChanged)
        Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStateChanged)

    public:
        /**
         * @brief Construct editor with explicit pillar dependencies
         * @param engine AudioEngine for analysis and processing (Pillar 1)
         * @param parent QObject parent
         */
        explicit AudioEditor(AudioEngine* engine, QObject* parent = nullptr);
        ~AudioEditor();

        // ============== File Operations ==============
        Q_INVOKABLE bool open(const QString& filePath);
        Q_INVOKABLE bool save();
        Q_INVOKABLE bool saveAs(const QString& filePath);
        Q_INVOKABLE bool exportFile(const QString& filePath, const QString& format);
        Q_INVOKABLE void close();

        // ============== Edit Operations ==============
        Q_INVOKABLE void cut();
        Q_INVOKABLE void copy();
        Q_INVOKABLE void paste(qint64 position = -1);
        Q_INVOKABLE void deleteSelection();
        Q_INVOKABLE void insertSilence(qint64 frames, qint64 position = -1);
        Q_INVOKABLE void trim();  // Keep only selection
        Q_INVOKABLE void trimOutside();

        // ============== Selection ==============
        Selection selection() const { return m_selection; }
        void setSelection(const Selection& sel);
        Q_INVOKABLE void selectAll();
        Q_INVOKABLE void selectNone();
        Q_INVOKABLE void selectRange(qint64 start, qint64 end);
        Q_INVOKABLE void extendSelection(qint64 toPosition);

        // ============== Navigation ==============
        double position() const { return m_position; }
        void setPosition(double seconds);
        Q_INVOKABLE void gotoStart();
        Q_INVOKABLE void gotoEnd();
        Q_INVOKABLE void gotoSelectionStart();
        Q_INVOKABLE void gotoSelectionEnd();

        // ============== Effects - Pillar 2 ==============
        Q_INVOKABLE bool applyEffect(std::shared_ptr<AudioEffect> effect);
        Q_INVOKABLE bool amplify(float gainDb);
        Q_INVOKABLE bool normalize(float targetDb = -1.0f);
        Q_INVOKABLE bool fadeIn();
        Q_INVOKABLE bool fadeOut();
        Q_INVOKABLE bool reverse();
        Q_INVOKABLE bool invert();
        Q_INVOKABLE bool silence();
        Q_INVOKABLE bool filter(FilterEffect::FilterType type, float freq, float q = 0.707f);

        // ============== Analysis - Pillar 1 ==============
        Q_INVOKABLE float getPeakLevel(const Selection& sel) const;
        Q_INVOKABLE float getRMSLevel(const Selection& sel) const;
        Q_INVOKABLE QVector<float> getSpectrum(const Selection& sel, int bins = 512);
        Q_INVOKABLE QVector<float> getWaveformData(int channel, qint64 start, qint64 end, int samples);

        // ============== Preview Playback - Pillar 3 ==============
        Q_INVOKABLE void play();
        Q_INVOKABLE void playSelection();
        Q_INVOKABLE void pause();
        Q_INVOKABLE void stop();
        bool isPlaying() const;

        // ============== Properties ==============
        bool modified() const { return m_modified; }
        QString filePath() const { return m_filePath; }
        AudioFormat format() const { return m_format; }
        qint64 totalFrames() const { return m_buffer.frames(); }
        double duration() const;
        WaveformCache* waveformCache() { return m_waveformCache; }

        // ============== Pillar Access ==============
        AudioEngine* audioEngine() const { return m_engine; }
        EffectChain* effectChain() const { return m_effects.get(); }
        MpvBackend* previewBackend() const { return m_previewBackend.get(); }

    signals:
        void modifiedChanged();
        void undoStateChanged();
        void filePathChanged();
        void formatChanged();
        void bufferChanged();
        void positionChanged();
        void selectionChanged();
        void playbackStateChanged();
        void effectProgress(int percent);
        void effectFinished(bool success);
        void error(const QString& message);
        void statusMessage(const QString& message);

    private:
        void markModified();
        bool writeToFile(const QString& path, const QString& format);

        // Dependencies
        AudioEngine* m_engine;  // Pillar 1 (borrowed)
        std::unique_ptr<EffectChain> m_effects;  // Pillar 2 (owned)
        std::unique_ptr<MpvBackend> m_previewBackend;  // Pillar 3 (owned for preview)

        // Data
        EnhancedAudioBuffer m_buffer;
        Aegis::AudioFormat m_format;
        QString m_filePath;
        bool m_modified = false;
        Selection m_selection;
        double m_position = 0.0;
        bool m_playing = false;

        // Cache
        WaveformCache* m_waveformCache;

        // Undo/Redo
        QVector<std::unique_ptr<Aegis::EditAction>> m_undoStack;
        int m_undoIndex = 0;
    };

    // ... (BatchProcessor unchanged) ...

} // namespace Aegis

