// disc.h - Optical disc (CD/DVD/Blu-ray) access and management
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QVariantList>
#include <QThread>
#include <atomic>
#include <memory>

// Forward declarations for libcdio types
typedef struct CdIo_s CdIo_t;
typedef struct cdrom_drive cdrom_drive_t;

namespace Aegis {

    /**
     * @brief Optical disc track information structure
     *
     * Contains metadata for a single track on an optical disc.
     * Used for both audio CDs and data discs.
     */
    struct DiscTrack {
        int number{0};              ///< Track number (1-based)
        int startFrame{0};          ///< Starting LBA frame number
        int endFrame{0};            ///< Ending LBA frame number
        int duration{0};            ///< Duration in seconds
        QString title;              ///< Track title from CD-TEXT
        QString artist;             ///< Track artist from CD-TEXT
        QString isrc;               ///< International Standard Recording Code
        bool isAudio{false};        ///< True if audio track
        bool isData{false};         ///< True if data track
        QString format;             ///< Track format description
        int channels{2};            ///< Audio channel count (2 for stereo)
        int sampleRate{44100};      ///< Audio sample rate in Hz
    };

    /**
     * @brief Complete disc information structure
     *
     * Contains metadata for an entire optical disc including all tracks.
     * Populated by DiscWorker during scanning operations.
     */
    struct DiscInfo {
        QString discId;             ///< Unique disc identifier (MusicBrainz compatible)
        QString artist;             ///< Primary artist from CD-TEXT
        QString title;              ///< Disc title from CD-TEXT
        QString genre;              ///< Music genre
        int year{0};                ///< Release year
        int totalTracks{0};         ///< Number of tracks on disc
        QVector<DiscTrack> tracks;  ///< List of all tracks
        int ripQuality{3};          ///< Default ripping quality (0-3)
        QString mcn;                ///< Media Catalog Number
        QString upc;                ///< Universal Product Code
        discmode_t discType;        ///< Disc type (CD-DA, DVD, Blu-ray, etc.)
        bool hasCDText{false};      ///< True if disc contains CD-TEXT data
        bool isCopyProtected{false}; ///< True if disc has copy protection
    };

    /**
     * @brief Background worker for disc operations
     *
     * Performs long-running disc operations in a separate thread:
     * - Scanning discs for metadata
     * - Ripping audio tracks with error correction
     * - Verifying ripped data integrity
     *
     * All operations support cancellation and progress reporting.
     */
    class DiscWorker : public QThread {
        Q_OBJECT

    public:
        /**
         * @brief Disc operation types
         */
        enum class Task {
            Scan,           ///< Scan disc for metadata
            RipTrack,       ///< Rip single audio track
            RipDisc,        ///< Rip entire disc
            VerifyRip,      ///< Verify ripped data integrity
            Eject,          ///< Eject disc from drive
            CloseTray       ///< Close drive tray
        };

        /**
         * @brief Construct disc worker for specific operation
         * @param device Optical drive device path (e.g., "/dev/sr0")
         * @param task Operation to perform
         * @param trackNumber Track number for track-specific operations
         * @param outputPath Output file path for ripping operations
         * @param paranoiaLevel Error correction level (0-3)
         * @param parent Parent QObject
         */
        explicit DiscWorker(const QString &device, Task task,
                            int trackNumber = 0, const QString &outputPath = QString(),
                            int paranoiaLevel = 3, QObject *parent = nullptr);

        /**
         * @brief Destructor with thread safety
         */
        ~DiscWorker() override;

        /**
         * @brief Request operation cancellation
         *
         * Sets stop flag and waits for graceful thread termination.
         * Safe to call from any thread.
         */
        void stopSafely();

    signals:
        /**
         * @brief Emitted when disc scanning completes
         * @param success True if scan succeeded
         * @param info Disc information structure (valid only if success=true)
         */
        void scanCompleted(bool success, const DiscInfo &info);

        /**
         * @brief Emitted during ripping operations
         * @param percent Completion percentage (0-100)
         */
        void ripProgress(int percent);

        /**
         * @brief Emitted when track ripping completes
         * @param success True if rip succeeded
         * @param filePath Path to ripped file (valid only if success=true)
         */
        void ripCompleted(bool success, const QString &filePath);

        /**
         * @brief Emitted for general operation progress
         * @param message Progress description
         * @param percent Completion percentage
         */
        void operationProgress(const QString &message, int percent);

        /**
         * @brief Emitted for operation status updates
         * @param status Status message
         */
        void statusChanged(const QString &status);

        /**
         * @brief Emitted when errors occur
         * @param errorMessage Error description
         */
        void error(const QString &errorMessage);

    protected:
        /**
         * @brief Main thread execution function
         *
         * Overrides QThread::run() to perform the requested disc operation.
         * Automatically called when thread starts.
         */
        void run() override;

    private:
        // ================ Operation Implementations ================

        /**
         * @brief Perform disc scanning operation
         * @return True if scan succeeded
         */
        bool performScan();

        /**
         * @brief Perform track ripping operation
         * @return True if rip succeeded
         */
        bool performRip();

        /**
         * @brief Perform complete disc ripping
         * @return True if all tracks ripped successfully
         */
        bool performRipDisc();

        /**
         * @brief Verify ripped data integrity
         * @return True if verification passed
         */
        bool performVerify();

        // ================ Metadata Extraction Methods ================

        /**
         * @brief Read disc metadata from libcdio
         * @param cdio CD I/O handle
         * @return Disc information structure
         */
        DiscInfo readDiscInfo(CdIo_t *cdio);

        /**
         * @brief Extract CD-TEXT metadata if available
         * @param cdio CD I/O handle
         * @param info Disc info structure to populate
         */
        void readCDText(CdIo_t *cdio, DiscInfo &info);

        /**
         * @brief Calculate MusicBrainz-compatible disc ID
         * @param cdio CD I/O handle
         * @param info Disc information
         * @return Disc ID string
         */
        QString calculateDiscId(CdIo_t *cdio, const DiscInfo &info);

        // ================ Member Variables ================

        QString m_device;            ///< Optical drive device path
        Task m_task;                 ///< Requested operation type
        int m_trackNumber;           ///< Track number for track operations
        QString m_outputPath;        ///< Output file/directory path
        int m_paranoiaLevel;         ///< Error correction level (0-3)
        std::atomic<bool> m_stop{false}; ///< Operation cancellation flag

        DiscInfo m_cachedInfo;       ///< Cached disc info from last scan
    };

    /**
     * @brief Optical disc management controller
     *
     * Provides high-level interface for disc operations with QML integration:
     * - Disc detection and metadata scanning
     * - Audio track playback and ripping
     * - Drive control (eject, close)
     * - DVD/Blu-ray support
     * - MusicBrainz metadata lookup
     *
     * All operations are asynchronous with progress reporting.
     */
    class Disc : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool discPresent READ discPresent NOTIFY discChanged)
        Q_PROPERTY(bool working READ working NOTIFY workingChanged)
        Q_PROPERTY(QString discLabel READ discLabel NOTIFY discChanged)
        Q_PROPERTY(int trackCount READ trackCount NOTIFY discChanged)
        Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY deviceChanged)
        Q_PROPERTY(bool isAudioCD READ isAudioCD NOTIFY discChanged)
        Q_PROPERTY(bool isDVDVideo READ isDVDVideo NOTIFY discChanged)
        Q_PROPERTY(bool isBluRay READ isBluRay NOTIFY discChanged)

    public:
        /**
         * @brief Construct disc controller for optical drive
         * @param device Optical drive device path
         * @param parent Parent QObject
         */
        explicit Disc(const QString &device = QString(), QObject *parent = nullptr);

        /**
         * @brief Destructor with operation cancellation
         */
        ~Disc();

        // ================ Property Getters ================

        bool discPresent() const;                ///< True if disc is inserted
        bool working() const;                    ///< True if operation in progress
        QString discLabel() const;               ///< Disc title or default label
        int trackCount() const;                  ///< Number of tracks on disc
        QString device() const;                  ///< Optical drive device path
        bool isAudioCD() const;                  ///< True if disc is audio CD
        bool isDVDVideo() const;                 ///< True if disc is DVD-Video
        bool isBluRay() const;                   ///< True if disc is Blu-ray

        // ================ Drive Control Methods ================

        /**
         * @brief Set optical drive device path
         * @param device Device path (e.g., "/dev/sr0")
         */
        Q_INVOKABLE void setDevice(const QString &device);

        /**
         * @brief Scan disc for metadata
         *
         * Asynchronous operation. Emits discChanged() when complete.
         */
        Q_INVOKABLE void scanDisc();

        /**
         * @brief Eject disc from drive
         *
         * Physically ejects the disc tray and clears cached metadata.
         */
        Q_INVOKABLE void eject();

        /**
         * @brief Close drive tray
         *
         * Closes tray and automatically scans disc if present.
         */
        Q_INVOKABLE void closeTray();

        // ================ Playback Methods ================

        /**
         * @brief Play specific audio track
         * @param trackNumber Track number (1-based)
         *
         * Emits playRequested() signal with cdda:// URL.
         */
        Q_INVOKABLE void playTrack(int trackNumber);

        /**
         * @brief Play DVD title/chapter
         * @param titleNumber Title number (usually 1)
         * @param chapterNumber Chapter number (0 for all chapters)
         */
        Q_INVOKABLE void playDVDTitle(int titleNumber = 1, int chapterNumber = 0);

        // ================ Ripping Methods ================

        /**
         * @brief Rip single audio track to file
         * @param trackNumber Track number to rip (1-based)
         * @param outputPath Destination file path
         * @param paranoiaLevel Error correction level (0-3)
         * @param format Output format ("wav", "flac", "mp3", "ogg")
         */
        Q_INVOKABLE void ripTrack(int trackNumber, const QString &outputPath,
                                  int paranoiaLevel = 3, const QString &format = "flac");

        /**
         * @brief Rip entire disc to directory
         * @param outputDir Destination directory
         * @param format Output format ("wav", "flac", "mp3", "ogg")
         * @param paranoiaLevel Error correction level (0-3)
         * @param createCueSheet True to generate .cue sheet file
         */
        Q_INVOKABLE void ripWholeDisc(const QString &outputDir, const QString &format = "flac",
                                      int paranoiaLevel = 3, bool createCueSheet = true);

        /**
         * @brief Cancel current operation
         *
         * Requests cancellation of scanning, ripping, or verification.
         */
        Q_INVOKABLE void cancelOperation();

        // ================ Metadata Methods ================

        /**
         * @brief Fetch metadata from MusicBrainz database
         *
         * Requires internet connection. Updates disc info with online metadata.
         */
        Q_INVOKABLE void fetchMetadataFromMusicBrainz();

        /**
         * @brief Get complete disc information
         * @return Disc information structure
         */
        Q_INVOKABLE DiscInfo discInfo() const { return m_info; }

        /**
         * @brief Get track list in QML-compatible format
         * @return List of track information maps
         */
        Q_INVOKABLE QVariantList tracks() const;

        /**
         * @brief Get DVD/Blu-ray titles and chapters
         * @return List of title information
         */
        Q_INVOKABLE QVariantList discTitles() const;

        /**
         * @brief Get disc type as human-readable string
         * @return Disc type description
         */
        Q_INVOKABLE QString discTypeString() const;

        /**
         * @brief Check if drive supports a specific feature
         * @param feature Feature name ("cdda", "dvd", "bluray", "burning")
         * @return True if feature is supported
         */
        Q_INVOKABLE bool driveSupports(const QString &feature) const;

    signals:
        /**
         * @brief Emitted when disc presence or metadata changes
         */
        void discChanged();

        /**
         * @brief Emitted when operation state changes
         */
        void workingChanged();

        /**
         * @brief Emitted when device path changes
         */
        void deviceChanged();

        /**
         * @brief Emitted during ripping operations
         * @param track Current track number
         * @param percent Completion percentage
         */
        void ripProgress(int track, int percent);

        /**
         * @brief Emitted for playback requests
         * @param url Media URL (cdda://, dvd://, bluray://)
         */
        void playRequested(const QString &url);

        /**
         * @brief Emitted for operation progress
         * @param message Progress description
         * @param percent Completion percentage
         */
        void operationProgress(const QString &message, int percent);

        /**
         * @brief Emitted when errors occur
         * @param message Error description
         */
        void error(const QString &message);

        /**
         * @brief Emitted when metadata is fetched from MusicBrainz
         * @param success True if metadata was retrieved
         */
        void musicBrainzMetadataFetched(bool success);

    private slots:
        /**
         * @brief Handle disc scan completion
         * @param success True if scan succeeded
         * @param info Disc information
         */
        void onScanCompleted(bool success, const DiscInfo &info);

        /**
         * @brief Handle track rip completion
         * @param success True if rip succeeded
         * @param filePath Path to ripped file
         */
        void onRipCompleted(bool success, const QString &filePath);

    private:
        // ================ Member Variables ================

        QString m_device;            ///< Optical drive device path
        DiscInfo m_info;             ///< Cached disc information
        DiscWorker *m_worker{nullptr}; ///< Background worker thread

        // ================ Private Helper Methods ================

        /**
         * @brief Detect optical drive capabilities
         * @return Bitmask of supported features
         */
        int detectDriveCapabilities() const;

        /**
         * @brief Find default optical drive if none specified
         * @return Device path or empty string
         */
        QString findDefaultDrive() const;

        /**
         * @brief Check if file path is writable
         * @param path File or directory path
         * @return True if path is writable
         */
        bool isPathWritable(const QString &path) const;

        /**
         * @brief Generate output filename for track
         * @param trackNumber Track number
         * @param format File format extension
         * @return Suggested filename
         */
        QString generateTrackFilename(int trackNumber, const QString &format) const;

        /**
         * @brief Clean up worker thread resources
         */
        void cleanupWorker();

        /**
         * @brief Update disc information cache
         * @param info New disc information
         */
        void updateDiscInfo(const DiscInfo &info);
    };

} // namespace Aegis
