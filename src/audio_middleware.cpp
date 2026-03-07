// audio_middleware.cpp - Audio middleware implementation
#include "audio_middleware.h"
#include <QDebug>
#include <QDataStream>

namespace Aegis {

    // =============================================================================
    // SharedRingBuffer Implementation
    // =============================================================================

    template<typename T>
    SharedRingBuffer<T>::SharedRingBuffer(const QString& name, size_t capacity, bool create)
    : m_name(name), m_capacity(capacity), m_creator(create) {}

    template<typename T>
    SharedRingBuffer<T>::~SharedRingBuffer() {
        shutdown();
    }

    template<typename T>
    bool SharedRingBuffer<T>::initialize() {
        if (m_initialized) return true;

        size_t totalSize = sizeof(Header) + m_capacity * sizeof(T);

        if (m_creator) {
            m_memory.setKey(m_name);
            if (!m_memory.create(totalSize)) {
                if (!m_memory.attach()) {
                    return false;
                }
            }
        } else {
            m_memory.setKey(m_name);
            if (!m_memory.attach()) {
                return false;
            }
        }

        m_header = static_cast<Header*>(m_memory.data());
        m_buffer = reinterpret_cast<T*>(static_cast<char*>(m_memory.data()) + sizeof(Header));

        if (m_creator) {
            new (m_header) Header();
            m_header->capacity = m_capacity;
            m_header->elementSize = sizeof(T);
            m_header->writeIndex = 0;
            m_header->readIndex = 0;
        }

        m_initialized = true;
        return true;
    }

    template<typename T>
    void SharedRingBuffer<T>::shutdown() {
        m_initialized = false;
        m_memory.detach();
    }

    template<typename T>
    bool SharedRingBuffer<T>::write(const T* data, size_t count) {
        if (!m_initialized || !m_header) return false;

        uint32_t writeIdx = m_header->writeIndex.load(std::memory_order_relaxed);
        uint32_t readIdx = m_header->readIndex.load(std::memory_order_acquire);
        uint32_t available = m_header->capacity - (writeIdx - readIdx);

        if (count > available) return false; // Buffer full

        for (size_t i = 0; i < count; i++) {
            m_buffer[(writeIdx + i) % m_header->capacity] = data[i];
        }

        m_header->writeIndex.store(writeIdx + count, std::memory_order_release);
        return true;
    }

    template<typename T>
    size_t SharedRingBuffer<T>::read(T* data, size_t maxCount) {
        if (!m_initialized || !m_header) return 0;

        uint32_t readIdx = m_header->readIndex.load(std::memory_order_relaxed);
        uint32_t writeIdx = m_header->writeIndex.load(std::memory_order_acquire);
        uint32_t available = writeIdx - readIdx;

        size_t toRead = std::min(static_cast<size_t>(available), maxCount);

        for (size_t i = 0; i < toRead; i++) {
            data[i] = m_buffer[(readIdx + i) % m_header->capacity];
        }

        m_header->readIndex.store(readIdx + toRead, std::memory_order_release);
        return toRead;
    }

    template<typename T>
    size_t SharedRingBuffer<T>::available() const {
        if (!m_initialized || !m_header) return 0;
        return m_header->writeIndex.load(std::memory_order_acquire) -
        m_header->readIndex.load(std::memory_order_acquire);
    }

    // =============================================================================
    // AudioMiddleware Implementation
    // =============================================================================

    // =============================================================================
    // AudioEndpoint base class
    // =============================================================================

    AudioEndpoint::AudioEndpoint(const StreamConfig& config, QObject* parent)
        : QObject(parent), m_config(config) {}

    AudioEndpoint::~AudioEndpoint() = default;

    void AudioEndpoint::setProcessCallback(std::function<void(float*, int)> callback) {
        m_processCallback = std::move(callback);
    }

    // LocalSocketEndpoint private slots
    void LocalSocketEndpoint::onNewConnection()   {}
    void LocalSocketEndpoint::onDataAvailable()   {}
    void LocalSocketEndpoint::onDisconnected()    {}

    // NetworkAudioEndpoint private slots
    void NetworkAudioEndpoint::onUdpReadyRead()  {}
    void NetworkAudioEndpoint::onTcpReadyRead()  {}
    void NetworkAudioEndpoint::onConnected()     {}
    void NetworkAudioEndpoint::onDisconnected()  {}

    // =============================================================================
    // AudioMiddleware missing Q_INVOKABLE stubs
    // =============================================================================

    QStringList AudioMiddleware::endpointNames() const {
        return m_endpoints.keys();
    }
    bool AudioMiddleware::endpointExists(const QString& name) const {
        return m_endpoints.contains(name);
    }
    void AudioMiddleware::disconnectEndpoints(const QString& source, const QString& destination) {
        m_routingTable[source].removeAll(destination);
    }
    bool AudioMiddleware::isConnected(const QString& source, const QString& destination) {
        return m_routingTable.value(source).contains(destination);
    }
    bool AudioMiddleware::startEndpoint(const QString& name) {
        auto it = m_endpoints.find(name);
        return it != m_endpoints.end() && it.value()->initialize();
    }
    void AudioMiddleware::stopEndpoint(const QString& name) {
        auto it = m_endpoints.find(name);
        if (it != m_endpoints.end()) it.value()->shutdown();
    }
    bool AudioMiddleware::isRunning(const QString& name) {
        auto it = m_endpoints.find(name);
        return it != m_endpoints.end() && it.value()->isRunning();
    }
    bool AudioMiddleware::injectAudio(const QString&, const float*, int)  { return false; }
    bool AudioMiddleware::captureAudio(const QString&, float*, int)       { return false; }
    bool AudioMiddleware::addEffectToEndpoint(const QString&, std::shared_ptr<AudioEffect>) { return false; }
    void AudioMiddleware::removeEffectFromEndpoint(const QString&, int)   {}
    bool AudioMiddleware::bridgeToApplication(const QString&, const QString&, StreamProtocol) { return false; }
    bool AudioMiddleware::bridgeToZoom(const QString&)                    { return false; }
    void AudioMiddleware::destroyVirtualDevice(const QString& name)       { destroyEndpoint(name); }
    void AudioMiddleware::setMonitoringEnabled(const QString&, bool)      {}
    float AudioMiddleware::getEndpointLevel(const QString&)               { return 0.0f; }
    bool AudioMiddleware::active() const                                  { return m_active; }

    // =============================================================================
    // GameAudioBridge stubs
    // =============================================================================

    GameAudioBridge::GameAudioBridge(AudioMiddleware* middleware, QObject* parent) : QObject(parent), m_middleware(middleware) {}
    bool GameAudioBridge::initializeEngine(const QString&)               { return false; }
    void GameAudioBridge::shutdownEngine()                               {}
    void GameAudioBridge::postEvent(const QString&)                      {}
    void GameAudioBridge::setState(const QString&, const QString&)       {}
    void GameAudioBridge::setRTPC(const QString&, float)                 {}
    void GameAudioBridge::setSwitch(const QString&, const QString&)      {}
    void GameAudioBridge::setListenerPosition(float, float, float)       {}
    void GameAudioBridge::setSourcePosition(const QString&, float, float, float) {}
    void GameAudioBridge::setSourceOcclusion(const QString&, float)      {}
    bool GameAudioBridge::loadBank(const QString&)                       { return false; }
    void GameAudioBridge::unloadBank(const QString&)                     {}

    // =============================================================================
    // StreamingAudio stubs
    // =============================================================================

    StreamingAudio::StreamingAudio(AudioMiddleware* middleware, QObject* parent) : QObject(parent), m_middleware(middleware) {}
    bool StreamingAudio::setupOBSCapture(const QString&)                 { return false; }
    bool StreamingAudio::setupOBSOutput(const QString&)                  { return false; }
    bool StreamingAudio::setupVirtualMicrophone(const QString&)          { return false; }
    bool StreamingAudio::setupVirtualSpeaker(const QString&)             { return false; }
    void StreamingAudio::setStreamMix(const QString&, float)             {}
    void StreamingAudio::muteStreamSource(const QString&, bool)          {}

    // =============================================================================
    // AudioMiddleware Implementation
    // =============================================================================

    AudioMiddleware::AudioMiddleware(AudioEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine) {}

    AudioMiddleware::~AudioMiddleware() {
        stopAll();
    }

    bool AudioMiddleware::createEndpoint(const QString& name,
                                         StreamProtocol protocol,
                                         StreamDirection direction) {
        if (m_endpoints.contains(name)) return false;

        StreamConfig config;
        config.endpointName = name;
        config.protocol = protocol;
        config.direction = direction;

        std::unique_ptr<AudioEndpoint> endpoint;

        switch (protocol) {
            case StreamProtocol::SharedMemory:
                endpoint = std::make_unique<SharedMemoryEndpoint>(config);
                break;
            case StreamProtocol::LocalSocket:
                endpoint = std::make_unique<LocalSocketEndpoint>(config);
                break;
            case StreamProtocol::UDP:
            case StreamProtocol::TCP:
                endpoint = std::make_unique<NetworkAudioEndpoint>(config);
                break;
            case StreamProtocol::PipeWire:
                endpoint = std::make_unique<PipeWireEndpoint>(config);
                break;
            default:
                return false;
        }

        connect(endpoint.get(), &AudioEndpoint::audioReceived,
                this, &AudioMiddleware::onEndpointAudio);
        connect(endpoint.get(), &AudioEndpoint::error,
                [this, name](const QString& err) { emit error(name, err); });

        m_endpoints[name] = std::move(endpoint);
        emit endpointsChanged();
        return true;
    }

    void AudioMiddleware::destroyEndpoint(const QString& name) {
        auto it = m_endpoints.find(name);
        if (it == m_endpoints.end()) return;

        it.value()->shutdown();
        m_endpoints.erase(it);
        m_routingTable.remove(name);

        // Remove from other routes
        for (auto itRoute = m_routingTable.begin(); itRoute != m_routingTable.end(); ++itRoute) {
            auto &destinations = itRoute.value();
            destinations.removeAll(name);
        }

        emit endpointsChanged();
    }

    bool AudioMiddleware::connectEndpoints(const QString& source, const QString& destination) {
                                             if (!m_endpoints.contains(source) || !m_endpoints.contains(destination)) {
                                                 return false;
                                             }

                                             m_routingTable[source].append(destination);
                                             emit endpointConnected(source, destination);
                                             return true;
                                         }

                                         void AudioMiddleware::routeAudio(const QString& source, const QByteArray& data) {
                                             if (!m_routingTable.contains(source)) return;

                                             for (const QString& dest : m_routingTable[source]) {
                                                 auto it = m_endpoints.find(dest);
                                                 if (it != m_endpoints.end()) {
                                                     // Route audio to destination
                                                     // This would involve writing to the destination's input
                                                 }
                                             }
                                         }

                                         bool AudioMiddleware::bridgeToGameEngine(const QString& engineName,
                                                                                  const QString& endpointName) {
                                             // Create shared memory endpoint for game engine
                                             if (!createEndpoint(endpointName, StreamProtocol::SharedMemory,
                                                 StreamDirection::Duplex)) {
                                                 return false;
                                                 }

                                                 // Engine-specific setup
                                                 if (engineName == "Unity" || engineName == "Unreal" || engineName == "Godot") {
                                                     // Setup native plugin bridge
                                                     // This would involve creating a native plugin that connects to our endpoint
                                                     qDebug() << "Setup bridge to" << engineName << "via endpoint" << endpointName;
                                                     return true;
                                                 }

                                                 return false;
                                                                                  }

                                                                                  bool AudioMiddleware::bridgeToOBS(const QString& endpointName) {
                                                                                      // Create PipeWire or virtual device for OBS capture
                                                                                      return createVirtualDevice(endpointName + "_obs", 2, 48000);
                                                                                  }

                                                                                  bool AudioMiddleware::createVirtualDevice(const QString& name, int channels, int sampleRate) {
                                                                                      // Use PipeWire to create a virtual node that appears as hardware
                                                                                      // This allows other applications to use it as if it were a real audio device
                                                                                      #ifdef HAS_PIPEWIRE
                                                                                      // Implementation using pw_context_create_node
                                                                                      // ...
                                                                                      #endif
                                                                                      return false;
                                                                                  }

                                                                                   void AudioMiddleware::startAll() {
                                                                                       for (auto it = m_endpoints.begin(); it != m_endpoints.end(); ++it) {
                                                                                           auto &endpoint = it.value();
                                                                                           endpoint->initialize();
                                                                                       }
                                                                                       m_active = true;
                                                                                       emit activeChanged();
                                                                                   }

                                                                                   void AudioMiddleware::stopAll() {
                                                                                       for (auto it = m_endpoints.begin(); it != m_endpoints.end(); ++it) {
                                                                                           auto &endpoint = it.value();
                                                                                           endpoint->shutdown();
                                                                                       }
                                                                                       m_active = false;
                                                                                       emit activeChanged();
                                                                                   }

    void AudioMiddleware::onEndpointAudio(const QByteArray& data, int sampleRate) {
        Q_UNUSED(data) Q_UNUSED(sampleRate)
    }

    // ─── SharedMemoryEndpoint ─────────────────────────────────────────────────

    SharedMemoryEndpoint::SharedMemoryEndpoint(const StreamConfig& config, QObject* parent)
        : AudioEndpoint(config, parent)
        , m_ringBuffer(
              config.endpointName.isEmpty() ? QStringLiteral("aegis_shm") : config.endpointName,
              static_cast<size_t>(config.bufferFrames * config.channels * 4),
              true)
    {}

    SharedMemoryEndpoint::~SharedMemoryEndpoint() { shutdown(); }

    bool SharedMemoryEndpoint::initialize() {
        m_running.store(true);
        return true;
    }

    void SharedMemoryEndpoint::shutdown() {
        m_running.store(false);
    }

    // ─── LocalSocketEndpoint ──────────────────────────────────────────────────

    LocalSocketEndpoint::LocalSocketEndpoint(const StreamConfig& config, QObject* parent)
        : AudioEndpoint(config, parent) {}

    LocalSocketEndpoint::~LocalSocketEndpoint() { shutdown(); }

    bool LocalSocketEndpoint::initialize() {
        m_running.store(true);
        return true;
    }

    void LocalSocketEndpoint::shutdown() {
        m_running.store(false);
        if (m_socket) { m_socket->disconnectFromServer(); }
    }

    bool LocalSocketEndpoint::isRunning() const { return m_running.load(); }

    // ─── NetworkAudioEndpoint ─────────────────────────────────────────────────

    NetworkAudioEndpoint::NetworkAudioEndpoint(const StreamConfig& config, QObject* parent)
        : AudioEndpoint(config, parent) {}

    NetworkAudioEndpoint::~NetworkAudioEndpoint() { shutdown(); }

    bool NetworkAudioEndpoint::initialize() {
        m_running.store(true);
        return true;
    }

    void NetworkAudioEndpoint::shutdown() { m_running.store(false); }

    bool NetworkAudioEndpoint::isRunning() const { return m_running.load(); }

    // ─── PipeWireEndpoint ─────────────────────────────────────────────────────

    PipeWireEndpoint::PipeWireEndpoint(const StreamConfig& config, QObject* parent)
        : AudioEndpoint(config, parent) {}

    PipeWireEndpoint::~PipeWireEndpoint() { shutdown(); }

    bool PipeWireEndpoint::initialize() {
        m_running.store(true);
        return true;
    }

    void PipeWireEndpoint::shutdown() { m_running.store(false); }

    bool PipeWireEndpoint::isRunning() const { return m_running.load(); }

} // namespace Aegis
