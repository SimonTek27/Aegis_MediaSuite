#include "kdeconnect.h"
#include <QDBusConnection>
#include <QDBusReply>
#include <QDBusMetaType>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>

KDEConnect::KDEConnect(QObject *parent) : QObject(parent) {
    // Register meta types for DBus
    qDBusRegisterMetaType<QMap<QString, QVariant>>();
    qDBusRegisterMetaType<QDBusArgument>();

    m_daemon = new QDBusInterface("org.kde.kdeconnect.daemon",
                                  "/modules/kdeconnect",
                                  "org.kde.kdeconnect.daemon",
                                  QDBusConnection::sessionBus(), this);

    if (m_daemon->isValid()) {
        // Monitor device changes
        QDBusConnection::sessionBus().connect("org.kde.kdeconnect.daemon",
                                              "/modules/kdeconnect",
                                              "org.kde.kdeconnect.daemon",
                                              "deviceListChanged",
                                              this, SLOT(onDeviceListChanged()));
        refreshDevices();
    }
}

bool KDEConnect::available() const {
    return m_daemon && m_daemon->isValid();
}

QVariantList KDEConnect::devices() const {
    QVariantList list;
    for (const auto &dev : m_devices) {
        QVariantMap map;
        map["id"] = dev.id;
        map["name"] = dev.name;
        map["type"] = dev.type;
        map["paired"] = dev.isPaired;
        map["reachable"] = dev.isReachable;
        list.append(map);
    }
    return list;
}

void KDEConnect::refreshDevices() {
    if (!available()) return;

    QDBusReply<QDBusArgument> reply = m_daemon->call("devices");
    if (reply.isValid()) {
        parseDeviceList(reply.value());
        emit devicesChanged();
    }
}

void KDEConnect::parseDeviceList(const QDBusArgument &arg) {
    m_devices.clear();
    arg.beginArray();
    while (!arg.atEnd()) {
        QString id;
        arg >> id;

        // Get device properties
        QDBusInterface device("org.kde.kdeconnect.daemon",
                              "/modules/kdeconnect/devices/" + id,
                              "org.kde.kdeconnect.device",
                              QDBusConnection::sessionBus());

        if (device.isValid()) {
            KdeConnectDevice dev;
            dev.id = id;
            dev.name = device.property("name").toString();
            dev.type = device.property("type").toString();
            dev.isPaired = device.property("isPaired").toBool();
            dev.isReachable = device.property("isReachable").toBool();

            // Check if Share plugin is loaded
            QDBusInterface share("org.kde.kdeconnect.daemon",
                                 "/modules/kdeconnect/devices/" + id,
                                 "org.kde.kdeconnect.device.share",
                                 QDBusConnection::sessionBus());
            dev.hasSharePlugin = share.isValid();

            if (dev.isPaired && dev.isReachable) {
                m_devices.append(dev);
            }
        }
    }
    arg.endArray();
}

bool KDEConnect::sendFile(const QString &filePath, const QString &deviceId) {
    if (!available()) {
        emit shareError("KDE Connect not available");
        return false;
    }

    QFileInfo info(filePath);
    if (!info.exists()) {
        emit shareError("File does not exist: " + filePath);
        return false;
    }

    QString targetId = deviceId;

    // If no device specified, pick first available
    if (targetId.isEmpty() && !m_devices.isEmpty()) {
        targetId = m_devices.first().id;
    }

    if (targetId.isEmpty()) {
        emit shareError("No reachable devices found");
        return false;
    }

    QDBusInterface share("org.kde.kdeconnect.daemon",
                         "/modules/kdeconnect/devices/" + targetId,
                         "org.kde.kdeconnect.device.share",
                         QDBusConnection::sessionBus());

    if (!share.isValid()) {
        emit shareError("Share plugin not available on device");
        return false;
    }

    QUrl url = QUrl::fromLocalFile(filePath);
    QDBusReply<bool> reply = share.call("shareUrl", url.toString());

    if (reply.isValid() && reply.value()) {
        QString devName = m_devices.first([targetId](const KdeConnectDevice &d) {
            return d.id == targetId;
        }).name;
        emit shareSuccess(devName);
        return true;
    } else {
        emit shareError("Failed to send file via KDE Connect");
        return false;
    }
}

void KDEConnect::sendToAll(const QString &filePath) {
    for (const auto &dev : m_devices) {
        sendFile(filePath, dev.id);
    }
}

bool KDEConnect::pingDevice(const QString &deviceId, const QString &message) {
    QDBusInterface ping("org.kde.kdeconnect.daemon",
                        "/modules/kdeconnect/devices/" + deviceId,
                        "org.kde.kdeconnect.device.ping",
                        QDBusConnection::sessionBus());

    if (ping.isValid()) {
        ping.call("sendPing", message);
        return true;
    }
    return false;
}

void KDEConnect::onDeviceListChanged() {
    refreshDevices();
}

// Handle incoming files from phone -> Aegis
void KDEConnect::handleShare(const QString &url, const QString &deviceName) {
    QUrl fileUrl(url);
    QString path = fileUrl.toLocalFile();

    if (path.isEmpty()) return;

    // Validate it's a media file
    QStringList mediaExts = {"mp3", "flac", "ogg", "m4a", "wav", "opus", "wma"};
    QFileInfo info(path);

    if (mediaExts.contains(info.suffix().toLower())) {
        emit fileReceived(path, deviceName);
        qDebug() << "Received media file from" << deviceName << ":" << path;
    } else {
        qDebug() << "Received non-media file from" << deviceName << ", ignoring";
    }
}

QString KDEConnect::kdeConnectDownloadPath() const {
    // Standard KDE Connect download location
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/KDE Connect";
}
