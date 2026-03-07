#pragma once

#include <QObject>
#include <QProcess>
#include <QMap>

class QNetworkAccessManager;

enum class ServiceType { YouTube, Spotify, Radio, IPTV };

struct StreamTrack {
    QString id;
    QString title;
    QString artist;
    QString url;  // May be temporary!
    int duration;
    ServiceType type;
};

class Streaming : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ytAvailable READ ytAvailable NOTIFY availabilityChanged)

public:
    explicit Streaming(QObject *parent = nullptr);

    bool ytAvailable() const { return !m_ytPath.isEmpty(); }

    // YouTube
    Q_INVOKABLE void searchYouTube(const QString &query);
    Q_INVOKABLE void getYouTubeStream(const QString &videoId);

    // Radio
    Q_INVOKABLE void searchRadio(const QString &genre);
    Q_INVOKABLE void getRadioStations();

    // IPTV
    Q_INVOKABLE void loadM3U(const QString &path);

    // Playback
    Q_INVOKABLE void resolveStream(const QString &id, ServiceType type);

signals:
        void serviceStatusChanged(const QString& service, bool available);
    void availabilityChanged();
    void searchResults(const QVariantList &results);
    void streamResolved(const QString &url, const QVariantMap &metadata);

private slots:
    void onYtFinished(int code);

private:
    QProcess *m_ytProcess = nullptr;
    QString m_ytPath;
    QNetworkAccessManager *m_net;
    QMap<QString, StreamTrack> m_trackCache;
};
