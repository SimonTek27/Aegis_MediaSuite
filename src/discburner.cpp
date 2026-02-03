// discburner.cpp
// Optical disc burning implementation for Aegis Multimedia Suite
// Uses Solid framework for device detection and libburn for burning operations
// Design: Thread-safe, sandbox-friendly, KDE/Plasma 6.6 compatible

#include "discburner.h"
#include <Solid/Device>
#include <Solid/OpticalDrive>
#include <Solid/Block>
#include <Solid/StorageAccess>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QMutexLocker>
#include <libburn/libburn.h>
#include <libisofs/libisofs.h>

// Mutex for libburn initialization (libburn is not thread-safe)
static QMutex s_libburnMutex;
static bool s_libburnInitialized = false;

namespace Aegis {

    /**
     * @brief Enumerate all available optical drives in the system
     * @return List of device paths (e.g., ["/dev/sr0", "/dev/sr1"])
     *
     * Uses Solid framework for hardware enumeration (KF6 standard).
     * Falls back to UDisks2 D-Bus if Solid is unavailable.
     */
    QStringList CDBurner::enumerateDrives() {
        QStringList devices;

        // Method 1: Use Solid framework (KDE Frameworks 6)
        Solid::DeviceList allDrives = Solid::Device::listFromType(Solid::DeviceInterface::OpticalDrive);

        qDebug() << "Solid found" << allDrives.size() << "optical drive(s)";

        for (const Solid::Device &device : allDrives) {
            const Solid::Block *block = device.as<Solid::Block>();
            if (block) {
                QString devNode = block->device();
                if (!devNode.isEmpty()) {
                    devices.append(devNode);
                    qDebug() << "Found optical drive via Solid:" << devNode
                    << "(" << device.displayName() << ")";
                } else {
                    qWarning() << "Solid device has empty device node:" << device.udi();
                }
            } else {
                qWarning() << "Solid device is not a Block device:" << device.udi();
            }
        }

        // Method 2: Fallback to UDisks2 D-Bus API
        if (devices.isEmpty()) {
            qDebug() << "No drives found via Solid, trying UDisks2...";

            QDBusInterface udisks2Interface("org.freedesktop.UDisks2",
                                            "/org/freedesktop/UDisks2/Manager",
                                            "org.freedesktop.UDisks2.Manager",
                                            QDBusConnection::systemBus());

            if (udisks2Interface.isValid()) {
                QDBusReply<QList<QDBusObjectPath>> reply = udisks2Interface.call("GetBlockDevices", QVariantMap());

                if (reply.isValid()) {
                    QList<QDBusObjectPath> blockDevices = reply.value();

                    for (const QDBusObjectPath &objPath : blockDevices) {
                        QDBusInterface driveIface("org.freedesktop.UDisks2",
                                                  objPath.path(),
                                                  "org.freedesktop.DBus.Properties",
                                                  QDBusConnection::systemBus());

                        // Check if this is an optical drive
                        QDBusReply<QVariant> mediaReply = driveIface.call("Get",
                                                                          "org.freedesktop.UDisks2.Drive",
                                                                          "MediaCompatibility");
                        if (mediaReply.isValid()) {
                            QStringList mediaTypes = mediaReply.value().toStringList();
                            bool isOptical = false;

                            for (const QString &type : mediaTypes) {
                                if (type.contains("optical", Qt::CaseInsensitive)) {
                                    isOptical = true;
                                    break;
                                }
                            }

                            if (isOptical) {
                                // Get device node
                                QDBusReply<QVariant> devReply = driveIface.call("Get",
                                                                                "org.freedesktop.UDisks2.Block",
                                                                                "Device");
                                if (devReply.isValid()) {
                                    QByteArray devData = devReply.value().toByteArray();
                                    QString devNode = QString::fromLocal8Bit(devData.constData());
                                    if (!devNode.isEmpty()) {
                                        devices.append(devNode);
                                        qDebug() << "Found optical drive via UDisks2:" << devNode;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Method 3: Check common device nodes (last resort)
        if (devices.isEmpty()) {
            qDebug() << "No drives found via APIs, checking common device nodes...";

            QStringList commonNodes = {
                "/dev/sr0", "/dev/sr1", "/dev/sr2",
                "/dev/cdrom", "/dev/cdrw", "/dev/dvd",
                "/dev/sr", "/dev/scd0", "/dev/scd1"
            };

            for (const QString &node : commonNodes) {
                if (QFile::exists(node)) {
                    devices.append(node);
                    qDebug() << "Found optical drive via device node:" << node;
                }
            }
        }

        if (devices.isEmpty()) {
            qWarning() << "No optical drives found in the system";
        } else {
            qDebug() << "Total optical drives found:" << devices.size() << devices;
        }

        return devices;
    }

    /**
     * @brief Query capabilities of a specific optical drive
     * @param device Device path (e.g., "/dev/sr0")
     * @return BurnerCapabilities structure with supported features
     *
     * Determines which media types the drive can read/write and maximum speeds.
     */
    BurnerCapabilities CDBurner::getCapabilities(const QString &device) {
        BurnerCapabilities caps = {};

        if (device.isEmpty()) {
            qWarning() << "Empty device path provided to getCapabilities";
            return caps;
        }

        // Find the Solid device that corresponds to this /dev node
        QString udi;
        Solid::DeviceList allDrives = Solid::Device::listFromType(Solid::DeviceInterface::OpticalDrive);

        for (const Solid::Device &dev : allDrives) {
            const Solid::Block *block = dev.as<Solid::Block>();
            if (block && block->device() == device) {
                udi = dev.udi();
                break;
            }
        }

        if (udi.isEmpty()) {
            qWarning() << "Could not find Solid UDI for device:" << device;

            // Try to get basic capabilities via libburn instead
            return getCapabilitiesViaLibburn(device);
        }

        // Get the Solid device using the UDI
        Solid::Device dev = Solid::Device(udi);
        const Solid::OpticalDrive *optical = dev.as<Solid::OpticalDrive>();

        if (!optical) {
            qWarning() << "Device is not an optical drive:" << device;
            return caps;
        }

        // Query read capabilities
        caps.canRead = optical->supportsMedia(Solid::OpticalDrive::Cdr);

        // Query write capabilities for various media types
        caps.canWriteCDR = optical->supportsMedia(Solid::OpticalDrive::Cdr);
        caps.canWriteCDRW = optical->supportsMedia(Solid::OpticalDrive::Cdrw);
        caps.canWriteDVDR = optical->supportsMedia(Solid::OpticalDrive::Dvdr);
        caps.canWriteDVDPlusR = optical->supportsMedia(Solid::OpticalDrive::Dvdplusr);
        caps.canWriteBD = optical->supportsMedia(Solid::OpticalDrive::Bdr);

        // Additional DVD formats
        caps.canWriteDVDRAM = optical->supportsMedia(Solid::OpticalDrive::Dvdram);

        // Get write speeds - Solid doesn't expose these directly, so we need other methods
        caps.maxSpeedCD = getMaxWriteSpeed(device, "cd");
        caps.maxSpeedDVD = getMaxWriteSpeed(device, "dvd");
        caps.maxSpeedBD = getMaxWriteSpeed(device, "bd");

        // Get additional features via UDisks2
        caps = queryUDisks2Capabilities(device, caps);

        qDebug() << "Drive capabilities for" << device << ":"
        << "CD-R:" << caps.canWriteCDR
        << "CD-RW:" << caps.canWriteCDRW
        << "DVD-R:" << caps.canWriteDVDR
        << "BD:" << caps.canWriteBD
        << "CD max speed:" << caps.maxSpeedCD << "KB/s";

        return caps;
    }

    /**
     * @brief Get drive capabilities using libburn (fallback method)
     */
    BurnerCapabilities CDBurner::getCapabilitiesViaLibburn(const QString &device) {
        BurnerCapabilities caps = {};

        QMutexLocker locker(&s_libburnMutex);

        if (!s_libburnInitialized) {
            burn_initialize();
            s_libburnInitialized = true;
        }

        // Create a burn_drive_info list
        struct burn_drive_info *drives = NULL;
        int drive_count = burn_drive_scan(&drives, 1);

        if (drive_count <= 0) {
            qWarning() << "libburn found no drives";
            return caps;
        }

        // Find our drive
        struct burn_drive_info *drive = drives;
        bool found = false;

        while (drive && !found) {
            if (QString::fromLocal8Bit(drive->location) == device) {
                found = true;

                // Check capabilities
                caps.canRead = true; // libburn drives can always read

                // Check write capabilities based on drive profile
                struct burn_write_capabilities *wcaps = drive->write_capabilities;
                while (wcaps) {
                    if (wcaps->profile == 0x08) caps.canWriteCDR = true; // CD-R
                    if (wcaps->profile == 0x09) caps.canWriteCDRW = true; // CD-RW
                    if (wcaps->profile == 0x11) caps.canWriteDVDR = true; // DVD-R
                    if (wcaps->profile == 0x1A) caps.canWriteBD = true; // BD-R
                    if (wcaps->profile == 0x12) caps.canWriteDVDRAM = true; // DVD-RAM

                    caps.maxSpeedCD = qMax(caps.maxSpeedCD, wcaps->speed_kb);
                    wcaps = wcaps->next;
                }
            }
            drive = drive->next;
        }

        burn_drive_info_free(drives);

        if (!found) {
            qWarning() << "libburn could not find drive:" << device;
        }

        return caps;
    }

    /**
     * @brief Get maximum write speed for a specific media type
     */
    int CDBurner::getMaxWriteSpeed(const QString &device, const QString &mediaType) {
        // Try to get speed from UDisks2 first
        QDBusInterface driveIface("org.freedesktop.UDisks2",
                                  QString("/org/freedesktop/UDisks2/drives/%1").arg(QFileInfo(device).fileName()),
                                  "org.freedesktop.DBus.Properties",
                                  QDBusConnection::systemBus());

        if (driveIface.isValid()) {
            QString propertyName;
            if (mediaType == "cd") propertyName = "WriteSpeedMaxCD";
            else if (mediaType == "dvd") propertyName = "WriteSpeedMaxDVD";
            else if (mediaType == "bd") propertyName = "WriteSpeedMaxBD";

            if (!propertyName.isEmpty()) {
                QDBusReply<QVariant> reply = driveIface.call("Get",
                                                             "org.freedesktop.UDisks2.Drive",
                                                             propertyName);
                if (reply.isValid()) {
                    return reply.value().toInt();
                }
            }
        }

        // Fallback: try to read from /proc or /sys
        QString sysPath = QString("/sys/block/%1/device/speed_max")
        .arg(QFileInfo(device).fileName());

        if (QFile::exists(sysPath)) {
            QFile speedFile(sysPath);
            if (speedFile.open(QIODevice::ReadOnly)) {
                QString speedStr = QString::fromLocal8Bit(speedFile.readAll()).trimmed();
                bool ok;
                int speed = speedStr.toInt(&ok);
                if (ok && speed > 0) {
                    // Convert from 1x units (KB/s for CD, different for DVD/BD)
                    if (mediaType == "cd") return speed * 176; // 1x CD = 176 KB/s
                    else if (mediaType == "dvd") return speed * 1385; // 1x DVD = 1385 KB/s
                    else if (mediaType == "bd") return speed * 4500; // 1x BD = 4500 KB/s
                }
            }
        }

        return 0; // Unknown
    }

    /**
     * @brief Query additional capabilities via UDisks2
     */
    BurnerCapabilities CDBurner::queryUDisks2Capabilities(const QString &device, BurnerCapabilities caps) {
        // Try to find the UDisks2 object path for this device
        QDBusInterface managerIface("org.freedesktop.UDisks2",
                                    "/org/freedesktop/UDisks2",
                                    "org.freedesktop.DBus.ObjectManager",
                                    QDBusConnection::systemBus());

        if (!managerIface.isValid()) {
            return caps;
        }

        QDBusReply<QDBusObjectPath> reply = managerIface.call("GetManagedObjects");
        if (!reply.isValid()) {
            return caps;
        }

        // Search for the drive with matching device
        QList<QDBusObjectPath> objects = reply.value();

        for (const QDBusObjectPath &objPath : objects) {
            QDBusInterface driveIface("org.freedesktop.UDisks2",
                                      objPath.path(),
                                      "org.freedesktop.DBus.Properties",
                                      QDBusConnection::systemBus());

            QDBusReply<QVariant> devReply = driveIface.call("Get",
                                                            "org.freedesktop.UDisks2.Block",
                                                            "Device");
            if (devReply.isValid()) {
                QByteArray devData = devReply.value().toByteArray();
                QString devNode = QString::fromLocal8Bit(devData.constData());

                if (devNode == device) {
                    // Found our drive, query additional properties
                    QDBusReply<QVariant> bufReply = driveIface.call("Get",
                                                                    "org.freedesktop.UDisks2.Drive",
                                                                    "Configuration");
                    if (bufReply.isValid()) {
                        QVariantMap config = bufReply.value().toMap();

                        // Check for buffer underrun protection
                        caps.supportsBurnProof = config.value("BurnProof", false).toBool();
                        caps.supportsSolidBurn = config.value("SolidBurn", false).toBool();
                    }

                    break;
                }
            }
        }

        return caps;
    }

    /**
     * @brief Eject media from an optical drive
     * @param device Device path (e.g., "/dev/sr0")
     *
     * Uses Solid framework to eject the media tray.
     * Falls back to direct ioctl if Solid is not available.
     */
    void CDBurner::eject(const QString &device) {
        if (device.isEmpty()) {
            qWarning() << "Empty device path provided to eject";
            return;
        }

        // Method 1: Use Solid framework
        Solid::DeviceList allDrives = Solid::Device::listFromType(Solid::DeviceInterface::OpticalDrive);

        for (const Solid::Device &dev : allDrives) {
            const Solid::Block *block = dev.as<Solid::Block>();
            if (block && block->device() == device) {
                const Solid::OpticalDrive *optical = dev.as<Solid::OpticalDrive>();
                if (optical) {
                    qDebug() << "Ejecting media from" << device << "via Solid";

                    // Connect to completion signal
                    connect(optical, &Solid::OpticalDrive::ejectDone,
                            [device](Solid::ErrorType error, const QVariant &errorData, const QString &udi) {
                                if (error == Solid::NoError) {
                                    qDebug() << "Successfully ejected media from:" << device;
                                } else {
                                    qWarning() << "Failed to eject from" << device
                                    << "Error:" << error << "Data:" << errorData;
                                }
                            });

                    optical->eject();
                    return;
                }
            }
        }

        // Method 2: Use UDisks2
        QDBusInterface driveIface("org.freedesktop.UDisks2",
                                  QString("/org/freedesktop/UDisks2/drives/%1").arg(QFileInfo(device).fileName()),
                                  "org.freedesktop.UDisks2.Drive",
                                  QDBusConnection::systemBus());

        if (driveIface.isValid()) {
            qDebug() << "Ejecting media from" << device << "via UDisks2";
            QDBusReply<void> reply = driveIface.call("Eject", QVariantMap());
            if (!reply.isValid()) {
                qWarning() << "UDisks2 eject failed:" << reply.error().message();
            }
            return;
        }

        // Method 3: Direct ioctl (last resort, requires root)
        qDebug() << "Ejecting media from" << device << "via ioctl";

        #ifdef Q_OS_LINUX
        int fd = open(device.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            if (ioctl(fd, CDROMEJECT) == 0) {
                qDebug() << "ioctl eject succeeded";
            } else {
                qWarning() << "ioctl eject failed:" << strerror(errno);
            }
            close(fd);
        } else {
            qWarning() << "Failed to open device for eject:" << strerror(errno);
        }
        #else
        qWarning() << "Direct eject not supported on this platform";
        #endif
    }

    /**
     * @brief Initialize libburn for burning operations
     * @return True if initialization succeeded
     *
     * Must be called before any burning operations.
     * Thread-safe via static mutex.
     */
    bool CDBurner::initializeLibburn() {
        QMutexLocker locker(&s_libburnMutex);

        if (s_libburnInitialized) {
            return true;
        }

        int ret = burn_initialize();
        if (ret != 1) {
            qCritical() << "Failed to initialize libburn, error:" << ret;
            return false;
        }

        s_libburnInitialized = true;
        qDebug() << "libburn initialized successfully";
        return true;
    }

    /**
     * @brief Cleanup libburn resources
     */
    void CDBurner::cleanupLibburn() {
        QMutexLocker locker(&s_libburnMutex);

        if (s_libburnInitialized) {
            burn_finish();
            s_libburnInitialized = false;
            qDebug() << "libburn cleanup completed";
        }
    }

    /**
     * @brief Get information about media in drive
     * @param device Drive to check
     * @return Map containing media type, capacity, free space, etc.
     */
    QVariantMap CDBurner::mediaInfo(const QString &device) {
        QVariantMap info;

        if (device.isEmpty()) {
            return info;
        }

        // Try Solid first
        Solid::DeviceList allDrives = Solid::Device::listFromType(Solid::DeviceInterface::OpticalDrive);

        for (const Solid::Device &dev : allDrives) {
            const Solid::Block *block = dev.as<Solid::Block>();
            if (block && block->device() == device) {
                const Solid::StorageAccess *access = dev.as<Solid::StorageAccess>();
                if (access && access->isAccessible()) {
                    info["mounted"] = true;
                    info["mountPoint"] = access->filePath();

                    // Get filesystem info
                    struct statvfs vfs;
                    if (statvfs(access->filePath().toUtf8().constData(), &vfs) == 0) {
                        qint64 totalBytes = vfs.f_blocks * vfs.f_frsize;
                        qint64 freeBytes = vfs.f_bfree * vfs.f_frsize;
                        info["totalBytes"] = totalBytes;
                        info["freeBytes"] = freeBytes;
                        info["usedBytes"] = totalBytes - freeBytes;
                    }
                } else {
                    info["mounted"] = false;
                }

                // Get media type
                const Solid::OpticalDrive *optical = dev.as<Solid::OpticalDrive>();
                if (optical) {
                    Solid::OpticalDrive::MediaTypes media = optical->media();

                    if (media & Solid::OpticalDrive::Cdr) info["mediaType"] = "CD-R";
                    else if (media & Solid::OpticalDrive::Cdrw) info["mediaType"] = "CD-RW";
                    else if (media & Solid::OpticalDrive::Dvdr) info["mediaType"] = "DVD-R";
                    else if (media & Solid::OpticalDrive::Dvdplusr) info["mediaType"] = "DVD+R";
                    else if (media & Solid::OpticalDrive::Bdr) info["mediaType"] = "BD-R";
                    else if (media & Solid::OpticalDrive::Cdr) info["mediaType"] = "CD-ROM";
                    else info["mediaType"] = "Unknown";
                }

                break;
            }
        }

        // If Solid didn't give us media type, try libburn
        if (!info.contains("mediaType")) {
            QMutexLocker locker(&s_libburnMutex);

            if (!s_libburnInitialized && !initializeLibburn()) {
                return info;
            }

            struct burn_drive *drive = burn_drive_find(device.toUtf8().constData());
            if (drive) {
                int ret = burn_drive_grab(drive, 1);
                if (ret > 0) {
                    ret = burn_disc_read_profile(drive);
                    if (ret > 0) {
                        // Profile indicates media type
                        switch (drive->profile) {
                            case 0x08: info["mediaType"] = "CD-R"; break;
                            case 0x09: info["mediaType"] = "CD-RW"; break;
                            case 0x10: info["mediaType"] = "DVD-ROM"; break;
                            case 0x11: info["mediaType"] = "DVD-R"; break;
                            case 0x1A: info["mediaType"] = "BD-R"; break;
                            default: info["mediaType"] = "Unknown"; break;
                        }

                        // Get capacity
                        info["capacityBytes"] = (qint64)drive->sectors * 2048;
                    }
                    burn_drive_release(drive, 0);
                }
            }
        }

        return info;
    }

} // namespace Aegis
