// library.h
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QFuture>
#include <QThreadPool>
#include <QVariantMap>

#ifdef QT_SQL_LIB
#include <QSqlDatabase>
#else
class QSqlDatabase;
#endif
#include <QSemaphore>
#include <QMutex>
#include <memory>
#include <atomic>
#include <queue>

namespace Aegis {

    struct Track {
        int id{-1};
        QString path;
        QString title;
        QString artist;
        QString album;
        int duration{0};
        int bitrate{0};
        QString genre;
        int year{0};
        int trackNumber{0};
        QString codec;

        bool isValid() const { return !path.isEmpty() && (id > 0 || !title.isEmpty()); }
    };

    class Library : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
        Q_PROPERTY(int trackCount READ trackCount NOTIFY trackCountChanged)

    public:
        explicit Library(const QString &dbPath, QObject *parent = nullptr);
        ~Library() override;

        QFuture<std::vector<Track>> search(const QString &query, int limit = 100);
        QFuture<std::vector<Track>> getAllTracks();
        QFuture<bool> insertTrack(const Track &track);
        QFuture<bool> deleteTrack(int trackId);
        QFuture<bool> updateTrack(const Track &track);
        QFuture<QVector<Track>> searchAlbumTracks(const QString& album);
        QString coverArtPath(const QString& album);

        Q_INVOKABLE void scanDirectory(const QString &path);
        Q_INVOKABLE void cancelScan();
        Q_INVOKABLE void updateTags(int trackId, const QVariantMap &tags);

        bool scanning() const { return m_scanning.load(); }
        int trackCount() const;
        Q_INVOKABLE QVariantMap statistics() const;

    signals:
        void scanningChanged(bool scanning);
        void trackCountChanged(int count);
        void scanProgress(int current, int total);
        void scanCompleted(int added, int errors);
        void tracksReady(const std::vector<Track> &tracks);
        void tracksFound(const QVariantList &tracks);
        void error(const QString &message);

    private:
        bool initializeSchema();
        bool extractMetadata(const QString &path, Track &outTrack);
        void processBatch(const std::vector<Track> &batch);

        class ConnectionPool {
            static constexpr int MaxConnections = 4;
            QSemaphore m_semaphore{MaxConnections};
            std::vector<QSqlDatabase> m_connections;
            std::vector<bool> m_inUse;
            QMutex m_mutex;
            bool m_valid{false};

        public:
            explicit ConnectionPool(const QString& path);
            ~ConnectionPool();
            QSqlDatabase acquire();
            void release(QSqlDatabase db);
            bool isValid() const { return m_valid; }
        };

        std::unique_ptr<ConnectionPool> m_pool;
        std::atomic<bool> m_scanning{false};
        std::atomic<bool> m_cancelScan{false};
        std::atomic<int> m_trackCount{0};
        QThreadPool m_scanThreadPool;
    };

} // namespace Aegis
