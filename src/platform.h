// platform.h - Platform integration and system services
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QDBusAbstractAdaptor>
#include <QPointer>

// Conditional compilation for KDE Framework 6 integration
#ifdef KF6_VERSION
#include <KStatusNotifierItem>
#include <KNotification>
#endif

// Forward declarations to avoid header dependencies
namespace Aegis {
    class Core;
    class Library;
    class MediaPlayer;
}

/**
 * @brief MPRIS2 D-Bus adaptor for media player integration
 *
 * Provides standardized D-Bus interface for:
 * - Media player control from system media keys
 * - Integration with desktop environments (GNOME, KDE, Unity)
 * - Media information display in system panels
 *
 * Implements org.mpris.MediaPlayer2.Player interface specification.
 */
class MprisAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

    // MPRIS2 Player interface properties
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus NOTIFY playbackStatusChanged)
    Q_PROPERTY(QVariantMap Metadata READ metadata NOTIFY metadataChanged)
    Q_PROPERTY(double Volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(qlonglong Position READ position NOTIFY positionChanged)
    Q_PROPERTY(bool CanGoNext READ canGoNext CONSTANT)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious CONSTANT)
    Q_PROPERTY(bool CanPlay READ canPlay CONSTANT)
    Q_PROPERTY(bool CanPause READ canPause CONSTANT)
    Q_PROPERTY(bool CanSeek READ canSeek CONSTANT)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)

public:
    /**
     * @brief Construct MPRIS adaptor for media player object
     * @param player Media player object to control
     * @param parent Parent QObject
     */
    explicit MprisAdaptor(QObject *player, QObject *parent = nullptr);

    // ================ Property Getters ================

    QString playbackStatus() const;      ///< "Playing", "Paused", or "Stopped"
    QVariantMap metadata() const;        ///< Current track metadata
    double volume() const;               ///< Volume level 0.0-1.0
    qlonglong position() const;          ///< Position in microseconds

    // Read-only capability flags
    bool canGoNext() const { return true; }
    bool canGoPrevious() const { return true; }
    bool canPlay() const { return true; }
    bool canPause() const { return true; }
    bool canSeek() const { return true; }
    bool canControl() const { return true; }

    // MPRIS2 Root interface properties
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)

    bool canQuit() const { return true; }    ///< Allow application termination
    bool canRaise() const { return true; }   ///< Allow window activation
    QString identity() const { return QStringLiteral("Aegis Media Player"); }

public slots:
    // ================ MPRIS2 Player Interface Methods ================

    void Play();                    ///< Start playback
    void Pause();                   ///< Pause playback
    void PlayPause();               ///< Toggle play/pause
    void Stop();                    ///< Stop playback
    void Next();                    ///< Skip to next track
    void Previous();                ///< Return to previous track
    void Seek(qlonglong offset);    ///< Relative seek (microseconds)
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position);
    ///< Absolute seek to position
    void OpenUri(const QString &uri); ///< Open media URI

    // ================ MPRIS2 Root Interface Methods ================

    void Quit();                    ///< Terminate application
    void Raise();                   ///< Activate application window

signals:
    // ================ MPRIS2 Property Change Signals ================

    void playbackStatusChanged();   ///< Playback state changed
    void metadataChanged();         ///< Track metadata changed
    void volumeChanged();           ///< Volume level changed
    void positionChanged();         ///< Playback position changed
    void seeked(qlonglong position); ///< Seek operation completed

private:
    QObject *m_player;              ///< Controlled media player object
};

/**
 * @brief Administrative D-Bus interface for remote control
 *
 * Provides extended control and monitoring capabilities:
 * - Library management and statistics
 * - System integration (Webmin, remote administration)
 * - Batch operations and configuration
 *
 * Implements org.aegis.Admin custom D-Bus interface.
 */
class AegisAdminAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.aegis.Admin")

    // Admin interface properties
    Q_PROPERTY(int trackCount READ trackCount)
    Q_PROPERTY(bool playing READ playing)
    Q_PROPERTY(double volume READ volume)
    Q_PROPERTY(QString currentFile READ currentFile)

public:
    /**
     * @brief Construct admin adaptor with core components
     * @param core Playback engine reference
     * @param library Media library reference
     * @param parent Parent QObject
     */
    explicit AegisAdminAdaptor(Aegis::Core *core, Aegis::Library *library,
                               QObject *parent = nullptr);

    // ================ Property Getters ================

    int trackCount() const;         ///< Total tracks in library
    bool playing() const;           ///< Current playback status
    double volume() const;          ///< Current volume level
    QString currentFile() const;    ///< Currently playing file path

public slots:
    // ================ Status & Monitoring Methods ================

    /**
     * @brief Get library statistics in JSON format
     * @return JSON string with track count, size, scanning status
     */
    Q_INVOKABLE QString getLibraryStats();

    /**
     * @brief Get playback status in JSON format
     * @return JSON string with position, duration, state
     */
    Q_INVOKABLE QString getPlaybackStatus();

    /**
     * @brief Get recently played tracks
     * @param limit Maximum tracks to return
     * @return List of recent track paths
     */
    Q_INVOKABLE QStringList getRecentTracks(int limit = 10);

    // ================ Playback Control Methods ================

    Q_INVOKABLE void playPause();   ///< Toggle play/pause
    Q_INVOKABLE void stop();        ///< Stop playback
    Q_INVOKABLE void next();        ///< Skip to next track
    Q_INVOKABLE void previous();    ///< Return to previous track
    Q_INVOKABLE void setVolume(double vol); ///< Set volume (0.0-100.0)
    Q_INVOKABLE void seek(double position); ///< Seek to position (seconds)
    Q_INVOKABLE void loadFile(const QString &path); ///< Load and play file

    // ================ Library Management Methods ================

    Q_INVOKABLE void scanDirectory(const QString &path); ///< Scan for media
    Q_INVOKABLE void searchLibrary(const QString &query); ///< Search library
    Q_INVOKABLE void deleteTrack(int trackId); ///< Remove track from library

    // ================ Configuration Methods ================

    Q_INVOKABLE QString getConfig(const QString &key); ///< Get config value
    Q_INVOKABLE void setConfig(const QString &key, const QString &value);
    ///< Set config value

    // ================ System Methods ================

    Q_INVOKABLE void reloadLibrary();   ///< Reload library from disk
    Q_INVOKABLE void clearCache();      ///< Clear application cache

signals:
    /**
     * @brief Emitted when library scanning completes
     * @param newTracks Number of newly added tracks
     */
    void libraryScanned(int newTracks);

    /**
     * @brief Emitted when playback starts
     * @param file Path to playing file
     */
    void playbackStarted(const QString &file);

    /**
     * @brief Emitted when error occurs
     * @param message Error description
     */
    void error(const QString &message);

private:
    Aegis::Core *m_core;           ///< Playback engine instance
    Aegis::Library *m_library;     ///< Media library instance
    QSettings *m_settings;         ///< Application settings
};

/**
 * @brief Platform integration manager
 *
 * Handles desktop environment integration:
 * - System tray icon and notifications
 * - MPRIS2 D-Bus service registration
 * - Platform-specific features (KDE StatusNotifier, etc.)
 * - Desktop notifications and alerts
 */
class Platform : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool trayVisible READ trayVisible WRITE setTrayVisible NOTIFY trayChanged)
    Q_PROPERTY(bool mprisEnabled READ mprisEnabled NOTIFY mprisChanged)
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled NOTIFY notificationsChanged)

public:
    /**
     * @brief Construct platform integration manager
     * @param parent Parent QObject
     */
    explicit Platform(QObject *parent = nullptr);

    /**
     * @brief Destructor with cleanup
     */
    ~Platform();

    // ================ Property Accessors ================

    bool trayVisible() const;                          ///< Tray icon visibility
    void setTrayVisible(bool visible);                 ///< Show/hide tray icon

    bool mprisEnabled() const { return m_mprisEnabled; } ///< MPRIS2 service status
    bool notificationsEnabled() const { return m_notificationsEnabled; } ///< Notification status
    void setNotificationsEnabled(bool enabled);        ///< Enable/disable notifications

    // ================ Core Setup Methods ================

    /**
     * @brief Set media player object for integration
     * @param player Media player instance
     */
    void setPlayerObject(QObject *player);

    /**
     * @brief Initialize MPRIS2 D-Bus service
     * @return True if service registered successfully
     */
    bool setupMpris();

    /**
     * @brief Initialize system tray icon
     * @return True if tray created successfully
     */
    bool initializeTray();

    // ================ Utility Methods ================

    /**
     * @brief Show desktop notification
     * @param title Notification title
     * @param body Notification message body
     * @param icon Icon name (freedesktop.org specification)
     * @param timeout Display timeout in milliseconds
     */
    Q_INVOKABLE void notify(const QString &title, const QString &body,
                            const QString &icon = "media-playback-start",
                            int timeout = 5000);

    /**
     * @brief Show error notification
     * @param message Error message
     */
    Q_INVOKABLE void notifyError(const QString &message);

    /**
     * @brief Show track change notification
     * @param title Track title
     * @param artist Track artist
     */
    Q_INVOKABLE void notifyTrackChange(const QString &title, const QString &artist);

    /**
     * @brief Get available audio output devices
     * @return List of device names
     */
    Q_INVOKABLE QStringList audioOutputDevices() const;

    /**
     * @brief Set preferred audio output device
     * @param device Device identifier
     */
    Q_INVOKABLE void setAudioOutputDevice(const QString &device);

signals:
    /**
     * @brief Emitted when tray visibility changes
     */
    void trayChanged();

    /**
     * @brief Emitted when MPRIS2 status changes
     */
    void mprisChanged();

    /**
     * @brief Emitted when notification settings change
     */
    void notificationsChanged();

    /**
     * @brief Emitted when tray icon is activated
     */
    void trayActivated();

    /**
     * @brief Emitted when play/pause is requested via tray
     */
    void playPauseRequested();

    /**
     * @brief Emitted when next track is requested via tray
     */
    void nextRequested();

    /**
     * @brief Emitted when previous track is requested via tray
     */
    void previousRequested();

    /**
     * @brief Emitted when audio device changes
     * @param device New audio device name
     */
    void audioDeviceChanged(const QString &device);

private slots:
    /**
     * @brief Handle tray icon activation
     * @param reason Activation reason (click, context menu, etc.)
     */
    void onTrayActivated(int reason);

    /**
     * @brief Handle MPRIS2 property changes
     */
    void onMprisPropertyChanged();

    /**
     * @brief Update tray icon based on playback state
     */
    void updateTrayIcon();

private:
    // ================ Member Variables ================

    #ifdef KF6_VERSION
    KStatusNotifierItem *m_tray{nullptr};   ///< KDE Status Notifier Item
    #else
    QPointer<QSystemTrayIcon> m_tray;       ///< Qt System Tray Icon
    #endif

    QObject *m_player{nullptr};             ///< Controlled media player
    MprisAdaptor *m_mprisAdaptor{nullptr};  ///< MPRIS2 D-Bus adaptor
    AegisAdminAdaptor *m_adminAdaptor{nullptr}; ///< Admin D-Bus adaptor

    bool m_trayVisible{false};              ///< Tray icon visibility flag
    bool m_mprisEnabled{false};             ///< MPRIS2 service status
    bool m_notificationsEnabled{true};      ///< Notification enabled flag

    QString m_currentIcon;                  ///< Current tray icon name

    // ================ Private Helper Methods ================

    /**
     * @brief Create tray icon context menu
     * @return Configured QMenu instance
     */
    QMenu* createTrayMenu();

    /**
     * @brief Register D-Bus services
     * @return True if services registered successfully
     */
    bool registerDBusServices();

    /**
     * @brief Unregister D-Bus services
     */
    void unregisterDBusServices();

    /**
     * @brief Platform-specific notification implementation
     */
    void sendNativeNotification(const QString &title, const QString &body,
                                const QString &icon, int timeout);
};

// Register types for Qt meta-object system
Q_DECLARE_METATYPE(Aegis::PlaybackState)
