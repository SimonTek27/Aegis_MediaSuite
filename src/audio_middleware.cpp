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

                                             it->second->shutdown();
                                             m_endpoints.erase(it);
                                             m_routingTable.remove(name);

                                             // Remove from other routes
                                             for (auto it = m_routingTable.begin(); it != m_routingTable.end(); ++it) {
                                                 auto &destinations = it.value();
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

} // namespace Aegis
