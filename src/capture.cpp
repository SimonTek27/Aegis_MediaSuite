// capture.cpp
#include "capture.h"
#include <QDir>
#include <QStandardPaths>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDebug>
#include <QRegularExpression>

Capture::Capture(QObject *parent) : QObject(parent) {
    m_portal = new QDBusInterface("org.freedesktop.portal.Desktop",
                                  "/org/freedesktop/portal/desktop",
                                  "org.freedesktop.portal.ScreenCast",
                                  QDBusConnection::sessionBus(), this);

    // Connect to response signals
    QDBusConnection::sessionBus().connect("org.freedesktop.portal.Desktop",
                                          QString(),
                                          "org.freedesktop.portal.Request",
                                          "Response",
                                          this,
                                          SLOT(onPortalResponse(uint,QVariantMap)));
}

bool Capture::recording() const { return m_recording; }

bool Capture::requestPortalSession() {
    if (!m_portal->isValid()) {
        emit error("ScreenCast portal not available");
        return false;
    }

    QString sessionToken = "aegis_session_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QString handleToken = "aegis_request_" + QString::number(QDateTime::currentMSecsSinceEpoch());

    QDBusMessage msg = m_portal->call("CreateSession", QVariantMap{
        {"session_handle_token", sessionToken},
        {"handle_token", handleToken}
    });

    if (msg.type() == QDBusMessage::ErrorMessage) {
        emit error(msg.errorMessage());
        return false;
    }

    // Store request handle for matching response
    m_requestToken = handleToken;
    m_sessionHandle = msg.arguments().first().toString();
    return true;
}

void Capture::requestScreenCapture() {
    if (m_recording) return;
    if (!requestPortalSession()) {
        emit error("Portal failed. Running outside sandbox?");
        return;
    }

    QString selectToken = "aegis_select_" + QString::number(QDateTime::currentMSecsSinceEpoch());

    QDBusMessage selectMsg = m_portal->call("SelectSources", m_sessionHandle, QVariantMap{
        {"handle_token", selectToken},
        {"types", 1 | 2},  // Monitor + Window
        {"multiple", false},
        {"cursor_mode", 2}  // Embedded cursor
    });

    if (selectMsg.type() == QDBusMessage::ErrorMessage) {
        emit error(selectMsg.errorMessage());
        return;
    }

    m_pendingSources = selectToken;

    QString startToken = "aegis_start_" + QString::number(QDateTime::currentDateTime().toMSecsSinceEpoch());
    QDBusMessage startMsg = m_portal->call("Start", m_sessionHandle, QString(), QVariantMap{
        {"handle_token", startToken}
    });

    if (startMsg.type() == QDBusMessage::ErrorMessage) {
        emit error(startMsg.errorMessage());
        return;
    }

    m_pendingStart = startToken;
}

void Capture::onPortalResponse(uint code, const QVariantMap &results) {
    if (code != 0) {
        emit error("Portal request cancelled or failed");
        return;
    }

    // Check if this is the Start response with streams
    if (results.contains("streams")) {
        // streams is array of (node_id, screen info)
        // Parse PipeWire node ID from results
        QVariantList streams = results["streams"].toList();
        if (!streams.isEmpty()) {
            QVariantMap stream = streams.first().toMap();
            uint nodeId = stream["id"].toUInt();

            QString pipeline = QString("pipewiresrc node-id=%1 ! videoconvert ! x264enc ! mp4mux").arg(nodeId);
            emit streamReady(pipeline);
            m_recording = true;
            emit recordingChanged();
        }
    }
}

QStringList Capture::listDevices() {
    QStringList devices;
    devices << "screen://portal" << "window://portal";
    return devices;
}

void Capture::startDeviceRecording(const QString &device) {
    if (device.startsWith("screen://") || device.startsWith("window://")) {
        requestScreenCapture();
        return;
    }

    // Legacy V4L2 path
    m_ffmpeg = new QProcess(this);
    QString outPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
    + "/Aegis/" + QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss") + ".mp4";
    QDir().mkpath(QFileInfo(outPath).path());

    QStringList args = {
        "-f", "v4l2", "-i", device,
        "-c:v", "libx264", "-preset", "fast", "-crf", "23",
        "-c:a", "aac",
        outPath
    };

    connect(m_ffmpeg,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &Capture::onFfmpegFinished);
    m_ffmpeg->start("ffmpeg", args);
    m_recording = true;
    emit recordingChanged();
}

void Capture::stopRecording() {
    if (m_ffmpeg) {
        m_ffmpeg->terminate();
        if (!m_ffmpeg->waitForFinished(5000)) {
            m_ffmpeg->kill();
        }
    }
    m_recording = false;
    emit recordingChanged();
}

void Capture::onFfmpegFinished(int code, QProcess::ExitStatus)
{
    m_recording = false;
    emit recordingChanged();
    if (m_ffmpeg) {
        m_ffmpeg->deleteLater();
        m_ffmpeg = nullptr;
    }
    if (code != 0) {
        emit error("Recording failed with code: " + QString::number(code));
    }
}
