// library.cpp - Thread-safe Media Library with Connection Pool

#include "library.h"
#include <QDirIterator>
#include <QtConcurrent>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QRegularExpression>
#include <QThread>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/audioproperties.h>
#include <atomic>
#include <chrono>

namespace Aegis {

    // ============================================================================
    // ConnectionPool - Thread-safe with RAII
    // ============================================================================

    class Library::ConnectionPool {
    private:
        static constexpr int MaxConnections = 4;
        static constexpr int ConnectionTimeoutMs = 5000;

        struct Connection {
            QSqlDatabase db;
            std::atomic<bool> inUse{false};
            std::chrono::steady_clock::time_point lastUsed;

            explicit Connection(const QString& name)
            : db(QSqlDatabase::addDatabase("QSQLITE", name)) {
                lastUsed = std::chrono::steady_clock::now();
            }
        };

        std::vector<std::unique_ptr<Connection>> m_connections;
        std::atomic<bool> m_valid{false};
        QString m_dbPath;

        // Condition variable for waiting on connections
        std::mutex m_mutex;
        std::condition_variable m_cv;

    public:
        class Handle {
        private:
            ConnectionPool* m_pool;
            Connection* m_conn;
            std::chrono::steady_clock::time_point m_acquireTime;

        public:
            Handle() : m_pool(nullptr), m_conn(nullptr) {}

            Handle(ConnectionPool* pool, Connection* conn)
            : m_pool(pool)
            , m_conn(conn)
            , m_acquireTime(std::chrono::steady_clock::now()) {
                if (m_conn) {
                    m_conn->inUse.store(true, std::memory_order_release);
                }
            }

            ~Handle() {
                if (m_conn) {
                    m_conn->inUse.store(false, std::memory_order_release);
                    m_conn->lastUsed = std::chrono::steady_clock::now();
                    if (m_pool) {
                        m_pool->m_cv.notify_one();
                    }
                }
            }

            // Move constructor
            Handle(Handle&& other) noexcept
            : m_pool(other.m_pool)
            , m_conn(other.m_conn)
            , m_acquireTime(other.m_acquireTime) {
                other.m_pool = nullptr;
                other.m_conn = nullptr;
            }

            // No copy
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;

            QSqlDatabase operator*() const {
                return m_conn ? m_conn->db : QSqlDatabase();
            }

            QSqlDatabase operator->() const {
                return m_conn ? m_conn->db : QSqlDatabase();
            }

            bool isValid() const { return m_conn != nullptr; }

            std::chrono::milliseconds age() const {
                return std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - m_acquireTime);
            }
        };

        explicit ConnectionPool(const QString& path) : m_dbPath(path) {
            initialize();
        }

        ~ConnectionPool() {
            for (auto& conn : m_connections) {
                if (conn->db.isOpen()) {
                    conn->db.close();
                }
            }
        }

        bool initialize() {
            try {
                for (int i = 0; i < MaxConnections; ++i) {
                    QString connName = QString("library_conn_%1_%2")
                    .arg(i)
                    .arg(reinterpret_cast<quintptr>(this));

                    auto conn = std::make_unique<Connection>(connName);

                    // Configure connection
                    conn->db.setDatabaseName(m_dbPath);
                    conn->db.setConnectOptions(
                        "QSQLITE_OPEN_NOMUTEX;"
                        "PRAGMA journal_mode=WAL;"
                        "PRAGMA synchronous=NORMAL;"
                        "PRAGMA cache_size=10000;"
                        "PRAGMA temp_store=MEMORY;"
                    );

                    if (!conn->db.open()) {
                        qCritical() << "Failed to open library DB:"
                        << conn->db.lastError().text();
                        return false;
                    }

                    // Set foreign keys
                    QSqlQuery q(conn->db);
                    if (!q.exec("PRAGMA foreign_keys = ON")) {
                        qWarning() << "Failed to enable foreign keys";
                    }

                    // Optimize for concurrent access
                    q.exec("PRAGMA mmap_size = 268435456");  // 256 MB

                    m_connections.push_back(std::move(conn));
                }

                m_valid = true;
                return true;

            } catch (const std::exception& e) {
                qCritical() << "Connection pool initialization failed:" << e.what();
                return false;
            }
        }

        Handle acquire() {
            std::unique_lock<std::mutex> lock(m_mutex);

            auto startTime = std::chrono::steady_clock::now();

            while (true) {
                // Try to find free connection
                for (auto& conn : m_connections) {
                    bool expected = false;
                    if (conn->inUse.compare_exchange_strong(expected, true,
                        std::memory_order_acq_rel)) {
                        return Handle(this, conn.get());
                        }
                }

                // Check timeout
                auto now = std::chrono::steady_clock::now();
                if (now - startTime > std::chrono::milliseconds(ConnectionTimeoutMs)) {
                    qWarning() << "Connection pool timeout - no free connections";
                    return Handle();
                }

                // Wait for notification
                m_cv.wait_for(lock, std::chrono::milliseconds(100));
            }
        }

        bool isValid() const { return m_valid.load(); }

        // Health check - close stale connections
        void healthCheck() {
            auto now = std::chrono::steady_clock::now();

            for (auto& conn : m_connections) {
                // Check if connection is stale (unused for > 5 minutes)
                if (!conn->inUse.load() &&
                    now - conn->lastUsed > std::chrono::minutes(5)) {

                    // Reconnect
                    conn->db.close();
                if (!conn->db.open()) {
                    qWarning() << "Failed to reconnect database";
                }
                conn->lastUsed = now;
                    }
            }
        }
    };

    // ============================================================================
    // Library Implementation
    // ============================================================================

    class Library::Private {
    public:
        std::unique_ptr<ConnectionPool> pool;
        std::atomic<bool> scanning{false};
        std::atomic<bool> cancelScan{false};
        std::atomic<int> trackCount{0};
        QThreadPool scanThreadPool;
        QTimer healthCheckTimer;

        // Statistics
        std::atomic<int64_t> totalScanTime{0};
        std::atomic<int> totalScannedFiles{0};
        std::atomic<int> totalErrors{0};

        Private() {
            scanThreadPool.setMaxThreadCount(2);  // Limit concurrent scans
        }
    };

    Library::Library(const QString& dbPath, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {

        d->pool = std::make_unique<ConnectionPool>(dbPath);
        if (!d->pool->isValid()) {
            throw std::runtime_error("Failed to initialize database connection pool");
        }

        // Initialize schema
        auto conn = d->pool->acquire();
        if (!conn.isValid()) {
            throw std::runtime_error("Failed to acquire database connection");
        }

        if (!initializeSchema(*conn)) {
            throw std::runtime_error("Failed to initialize database schema");
        }

        // Load track count
        QSqlQuery q(*conn);
        if (q.exec("SELECT COUNT(*) FROM tracks") && q.next()) {
            d->trackCount.store(q.value(0).toInt());
        }

        // Start health check timer
        d->healthCheckTimer.setInterval(60000);  // Every minute
        connect(&d->healthCheckTimer, &QTimer::timeout, this, [this]() {
            d->pool->healthCheck();
        });
        d->healthCheckTimer.start();
    }

    Library::~Library() = default;

    bool Library::initializeSchema(QSqlDatabase& db) {
        QStringList statements = {
            R"(
            CREATE TABLE IF NOT EXISTS tracks (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                path TEXT UNIQUE NOT NULL,
                title TEXT COLLATE NOCASE,
                artist TEXT COLLATE NOCASE,
                album TEXT COLLATE NOCASE,
                duration INTEGER DEFAULT 0,
                bitrate INTEGER DEFAULT 0,
                genre TEXT,
                year INTEGER DEFAULT 0,
                track_number INTEGER DEFAULT 0,
                codec TEXT,
                file_hash TEXT,
                file_size INTEGER DEFAULT 0,
                file_modified INTEGER DEFAULT 0,
                added_date INTEGER DEFAULT (strftime('%s', 'now')),
                last_played INTEGER DEFAULT 0,
                play_count INTEGER DEFAULT 0,
                rating INTEGER DEFAULT 0
            )
            )",

            "CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist)",
            "CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album)",
            "CREATE INDEX IF NOT EXISTS idx_tracks_title ON tracks(title)",
            "CREATE INDEX IF NOT EXISTS idx_tracks_path ON tracks(path)",
            "CREATE INDEX IF NOT EXISTS idx_tracks_modified ON tracks(file_modified)",

            R"(
            CREATE TABLE IF NOT EXISTS playlists (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT UNIQUE NOT NULL,
                created INTEGER DEFAULT (strftime('%s', 'now')),
                modified INTEGER DEFAULT (strftime('%s', 'now'))
            )
            )",

            R"(
            CREATE TABLE IF NOT EXISTS playlist_entries (
                playlist_id INTEGER,
                track_id INTEGER,
                position INTEGER,
                FOREIGN KEY(playlist_id) REFERENCES playlists(id) ON DELETE CASCADE,
                FOREIGN KEY(track_id) REFERENCES tracks(id) ON DELETE CASCADE
            )
            )",

            "CREATE INDEX IF NOT EXISTS idx_playlist_entries ON playlist_entries(playlist_id, position)"
        };

        QSqlQuery q(db);
        for (const QString& stmt : statements) {
            if (!q.exec(stmt)) {
                qCritical() << "Schema initialization failed:" << q.lastError().text();
                return false;
            }
        }

        return true;
    }

    int Library::trackCount() const {
        return d->trackCount.load();
    }

    void Library::scanDirectory(const QString& path) {
        if (d->scanning.exchange(true)) {
            emit error(tr("Scan already in progress"));
            return;
        }

        d->cancelScan.store(false);
        emit scanningChanged(true);

        QString canonicalPath = QFileInfo(path).canonicalFilePath();
        if (canonicalPath.isEmpty()) {
            emit error(tr("Invalid path: %1").arg(path));
            d->scanning.store(false);
            emit scanningChanged(false);
            return;
        }

        auto future = QtConcurrent::run(&d->scanThreadPool, [this, canonicalPath]() {
            scanDirectorySync(canonicalPath);
        });

        auto* watcher = new QFutureWatcher<void>(this);
        connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
            d->scanning.store(false);
            emit scanningChanged(false);
            emit scanCompleted(d->totalScannedFiles.load(), d->totalErrors.load());
            watcher->deleteLater();
        });

        watcher->setFuture(future);
    }

    void Library::scanDirectorySync(const QString& path) {
        const QStringList filters = {
            "*.mp3", "*.flac", "*.ogg", "*.m4a", "*.wav",
            "*.opus", "*.wma", "*.aac", "*.ape", "*.mpc"
        };

        QDirIterator it(path, filters, QDir::Files, QDirIterator::Subdirectories);

        std::vector<Track> batch;
        batch.reserve(100);
        int processed = 0;
        int added = 0;
        int errors = 0;
        int updated = 0;
        int skipped = 0;

        auto startTime = std::chrono::steady_clock::now();

        while (it.hasNext() && !d->cancelScan.load()) {
            QString filePath = it.next();
            QFileInfo fileInfo(filePath);

            // Skip if file is too small
            if (fileInfo.size() < 1024) {  // < 1 KB
                skipped++;
                continue;
            }

            Track track;
            if (extractMetadata(filePath, track)) {
                track.fileSize = fileInfo.size();
                track.fileModified = fileInfo.lastModified().toSecsSinceEpoch();

                // Check if track exists and needs update
                if (trackNeedsUpdate(track)) {
                    if (insertOrUpdateTrack(track)) {
                        added++;
                    } else {
                        errors++;
                    }
                } else {
                    updated++;
                }

                batch.push_back(track);
            } else {
                errors++;
            }

            if (batch.size() >= 100) {
                processBatch(batch);
                batch.clear();
            }

            if (++processed % 50 == 0) {
                int elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - startTime).count();

                    emit scanProgress(processed, 0);
                    emit statusMessage(tr("Scanned %1 files (%2 errors, %3/sec)")
                    .arg(processed).arg(errors)
                    .arg(elapsed > 0 ? processed / elapsed : 0));
            }
        }

        if (!batch.empty()) {
            processBatch(batch);
        }

        d->totalScannedFiles = added;
        d->totalErrors = errors;
        d->totalScanTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();

            emit scanCompleted(added, errors);
    }

    bool Library::trackNeedsUpdate(const Track& track) const {
        auto conn = d->pool->acquire();
        if (!conn.isValid()) return false;

        QSqlQuery q(*conn);
        q.prepare("SELECT file_modified FROM tracks WHERE path = ?");
        q.addBindValue(track.path);

        if (q.exec() && q.next()) {
            int64_t storedModified = q.value(0).toLongLong();
            return storedModified != track.fileModified;
        }

        return true;  // Not found, needs insert
    }

    bool Library::insertOrUpdateTrack(const Track& track) {
        auto conn = d->pool->acquire();
        if (!conn.isValid()) return false;

        QSqlQuery q(*conn);
        q.prepare(R"(
            INSERT OR REPLACE INTO tracks
            (path, title, artist, album, duration, bitrate, genre, year,
             track_number, codec, file_size, file_modified)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )");

        q.addBindValue(track.path);
        q.addBindValue(track.title.left(500));
        q.addBindValue(track.artist.left(500));
        q.addBindValue(track.album.left(500));
        q.addBindValue(track.duration);
        q.addBindValue(track.bitrate);
        q.addBindValue(track.genre.left(100));
        q.addBindValue(track.year);
        q.addBindValue(track.trackNumber);
        q.addBindValue(track.codec);
        q.addBindValue(track.fileSize);
        q.addBindValue(static_cast<qint64>(track.fileModified));

        if (!q.exec()) {
            qWarning() << "Insert failed:" << q.lastError().text();
            return false;
        }

        d->trackCount.fetch_add(1);
        return true;
    }

    void Library::processBatch(const std::vector<Track>& tracks) {
        auto conn = d->pool->acquire();
        if (!conn.isValid()) return;

        if (!(*conn).transaction()) {
            qWarning() << "Failed to start transaction";
            return;
        }

        QSqlQuery q(*conn);
        q.prepare(R"(
            INSERT OR REPLACE INTO tracks
            (path, title, artist, album, duration, bitrate, genre, year,
             track_number, codec, file_size, file_modified)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )");

        int success = 0;
        for (const auto& track : tracks) {
            q.addBindValue(track.path);
            q.addBindValue(track.title.left(500));
            q.addBindValue(track.artist.left(500));
            q.addBindValue(track.album.left(500));
            q.addBindValue(track.duration);
            q.addBindValue(track.bitrate);
            q.addBindValue(track.genre.left(100));
            q.addBindValue(track.year);
            q.addBindValue(track.trackNumber);
            q.addBindValue(track.codec);
            q.addBindValue(track.fileSize);
            q.addBindValue(static_cast<qint64>(track.fileModified));

            if (q.exec()) {
                success++;
            } else {
                qWarning() << "Batch insert failed:" << q.lastError().text();
            }
        }

        if (!(*conn).commit()) {
            qWarning() << "Failed to commit transaction";
            (*conn).rollback();
        }

        d->trackCount.fetch_add(success);
    }

    bool Library::extractMetadata(const QString& path, Track& outTrack) {
        try {
            TagLib::FileRef f(path.toUtf8().constData());
            if (f.isNull() || !f.file()) return false;

            outTrack.path = path;

            // Extract tags
            if (f.tag()) {
                outTrack.title = QString::fromUtf8(
                    f.tag()->title().toCString(true)).trimmed();
                    outTrack.artist = QString::fromUtf8(
                        f.tag()->artist().toCString(true)).trimmed();
                        outTrack.album = QString::fromUtf8(
                            f.tag()->album().toCString(true)).trimmed();
                            outTrack.genre = QString::fromUtf8(
                                f.tag()->genre().toCString(true)).trimmed();
                                outTrack.year = f.tag()->year();
                                outTrack.trackNumber = f.tag()->track();
            }

            // Extract audio properties
            if (f.audioProperties()) {
                outTrack.duration = f.audioProperties()->lengthInSeconds();
                outTrack.bitrate = f.audioProperties()->bitrate();
            }

            // Detect codec from file extension
            QFileInfo info(path);
            outTrack.codec = info.suffix().toLower();

            // Fallback for title
            if (outTrack.title.isEmpty()) {
                outTrack.title = info.baseName();
            }

            return true;

        } catch (const std::exception& e) {
            qWarning() << "Metadata extraction failed for" << path << ":" << e.what();
            return false;
        }
    }

    QFuture<std::vector<Track>> Library::search(const QString& query, int limit) {
        return QtConcurrent::run([this, query, limit]() {
            auto conn = d->pool->acquire();
            std::vector<Track> results;

            if (!conn.isValid()) {
                return results;
            }

            QString pattern = "%" + query.simplified()
            .replace("%", "\\%")
            .replace("_", "\\_") + "%";

            QSqlQuery q(*conn);
            q.prepare(R"(
                SELECT id, path, title, artist, album, duration, bitrate,
                       genre, year, track_number, file_size, play_count, rating
                FROM tracks
                WHERE title LIKE ? ESCAPE '\'
                   OR artist LIKE ? ESCAPE '\'
                   OR album LIKE ? ESCAPE '\'
                ORDER BY
                    CASE
                        WHEN title = ? THEN 0
                        WHEN title LIKE ? THEN 1
                        ELSE 2
                    END,
                    artist, album, title
                LIMIT ?
            )");

            q.addBindValue(pattern);
            q.addBindValue(pattern);
            q.addBindValue(pattern);
            q.addBindValue(query);  // Exact match
            q.addBindValue(query + "%");  // Starts with
            q.addBindValue(limit);

            if (q.exec()) {
                while (q.next()) {
                    Track t;
                    t.id = q.value(0).toInt();
                    t.path = q.value(1).toString();
                    t.title = q.value(2).toString();
                    t.artist = q.value(3).toString();
                    t.album = q.value(4).toString();
                    t.duration = q.value(5).toInt();
                    t.bitrate = q.value(6).toInt();
                    t.genre = q.value(7).toString();
                    t.year = q.value(8).toInt();
                    t.trackNumber = q.value(9).toInt();
                    results.push_back(t);
                }
            }

            return results;
        });
    }

    void Library::updateTags(int trackId, const QVariantMap& tags) {
        auto conn = d->pool->acquire();
        if (!conn.isValid()) return;

        QStringList setFields;
        QVariantList values;

        auto addField = [&](const QString& field, const QString& tagKey) {
            if (tags.contains(tagKey)) {
                setFields.append(field + " = ?");
                values.append(tags[tagKey]);
            }
        };

        addField("title", "title");
        addField("artist", "artist");
        addField("album", "album");
        addField("genre", "genre");
        addField("year", "year");
        addField("track_number", "trackNumber");
        addField("rating", "rating");

        if (setFields.isEmpty()) return;

        QSqlQuery q(*conn);
        q.prepare(QString("UPDATE tracks SET %1 WHERE id = ?")
        .arg(setFields.join(", ")));

        for (const auto& value : values) {
            q.addBindValue(value);
        }
        q.addBindValue(trackId);

        if (q.exec()) {
            emit trackCountChanged(d->trackCount.load());
        }
    }

    QVariantMap Library::statistics() const {
        QVariantMap stats;
        stats["trackCount"] = d->trackCount.load();
        stats["scanning"] = d->scanning.load();
        stats["totalScanTime"] = d->totalScanTime.load();
        stats["totalScannedFiles"] = d->totalScannedFiles.load();
        stats["totalErrors"] = d->totalErrors.load();

        auto conn = d->pool->acquire();
        if (conn.isValid()) {
            QSqlQuery q(*conn);

            if (q.exec("SELECT COUNT(DISTINCT artist) FROM tracks") && q.next()) {
                stats["artistCount"] = q.value(0).toInt();
            }

            if (q.exec("SELECT COUNT(DISTINCT album) FROM tracks") && q.next()) {
                stats["albumCount"] = q.value(0).toInt();
            }

            if (q.exec("SELECT SUM(duration) FROM tracks") && q.next()) {
                stats["totalDuration"] = q.value(0).toLongLong();
            }

            if (q.exec("SELECT SUM(file_size) FROM tracks") && q.next()) {
                stats["totalSize"] = q.value(0).toLongLong();
            }
        }

        return stats;
    }

    void Library::cancelScan() {
        d->cancelScan.store(true);
    }

} // namespace Aegis
