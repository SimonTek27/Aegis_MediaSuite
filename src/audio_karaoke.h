// karaoke.h - Karaoke controller
#pragma once

#include <QObject>
#include <QProcess>
#include <QDir>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QUuid>
#include <QColor>
#include <QImage>
#include <memory>

// Forward declarations - only pillars 1 & 2
namespace Aegis {
    class AudioEngine;           // Pillar 1
    class KaraokeProcessor;      // From audio.h
    class EffectChain;           // Pillar 2
    class MpvBackend;            // Pillar 3
}

namespace Aegis {

    struct KaraokeSong {
        QString id;
        QString filePath;
        QString audioPath;
        QString cdgPath;
        QString title;
        QString artist;
        QString language;
        int durationSeconds{0};
    };

    struct KaraokeSinger {
        QString id;
        QString name;
        QString displayName;
        int rotationIndex{0};
    };

    struct KaraokeQueueItem {
        QString id;
        QString songId;
        QString singerId;
        int keyChange{0};
        bool isCompleted{false};
        bool isPlaying{false};
    };

    /**
     * @brief Karaoke controller - uses AudioEngine for all audio processing
     *
     * Architecture:
     * - Pillar 1 (audio): DSP processing, karaoke effects, playback control
     * - Pillar 2 (audio_effects): Not directly used (KaraokeProcessor is in audio)
     * - Pillar 3 (mpv_backend): Media decoding via MpvBackend
     */
    class KaraokeController : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool active READ active NOTIFY activeChanged)
        Q_PROPERTY(int queueSize READ queueSize NOTIFY queueChanged)
        Q_PROPERTY(QString currentSinger READ currentSinger NOTIFY singerChanged)
        Q_PROPERTY(QString currentSong READ currentSong NOTIFY songChanged)
        Q_PROPERTY(double position READ position NOTIFY positionChanged)
        Q_PROPERTY(double duration READ duration NOTIFY songChanged)
        Q_PROPERTY(bool paused READ paused NOTIFY playbackChanged)
        Q_PROPERTY(int rotationNumber READ rotationNumber NOTIFY rotationChanged)
        Q_PROPERTY(bool vocalSuppression READ vocalSuppression WRITE setVocalSuppression NOTIFY vocalSuppressionChanged)

    public:
        /**
         * @brief Construct with explicit audio engine dependency
         * @param engine AudioEngine for DSP processing (Pillar 1)
         * @param backend MpvBackend for media decoding (Pillar 3)
         * @param parent QObject parent
         */
        explicit KaraokeController(AudioEngine* engine, MpvBackend* backend, QObject *parent = nullptr);
        ~KaraokeController();

        // Core functionality
        Q_INVOKABLE void startKaraoke();
        Q_INVOKABLE void stopKaraoke();
        Q_INVOKABLE void togglePause();
        Q_INVOKABLE void nextSong();

        // Singer management
        Q_INVOKABLE QString addSinger(const QString &name, const QString &displayName = "");
        Q_INVOKABLE void removeSinger(const QString &singerId);
        Q_INVOKABLE void moveSinger(const QString &singerId, int newPosition);
        Q_INVOKABLE QVariantList singers() const;

        // Queue management
        Q_INVOKABLE QString queueSong(const QString &songId, const QString &singerId = "", int keyChange = 0);
        Q_INVOKABLE void removeFromQueue(const QString &queueId);
        Q_INVOKABLE QVariantList queue() const;

        // Song database
        Q_INVOKABLE void scanLibrary(const QString &path = "");
        Q_INVOKABLE QVariantList searchSongs(const QString &query, int limit = 50);
        Q_INVOKABLE void importOpenKJDatabase(const QString &dbPath);

        // Audio controls - delegate to AudioEngine (Pillar 1)
        Q_INVOKABLE void setKeyChange(int semitones);  // -12 to +12
        Q_INVOKABLE void setVocalVolume(double volume);
        Q_INVOKABLE void setMusicVolume(double volume);
        Q_INVOKABLE void setEchoLevel(double level);
        Q_INVOKABLE void setVocalSuppression(bool enabled);
        bool vocalSuppression() const;

        // Display controls
        Q_INVOKABLE void setLyricsFont(const QString &fontFamily, int size);
        Q_INVOKABLE void setLyricsColor(const QColor &color);

        // Getters
        bool active() const { return m_active; }
        int queueSize() const { return m_queue.size(); }
        QString currentSinger() const;
        QString currentSong() const;
        double position() const { return m_position; }
        double duration() const { return m_duration; }
        bool paused() const { return m_paused; }
        int rotationNumber() const { return m_rotationNumber; }

        // Direct access to processors for advanced use
        AudioEngine* audioEngine() const { return m_engine; }
        KaraokeProcessor* karaokeProcessor() const;

    signals:
        void activeChanged();
        void queueChanged();
        void singerChanged();
        void songChanged();
        void positionChanged();
        void playbackChanged();
        void rotationChanged();
        void vocalSuppressionChanged();
        void songStarted(const QString &songId, const QString &singerId);
        void songEnded(const QString &songId, const QString &singerId, double rating);
        void lyricsLineChanged(const QString &line, int lineNumber, double timing);
        void pitchDetected(double frequency, const QString &note);
        void error(const QString &message);
        void frameReady(const QImage &frame);

    private slots:
        void onPlaybackPositionChanged(double pos);
        void onPlaybackFinished();
        void onCdgFrameReady(const QImage &frame);

    private:
        void initDatabase();
        void processQueue();
        void advanceRotation();
        bool parseKfnFile(const QString &path, KaraokeSong &song);
        bool parseZipFile(const QString &path, KaraokeSong &song);

        // Dependencies - explicit pillar usage
        AudioEngine* m_engine;       // Pillar 1: DSP processing (borrowed)
        MpvBackend* m_backend;       // Pillar 3: Media decoding (borrowed)

        bool m_active = false;
        bool m_paused = false;
        double m_position = 0.0;
        double m_duration = 0.0;
        int m_rotationNumber = 1;

        QHash<QString, KaraokeSong> m_songs;
        QHash<QString, KaraokeSinger> m_singers;
        QVector<KaraokeQueueItem> m_queue;

        QString m_currentSongId;
        QString m_currentSingerId;
        QString m_currentQueueId;

        // CDG/Graphics (not part of audio pillars)
        std::unique_ptr<class CdgDecoder> m_cdgDecoder;
        std::unique_ptr<class LyricsRenderer> m_lyricsRenderer;

        QSqlDatabase m_db;
    };

    // CDG Decoder remains (graphics only, not audio)
    class CdgDecoder : public QObject {
        Q_OBJECT
    public:
        explicit CdgDecoder(QObject *parent = nullptr);
        bool load(const QString &cdgPath);
        QImage frameAtTime(double timeSeconds);

    signals:
        void frameReady(const QImage &frame, double timestamp);

    private:
        struct CdgPacket {
            quint8 command;
            quint8 instruction;
            quint8 data[16];
        };
        QVector<CdgPacket> m_packets;
        QVector<double> m_packetTimes;
    };

    // Lyrics Renderer remains (UI component)
    class LyricsRenderer : public QObject {
        Q_OBJECT
    public:
        explicit LyricsRenderer(QObject *parent = nullptr);
        bool loadLrcFile(const QString &path);
        QString lineAtTime(double timeSeconds) const;

    private:
        struct LyricLine {
            double time;
            QString text;
        };
        QVector<LyricLine> m_lines;
    };

} // namespace Aegis
