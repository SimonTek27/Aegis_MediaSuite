#pragma once

#include <QObject>
#include <QDBusInterface>
#include <QDBusArgument>

struct KdeConnectDevice {
    QString id;
    QString name;
    QString type; // "phone", "tablet", "computer"
    bool isPaired;
    bool isReachable;
    bool hasSharePlugin;
};

Q_DECLARE_METATYPE(KdeConnectDevice)

class KDEConnect : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

public:
    explicit KDEConnect(QObject *parent = nullptr);

    bool available() const;
    QVariantList devices() const;

    // Send media to specific device or broadcast
    Q_INVOKABLE bool sendFile(const QString &filePath, const QString &deviceId = QString());
    Q_INVOKABLE void sendToAll(const QString &filePath);
    Q_INVOKABLE bool pingDevice(const QString &deviceId, const QString &message = "Ping from Aegis");

    // Request photo from device (advanced)
    Q_INVOKABLE void requestPhoto(const QString &deviceId);

public slots:
    // Handle files shared from phone to Aegis
    void handleShare(const QString &url, const QString &deviceName);
    void refreshDevices();

signals:
    void availabilityChanged();
    void devicesChanged();
    void fileReceived(const QString &path, const QString &fromDevice);
    void shareError(const QString &error);
    void shareSuccess(const QString &deviceName);

private slots:
    void onDeviceListChanged();

private:
    void parseDeviceList(const QDBusArgument &arg);
    QDBusInterface *m_daemon = nullptr;
    QList<KdeConnectDevice> m_devices;
    QString kdeConnectDownloadPath() const;
};
