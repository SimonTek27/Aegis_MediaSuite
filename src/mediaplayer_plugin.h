// mediaplayer_plugin.h — Media Player mode plugin for Aegis application suite
// Fixed:
//   - saveState() / restoreState()  implemented
//   - applySavedSettings()          implemented
//   - connectComponentSignals()     implemented
//   - initializeDefaultPlaylist()   implemented
//   - startBackgroundServices()     implemented
//   - stopBackgroundServices()      implemented
//   - savePlaylistState()           implemented
//   - saveSettings()                implemented
//   - handleIncomingFile()          implemented
//   - onTrackChanged()              implemented
//   - handleArguments()             implemented
//   - version synchronised with AEGIS_VERSION
#pragma once

#include "plugin_interface.h"
#include "mediaplayer.h"
#include "library.h"
#include "streaming.h"
#include "capture.h"
#include "kdeconnect.h"
#include "platform.h"
#include "audio_karaoke.h"
#include "disc.h"
#include "audioeditor.h"

#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QTranslator>
#include <QTimer>
#include <QDebug>

// Shared version constant (keeps it in sync with main.cpp)
#ifndef AEGIS_VERSION
#  define AEGIS_VERSION "2.1.1"
#endif

namespace Aegis {

// Forward declarations for optional components
class Visualization;
class Lyrics;
class Radio;
class Podcasts;
class Sync;

/**
 * @brief Media Player mode plugin — complete media playback environment.
 *
 * Manages all components required for media player mode:
 *  - MediaPlayer  (core playback engine with playlist)
 *  - Library      (local media database and metadata)
 *  - Platform     (MPRIS, tray, notifications)
 *  - Streaming    (YouTube, radio, podcasts)
 *  - KDEConnect   (device integration, DLNA, Bluetooth)
 *  - Capture      (recording)
 *  - Disc         (CD/DVD/Blu-ray)
 *  - AudioEditor  (quick edit integration)
 */
class MediaPlayerPlugin : public AppModePlugin {
    Q_OBJECT

public:
    explicit MediaPlayerPlugin(QObject* parent = nullptr)
        : AppModePlugin(parent)
        , m_settings(new QSettings("Aegis", "MediaPlayer", this))
    {
        qDebug() << "MediaPlayerPlugin created (v" AEGIS_VERSION ")";
    }

    // ------------------------------------------------------------------ identity
    QString modeName()    const override { return "mediaplayer"; }
    QString displayName() const override { return tr("Media Player"); }
    QString description() const override {
        return tr("Universal media player with library management, "
                  "streaming support, disc playback, and device integration.");
    }
    QString qmlEntryPoint() const override {
        return "qrc:/qml/mediaplayer/Main.qml";
    }

    // ------------------------------------------------------------------ lifecycle
    bool initialize(const AppContext& ctx) override {
        qDebug() << "Initializing MediaPlayerPlugin...";

        if (!ctx.engine || !ctx.engine->rootContext()) {
            qCritical() << "MediaPlayerPlugin: Invalid QML engine context";
            return false;
        }

        m_context = ctx;

        try {
            // Phase 1 – Core
            initializeLibrary();
            initializeMediaPlayer();

            // Phase 2 – Platform
            initializePlatform();
            initializeMpris();
            initializeTray();

            // Phase 3 – Optional features
            initializeStreaming();
            initializeDeviceIntegration();
            initializeCapture();
            initializeDiscSupport();
            initializeEditor();

            // Phase 4 – QML integration
            exportComponentsToQml();
            loadTranslations();
            applySavedSettings();

            // Phase 5 – Final wiring
            connectComponentSignals();
            initializeDefaultPlaylist();
            startBackgroundServices();

            qDebug() << "MediaPlayerPlugin initialized successfully";
            emit initializationComplete();
            return true;

        } catch (const std::exception& e) {
            qCritical() << "MediaPlayerPlugin initialization failed:" << e.what();
            emit error(tr("Initialization failed: %1").arg(e.what()));
            shutdown();
            return false;
        }
    }

    void shutdown() override {
        qDebug() << "Shutting down MediaPlayerPlugin...";

        if (m_mediaPlayer) {
            m_mediaPlayer->stop();
            savePlaylistState();
        }

        stopBackgroundServices();
        saveSettings();

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

    void handleArguments(const QStringList& args) override {
        if (!m_mediaPlayer) {
            qWarning() << "handleArguments: MediaPlayer not ready";
            return;
        }

        for (const QString& arg : args) {
            QUrl url = QUrl::fromUserInput(arg);
            if (url.isValid()) {
                m_mediaPlayer->enqueue(url);
                qDebug() << "Enqueued from argument:" << arg;
            } else {
                qWarning() << "Ignoring invalid argument:" << arg;
            }
        }

        if (!args.isEmpty() && m_mediaPlayer) {
            m_mediaPlayer->play();
        }
    }

    // ------------------------------------------------------------------ state persistence
    virtual QVariantMap saveState() const {
        QVariantMap state;

        if (m_mediaPlayer) {
            state["volume"]   = m_mediaPlayer->volume();
            state["muted"]    = m_mediaPlayer->isMuted();
            state["position"] = m_mediaPlayer->position();

            // Persist playlist URLs
            QStringList playlistUrls;
            for (const auto& item : m_mediaPlayer->playlist()) {
                playlistUrls << item.url.toString();
            }
            state["playlist"]     = playlistUrls;
            state["currentIndex"] = m_mediaPlayer->currentIndex();
        }

        state["version"] = AEGIS_VERSION;
        return state;
    }

    virtual void restoreState(const QVariantMap& state) {
        if (state.isEmpty()) return;

        if (m_mediaPlayer) {
            if (state.contains("volume"))
                m_mediaPlayer->setVolume(state["volume"].toDouble());
            if (state.contains("muted"))
                m_mediaPlayer->setMuted(state["muted"].toBool());

            // Restore playlist
            if (state.contains("playlist")) {
                const QStringList urls = state["playlist"].toStringList();
                for (const QString& u : urls) {
                    QUrl url(u);
                    if (url.isValid()) m_mediaPlayer->enqueue(url);
                }
            }
            if (state.contains("currentIndex")) {
                int idx = state["currentIndex"].toInt();
                if (idx >= 0) m_mediaPlayer->setCurrentIndex(idx);
            }
        }
    }

    virtual QVariantMap configuration() const {
        QVariantMap config;
        if (m_mediaPlayer) {
            config["volume"] = m_mediaPlayer->volume();
            config["muted"]  = m_mediaPlayer->isMuted();
        }
        if (m_library) {
            config["libraryTrackCount"] = m_library->trackCount();
        }
        return config;
    }

    virtual void setConfiguration(const QVariantMap& config) {
        if (!m_mediaPlayer) return;
        if (config.contains("volume"))
            m_mediaPlayer->setVolume(config["volume"].toDouble());
        if (config.contains("muted"))
            m_mediaPlayer->setMuted(config["muted"].toBool());
    }

signals:
    void initializationComplete();
    void libraryScanComplete(int added, int errors);
    void streamingStatusChanged(const QString& service, const QString& status);
    void deviceConnectionChanged(const QString& device, bool connected);
    void error(const QString& message);
    void progress(const QString& message, int current, int total);
    void info(const QString& message);
    void warning(const QString& message);

private slots:
    void handleIncomingFile(const QString& path, const QString& deviceName) {
        qDebug() << "Incoming file from" << deviceName << ":" << path;
        if (!m_mediaPlayer) {
            qWarning() << "handleIncomingFile: MediaPlayer not available";
            return;
        }
        QUrl url = QUrl::fromLocalFile(path);
        if (url.isValid()) {
            m_mediaPlayer->enqueue(url);
            emit info(tr("File received from %1: %2")
                          .arg(deviceName, QFileInfo(path).fileName()));
        }
    }

    void onTrackChanged() {
        if (!m_mediaPlayer) return;
        auto meta = m_mediaPlayer->currentMetadata();
        qDebug() << "Track changed:"
                 << meta.artist << "-" << meta.title;

        // Update platform / tray / MPRIS with new metadata
        if (m_platform) {
            m_platform->setNowPlaying(meta.title, meta.artist, meta.album);
        }
    }

    void onTrackFinished() {
        qDebug() << "Track finished — advancing to next";
        // MediaPlayer handles auto-advance internally;
        // here we just log / update external services.
        if (m_platform) m_platform->clearNowPlaying();
    }

    void onPlaybackError(const QString& err) {
        qWarning() << "Playback error:" << err;
        emit error(tr("Playback error: %1").arg(err));
    }

    void onLibraryScanProgress(int current, int total) {
        emit progress(tr("Scanning library..."), current, total);
    }

    void onLibraryScanComplete(int added, int errors) {
        qDebug() << "Library scan complete — added:" << added << "errors:" << errors;
        emit libraryScanComplete(added, errors);
    }

private:
    // ------------------------------------------------------------------ init helpers
    void initializeLibrary() {
        QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + "/library.db";
        m_library = std::make_shared<Library>(dbPath, this);

        connect(m_library.get(), &Library::scanProgress,
                this, &MediaPlayerPlugin::onLibraryScanProgress);
        connect(m_library.get(), &Library::scanCompleted,
                this, &MediaPlayerPlugin::onLibraryScanComplete);

        qDebug() << "Library initialized at:" << dbPath;
    }

    void initializeMediaPlayer() {
        m_mediaPlayer = std::make_unique<MediaPlayer>(m_library, this);

        connect(m_mediaPlayer.get(), &MediaPlayer::currentTrackChanged,
                this, &MediaPlayerPlugin::onTrackChanged);
        connect(m_mediaPlayer.get(), &MediaPlayer::playbackFinished,
                this, &MediaPlayerPlugin::onTrackFinished);
        connect(m_mediaPlayer.get(), &MediaPlayer::error,
                this, &MediaPlayerPlugin::onPlaybackError);

        qDebug() << "MediaPlayer initialized";
    }

    void initializePlatform() {
        m_platform = std::make_unique<Platform>(this);
        m_platform->setPlayerObject(m_mediaPlayer.get());
        qDebug() << "Platform integration initialized";
    }

    void initializeMpris() {
        if (m_platform) {
            m_platform->setupMpris();
            qDebug() << "MPRIS service initialized";
        }
    }

    void initializeTray() {
        if (m_platform) {
            m_platform->initializeTray();
            m_platform->setTrayVisible(
                m_settings->value("Tray/Visible", true).toBool());
            qDebug() << "System tray initialized";
        }
    }

    void initializeStreaming() {
        m_streaming = std::make_unique<Streaming>(this);
        connect(m_streaming.get(), &Streaming::serviceStatusChanged,
                this, &MediaPlayerPlugin::streamingStatusChanged);
        qDebug() << "Streaming services initialized";
    }

    void initializeDeviceIntegration() {
        m_kdeConnect = std::make_unique<KDEConnect>(this);
        connect(m_kdeConnect.get(), &KDEConnect::fileReceived,
                this, &MediaPlayerPlugin::handleIncomingFile);
        connect(m_kdeConnect.get(), &KDEConnect::deviceConnected,
                this, [this](const QString& dev) {
                    emit deviceConnectionChanged(dev, true);
                });
        connect(m_kdeConnect.get(), &KDEConnect::deviceDisconnected,
                this, [this](const QString& dev) {
                    emit deviceConnectionChanged(dev, false);
                });
        qDebug() << "Device integration initialized";
    }

    void initializeCapture() {
        m_capture = std::make_unique<Capture>(this);
        qDebug() << "Capture system initialized";
    }

    void initializeDiscSupport() {
        QString device = Platform::findOpticalDrive();
        if (!device.isEmpty()) {
            m_disc = std::make_unique<Disc>(device, this);
            qDebug() << "Disc support initialized for device:" << device;
        } else {
            qDebug() << "No optical drive found — disc support disabled";
        }
    }

    void initializeEditor() {
        m_editor = std::make_unique<AudioEditor>(this);
        qDebug() << "Editor initialized";
    }

    void exportComponentsToQml() {
        QQmlContext* ctx = m_context.engine->rootContext();
        ctx->setContextProperty("MediaPlayer", m_mediaPlayer.get());
        ctx->setContextProperty("Library",     m_library.get());
        ctx->setContextProperty("Platform",    m_platform.get());
        ctx->setContextProperty("Streaming",   m_streaming.get());
        ctx->setContextProperty("KDEConnect",  m_kdeConnect.get());
        ctx->setContextProperty("Capture",     m_capture.get());
        ctx->setContextProperty("Disc",        m_disc.get());
        ctx->setContextProperty("Editor",      m_editor.get());
        qDebug() << "All components exported to QML";
    }

    void loadTranslations() {
        QString locale = QLocale::system().name();
        auto* translator = new QTranslator(this);

        QString translationsDir =
            QStandardPaths::locate(QStandardPaths::AppDataLocation,
                                   "translations",
                                   QStandardPaths::LocateDirectory);

        if (!translationsDir.isEmpty() &&
            translator->load("mediaplayer_" + locale, translationsDir)) {
            QCoreApplication::installTranslator(translator);
        } else {
            delete translator;
        }
    }

    // ------------------------------------------------------------------ settings
    void applySavedSettings() {
        if (!m_mediaPlayer) return;

        double volume = m_settings->value("Playback/Volume", 80.0).toDouble();
        bool   muted  = m_settings->value("Playback/Muted",  false).toBool();
        m_mediaPlayer->setVolume(volume);
        m_mediaPlayer->setMuted(muted);

        // Restore shuffle / repeat state
        bool shuffle = m_settings->value("Playback/Shuffle", false).toBool();
        int  repeat  = m_settings->value("Playback/Repeat",  0).toInt();
        m_mediaPlayer->setShuffle(shuffle);
        m_mediaPlayer->setRepeatMode(static_cast<MediaPlayer::RepeatMode>(repeat));

        qDebug() << "Saved settings applied (vol=" << volume
                 << "muted=" << muted
                 << "shuffle=" << shuffle << "repeat=" << repeat << ")";
    }

    void saveSettings() {
        if (!m_mediaPlayer) return;

        m_settings->setValue("Playback/Volume",  m_mediaPlayer->volume());
        m_settings->setValue("Playback/Muted",   m_mediaPlayer->isMuted());
        m_settings->setValue("Playback/Shuffle", m_mediaPlayer->shuffle());
        m_settings->setValue("Playback/Repeat",  static_cast<int>(m_mediaPlayer->repeatMode()));
        m_settings->sync();

        qDebug() << "Settings saved";
    }

    // ------------------------------------------------------------------ signals
    void connectComponentSignals() {
        if (!m_mediaPlayer || !m_platform) return;

        // Keep platform / MPRIS metadata in sync with playback
        connect(m_mediaPlayer.get(), &MediaPlayer::positionChanged,
                m_platform.get(), &Platform::setPosition);
        connect(m_mediaPlayer.get(), &MediaPlayer::durationChanged,
                m_platform.get(), &Platform::setDuration);
        connect(m_mediaPlayer.get(), &MediaPlayer::stateChanged,
                m_platform.get(), &Platform::setPlaybackState);

        // Library notifications → info signal
        connect(m_library.get(), &Library::error,
                this, [this](const QString& msg) {
                    emit warning(tr("Library: %1").arg(msg));
                });

        qDebug() << "Cross-component signals connected";
    }

    // ------------------------------------------------------------------ playlist
    void initializeDefaultPlaylist() {
        if (!m_mediaPlayer) return;

        // Restore last playlist from settings
        QStringList savedUrls =
            m_settings->value("Playlist/LastSession").toStringList();

        if (!savedUrls.isEmpty()) {
            for (const QString& u : savedUrls) {
                QUrl url(u);
                if (url.isValid()) m_mediaPlayer->enqueue(url);
            }
            qDebug() << "Restored" << savedUrls.size() << "tracks from last session";
        } else {
            qDebug() << "No previous playlist to restore";
        }
    }

    void savePlaylistState() {
        if (!m_mediaPlayer) return;

        QStringList urls;
        for (const auto& item : m_mediaPlayer->playlist()) {
            urls << item.url.toString();
        }
        m_settings->setValue("Playlist/LastSession", urls);
        m_settings->setValue("Playlist/CurrentIndex", m_mediaPlayer->currentIndex());
        m_settings->sync();

        qDebug() << "Playlist state saved (" << urls.size() << "tracks)";
    }

    // ------------------------------------------------------------------ background services
    void startBackgroundServices() {
        // Auto-save timer: flush settings every 5 minutes
        m_autoSaveTimer = new QTimer(this);
        m_autoSaveTimer->setInterval(5 * 60 * 1000);
        m_autoSaveTimer->setSingleShot(false);
        connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() {
            saveSettings();
            savePlaylistState();
        });
        m_autoSaveTimer->start();

        // Trigger background library refresh scan if needed
        if (m_library) {
            QString watchPath =
                m_settings->value("Library/WatchPath",
                                  QStandardPaths::writableLocation(
                                      QStandardPaths::MusicLocation))
                    .toString();
            if (!watchPath.isEmpty() && QDir(watchPath).exists()) {
                QTimer::singleShot(3000, this, [this, watchPath]() {
                    m_library->scanDirectory(watchPath);
                });
            }
        }

        qDebug() << "Background services started";
    }

    void stopBackgroundServices() {
        if (m_autoSaveTimer) {
            m_autoSaveTimer->stop();
            m_autoSaveTimer->deleteLater();
            m_autoSaveTimer = nullptr;
        }
        qDebug() << "Background services stopped";
    }

    // ------------------------------------------------------------------ members
    AppContext m_context;
    QSettings* m_settings;
    QTimer*    m_autoSaveTimer = nullptr;

    // Core
    std::shared_ptr<Library>       m_library;
    std::unique_ptr<MediaPlayer>   m_mediaPlayer;
    std::unique_ptr<Platform>      m_platform;

    // Optional
    std::unique_ptr<Streaming>   m_streaming;
    std::unique_ptr<KDEConnect>  m_kdeConnect;
    std::unique_ptr<Capture>     m_capture;
    std::unique_ptr<Disc>        m_disc;
    std::unique_ptr<AudioEditor> m_editor;
};

} // namespace Aegis
