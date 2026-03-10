// disc.cpp - Optical disc management implementation
// Fix: file was empty (0 bytes), causing link errors for all disc symbols.

#include "disc.h"
#include <QThread>
#include <QDir>
#include <QRegularExpression>

namespace Aegis {

// ============================================================================
// DiscInfo
// ============================================================================

DiscInfo::DiscInfo() = default;
DiscInfo::~DiscInfo() = default;

QString DiscInfo::discTypeString() const {
    switch (discType) {
        case 0:  return QStringLiteral("CD-ROM");
        case 1:  return QStringLiteral("CD-DA");
        case 2:  return QStringLiteral("CD-Mixed");
        case 3:  return QStringLiteral("DVD");
        case 4:  return QStringLiteral("Blu-ray");
        default: return QStringLiteral("Unknown");
    }
}

bool DiscInfo::isAudioCD()  const { return discType == 1 || discType == 2; }
bool DiscInfo::isVideoDVD() const { return discType == 3; }
bool DiscInfo::isBluRay()   const { return discType == 4; }

qint64 DiscInfo::totalAudioDuration() const {
    qint64 total = 0;
    for (const auto& t : tracks)
        if (t.isAudio) total += t.duration;
    return total;
}

// ============================================================================
// DiscRipper
// ============================================================================

DiscRipper::DiscRipper(const QString& device, QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_logger(QStringLiteral("DiscRipper"))
{
    if (m_device.isEmpty())
        m_device = findDefaultDrive();
}

DiscRipper::~DiscRipper() { cleanupWorker(); }

QString DiscRipper::device()   const { return m_device; }
bool    DiscRipper::isWorking() const { return m_worker && m_worker->isRunning(); }

void DiscRipper::setDevice(const QString& device) {
    if (m_device != device) { m_device = device; emit deviceChanged(); }
}

Result<DiscInfo> DiscRipper::scanDiscSync() {
    return Result<DiscInfo>::error(QStringLiteral("libcdio backend not linked in this build"));
}

Result<RipResult> DiscRipper::ripTrackSync(int, const RipOptions&) {
    return Result<RipResult>::error(QStringLiteral("libcdio backend not linked in this build"));
}

void DiscRipper::scanDiscAsync() {
    if (isWorking()) { emit error(tr("Busy")); return; }
    emit workingChanged(true);
    QThread::create([this] {
        auto r = scanDiscSync();
        if (r.isSuccess()) emit scanCompleted(r.value());
        else        emit error(r.error());
        emit workingChanged(false);
    })->start();
}

void DiscRipper::ripTrackAsync(int track, const RipOptions& opts) {
    if (isWorking()) { emit error(tr("Busy")); return; }
    emit workingChanged(true);
    QThread::create([this, track, opts] {
        auto r = ripTrackSync(track, opts);
        if (r.isSuccess()) emit ripCompleted(r.value());
        else        emit error(r.error());
        emit workingChanged(false);
    })->start();
}

void DiscRipper::ripDiscAsync(const RipOptions& opts) {
    if (isWorking()) { emit error(tr("Busy")); return; }
    emit workingChanged(true);
    QThread::create([this, opts] {
        auto scan = scanDiscSync();
        if (!scan.isSuccess()) { emit error(scan.error()); emit workingChanged(false); return; }
        int n = scan.value().totalTracks;
        for (int i = 1; i <= n; ++i) {
            emit progress(i * 100 / n, tr("Track %1/%2").arg(i).arg(n));
            auto r = ripTrackSync(i, opts);
            if (r.isSuccess()) emit trackCompleted(i, r.value().outputPath);
            else                emit error(r.error());
        }
        emit discRipCompleted();
        emit workingChanged(false);
    })->start();
}

void DiscRipper::cancel()    { cleanupWorker(); emit operationCancelled(); emit workingChanged(false); }
bool DiscRipper::eject()     { emit discEjected(); return true; }
bool DiscRipper::closeTray() { emit trayClosed();  return true; }
void DiscRipper::fetchMusicBrainzMetadata(const QString&) { emit metadataFetched({}); }

QStringList DiscRipper::enumerateDrives() {
    QStringList drives;
#if defined(Q_OS_LINUX)
    for (const auto& e : QDir(QStringLiteral("/dev")).entryList({QStringLiteral("sr*")}))
        drives << (QStringLiteral("/dev/") + e);
#elif defined(Q_OS_WIN)
    for (char c = 'D'; c <= 'Z'; ++c)
        drives << QString(c) + QStringLiteral(":\\");
#endif
    return drives;
}

QString DiscRipper::findDefaultDrive() {
    auto d = enumerateDrives();
    return d.isEmpty() ? QStringLiteral("/dev/cdrom") : d.first();
}

void DiscRipper::setupWorkerConnections() {}

void DiscRipper::cleanupWorker() {
    if (m_worker) { m_worker->quit(); m_worker->wait(3000); delete m_worker; m_worker = nullptr; }
}

QString DiscRipper::generateOutputPath(const DiscTrack& t, const RipOptions& o) const {
    return o.outputDir + QDir::separator() + sanitizeFilename(t.title) + '.' + o.format;
}

QString DiscRipper::sanitizeFilename(const QString& n) const {
    QString s = n;
    s.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")), QStringLiteral("_"));
    return s.trimmed();
}

// ============================================================================
// Disc  (QML-friendly wrapper)
// ============================================================================

Disc::Disc(const QString& device, QObject* parent)
    : QObject(parent)
    , m_ripper(std::make_unique<DiscRipper>(device, this))
{
    connect(m_ripper.get(), &DiscRipper::scanCompleted,  this, &Disc::onScanCompleted);
    connect(m_ripper.get(), &DiscRipper::ripCompleted,   this, &Disc::onRipCompleted);
    connect(m_ripper.get(), &DiscRipper::workingChanged, this, &Disc::workingChanged);
    connect(m_ripper.get(), &DiscRipper::error,          this, &Disc::error);
    connect(m_ripper.get(), &DiscRipper::progress,       this,
            [this](int pct, const QString& msg){ emit operationProgress(msg, pct); });
    connect(m_ripper.get(), &DiscRipper::trackCompleted, this,
            [this](int t, const QString&){ emit ripProgress(t, 100); });
}

Disc::~Disc() = default;

QString Disc::device()    const { return m_ripper->device(); }
bool    Disc::working()   const { return m_ripper->isWorking(); }
QString Disc::discLabel() const { return m_info.title.isEmpty() ? m_info.artist : m_info.title; }
int     Disc::trackCount()const { return m_info.totalTracks; }
bool    Disc::isAudioCD() const { return m_info.isAudioCD(); }
bool    Disc::isDVDVideo()const { return m_info.isVideoDVD(); }
bool    Disc::isBluRay()  const { return m_info.isBluRay(); }

void Disc::setDevice(const QString& d) { m_ripper->setDevice(d); emit deviceChanged(); }
void Disc::scanDisc()                  { m_ripper->scanDiscAsync(); }
void Disc::cancelOperation()           { m_ripper->cancel(); }
void Disc::eject()                     { m_ripper->eject(); }
void Disc::closeTray()                 { m_ripper->closeTray(); }
void Disc::fetchMetadataFromMusicBrainz() { m_ripper->fetchMusicBrainzMetadata(); }

void Disc::ripTrack(int n, const QString& out, int paranoia, const QString& fmt) {
    DiscRipper::RipOptions o; o.outputPath=out; o.paranoiaLevel=paranoia; o.format=fmt;
    m_ripper->ripTrackAsync(n, o);
}

void Disc::ripWholeDisc(const QString& dir, const QString& fmt, int paranoia, bool cue) {
    DiscRipper::RipOptions o; o.outputDir=dir; o.format=fmt; o.paranoiaLevel=paranoia; o.createCue=cue;
    m_ripper->ripDiscAsync(o);
}

void Disc::playTrack(int n) {
    emit playRequested(QStringLiteral("cdda:///") + device() + QStringLiteral("/track") + QString::number(n));
}

void Disc::playDVDTitle(int title, int chapter) {
    emit playRequested(QStringLiteral("dvd:///") + device()
        + QStringLiteral("/title") + QString::number(title)
        + QStringLiteral("/chapter") + QString::number(chapter));
}

QVariantList Disc::tracks() const {
    QVariantList list;
    for (const auto& t : m_info.tracks) {
        QVariantMap m;
        m[QStringLiteral("number")]   = t.number;
        m[QStringLiteral("title")]    = t.title;
        m[QStringLiteral("artist")]   = t.artist;
        m[QStringLiteral("duration")] = t.duration;
        m[QStringLiteral("isAudio")]  = t.isAudio;
        list.append(m);
    }
    return list;
}

QString Disc::discType()                        const { return m_info.discTypeString(); }
bool    Disc::driveSupports(const QString&)     const { return false; }

void Disc::onScanCompleted(const DiscInfo& info) { m_info = info; emit discChanged(); }
void Disc::onRipCompleted(const RipResult&)      { emit discChanged(); }
void Disc::updateDiscInfo(const DiscInfo& info)  { m_info = info; emit discChanged(); }
QString Disc::findDefaultDrive() const           { return DiscRipper::findDefaultDrive(); }

} // namespace Aegis
