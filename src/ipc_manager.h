// ipc_manager.h - Complete IPC system for single-instance coordination
#pragma once

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSharedMemory>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QCoreApplication>
#include <QUrl>
#include <memory>
#include <variant>
#include <optional>

namespace Aegis {

    // ============================================================================
    // IPC Message Types
    // ============================================================================

    struct IpcFileOpenMessage {
        QStringList files;
        QString suggestedMode;
        qint64 timestamp{QDateTime::currentMSecsSinceEpoch()};
    };

    struct IpcCommandMessage {
        enum class Command {
            Play,
            Pause,
            Stop,
            Next,
            Previous,
            Raise,
            Quit,
            Ping
        };
        Command command;
        QVariantMap parameters;
    };

    struct IpcStatusRequest {
        enum class RequestType {
            PlaybackStatus,
            LibraryStats,
            ActiveMode,
            All
        };
        RequestType type;
    };

    using IpcMessage = std::variant<
    IpcFileOpenMessage,
    IpcCommandMessage,
    IpcStatusRequest,
    QString  // Raw text message
    >;

    // ============================================================================
    // IPC Manager - Full-Featured Inter-Process Communication
    // ============================================================================

    class IpcManager : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool isPrimary READ isPrimary NOTIFY primaryChanged)
        Q_PROPERTY(QString instanceId READ instanceId CONSTANT)
        Q_PROPERTY(QStringList connectedClients READ connectedClients NOTIFY clientsChanged)

    public:
        static IpcManager& instance() {
            static IpcManager manager;
            return manager;
        }

        ~IpcManager() override {
            cleanup();
        }

        bool isPrimary() const { return m_isPrimary; }
        QString instanceId() const { return m_instanceId; }
        QStringList connectedClients() const { return m_connectedClients.keys(); }

        // Send message to primary instance
        bool sendToPrimary(const IpcMessage& message, int timeoutMs = 5000) {
            if (m_isPrimary) {
                // Already primary, handle locally
                handleMessage(message, "local");
                return true;
            }

            return sendViaLocalSocket(message, timeoutMs);
        }

        // Broadcast to all clients (primary only)
        bool broadcastToClients(const IpcMessage& message) {
            if (!m_isPrimary) return false;

            QJsonDocument doc = serializeMessage(message);
            QByteArray data = doc.toJson(QJsonDocument::Compact);

            for (auto* socket : m_connectedClients) {
                if (socket && socket->state() == QLocalSocket::ConnectedState) {
                    socket->write(data + "\n");
                    socket->flush();
                }
            }
            return true;
        }

    signals:
        void primaryChanged();
        void clientsChanged();
        void messageReceived(const IpcMessage& message, const QString& clientId);
        void fileOpenRequested(const QStringList& files, const QString& mode);
        void commandReceived(IpcCommandMessage::Command cmd, const QVariantMap& params);
        void error(const QString& message);

    private:
        IpcManager(QObject* parent = nullptr) : QObject(parent) {
            m_instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            initialize();
        }

        void initialize() {
            // Set up shared memory for primary detection
            QString sharedMemKey = "aegis_ipc_" + QCoreApplication::applicationName();
            m_sharedMemory = std::make_unique<QSharedMemory>(sharedMemKey);

            // Try to attach to existing
            if (m_sharedMemory->attach()) {
                m_sharedMemory->detach();
            }

            // Try to create - if succeeds, we're primary
            if (m_sharedMemory->create(sizeof(quint64))) {
                m_sharedMemory->lock();
                quint64* data = static_cast<quint64*>(m_sharedMemory->data());
                *data = QCoreApplication::applicationPid();
                m_sharedMemory->unlock();
                m_isPrimary = true;
                setupLocalServer();
            } else {
                m_isPrimary = false;
            }

            // Start heartbeat timer
            m_heartbeatTimer.setInterval(2000);
            connect(&m_heartbeatTimer, &QTimer::timeout, this, &IpcManager::heartbeat);
            m_heartbeatTimer.start();

            qDebug() << "IPC Manager initialized: primary=" << m_isPrimary
            << "instance=" << m_instanceId;
        }

        void setupLocalServer() {
            m_localServer = std::make_unique<QLocalServer>(this);

            QString serverName = "aegis_" + QCoreApplication::applicationName();

            // Remove stale server if exists
            QLocalServer::removeServer(serverName);

            if (!m_localServer->listen(serverName)) {
                emit error("Failed to start IPC server: " + m_localServer->errorString());
                return;
            }

            connect(m_localServer.get(), &QLocalServer::newConnection,
                    this, &IpcManager::onNewConnection);

            qDebug() << "IPC server listening on:" << serverName;
        }

        void onNewConnection() {
            while (auto* socket = m_localServer->nextPendingConnection()) {
                QString clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                m_connectedClients[clientId] = socket;

                connect(socket, &QLocalSocket::readyRead, this, [this, clientId, socket]() {
                    onClientData(clientId, socket);
                });

                connect(socket, &QLocalSocket::disconnected, this, [this, clientId, socket]() {
                    socket->deleteLater();
                    m_connectedClients.remove(clientId);
                    emit clientsChanged();
                });

                emit clientsChanged();
            }
        }

        void onClientData(const QString& clientId, QLocalSocket* socket) {
            while (socket->canReadLine()) {
                QByteArray line = socket->readLine().trimmed();
                if (line.isEmpty()) continue;

                QJsonParseError error;
                QJsonDocument doc = QJsonDocument::fromJson(line, &error);

                if (error.error != QJsonParseError::NoError) {
                    qWarning() << "Invalid JSON from client:" << error.errorString();
                    continue;
                }

                auto message = deserializeMessage(doc);
                handleMessage(message, clientId);
            }
        }

        bool sendViaLocalSocket(const IpcMessage& message, int timeoutMs) {
            QLocalSocket socket;
            socket.connectToServer("aegis_" + QCoreApplication::applicationName());

            if (!socket.waitForConnected(1000)) {
                qWarning() << "Could not connect to primary instance:"
                << socket.errorString();
                return false;
            }

            QJsonDocument doc = serializeMessage(message);
            QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";

            socket.write(data);
            if (!socket.waitForBytesWritten(timeoutMs / 2)) {
                return false;
            }

            // Wait for acknowledgment
            if (socket.waitForReadyRead(timeoutMs / 2)) {
                QByteArray response = socket.readLine();
                return response.trimmed() == "ACK";
            }

            return false;
        }

        void handleMessage(const IpcMessage& message, const QString& clientId) {
            std::visit([this, &clientId](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;

                if constexpr (std::is_same_v<T, IpcFileOpenMessage>) {
                    emit fileOpenRequested(arg.files, arg.suggestedMode);
                    emit messageReceived(arg, clientId);

                } else if constexpr (std::is_same_v<T, IpcCommandMessage>) {
                    emit commandReceived(arg.command, arg.parameters);
                    emit messageReceived(arg, clientId);

                } else if constexpr (std::is_same_v<T, IpcStatusRequest>) {
                    // Handle status request - would need integration with Core
                    emit messageReceived(arg, clientId);

                } else if constexpr (std::is_same_v<T, QString>) {
                    emit messageReceived(arg, clientId);
                }
            }, message);
        }

        QJsonDocument serializeMessage(const IpcMessage& message) {
            QJsonObject root;
            root["timestamp"] = QDateTime::currentMSecsSinceEpoch();
            root["instanceId"] = m_instanceId;

            std::visit([&root](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;

                if constexpr (std::is_same_v<T, IpcFileOpenMessage>) {
                    root["type"] = "file_open";
                    QJsonArray files;
                    for (const auto& f : arg.files) files.append(f);
                    root["files"] = files;
                    root["mode"] = arg.suggestedMode;

                } else if constexpr (std::is_same_v<T, IpcCommandMessage>) {
                    root["type"] = "command";
                    root["command"] = static_cast<int>(arg.command);
                    root["parameters"] = QJsonObject::fromVariantMap(arg.parameters);

                } else if constexpr (std::is_same_v<T, IpcStatusRequest>) {
                    root["type"] = "status_request";
                    root["requestType"] = static_cast<int>(arg.type);

                } else if constexpr (std::is_same_v<T, QString>) {
                    root["type"] = "text";
                    root["text"] = arg;
                }
            }, message);

            return QJsonDocument(root);
        }

        IpcMessage deserializeMessage(const QJsonDocument& doc) {
            QJsonObject root = doc.object();
            QString type = root["type"].toString();

            if (type == "file_open") {
                IpcFileOpenMessage msg;
                QJsonArray files = root["files"].toArray();
                for (int _i = 0; _i < files.size(); ++_i) {
                    msg.files.append(files.at(_i).toString());
                }
                msg.suggestedMode = root["mode"].toString();
                return msg;

            } else if (type == "command") {
                IpcCommandMessage msg;
                msg.command = static_cast<IpcCommandMessage::Command>(
                    root["command"].toInt());
                msg.parameters = root["parameters"].toObject().toVariantMap();
                return msg;

            } else if (type == "status_request") {
                IpcStatusRequest msg;
                msg.type = static_cast<IpcStatusRequest::RequestType>(
                    root["requestType"].toInt());
                return msg;

            } else if (type == "text") {
                return root["text"].toString();
            }

            return QString("Unknown message type");
        }

        void heartbeat() {
            if (!m_isPrimary) return;

            // Check if we're still primary by verifying shared memory
            if (m_sharedMemory && m_sharedMemory->isAttached()) {
                m_sharedMemory->lock();
                quint64* data = static_cast<quint64*>(m_sharedMemory->data());
                if (data && *data != QCoreApplication::applicationPid()) {
                    // Another instance stole primary status - rare but handle
                    m_sharedMemory->unlock();
                    handlePrimaryLoss();
                    return;
                }
                m_sharedMemory->unlock();
            }

            // Broadcast heartbeat to clients
            broadcastToClients(QString("heartbeat"));
        }

        void handlePrimaryLoss() {
            qWarning() << "Primary status lost!";
            m_isPrimary = false;
            emit primaryChanged();

            // Clean up and try to become primary again
            cleanup();
            QTimer::singleShot(1000, this, [this]() { initialize(); });
        }

        void cleanup() {
            if (m_localServer) {
                m_localServer->close();
                m_localServer.reset();
            }

            for (auto* socket : m_connectedClients) {
                socket->disconnectFromServer();
                socket->deleteLater();
            }
            m_connectedClients.clear();

            if (m_sharedMemory && m_sharedMemory->isAttached()) {
                m_sharedMemory->detach();
            }
        }

        bool m_isPrimary{false};
        QString m_instanceId;
        std::unique_ptr<QSharedMemory> m_sharedMemory;
        std::unique_ptr<QLocalServer> m_localServer;
        QHash<QString, QLocalSocket*> m_connectedClients;
        QTimer m_heartbeatTimer;
    };

} // namespace Aegis
