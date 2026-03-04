// disc.h - Production-grade optical disc management
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QVariantList>
#include <QUrl>
#include <memory>
#include <optional>

// Forward declarations
struct CdIo_t;
struct cdrom_drive_t;
struct cdrom_paranoia_t;
struct SNDFILE;

namespace Aegis {

// ============================================================================
// Data Structures
// ============================================================================

struct DiscTrack {
    int number{0};
    int startFrame{0};
    int endFrame{0};
    int duration{0};  // seconds
    QString title;
    QString artist;
    QString isrc;
    bool isAudio{false};
    bool isData{false};
    QString format;
    int channels{2};
    int sampleRate{44100};
};

struct DiscInfo {
    QString discId;
    QString artist;
    QString title;
    QString genre;
    int year{0};
    int totalTracks{0};
    QVector<DiscTrack> tracks;
    int discType{0};  // discmode_t from libcdio
    bool hasCDText{false};
    
    DiscInfo();
    ~DiscInfo();
    
    QString discTypeString() const;
    bool isAudioCD() const;
    bool isVideoDVD() const;
    bool isBluRay() const;
    qint64 totalAudioDuration() const;
};

struct RipResult {
    QString outputPath;
    qint64 bytesWritten{0};
    int framesWritten{0};
    int errorsEncountered{0};
};

// ============================================================================
// Result Type (forward declaration)
// ============================================================================

template<typename T>
class Result;

// ============================================================================
// DiscRipper - Main API for disc operations
// ============================================================================

class DiscRipper : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY deviceChanged)
    Q_PROPERTY(bool working READ isWorking NOTIFY workingChanged)

public:
    struct RipOptions {
        QString outputPath;      // For single track
        QString outputDir;       // For whole disc
        int paranoiaLevel{3};    // 0-3
        QString format{"flac"};  // flac, wav, aiff, etc.
        bool ignoreErrors{false};
        bool createCue{true};
    };

    explicit DiscRipper(const QString& device = QString(), QObject* parent = nullptr);
    ~DiscRipper() override;

    // Device management
    QString device() const;
    void setDevice(const QString& device);
    bool isWorking() const;

    // Synchronous operations (blocking)
    Result<DiscInfo> scanDiscSync();
    Result<RipResult> ripTrackSync(int trackNumber, const RipOptions& options);

    // Asynchronous operations (signals)
    Q_INVOKABLE void scanDiscAsync();
    Q_INVOKABLE void ripTrackAsync(int trackNumber, const RipOptions& options);
    Q_INVOKABLE void ripDiscAsync(const RipOptions& options);
    Q_INVOKABLE void cancel();

    // Drive control
    Q_INVOKABLE bool eject();
    Q_INVOKABLE bool closeTray();

    // Metadata
    Q_INVOKABLE void fetchMusicBrainzMetadata(const QString& discId = QString());

    // Utility
    static QStringList enumerateDrives();
    static QString findDefaultDrive();

signals:
    void deviceChanged();
    void workingChanged(bool working);
    void progress(int percent, const QString& message);
    void trackProgress(int track, int percent);
    void trackCompleted(int track, const QString& path);
    void scanCompleted(const DiscInfo& info);
    void ripCompleted(const RipResult& result);
    void discRipCompleted();
    void operationCancelled();
    void discEjected();
    void trayClosed();
    void metadataFetched(const QVariantMap& metadata);
    void error(const QString& message);

private:
    void setupWorkerConnections();
    void cleanupWorker();
    QString generateOutputPath(const DiscTrack& track, const RipOptions& options) const;
    QString sanitizeFilename(const QString& name) const;

    QString m_device;
    DiscInfo m_lastScanInfo;
    QString m_lastDiscId;
    QThread* m_worker{nullptr};
    class DiscLogger m_logger;
};

// ============================================================================
// Disc - Legacy compatibility layer (QML-friendly)
// ============================================================================

class Disc : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY deviceChanged)
    Q_PROPERTY(bool working READ working NOTIFY workingChanged)
    Q_PROPERTY(bool discPresent READ discPresent NOTIFY discChanged)
    Q_PROPERTY(QString discLabel READ discLabel NOTIFY discChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY discChanged)
    Q_PROPERTY(bool isAudioCD READ isAudioCD NOTIFY discChanged)
    Q_PROPERTY(bool isDVDVideo READ isDVDVideo NOTIFY discChanged)
    Q_PROPERTY(bool isBluRay READ isBluRay NOTIFY discChanged)

public:
    explicit Disc(const QString& device = QString(), QObject* parent = nullptr);
    ~Disc() override;

    // Properties
    QString device() const;
    void setDevice(const QString& device);
    bool working() const;
    bool discPresent() const { return m_info.totalTracks > 0; }
    QString discLabel() const;
    int trackCount() const;
    bool isAudioCD() const;
    bool isDVDVideo() const;
    bool isBluRay() const;

    // Operations
    Q_INVOKABLE void scanDisc();
    Q_INVOKABLE void ripTrack(int trackNumber, const QString& outputPath,
                              int paranoiaLevel = 3, const QString& format = "flac");
    Q_INVOKABLE void ripWholeDisc(const QString& outputDir, const QString& format = "flac",
                                  int paranoiaLevel = 3, bool createCueSheet = true);
    Q_INVOKABLE void cancelOperation();
    Q_INVOKABLE void eject();
    Q_INVOKABLE void closeTray();
    Q_INVOKABLE void playTrack(int trackNumber);
    Q_INVOKABLE void playDVDTitle(int titleNumber = 1, int chapterNumber = 0);
    Q_INVOKABLE void fetchMetadataFromMusicBrainz();

    // Data access
    Q_INVOKABLE QVariantList tracks() const;
    Q_INVOKABLE QString discType() const;
    Q_INVOKABLE bool driveSupports(const QString& feature) const;

signals:
    void deviceChanged();
    void workingChanged();
    void discChanged();
    void ripProgress(int track, int percent);
    void operationProgress(const QString& message, int percent);
    void playRequested(const QString& url);
    void error(const QString& message);

private slots:
    void onScanCompleted(const DiscInfo& info);
    void onRipCompleted(const RipResult& result);

private:
    QString findDefaultDrive() const;
    void updateDiscInfo(const DiscInfo& info);

    std::unique_ptr<DiscRipper> m_ripper;
    DiscInfo m_info;
};

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::DiscInfo)
Q_DECLARE_METATYPE(Aegis::RipResult)