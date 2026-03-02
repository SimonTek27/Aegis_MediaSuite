// disc.cpp - Optical disc implementation with libcdio integration
#include "disc.h"

// libcdio headers for CD/DVD access
#include <cdio/cdio.h>
#include <cdio/cdtext.h>
#include <cdio/mmc.h>
#include <cdio/paranoia.h>
#include <cdio/mmc_ll_cmds.h>

// Audio encoding support
#include <sndfile.h>

// Platform-specific headers for drive control
#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#endif

// Qt headers
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace Aegis {

    // ================ Internal RAII Wrappers ================

    /**
     * @brief Custom deleter for CdIo_t pointers
     */
    struct CdIoDeleter {
        void operator()(CdIo_t* p) const {
            if (p) cdio_destroy(p);
        }
    };

    /**
     * @brief Custom deleter for cdrom_drive_t pointers
     */
    struct CddaDriveDeleter {
        void operator()(cdrom_drive_t* p) const {
            if (p) cdda_close(p);
        }
    };

    /**
     * @brief Custom deleter for cdrom_paranoia_t pointers
     */
    struct ParanoiaDeleter {
        void operator()(cdrom_paranoia_t* p) const {
            if (p) paranoia_free(p);
        }
    };

    /**
     * @brief Custom deleter for SNDFILE pointers
     */
    struct SndFileDeleter {
        void operator()(SNDFILE* p) const {
            if (p) sf_close(p);
        }
    };

    // RAII type aliases for automatic resource management
    using CdIoPtr        = std::unique_ptr<CdIo_t, CdIoDeleter>;
    using CddaDrivePtr   = std::unique_ptr<cdrom_drive_t, CddaDriveDeleter>;
    using ParanoiaPtr    = std::unique_ptr<cdrom_paranoia_t, ParanoiaDeleter>;
    using SndFilePtr     = std::unique_ptr<SNDFILE, SndFileDeleter>;

    // ================ DiscWorker Implementation ================

    /**
     * @brief DiscWorker constructor
     */
    DiscWorker::DiscWorker(const QString &device, Task task, int trackNumber,
                           const QString &outputPath, int paranoiaLevel, QObject *parent)
    : QThread(parent)
    , m_device(device)
    , m_task(task)
    , m_trackNumber(trackNumber)
    , m_outputPath(outputPath)
    , m_paranoiaLevel(paranoiaLevel)
    {
        qDebug() << "DiscWorker created for device:" << device << "task:" << static_cast<int>(task);
    }

    /**
     * @brief DiscWorker destructor with thread safety
     */
    DiscWorker::~DiscWorker()
    {
        stopSafely();

        if (isRunning()) {
            wait(5000);
            if (isRunning()) {
                qWarning() << "DiscWorker thread did not terminate gracefully";
                terminate();
                wait(1000);
            }
        }

        qDebug() << "DiscWorker destroyed";
    }

    /**
     * @brief Request operation cancellation
     */
    void DiscWorker::stopSafely()
    {
        m_stop.store(true);
        emit statusChanged("Cancellation requested...");
    }

    /**
     * @brief Main thread execution function
     */
    void DiscWorker::run()
    {
        qDebug() << "DiscWorker thread started for task:" << static_cast<int>(m_task);

        try {
            switch (m_task) {
                case Task::Scan:
                    performScan();
                    break;

                case Task::RipTrack:
                    performRip();
                    break;

                case Task::RipDisc:
                    performRipDisc();
                    break;

                case Task::VerifyRip:
                    performVerify();
                    break;

                default:
                    qWarning() << "Unknown disc task:" << static_cast<int>(m_task);
                    emit error("Unknown operation type");
                    break;
            }
        } catch (const std::exception &e) {
            QString errorMsg = QString("Disc operation failed: %1").arg(e.what());
            qCritical() << errorMsg;
            emit error(errorMsg);
        }

        qDebug() << "DiscWorker thread completed";
    }

    /**
     * @brief Perform disc scanning operation
     * @return True if scan succeeded
     */
    bool DiscWorker::performScan()
    {
        emit statusChanged("Opening disc drive...");

        CdIoPtr cdio(cdio_open(m_device.toUtf8().constData(), DRIVER_DEVICE));
        if (!cdio) {
            emit error("Failed to open optical drive: " + m_device);
            emit scanCompleted(false, DiscInfo());
            return false;
        }

        discmode_t discMode = cdio_get_discmode(cdio.get());
        if (discMode == CDIO_DISC_NO_INFO || discMode == CDIO_DISC_MODE_NO_INFO) {
            emit error("No disc found or disc is unreadable");
            emit scanCompleted(false, DiscInfo());
            return false;
        }

        emit statusChanged("Reading disc structure...");

        DiscInfo info = readDiscInfo(cdio.get());
        m_cachedInfo = info;

        QString discTypeStr;
        switch (discMode) {
            case CDIO_DISC_MODE_CD_DA:   discTypeStr = "Audio CD";  break;
            case CDIO_DISC_MODE_DVD_ROM:
            case CDIO_DISC_MODE_DVD_VIDEO: discTypeStr = "DVD";     break;
            case CDIO_DISC_MODE_BD:      discTypeStr = "Blu-ray";   break;
            default:                     discTypeStr = "Data Disc"; break;
        }

        emit statusChanged(QString("Disc detected: %1").arg(discTypeStr));
        emit scanCompleted(true, info);

        return true;
    }

    /**
     * @brief Read disc metadata from libcdio
     * @param cdio CD I/O handle
     * @return Disc information structure
     */
    DiscInfo DiscWorker::readDiscInfo(CdIo_t *cdio)
    {
        DiscInfo info;

        track_t firstTrack = cdio_get_first_track_num(cdio);
        track_t lastTrack  = cdio_get_last_track_num(cdio);
        info.totalTracks   = lastTrack - firstTrack + 1;

        lsn_t leadout = cdio_get_track_lsn(cdio, CDIO_CDROM_LEADOUT_TRACK);

        for (track_t trackNum = firstTrack; trackNum <= lastTrack; ++trackNum) {
            if (m_stop.load()) break;

            DiscTrack track;
            track.number = trackNum;

            lsn_t start = cdio_get_track_lsn(cdio, trackNum);
            lsn_t end   = (trackNum == lastTrack)
                          ? leadout
                          : cdio_get_track_lsn(cdio, trackNum + 1);

            track.startFrame = static_cast<int>(start);
            track.endFrame   = static_cast<int>(end);
            track.duration   = static_cast<int>((end - start) / CDIO_CD_FRAMES_PER_SEC);

            track_format_t format = cdio_get_track_format(cdio, trackNum);
            track.isAudio = (format == TRACK_FORMAT_AUDIO);
            track.isData  = !track.isAudio;

            switch (format) {
                case TRACK_FORMAT_AUDIO:
                    track.format     = "Audio";
                    track.channels   = 2;
                    track.sampleRate = 44100;
                    break;
                case TRACK_FORMAT_DATA:  track.format = "Data";       break;
                case TRACK_FORMAT_CDI:   track.format = "CD-i";       break;
                case TRACK_FORMAT_XA:    track.format = "CD-ROM XA";  break;
                default:                 track.format = "Unknown";     break;
            }

            char isrcBuffer[13] = {0};
            if (mmc_get_isrc(cdio, trackNum, isrcBuffer) == DRIVER_OP_SUCCESS) {
                track.isrc = QString::fromLatin1(isrcBuffer);
            }

            info.tracks.append(track);

            if (info.totalTracks > 20 && trackNum % 5 == 0) {
                int percent = ((trackNum - firstTrack) * 100) / info.totalTracks;
                emit operationProgress(QString("Reading track %1/%2").arg(trackNum).arg(lastTrack), percent);
            }
        }

        readCDText(cdio, info);
        info.discId   = calculateDiscId(cdio, info);
        info.discType = cdio_get_discmode(cdio);

        qDebug() << "Disc scan completed:" << info.totalTracks << "tracks, ID:" << info.discId;

        return info;
    }

    /**
     * @brief Extract CD-TEXT metadata if available
     */
    void DiscWorker::readCDText(CdIo_t *cdio, DiscInfo &info)
    {
        cdtext_t *cdtext = cdio_get_cdtext(cdio);
        if (!cdtext) {
            info.hasCDText = false;
            return;
        }

        info.hasCDText = true;

        const char *albumTitle  = cdtext_get(ETITLE,     cdtext, 0);
        const char *albumArtist = cdtext_get(EPERFORMER, cdtext, 0);
        const char *albumGenre  = cdtext_get(EGENRE,     cdtext, 0);

        if (albumTitle)  info.title  = QString::fromUtf8(albumTitle);
        if (albumArtist) info.artist = QString::fromUtf8(albumArtist);
        if (albumGenre)  info.genre  = QString::fromUtf8(albumGenre);

        for (int i = 0; i < info.tracks.size(); ++i) {
            const char *trackTitle  = cdtext_get(ETITLE,     cdtext, i + 1);
            const char *trackArtist = cdtext_get(EPERFORMER, cdtext, i + 1);

            if (trackTitle)  info.tracks[i].title  = QString::fromUtf8(trackTitle);
            if (trackArtist) info.tracks[i].artist = QString::fromUtf8(trackArtist);
        }

        qDebug() << "CD-TEXT extracted:" << info.artist << "-" << info.title;
    }

    /**
     * @brief Calculate MusicBrainz-compatible disc ID
     */
    QString DiscWorker::calculateDiscId(CdIo_t *cdio, const DiscInfo &info)
    {
        if (info.tracks.isEmpty()) return QString();

        QStringList offsets;

        // First offset: first track start + 150 sectors
        offsets.append(QString::number(info.tracks.first().startFrame + 150));

        // Each track's start offset + 150 sectors
        for (const auto &track : info.tracks) {
            offsets.append(QString::number(track.startFrame + 150));
        }

        // Lead-out offset + 150 sectors
        lsn_t leadout = cdio_get_track_lsn(cdio, CDIO_CDROM_LEADOUT_TRACK);
        offsets.append(QString::number(static_cast<int>(leadout + 150)));

        return QString("%1+%2+%3").arg(info.totalTracks).arg(offsets.first()).arg(offsets.join("+"));
    }

    /**
     * @brief Perform track ripping operation
     * @return True if rip succeeded
     */
    bool DiscWorker::performRip()
    {
        if (m_trackNumber < 1) {
            emit error("Invalid track number");
            return false;
        }

        emit statusChanged("Initializing CD-DA extraction...");

        CddaDrivePtr drive(cdda_identify(m_device.toUtf8().constData(), 0, nullptr));
        if (!drive) {
            emit error("Drive does not support CD-DA extraction");
            return false;
        }

        if (cdda_open(drive.get()) != 0) {
            emit error("Failed to open drive for audio extraction");
            return false;
        }

        cdda_verbose_set(drive.get(), CDDA_MESSAGE_FORGETIT, CDDA_MESSAGE_FORGETIT);

        ParanoiaPtr paranoia(paranoia_init(drive.get()));
        if (!paranoia) {
            emit error("Failed to initialize paranoia error correction");
            return false;
        }

        paranoia_mode_t mode;
        switch (m_paranoiaLevel) {
            case 0:  mode = PARANOIA_MODE_DISABLE; break;
            case 1:  mode = PARANOIA_MODE_OVERLAP; break;
            case 2:  mode = PARANOIA_MODE_VERIFY;  break;
            default: mode = PARANOIA_MODE_FULL;    break;
        }
        paranoia_modeset(paranoia.get(), mode);

        lsn_t start = cdda_track_firstsector(drive.get(), m_trackNumber);
        lsn_t end   = cdda_track_lastsector(drive.get(), m_trackNumber);
        lsn_t total = end - start;

        if (start < 0 || end < 0 || paranoia_seek(paranoia.get(), start, SEEK_SET) < 0) {
            emit error("Invalid track sector boundaries");
            return false;
        }

        QFileInfo outputInfo(m_outputPath);
        QDir().mkpath(outputInfo.path());

        SF_INFO sfinfo    = {};
        sfinfo.samplerate = 44100;
        sfinfo.channels   = 2;
        sfinfo.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

        SndFilePtr outfile(sf_open(m_outputPath.toUtf8().constData(), SFM_WRITE, &sfinfo), sf_close);
        if (!outfile) {
            emit error("Failed to create output file: " + m_outputPath);
            return false;
        }

        std::vector<float> floatBuffer(CDIO_CD_FRAMESIZE_RAW * 2);
        lsn_t current = start;
        bool success  = true;

        emit statusChanged("Ripping track...");

        while (current <= end && !m_stop.load()) {
            int16_t *rawBuffer = paranoia_read(paranoia.get(), nullptr);
            if (!rawBuffer) {
                emit error("Read error at sector " + QString::number(current));
                success = false;
                break;
            }

            for (int i = 0; i < CDIO_CD_FRAMESIZE_RAW * 2; i++) {
                floatBuffer[i] = rawBuffer[i] / 32768.0f;
            }

            sf_count_t written = sf_writef_float(outfile.get(), floatBuffer.data(), CDIO_CD_FRAMESIZE_RAW);
            if (written != CDIO_CD_FRAMESIZE_RAW) {
                emit error("Write error at sector " + QString::number(current));
                success = false;
                break;
            }

            current++;

            if (current % 100 == 0 || current == end) {
                int percent = static_cast<int>(((current - start) * 100) / total);
                emit ripProgress(percent);
                emit operationProgress(QString("Ripping sector %1/%2").arg(current - start).arg(total), percent);
            }
        }

        if (m_stop.load()) {
            emit statusChanged("Ripping cancelled");
            QFile::remove(m_outputPath);
            success = false;
        }

        outfile.reset();

        if (success) {
            emit statusChanged("Ripping completed successfully");
            emit ripCompleted(true, m_outputPath);
            qDebug() << "Track ripped successfully:" << m_outputPath;
        } else {
            emit ripCompleted(false, QString());
        }

        return success;
    }

    /**
     * @brief Perform complete disc ripping
     * @return True if all tracks ripped successfully
     */
    bool DiscWorker::performRipDisc()
    {
        emit error("Full disc ripping not yet implemented");
        return false;
    }

    /**
     * @brief Verify ripped data integrity
     * @return True if verification passed
     */
    bool DiscWorker::performVerify()
    {
        emit error("Rip verification not yet implemented");
        return false;
    }

    // ================ Disc Controller Implementation ================

    /**
     * @brief Disc constructor
     */
    Disc::Disc(const QString &device, QObject *parent)
    : QObject(parent)
    , m_device(device.isEmpty() ? findDefaultDrive() : device)
    {
        qDebug() << "Disc controller created for device:" << m_device;
    }

    Disc::~Disc()
    {
        cleanupWorker();
    }

    QString Disc::device() const { return m_device; }

    void Disc::setDevice(const QString &device)
    {
        if (m_device != device && !device.isEmpty()) {
            m_device = device;
            emit deviceChanged();
            scanDisc();
        }
    }

    bool Disc::isAudioCD() const  { return m_info.discType == CDIO_DISC_MODE_CD_DA;    }
    bool Disc::isDVDVideo() const { return m_info.discType == CDIO_DISC_MODE_DVD_VIDEO; }
    bool Disc::isBluRay() const   { return m_info.discType == CDIO_DISC_MODE_BD;        }

    /**
     * @brief Scan disc for metadata
     */
    void Disc::scanDisc()
    {
        if (m_worker) {
            qWarning() << "Scan already in progress";
            return;
        }

        if (m_device.isEmpty()) {
            emit error("No optical drive specified");
            return;
        }

        m_worker = new DiscWorker(m_device, DiscWorker::Task::Scan, 0, QString(), 0, this);

        connect(m_worker, &DiscWorker::scanCompleted,    this, &Disc::onScanCompleted);
        connect(m_worker, &DiscWorker::operationProgress,this, &Disc::operationProgress);
        connect(m_worker, &DiscWorker::statusChanged,    this, [this](const QString &status) {
            emit operationProgress(status, -1);
        });
        connect(m_worker, &DiscWorker::error, this, &Disc::error);

        connect(m_worker, &DiscWorker::finished, this, [this]() {
            m_worker->deleteLater();
            m_worker = nullptr;
            emit workingChanged();
        });

        emit workingChanged();
        m_worker->start();

        qDebug() << "Started disc scan for device:" << m_device;
    }

    /**
     * @brief Handle scan completion
     */
    void Disc::onScanCompleted(bool success, const DiscInfo &info)
    {
        if (success) {
            updateDiscInfo(info);
            emit discChanged();
            qDebug() << "Disc scan completed:" << info.totalTracks << "tracks";
        } else {
            m_info = DiscInfo();
            emit discChanged();
            emit error("Failed to read disc");
        }
    }

    /**
     * @brief Eject disc from drive
     */
    void Disc::eject()
    {
        #ifdef __linux__
        int fd = open(m_device.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            if (ioctl(fd, CDROMEJECT) == 0) {
                qDebug() << "Disc ejected from" << m_device;
            } else {
                emit error("Failed to eject disc");
            }
            close(fd);
        } else {
            emit error("Cannot open device for eject");
        }
        #else
        emit error("Eject not supported on this platform");
        #endif

        m_info = DiscInfo();
        emit discChanged();
    }

    /**
     * @brief Close drive tray
     */
    void Disc::closeTray()
    {
        #ifdef __linux__
        int fd = open(m_device.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            if (ioctl(fd, CDROMCLOSETRAY) == 0) {
                qDebug() << "Tray closed for" << m_device;
                QTimer::singleShot(2000, this, &Disc::scanDisc);
            } else {
                emit error("Failed to close tray");
            }
            close(fd);
        }
        #else
        emit error("Tray control not supported on this platform");
        #endif
    }

    /**
     * @brief Play audio track
     */
    void Disc::playTrack(int trackNumber)
    {
        if (trackNumber < 1 || trackNumber > m_info.totalTracks) {
            emit error(QString("Invalid track number: %1").arg(trackNumber));
            return;
        }

        QString url = QString("cdda://%1/%2").arg(m_device).arg(trackNumber);
        emit playRequested(url);

        qDebug() << "Play requested for track" << trackNumber << "URL:" << url;
    }

    /**
     * @brief Play DVD title
     */
    void Disc::playDVDTitle(int titleNumber, int chapterNumber)
    {
        if (!isDVDVideo()) {
            emit error("Disc is not a DVD-Video");
            return;
        }

        QString url = QString("dvd://%1/%2").arg(m_device).arg(titleNumber);
        if (chapterNumber > 0) {
            url += QString("#%1").arg(chapterNumber);
        }

        emit playRequested(url);
    }

    /**
     * @brief Rip single audio track to file
     */
    void Disc::ripTrack(int trackNumber, const QString &outputPath,
                        int paranoiaLevel, const QString &format)
    {
        if (trackNumber < 1 || trackNumber > m_info.totalTracks) {
            emit error(QString("Invalid track number: %1").arg(trackNumber));
            return;
        }

        if (m_worker) {
            emit error("Another operation is in progress");
            return;
        }

        QFileInfo outputInfo(outputPath);
        if (!outputInfo.absoluteDir().exists()) {
            emit error("Output directory does not exist");
            return;
        }

        m_worker = new DiscWorker(m_device, DiscWorker::Task::RipTrack,
                                  trackNumber, outputPath, paranoiaLevel, this);

        connect(m_worker, &DiscWorker::ripProgress, this, [this, trackNumber](int percent) {
            emit ripProgress(trackNumber, percent);
        });
        connect(m_worker, &DiscWorker::ripCompleted, this, &Disc::onRipCompleted);
        connect(m_worker, &DiscWorker::error, this, &Disc::error);

        connect(m_worker, &DiscWorker::finished, this, [this]() {
            m_worker->deleteLater();
            m_worker = nullptr;
            emit workingChanged();
        });

        emit workingChanged();
        m_worker->start();

        qDebug() << "Started ripping track" << trackNumber << "to" << outputPath;
    }

    /**
     * @brief Handle rip completion
     *
     * [FIX Bug #3] La condizione originale `else if (!filePath.isEmpty())` era invertita:
     * in caso di fallimento performRip() emette ripCompleted(false, QString()), quindi
     * il path è sempre vuoto e l'errore non veniva mai segnalato. Corretta in `else`.
     */
    void Disc::onRipCompleted(bool success, const QString &filePath)
    {
        if (success) {
            emit operationProgress("Ripping completed successfully", 100);
            qDebug() << "Track ripped to:" << filePath;
        } else {
            emit error("Ripping failed");
        }
    }

    /**
     * @brief Rip entire disc
     *
     * [FIX Bug #1] La versione originale catturava `ripNext` e `currentTrack` per
     * riferimento in una std::function locale (stack), causando dangling reference
     * quando QTimer::singleShot invocava la lambda in modo asincrono, dopo che lo
     * stack frame di ripWholeDisc era già stato deallocato. Corretta usando
     * std::shared_ptr per condividere lo stato tra la lambda e il timer.
     */
    void Disc::ripWholeDisc(const QString &outputDir, const QString &format,
                            int paranoiaLevel, bool createCueSheet)
    {
        Q_UNUSED(createCueSheet)

        if (m_info.totalTracks == 0) {
            emit error("No tracks to rip");
            return;
        }

        if (m_worker) {
            emit error("Another operation is in progress");
            return;
        }

        QDir dir(outputDir);
        if (!dir.exists()) {
            if (!dir.mkpath(".")) {
                emit error("Failed to create output directory");
                return;
            }
        }

        // Use shared_ptr so the lambda owns the state and remains valid
        // even after ripWholeDisc() has returned (async timer callbacks).
        auto state = std::make_shared<int>(1);
        auto ripNext = std::make_shared<std::function<void()>>();

        *ripNext = [this, ripNext, state, outputDir, format, paranoiaLevel]() {
            // Skip non-audio tracks
            while (*state <= m_info.totalTracks && !m_info.tracks[*state - 1].isAudio) {
                (*state)++;
            }

            if (*state > m_info.totalTracks) {
                emit operationProgress("All tracks ripped successfully", 100);
                return;
            }

            int trackNum = *state;
            QString title = m_info.tracks[trackNum - 1].title.isEmpty()
                            ? QString("Track %1").arg(trackNum)
                            : m_info.tracks[trackNum - 1].title;

            QString filename = QString("%1/%2 - %3.%4")
                               .arg(outputDir)
                               .arg(trackNum, 2, 10, QChar('0'))
                               .arg(title)
                               .arg(format);

            ripTrack(trackNum, filename, paranoiaLevel, format);

            // Chain next rip when this track reaches 100%
            connect(this, &Disc::ripProgress, this,
                    [this, ripNext, state, trackNum](int track, int percent) {
                        if (percent >= 100 && track == trackNum) {
                            disconnect(this, &Disc::ripProgress, nullptr, nullptr);
                            (*state)++;
                            // Capture ripNext by value so the shared_ptr keeps the
                            // lambda alive until QTimer fires the callback.
                            QTimer::singleShot(100, this, [ripNext]() {
                                (*ripNext)();
                            });
                        }
                    }, Qt::SingleShotConnection);
        };

        (*ripNext)();
    }

    /**
     * @brief Cancel current operation
     */
    void Disc::cancelOperation()
    {
        if (m_worker) {
            m_worker->stopSafely();
            if (m_worker->isRunning()) {
                m_worker->wait(5000);
            }
            cleanupWorker();
        }
    }

    /**
     * @brief Fetch metadata from MusicBrainz
     */
    void Disc::fetchMetadataFromMusicBrainz()
    {
        if (m_info.discId.isEmpty()) {
            emit error("No disc ID available for lookup");
            return;
        }

        QNetworkAccessManager *nam = new QNetworkAccessManager(this);
        QString url = QString("https://musicbrainz.org/ws/2/discid/%1?fmt=json").arg(m_info.discId);

        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "AegisMediaPlayer/1.0 (https://github.com/aegis)");

        QNetworkReply *reply = nam->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
            reply->deleteLater();
            nam->deleteLater();

            if (reply->error() != QNetworkReply::NoError) {
                emit error("MusicBrainz lookup failed: " + reply->errorString());
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isNull()) {
                emit error("Invalid MusicBrainz response");
                return;
            }

            emit metadataFetched(doc.object().toVariantMap());
        });
    }

    QString Disc::discType() const
    {
        switch (m_info.discType) {
            case CDIO_DISC_MODE_CD_DA:    return "Audio CD";
            case CDIO_DISC_MODE_DVD_ROM:  return "DVD-ROM";
            case CDIO_DISC_MODE_DVD_VIDEO:return "DVD-Video";
            case CDIO_DISC_MODE_BD:       return "Blu-ray";
            case CDIO_DISC_MODE_CD_XA:    return "CD-ROM XA";
            default:                      return "Data Disc";
        }
    }

    bool Disc::driveSupports(const QString &feature) const
    {
        if (feature == "cdda")   return true;
        if (feature == "dvd")    return m_device.contains("dvd", Qt::CaseInsensitive);
        if (feature == "bluray") return m_device.contains("bd",  Qt::CaseInsensitive);
        if (feature == "burning")return false;
        return false;
    }

    // ================ Private Helper Methods ================

    QString Disc::findDefaultDrive() const
    {
        QStringList candidates = {
            "/dev/sr0", "/dev/cdrom", "/dev/dvd", "/dev/sr1",
            "/dev/cdrom1", "/dev/dvd1"
        };

        for (const QString &candidate : candidates) {
            if (QFile::exists(candidate)) {
                return candidate;
            }
        }

        char **drives = cdio_get_devices(DRIVER_DEVICE);
        if (drives && drives[0]) {
            QString device = QString::fromUtf8(drives[0]);
            cdio_free_device_list(drives);
            return device;
        }

        return QString();
    }

    void Disc::cleanupWorker()
    {
        if (m_worker) {
            if (m_worker->isRunning()) {
                m_worker->terminate();
                m_worker->wait(1000);
            }
            m_worker->deleteLater();
            m_worker = nullptr;
            emit workingChanged();
        }
    }

    void Disc::updateDiscInfo(const DiscInfo &info)
    {
        m_info = info;

        if (m_info.title.isEmpty() && m_info.totalTracks > 0) {
            if (m_info.artist.isEmpty()) {
                m_info.title = QString("Unknown Album (%1 Tracks)").arg(m_info.totalTracks);
            } else {
                m_info.title = QString("%1 - Unknown Album").arg(m_info.artist);
            }
        }
    }

} // namespace Aegis
