// discburner.h
// Optical disc burning interface for Aegis Multimedia Suite
// Provides high-level API for CD/DVD/Blu-ray burning operations
// Design: Thread-safe, QML-friendly, with comprehensive media support

#pragma once

#include <QObject>
#include <QThread>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <memory>

// Forward declarations to minimize header exposure and reduce dependencies
struct burn_drive_info;
struct burn_drive;
struct burn_progress;

namespace Aegis {

    /**
     * @brief Types of disc burning operations supported
     *
     * Defines the various media formats and disc types that can be created.
     * Each type has specific requirements and restrictions.
     */
    enum class BurnType {
        AudioCD,        ///< Red Book audio CD (CD-DA) - standard audio CDs
        DataCD,         ///< ISO9660/Joliet filesystem CD-ROM
        DVDVideo,       ///< DVD-Video with VIDEO_TS structure
        ISOImage,       ///< Burn existing ISO image to disc
        MixedMode,      ///< CD-Extra (mixed audio/data)
        DVDData,        ///< UDF/ISO9660 DVD data disc
        BluRayData,     ///< Blu-ray data disc (UDF 2.5/2.6)
        BootableCD,     ///< Bootable CD (El Torito)
        BootableDVD     ///< Bootable DVD
    };

    /**
     * @brief Burning speed presets for quality control
     *
     * Higher speeds may reduce burn quality but save time.
     * "Auto" lets the drive choose optimal speed based on media.
     */
    enum class BurnSpeed {
        Auto,           ///< Drive default optimal speed
        Slow,           ///< 1x-4x for maximum quality (audio CDs, important data)
        Normal,         ///< 8x-16x balanced speed/quality
        Fast,           ///< 24x-52x for data where quality is less critical
        Max             ///< Drive maximum speed (not recommended for important burns)
    };

    /**
     * @brief Configuration for a burning job
     *
     * Contains all parameters needed to execute a burn operation.
     * Passed to BurnWorker for processing.
     */
    struct BurnJob {
        BurnType type = BurnType::DataCD;  ///< Type of disc to create
        QString device = "/dev/sr0";       ///< Target drive device path

        // Source data specifications
        QStringList sourceFiles;           ///< Files to burn (for data discs)
        QList<int> trackIds;               ///< Track IDs from library (for audio CDs)
        QString isoPath;                   ///< Path to ISO image (for ISO burning)

        // Disc metadata
        QString volumeLabel = "Aegis";     ///< Disc label (shown in file managers)
        QString albumTitle;                ///< Album title (for audio CDs)
        QString artist;                    ///< Artist name (for audio CDs)

        // Quality and verification options
        bool verify = false;               ///< Verify written data after burn
        bool ejectAfter = false;           ///< Eject disc after successful burn
        bool dummyMode = false;            ///< Test burn without actually writing
        bool closeSession = true;          ///< Close disc session after writing

        // Speed control
        BurnSpeed speed = BurnSpeed::Auto; ///< Burning speed preset

        // Audio CD specific
        int gapBetweenTracks = 2;          ///< Gap between audio tracks in seconds
        bool useCDText = false;            ///< Embed CD-TEXT information

        // Data disc specific
        bool useJoliet = true;             ///< Use Joliet extensions for long filenames
        bool useRockRidge = false;         ///< Use Rock Ridge Unix extensions
        bool allowDeepPaths = false;       ///< Allow paths deeper than 8 directories

        // Constructor with sensible defaults
        BurnJob() = default;

        // Helper method to validate job parameters
        bool isValid() const;
    };

    /**
     * @brief Hardware capabilities of an optical drive
     *
     * Queried from the drive to determine what media types it supports.
     * Used to enable/disable UI options based on hardware.
     */
    struct BurnerCapabilities {
        // Read capabilities
        bool canRead = false;              ///< Can read optical media

        // Write capabilities by media type
        bool canWriteCDR = false;          ///< Can write to CD-R media
        bool canWriteCDRW = false;         ///< Can write to CD-RW media
        bool canWriteDVDR = false;         ///< Can write to DVD-R/RW
        bool canWriteDVDRAM = false;       ///< Can write to DVD-RAM
        bool canWriteDVDPlusR = false;     ///< Can write to DVD+R/RW
        bool canWriteDVDPlusRWDL = false;  ///< Can write to DVD+RW DL (dual layer)
        bool canWriteDVDMinusR = false;    ///< Can write to DVD-R (historical)
        bool canWriteBD = false;           ///< Can write to Blu-ray discs
        bool canWriteBDR = false;          ///< Can write to BD-R
        bool canWriteBDRE = false;         ///< Can write to BD-RE (rewritable)

        // Maximum write speeds in KB/s (0 = unknown)
        int maxSpeedCD = 0;                ///< Maximum CD writing speed
        int maxSpeedDVD = 0;               ///< Maximum DVD writing speed
        int maxSpeedBD = 0;                ///< Maximum Blu-ray writing speed

        // Additional features
        bool supportsBurnProof = false;    ///< Supports buffer underrun protection
        bool supportsSolidBurn = false;    ///< Supports solid burn (Plextor)
        bool supportsMountRainier = false; ///< Supports MRW formatting
        bool supportsDVDPlusRW = false;    ///< Supports DVD+RW background formatting

        // Helper method to check if drive can write any media
        bool canWrite() const {
            return canWriteCDR || canWriteCDRW || canWriteDVDR ||
            canWriteDVDPlusR || canWriteBD || canWriteBDR;
        }

        // Helper to get human-readable description
        QString description() const;
    };

    /**
     * @brief Worker thread for burning operations
     *
     * Runs burning operations in background thread to keep UI responsive.
     * Uses libburn for low-level burning operations.
     * Emits progress signals during operation.
     */
    class BurnWorker : public QThread {
        Q_OBJECT

    public:
        /**
         * @brief Construct a burn worker with specified job
         * @param job BurnJob configuration
         * @param parent Parent QObject
         */
        explicit BurnWorker(const BurnJob &job, QObject *parent = nullptr);

        /**
         * @brief Main execution method called by QThread
         *
         * Performs the actual burning operation based on job type.
         * Calls appropriate burn method (audio, data, ISO, etc.)
         */
        void run() override;

        /**
         * @brief Request cancellation of current burn operation
         *
         * Safe to call from any thread. Sets cancellation flag
         * that will be checked during burn process.
         */
        void cancel();

    signals:
        /**
         * @brief Emitted when burning operation completes
         * @param success True if burn was successful
         * @param message Human-readable status message
         */
        void burnCompleted(bool success, const QString &message);

        /**
         * @brief Emitted during burning to report progress
         * @param percent Completion percentage (0-100)
         * @param status Current operation status description
         */
        void progress(int percent, const QString &status);

        /**
         * @brief Emitted when buffer underrun protection state changes
         * @param active True if buffer underrun protection is active
         */
        void bufferUnderrunProtection(bool active);

        /**
         * @brief Emitted when media is inserted/removed during operation
         * @param mediaPresent True if media is present in drive
         */
        void mediaStatusChanged(bool mediaPresent);

        /**
         * @brief Emitted for detailed logging
         * @param message Log message
         * @param isError True if message indicates an error
         */
        void logMessage(const QString &message, bool isError = false);

    private:
        /**
         * @brief Initialize and acquire exclusive access to drive
         * @return True if drive setup succeeded
         */
        bool setupDrive();

        /**
         * @brief Burn an audio CD from track sources
         * @return True if burn succeeded
         */
        bool burnAudioCD();

        /**
         * @brief Burn a data CD/DVD from file list
         * @return True if burn succeeded
         */
        bool burnDataCD();

        /**
         * @brief Burn an ISO image to disc
         * @return True if burn succeeded
         */
        bool burnISO();

        /**
         * @brief Burn a DVD-Video structure
         * @return True if burn succeeded
         */
        bool burnDVDVideo();

        /**
         * @brief Convert BurnSpeed enum to drive multiplier
         * @param speed Speed preset
         * @param isDVD True if burning DVD (different speed scale)
         * @return Speed multiplier (1x = 176 KB/s for CD, 1385 KB/s for DVD)
         */
        int speedToMultiplier(BurnSpeed speed, bool isDVD);

        /**
         * @brief Update progress from libburn callback
         * @param p libburn progress structure
         */
        void updateProgress(struct burn_progress p);

        // Private data members
        BurnJob m_job;                      ///< Burning job configuration
        std::atomic<bool> m_cancel{false};  ///< Cancellation flag
        struct burn_drive *m_drive = nullptr; ///< libburn drive handle
        QString m_tempDirectory;            ///< Temporary directory for image creation
    };

    /**
     * @brief Main burning controller class
     *
     * Provides QML-friendly API for disc burning operations.
     * Manages worker threads, drive enumeration, and capabilities querying.
     * Thread-safe for concurrent access from UI.
     */
    class CDBurner : public QObject {
        Q_OBJECT

        // QML properties
        Q_PROPERTY(bool burning READ burning NOTIFY burningChanged)
        Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
        Q_PROPERTY(QString currentDevice READ currentDevice NOTIFY deviceChanged)
        Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
        Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
        Q_PROPERTY(QStringList availableDrives READ availableDrives NOTIFY drivesChanged)

    public:
        /**
         * @brief Construct a CDBurner instance
         * @param parent Parent QObject
         */
        explicit CDBurner(QObject *parent = nullptr);

        /**
         * @brief Destructor - ensures cleanup of resources
         */
        ~CDBurner();

        // Static utility methods
        /**
         * @brief Enumerate all optical drives in the system
         * @return List of device paths (e.g., ["/dev/sr0", "/dev/sr1"])
         */
        static QStringList enumerateDrives();

        /**
         * @brief Query capabilities of a specific optical drive
         * @param device Device path (e.g., "/dev/sr0")
         * @return BurnerCapabilities structure
         */
        static BurnerCapabilities getCapabilities(const QString &device);

        // Property getters
        bool burning() const { return m_worker && m_worker->isRunning(); }
        bool ready() const { return !burning(); }
        QString currentDevice() const { return m_currentDevice; }
        int progress() const { return m_progress; }
        QString statusMessage() const { return m_statusMessage; }
        QStringList availableDrives() const { return m_availableDrives; }

        // Audio CD burning
        /**
         * @brief Burn audio CD from library playlist
         * @param playlistName Name of playlist in library
         * @param device Target drive (default: first available)
         * @param speed Burning speed (default: Auto)
         */
        Q_INVOKABLE void burnAudioFromPlaylist(const QString &playlistName,
                                               const QString &device = "/dev/sr0",
                                               BurnSpeed speed = BurnSpeed::Auto);

        /**
         * @brief Burn audio CD from specific track IDs
         * @param trackIds List of track database IDs
         * @param device Target drive (default: first available)
         * @param albumTitle Album title for CD-TEXT
         */
        Q_INVOKABLE void burnAudioFromTracks(const QVariantList &trackIds,
                                             const QString &device = "/dev/sr0",
                                             const QString &albumTitle = "Audio CD");

        // Data burning
        /**
         * @brief Burn files/folders to data disc
         * @param filePaths List of files/directories to burn
         * @param device Target drive (default: first available)
         * @param volumeLabel Disc label (shown in file manager)
         * @param dvd True to burn as DVD (false for CD)
         */
        Q_INVOKABLE void burnFiles(const QStringList &filePaths,
                                   const QString &device = "/dev/sr0",
                                   const QString &volumeLabel = "Aegis Data",
                                   bool dvd = false);

        // ISO burning
        /**
         * @brief Burn existing ISO image to disc
         * @param isoPath Path to ISO image file
         * @param device Target drive (default: first available)
         * @param verify Verify written data after burn
         */
        Q_INVOKABLE void burnISO(const QString &isoPath,
                                 const QString &device = "/dev/sr0",
                                 bool verify = false);

        // DVD-Video burning
        /**
         * @brief Burn DVD-Video from VIDEO_TS folder
         * @param videoTsFolder Path to VIDEO_TS folder
         * @param device Target drive (default: first available)
         */
        Q_INVOKABLE void burnDVDVideo(const QString &videoTsFolder,
                                      const QString &device = "/dev/sr0");

        // Disc management
        /**
         * @brief Blank (erase) CD-RW media
         * @param device Target drive (default: first available)
         * @param fast True for quick erase, false for full erase
         */
        Q_INVOKABLE void blankCDRW(const QString &device = "/dev/sr0",
                                   bool fast = true);

        /**
         * @brief Format DVD+RW media
         * @param device Target drive (default: first available)
         */
        Q_INVOKABLE void formatDVDPlusRW(const QString &device = "/dev/sr0");

        /**
         * @brief Eject media from drive
         * @param device Target drive (default: first available)
         */
        Q_INVOKABLE void eject(const QString &device = "/dev/sr0");

        /**
         * @brief Close drive tray
         * @param device Target drive (default: first available)
         */
        Q_INVOKABLE void closeTray(const QString &device = "/dev/sr0");

        // Advanced operations
        /**
         * @brief Create ISO image from files without burning
         * @param files List of files/directories to include
         * @param outputPath Path for output ISO file
         * @param volumeLabel ISO volume label
         */
        Q_INVOKABLE void createISOFromFiles(const QStringList &files,
                                            const QString &outputPath,
                                            const QString &volumeLabel = "Aegis");

        /**
         * @brief Cancel current burn operation
         *
         * Safe to call while burning is in progress.
         * Will attempt to stop burning gracefully.
         */
        Q_INVOKABLE void cancelBurn();

        /**
         * @brief Set CD-TEXT information for audio CD
         * @param albumInfo Map of album metadata (title, artist, genre, etc.)
         * @param trackTitles List of track titles (one per track)
         */
        Q_INVOKABLE void setCDText(const QVariantMap &albumInfo,
                                   const QVariantList &trackTitles);

        /**
         * @brief Refresh list of available drives
         *
         * Re-enumerates drives and updates availableDrives property.
         */
        Q_INVOKABLE void refreshDrives();

        /**
         * @brief Check if media is present in drive
         * @param device Drive to check (default: current device)
         * @return True if media is present and readable
         */
        Q_INVOKABLE bool mediaPresent(const QString &device = "");

        /**
         * @brief Get information about media in drive
         * @param device Drive to check (default: current device)
         * @return Map containing media type, capacity, free space, etc.
         */
        Q_INVOKABLE QVariantMap mediaInfo(const QString &device = "");

    signals:
        // Property change signals
        void burningChanged();
        void readyChanged();
        void deviceChanged();
        void progressChanged();
        void statusChanged();
        void drivesChanged();

        // Operation signals
        void burnProgress(int percent, QString status);
        void burnFinished(bool success, QString message);
        void availableSpace(qint64 bytesFree, qint64 bytesTotal);
        void driveEjected(const QString &device);
        void mediaChanged(const QString &device, bool present);
        void errorOccurred(QString errorMessage, int errorCode);

        // Logging signals
        void logInfo(QString message);
        void logWarning(QString message);
        void logError(QString message);

    private slots:
        /**
         * @brief Handle burn completion from worker thread
         * @param success True if burn succeeded
         * @param message Status message
         */
        void onBurnCompleted(bool success, const QString &message);

        /**
         * @brief Handle progress updates from worker thread
         * @param percent Completion percentage
         * @param status Status description
         */
        void onProgress(int percent, const QString &status);

    private:
        /**
         * @brief Initialize libburn library
         *
         * Must be called before any burning operations.
         * Thread-safe via static initialization.
         */
        bool initializeLibburn();
        BurnerCapabilities getCapabilitiesViaLibburn(const QString& device);
        int getMaxWriteSpeed(const QString& device, const QString& mediaType);
        BurnerCapabilities queryUDisks2Capabilities(const QString& device, BurnerCapabilities caps);

        /**
         * @brief Cleanup libburn resources
         */
        void cleanup();

        /**
         * @brief Start a burn operation with specified job
         * @param job Burn job configuration
         */
        void startBurn(const BurnJob &job);

        /**
         * @brief Update internal progress state
         * @param percent New progress percentage
         * @param status New status message
         */
        void updateProgress(int percent, const QString &status);

        // Private data members
        QString m_currentDevice;               ///< Currently selected drive
        BurnWorker *m_worker = nullptr;        ///< Active burn worker thread
        QStringList m_availableDrives;         ///< Cached list of available drives
        int m_progress = 0;                    ///< Current progress percentage
        QString m_statusMessage;               ///< Current status message
        struct burn_drive_info *m_driveList = nullptr;  ///< libburn drive list
        unsigned int m_driveCount = 0;         ///< Number of drives in list
        bool m_libburnInitialized = false;     ///< libburn initialization flag
        QMutex m_operationMutex;               ///< Mutex for thread-safe operations
    };

} // namespace Aegis

// QML type registration
Q_DECLARE_METATYPE(Aegis::BurnType)
Q_DECLARE_METATYPE(Aegis::BurnSpeed)
Q_DECLARE_METATYPE(Aegis::BurnerCapabilities)
