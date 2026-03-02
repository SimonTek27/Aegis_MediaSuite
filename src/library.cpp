// library.cpp - Media library with SQLite backend
#include "library.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QFileInfo>
#include <QDirIterator>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QMutexLocker>
#include <QDebug>

// TagLib
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/audioproperties.h>

namespace Aegis {

    // ============================================================================
    // ConnectionPool Implementation
    // ============================================================================

    Library::ConnectionPool::ConnectionPool(const QString &path)
    {
        for (int i = 0; i < MaxConnections; ++i) {
            QString connName = QString("aegis_db_%1").arg(i);
            QSqlDatabase db  = QSqlDatabase::addDatabase("QSQLITE", connName);
            db.setDatabaseName(path);
            db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");

            if (!db.open()) {
                qCritical() << "Failed to open DB connection" << i << ":" << db.lastError().text();
                m_valid = false;
                return;
            }

            // Enable WAL mode for better concurrent read performance
            QSqlQuery q(db);
            q.exec("PRAGMA journal_mode=WAL");
            q.exec("PRAGMA synchronous=NORMAL");
            q.exec("PRAGMA foreign_keys=ON");

            m_connections.push_back(db);
            m_inUse.push_back(false);
        }
        m_valid = true;
    }

    Library::ConnectionPool::~ConnectionPool()
    {
        for (auto &db : m_connections) {
            QString name = db.connectionName();
            db.close();
            QSqlDatabase::removeDatabase(name);
        }
    }

    QSqlDatabase Library::ConnectionPool::acquire()
    {
        m_semaphore.acquire();
        QMutexLocker lock(&m_mutex);

        for (int i = 0; i < static_cast<int>(m_connections.size()); ++i) {
            if (!m_inUse[i]) {
                m_inUse[i] = true;
                return m_connections[i];
            }
        }

        // Should never reach here (semaphore guarantees an available slot)
        m_semaphore.release();
        return QSqlDatabase();
    }

    void Library::ConnectionPool::release(QSqlDatabase db)
    {
        QMutexLocker lock(&m_mutex);
        for (int i = 0; i < static_cast<int>(m_connections.size()); ++i) {
            if (m_connections[i].connectionName() == db.connectionName()) {
                m_inUse[i] = false;
                m_semaphore.release();
                return;
            }
        }
    }

    // ============================================================================
    // Library Implementation
    // ============================================================================

    Library::Library(const QString &dbPath, QObject *parent)
    : QObject(parent)
    {
        m_pool = std::make_unique<ConnectionPool>(dbPath);
        if (!m_pool->isValid()) {
            throw std::runtime_error("Failed to initialize database connection pool");
        }

        auto db = m_pool->acquire();
        if (!db.isOpen()) {
            m_pool->release(db);
            throw std::runtime_error("Database connection not open");
        }

        QSqlQuery q(db);
        q.exec(R"(
        CREATE TABLE IF NOT EXISTS tracks (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            path         TEXT UNIQUE NOT NULL,
            title        TEXT COLLATE NOCASE,
            artist       TEXT COLLATE NOCASE,
            album        TEXT COLLATE NOCASE,
            duration     INTEGER DEFAULT 0,
            bitrate      INTEGER DEFAULT 0,
            genre        TEXT,
            year         INTEGER DEFAULT 0,
            track_number INTEGER DEFAULT 0,
            codec        TEXT,
            added_date   INTEGER DEFAULT (strftime('%s', 'now'))
        )
        )");
        q.exec("CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist)");
        q.exec("CREATE INDEX IF NOT EXISTS idx_tracks_album  ON tracks(album)");
        q.exec("CREATE INDEX IF NOT EXISTS idx_tracks_title  ON tracks(title)");

        QSqlQuery countQuery(db);
        countQuery.exec("SELECT COUNT(*) FROM tracks");
        if (countQuery.next()) {
            m_trackCount.store(countQuery.value(0).toInt());
        }

        m_pool->release(db);
    }

    Library::~Library() = default;

    int Library::trackCount() const { return m_trackCount.load(); }

    void Library::scanDirectory(const QString &path)
    {
        if (m_scanning.exchange(true)) {
            emit error("Scan already in progress");
            return;
        }

        m_cancelScan.store(false);
        emit scanningChanged(true);

        QString canonicalPath = QFileInfo(path).canonicalFilePath();
        if (canonicalPath.isEmpty()) {
            emit error("Invalid path: " + path);
            m_scanning.store(false);
            emit scanningChanged(false);
            return;
        }

        QFutureWatcher<void> *watcher = new QFutureWatcher<void>(this);
        connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
            m_scanning.store(false);
            emit scanningChanged(false);
            emit trackCountChanged(m_trackCount.load());
            watcher->deleteLater();
        });

        QFuture<void> future = QtConcurrent::run([this, canonicalPath]() {
            const QStringList filters = {
                "*.mp3", "*.flac", "*.ogg", "*.m4a",
                "*.wav", "*.opus", "*.wma", "*.aac"
            };
            QDirIterator it(canonicalPath, filters, QDir::Files, QDirIterator::Subdirectories);

            std::vector<Track> batch;
            batch.reserve(100);
            int processed = 0;
            int added     = 0;
            int errors    = 0;

            while (it.hasNext() && !m_cancelScan.load()) {
                QString file = it.next();
                Track t;
                if (extractMetadata(file, t)) {
                    batch.push_back(t);
                    ++added;
                } else {
                    ++errors;
                }

                if (batch.size() >= 100) {
                    processBatch(batch);
                    batch.clear();
                }

                if (++processed % 50 == 0) {
                    emit scanProgress(processed, 0);
                }
            }

            if (!batch.empty()) {
                processBatch(batch);
            }

            emit scanCompleted(added, errors);
        });

        watcher->setFuture(future);
    }

    void Library::cancelScan()
    {
        m_cancelScan.store(true);
    }

    bool Library::extractMetadata(const QString &path, Track &outTrack)
    {
        try {
            TagLib::FileRef f(path.toUtf8().constData());
            if (f.isNull()) return false;

            outTrack.path = path;
            if (f.tag()) {
                outTrack.title  = QString::fromUtf8(f.tag()->title().toCString(true)).trimmed();
                outTrack.artist = QString::fromUtf8(f.tag()->artist().toCString(true)).trimmed();
                outTrack.album  = QString::fromUtf8(f.tag()->album().toCString(true)).trimmed();
                outTrack.genre  = QString::fromUtf8(f.tag()->genre().toCString(true)).trimmed();
                outTrack.year        = f.tag()->year();
                outTrack.trackNumber = f.tag()->track();
            }

            if (f.audioProperties()) {
                outTrack.duration = f.audioProperties()->lengthInSeconds();
                outTrack.bitrate  = f.audioProperties()->bitrate();
            }

            if (outTrack.title.isEmpty()) {
                QFileInfo info(path);
                outTrack.title = info.baseName();
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    /**
     * @brief Insert a batch of tracks into the database within a single transaction.
     *
     * [FIX Bug #5] La versione originale:
     *   - Non eseguiva rollback se q.exec() falliva per alcune tracce, lasciando
     *     la transazione nello stato "parzialmente eseguita" prima del commit.
     *   - In caso di errore su db.commit() (es. disco pieno) non lo rilevava.
     *   - m_trackCount veniva incrementato anche per righe la cui INSERT era fallita,
     *     producendo un contatore superiore al numero reale di tracce nel DB.
     *
     * Correzioni:
     *   - Se una singola INSERT fallisce viene loggata ma la transazione continua
     *     (INSERT OR IGNORE/REPLACE garantisce idempotenza sulle path duplicate).
     *   - In caso di errore critico viene eseguito rollback esplicito.
     *   - m_trackCount viene incrementato solo per le righe effettivamente inserite.
     *   - Verifica il risultato di db.commit() e logga eventuali errori.
     */
    void Library::processBatch(const std::vector<Track> &tracks)
    {
        auto db = m_pool->acquire();
        if (!db.isOpen()) {
            m_pool->release(db);
            return;
        }

        if (!db.transaction()) {
            qWarning() << "processBatch: could not start transaction:"
                       << db.lastError().text();
            m_pool->release(db);
            return;
        }

        QSqlQuery q(db);
        q.prepare(R"(INSERT OR REPLACE INTO tracks
            (path, title, artist, album, duration, bitrate, genre, year, track_number)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?))");

        int insertedCount = 0;

        for (const auto &t : tracks) {
            q.addBindValue(t.path);
            q.addBindValue(t.title.left(500));
            q.addBindValue(t.artist.left(500));
            q.addBindValue(t.album.left(500));
            q.addBindValue(t.duration);
            q.addBindValue(t.bitrate);
            q.addBindValue(t.genre.left(100));
            q.addBindValue(t.year);
            q.addBindValue(t.trackNumber);

            if (!q.exec()) {
                // Log the error but continue: one bad row should not block the rest.
                qWarning() << "processBatch: INSERT failed for"
                           << t.path << ":" << q.lastError().text();
            } else {
                ++insertedCount;
            }
        }

        if (!db.commit()) {
            qCritical() << "processBatch: commit failed:" << db.lastError().text();
            db.rollback();
            // Do not update m_trackCount: the data was not persisted.
            m_pool->release(db);
            return;
        }

        // Only update the in-memory counter after a successful commit.
        m_trackCount.fetch_add(insertedCount);

        m_pool->release(db);
    }

    QFuture<std::vector<Track>> Library::search(const QString &query, int limit)
    {
        return QtConcurrent::run([this, query, limit]() -> std::vector<Track> {
            auto db = m_pool->acquire();
            std::vector<Track> results;

            if (!db.isOpen()) {
                m_pool->release(db);
                return results;
            }

            QString pattern = "%" + query.simplified()
                                         .replace("%", "\\%")
                                         .replace("_", "\\_") + "%";
            QSqlQuery q(db);
            q.prepare(R"(SELECT id, path, title, artist, album, duration, bitrate
                         FROM tracks
                         WHERE title  LIKE ? ESCAPE '\'
                            OR artist LIKE ? ESCAPE '\'
                            OR album  LIKE ? ESCAPE '\'
                         ORDER BY artist, album, title
                         LIMIT ?)");
            q.addBindValue(pattern);
            q.addBindValue(pattern);
            q.addBindValue(pattern);
            q.addBindValue(limit);

            if (q.exec()) {
                while (q.next()) {
                    Track t;
                    t.id      = q.value(0).toInt();
                    t.path    = q.value(1).toString();
                    t.title   = q.value(2).toString();
                    t.artist  = q.value(3).toString();
                    t.album   = q.value(4).toString();
                    t.duration= q.value(5).toInt();
                    t.bitrate = q.value(6).toInt();
                    results.push_back(t);
                }
            }

            m_pool->release(db);
            return results;
        });
    }

    void Library::getAllTracks()
    {
        auto future = QtConcurrent::run([this]() -> QVariantList {
            auto db = m_pool->acquire();
            QVariantList results;

            if (!db.isOpen()) {
                m_pool->release(db);
                return results;
            }

            QSqlQuery q(db);
            q.setForwardOnly(true);
            q.exec("SELECT id, path, title, artist, album, duration FROM tracks ORDER BY artist");

            while (q.next()) {
                QVariantMap track;
                track["id"]      = q.value("id");
                track["path"]    = q.value("path");
                track["title"]   = q.value("title");
                track["artist"]  = q.value("artist");
                track["album"]   = q.value("album");
                track["duration"]= q.value("duration");
                results.append(track);
            }

            m_pool->release(db);
            return results;
        });

        QFutureWatcher<QVariantList> *watcher = new QFutureWatcher<QVariantList>(this);
        connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher]() {
            emit tracksFound(watcher->result());
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    }

    void Library::updateTags(int trackId, const QVariantMap &tags)
    {
        auto db = m_pool->acquire();
        if (!db.isOpen()) {
            m_pool->release(db);
            return;
        }

        QSqlQuery q(db);
        q.prepare("UPDATE tracks SET title=?, artist=?, album=? WHERE id=?");
        q.addBindValue(tags["title"]);
        q.addBindValue(tags["artist"]);
        q.addBindValue(tags["album"]);
        q.addBindValue(trackId);

        if (q.exec()) {
            emit trackCountChanged(m_trackCount.load());
        } else {
            qWarning() << "updateTags failed:" << q.lastError().text();
        }
        m_pool->release(db);
    }

    QVariantMap Library::statistics() const
    {
        QVariantMap stats;
        stats["trackCount"] = m_trackCount.load();
        stats["scanning"]   = m_scanning.load();
        return stats;
    }

    QString Library::coverArtPath(const QString& album)
    {
        Q_UNUSED(album)
        return QString();
    }

} // namespace Aegis
