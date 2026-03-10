// discburner.cpp
// Optical disc burning implementation for Aegis Multimedia Suite
// Uses Solid framework for device detection and libburn for burning operations
// Design: Thread-safe, sandbox-friendly, KDE/Plasma 6.6 compatible

#include "discburner.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#ifdef __linux__
#include <linux/cdrom.h>
#endif
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
#ifdef HAVE_LIBBURN
#include <libburn.h>
#include <libisofs/libisofs.h>
#endif

// Mutex for libburn initialization (libburn is not thread-safe)
static QMutex s_libburnMutex;
#ifdef HAVE_LIBBURN
static bool s_libburnInitialized = false;
#endif

namespace Aegis {

    /**
     * @brief Enumerate all available optical drives in the system
     * @return List of device paths (e.g., ["/dev/sr0", "/dev/sr1"])
     *
     * Uses Solid framework for hardware enumeration (KF6 standard).
     * Falls back to UDisks2 D-Bus if Solid is unavailable.
     */
    QStringList CDBurner::enumerateDrivesStatic() {
        QStringList devices;

        // Method 1: Use Solid framework (KDE Frameworks 6)
        QList<Solid::Device> allDrives = Solid::Device::listFromType(Solid::DeviceInterface::OpticalDrive);

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
    BurnerCapabilities CDBurner::getCapabilitiesStatic(const QString &device) {
        BurnerCapabilities caps = {};

        if (device.isEmpty()) {
            qWarning() << "Empty device path provided to getCapabilities";
            return caps;
        }

        // Find the Solid device that corresponds to this /dev node
        QString udi;
        QList<Solid::Device> allDrives = Solid::Device::listFromType(Solid::DeviceInterface::OpticalDrive);

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
            // FIX: Create a temporary object to call non-static method
            CDBurner tempBurner;
            return tempBurner.getCapabilitiesViaLibburn(device);
        }

        // Get the Solid device using the UDI
        Solid::Device dev = Solid::Device(udi);
        const Solid::OpticalDrive *optical = dev.as<Solid::OpticalDrive>();

        if (!optical) {
            qWarning() << "Device is not an optical drive:" << device;
            return caps;
        }

        // Query read capabilities - FIX: Remove duplicate Solid:: namespace qualification
        caps.canRead = optical->supportedMedia() & Solid::OpticalDrive::Cdr;

        // Query write capabilities for various media types - FIX: Remove duplicate Solid:: qualification
        caps.canWriteCDR = optical->supportedMedia() & Solid::OpticalDrive::Cdr;
        caps.canWriteCDRW = optical->supportedMedia() & Solid::OpticalDrive::Cdrw;
        caps.canWriteDVDR = optical->supportedMedia() & Solid::OpticalDrive::Dvdr;
        caps.canWriteDVDPlusR = optical->supportedMedia() & Solid::OpticalDrive::Dvdplusr;
        caps.canWriteBD = optical->supportedMedia() & Solid::OpticalDrive::Bdr;

        // Additional DVD formats
        caps.canWriteDVDRAM = optical->supportedMedia() & Solid::OpticalDrive::Dvdram;

        // Get write speeds - FIX: Create temporary object to call non-static methods
        CDBurner tempBurner2;
        caps.maxSpeedCD = tempBurner2.getMaxWriteSpeed(device, "cd");
        caps.maxSpeedDVD = tempBurner2.getMaxWriteSpeed(device, "dvd");
        caps.maxSpeedBD = tempBurner2.getMaxWriteSpeed(device, "bd");

        // Get additional features via UDisks2
        caps = tempBurner2.queryUDisks2Capabilities(device, caps);

        qDebug() << "Drive capabilities for" << device << ":"
        << "CD-R:" << caps.canWriteCDR
        << "CD-RW:" << caps.canWriteCDRW
        << "DVD-R:" << caps.canWriteDVDR
        << "BD:" << caps.canWriteBD
        << "CD max speed:" << caps.maxSpeedCD << "KB/s";

        return caps;
    }

    // ── QML-invokable wrappers ────────────────────────────────────────────────
    QVariantMap CDBurner::getCapabilities(const QString &device) {
        const QString dev = device.isEmpty() ? m_currentDevice : device;
        BurnerCapabilities caps = getCapabilitiesStatic(dev);
        QVariantMap map;
        map["canRead"]              = caps.canRead;
        map["canBurnCD"]            = caps.canWriteCDR || caps.canWriteCDRW;
        map["canBurnDVD"]           = caps.canWriteDVDR || caps.canWriteDVDPlusR || caps.canWriteDVDRAM;
        map["canBurnBD"]            = caps.canWriteBD || caps.canWriteBDR || caps.canWriteBDRE;
        map["canWriteCDR"]          = caps.canWriteCDR;
        map["canWriteCDRW"]         = caps.canWriteCDRW;
        map["canWriteDVDR"]         = caps.canWriteDVDR;
        map["canWriteDVDPlusR"]     = caps.canWriteDVDPlusR;
        map["canWriteBDR"]          = caps.canWriteBDR;
        map["maxWriteSpeedCD"]      = caps.maxSpeedCD;
        map["maxWriteSpeedDVD"]     = caps.maxSpeedDVD;
        map["maxWriteSpeedBD"]      = caps.maxSpeedBD;
        map["supportsBurnProof"]    = caps.supportsBurnProof;
        return map;
    }

    void CDBurner::startBurn(const QVariantMap &jobMap) {
        BurnJob job;
        job.device      = jobMap.value("device", m_currentDevice).toString();
        job.volumeLabel = jobMap.value("volumeLabel", "Aegis Disc").toString();
        job.speed       = BurnSpeed::Auto;
        // Delegate to internal implementation
        startBurn(job);
    }

    /**
     * @brief Get drive capabilities using libburn (fallback method)
     */
    BurnerCapabilities CDBurner::getCapabilitiesViaLibburn(const QString &device) {
        BurnerCapabilities caps = {};
        bool found = false; // declared before #ifdef so visible after #endif

        QMutexLocker locker(&s_libburnMutex);

        #ifdef HAVE_LIBBURN
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
        #endif // HAVE_LIBBURN

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

        // FIX: Get the object path properly - GetManagedObjects returns an array of object paths
        QDBusObjectPath objectPath = reply.value();

        // For UDisks2, we need to get the list of objects differently
        // Let's query the manager for block devices instead
        QDBusInterface udisksManager("org.freedesktop.UDisks2",
                                     "/org/freedesktop/UDisks2/Manager",
                                     "org.freedesktop.UDisks2.Manager",
                                     QDBusConnection::systemBus());

        if (udisksManager.isValid()) {
            QDBusReply<QList<QDBusObjectPath>> blockDevicesReply = udisksManager.call("GetBlockDevices", QVariantMap());

            if (blockDevicesReply.isValid()) {
                QList<QDBusObjectPath> objects = blockDevicesReply.value();

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
        QList<Solid::Device> allDrives = Solid::Device::listFromType(Solid::DeviceInterface::OpticalDrive);

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

                    const_cast<Solid::OpticalDrive*>(optical)->eject();
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
    bool CDBurner::initializeLibburn() { // FIX: Return type should be bool
        QMutexLocker locker(&s_libburnMutex);
        #ifdef HAVE_LIBBURN
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
        #else
        return false;
        #endif // HAVE_LIBBURN
    }

    /**
     * @brief Cleanup libburn resources
     */
    void CDBurner::cleanup() {
        QMutexLocker locker(&s_libburnMutex);
        #ifdef HAVE_LIBBURN
        if (s_libburnInitialized) {
            burn_finish();
            s_libburnInitialized = false;
            qDebug() << "libburn cleanup completed";
        }
        #endif // HAVE_LIBBURN
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
        QList<Solid::Device> allDrives = Solid::Device::listFromType(Solid::DeviceInterface::OpticalDrive);

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

                // Get media type - FIX: media() method doesn't exist, use supportedMedia()
                const Solid::OpticalDrive *optical = dev.as<Solid::OpticalDrive>();
                if (optical) {
                    auto mediaTypes = optical->supportedMedia();

                    if (mediaTypes & Solid::OpticalDrive::Cdr) info["mediaType"] = "CD-R";
                    else if (mediaTypes & Solid::OpticalDrive::Cdrw) info["mediaType"] = "CD-RW";
                    else if (mediaTypes & Solid::OpticalDrive::Dvdr) info["mediaType"] = "DVD-R";
                    else if (mediaTypes & Solid::OpticalDrive::Dvdplusr) info["mediaType"] = "DVD+R";
                    else if (mediaTypes & Solid::OpticalDrive::Bdr) info["mediaType"] = "BD-R";
                    else info["mediaType"] = "Unknown/CD-ROM";
                }

                break;
            }
        }

        // If Solid didn't give us media type, try libburn
        if (!info.contains("mediaType")) {
            QMutexLocker locker(&s_libburnMutex);

            #ifdef HAVE_LIBBURN
            if (!s_libburnInitialized && !initializeLibburn()) {
                return info;
            }

            struct burn_drive *drive = burn_drive_find(device.toUtf8().constData());
            if (drive) {
                int ret = burn_drive_grab(drive, 1);
                if (ret > 0) {
                    ret = burn_disc_read_profile(drive);
                    if (ret > 0) {
                        switch (drive->profile) {
                            case 0x08: info["mediaType"] = "CD-R"; break;
                            case 0x09: info["mediaType"] = "CD-RW"; break;
                            case 0x10: info["mediaType"] = "DVD-ROM"; break;
                            case 0x11: info["mediaType"] = "DVD-R"; break;
                            case 0x1A: info["mediaType"] = "BD-R"; break;
                            default: info["mediaType"] = "Unknown"; break;
                        }
                        info["capacityBytes"] = (qint64)drive->sectors * 2048;
                    }
                    burn_drive_release(drive, 0);
                }
            }
            #endif // HAVE_LIBBURN
        }

        return info;
    }

    // ─── CDBurner constructor / destructor ────────────────────────────────────

    CDBurner::CDBurner(QObject* parent)
        : QObject(parent)
    {}

    CDBurner::~CDBurner() {
        cleanup();
    }

    // ─── BurnWorker::run ─────────────────────────────────────────────────────

    BurnWorker::BurnWorker(const BurnJob& job, QObject* parent)
        : QThread(parent), m_job(job)
    {}

    void BurnWorker::run() {
        emit logMessage(QStringLiteral("Burn started"), false);
        bool ok = false;
        switch (m_job.type) {
            case BurnType::AudioCD:   ok = burnAudioCD();   break;
            case BurnType::DataCD:    ok = burnDataCD();    break;
            case BurnType::ISOImage:  ok = burnISO();       break;
            case BurnType::DVDVideo:  ok = burnDVDVideo();  break;
            default: break;
        }
        emit burnCompleted(ok, ok ? QStringLiteral("Done") : QStringLiteral("Failed"));
    }

    void BurnWorker::cancel() { m_cancel.store(true); }

    bool BurnWorker::setupDrive()    { return false; }
    bool BurnWorker::burnAudioCD()   { emit progress(0, "stub"); return false; }
    bool BurnWorker::burnDataCD()    { emit progress(0, "stub"); return false; }
    bool BurnWorker::burnISO()       { emit progress(0, "stub"); return false; }
    bool BurnWorker::burnDVDVideo()  { emit progress(0, "stub"); return false; }
    int  BurnWorker::speedToMultiplier(BurnSpeed, bool) { return 1; }

    // ─── CDBurner public Q_INVOKABLE stubs ───────────────────────────────────

    void CDBurner::onBurnCompleted(bool ok, const QString& msg) {
        emit burnFinished(ok, msg);
    }
    void CDBurner::onProgress(int pct, const QString& msg) {
        emit burnProgress(pct, msg);
    }
    void CDBurner::burnAudioFromPlaylist(const QString& drive, const QString& playlist, BurnSpeed speed) {
        BurnJob job;
        job.type = BurnType::AudioCD;
        job.device = drive;
        job.volumeLabel = playlist;
        job.speed = speed;
        startBurn(job);
    }
    void CDBurner::burnAudioFromTracks(const QList<QVariant>& tracks, const QString& drive, const QString& label) {
        Q_UNUSED(tracks)
        BurnJob job; job.type = BurnType::AudioCD; job.device = drive; job.volumeLabel = label;
        startBurn(job);
    }
    void CDBurner::burnFiles(const QList<QString>& files, const QString& drive, const QString& label, bool dvd) {
        Q_UNUSED(files)
        BurnJob job; job.type = dvd ? BurnType::DVDVideo : BurnType::DataCD;
        job.device = drive; job.volumeLabel = label;
        startBurn(job);
    }
    void CDBurner::burnISO(const QString& isoPath, const QString& drive, bool verify) {
        Q_UNUSED(verify)
        BurnJob job; job.type = BurnType::ISOImage; job.isoPath = isoPath; job.device = drive;
        startBurn(job);
    }
    void CDBurner::burnDVDVideo(const QString& videoDir, const QString& drive) {
        BurnJob job; job.type = BurnType::DVDVideo; job.isoPath = videoDir; job.device = drive;
        startBurn(job);
    }
    void CDBurner::blankCDRW(const QString& drive, bool fast) {
        Q_UNUSED(drive) Q_UNUSED(fast)
    }
    void CDBurner::formatDVDPlusRW(const QString& drive) { Q_UNUSED(drive) }
    void CDBurner::closeTray(const QString& drive)       { Q_UNUSED(drive) }
    void CDBurner::createISOFromFiles(const QList<QString>& files, const QString& isoPath, const QString& label) {
        Q_UNUSED(files) Q_UNUSED(isoPath) Q_UNUSED(label)
    }
    void CDBurner::cancelBurn() { if (m_worker) m_worker->cancel(); }
    void CDBurner::setCDText(const QMap<QString, QVariant>& albumMeta, const QList<QVariant>& trackMeta) {
        Q_UNUSED(albumMeta) Q_UNUSED(trackMeta)
    }

    void CDBurner::refreshDrives() {
        m_availableDrives = enumerateDrives();
        emit drivesChanged();
    }

    bool CDBurner::mediaPresent(const QString& device) {
        QVariantMap info = mediaInfo(device);
        bool present = info.contains("mediaType");
        if (present != m_discPresent) {
            m_discPresent = present;
            emit mediaStatusChanged(m_discPresent);
        }
        return present;
    }

    void CDBurner::startBurn(const BurnJob &job) {
        Q_UNUSED(job)
        // Placeholder implementation: emit burnFinished immediately
        emit burnFinished(false, QStringLiteral("Burn subsystem not implemented in this build."));
    }

} // namespace Aegis

