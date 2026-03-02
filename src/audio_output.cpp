// audio_output.cpp - Complete audio output implementations
//
// [FIX Bug #4] QtAudioOutput stats_timer: il weak_self non era mai inizializzato
// (d->weak_self è sempre vuoto), quindi weak_self.lock() restituiva sempre nullptr
// e il corpo del timer non veniva mai eseguito. Corretto collegando il timer con
// `this` come contesto Qt: Qt disconnette automaticamente alla distruzione.

#include "audio_output.h"
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QMediaDevices>
#include <QCoreApplication>
#include <cmath>
#include <algorithm>
#include <memory>
#include <shared_mutex>

namespace Aegis {

    // ============================================================================
    // QtAudioOutput Implementation (Thread-Safe)
    // ============================================================================

    class QtAudioOutput::Private {
    public:
        QAudioFormat format;
        QAudioDevice device;
        std::unique_ptr<QAudioSink> audio_sink;
        QIODevice* audio_io = nullptr;

        OutputConfig config;
        std::atomic<bool> initialized{false};
        std::atomic<bool> active{false};
        std::atomic<double> volume{1.0};
        std::atomic<int> latency_ms{0};
        std::atomic<int> underrun_count{0};
        std::atomic<uint64_t> bytes_played{0};
        std::atomic<uint64_t> frames_played{0};

        QTimer stats_timer;
        mutable std::shared_mutex device_mutex;
        std::function<void(float*, int)> callback;
    };

    // [FIX Bug #4] Connessione con `this` come contesto invece di weak_ptr non inizializzato.
    QtAudioOutput::QtAudioOutput(QObject* parent)
    : AudioOutput(parent)
    , d(std::make_unique<Private>()) {

        d->stats_timer.setInterval(1000);

        connect(&d->stats_timer, &QTimer::timeout, this, [this]() {
            d->latency_ms.store(estimate_latency());
            emit stats_updated(d->bytes_played.load(), d->frames_played.load());
        });
    }

    QtAudioOutput::~QtAudioOutput() {
        shutdown();
    }

    bool QtAudioOutput::initialize(const OutputConfig& config) {
        if (d->initialized.load(std::memory_order_acquire)) return true;

        d->config = config;

        d->format.setSampleRate(config.sampleRate);
        d->format.setChannelCount(config.channels);
        d->format.setSampleFormat(QAudioFormat::Float);

        if (!d->format.isValid()) {
            emit error("Invalid audio format");
            return false;
        }

        {
            std::unique_lock lock(d->device_mutex);
            d->device = QMediaDevices::defaultAudioOutput();
            if (d->device.isNull()) {
                emit error("No audio output device available");
                return false;
            }

            if (!d->device.isFormatSupported(d->format)) {
                qWarning() << "Format not supported, using nearest";
                d->format = d->device.preferredFormat();
            }
        }

        d->initialized.store(true, std::memory_order_release);
        return true;
    }

    void QtAudioOutput::shutdown() {
        stop();
        {
            std::unique_lock lock(d->device_mutex);
            d->audio_sink.reset();
        }
        d->initialized.store(false, std::memory_order_release);
    }

    bool QtAudioOutput::isInitialized() const {
        return d->initialized.load(std::memory_order_acquire);
    }

    void QtAudioOutput::write(const float* interleavedData, int frames) {
        std::shared_lock lock(d->device_mutex);
        if (!d->audio_io || !d->audio_sink) return;

        const int bytes = frames * d->format.channelCount() * static_cast<int>(sizeof(float));
        d->audio_io->write(reinterpret_cast<const char*>(interleavedData), bytes);

        d->frames_played.fetch_add(frames, std::memory_order_relaxed);
        d->bytes_played.fetch_add(static_cast<uint64_t>(bytes), std::memory_order_relaxed);
    }

    void QtAudioOutput::setAudioCallback(std::function<void(float*, int)> callback) {
        d->callback = std::move(callback);
    }

    int QtAudioOutput::sampleRate() const {
        std::shared_lock lock(d->device_mutex);
        return d->format.sampleRate();
    }

    int QtAudioOutput::channels() const {
        std::shared_lock lock(d->device_mutex);
        return d->format.channelCount();
    }

    double QtAudioOutput::latencyMs() const {
        return static_cast<double>(d->latency_ms.load(std::memory_order_acquire));
    }

    int QtAudioOutput::estimate_latency() const {
        std::shared_lock lock(d->device_mutex);
        if (!d->audio_sink) return 0;
        qint64 buf_size      = d->audio_sink->bufferSize();
        int    bytes_per_frm = d->format.bytesPerFrame();
        int    rate          = d->format.sampleRate();
        if (bytes_per_frm == 0 || rate == 0) return 0;
        return static_cast<int>((buf_size / bytes_per_frm) * 1000 / rate);
    }

    void QtAudioOutput::start() {
        if (!d->initialized.load(std::memory_order_acquire) ||
            d->active.load(std::memory_order_acquire)) return;

        {
            std::unique_lock lock(d->device_mutex);
            d->audio_sink = std::make_unique<QAudioSink>(d->device, d->format);

            connect(d->audio_sink.get(), &QAudioSink::stateChanged,
                    this, &QtAudioOutput::on_state_changed, Qt::DirectConnection);

            d->audio_io = d->audio_sink->start();
            if (!d->audio_io) {
                emit error("Failed to start audio output");
                return;
            }
        }

        d->active.store(true, std::memory_order_release);
        d->stats_timer.start();
        emit state_changed(true);
    }

    void QtAudioOutput::stop() {
        if (!d->active.load(std::memory_order_acquire)) return;

        {
            std::unique_lock lock(d->device_mutex);
            if (d->audio_sink) {
                d->audio_sink->stop();
            }
            d->audio_io = nullptr;
        }

        d->active.store(false, std::memory_order_release);
        d->stats_timer.stop();
        emit state_changed(false);
    }

    bool QtAudioOutput::isPlaying() const {
        return d->active.load(std::memory_order_acquire);
    }

    void QtAudioOutput::setVolume(double volume) {
        double v = std::clamp(volume, 0.0, 1.0);
        d->volume.store(v, std::memory_order_release);
        std::shared_lock lock(d->device_mutex);
        if (d->audio_sink) {
            d->audio_sink->setVolume(v);
        }
    }

    double QtAudioOutput::volume() const {
        return d->volume.load(std::memory_order_acquire);
    }

    void QtAudioOutput::on_state_changed(QAudio::State state) {
        switch (state) {
            case QAudio::ActiveState:
                qDebug() << "QtAudio: Active";
                break;

            case QAudio::SuspendedState:
                qDebug() << "QtAudio: Suspended";
                break;

            case QAudio::StoppedState: {
                std::shared_lock lock(d->device_mutex);
                if (d->audio_sink && d->audio_sink->error() != QAudio::NoError) {
                    emit error("Audio error: " +
                               QString::number(static_cast<int>(d->audio_sink->error())));
                }
                d->active.store(false, std::memory_order_release);
                emit state_changed(false);
                break;
            }

            case QAudio::IdleState:
                d->underrun_count.fetch_add(1, std::memory_order_relaxed);
                emit underrun_detected();
                break;

            default:
                break;
        }
    }

    // ============================================================================
    // PipeWire Backend Implementation (Thread-Safe)
    // ============================================================================

#if defined(AEGIS_HAVE_PIPEWIRE)

    PipeWireOutput::PipeWireOutput(QObject* parent)
    : AudioOutput(parent)
    {
        pw_init(nullptr, nullptr);
    }

    PipeWireOutput::~PipeWireOutput() {
        shutdown();
        pw_deinit();
    }

    bool PipeWireOutput::initialize(const OutputConfig& config) {
        if (m_stream) return true;

        m_config = config;

        m_loop = pw_main_loop_new(nullptr);
        if (!m_loop) { emit error("Failed to create PipeWire main loop"); return false; }

        m_context = pw_context_new(pw_main_loop_get_loop(m_loop), nullptr, 0);
        if (!m_context) { emit error("Failed to create PipeWire context"); return false; }

        pw_properties* props = pw_properties_new(
            PW_KEY_MEDIA_TYPE,     "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE,     "Music",
            PW_KEY_APP_NAME,       "Aegis",
            PW_KEY_NODE_NAME,      m_nodeName.toUtf8().constData(),
            nullptr);

        m_stream = pw_stream_new_simple(
            pw_main_loop_get_loop(m_loop),
            m_nodeName.toUtf8().constData(),
            props, nullptr, this);

        if (!m_stream) { emit error("Failed to create PipeWire stream"); return false; }

        uint8_t buf[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
        spa_audio_info_raw info = {};
        info.format   = SPA_AUDIO_FORMAT_F32;
        info.rate     = static_cast<uint32_t>(config.sampleRate);
        info.channels = static_cast<uint32_t>(config.channels);
        const spa_pod* params[1];
        params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

        static const pw_stream_events stream_events = {
            .version = PW_VERSION_STREAM_EVENTS,
            .process = &PipeWireOutput::onProcess,
        };
        m_streamListener = new spa_hook{};
        pw_stream_add_listener(m_stream, m_streamListener, &stream_events, this);

        pw_stream_flags flags = static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);
        if (pw_stream_connect(m_stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, params, 1) < 0) {
            emit error("Failed to connect PipeWire stream");
            return false;
        }

        return true;
    }

    void PipeWireOutput::shutdown() {
        stop();
        if (m_stream)         { pw_stream_destroy(m_stream);   m_stream  = nullptr; }
        if (m_streamListener) { delete m_streamListener;        m_streamListener = nullptr; }
        if (m_context)        { pw_context_destroy(m_context);  m_context = nullptr; }
        if (m_loop)           { pw_main_loop_destroy(m_loop);   m_loop    = nullptr; }
    }

    void PipeWireOutput::write(const float*, int) { /* pull-mode only */ }

    void PipeWireOutput::setAudioCallback(std::function<void(float*, int)> callback) {
        m_audioCallback = std::move(callback);
    }

    int    PipeWireOutput::sampleRate() const { return m_config.sampleRate; }
    int    PipeWireOutput::channels()   const { return m_config.channels;   }
    double PipeWireOutput::latencyMs()  const { return m_currentLatency.load(std::memory_order_acquire); }

    void PipeWireOutput::start() {
        if (m_running.load(std::memory_order_acquire)) return;
        if (!m_loop) return;
        m_running.store(true, std::memory_order_release);
        m_playing.store(true, std::memory_order_release);
        m_loopThread = std::thread([this]() { pw_main_loop_run(m_loop); });
        emit state_changed(true);
    }

    void PipeWireOutput::stop() {
        if (!m_running.load(std::memory_order_acquire)) return;
        m_running.store(false, std::memory_order_release);
        if (m_loop) pw_main_loop_quit(m_loop);
        if (m_loopThread.joinable()) m_loopThread.join();
        m_playing.store(false, std::memory_order_release);
        emit state_changed(false);
    }

    bool   PipeWireOutput::isPlaying() const { return m_playing.load(std::memory_order_acquire); }
    void   PipeWireOutput::setVolume(double v) { m_volume.store(std::clamp(v,0.0,1.0), std::memory_order_release); }
    double PipeWireOutput::volume()    const { return m_volume.load(std::memory_order_acquire); }

    QString PipeWireOutput::nodeName() const           { return m_nodeName; }
    void    PipeWireOutput::setNodeName(const QString& n) { m_nodeName = n; }

    // static
    void PipeWireOutput::onProcess(void* userdata) {
        auto* self = static_cast<PipeWireOutput*>(userdata);
        if (self) self->processBuffer(nullptr);
    }

    void PipeWireOutput::processBuffer(struct pw_buffer*) {
        if (!m_stream) return;
        pw_buffer* buf = pw_stream_dequeue_buffer(m_stream);
        if (!buf) return;

        auto*    data     = static_cast<float*>(buf->buffer->datas[0].data);
        uint32_t n_frames = buf->buffer->datas[0].maxsize /
                            (static_cast<uint32_t>(m_config.channels) * sizeof(float));

        if (m_audioCallback && m_running.load(std::memory_order_acquire)) {
            m_audioCallback(data, static_cast<int>(n_frames));
            double vol = m_volume.load(std::memory_order_acquire);
            if (vol != 1.0) {
                float fv = static_cast<float>(vol);
                for (uint32_t i = 0; i < n_frames * static_cast<uint32_t>(m_config.channels); ++i)
                    data[i] *= fv;
            }
        } else {
            std::fill(data, data + n_frames * static_cast<uint32_t>(m_config.channels), 0.0f);
        }

        buf->buffer->datas[0].chunk->size   = n_frames * static_cast<uint32_t>(m_config.channels) * sizeof(float);
        buf->buffer->datas[0].chunk->offset = 0;
        buf->buffer->datas[0].chunk->stride = static_cast<int32_t>(m_config.channels) * static_cast<int32_t>(sizeof(float));

        pw_stream_queue_buffer(m_stream, buf);
    }

    void PipeWireOutput::updateLatency() {
        if (!m_stream) return;
        pw_time t{};
        if (pw_stream_get_time_n(m_stream, &t, sizeof(t)) == 0 && t.rate.denom > 0) {
            double lat = static_cast<double>(t.delay) * 1000.0 * t.rate.num / t.rate.denom;
            m_currentLatency.store(lat, std::memory_order_release);
        }
    }

#endif // AEGIS_HAVE_PIPEWIRE

    // ============================================================================
    // Factory
    // ============================================================================

    std::unique_ptr<AudioOutput> AudioOutputFactory::create(OutputBackend preferred) {
        if (preferred == OutputBackend::Auto) {
#if defined(AEGIS_HAVE_PIPEWIRE)
            preferred = OutputBackend::PipeWire;
#else
            preferred = OutputBackend::QtMultimedia;
#endif
        }

        switch (preferred) {
#if defined(AEGIS_HAVE_PIPEWIRE)
            case OutputBackend::PipeWire:
                return std::make_unique<PipeWireOutput>();
#endif
            case OutputBackend::QtMultimedia:
            default:
                return std::make_unique<QtAudioOutput>();
        }
    }

    bool AudioOutputFactory::isBackendAvailable(OutputBackend backend) {
        switch (backend) {
            case OutputBackend::PipeWire:
#if defined(AEGIS_HAVE_PIPEWIRE)
                return true;
#else
                return false;
#endif
            case OutputBackend::ALSA:
#if defined(AEGIS_HAVE_ALSA)
                return true;
#else
                return false;
#endif
            case OutputBackend::QtMultimedia:
                return true;
            default:
                return false;
        }
    }

    QString AudioOutputFactory::backendName(OutputBackend backend) {
        switch (backend) {
            case OutputBackend::PipeWire:     return "PipeWire";
            case OutputBackend::QtMultimedia: return "Qt Multimedia";
            case OutputBackend::ALSA:         return "ALSA";
            case OutputBackend::Auto:         return "Auto";
            default:                          return "Unknown";
        }
    }

} // namespace Aegis
