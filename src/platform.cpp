// platform.cpp - Fixed Platform Integration Implementation
#include "platform.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QDateTime>
#include <QDBusConnection>
#include <QIcon>
#include <QApplication>
#include <QMenu>
#include <QStandardPaths>
#include <QDebug>

#ifdef KF6_VERSION
#include <KNotification>
#include <KLocalizedString>
#else
#include <QSystemTrayIcon>
#include <QProcess>
#endif

// ============================================================================
// Platform Implementation
// ============================================================================

Platform::Platform(QObject *parent) 
    : QObject(parent)
    , m_trayVisible(false)
    , m_mprisEnabled(false)
    , m_notificationsEnabled(true)
{
}

Platform::~Platform() {
    unregisterDBusServices();
#ifdef KF6_VERSION
    delete m_tray;
#endif
}

void Platform::setPlayerObject(QObject *player) {
    m_player = player;

    if (player) {
        connect(player, SIGNAL(playingChanged()),
                this, SLOT(onMprisPropertyChanged()));
        connect(player, SIGNAL(currentFileChanged()),
                this, SLOT(onMprisPropertyChanged()));
    }
}

bool Platform::initializeTray() {
#ifdef KF6_VERSION
    m_tray = new KStatusNotifierItem(this);
    m_tray->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_tray->setStatus(KStatusNotifierItem::Passive);
    m_tray->setIconName(QStringLiteral("media-playback-start"));
    m_tray->setTitle(QStringLiteral("Aegis"));

    QMenu *menu = new QMenu();
    menu->addAction(QIcon::fromTheme("media-playback-start"), i18n("Play/Pause"),
                    this, &Platform::playPauseRequested);
    menu->addAction(QIcon::fromTheme("media-skip-forward"), i18n("Next"),
                    this, &Platform::nextRequested);
    menu->addAction(QIcon::fromTheme("application-exit"), i18n("Quit"),
                    qApp, &QApplication::quit);
    m_tray->setContextMenu(menu);

    connect(m_tray, &KStatusNotifierItem::activateRequested,
            this, &Platform::trayActivated);
#else
    m_tray = new QSystemTrayIcon(QIcon::fromTheme("media-playback-start"), this);
    QMenu *menu = new QMenu();
    menu->addAction(tr("Play/Pause"), this, &Platform::playPauseRequested);
    menu->addAction(tr("Next"), this, &Platform::nextRequested);
    menu->addAction(tr("Quit"), qApp, &QApplication::quit);
    m_tray->setContextMenu(menu);

    connect(m_tray, &QSystemTrayIcon::activated,
            this, &Platform::onTrayActivated);
#endif
    return true;
}

bool Platform::trayVisible() const { 
    return m_trayVisible; 
}

void Platform::setTrayVisible(bool visible) {
    m_trayVisible = visible;
    if (m_tray) {
#ifdef KF6_VERSION
        m_tray->setStatus(visible ? KStatusNotifierItem::Active : KStatusNotifierItem::Passive);
#else
        m_tray->setVisible(visible);
#endif
    }
    emit trayChanged();
}

void Platform::notify(const QString &title, const QString &body,
                      const QString &icon, int timeout) {
    Q_UNUSED(timeout)
#ifdef KF6_VERSION
    KNotification *notify = new KNotification(QStringLiteral("playbackStatus"), this);
    notify->setTitle(title);
    notify->setText(body);
    notify->setIconName(icon);
    notify->sendEvent();
#else
    QProcess::startDetached("notify-send", QStringList() << "-i" << icon << title << body);
#endif
}

void Platform::notifyError(const QString &message) {
    notify(tr("Error"), message, "dialog-error");
}

void Platform::notifyTrackChange(const QString &title, const QString &artist) {
    QString body = artist.isEmpty() ? title : tr("%1 - %2").arg(title, artist);
    notify(tr("Now Playing"), body, "media-playback-start");
}

bool Platform::setupMpris() {
    if (!m_player) return false;

    if (m_mprisAdaptor) {
        delete m_mprisAdaptor;
    }

    m_mprisAdaptor = new MprisAdaptor(m_player, this);
    
    QDBusConnection conn = QDBusConnection::sessionBus();
    bool success = conn.registerService("org.mpris.MediaPlayer2.aegis");
    if (success) {
        success = conn.registerObject("/org/mpris/MediaPlayer2", m_player);
    }
    
    m_mprisEnabled = success;
    if (success) {
        emit mprisChanged();
    }
    
    return success;
}

void Platform::unregisterDBusServices() {
    QDBusConnection conn = QDBusConnection::sessionBus();
    conn.unregisterObject("/org/mpris/MediaPlayer2");
    conn.unregisterService("org.mpris.MediaPlayer2.aegis");
    
    delete m_mprisAdaptor;
    m_mprisAdaptor = nullptr;
    m_mprisEnabled = false;
}

void Platform::onTrayActivated(int reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        emit trayActivated();
    }
}

void Platform::onMprisPropertyChanged() {
    // Notify MPRIS listeners of property changes
    if (m_mprisAdaptor) {
        // The adaptor will emit property change signals
    }
}

void Platform::updateTrayIcon() {
    // Update tray icon based on playback state
    if (!m_tray) return;
    
    bool playing = m_player ? m_player->property("playing").toBool() : false;
    QString iconName = playing ? "media-playback-start" : "media-playback-pause";
    
#ifdef KF6_VERSION
    m_tray->setIconName(iconName);
#else
    m_tray->setIcon(QIcon::fromTheme(iconName));
#endif
}

QStringList Platform::audioOutputDevices() const {
    // TODO: Implement audio device enumeration
    return QStringList();
}

void Platform::setAudioOutputDevice(const QString &device) {
    Q_UNUSED(device)
    // TODO: Implement audio device selection
}

void Platform::setNotificationsEnabled(bool enabled) {
    m_notificationsEnabled = enabled;
    emit notificationsChanged();
}

// ============================================================================
// MprisAdaptor Implementation
// ============================================================================

MprisAdaptor::MprisAdaptor(QObject *player, QObject *parent)
    : QDBusAbstractAdaptor(parent), m_player(player) {}

QString MprisAdaptor::playbackStatus() const {
    if (!m_player) return QStringLiteral("Stopped");
    return m_player->property("playing").toBool() ? QStringLiteral("Playing") : QStringLiteral("Paused");
}

QVariantMap MprisAdaptor::metadata() const {
    QVariantMap map;
    if (!m_player) return map;

    QString file = m_player->property("currentFile").toString();
    QFileInfo fi(file);
    map["xesam:title"] = fi.baseName();
    map["xesam:url"] = QUrl::fromLocalFile(file).toString();
    map["mpris:length"] = qlonglong(m_player->property("duration").toDouble() * 1000000);
    map["xesam:trackId"] = QDBusObjectPath("/org/aegis/Track/0");
    return map;
}

double MprisAdaptor::volume() const {
    if (!m_player) return 1.0;
    return m_player->property("volume").toDouble() / 100.0;
}

void MprisAdaptor::setVolume(double vol) {
    if (m_player) {
        QMetaObject::invokeMethod(m_player, "setVolume", Q_ARG(double, vol * 100.0));
        emit volumeChanged();
    }
}

qlonglong MprisAdaptor::position() const {
    if (!m_player) return 0;
    return qlonglong(m_player->property("position").toDouble() * 1000000);
}

void MprisAdaptor::Play() {
    QMetaObject::invokeMethod(m_player, "play");
    emit playbackStatusChanged();
}

void MprisAdaptor::Pause() {
    QMetaObject::invokeMethod(m_player, "pause");
    emit playbackStatusChanged();
}

void MprisAdaptor::PlayPause() {
    QMetaObject::invokeMethod(m_player, "playPause");
    emit playbackStatusChanged();
}

void MprisAdaptor::Stop() {
    QMetaObject::invokeMethod(m_player, "stop");
    emit playbackStatusChanged();
}

void MprisAdaptor::Next() { 
    QMetaObject::invokeMethod(m_player, "next"); 
}

void MprisAdaptor::Previous() { 
    QMetaObject::invokeMethod(m_player, "previous"); 
}

void MprisAdaptor::Seek(qlonglong offset) {
    double pos = m_player->property("position").toDouble();
    pos += offset / 1000000.0;
    QMetaObject::invokeMethod(m_player, "seek", Q_ARG(double, pos));
    emit positionChanged();
}

void MprisAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong pos) {
    Q_UNUSED(trackId)
    QMetaObject::invokeMethod(m_player, "seek", Q_ARG(double, pos / 1000000.0));
    emit positionChanged();
}

void MprisAdaptor::OpenUri(const QString &uri) {
    QMetaObject::invokeMethod(m_player, "load", Q_ARG(QUrl, QUrl(uri)));
}

void MprisAdaptor::Quit() { 
    QCoreApplication::quit(); 
}

void MprisAdaptor::Raise() { 
    emit qobject_cast<Platform*>(parent())->trayActivated(); 
}

// ============================================================================
// AegisAdminAdaptor Implementation
// ============================================================================

AegisAdminAdaptor::AegisAdminAdaptor(Aegis::Core *core, Aegis::Library *library,
                                     QObject *parent)
    : QDBusAbstractAdaptor(parent), m_core(core), m_library(library) {
    m_settings = new QSettings("Aegis", "Aegis", this);
}

int AegisAdminAdaptor::trackCount() const {
    return m_library ? m_library->trackCount() : 0;
}

bool AegisAdminAdaptor::playing() const {
    return m_core ? m_core->playing() : false;
}

double AegisAdminAdaptor::volume() const {
    return m_core ? m_core->volume() : 0.0;
}

QString AegisAdminAdaptor::currentFile() const {
    return m_core ? m_core->currentFile() : QString();
}

void AegisAdminAdaptor::deleteTrack(int trackId) {
    if (!m_library) {
        emit error("Library not available");
        return;
    }
    m_library->deleteTrack(trackId);
}

QString AegisAdminAdaptor::getLibraryStats() {
    QJsonObject stats;
    stats["trackCount"] = trackCount();
    stats["scanning"] = m_library ? m_library->scanning() : false;
    stats["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/library.db";
    QFileInfo dbInfo(dbPath);
    stats["dbSizeBytes"] = dbInfo.exists() ? dbInfo.size() : 0;

    return QString::fromUtf8(QJsonDocument(stats).toJson(QJsonDocument::Compact));
}

QString AegisAdminAdaptor::getPlaybackStatus() {
    QJsonObject status;
    status["playing"] = playing();
    status["position"] = m_core ? m_core->position() : 0.0;
    status["duration"] = m_core ? m_core->duration() : 0.0;
    status["volume"] = volume();
    status["currentFile"] = currentFile();
    status["hasVideo"] = m_core ? m_core->hasVideo() : false;
    return QString::fromUtf8(QJsonDocument(status).toJson(QJsonDocument::Compact));
}

void AegisAdminAdaptor::playPause() {
    if (m_core) QMetaObject::invokeMethod(m_core, "playPause", Qt::QueuedConnection);
}

void AegisAdminAdaptor::setVolume(double vol) {
    if (m_core) QMetaObject::invokeMethod(m_core, "setVolume", Qt::QueuedConnection, Q_ARG(double, vol));
}

void AegisAdminAdaptor::scanDirectory(const QString &path) {
    if (m_library && !path.isEmpty()) {
        QFileInfo info(path);
        if (info.exists() && info.isDir() && info.isReadable()) {
            QMetaObject::invokeMethod(m_library, "scanDirectory", Qt::QueuedConnection, Q_ARG(QString, path));
        } else {
            emit error("Invalid or inaccessible directory: " + path);
        }
    }
}

QString AegisAdminAdaptor::getConfig(const QString &key) {
    return m_settings->value(key).toString();
}

void AegisAdminAdaptor::setConfig(const QString &key, const QString &value) {
    static const QStringList allowedKeys = {
        "library/autoScan", "playback/defaultVolume", "ui/theme",
        "audio/outputDevice", "network/proxy"
    };

    if (allowedKeys.contains(key)) {
        m_settings->setValue(key, value);
        m_settings->sync();
    } else {
        emit error("Config key not in whitelist: " + key);
    }
}
