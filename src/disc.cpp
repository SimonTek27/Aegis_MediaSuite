// disc.cpp - Production-grade optical disc implementation
// Features:
// - Complete RAII resource management
// - Thread-safe operations with worker threads
// - Comprehensive error handling with Result type
// - Async/sync operation support
// - Full disc ripping with paranoia error correction
// - MusicBrainz integration
// - CD-TEXT support

#include "disc.h"
#include "raii_wrappers.h"

// libcdio headers
#include <cdio/cdio.h>
#include <cdio/cdtext.h>
#include <cdio/mmc.h>
#include <cdio/paranoia.h>
#include <cdio/mmc_ll_cmds.h>

// Audio encoding
#include <sndfile.h>

// Platform headers
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
#include <QTimer>
#include <QStandardPaths>
#include <QThread>

namespace Aegis {

    // ============================================================================
    // Logger for disc operations
    // ============================================================================

    class DiscLogger {
    public:
        enum Level { Debug, Info, Warning, Error };

        static void log(Level level, const QString& message,
                        const QString& component = "Disc") {
            QString prefix;
            switch (level) {
                case Debug:   prefix = "DEBUG"; break;
                case Info:    prefix = "INFO"; break;
                case Warning: prefix = "WARN"; break;
                case Error:   prefix = "ERROR"; break;
            }

            qDebug().noquote() << QString("[%1] %2: %3")
            .arg(prefix, component, message);
                        }

                        static void debug(const QString& msg, const QString& comp = "Disc") {
                            log(Debug, msg, comp);
                        }
                        static void info(const QString& msg, const QString& comp = "Disc") {
                            log(Info, msg, comp);
                        }
                        static void warning(const QString& msg, const QString& comp = "Disc") {
                            log(Warning, msg, comp);
                        }
                        static void error(const QString& msg, const QString& comp = "Disc") {
                            log(Error, msg, comp);
                        }
    };

    // ============================================================================
    // Result Type for Error Handling
    // ============================================================================

    template<typename T>
    class Result {
        std::variant<T, QString> m_value;

    public:
        Result(const T& value) : m_value(value) {}
        Result(T&& value) : m_value(std::move(value)) {}
        Result(const QString& error) : m_value(error) {}

        bool isSuccess() const { return std::holds_alternative<T>(m_value); }
        bool isError() const { return std::holds_alternative<QString>(m_value); }

        const T& value() const { return std::get<T>(m_value); }
        T&& takeValue() { return std::move(std::get<T>(m_value)); }

        const QString& error() const { return std::get<QString>(m_value); }

        template<typename Func>
        auto map(Func&& f) const -> Result<decltype(f(std::declval<T>()))> {
            if (isSuccess()) {
                return Result<decltype(f(std::declval<T>()))>(f(value()));
            }
            return error();
        }

        template<typename Func>
        auto andThen(Func&& f) const -> decltype(f(std::declval<T>())) {
            if (isSuccess()) {
                return f(value());
            }
            return error();
        }
    };

    // ============================================================================
    // RAII Wrappers for libcdio (using the enhanced wrappers from raii_wrappers.h)
    // ============================================================================

    // Custom deleters
    struct CdIoDeleter {
        void operator()(CdIo_t* p) const { if (p) cdio_destroy(p); }
    };
    struct CddaDriveDeleter {
        void operator()(cdrom_drive_t* p) const { if (p) cdda_close(p); }
    };
    struct ParanoiaDeleter {
        void operator()(cdrom_paranoia_t* p) const { if (p) paranoia_free(p); }
    };
    struct SndFileDeleter {
        void operator()(SNDFILE* p) const { if (p) sf_close(p); }
    };

    // Resource handles
    using CdIoHandle = std::unique_ptr<CdIo_t, CdIoDeleter>;
    using CddaHandle = std::unique_ptr<cdrom_drive_t, CddaDriveDeleter>;
    using ParanoiaHandle = std::unique_ptr<cdrom_paranoia_t, ParanoiaDeleter>;
    using SndFileHandle = std::unique_ptr<SNDFILE, SndFileDeleter>;

    // ============================================================================
    // DiscInfo Implementation
    // ============================================================================

    DiscInfo::DiscInfo() = default;
    DiscInfo::~DiscInfo() = default;

    QString DiscInfo::discTypeString() const {
        switch (discType) {
            case CDIO_DISC_MODE_CD_DA:    return "Audio CD";
            case CDIO_DISC_MODE_CD_ROM:   return "CD-ROM";
            case CDIO_DISC_MODE_CD_R:     return "CD-R";
            case CDIO_DISC_MODE_CD_RW:    return "CD-RW";
            case CDIO_DISC_MODE_DVD_ROM:  return "DVD-ROM";
            case CDIO_DISC_MODE_DVD_RAM:  return "DVD-RAM";
            case CDIO_DISC_MODE_DVD_R:    return "DVD-R";
            case CDIO_DISC_MODE_DVD_RW:   return "DVD-RW";
            case CDIO_DISC_MODE_DVD_PR:   return "DVD+R";
            case CDIO_DISC_MODE_DVD_PRW:  return "DVD+RW";
            case CDIO_DISC_MODE_DVD_VIDEO:return "DVD-Video";
            case CDIO_DISC_MODE_BD:       return "Blu-ray";
            case CDIO_DISC_MODE_BD_R:     return "BD-R";
            case CDIO_DISC_MODE_BD_RE:    return "BD-RE";
            default:                      return "Data Disc";
        }
    }

    bool DiscInfo::isAudioCD() const {
        return discType == CDIO_DISC_MODE_CD_DA;
    }

    bool DiscInfo::isVideoDVD() const {
        return discType == CDIO_DISC_MODE_DVD_VIDEO;
    }

    bool DiscInfo::isBluRay() const {
        return discType == CDIO_DISC_MODE_BD ||
        discType == CDIO_DISC_MODE_BD_R ||
        discType == CDIO_DISC_MODE_BD_RE;
    }

    qint64 DiscInfo::totalAudioDuration() const {
        qint64 total = 0;
        for (const auto& track : tracks) {
            if (track.isAudio) total += track.duration;
        }
        return total;
    }

    // ============================================================================
    // DiscScanner - Synchronous disc scanning
    // ============================================================================

    class DiscScanner {
    public:
        static Result<DiscInfo> scan(const QString& device) {
            DiscLogger::info(QString("Scanning disc in device: %1").arg(device));

            auto cdioResult = openDevice(device);
            if (cdioResult.isError()) return cdioResult.error();

            auto cdio = cdioResult.takeValue();

            auto modeResult = getDiscMode(cdio);
            if (modeResult.isError()) return modeResult.error();

            DiscInfo info;
            info.discType = modeResult.value();

            auto tracksResult = readTracks(cdio, info);
            if (tracksResult.isError()) return tracksResult.error();
            info.tracks = tracksResult.value();
            info.totalTracks = info.tracks.size();

            readCDText(cdio, info);
            info.discId = calculateDiscId(cdio, info);

            DiscLogger::info(QString("Scan completed: %1 tracks, ID: %2")
            .arg(info.totalTracks).arg(info.discId));

            return Result<DiscInfo>(std::move(info));
        }

    private:
        static Result<CdIoHandle> openDevice(const QString& device) {
            CdIo_t* raw = cdio_open(device.toUtf8().constData(), DRIVER_DEVICE);
            if (!raw) {
                return Result<CdIoHandle>("Failed to open optical drive: " + device);
            }
            return Result<CdIoHandle>(CdIoHandle(raw));
        }

        static Result<discmode_t> getDiscMode(const CdIoHandle& cdio) {
            discmode_t mode = cdio_get_discmode(cdio.get());
            if (mode == CDIO_DISC_NO_INFO || mode == CDIO_DISC_MODE_NO_INFO) {
                return Result<discmode_t>("No disc found or disc is unreadable");
            }
            return Result<discmode_t>(mode);
        }

        static Result<QVector<DiscTrack>> readTracks(const CdIoHandle& cdio, DiscInfo& info) {
            QVector<DiscTrack> tracks;

            track_t firstTrack = cdio_get_first_track_num(cdio.get());
            track_t lastTrack = cdio_get_last_track_num(cdio.get());

            if (firstTrack == CDIO_INVALID_TRACK || lastTrack == CDIO_INVALID_TRACK) {
                return Result<QVector<DiscTrack>>("Failed to read track numbers");
            }

            lsn_t leadout = cdio_get_track_lsn(cdio.get(), CDIO_CDROM_LEADOUT_TRACK);

            for (track_t trackNum = firstTrack; trackNum <= lastTrack; ++trackNum) {
                DiscTrack track;
                track.number = trackNum;

                lsn_t start = cdio_get_track_lsn(cdio.get(), trackNum);
                lsn_t end = (trackNum == lastTrack)
                ? leadout
                : cdio_get_track_lsn(cdio.get(), trackNum + 1);

                track.startFrame = static_cast<int>(start);
                track.endFrame = static_cast<int>(end);
                track.duration = static_cast<int>((end - start) / CDIO_CD_FRAMES_PER_SEC);

                track_format_t format = cdio_get_track_format(cdio.get(), trackNum);
                track.isAudio = (format == TRACK_FORMAT_AUDIO);
                track.isData = !track.isAudio;

                // Set format string
                switch (format) {
                    case TRACK_FORMAT_AUDIO: track.format = "Audio"; break;
                    case TRACK_FORMAT_DATA:  track.format = "Data"; break;
                    case TRACK_FORMAT_CDI:   track.format = "CD-i"; break;
                    case TRACK_FORMAT_XA:    track.format = "CD-ROM XA"; break;
                    default:                  track.format = "Unknown"; break;
                }

                // Get ISRC
                char isrcBuffer[13] = {0};
                if (mmc_get_isrc(cdio.get(), trackNum, isrcBuffer) == DRIVER_OP_SUCCESS) {
                    track.isrc = QString::fromLatin1(isrcBuffer);
                }

                tracks.append(track);
            }

            return Result<QVector<DiscTrack>>(std::move(tracks));
        }

        static void readCDText(const CdIoHandle& cdio, DiscInfo& info) {
            cdtext_t* cdtext = cdio_get_cdtext(cdio.get());
            if (!cdtext) {
                info.hasCDText = false;
                return;
            }

            info.hasCDText = true;

            const char* albumTitle = cdtext_get(ETITLE, cdtext, 0);
            const char* albumArtist = cdtext_get(EPERFORMER, cdtext, 0);
            const char* albumGenre = cdtext_get(EGENRE, cdtext, 0);

            if (albumTitle) info.title = QString::fromUtf8(albumTitle);
            if (albumArtist) info.artist = QString::fromUtf8(albumArtist);
            if (albumGenre) info.genre = QString::fromUtf8(albumGenre);

            for (int i = 0; i < info.tracks.size(); ++i) {
                const char* trackTitle = cdtext_get(ETITLE, cdtext, i + 1);
                const char* trackArtist = cdtext_get(EPERFORMER, cdtext, i + 1);

                if (trackTitle) info.tracks[i].title = QString::fromUtf8(trackTitle);
                if (trackArtist) info.tracks[i].artist = QString::fromUtf8(trackArtist);
            }
        }

        static QString calculateDiscId(const CdIoHandle& cdio, const DiscInfo& info) {
            if (info.tracks.isEmpty()) return QString();

            QStringList offsets;
            offsets.append(QString::number(info.tracks.first().startFrame + 150));

            for (const auto& track : info.tracks) {
                offsets.append(QString::number(track.startFrame + 150));
            }

            lsn_t leadout = cdio_get_track_lsn(cdio.get(), CDIO_CDROM_LEADOUT_TRACK);
            offsets.append(QString::number(static_cast<int>(leadout + 150)));

            return QString("%1+%2+%3").arg(info.totalTracks)
            .arg(offsets.first())
            .arg(offsets.join("+"));
        }
    };

    // ============================================================================
    // TrackRipper - Synchronous track ripping
    // ============================================================================

    struct RipResult {
        QString outputPath;
        qint64 bytesWritten{0};
        int framesWritten{0};
        int errorsEncountered{0};
    };

    class TrackRipper {
    public:
        struct Options {
            int paranoiaLevel{3};
            QString format{"flac"};
            bool ignoreErrors{false};
            bool synchronous{false};
        };

        static Result<RipResult> rip(const QString& device, int trackNumber,
                                     const QString& outputPath, const Options& options) {
            DiscLogger::info(QString("Ripping track %1 to: %2").arg(trackNumber).arg(outputPath));

            auto driveResult = openDrive(device);
            if (driveResult.isError()) return driveResult.error();
            auto drive = driveResult.takeValue();

            auto paranoiaResult = initParanoia(drive, options.paranoiaLevel);
            if (paranoiaResult.isError()) return paranoiaResult.error();
            auto paranoia = paranoiaResult.takeValue();

            auto sectorsResult = getTrackSectors(drive, trackNumber);
            if (sectorsResult.isError()) return sectorsResult.error();
            auto [start, end] = sectorsResult.value();
            lsn_t totalSectors = end - start;

            auto fileResult = openOutputFile(outputPath, options.format);
            if (fileResult.isError()) return fileResult.error();
            auto outFile = fileResult.takeValue();

            // Seek to start
            if (paranoia_seek(paranoia.get(), start, SEEK_SET) < 0) {
                return Result<RipResult>("Failed to seek to track start");
            }

            // Rip loop
            std::vector<float> floatBuffer(CDIO_CD_FRAMESIZE_RAW * 2);
            lsn_t current = start;
            RipResult result;
            result.outputPath = outputPath;

            while (current <= end) {
                int16_t* rawBuffer = paranoia_read(paranoia.get(), nullptr);
                if (!rawBuffer) {
                    if (!options.ignoreErrors) {
                        return Result<RipResult>(QString("Read error at sector %1").arg(current));
                    }
                    result.errorsEncountered++;
                    continue;
                }

                // Convert to float
                for (int i = 0; i < CDIO_CD_FRAMESIZE_RAW * 2; ++i) {
                    floatBuffer[i] = rawBuffer[i] / 32768.0f;
                }

                // Write
                sf_count_t written = sf_writef_float(outFile.get(), floatBuffer.data(),
                                                     CDIO_CD_FRAMESIZE_RAW);
                if (written != CDIO_CD_FRAMESIZE_RAW) {
                    return Result<RipResult>("Write error at sector " + QString::number(current));
                }

                result.bytesWritten += written * sizeof(float) * 2;
                result.framesWritten += written;
                current++;
            }

            DiscLogger::info(QString("Rip completed: %1 frames, %2 bytes")
            .arg(result.framesWritten).arg(result.bytesWritten));

            return Result<RipResult>(std::move(result));
                                     }

    private:
        static Result<CddaHandle> openDrive(const QString& device) {
            cdrom_drive_t* raw = cdda_identify(device.toUtf8().constData(), 0, nullptr);
            if (!raw) {
                return Result<CddaHandle>("Drive does not support CD-DA extraction");
            }

            if (cdda_open(raw) != 0) {
                cdda_close(raw);
                return Result<CddaHandle>("Failed to open drive for audio extraction");
            }

            cdda_verbose_set(raw, CDDA_MESSAGE_FORGETIT, CDDA_MESSAGE_FORGETIT);
            return Result<CddaHandle>(CddaHandle(raw));
        }

        static Result<ParanoiaHandle> initParanoia(const CddaHandle& drive, int level) {
            cdrom_paranoia_t* raw = paranoia_init(drive.get());
            if (!raw) {
                return Result<ParanoiaHandle>("Failed to initialize paranoia");
            }

            paranoia_mode_t mode;
            switch (level) {
                case 0: mode = PARANOIA_MODE_DISABLE; break;
                case 1: mode = PARANOIA_MODE_OVERLAP; break;
                case 2: mode = PARANOIA_MODE_VERIFY; break;
                default: mode = PARANOIA_MODE_FULL; break;
            }
            paranoia_modeset(raw, mode);

            return Result<ParanoiaHandle>(ParanoiaHandle(raw));
        }

        static Result<std::pair<lsn_t, lsn_t>> getTrackSectors(const CddaHandle& drive, int track) {
            lsn_t start = cdda_track_firstsector(drive.get(), track);
            lsn_t end = cdda_track_lastsector(drive.get(), track);

            if (start < 0 || end < 0) {
                return Result<std::pair<lsn_t, lsn_t>>("Invalid track sectors");
            }

            return Result<std::pair<lsn_t, lsn_t>>({start, end});
        }

        static Result<SndFileHandle> openOutputFile(const QString& path, const QString& format) {
            SF_INFO sfinfo{};
            sfinfo.samplerate = 44100;
            sfinfo.channels = 2;

            if (format == "flac") {
                sfinfo.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
            } else if (format == "wav") {
                sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
            } else if (format == "aiff") {
                sfinfo.format = SF_FORMAT_AIFF | SF_FORMAT_PCM_16;
            } else {
                sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
            }

            SNDFILE* raw = sf_open(path.toUtf8().constData(), SFM_WRITE, &sfinfo);
            if (!raw) {
                return Result<SndFileHandle>("Failed to create output file: " +
                QString(sf_strerror(nullptr)));
            }

            return Result<SndFileHandle>(SndFileHandle(raw));
        }
    };

    // ============================================================================
    // DiscWorker - Thread wrapper for async operations
    // ============================================================================

    class DiscWorker : public QThread {
        Q_OBJECT

    public:
        enum class Task {
            Scan,
            RipTrack,
            RipDisc,
            Verify,
            Eject,
            CloseTray
        };

        DiscWorker(const QString& device, Task task, QObject* parent = nullptr)
        : QThread(parent)
        , m_device(device)
        , m_task(task) {}

        void setTrackNumber(int track) { m_trackNumber = track; }
        void setOutputPath(const QString& path) { m_outputPath = path; }
        void setRipOptions(const TrackRipper::Options& opts) { m_ripOptions = opts; }

        Result<DiscInfo> scanResult() const { return m_scanResult; }
        Result<RipResult> ripResult() const { return m_ripResult; }
        QVector<Result<RipResult>> batchResults() const { return m_batchResults; }

    signals:
        void progress(int percent, const QString& message);
        void trackProgress(int track, int percent);
        void trackCompleted(int track, const QString& path);
        void operationCompleted(bool success, const QString& message);

    protected:
        void run() override {
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
                    default:
                        emit operationCompleted(false, "Task not implemented");
                        break;
                }
            } catch (const std::exception& e) {
                emit operationCompleted(false, QString("Exception: %1").arg(e.what()));
            }
        }

    private:
        void performScan() {
            emit progress(0, "Opening drive...");
            m_scanResult = DiscScanner::scan(m_device);

            if (m_scanResult.isSuccess()) {
                emit progress(100, "Scan completed");
                emit operationCompleted(true, "Scan successful");
            } else {
                emit operationCompleted(false, m_scanResult.error());
            }
        }

        void performRip() {
            emit progress(0, "Starting rip...");

            m_ripResult = TrackRipper::rip(m_device, m_trackNumber,
                                           m_outputPath, m_ripOptions);

            if (m_ripResult.isSuccess()) {
                emit progress(100, "Rip completed");
                emit operationCompleted(true, "Rip successful");
            } else {
                emit operationCompleted(false, m_ripResult.error());
            }
        }

        void performRipDisc() {
            // First scan the disc
            auto scanResult = DiscScanner::scan(m_device);
            if (scanResult.isError()) {
                emit operationCompleted(false, scanResult.error());
                return;
            }

            const auto& info = scanResult.value();
            int totalTracks = 0;
            for (const auto& track : info.tracks) {
                if (track.isAudio) totalTracks++;
            }

            int current = 0;
            m_batchResults.clear();

            for (int i = 0; i < info.tracks.size(); ++i) {
                const auto& track = info.tracks[i];
                if (!track.isAudio) continue;

                current++;
                emit trackProgress(current, 0);

                // Generate output path
                QString filename = QString("%1/%2 - %3.%4")
                .arg(m_outputPath)
                .arg(current, 2, 10, QChar('0'))
                .arg(track.title.isEmpty() ? QString("Track %1").arg(current) : track.title)
                .arg(m_ripOptions.format);

                // Rip track
                auto result = TrackRipper::rip(m_device, i + 1, filename, m_ripOptions);
                m_batchResults.append(result);

                if (result.isSuccess()) {
                    emit trackCompleted(current, filename);
                }

                emit trackProgress(current, 100);
            }

            emit operationCompleted(true, "Disc rip completed");
        }

        QString m_device;
        Task m_task;
        int m_trackNumber{0};
        QString m_outputPath;
        TrackRipper::Options m_ripOptions;

        Result<DiscInfo> m_scanResult{DiscInfo()};
        Result<RipResult> m_ripResult{RipResult()};
        QVector<Result<RipResult>> m_batchResults;
    };

    // ============================================================================
    // DiscRipper Implementation (Main API)
    // ============================================================================

    DiscRipper::DiscRipper(const QString& device, QObject* parent)
    : QObject(parent)
    , m_device(device.isEmpty() ? findDefaultDrive() : device)
    , m_logger("DiscRipper") {

        m_logger.info(QString("Initialized for device: %1").arg(m_device));
    }

    DiscRipper::~DiscRipper() {
        cancel();
    }

    QString DiscRipper::device() const {
        return m_device;
    }

    void DiscRipper::setDevice(const QString& device) {
        if (m_device != device && !device.isEmpty()) {
            m_device = device;
            emit deviceChanged();
        }
    }

    bool DiscRipper::isWorking() const {
        return m_worker && m_worker->isRunning();
    }

    // ================ Synchronous Operations ================

    Result<DiscInfo> DiscRipper::scanDiscSync() {
        if (isWorking()) {
            return Result<DiscInfo>("Another operation is in progress");
        }
        return DiscScanner::scan(m_device);
    }

    Result<RipResult> DiscRipper::ripTrackSync(int trackNumber, const RipOptions& options) {
        if (isWorking()) {
            return Result<RipResult>("Another operation is in progress");
        }

        // Validate track first
        auto scanResult = DiscScanner::scan(m_device);
        if (scanResult.isError()) {
            return scanResult.mapError([](const QString& e) { return e; });
        }

        const auto& info = scanResult.value();
        if (trackNumber < 1 || trackNumber > info.tracks.size()) {
            return Result<RipResult>(QString("Invalid track number: %1").arg(trackNumber));
        }

        if (!info.tracks[trackNumber - 1].isAudio) {
            return Result<RipResult>("Track is not an audio track");
        }

        QString outputPath = options.outputPath;
        if (outputPath.isEmpty()) {
            outputPath = generateOutputPath(info.tracks[trackNumber - 1], options);
        }

        TrackRipper::Options ripOpts;
        ripOpts.paranoiaLevel = options.paranoiaLevel;
        ripOpts.format = options.format;
        ripOpts.ignoreErrors = options.ignoreErrors;

        return TrackRipper::rip(m_device, trackNumber, outputPath, ripOpts);
    }

    // ================ Asynchronous Operations ================

    void DiscRipper::scanDiscAsync() {
        if (isWorking()) {
            emit error("Another operation is in progress");
            return;
        }

        m_worker = new DiscWorker(m_device, DiscWorker::Task::Scan, this);
        setupWorkerConnections();
        m_worker->start();
    }

    void DiscRipper::ripTrackAsync(int trackNumber, const RipOptions& options) {
        if (isWorking()) {
            emit error("Another operation is in progress");
            return;
        }

        // Validate quickly (will be validated again in worker)
        if (trackNumber < 1) {
            emit error("Invalid track number");
            return;
        }

        QString outputPath = options.outputPath;
        if (outputPath.isEmpty()) {
            // We'll generate path in worker after scan
        } else {
            QFileInfo outputInfo(outputPath);
            if (!outputInfo.absoluteDir().exists()) {
                emit error("Output directory does not exist");
                return;
            }
        }

        auto* worker = new DiscWorker(m_device, DiscWorker::Task::RipTrack, this);
        worker->setTrackNumber(trackNumber);
        worker->setOutputPath(outputPath);

        TrackRipper::Options ripOpts;
        ripOpts.paranoiaLevel = options.paranoiaLevel;
        ripOpts.format = options.format;
        ripOpts.ignoreErrors = options.ignoreErrors;
        worker->setRipOptions(ripOpts);

        m_worker = worker;
        setupWorkerConnections();
        m_worker->start();
    }

    void DiscRipper::ripDiscAsync(const RipOptions& options) {
        if (isWorking()) {
            emit error("Another operation is in progress");
            return;
        }

        QDir dir(options.outputDir);
        if (!dir.exists() && !dir.mkpath(".")) {
            emit error("Failed to create output directory");
            return;
        }

        auto* worker = new DiscWorker(m_device, DiscWorker::Task::RipDisc, this);
        worker->setOutputPath(options.outputDir);

        TrackRipper::Options ripOpts;
        ripOpts.paranoiaLevel = options.paranoiaLevel;
        ripOpts.format = options.format;
        ripOpts.ignoreErrors = options.ignoreErrors;
        worker->setRipOptions(ripOpts);

        m_worker = worker;
        setupWorkerConnections();
        m_worker->start();
    }

    void DiscRipper::cancel() {
        if (m_worker && m_worker->isRunning()) {
            m_logger.info("Cancelling operation...");
            m_worker->requestInterruption();

            // Give it time to cancel gracefully
            if (!m_worker->wait(5000)) {
                m_logger.warning("Worker not responding, terminating");
                m_worker->terminate();
                m_worker->wait(1000);
            }

            cleanupWorker();
            emit operationCancelled();
        }
    }

    // ================ Drive Control ================

    bool DiscRipper::eject() {
        #ifdef __linux__
        int fd = open(m_device.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            bool success = (ioctl(fd, CDROMEJECT) == 0);
            close(fd);

            if (success) {
                m_logger.info("Disc ejected");
                emit discEjected();
                return true;
            }
        }
        emit error("Failed to eject disc");
        return false;
        #else
        emit error("Eject not supported on this platform");
        return false;
        #endif
    }

    bool DiscRipper::closeTray() {
        #ifdef __linux__
        int fd = open(m_device.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            bool success = (ioctl(fd, CDROMCLOSETRAY) == 0);
            close(fd);

            if (success) {
                m_logger.info("Tray closed");
                emit trayClosed();
                // Auto-scan after a delay
                QTimer::singleShot(2000, this, &DiscRipper::scanDiscAsync);
                return true;
            }
        }
        emit error("Failed to close tray");
        return false;
        #else
        emit error("Tray control not supported on this platform");
        return false;
        #endif
    }

    // ================ Metadata ================

    void DiscRipper::fetchMusicBrainzMetadata(const QString& discId) {
        QString idToUse = discId.isEmpty() ? m_lastDiscId : discId;

        if (idToUse.isEmpty()) {
            emit error("No disc ID available for lookup");
            return;
        }

        QNetworkAccessManager* nam = new QNetworkAccessManager(this);
        QString url = QString("https://musicbrainz.org/ws/2/discid/%1?fmt=json").arg(idToUse);

        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "AegisMediaPlayer/2.0 (https://github.com/aegis)");

        QNetworkReply* reply = nam->get(request);

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

    // ================ Utility Methods ================

    QStringList DiscRipper::enumerateDrives() {
        QStringList devices;

        // Common device nodes
        QStringList candidates = {
            "/dev/sr0", "/dev/sr1", "/dev/sr2",
            "/dev/cdrom", "/dev/cdrw", "/dev/dvd",
            "/dev/cdrom0", "/dev/cdrom1"
        };

        for (const QString& candidate : candidates) {
            if (QFile::exists(candidate)) {
                devices.append(candidate);
            }
        }

        // Try libcdio enumeration
        char** drives = cdio_get_devices(DRIVER_DEVICE);
        if (drives) {
            for (int i = 0; drives[i]; ++i) {
                QString device = QString::fromUtf8(drives[i]);
                if (!devices.contains(device)) {
                    devices.append(device);
                }
            }
            cdio_free_device_list(drives);
        }

        return devices;
    }

    QString DiscRipper::findDefaultDrive() const {
        auto drives = enumerateDrives();
        return drives.isEmpty() ? QString() : drives.first();
    }

    // ================ Private Methods ================

    void DiscRipper::setupWorkerConnections() {
        if (!m_worker) return;

        connect(m_worker, &DiscWorker::progress, this, &DiscRipper::progress);
        connect(m_worker, &DiscWorker::trackProgress, this, &DiscRipper::trackProgress);
        connect(m_worker, &DiscWorker::trackCompleted, this, &DiscRipper::trackCompleted);

        connect(m_worker, &DiscWorker::operationCompleted, this, [this](bool success, const QString& message) {
            if (auto* worker = qobject_cast<DiscWorker*>(sender())) {
                if (success) {
                    if (worker->scanResult().isSuccess()) {
                        m_lastScanInfo = worker->scanResult().value();
                        m_lastDiscId = m_lastScanInfo.discId;
                        emit scanCompleted(m_lastScanInfo);
                    } else if (worker->ripResult().isSuccess()) {
                        emit ripCompleted(worker->ripResult().value());
                    } else if (!worker->batchResults().isEmpty()) {
                        emit discRipCompleted();
                    }
                } else {
                    emit error(message);
                }
            }
            cleanupWorker();
        });

        connect(m_worker, &DiscWorker::finished, this, [this]() {
            emit workingChanged(false);
        });

        emit workingChanged(true);
    }

    void DiscRipper::cleanupWorker() {
        if (m_worker) {
            m_worker->deleteLater();
            m_worker = nullptr;
            emit workingChanged(false);
        }
    }

    QString DiscRipper::generateOutputPath(const DiscTrack& track, const RipOptions& options) const {
        QString baseDir = options.outputDir;
        if (baseDir.isEmpty()) {
            baseDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
            + "/Aegis/Rips";
        }

        QString artist = track.artist.isEmpty() ? "Unknown Artist" : track.artist;
        QString title = track.title.isEmpty()
        ? QString("Track %1").arg(track.number, 2, 10, QChar('0'))
        : track.title;

        // Sanitize filenames
        artist = sanitizeFilename(artist);
        title = sanitizeFilename(title);

        QString filename = QString("%1 - %2.%3")
        .arg(artist, title, options.format);

        return baseDir + "/" + filename;
    }

    QString DiscRipper::sanitizeFilename(const QString& name) const {
        QString result = name;
        QList<QChar> forbidden = {'/', '\\', ':', '*', '?', '"', '<', '>', '|'};
        for (QChar c : forbidden) {
            result.replace(c, '_');
        }
        return result;
    }

    // ============================================================================
    // Disc Controller (Legacy Compatibility Layer)
    // ============================================================================

    Disc::Disc(const QString& device, QObject* parent)
    : QObject(parent)
    , m_ripper(std::make_unique<DiscRipper>(device, this)) {

        connect(m_ripper.get(), &DiscRipper::scanCompleted, this, &Disc::onScanCompleted);
        connect(m_ripper.get(), &DiscRipper::ripCompleted, this, &Disc::onRipCompleted);
        connect(m_ripper.get(), &DiscRipper::progress, this, &Disc::operationProgress);
        connect(m_ripper.get(), &DiscRipper::trackProgress, this, &Disc::ripProgress);
        connect(m_ripper.get(), &DiscRipper::error, this, &Disc::error);
        connect(m_ripper.get(), &DiscRipper::workingChanged, this, &Disc::workingChanged);
    }

    Disc::~Disc() = default;

    void Disc::scanDisc() {
        m_ripper->scanDiscAsync();
    }

    void Disc::ripTrack(int trackNumber, const QString& outputPath,
                        int paranoiaLevel, const QString& format) {
        DiscRipper::RipOptions opts;
        opts.outputPath = outputPath;
        opts.paranoiaLevel = paranoiaLevel;
        opts.format = format;
        m_ripper->ripTrackAsync(trackNumber, opts);
                        }

                        void Disc::ripWholeDisc(const QString& outputDir, const QString& format,
                                                int paranoiaLevel, bool createCueSheet) {
                            Q_UNUSED(createCueSheet)

                            DiscRipper::RipOptions opts;
                            opts.outputDir = outputDir;
                            opts.paranoiaLevel = paranoiaLevel;
                            opts.format = format;
                            m_ripper->ripDiscAsync(opts);
                                                }

                                                void Disc::cancelOperation() {
                                                    m_ripper->cancel();
                                                }

                                                void Disc::eject() {
                                                    m_ripper->eject();
                                                }

                                                void Disc::closeTray() {
                                                    m_ripper->closeTray();
                                                }

                                                void Disc::fetchMetadataFromMusicBrainz() {
                                                    m_ripper->fetchMusicBrainzMetadata(m_info.discId);
                                                }

                                                void Disc::onScanCompleted(const DiscInfo& info) {
                                                    m_info = info;
                                                    emit discChanged();
                                                }

                                                void Disc::onRipCompleted(const RipResult& result) {
                                                    Q_UNUSED(result)
                                                    emit operationProgress("Ripping completed successfully", 100);
                                                }

                                                QString Disc::device() const {
                                                    return m_ripper->device();
                                                }

                                                void Disc::setDevice(const QString& device) {
                                                    m_ripper->setDevice(device);
                                                    emit deviceChanged();
                                                }

                                                bool Disc::isAudioCD() const {
                                                    return m_info.isAudioCD();
                                                }

                                                bool Disc::isDVDVideo() const {
                                                    return m_info.isVideoDVD();
                                                }

                                                bool Disc::isBluRay() const {
                                                    return m_info.isBluRay();
                                                }

                                                bool Disc::working() const {
                                                    return m_ripper->isWorking();
                                                }

                                                QString Disc::discType() const {
                                                    return m_info.discTypeString();
                                                }

                                                int Disc::trackCount() const {
                                                    return m_info.totalTracks;
                                                }

                                                QString Disc::discLabel() const {
                                                    if (!m_info.title.isEmpty()) {
                                                        return m_info.title;
                                                    }
                                                    if (!m_info.artist.isEmpty()) {
                                                        return QString("%1 - Unknown Album").arg(m_info.artist);
                                                    }
                                                    return QString("Unknown Disc (%1 tracks)").arg(m_info.totalTracks);
                                                }

                                                QVariantList Disc::tracks() const {
                                                    QVariantList list;
                                                    for (const auto& track : m_info.tracks) {
                                                        QVariantMap map;
                                                        map["number"] = track.number;
                                                        map["title"] = track.title;
                                                        map["artist"] = track.artist;
                                                        map["duration"] = track.duration;
                                                        map["isAudio"] = track.isAudio;
                                                        map["isrc"] = track.isrc;
                                                        list.append(map);
                                                    }
                                                    return list;
                                                }

} // namespace Aegis

#include "disc.moc"
