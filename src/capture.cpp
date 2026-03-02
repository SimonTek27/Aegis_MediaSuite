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
    QString handleToken  = "aegis_request_" + QString::number(QDateTime::currentMSecsSinceEpoch());

    QDBusMessage msg = m_portal->call("CreateSession", QVariantMap{
        {"session_handle_token", sessionToken},
        {"handle_token", handleToken}
    });

    if (msg.type() == QDBusMessage::ErrorMessage) {
        emit error(msg.errorMessage());
        return false;
    }

    m_requestToken  = handleToken;
    m_sessionHandle = msg.arguments().first().toString();
    return true;
}

void Capture::requestScreenCapture() {
    if (m_recording) return;
    if (!requestPortalSession()) {
        emit error("Portal failed. Running outside sandbox?");
        return;
    }
}

void Capture::onPortalResponse(uint code, const QVariantMap &results) {
    if (code != 0) {
        emit error("Portal request denied or cancelled");
        return;
    }

    QVariantMap streams = results.value("streams").toMap();
    if (streams.isEmpty()) {
        emit error("No streams returned by portal");
        return;
    }

    // Extract the PipeWire node ID from the first stream
    uint nodeId = streams.keys().isEmpty() ? 0 : streams.first().toMap().value("pipe_wire_node_id").toUInt();
    if (nodeId == 0) {
        emit error("Invalid PipeWire node ID");
        return;
    }

    QString outPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                      + "/Aegis/" + QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss") + ".mp4";
    QDir().mkpath(QFileInfo(outPath).path());

    // Build GStreamer pipeline using PipeWire source
    QString pipeline = QString("pipewiresrc path=%1 ! videoconvert ! x264enc ! mp4mux").arg(nodeId);
    emit streamReady(pipeline);
    m_recording = true;
    emit recordingChanged();
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

/**
 * @brief Stop the current recording session.
 *
 * [FIX Bug #2] La versione originale:
 *   - Non disconnetteva il segnale QProcess::finished prima di terminate()/kill(),
 *     lasciando che onFfmpegFinished venisse chiamato dopo waitForFinished(), con
 *     m_ffmpeg ancora non-null ma il processo già terminato.
 *   - Non azzerava m_ffmpeg a nullptr dopo il cleanup sincrono, rendendo possibile
 *     una seconda chiamata a stopRecording() su un puntatore stale.
 *   - In caso di timeout su waitForFinished(5000), kill() veniva chiamato ma
 *     il processo non era necessariamente terminato al ritorno.
 *
 * Correzioni applicate:
 *   1. disconnect(m_ffmpeg, nullptr, this, nullptr) prima di terminate() per
 *      impedire che onFfmpegFinished venga chiamato due volte.
 *   2. Secondo waitForFinished() dopo kill() per garantire che il processo sia
 *      davvero terminato prima di procedere.
 *   3. deleteLater() + m_ffmpeg = nullptr subito dopo, in modo che lo stato sia
 *      consistente al termine di stopRecording().
 */
void Capture::stopRecording() {
    if (m_ffmpeg) {
        // Disconnect before terminating to prevent onFfmpegFinished from firing
        // for this synchronous shutdown (it would fire again on the queued signal).
        disconnect(m_ffmpeg, nullptr, this, nullptr);

        m_ffmpeg->terminate();
        if (!m_ffmpeg->waitForFinished(5000)) {
            m_ffmpeg->kill();
            m_ffmpeg->waitForFinished(2000); // ensure process is really gone
        }

        m_ffmpeg->deleteLater();
        m_ffmpeg = nullptr;
    }

    m_recording = false;
    emit recordingChanged();
}

void Capture::onFfmpegFinished(int code, QProcess::ExitStatus)
{
    // This slot is only reached when ffmpeg exits on its own (not via stopRecording).
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
