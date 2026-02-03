// audio_middleware.h - Audio middleware abstraction for game audio, streaming, IPC
// Provides unified interface for audio routing between applications
// Uses all three pillars: audio, audio_effects, mpv_backend

#pragma once

#include "audio.h"
#include "audio_effects.h"
#include "audio_output.h"
#include <QObject>
#include <QSharedMemory>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <atomic>
#include <memory>
#include <functional>

namespace Aegis {

    // =============================================================================
    // Shared Memory Ring Buffer (Lock-free IPC)
    // =============================================================================

    template<typename T>
    class SharedRingBuffer {
    public:
        struct Header {
            std::atomic<uint32_t> writeIndex{0};
            std::atomic<uint32_t> readIndex{0};
            uint32_t capacity;
            uint32_t elementSize;
            char padding[64 - 16]; // Cache line alignment
        };

        SharedRingBuffer(const QString& name, size_t capacity, bool create);
        ~SharedRingBuffer();

        bool initialize();
        void shutdown();

        // Writer (single producer)
        bool write(const T* data, size_t count);

        // Reader (single consumer)
        size_t read(T* data, size_t maxCount);

        // Non-blocking peek
        size_t available() const;
        bool isEmpty() const { return available() == 0; }
        bool isFull() const;

        QString name() const { return m_name; }

    private:
        QString m_name;
        size_t m_capacity;
        bool m_creator;
        bool m_initialized = false;

        QSharedMemory m_memory;
        Header* m_header = nullptr;
        T* m_buffer = nullptr;

        // Cache line separation for reader/writer
        char m_padding[64];
    };

    // =============================================================================
    // Audio Stream Types
    // =============================================================================

    enum class StreamDirection {
        Input,      // Receiving audio
        Output,     // Sending audio
        Duplex      // Bidirectional
    };

    enum class StreamProtocol {
        SharedMemory,   // Fastest, same machine only
        LocalSocket,    // Unix/Windows named pipes
        UDP,            // Network, low latency
        TCP,            // Network, reliable
        PipeWire,       // Native PipeWire protocol
        Jack            // JACK audio connection
    };

    struct StreamConfig {
        StreamDirection direction = StreamDirection::Duplex;
        StreamProtocol protocol = StreamProtocol::SharedMemory;
        int sampleRate = 48000;
        int channels = 2;
        int bufferFrames = 1024;
        int latencyMs = 10;
        QString endpointName;  // e.g., "game_audio", "obs_capture"
        QString remoteEndpoint; // For network protocols
    };

    // =============================================================================
    // Audio Endpoint (Source or Sink)
    // =============================================================================

    class AudioEndpoint : public QObject {
        Q_OBJECT
    public:
        explicit AudioEndpoint(const StreamConfig& config, QObject* parent = nullptr);
        virtual ~AudioEndpoint();

        virtual bool initialize() = 0;
        virtual void shutdown() = 0;
        virtual bool isRunning() const = 0;

        StreamConfig config() const { return m_config; }
        QString name() const { return m_config.endpointName; }

        // Audio processing callback
        void setProcessCallback(std::function<void(float*, int)> callback);

    signals:
        void audioReceived(const QByteArray& data, int sampleRate);
        void connected();
        void disconnected();
        void error(const QString& message);

    protected:
        StreamConfig m_config;
        std::function<void(float*, int)> m_processCallback;
        std::atomic<bool> m_running{false};
    };

    // =============================================================================
    // Shared Memory Endpoint (Fastest local IPC)
    // =============================================================================

    class SharedMemoryEndpoint : public AudioEndpoint {
        Q_OBJECT
    public:
        explicit SharedMemoryEndpoint(const StreamConfig& config, QObject* parent = nullptr);
        ~SharedMemoryEndpoint() override;

        bool initialize() override;
        void shutdown() override;
        bool isRunning() const override { return m_running.load(); }

        // Write audio data (for output endpoints)
        bool writeAudio(const float* data, int frames);

        // Read audio data (for input endpoints)
        bool readAudio(float* data, int maxFrames, int& framesRead);

    private:
        void runReader();
        void runWriter();

        SharedRingBuffer<float> m_ringBuffer;
        std::unique_ptr<QThread> m_thread;
        QTimer m_processTimer;

        // Double buffering
        QVector<float> m_inputBuffer;
        QVector<float> m_outputBuffer;
    };

    // =============================================================================
    // Local Socket Endpoint (Cross-platform pipes)
    // =============================================================================

    class LocalSocketEndpoint : public AudioEndpoint {
        Q_OBJECT
    public:
        explicit LocalSocketEndpoint(const StreamConfig& config, QObject* parent = nullptr);
        ~LocalSocketEndpoint() override;

        bool initialize() override;
        void shutdown() override;
        bool isRunning() const override;

    private slots:
        void onNewConnection();
        void onDataAvailable();
        void onDisconnected();

    private:
        QLocalServer* m_server = nullptr;
        QLocalSocket* m_socket = nullptr;
        QByteArray m_buffer;
    };

    // =============================================================================
    // Network Audio Endpoint (UDP/TCP)
    // =============================================================================

    class NetworkAudioEndpoint : public AudioEndpoint {
        Q_OBJECT
    public:
        explicit NetworkAudioEndpoint(const StreamConfig& config, QObject* parent = nullptr);
        ~NetworkAudioEndpoint() override;

        bool initialize() override;
        void shutdown() override;
        bool isRunning() const override;

    private slots:
        void onUdpReadyRead();
        void onTcpReadyRead();
        void onConnected();
        void onDisconnected();

    private:
        void sendAudioPacket(const float* data, int frames);
        void processAudioPacket(const QByteArray& packet);

        QUdpSocket* m_udpSocket = nullptr;
        QTcpServer* m_tcpServer = nullptr;
        QTcpSocket* m_tcpSocket = nullptr;

        // Packet format: [header: 8 bytes][audio data]
        struct PacketHeader {
            uint32_t sequence;
            uint32_t timestamp;
        };

        uint32_t m_sequence = 0;
        QHostAddress m_remoteAddress;
        quint16 m_remotePort = 0;
    };

    // =============================================================================
    // PipeWire Endpoint (Native protocol)
    // =============================================================================

    class PipeWireEndpoint : public AudioEndpoint {
        Q_OBJECT
    public:
        explicit PipeWireEndpoint(const StreamConfig& config, QObject* parent = nullptr);
        ~PipeWireEndpoint() override;

        bool initialize() override;
        void shutdown() override;
        bool isRunning() const override;

    private:
        // Uses PipeWire's native protocol for node-to-node communication
        void setupPipeWireNode();

        struct pw_context* m_context = nullptr;
        struct pw_core* m_core = nullptr;
        struct pw_stream* m_stream = nullptr;
        struct pw_thread_loop* m_loop = nullptr;
    };

    // =============================================================================
    // Audio Middleware (Central Hub)
    // =============================================================================

    class AudioMiddleware : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool active READ active NOTIFY activeChanged)
        Q_PROPERTY(int endpointCount READ endpointCount NOTIFY endpointsChanged)

    public:
        /**
         * @brief Audio middleware for inter-application routing
         * @param engine AudioEngine for processing (Pillar 1)
         * @param parent QObject parent
         */
        explicit AudioMiddleware(AudioEngine* engine, QObject* parent = nullptr);
        ~AudioMiddleware();

        // Endpoint management
        Q_INVOKABLE bool createEndpoint(const QString& name,
                                        StreamProtocol protocol,
                                        StreamDirection direction);
        Q_INVOKABLE void destroyEndpoint(const QString& name);
        Q_INVOKABLE QStringList endpointNames() const;
        Q_INVOKABLE bool endpointExists(const QString& name) const;

        // Routing
        Q_INVOKABLE bool connectEndpoints(const QString& source, const QString& destination);
        Q_INVOKABLE void disconnectEndpoints(const QString& source, const QString& destination);
        Q_INVOKABLE bool isConnected(const QString& source, const QString& destination);

        // Control
        Q_INVOKABLE bool startEndpoint(const QString& name);
        Q_INVOKABLE void stopEndpoint(const QString& name);
        Q_INVOKABLE bool isRunning(const QString& name);

        // Audio injection/capture
        Q_INVOKABLE bool injectAudio(const QString& endpoint, const float* data, int frames);
        Q_INVOKABLE bool captureAudio(const QString& endpoint, float* data, int maxFrames);

        // Effects processing on streams
        Q_INVOKABLE bool addEffectToEndpoint(const QString& endpoint,
                                             std::shared_ptr<AudioEffect> effect);
        Q_INVOKABLE void removeEffectFromEndpoint(const QString& endpoint, int index);

        // Application bridging (high-level API)
        Q_INVOKABLE bool bridgeToApplication(const QString& appName,
                                             const QString& endpointName,
                                             StreamProtocol protocol = StreamProtocol::SharedMemory);
        Q_INVOKABLE bool bridgeToGameEngine(const QString& engineName,
                                            const QString& endpointName);
        Q_INVOKABLE bool bridgeToOBS(const QString& endpointName);
        Q_INVOKABLE bool bridgeToZoom(const QString& endpointName);

        // Virtual device creation (appears as hardware to other apps)
        Q_INVOKABLE bool createVirtualDevice(const QString& name,
                                             int channels = 2,
                                             int sampleRate = 48000);
        Q_INVOKABLE void destroyVirtualDevice(const QString& name);

        // Monitoring
        Q_INVOKABLE void setMonitoringEnabled(const QString& endpoint, bool enabled);
        Q_INVOKABLE float getEndpointLevel(const QString& endpoint);

        // Global control
        Q_INVOKABLE void startAll();
        Q_INVOKABLE void stopAll();
        bool active() const;

        // Access
        AudioEngine* engine() const { return m_engine; }
        AudioEndpoint* getEndpoint(const QString& name) const;

    signals:
        void activeChanged();
        void endpointsChanged();
        void endpointConnected(const QString& source, const QString& destination);
        void endpointDisconnected(const QString& source, const QString& destination);
        void audioReceived(const QString& endpoint, const QByteArray& data);
        void error(const QString& endpoint, const QString& message);

    private slots:
        void onEndpointAudio(const QByteArray& data, int sampleRate);
        void routeAudio(const QString& source, const QByteArray& data);

    private:
        void setupDefaultRoutes();

        AudioEngine* m_engine;  // Pillar 1
        QHash<QString, std::shared_ptr<AudioEndpoint>> m_endpoints;
        QHash<QString, QVector<QString>> m_routingTable; // source -> destinations
        QHash<QString, std::shared_ptr<EffectChain>> m_endpointEffects;

        bool m_active = false;
    };

    // =============================================================================
    // Game Audio Integration (Common middleware use case)
    // =============================================================================

    class GameAudioBridge : public QObject {
        Q_OBJECT
    public:
        explicit GameAudioBridge(AudioMiddleware* middleware, QObject* parent = nullptr);

        // Unity/Unreal/Godot integration
        Q_INVOKABLE bool initializeEngine(const QString& engineName);
        Q_INVOKABLE void shutdownEngine();

        // Event-driven audio (game audio middleware style)
        Q_INVOKABLE void postEvent(const QString& eventName);
        Q_INVOKABLE void setState(const QString& stateGroup, const QString& state);
        Q_INVOKABLE void setRTPC(const QString& rtpcName, float value);
        Q_INVOKABLE void setSwitch(const QString& switchGroup, const QString& switchValue);

        // 3D audio positioning
        Q_INVOKABLE void setListenerPosition(float x, float y, float z);
        Q_INVOKABLE void setSourcePosition(const QString& source, float x, float y, float z);
        Q_INVOKABLE void setSourceOcclusion(const QString& source, float occlusion);

        // Banks
        Q_INVOKABLE bool loadBank(const QString& bankPath);
        Q_INVOKABLE void unloadBank(const QString& bankName);

    private:
        AudioMiddleware* m_middleware;
        QString m_currentEngine;
        QHash<QString, float> m_rtpcValues;
    };

    // =============================================================================
    // Streaming Audio (OBS, Zoom, etc.)
    // =============================================================================

    class StreamingAudio : public QObject {
        Q_OBJECT
    public:
        explicit StreamingAudio(AudioMiddleware* middleware, QObject* parent = nullptr);

        // OBS integration
        Q_INVOKABLE bool setupOBSCapture(const QString& sourceName);
        Q_INVOKABLE bool setupOBSOutput(const QString& outputName);

        // Zoom/Teams integration
        Q_INVOKABLE bool setupVirtualMicrophone(const QString& name);
        Q_INVOKABLE bool setupVirtualSpeaker(const QString& name);

        // Mixing for streaming
        Q_INVOKABLE void setStreamMix(const QString& source, float level);
        Q_INVOKABLE void muteStreamSource(const QString& source, bool mute);

    private:
        AudioMiddleware* m_middleware;
        QHash<QString, float> m_streamMix;
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::StreamDirection)
Q_DECLARE_METATYPE(Aegis::StreamProtocol)
Q_DECLARE_METATYPE(Aegis::StreamConfig)
