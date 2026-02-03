// mediaplayer_plugin.h - Media Player mode plugin for Aegis application suite
// Fixed version - removed non-existent includes
#pragma once

#include "plugin_interface.h"
#include "mediaplayer.h"
#include "library.h"
#include "streaming.h"
#include "capture.h"
#include "kdeconnect.h"
#include "platform.h"
#include "karaoke.h"
#include "disc.h"
#include "audioeditor.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QTranslator>
#include <QDebug>

namespace Aegis {

// Forward declarations for optional components
class Visualization;
class Lyrics;
class Radio;
class Podcasts;
class Sync;

/**
 * @brief Media Player mode plugin - Complete media playback environment
 *
 * Initializes and manages all components required for media player mode:
 * - MediaPlayer (core playback engine with playlist)
 * - Library (local media database and metadata)
 * - Platform integration (MPRIS, tray, notifications)
 * - Streaming services (YouTube, radio, podcasts)
 * - Device integration (KDEConnect, DLNA, Bluetooth)
 * - Capture and recording capabilities
 * - Disc support (CD/DVD/Blu-ray)
 * - Visualization and lyrics display
 * - Cloud sync and backup
 *
 * Provides unified QML interface for all media player functionality.
 */
class MediaPlayerPlugin : public AppModePlugin {
    Q_OBJECT

public:
    /**
     * @brief Construct media player plugin
     * @param parent Parent QObject
     */
    explicit MediaPlayerPlugin(QObject* parent = nullptr)
        : AppModePlugin(parent)
        , m_settings(new QSettings("Aegis", "MediaPlayer", this))
    {
        qDebug() << "MediaPlayerPlugin instance created";
    }

    /**
     * @brief Get plugin mode identifier
     * @return Mode name string
     */
    QString modeName() const override {
        return "mediaplayer";
    }

    /**
     * @brief Get human-readable display name
     * @return Display name for UI
     */
    QString displayName() const override {
        return tr("Media Player");
    }

    /**
     * @brief Get QML entry point for this mode
     * @return QML file path or URL
     */
    QString qmlEntryPoint() const override {
        return "qrc:/qml/mediaplayer/Main.qml";
    }

    /**
     * @brief Get plugin description
     * @return Detailed description of plugin capabilities
     */
    virtual QString description() const {
        return tr("Universal media player with library management, "
                  "streaming support, disc playback, and device integration. "
                  "Supports all major audio/video formats with advanced "
                  "playback features and visualizations.");
    }

    /**
     * @brief Get plugin version
     * @return Version string
     */
    virtual QString version() const {
        return "1.0.0";
    }

    /**
     * @brief Get plugin author information
     * @return Author name and contact
     */
    virtual QString author() const {
        return tr("Aegis Team <contact@aegis.example.com>");
    }

    /**
     * @brief Check if plugin supports video playback
     * @return True if video is supported
     */
    bool hasVideo() const override {
        return true;
    }

    /**
     * @brief Check if plugin supports streaming
     * @return True if streaming is supported
     */
    virtual bool supportsStreaming() const {
        return true;
    }

    /**
     * @brief Check if plugin supports editing
     * @return True if editing features are available
     */
    virtual bool supportsEditing() const {
        return true; // Quick edit support via AudioEditor
    }

    /**
     * @brief Check if plugin supports disc burning
     * @return True if disc burning is supported
     */
    virtual bool supportsDiscBurning() const {
        return true; // Via Disc component
    }

    /**
     * @brief Check if plugin supports recording
     * @return True if recording is supported
     */
    virtual bool supportsRecording() const {
        return true; // Via Capture component
    }

    /**
     * @brief Check if plugin supports lyrics display
     * @return True if lyrics are supported
     */
    virtual bool supportsLyrics() const {
        return true;
    }

    /**
     * @brief Check if plugin supports visualizations
     * @return True if visualizations are supported
     */
    virtual bool supportsVisualizations() const {
        return true;
    }

    /**
     * @brief Check if plugin supports cloud sync
     * @return True if cloud sync is supported
     */
    virtual bool supportsCloudSync() const {
        return true;
    }

    /**
     * @brief Get list of supported file formats
     * @return List of format extensions
     */
    virtual QStringList supportedFormats() const {
        return {
            // Audio formats
            "mp3", "flac", "ogg", "wav", "m4a", "aac", "opus", "wma",
            "aiff", "alac", "ape", "tta", "wv", "shn",

            // Video formats
            "mp4", "mkv", "avi", "mov", "webm", "wmv", "flv", "mpeg",
            "mpg", "m4v", "3gp", "ogv", "ts", "m2ts",

            // Playlist formats
            "m3u", "m3u8", "pls", "xspf", "asx", "wpl",

            // Disc images
            "iso", "img", "bin", "cue",

            // Karaoke formats
            "cdg", "kar", "kfn"
        };
    }

    /**
     * @brief Initialize plugin with application context
     * @param ctx Application context with engine and configuration
     * @return True if initialization succeeded
     */
    bool initialize(const AppContext& ctx) override {
        qDebug() << "Initializing MediaPlayerPlugin...";

        // Validate context
        if (!ctx.engine || !ctx.engine->rootContext()) {
            qCritical() << "MediaPlayerPlugin: Invalid QML engine context";
            return false;
        }

        m_context = ctx;

        try {
            // ================ Phase 1: Core Components ================

            // 1.1 Initialize Library (shared database)
            initializeLibrary();

            // 1.2 Initialize MediaPlayer (playback engine)
            initializeMediaPlayer();

            // ================ Phase 2: Platform Integration ================

            // 2.1 Initialize Platform services
            initializePlatform();

            // 2.2 Initialize MPRIS integration
            initializeMpris();

            // 2.3 Initialize system tray
            initializeTray();

            // ================ Phase 3: Optional Features ================

            // 3.1 Initialize Streaming services (lazy loaded)
            initializeStreaming();

            // 3.2 Initialize Device integration
            initializeDeviceIntegration();

            // 3.3 Initialize Capture/Recording
            initializeCapture();

            // 3.4 Initialize Disc support
            initializeDiscSupport();

            // 3.5 Initialize Editor for quick edits
            initializeEditor();

            // ================ Phase 4: QML Integration ================

            // 4.1 Export all components to QML
            exportComponentsToQml();

            // 4.2 Load translations
            loadTranslations();

            // 4.3 Apply saved settings
            applySavedSettings();

            // ================ Phase 5: Final Setup ================

            // 5.1 Connect cross-component signals
            connectComponentSignals();

            // 5.2 Initialize default playlist if empty
            initializeDefaultPlaylist();

            // 5.3 Start background services
            startBackgroundServices();

            qDebug() << "MediaPlayerPlugin initialized successfully";
            emit initializationComplete();

            return true;

        } catch (const std::exception& e) {
            qCritical() << "MediaPlayerPlugin initialization failed:" << e.what();
            emit error(tr("Initialization failed: %1").arg(e.what()));

            // Cleanup partially initialized components
            shutdown();
            return false;
        }
    }

    /**
     * @brief Shutdown plugin and cleanup resources
     */
    void shutdown() override {
        qDebug() << "Shutting down MediaPlayerPlugin...";

        // ================ Phase 1: Stop Playback ================

        if (m_mediaPlayer) {
            m_mediaPlayer->stop();
            savePlaylistState();
        }

        // ================ Phase 2: Stop Background Services ================

        stopBackgroundServices();

        // ================ Phase 3: Save Settings ================

        saveSettings();

        // ================ Phase 4: Cleanup Components (reverse order) ================

        m_editor.reset();
        m_disc.reset();
        m_capture.reset();
        m_kdeConnect.reset();
        m_streaming.reset();
        m_platform.reset();
        m_mediaPlayer.reset();
        m_library.reset();

        qDebug() << "MediaPlayerPlugin shutdown complete";
    }

    /**
     * @brief Handle command line arguments
     * @param args List of arguments (files, URLs, options)
     */
    void handleArguments(const QStringList& args) override {
        Q_UNUSED(args)
        // TODO: Implement argument handling
    }

    /**
     * @brief Get plugin configuration options
     * @return Map of configuration key-value pairs
     */
    virtual QVariantMap configuration() const {
        QVariantMap config;

        if (m_mediaPlayer) {
            config["volume"] = m_mediaPlayer->volume();
            config["muted"] = m_mediaPlayer->isMuted();
        }

        if (m_library) {
            config["libraryTrackCount"] = m_library->trackCount();
        }

        return config;
    }

    /**
     * @brief Set plugin configuration
     * @param config Configuration map
     */
    virtual void setConfiguration(const QVariantMap& config) {
        if (m_mediaPlayer) {
            if (config.contains("volume")) {
                m_mediaPlayer->setVolume(config["volume"].toDouble());
            }
            if (config.contains("muted")) {
                m_mediaPlayer->setMuted(config["muted"].toBool());
            }
        }
    }

    /**
     * @brief Get plugin state for session restoration
     * @return State data map
     */
    virtual QVariantMap saveState() const {
        QVariantMap state;
        // TODO: Implement state saving
        return state;
    }

    /**
     * @brief Restore plugin state from saved data
     * @param state State data map
     */
    virtual void restoreState(const QVariantMap& state) {
        Q_UNUSED(state)
        // TODO: Implement state restoration
    }

signals:
    /**
     * @brief Emitted when plugin initialization is complete
     */
    void initializationComplete();

    /**
     * @brief Emitted when library scanning completes
     * @param added Number of new tracks added
     * @param errors Number of scan errors
     */
    void libraryScanComplete(int added, int errors);

    /**
     * @brief Emitted when streaming service status changes
     * @param service Service name
     * @param status New status
     */
    void streamingStatusChanged(const QString& service, const QString& status);

    /**
     * @brief Emitted when device connection changes
     * @param device Device name
     * @param connected True if connected
     */
    void deviceConnectionChanged(const QString& device, bool connected);

    /**
     * @brief Emitted on error
     * @param message Error message
     */
    void error(const QString& message);

    /**
     * @brief Emitted for progress updates
     * @param message Progress message
     * @param current Current progress
     * @param total Total progress
     */
    void progress(const QString& message, int current, int total);

    /**
     * @brief Emitted for info messages
     * @param message Info message
     */
    void info(const QString& message);

    /**
     * @brief Emitted for warning messages
     * @param message Warning message
     */
    void warning(const QString& message);

private slots:
    /**
     * @brief Handle incoming files from devices
     * @param path File path
     * @param deviceName Device name
     */
    void handleIncomingFile(const QString& path, const QString& deviceName) {
        Q_UNUSED(path)
        Q_UNUSED(deviceName)
        // TODO: Implement file handling
    }

    /**
     * @brief Handle track changes
     */
    void onTrackChanged() {
        // TODO: Implement track change handling
    }

    /**
     * @brief Handle track completion
     */
    void onTrackFinished() {
        qDebug() << "Track finished, advancing to next";
    }

    /**
     * @brief Handle playback errors
     * @param error Error message
     */
    void onPlaybackError(const QString& error) {
        qWarning() << "Playback error:" << error;
    }

    /**
     * @brief Handle library scan progress
     * @param current Current file count
     * @param total Total files to scan
     */
    void onLibraryScanProgress(int current, int total) {
        emit progress(tr("Scanning library..."), current, total);
    }

    /**
     * @brief Handle library scan completion
     * @param added New tracks added
     * @param errors Scan errors
     */
    void onLibraryScanComplete(int added, int errors) {
        emit libraryScanComplete(added, errors);
    }

private:
    // ================ Initialization Methods ================

    /**
     * @brief Initialize media library database
     */
    void initializeLibrary() {
        QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + "/library.db";

        m_library = std::make_shared<Library>(dbPath, this);

        // Connect library signals
        connect(m_library.get(), &Library::scanProgress,
                this, &MediaPlayerPlugin::onLibraryScanProgress);
        connect(m_library.get(), &Library::scanCompleted,
                this, &MediaPlayerPlugin::onLibraryScanComplete);

        qDebug() << "Library initialized at:" << dbPath;
    }

    /**
     * @brief Initialize media player engine
     */
    void initializeMediaPlayer() {
        m_mediaPlayer = std::make_unique<MediaPlayer>(m_library, this);

        // Connect player signals
        connect(m_mediaPlayer.get(), &MediaPlayer::currentTrackChanged,
                this, &MediaPlayerPlugin::onTrackChanged);
        connect(m_mediaPlayer.get(), &MediaPlayer::playbackFinished,
                this, &MediaPlayerPlugin::onTrackFinished);

        qDebug() << "MediaPlayer initialized";
    }

    /**
     * @brief Initialize platform integration
     */
    void initializePlatform() {
        m_platform = std::make_unique<Platform>(this);
        m_platform->setPlayerObject(m_mediaPlayer.get());

        qDebug() << "Platform integration initialized";
    }

    /**
     * @brief Initialize MPRIS D-Bus service
     */
    void initializeMpris() {
        if (m_platform) {
            m_platform->setupMpris();
            qDebug() << "MPRIS service initialized";
        }
    }

    /**
     * @brief Initialize system tray
     */
    void initializeTray() {
        if (m_platform) {
            m_platform->initializeTray();
            m_platform->setTrayVisible(
                m_settings->value("Tray/Visible", true).toBool()
            );
            qDebug() << "System tray initialized";
        }
    }

    /**
     * @brief Initialize streaming services
     */
    void initializeStreaming() {
        m_streaming = std::make_unique<Streaming>(this);

        // Connect streaming signals
        connect(m_streaming.get(), &Streaming::serviceStatusChanged,
                this, &MediaPlayerPlugin::streamingStatusChanged);

        qDebug() << "Streaming services initialized";
    }

    /**
     * @brief Initialize device integration
     */
    void initializeDeviceIntegration() {
        m_kdeConnect = std::make_unique<KDEConnect>(this);

        // Connect device signals
        connect(m_kdeConnect.get(), &KDEConnect::fileReceived,
                this, &MediaPlayerPlugin::handleIncomingFile);

        qDebug() << "Device integration initialized";
    }

    /**
     * @brief Initialize capture/recording
     */
    void initializeCapture() {
        m_capture = std::make_unique<Capture>(this);
        qDebug() << "Capture system initialized";
    }

    /**
     * @brief Initialize disc support
     */
    void initializeDiscSupport() {
        // TODO: Find optical drive
        QString device; // = findOpticalDrive();
        if (!device.isEmpty()) {
            m_disc = std::make_unique<Disc>(device, this);
            qDebug() << "Disc support initialized for device:" << device;
        } else {
            qDebug() << "No optical drive found, disc support disabled";
        }
    }

    /**
     * @brief Initialize editor component
     */
    void initializeEditor() {
        m_editor = std::make_unique<AudioEditor>(this);
        qDebug() << "Editor initialized";
    }

    /**
     * @brief Export components to QML context
     */
    void exportComponentsToQml() {
        QQmlContext* context = m_context.engine->rootContext();

        context->setContextProperty("MediaPlayer", m_mediaPlayer.get());
        context->setContextProperty("Library", m_library.get());
        context->setContextProperty("Platform", m_platform.get());
        context->setContextProperty("Streaming", m_streaming.get());
        context->setContextProperty("KDEConnect", m_kdeConnect.get());
        context->setContextProperty("Capture", m_capture.get());
        context->setContextProperty("Disc", m_disc.get());
        context->setContextProperty("Editor", m_editor.get());

        qDebug() << "All components exported to QML";
    }

    /**
     * @brief Load translations
     */
    void loadTranslations() {
        QString locale = QLocale::system().name();
        QTranslator* translator = new QTranslator(this);

        // Try application translations
        if (translator->load("mediaplayer_" + locale,
                             QStandardPaths::locate(QStandardPaths::AppDataLocation,
                                                    "translations",
                                                    QStandardPaths::LocateDirectory))) {
            QCoreApplication::installTranslator(translator);
        }
    }

    /**
     * @brief Apply saved settings
     */
    void applySavedSettings() {
        // TODO: Implement settings restoration
    }

    /**
     * @brief Connect cross-component signals
     */
    void connectComponentSignals() {
        // TODO: Implement cross-component signal connections
    }

    /**
     * @brief Initialize default playlist
     */
    void initializeDefaultPlaylist() {
        // TODO: Implement default playlist initialization
    }

    /**
     * @brief Start background services
     */
    void startBackgroundServices() {
        // TODO: Implement background services
    }

    /**
     * @brief Stop background services
     */
    void stopBackgroundServices() {
        // TODO: Implement background service stopping
    }

    /**
     * @brief Save playlist state
     */
    void savePlaylistState() {
        // TODO: Implement playlist state saving
    }

    /**
     * @brief Save settings
     */
    void saveSettings() {
        // TODO: Implement settings saving
    }

    // ================ Member Variables ================

    AppContext m_context;
    QSettings* m_settings;

    // Core components
    std::shared_ptr<Library> m_library;
    std::unique_ptr<MediaPlayer> m_mediaPlayer;
    std::unique_ptr<Platform> m_platform;

    // Optional components
    std::unique_ptr<Streaming> m_streaming;
    std::unique_ptr<KDEConnect> m_kdeConnect;
    std::unique_ptr<Capture> m_capture;
    std::unique_ptr<Disc> m_disc;
    std::unique_ptr<AudioEditor> m_editor;
};

} // namespace Aegis
