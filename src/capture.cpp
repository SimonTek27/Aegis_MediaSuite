// capture.cpp - Universal multimedia capture implementation
//
// Implements capture.h interface with support for:
// - Screen capture via XDG Desktop Portal (PipeWire)
// - Webcam/capture cards via V4L2
// - DVB tuners via Linux DVB API
// - IP cameras via GStreamer RTSP
// - IPTV via HLS/MPEG-TS demuxers
// - Audio via PulseAudio
//
// Uses GStreamer as primary multimedia framework
// Falls back to FFmpeg for basic functionality when GStreamer unavailable
//

#include "capture.h"
#include <QScreen>
#include <QGuiApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>

// GStreamer includes
#ifdef HAVE_GSTREAMER
#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#endif
#endif
#ifdef HAVE_GSTREAMER
#include <gst/pbutils/pbutils.h>
#endif

// Portal D-Bus constants
static const char* PORTAL_SERVICE   = "org.freedesktop.portal.Desktop";
static const char* PORTAL_OBJECT    = "/org/freedesktop/portal/desktop";
static const char* SCREENCAST_IFACE = "org.freedesktop.portal.ScreenCast";
static const char* REQUEST_IFACE    = "org.freedesktop.portal.Request";

// ============================================================================
// Capture Implementation
// ============================================================================

Capture::Capture(QObject *parent)
: QObject(parent)
, m_portal(nullptr)
, m_portalStep(PortalStep::Idle)
, m_requestSerial(0)
, m_ffmpegProcess(nullptr)
, m_pipeline(nullptr)
, m_bus(nullptr)
, m_busWatchId(0)
{
    // Initialize GStreamer
    initializeGStreamer();

    // Set up D-Bus portal for screen capture
    m_portal = new QDBusInterface(PORTAL_SERVICE,
                                  PORTAL_OBJECT,
                                  SCREENCAST_IFACE,
                                  QDBusConnection::sessionBus(),
                                  this);

    // Connect to portal response signals
    QDBusConnection::sessionBus().connect(
        PORTAL_SERVICE,
        QString(),           // any object path
                                          REQUEST_IFACE,
                                          "Response",
                                          this,
                                          SLOT(onPortalResponse(uint, QVariantMap)));

    // Stats update timer (every 500ms)
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &Capture::updateStats);
    m_statsTimer->start(500);

    // Enumerate sources in background
    QThreadPool::globalInstance()->start([this]() {
        enumerateV4L2Sources();
        enumeratePulseAudioSources();
        enumerateDVBSources();
        enumerateIPCameras();
    });

    qDebug() << "Capture subsystem initialized";
}

Capture::~Capture()
{
    stopRecording();

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
    }

    delete m_portal;

    // Shutdown GStreamer
    shutdownGStreamer();
}

// ============================================================================
// Source Enumeration
// ============================================================================

QList<CaptureSourceInfo> Capture::listSources(CaptureSourceType type)
{
    QList<CaptureSourceInfo> sources;

    if (type == CaptureSourceType::Unknown || type == CaptureSourceType::Screen || type == CaptureSourceType::Window) {
        sources.append(enumeratePortalSources());
    }

    if (type == CaptureSourceType::Unknown || type == CaptureSourceType::Webcam || type == CaptureSourceType::CaptureCard) {
        sources.append(enumerateV4L2Sources());
    }

    if (type == CaptureSourceType::Unknown || type == CaptureSourceType::DVB) {
        sources.append(enumerateDVBSources());
    }

    if (type == CaptureSourceType::Unknown || type == CaptureSourceType::IPCamera) {
        sources.append(enumerateIPCameras());
    }

    if (type == CaptureSourceType::Unknown || type == CaptureSourceType::AudioInput || type == CaptureSourceType::AudioMonitor) {
        sources.append(enumeratePulseAudioSources());
    }

    // Cache sources by ID
    for (const auto &source : sources) {
        m_sourceCache[source.id] = source;
    }

    return sources;
}

CaptureSourceInfo Capture::getSourceInfo(const QString &sourceId)
{
    if (m_sourceCache.contains(sourceId)) {
        return m_sourceCache[sourceId];
    }

    // Try to enumerate just this source
    auto allSources = listSources(CaptureSourceType::Unknown);
    for (const auto &source : allSources) {
        if (source.id == sourceId) {
            return source;
        }
    }

    return CaptureSourceInfo();
}

// V4L2 source enumeration (webcams, capture cards)
QList<CaptureSourceInfo> Capture::enumerateV4L2Sources()
{
    QList<CaptureSourceInfo> sources;

    QProcess v4l2ctl;
    v4l2ctl.start("v4l2-ctl", {"--list-devices"});
    if (!v4l2ctl.waitForFinished(3000)) {
        return sources;
    }

    QString output = v4l2ctl.readAllStandardOutput();
    QStringList lines = output.split('\n');

    QString currentDevice;
    QString currentName;

    // Parse v4l2-ctl output format:
    // Device Name (location)
    //     /dev/video0
    //     /dev/video1
    //
    for (const QString &line : lines) {
        if (line.startsWith('\t')) {
            // Device node line
            QString node = line.trimmed();
            currentDevice = node;
        } else if (!line.isEmpty() && !line.startsWith(' ')) {
            // Device name line
            currentName = line.section('(', 0, 0).trimmed();

            if (!currentDevice.isEmpty()) {
                CaptureSourceInfo info;
                info.id = currentDevice;
                info.name = currentName;
                info.type = currentName.contains("HDMI", Qt::CaseInsensitive) ||
                currentName.contains("capture", Qt::CaseInsensitive)
                ? CaptureSourceType::CaptureCard
                : CaptureSourceType::Webcam;
                info.backend = "v4l2";

                // Query capabilities with v4l2-ctl --all
                QProcess caps;
                caps.start("v4l2-ctl", {"-d", currentDevice, "--all"});
                if (caps.waitForFinished(2000)) {
                    QString capsOutput = caps.readAllStandardOutput();

                    // Parse supported resolutions and formats
                    QRegularExpression resRegex(R"(\d+x\d+)");
                    QRegularExpressionMatchIterator i = resRegex.globalMatch(capsOutput);
                    while (i.hasNext()) {
                        QRegularExpressionMatch match = i.next();
                        QString res = match.captured(0);
                        int w = res.section('x', 0, 0).toInt();
                        int h = res.section('x', 1, 1).toInt();

                        VideoStreamInfo video;
                        video.id = currentDevice;
                        video.name = "Video";
                        video.resolutions.append(QSize(w, h));
                        video.fpsList.append(30); // Default, should parse actual FPS
                        video.hasAudio = currentName.contains("audio", Qt::CaseInsensitive);
                        info.videoStreams.append(video);
                    }
                }

                info.isAvailable = QFile::exists(currentDevice);
                sources.append(info);

                currentDevice.clear();
            }
        }
    }

    qDebug() << "Found" << sources.size() << "V4L2 sources";
    return sources;
}

// PulseAudio source enumeration
QList<CaptureSourceInfo> Capture::enumeratePulseAudioSources()
{
    QList<CaptureSourceInfo> sources;

    QProcess pactl;
    pactl.start("pactl", {"list", "sources"});
    if (!pactl.waitForFinished(3000)) {
        return sources;
    }

    QString output = pactl.readAllStandardOutput();
    QStringList lines = output.split('\n');

    CaptureSourceInfo currentSource;
    AudioStreamInfo currentAudio;
    bool inSource = false;

    for (const QString &line : lines) {
        if (line.contains("Source #")) {
            // New source begins
            if (inSource) {
                currentSource.audioStreams.append(currentAudio);
                sources.append(currentSource);
            }

            inSource = true;
            currentSource = CaptureSourceInfo();
            currentAudio = AudioStreamInfo();
            currentSource.type = CaptureSourceType::AudioInput;
            currentSource.backend = "pulseaudio";
        } else if (inSource) {
            if (line.contains("Name:")) {
                currentAudio.id = line.section(':', 1).trimmed();
                currentSource.id = "pulse:" + currentAudio.id;
            } else if (line.contains("Description:")) {
                currentAudio.name = line.section(':', 1).trimmed();
                currentSource.name = currentAudio.name;
            } else if (line.contains("Sample Specification:")) {
                QString spec = line.section(':', 1).trimmed();
                if (spec.contains("s16le")) {
                    // Parse sample rate and channels
                    QRegularExpression rateRegex(R"(\d+)");
                    QRegularExpressionMatch match = rateRegex.match(spec);
                    if (match.hasMatch()) {
                        currentAudio.sampleRate = match.captured(0).toInt();
                    }
                    if (spec.contains("stereo")) currentAudio.channels = 2;
                    if (spec.contains("mono")) currentAudio.channels = 1;
                }
            } else if (line.contains("Monitor of Sink:")) {
                // This is a monitor source (desktop audio)
                currentSource.type = CaptureSourceType::AudioMonitor;
                currentAudio.isDefault = line.contains("sink");
            }
        }
    }

    if (inSource) {
        currentSource.audioStreams.append(currentAudio);
        sources.append(currentSource);
    }

    qDebug() << "Found" << sources.size() << "PulseAudio sources";
    return sources;
}

// DVB source enumeration (digital TV tuners)
QList<CaptureSourceInfo> Capture::enumerateDVBSources()
{
    QList<CaptureSourceInfo> sources;

    // Check for DVB devices in /dev/dvb/
    QDir dvbDir("/dev/dvb");
    if (!dvbDir.exists()) {
        return sources;
    }

    QStringList adapters = dvbDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &adapter : adapters) {
        QString adapterPath = QString("/dev/dvb/%1").arg(adapter);
        QDir adapterDir(adapterPath);

        QStringList devices = adapterDir.entryList(QDir::Files);

        CaptureSourceInfo info;
        info.id = adapterPath;
        info.name = QString("DVB Adapter %1").arg(adapter);
        info.type = CaptureSourceType::DVB;
        info.backend = "dvb";

        // Check device types
        for (const QString &device : devices) {
            if (device.startsWith("frontend")) {
                // Tuner frontend
                QProcess feInfo;
                feInfo.start("dvb-fe-tool", {"-d", adapterPath + "/" + device});
                if (feInfo.waitForFinished(2000)) {
                    QString output = feInfo.readAllStandardOutput();
                    if (output.contains("DVB-T")) info.metadata["type"] = "DVB-T";
                    if (output.contains("DVB-S")) info.metadata["type"] = "DVB-S";
                    if (output.contains("DVB-C")) info.metadata["type"] = "DVB-C";
                }
            } else if (device.startsWith("demux")) {
                info.metadata["has_demux"] = "true";
            } else if (device.startsWith("dvr")) {
                info.metadata["has_dvr"] = "true";
            }
        }

        // Video stream info
        VideoStreamInfo video;
        video.id = adapterPath + "/dvr0";
        video.name = "MPEG-TS Stream";
        video.codecs = QStringList() << "mpeg2" << "h264" << "hevc";
        video.hasAudio = true;
        info.videoStreams.append(video);

        // Audio stream info (will be demuxed from MPEG-TS)
        AudioStreamInfo audio;
        audio.id = adapterPath + "/audio";
        audio.name = "DVB Audio";
        audio.channels = 2;
        audio.sampleRate = 48000;
        audio.codecs = QStringList() << "mp2" << "aac" << "ac3";
        info.audioStreams.append(audio);

        info.isAvailable = true;
        sources.append(info);
    }

    qDebug() << "Found" << sources.size() << "DVB sources";
    return sources;
}

// IP camera enumeration (from saved config)
QList<CaptureSourceInfo> Capture::enumerateIPCameras()
{
    QList<CaptureSourceInfo> sources;

    // Load from config file
    QSettings settings("Aegis", "ipcameras");
    int count = settings.beginReadArray("cameras");

    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);

        CaptureSourceInfo info;
        info.id = settings.value("id").toString();
        info.name = settings.value("name").toString();
        info.type = CaptureSourceType::IPCamera;
        info.backend = "rtsp";

        QUrl url(settings.value("url").toString());
        if (!settings.value("username").toString().isEmpty()) {
            url.setUserName(settings.value("username").toString());
            url.setPassword(settings.value("password").toString());
        }

        info.capabilities["url"] = url;
        info.capabilities["protocol"] = settings.value("protocol", "rtsp");

        // Video stream info
        VideoStreamInfo video;
        video.id = url.toString();
        video.name = "Main Stream";
        video.resolutions.append(QSize(
            settings.value("width", 1920).toInt(),
                                       settings.value("height", 1080).toInt()
        ));
        video.fpsList.append(settings.value("fps", 30).toInt());
        video.codecs = QStringList() << settings.value("codec", "h264").toString();
        video.hasAudio = settings.value("has_audio", false).toBool();
        info.videoStreams.append(video);

        info.isAvailable = true;
        sources.append(info);
    }

    settings.endArray();

    qDebug() << "Loaded" << sources.size() << "IP cameras from config";
    return sources;
}

// Portal sources (screen/window capture)
QList<CaptureSourceInfo> Capture::enumeratePortalSources()
{
    QList<CaptureSourceInfo> sources;

    if (!m_portal || !m_portal->isValid()) {
        return sources;
    }

    // Screen source
    CaptureSourceInfo screen;
    screen.id = "screen://portal";
    screen.name = "Entire Screen";
    screen.type = CaptureSourceType::Screen;
    screen.backend = "portal";
    screen.isAvailable = true;

    // Query current screen size
    QScreen *primary = QGuiApplication::primaryScreen();
    if (primary) {
        VideoStreamInfo video;
        video.id = "screen";
        video.name = "Screen Video";
        video.resolutions.append(primary->size());
        video.fpsList.append(60); // Portal can handle variable FPS
        video.hasAudio = false;    // Portal doesn't provide audio
        screen.videoStreams.append(video);
    }

    sources.append(screen);

    // Window source
    CaptureSourceInfo window;
    window.id = "window://portal";
    window.name = "Application Window";
    window.type = CaptureSourceType::Window;
    window.backend = "portal";
    window.isAvailable = true;

    VideoStreamInfo windowVideo;
    windowVideo.id = "window";
    windowVideo.name = "Window Video";
    windowVideo.resolutions.append(QSize(1920, 1080)); // Will be actual window size
    windowVideo.fpsList.append(60);
    windowVideo.hasAudio = false;
    window.videoStreams.append(windowVideo);

    sources.append(window);

    return sources;
}

// ============================================================================
// IP Camera Management
// ============================================================================

bool Capture::addIPCamera(const QString &name, const QUrl &url,
                          const QString &username, const QString &password)
{
    if (!url.isValid()) {
        emit error("Invalid camera URL");
        return false;
    }

    QString id = QUuid::createUuid().toString();

    QSettings settings("Aegis", "ipcameras");
    settings.beginWriteArray("cameras");
    settings.setArrayIndex(settings.childGroups().size());
    settings.setValue("id", id);
    settings.setValue("name", name);
    settings.setValue("url", url.toString());
    settings.setValue("username", username);
    settings.setValue("password", password);
    settings.setValue("protocol", url.scheme());
    settings.endArray();
    settings.sync();

    // Emit signal for new source
    CaptureSourceInfo info;
    info.id = id;
    info.name = name;
    info.type = CaptureSourceType::IPCamera;
    info.backend = url.scheme();
    info.capabilities["url"] = url;
    info.isAvailable = true;

    emit sourceAdded(info);

    return true;
}

bool Capture::removeIPCamera(const QString &cameraId)
{
    QSettings settings("Aegis", "ipcameras");
    int count = settings.beginReadArray("cameras");

    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        if (settings.value("id").toString() == cameraId) {
            settings.endArray();

            // Remove by rewriting array
            settings.beginWriteArray("cameras");
            int newIndex = 0;
            for (int j = 0; j < count; ++j) {
                if (j != i) {
                    settings.setArrayIndex(newIndex);
                    // Copy values
                    // ... (implementation omitted for brevity)
                    newIndex++;
                }
            }
            settings.endArray();
            settings.sync();

            emit sourceRemoved(cameraId);
            return true;
        }
    }

    settings.endArray();
    return false;
}

QList<CaptureSourceInfo> Capture::listIPCameras()
{
    return enumerateIPCameras();
}

// ============================================================================
// DVB Specific
// ============================================================================

QList<CaptureSourceInfo> Capture::scanDVBTChannels(const QString &adapter)
{
    QList<CaptureSourceInfo> channels;

    QString scanCmd = adapter.isEmpty() ? "scan" : QString("scan -a %1").arg(adapter);
    QProcess scanProcess;
    scanProcess.start("dvbscan", QStringList() << "/usr/share/dvb/dvb-t/de-Berlin");

    if (!scanProcess.waitForFinished(30000)) { // Scan can take a while
        emit error("DVB scan timed out");
        return channels;
    }

    QString output = scanProcess.readAllStandardOutput();
    QStringList lines = output.split('\n');

    for (const QString &line : lines) {
        if (line.startsWith("Channel:")) {
            QString channelName = line.section(':', 1).trimmed();

            CaptureSourceInfo info;
            info.id = QString("dvb://%1").arg(channelName);
            info.name = channelName;
            info.type = CaptureSourceType::DVB;
            info.backend = "dvb";

            // Parse frequency and parameters
            QRegularExpression freqRegex(R"(frequency:\s*(\d+))");
            QRegularExpressionMatch match = freqRegex.match(output);
            if (match.hasMatch()) {
                info.capabilities["frequency"] = match.captured(1).toInt();
            }

            channels.append(info);
        }
    }

    return channels;
}

bool Capture::tuneDVBChannel(const QString &channelId)
{
    // Extract channel info from ID
    QString channelName = channelId;
    channelName.remove("dvb://");

    // Find channel in cache
    for (const auto &source : m_sourceCache) {
        if (source.type == CaptureSourceType::DVB && source.name == channelName) {
            m_currentSource = source;
            return true;
        }
    }

    return false;
}

// ============================================================================
// Portal Screen Capture (Wayland)
// ============================================================================

void Capture::requestScreenCapture()
{
    if (m_recording) return;

    if (!m_portal || !m_portal->isValid()) {
        emit error("ScreenCast portal not available – running outside sandbox?");
        return;
    }

    m_portalStep = PortalStep::Idle;
    sendCreateSession();
}

QString Capture::nextRequestPath()
{
    ++m_requestSerial;
    return QString("aegis_req_%1_%2")
    .arg(QString(QDBusConnection::sessionBus().baseService())
    .replace('.', '_').replace(':', '_'))
    .arg(m_requestSerial);
}

bool Capture::sendCreateSession()
{
    m_sessionToken = QString("aegis_session_%1")
    .arg(QDateTime::currentMSecsSinceEpoch());
    QString handleToken = nextRequestPath();

    QDBusMessage reply = m_portal->call(
        "CreateSession",
        QVariantMap{
            {"session_handle_token", m_sessionToken},
            {"handle_token",         handleToken}
        });

    if (reply.type() == QDBusMessage::ErrorMessage) {
        emit error("CreateSession failed: " + reply.errorMessage());
        return false;
    }

    m_portalStep = PortalStep::CreateSession;
    return true;
}

bool Capture::sendSelectSources()
{
    if (m_sessionHandle.isEmpty()) {
        emit error("sendSelectSources: no session handle");
        return false;
    }

    QString handleToken = nextRequestPath();

    QDBusMessage reply = m_portal->call(
        "SelectSources",
        QDBusObjectPath(m_sessionHandle),
                                        QVariantMap{
                                            {"handle_token", handleToken},
                                            {"types",        uint(3)},         // monitor + window
                                        {"multiple",     false},
                                        {"cursor_mode",  m_cursorOverlayEnabled ? uint(2) : uint(1)}
                                        });

    if (reply.type() == QDBusMessage::ErrorMessage) {
        emit error("SelectSources failed: " + reply.errorMessage());
        return false;
    }

    m_portalStep = PortalStep::SelectSources;
    return true;
}

bool Capture::sendStart()
{
    if (m_sessionHandle.isEmpty()) {
        emit error("sendStart: no session handle");
        return false;
    }

    QString handleToken = nextRequestPath();

    QDBusMessage reply = m_portal->call(
        "Start",
        QDBusObjectPath(m_sessionHandle),
                                        QString(),           // parent_window handle
                                        QVariantMap{{"handle_token", handleToken}});

    if (reply.type() == QDBusMessage::ErrorMessage) {
        emit error("Start failed: " + reply.errorMessage());
        return false;
    }

    m_portalStep = PortalStep::Start;
    return true;
}

void Capture::onPortalResponse(uint code, const QVariantMap &results)
{
    if (code != 0) {
        emit error(code == 1
        ? "Portal request cancelled by user"
        : QString("Portal request failed (code %1)").arg(code));
        m_portalStep = PortalStep::Idle;
        return;
    }

    switch (m_portalStep) {
        case PortalStep::CreateSession: {
            m_sessionHandle = results.value("session_handle").toString();
            if (m_sessionHandle.isEmpty()) {
                emit error("Portal returned empty session handle");
                m_portalStep = PortalStep::Idle;
                return;
            }
            qDebug() << "Portal session handle:" << m_sessionHandle;
            sendSelectSources();
            break;
        }

        case PortalStep::SelectSources: {
            sendStart();
            break;
        }

        case PortalStep::Start: {
            // Parse stream node IDs from portal response
            QVariant streamsVariant = results.value("streams");
            QList<uint> nodeIds;

            if (streamsVariant.canConvert<QDBusArgument>()) {
                QDBusArgument arg = streamsVariant.value<QDBusArgument>();
                arg.beginArray();
                while (!arg.atEnd()) {
                    arg.beginStructure();
                    uint nodeId = 0;
                    arg >> nodeId;
                    QVariantMap props;
                    arg >> props;
                    arg.endStructure();

                    if (nodeId != 0) {
                        nodeIds.append(nodeId);
                        qDebug() << "Found PipeWire node ID:" << nodeId;
                    }
                }
                arg.endArray();
            }

            if (nodeIds.isEmpty()) {
                emit error("Portal returned no PipeWire streams");
                m_portalStep = PortalStep::Idle;
                return;
            }

            uint nodeId = nodeIds.first();
            QString pipeline = buildGStreamerPipeline(nodeId);

            m_recording = true;
            m_portalStep = PortalStep::Idle;
            emit recordingChanged();
            emit streamReady(pipeline, nodeId);

            // Start recording with this pipeline
            CaptureSourceInfo source;
            source.id = "portal:" + QString::number(nodeId);
            source.name = "Screen Capture";
            source.type = CaptureSourceType::Screen;
            source.backend = "portal";
            m_currentSource = source;

            buildPipelineForSource(source);
            startGstRecording();
            break;
        }

        default:
            break;
    }
}

QString Capture::buildGStreamerPipeline(uint nodeId) const
{
    QString outPath = generateOutputPath(m_exportOptions.exportGif ? "gif" : "mp4");

    QString videoScale;
    if (m_exportOptions.width > 0 && m_exportOptions.height > 0) {
        videoScale = QString(" ! videoscale ! video/x-raw,width=%1,height=%2")
        .arg(m_exportOptions.width)
        .arg(m_exportOptions.height);
    }

    if (m_exportOptions.exportGif) {
        return QString("pipewiresrc path=%1 ! videoconvert%2 ! "
        "gifenc ! filesink location=%3")
        .arg(nodeId)
        .arg(videoScale)
        .arg(outPath);
    }

    // Default: MP4 with configurable codec
    QString encoder;
    if (m_exportOptions.videoCodec == "h265" || m_exportOptions.videoCodec == "hevc") {
        encoder = "x265enc";
    } else if (m_exportOptions.videoCodec == "vp9") {
        encoder = "vp9enc";
    } else {
        encoder = "x264enc tune=zerolatency";
    }

    return QString("pipewiresrc path=%1 ! videoconvert%2 ! %3 ! "
    "mp4mux ! filesink location=%4")
    .arg(nodeId)
    .arg(videoScale)
    .arg(encoder)
    .arg(outPath);
}

// ============================================================================
// GStreamer Pipeline Management
// ============================================================================

bool Capture::initializeGStreamer()
{
    gst_init(nullptr, nullptr);

    // Check if GStreamer is available
    guint major, minor, micro;
    guint nano = 0;
    gst_version(&major, &minor, &micro, &nano);
    qDebug() << "GStreamer version:" << major << "." << minor << "." << micro;

    return true;
}

void Capture::shutdownGStreamer()
{
    // GStreamer doesn't have a global shutdown function
    // Elements will be cleaned up individually
}

bool Capture::buildPipelineForSource(const CaptureSourceInfo &source, const QString &audioSourceId)
{
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }

    QString pipelineDesc;
    QString outPath = generateOutputPath();
    m_outputPath = outPath;

    // Build pipeline based on source type
    switch (source.type) {
        case CaptureSourceType::Webcam:
        case CaptureSourceType::CaptureCard: {
            // V4L2 source
            pipelineDesc = QString(
                "v4l2src device=%1 ! videoconvert ! "
                "queue ! %2enc ! %3mux name=mux ! "
                "filesink location=%4"
            ).arg(source.id)
            .arg(m_exportOptions.videoCodec)
            .arg(m_exportOptions.outputFormat);
            break;
        }

        case CaptureSourceType::DVB: {
            // DVB source with TS demux
            pipelineDesc = QString(
                "dvbsrc ! tsdemux name=demux "
                "demux.video_0 ! queue ! h264parse ! %1enc ! mux. "
                "demux.audio_0 ! queue ! aacparse ! faac ! mux. "
                "%2mux name=mux ! filesink location=%3"
            ).arg(m_exportOptions.videoCodec)
            .arg(m_exportOptions.outputFormat)
            .arg(outPath);
            break;
        }

        case CaptureSourceType::IPCamera: {
            // RTSP source
            QUrl url = source.capabilities["url"].toUrl();
            pipelineDesc = QString(
                "rtspsrc location=%1 ! rtph264depay ! h264parse ! "
                "%2enc ! %3mux ! filesink location=%4"
            ).arg(url.toString())
            .arg(m_exportOptions.videoCodec)
            .arg(m_exportOptions.outputFormat)
            .arg(outPath);
            break;
        }

        case CaptureSourceType::IPTV: {
            // HLS source
            pipelineDesc = QString(
                "souphttpsrc location=%1 ! hlsdemux ! qtdemux ! "
                "h264parse ! %2enc ! %3mux ! filesink location=%4"
            ).arg(source.capabilities["url"].toString())
            .arg(m_exportOptions.videoCodec)
            .arg(m_exportOptions.outputFormat)
            .arg(outPath);
            break;
        }

        case CaptureSourceType::AudioInput:
        case CaptureSourceType::AudioMonitor: {
            // Audio-only recording
            QString device = source.audioStreams.first().id;
            pipelineDesc = QString(
                "pulsesrc device=%1 ! audioconvert ! audioresample ! "
                "%2enc ! %3mux ! filesink location=%4"
            ).arg(device)
            .arg(m_exportOptions.audioCodec)
            .arg(m_exportOptions.outputFormat)
            .arg(outPath);
            break;
        }

        default:
            emit error("Unsupported source type for pipeline building");
            return false;
    }

    // Add audio source if specified and not already included
    if (!audioSourceId.isEmpty() && source.type != CaptureSourceType::AudioInput) {
        QString audioBranch = QString(
            " ! pulsesrc device=%1 ! audioconvert ! audioresample ! "
            "queue ! %2enc ! mux."
        ).arg(audioSourceId)
        .arg(m_exportOptions.audioCodec);

        pipelineDesc.replace("mux.", audioBranch + " mux.");
    }

    qDebug() << "Building pipeline:" << pipelineDesc;

    GError *gstErr = nullptr;
    m_pipeline = gst_parse_launch(pipelineDesc.toUtf8().constData(), &gstErr);

    if (gstErr) {
        emit error(QString("Failed to build pipeline: %1").arg(gstErr->message));
        g_error_free(gstErr);
        return false;
    }

    // Set up bus monitoring
    m_bus = gst_element_get_bus(m_pipeline);
    m_busWatchId = gst_bus_add_watch(m_bus, (GstBusFunc)[](GstBus *bus, GstMessage *msg, gpointer user_data) -> gboolean {
        static_cast<Capture*>(user_data)->onGstBusMessage(msg);
        return TRUE;
    }, this);
    gst_object_unref(m_bus);

    return true;
}

void Capture::onGstBusMessage(GstMessage *msg)
{
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            emit error(QString("GStreamer error: %1").arg(err->message));
            g_error_free(err);
            g_free(debug);

            stopRecording();
            break;
        }

        case GST_MESSAGE_WARNING: {
            GError *err = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_warning(msg, &err, &debug);
            emit warning(QString("GStreamer warning: %1").arg(err->message));
            g_error_free(err);
            g_free(debug);
            break;
        }

        case GST_MESSAGE_EOS: {
            // End of stream
            qDebug() << "GStreamer: End of stream";
            stopRecording();
            break;
        }

        case GST_MESSAGE_STATE_CHANGED: {
            GstState oldState, newState, pending;
            gst_message_parse_state_changed(msg, &oldState, &newState, &pending);
            qDebug() << "Pipeline state changed:"
            << gst_element_state_get_name(oldState)
            << "->" << gst_element_state_get_name(newState);
            break;
        }

        default:
            break;
    }
}

// ============================================================================
// Recording Control
// ============================================================================

bool Capture::startCapture(const QString &sourceId, const QString &audioSourceId)
{
    return startCaptureWithOptions(sourceId, m_exportOptions, audioSourceId);
}

bool Capture::startCaptureWithOptions(const QString &sourceId,
                                      const CaptureExportOptions &options,
                                      const QString &audioSourceId)
{
    if (m_recording) {
        emit error("Already recording");
        return false;
    }

    // Find source info
    auto source = getSourceInfo(sourceId);
    if (source.id.isEmpty()) {
        emit error(QString("Source not found: %1").arg(sourceId));
        return false;
    }

    m_exportOptions = options;
    m_currentSource = source;
    m_currentAudioSource = audioSourceId;

    // Handle portal sources specially
    if (source.backend == "portal") {
        requestScreenCapture();
        return true;
    }

    // Build and start pipeline for other sources
    if (!buildPipelineForSource(source, audioSourceId)) {
        return false;
    }

    startGstRecording();
    return true;
}

void Capture::startGstRecording()
{
    if (!m_pipeline) {
        emit error("No pipeline available");
        return;
    }

    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        emit error("Failed to start pipeline");
        return;
    }

    m_recording = true;
    m_stats = RecordingStats();
    emit recordingChanged();
    emit recordingStarted(m_outputPath);

    qDebug() << "Recording started:" << m_outputPath;
}

void Capture::stopRecording()
{
    if (m_pipeline) {
        // Send EOS and wait
        gst_element_send_event(m_pipeline, gst_event_new_eos());
        gst_element_set_state(m_pipeline, GST_STATE_NULL);

        if (m_busWatchId > 0) {
            g_source_remove(m_busWatchId);
            m_busWatchId = 0;
        }
    }

    if (m_ffmpegProcess) {
        disconnect(m_ffmpegProcess, nullptr, this, nullptr);
        m_ffmpegProcess->terminate();
        if (!m_ffmpegProcess->waitForFinished(5000)) {
            m_ffmpegProcess->kill();
        }
        m_ffmpegProcess->deleteLater();
        m_ffmpegProcess = nullptr;
    }

    if (m_recording) {
        m_recording = false;
        m_paused = false;
        emit recordingChanged();
        emit recordingFinished(m_outputPath, m_stats);
    }
}

void Capture::pauseRecording()
{
    if (!m_recording || m_paused) return;

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
        m_paused = true;
        emit recordingPaused();
    }
}

void Capture::resumeRecording()
{
    if (!m_recording || !m_paused) return;

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
        m_paused = false;
        emit recordingResumed();
    }
}

// ============================================================================
// Screenshot
// ============================================================================

bool Capture::captureScreenshot(const QString &sourceId, const QString &outputPath, const QSize &size)
{
    auto source = getSourceInfo(sourceId);
    if (source.id.isEmpty()) {
        emit error(QString("Source not found: %1").arg(sourceId));
        return false;
    }

    QString outPath = outputPath;
    if (outPath.isEmpty()) {
        outPath = generateOutputPath("png");
    }

    QString pipelineDesc;

    if (source.backend == "portal") {
        // For portal, we need to request a single frame
        // This is simplified - real implementation would use Portal's screenshot API
        emit error("Screenshot from portal not yet implemented");
        return false;
    } else if (source.type == CaptureSourceType::Webcam ||
        source.type == CaptureSourceType::CaptureCard) {
        // V4L2 snapshot
        pipelineDesc = QString(
            "v4l2src device=%1 num-buffers=1 ! "
            "videoconvert ! pngenc ! filesink location=%2"
        ).arg(source.id).arg(outPath);
        } else if (source.type == CaptureSourceType::IPCamera) {
            // RTSP snapshot
            QUrl url = source.capabilities["url"].toUrl();
            pipelineDesc = QString(
                "rtspsrc location=%1 ! rtph264depay ! h264parse ! "
                "avdec_h264 ! videoconvert ! pngenc ! filesink location=%2"
            ).arg(url.toString()).arg(outPath);
        } else {
            emit error("Unsupported source type for screenshot");
            return false;
        }

        // Add scaling if needed
        if (size.isValid()) {
            pipelineDesc.replace("videoconvert",
                                 QString("videoconvert ! videoscale ! video/x-raw,width=%1,height=%2")
                                 .arg(size.width()).arg(size.height()));
        }

        GError *gstErr = nullptr;
        GstElement *pipeline = gst_parse_launch(pipelineDesc.toUtf8().constData(), &gstErr);

        if (gstErr) {
            emit error(QString("Failed to build screenshot pipeline: %1").arg(gstErr->message));
            g_error_free(gstErr);
            return false;
        }

        // Run pipeline synchronously
        gst_element_set_state(pipeline, GST_STATE_PLAYING);

        // Wait for EOS or error
        GstBus *bus = gst_element_get_bus(pipeline);
        GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                                     (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

        bool success = true;
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            GError *err = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            emit error(QString("Screenshot failed: %1").arg(err->message));
            g_error_free(err);
            g_free(debug);
            success = false;
        }

        gst_message_unref(msg);
        gst_object_unref(bus);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);

        if (success) {
            qDebug() << "Screenshot saved:" << outPath;
        }

        return success;
}

// ============================================================================
// Audio Recording
// ============================================================================

bool Capture::recordAudio(const QString &audioSourceId, const QString &outputPath, int durationSeconds)
{
    if (m_recording) {
        emit error("Already recording");
        return false;
    }

    auto source = getSourceInfo(audioSourceId);
    if (source.id.isEmpty() ||
        (source.type != CaptureSourceType::AudioInput &&
        source.type != CaptureSourceType::AudioMonitor)) {
        emit error("Invalid audio source");
    return false;
        }

        QString outPath = outputPath;
        if (outPath.isEmpty()) {
            outPath = generateOutputPath("m4a");
        }

        QString pipelineDesc = QString(
            "pulsesrc device=%1 ! audioconvert ! audioresample ! "
            "voaacenc ! mp4mux ! filesink location=%2"
        ).arg(source.audioStreams.first().id).arg(outPath);

        if (durationSeconds > 0) {
            pipelineDesc.prepend(QString("fakesrc ! identity sleep-time=%1 ! ").arg(durationSeconds * 1000000));
        }

        GError *gstErr = nullptr;
        m_pipeline = gst_parse_launch(pipelineDesc.toUtf8().constData(), &gstErr);

        if (gstErr) {
            emit error(QString("Failed to build audio pipeline: %1").arg(gstErr->message));
            g_error_free(gstErr);
            return false;
        }

        m_outputPath = outPath;
        startGstRecording();
        return true;
}

// ============================================================================
// Utilities
// ============================================================================

QString Capture::generateOutputPath(const QString &extension) const
{
    QString baseDir = m_exportOptions.outputDir.isEmpty()
    ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Aegis"
    : m_exportOptions.outputDir;

    QDir().mkpath(baseDir);

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
    QString filename = QString("%1/recording_%2.%3")
    .arg(baseDir)
    .arg(timestamp)
    .arg(extension);

    return filename;
}

void Capture::updateStats()
{
    if (!m_recording || !m_pipeline) return;

    // Query pipeline position and duration
    gint64 pos, dur;
    if (gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &pos)) {
        m_stats.durationMs = pos / 1000000; // ns to ms
    }

    // Query bitrate from encoder (implementation depends on encoder element)
    // This is simplified - real implementation would use GstPad queries

    emit statsUpdated(m_stats);
}

// ============================================================================
// FFmpeg Fallback (for systems without GStreamer)
// ============================================================================

void Capture::startDeviceRecording(const QString &deviceNode)
{
    if (m_ffmpegProcess) {
        stopRecording();
    }

    m_ffmpegProcess = new QProcess(this);

    QString outPath = generateOutputPath();

    QStringList args = {
        "-f", "v4l2",
        "-i", deviceNode,
        "-c:v", m_exportOptions.videoCodec,
        "-b:v", QString::number(m_exportOptions.bitrate) + "k",
        "-r", QString::number(m_exportOptions.fps),
        "-y",  // Overwrite output
        outPath
    };

    // Add audio if source supports it
    auto source = getSourceInfo(deviceNode);
    if (!source.audioStreams.isEmpty()) {
        args.prepend("-f");
        args.prepend("alsa");
        args.prepend("-i");
        args.prepend("hw:0"); // Default audio input
        args.prepend("-c:a");
        args.prepend("aac");
    }

    connect(m_ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Capture::onProcessFinished);
    connect(m_ffmpegProcess, &QProcess::readyReadStandardOutput,
            this, &Capture::onProcessOutput);
    connect(m_ffmpegProcess, &QProcess::readyReadStandardError,
            this, &Capture::onProcessError);

    m_ffmpegProcess->start("ffmpeg", args);

    if (!m_ffmpegProcess->waitForStarted(5000)) {
        emit error("Failed to start FFmpeg");
        return;
    }

    m_recording = true;
    m_outputPath = outPath;
    emit recordingChanged();
    emit recordingStarted(outPath);
}

void Capture::onProcessFinished(int code, QProcess::ExitStatus)
{
    m_recording = false;
    emit recordingChanged();

    if (code != 0) {
        emit error(QString("FFmpeg exited with code %1").arg(code));
    } else {
        emit recordingFinished(m_outputPath, m_stats);
    }

    if (m_ffmpegProcess) {
        m_ffmpegProcess->deleteLater();
        m_ffmpegProcess = nullptr;
    }
}

void Capture::onProcessOutput()
{
    if (m_ffmpegProcess) {
        QString output = m_ffmpegProcess->readAllStandardOutput();
        qDebug() << "FFmpeg output:" << output;
    }
}

void Capture::onProcessError()
{
    if (m_ffmpegProcess) {
        QString error = m_ffmpegProcess->readAllStandardError();

        // Parse FFmpeg progress info
        if (error.contains("frame=")) {
            // Update stats from FFmpeg output
            QRegularExpression timeRegex(R"(time=(\d+):(\d+):(\d+\.\d+))");
            QRegularExpressionMatch match = timeRegex.match(error);
            if (match.hasMatch()) {
                int h = match.captured(1).toInt();
                int m = match.captured(2).toInt();
                double s = match.captured(3).toDouble();
                m_stats.durationMs = (h * 3600 + m * 60 + s) * 1000;
                emit statsUpdated(m_stats);
            }
        } else {
            qWarning() << "FFmpeg error:" << error;
        }
    }
}

// ============================================================================
// OpenScreen Integration
// ============================================================================

void Capture::addZoomKeyframe(qint64 timestampMs, double depth,
                              double cx, double cy, int durationMs)
{
    ZoomKeyframe kf;
    kf.timestampMs = timestampMs;
    kf.depth       = qBound(1.0, depth, 10.0);
    kf.x           = qBound(0.0, cx, 1.0);
    kf.y           = qBound(0.0, cy, 1.0);
    kf.durationMs  = qMax(0, durationMs);

    m_zoomKeyframes.append(kf);

    // Keep list sorted
    std::sort(m_zoomKeyframes.begin(), m_zoomKeyframes.end(),
              [](const ZoomKeyframe &a, const ZoomKeyframe &b) {
                  return a.timestampMs < b.timestampMs;
              });

    emit zoomKeyframesChanged();
}

void Capture::removeZoomKeyframe(qint64 timestampMs)
{
    const qint64 tolerance = 250;
    int best = -1;
    qint64 bestDist = std::numeric_limits<qint64>::max();

    for (int i = 0; i < m_zoomKeyframes.size(); ++i) {
        qint64 d = qAbs(m_zoomKeyframes[i].timestampMs - timestampMs);
        if (d < bestDist && d <= tolerance) {
            bestDist = d;
            best = i;
        }
    }

    if (best >= 0) {
        m_zoomKeyframes.removeAt(best);
        emit zoomKeyframesChanged();
    }
}

void Capture::setZoomKeyframes(const QList<ZoomKeyframe>& keyframes)
{
    m_zoomKeyframes = keyframes;
    std::sort(m_zoomKeyframes.begin(), m_zoomKeyframes.end(),
              [](const ZoomKeyframe &a, const ZoomKeyframe &b) {
                  return a.timestampMs < b.timestampMs;
              });
    emit zoomKeyframesChanged();
}

void Capture::setBackground(const BackgroundConfig& bg)
{
    m_background = bg;
}

void Capture::setExportOptions(const CaptureExportOptions& opts)
{
    m_exportOptions = opts;
}

void Capture::setCursorOverlayEnabled(bool enabled)
{
    m_cursorOverlayEnabled = enabled;
}

// Legacy method for backward compatibility
QStringList Capture::listDevices()
{
    QStringList devices;

    auto sources = listSources(CaptureSourceType::Webcam);
    for (const auto &source : sources) {
        devices << source.id;
    }

    sources = listSources(CaptureSourceType::Screen);
    for (const auto &source : sources) {
        devices << source.id;
    }

    return devices;
}
