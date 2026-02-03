// capture.h
#pragma once

#include <QObject>
#include <QProcess>
#include <QDBusInterface>
#include <QVariantMap>
#include <QDateTime>

class Capture : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)

public:
    explicit Capture(QObject *parent = nullptr);
    bool recording() const;

    // Portal-based screen/window capture (Plasma 6.6 Wayland compatible)
    Q_INVOKABLE void requestScreenCapture();
    Q_INVOKABLE void stopRecording();

    // Legacy device capture (X11/non-sandboxed fallback)
    Q_INVOKABLE QStringList listDevices();
    Q_INVOKABLE void startDeviceRecording(const QString &deviceNode);

signals:
    void recordingChanged();
    void error(const QString &message);
    void streamReady(const QString &pipeline);

private slots:
    // xdg-desktop-portal Response handler
    void onPortalResponse(uint code, const QVariantMap &results);
    void onFfmpegFinished(int code);

private:
    bool requestPortalSession();

    QProcess *m_ffmpeg = nullptr;
    bool m_recording = false;

    // Portal state
    QDBusInterface *m_portal = nullptr;
    QString m_sessionHandle;
    QString m_requestToken;

    // Track async portal operations to match responses
    QString m_pendingSources;
    QString m_pendingStart;
};
