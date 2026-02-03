#include "streaming.h"
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QUrlQuery>
#include <QNetworkReply>

Streaming::Streaming(QObject *parent)
: QObject(parent), m_net(new QNetworkAccessManager(this)) {

    m_ytPath = QStandardPaths::findExecutable("yt-dlp");
    if (m_ytPath.isEmpty()) {
        m_ytPath = QStandardPaths::findExecutable("youtube-dl");
    }
}

void Streaming::searchYouTube(const QString &query) {
    if (m_ytPath.isEmpty()) return;

    m_ytProcess = new QProcess(this);
    connect(m_ytProcess, &QProcess::finished,
            this, &Streaming::onYtFinished);

    QStringList args = {
        "ytsearch10:" + query,
        "--dump-json",
        "--flat-playlist"
    };

    m_ytProcess->setProgram(m_ytPath);
    m_ytProcess->setArguments(args);
    m_ytProcess->start();
}

void Streaming::onYtFinished(int code) {
    if (code != 0) return;

    QByteArray data = m_ytProcess->readAllStandardOutput();
    QVariantList results;

    for (const QByteArray &line : data.split('\n')) {
        if (line.isEmpty()) continue;

        QJsonObject obj = QJsonDocument::fromJson(line).object();
        QVariantMap track;
        track["id"] = obj["id"].toString();
        track["title"] = obj["title"].toString();
        track["artist"] = obj["uploader"].toString();
        track["duration"] = obj["duration"].toInt();
        track["type"] = "youtube";
        results.append(track);

        StreamTrack st;
        st.id = obj["id"].toString();
        st.title = obj["title"].toString();
        st.artist = obj["uploader"].toString();
        st.duration = obj["duration"].toInt();
        st.type = ServiceType::YouTube;
        m_trackCache[st.id] = st;
    }

    emit searchResults(results);
    m_ytProcess->deleteLater();
    m_ytProcess = nullptr;
}

void Streaming::getYouTubeStream(const QString &videoId) {
    if (m_ytPath.isEmpty()) return;

    QProcess *proc = new QProcess(this);
    connect(proc, &QProcess::finished, [this, proc, videoId](int code, QProcess::ExitStatus status) {
        Q_UNUSED(status);
        if (code == 0) {
            QByteArray data = proc->readAllStandardOutput();
            QJsonObject obj = QJsonDocument::fromJson(data).object();
            QString url = obj["url"].toString();

            QVariantMap meta;
            if (m_trackCache.contains(videoId)) {
                meta["title"] = m_trackCache[videoId].title;
                meta["artist"] = m_trackCache[videoId].artist;
            }
            emit streamResolved(url, meta);
        }
        proc->deleteLater();
    });

    QStringList args = {
        "--format", "bestaudio",
        "--get-url",
        "https://youtube.com/watch?v=" + videoId
    };

    proc->setProgram(m_ytPath);
    proc->setArguments(args);
    proc->start();
}

void Streaming::searchRadio(const QString &genre) {
    // Radio-browser API
    QUrl url("https://de1.api.radio-browser.info/json/stations/search");
    QUrlQuery q;
    q.addQueryItem("genre", genre);
    q.addQueryItem("limit", "20");
    url.setQuery(q);

    QNetworkRequest req(url);
    QNetworkReply *reply = m_net->get(req);

    connect(reply, &QNetworkReply::finished, [this, reply]() {
        QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
        QVariantList results;

        for (const auto &val : arr) {
            QJsonObject obj = val.toObject();
            QVariantMap track;
            track["id"] = obj["stationuuid"].toString();
            track["title"] = obj["name"].toString();
            track["artist"] = obj["tags"].toString();
            track["url"] = obj["url_resolved"].toString();
            track["type"] = "radio";
            results.append(track);
        }
        emit searchResults(results);
        reply->deleteLater();
    });
}

void Streaming::getRadioStations() {
    searchRadio("");
}

void Streaming::loadM3U(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QVariantList results;
    QTextStream stream(&file);
    QString line;
    QString currentName;

    while (!stream.atEnd()) {
        line = stream.readLine();
        if (line.startsWith("#EXTINF:")) {
            int comma = line.lastIndexOf(',');
            currentName = line.mid(comma + 1);
        } else if (!line.isEmpty() && !line.startsWith('#')) {
            QVariantMap track;
            track["id"] = line;
            track["title"] = currentName;
            track["url"] = line;
            track["type"] = "iptv";
            results.append(track);
            currentName.clear();
        }
    }

    emit searchResults(results);
}

void Streaming::resolveStream(const QString &id, ServiceType type) {
    if (type == ServiceType::YouTube) {
        getYouTubeStream(id);
    } else {
        // Direct URL for radio/IPTV
        QVariantMap meta;
        emit streamResolved(id, meta);
    }
}
