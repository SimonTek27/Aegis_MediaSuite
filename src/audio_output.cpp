// audio_output.cpp - Complete audio output implementations

#include "audio_output.h"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <cmath>
#include <algorithm>
#include <memory>
#include <shared_mutex>

namespace Aegis {

    // ============================================================================
    // AudioOutput Base Class Implementation
    // ============================================================================

    AudioOutput::AudioOutput(QObject* parent) : QObject(parent) {}

    AudioOutput::~AudioOutput() = default;

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
        std::weak_ptr<QtAudioOutput> weak_self;
    };

    QtAudioOutput::QtAudioOutput(QObject* parent)
    : AudioOutput(parent)
    , d(std::make_unique<Private>()) {

        d->stats_timer.setInterval(1000);

        // Use weak pointer for timer callback to avoid dangling
        std::weak_ptr<QtAudioOutput> weak_self = d->weak_self;
        connect(&d->stats_timer, &QTimer::timeout, [this, weak_self]() {
            if (auto shared = weak_self.lock()) {
                // Update statistics periodically
                d->latency_ms.store(estimate_latency());
                emit stats_updated(d->bytes_played.load(), d->frames_played.load());
            }
        });
    }

    QtAudioOutput::~QtAudioOutput() {
        shutdown();
    }

    bool QtAudioOutput::initialize(const OutputConfig& config) {
        if (d->initialized.load(std::memory_order_acquire)) return true;

        d->config = config;

        // Set up audio format
        d->format.setSampleRate(config.sample_rate);
        d->format.setChannelCount(config.channels);
        d->format.setSampleFormat(QAudioFormat::Float);

        if (!d->format.isValid()) {
            emit error("Invalid audio format");
            return false;
        }

        {
            std::unique_lock lock(d->device_mutex);
            // Get default audio output device
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

    void QtAudioOutput::start() {
        if (!d->initialized.load(std::memory_order_acquire) ||
            d->active.load(std::memory_order_acquire)) return;

        {
            std::unique_lock lock(d->device_mutex);
            d->audio_sink = std::make_unique<QAudioSink>(d->device, d->format);

            // Use direct connection for state changes
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
        }

        d->active.store(false, std::memory_order_release);
        d->stats_timer.stop();
        emit state_changed(false);
    }

    void QtAudioOutput::set_volume(double volume) {
        double clamped_volume = std::clamp(volume, 0.0, 1.0);
        d->volume.store(clamped_volume, std::memory_order_release);

        std::shared_lock lock(d->device_mutex);
        if (d->audio_sink) {
            d->audio_sink->setVolume(clamped_volume);
        }
    }

    QString QtAudioOutput::current_device() const {
        std::shared_lock lock(d->device_mutex);
        return d->device.description();
    }

    bool QtAudioOutput::set_device(const QString& name) {
        std::unique_lock lock(d->device_mutex);
        for (const auto& device : QMediaDevices::audioOutputs()) {
            if (device.description() == name) {
                d->device = device;
                emit device_changed(name);
                return true;
            }
        }
        return false;
    }

    QStringList QtAudioOutput::available_devices() const {
        QStringList list;
        for (const auto& device : QMediaDevices::audioOutputs()) {
            list << device.description();
        }
        return list;
    }

    int QtAudioOutput::estimate_latency() const {
        std::shared_lock lock(d->device_mutex);
        if (!d->audio_sink) return 0;

        qint64 buffer_size = d->audio_sink->bufferSize();
        int bytes_per_frame = d->format.bytesPerFrame();
        if (bytes_per_frame == 0) return 0;

        return static_cast<int>((buffer_size / bytes_per_frame) * 1000 / d->format.sampleRate());
    }

    void QtAudioOutput::on_state_changed(QAudio::State state) {
        switch (state) {
            case QAudio::ActiveState:
                qDebug() << "QtAudio: Active";
                break;

            case QAudio::SuspendedState:
                qDebug() << "QtAudio: Suspended";
                break;

            case QAudio::StoppedState:
            {
                std::shared_lock lock(d->device_mutex);
                if (d->audio_sink && d->audio_sink->error() != QAudio::NoError) {
                    emit error("Audio error: " + d->audio_sink->errorString());
                }
            }
            d->active.store(false, std::memory_order_release);
            emit state_changed(false);
            break;

            case QAudio::IdleState:
                // Buffer underrun
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

    #if AEGIS_HAVE_PIPEWIRE

    struct PipeWireOutput::Private {
        pw_main_loop* mainloop = nullptr;
        pw_context* context = nullptr;
        pw_core* core = nullptr;
        pw_stream* stream = nullptr;
        spa_hook stream_listener;

        std::atomic<bool> initialized{false};
        std::atomic<bool> active{false};
        std::atomic<double> volume{1.0};
        std::atomic<int> latency_ms{0};
        std::atomic<int> underrun_count{0};
        std::atomic<uint64_t> bytes_played{0};
        std::atomic<uint64_t> frames_played{0};

        std::vector<float> buffer;
        std::thread mainloop_thread;
        std::mutex mainloop_mutex;
        std::condition_variable mainloop_cv;

        OutputConfig config;
        AudioCallback callback;

        ~Private() {
            cleanup();
        }

        void cleanup() {
            if (stream) pw_stream_destroy(stream);
            if (core) pw_core_disconnect(core);
            if (context) pw_context_destroy(context);
            if (mainloop) pw_main_loop_destroy(mainloop);
            stream = nullptr;
            core = nullptr;
            context = nullptr;
            mainloop = nullptr;
        }

        static void on_process(void* userdata) {
            auto* self = static_cast<PipeWireOutput*>(userdata);
            if (self) self->process();
        }

        static void on_destroy(void* userdata) {
            Q_UNUSED(userdata)
        }
    };

    PipeWireOutput::PipeWireOutput(QObject* parent)
    : AudioOutput(parent)
    , d(std::make_unique<Private>()) {
        pw_init(nullptr, nullptr);
    }

    PipeWireOutput::~PipeWireOutput() {
        shutdown();
        pw_deinit();
    }

    bool PipeWireOutput::initialize(const OutputConfig& config) {
        if (d->initialized.load(std::memory_order_acquire)) return true;

        d->config = config;

        if (!init_pipewire()) {
            emit error("Failed to initialize PipeWire");
            return false;
        }

        d->initialized.store(true, std::memory_order_release);
        return true;
    }

    void PipeWireOutput::shutdown() {
        stop();
        {
            std::lock_guard lock(d->mainloop_mutex);
            d->cleanup();
        }
        d->initialized.store(false, std::memory_order_release);
    }

    void PipeWireOutput::start() {
        if (!d->initialized.load(std::memory_order_acquire) ||
            d->active.load(std::memory_order_acquire)) return;

        d->active.store(true, std::memory_order_release);

        // Start mainloop in separate thread
        d->mainloop_thread = std::thread([this]() {
            std::unique_lock lock(d->mainloop_mutex);
            if (d->mainloop) {
                pw_main_loop_run(d->mainloop);
            }
        });

        emit state_changed(true);
    }

    void PipeWireOutput::stop() {
        if (!d->active.load(std::memory_order_acquire)) return;

        d->active.store(false, std::memory_order_release);

        {
            std::lock_guard lock(d->mainloop_mutex);
            if (d->mainloop) {
                pw_main_loop_quit(d->mainloop);
            }
        }

        if (d->mainloop_thread.joinable()) {
            d->mainloop_thread.join();
        }

        emit state_changed(false);
    }

    void PipeWireOutput::set_volume(double volume) {
        d->volume.store(std::clamp(volume, 0.0, 1.0), std::memory_order_release);
        // PipeWire volume control would go here
    }

    void PipeWireOutput::process() {
        // Get stream buffer
        pw_buffer* buf = pw_stream_dequeue_buffer(d->stream);
        if (!buf) return;

        auto* audio_data = static_cast<float*>(buf->buffer->datas[0].data);
        uint32_t n_frames = buf->buffer->datas[0].maxsize /
        (d->config.channels * sizeof(float));

        // Thread-safe callback invocation
        AudioCallback callback_copy;
        {
            std::shared_lock lock(m_callback_mutex);
            callback_copy = m_callback;
        }

        if (callback_copy && d->active.load(std::memory_order_acquire)) {
            // Fill buffer with audio
            callback_copy(audio_data, n_frames);

            // Apply volume
            double vol = d->volume.load(std::memory_order_acquire);
            if (vol != 1.0) {
                for (uint32_t i = 0; i < n_frames * d->config.channels; ++i) {
                    audio_data[i] *= vol;
                }
            }

            // Update statistics
            d->frames_played.fetch_add(n_frames, std::memory_order_relaxed);
            d->bytes_played.fetch_add(n_frames * d->config.channels * sizeof(float),
                                      std::memory_order_relaxed);
        }

        buf->buffer->datas[0].chunk->offset = 0;
        buf->buffer->datas[0].chunk->stride = d->config.channels * sizeof(float);
        buf->buffer->datas[0].chunk->size = n_frames * d->config.channels * sizeof(float);

        pw_stream_queue_buffer(d->stream, buf);
    }

    bool PipeWireOutput::init_pipewire() {
        std::lock_guard lock(d->mainloop_mutex);

        // Create main loop
        d->mainloop = pw_main_loop_new(nullptr);
        if (!d->mainloop) {
            qCritical() << "Failed to create PipeWire main loop";
            return false;
        }

        // Create context
        d->context = pw_context_new(pw_main_loop_get_loop(d->mainloop), nullptr, 0);
        if (!d->context) {
            qCritical() << "Failed to create PipeWire context";
            return false;
        }

        // Connect core
        d->core = pw_context_connect(d->context, nullptr, 0);
        if (!d->core) {
            qCritical() << "Failed to connect PipeWire core";
            return false;
        }

        // Create stream parameters
        uint8_t buffer[1024];
        auto params = spa_pod_builder(&SPA_POD_BUILDER_INIT(buffer, sizeof(buffer)));

        auto audio_info = spa_format_audio_raw_build(&params,
                                                     SPA_PARAM_EnumFormat,
                                                     &SPA_AUDIO_INFO_RAW_INIT(
                                                         .format = SPA_AUDIO_FORMAT_F32,
                                                         .channels = static_cast<uint32_t>(d->config.channels),
                                                                              .rate = static_cast<uint32_t>(d->config.sample_rate)));

        // Create stream
        d->stream = pw_stream_new(d->core, "Aegis Audio Output", nullptr);
        if (!d->stream) {
            qCritical() << "Failed to create PipeWire stream";
            return false;
        }

        // Set up stream events
        static const pw_stream_events stream_events = {
            .version = PW_VERSION_STREAM_EVENTS,
            .destroy = &Private::on_destroy,
            .process = &Private::on_process,
        };

        pw_stream_add_listener(d->stream, &d->stream_listener, &stream_events, this);

        // Set stream flags
        pw_stream_flags flags = static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS);

        // Connect stream
        int res = pw_stream_connect(d->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                    flags, &params, 1);

        if (res < 0) {
            qCritical() << "Failed to connect PipeWire stream:" << spa_strerror(res);
            return false;
        }

        return true;
    }

    #endif // AEGIS_HAVE_PIPEWIRE

    // ============================================================================
    // ALSA Backend Implementation (Thread-Safe)
    // ============================================================================

    #if AEGIS_HAVE_ALSA

    class AlsaOutput::Private {
    public:
        snd_pcm_t* pcm_handle = nullptr;
        OutputConfig config;

        std::atomic<bool> initialized{false};
        std::atomic<bool> active{false};
        std::atomic<bool> running{false};
        std::atomic<double> volume{1.0};
        std::atomic<int> latency_ms{0};
        std::atomic<int> underrun_count{0};
        std::atomic<uint64_t> bytes_played{0};
        std::atomic<uint64_t> frames_played{0};

        std::vector<float> buffer;
        std::thread audio_thread;
        mutable std::mutex alsa_mutex;
        std::condition_variable thread_cv;

        AudioCallback callback;

        ~Private() {
            shutdown();
        }

        void shutdown() {
            std::lock_guard lock(alsa_mutex);
            if (pcm_handle) {
                snd_pcm_close(pcm_handle);
                pcm_handle = nullptr;
            }
        }
    };

    AlsaOutput::AlsaOutput(QObject* parent)
    : AudioOutput(parent)
    , d(std::make_unique<Private>()) {
    }

    AlsaOutput::~AlsaOutput() {
        shutdown();
    }

    bool AlsaOutput::initialize(const OutputConfig& config) {
        if (d->initialized.load(std::memory_order_acquire)) return true;

        d->config = config;
        QString device = config.device_name.isEmpty() ? "default" : config.device_name;

        std::lock_guard lock(d->alsa_mutex);

        // Open PCM device
        int err = snd_pcm_open(&d->pcm_handle, device.toUtf8().constData(),
                               SND_PCM_STREAM_PLAYBACK, 0);

        if (err < 0) {
            emit error(QString("ALSA: Failed to open device: %1").arg(snd_strerror(err)));
            return false;
        }

        // Set hardware parameters
        snd_pcm_hw_params_t* hw_params;
        snd_pcm_hw_params_alloca(&hw_params);

        err = snd_pcm_hw_params_any(d->pcm_handle, hw_params);
        if (err < 0) {
            emit error(QString("ALSA: No config available: %1").arg(snd_strerror(err)));
            return false;
        }

        err = snd_pcm_hw_params_set_access(d->pcm_handle, hw_params,
                                           SND_PCM_ACCESS_RW_INTERLEAVED);
        if (err < 0) {
            emit error(QString("ALSA: Failed to set access: %1").arg(snd_strerror(err)));
            return false;
        }

        err = snd_pcm_hw_params_set_format(d->pcm_handle, hw_params,
                                           SND_PCM_FORMAT_FLOAT_LE);
        if (err < 0) {
            emit error(QString("ALSA: Failed to set format: %1").arg(snd_strerror(err)));
            return false;
        }

        err = snd_pcm_hw_params_set_rate_near(d->pcm_handle, hw_params,
                                              &d->config.sample_rate, nullptr);
        if (err < 0) {
            emit error(QString("ALSA: Failed to set rate: %1").arg(snd_strerror(err)));
            return false;
        }

        err = snd_pcm_hw_params_set_channels(d->pcm_handle, hw_params,
                                             d->config.channels);
        if (err < 0) {
            emit error(QString("ALSA: Failed to set channels: %1").arg(snd_strerror(err)));
            return false;
        }

        // Set buffer size
        snd_pcm_uframes_t period_size = d->config.buffer_size;
        err = snd_pcm_hw_params_set_period_size_near(d->pcm_handle, hw_params,
                                                     &period_size, nullptr);
        if (err < 0) {
            emit error(QString("ALSA: Failed to set period size: %1").arg(snd_strerror(err)));
            return false;
        }

        err = snd_pcm_hw_params(d->pcm_handle, hw_params);
        if (err < 0) {
            emit error(QString("ALSA: Failed to set params: %1").arg(snd_strerror(err)));
            return false;
        }

        // Prepare buffer
        d->buffer.resize(d->config.buffer_size * d->config.channels);

        d->initialized.store(true, std::memory_order_release);
        return true;
    }

    void AlsaOutput::shutdown() {
        stop();
        d->shutdown();
        d->initialized.store(false, std::memory_order_release);
    }

    void AlsaOutput::start() {
        if (!d->initialized.load(std::memory_order_acquire) ||
            d->active.load(std::memory_order_acquire)) return;

        d->running.store(true, std::memory_order_release);
        d->active.store(true, std::memory_order_release);
        d->audio_thread = std::thread(&AlsaOutput::alsa_thread_func, this);

        emit state_changed(true);
    }

    void AlsaOutput::stop() {
        d->running.store(false, std::memory_order_release);
        d->active.store(false, std::memory_order_release);

        {
            std::lock_guard lock(d->alsa_mutex);
            if (d->pcm_handle) {
                snd_pcm_drop(d->pcm_handle);
            }
            d->thread_cv.notify_all();
        }

        if (d->audio_thread.joinable()) {
            d->audio_thread.join();
        }

        emit state_changed(false);
    }

    void AlsaOutput::set_volume(double volume) {
        d->volume.store(std::clamp(volume, 0.0, 1.0), std::memory_order_release);
    }

    void AlsaOutput::alsa_thread_func() {
        snd_pcm_sframes_t frames_to_write = d->config.buffer_size;

        // Set thread name for debugging
        pthread_setname_np(pthread_self(), "alsa_audio");

        while (d->running.load(std::memory_order_acquire)) {
            std::unique_lock lock(d->alsa_mutex);

            if (!d->pcm_handle) break;

            // Wait for space in buffer with timeout
            int err = snd_pcm_wait(d->pcm_handle, 100);
            if (err < 0) {
                if (err == -EPIPE) {
                    // Underrun
                    d->underrun_count.fetch_add(1, std::memory_order_relaxed);
                    emit underrun_detected();
                    snd_pcm_prepare(d->pcm_handle);
                }
                continue;
            }

            // Get audio from callback (thread-safe copy)
            AudioCallback callback_copy;
            {
                std::shared_lock cb_lock(m_callback_mutex);
                callback_copy = m_callback;
            }

            if (callback_copy) {
                callback_copy(d->buffer.data(), frames_to_write);
            } else {
                std::fill(d->buffer.begin(), d->buffer.end(), 0.0f);
            }

            // Apply volume
            double vol = d->volume.load(std::memory_order_acquire);
            if (vol != 1.0) {
                for (auto& sample : d->buffer) {
                    sample *= vol;
                }
            }

            // Write to device
            snd_pcm_sframes_t frames = snd_pcm_writei(d->pcm_handle,
                                                      d->buffer.data(),
                                                      frames_to_write);

            if (frames < 0) {
                if (frames == -EPIPE) {
                    // Underrun
                    d->underrun_count.fetch_add(1, std::memory_order_relaxed);
                    emit underrun_detected();
                    snd_pcm_prepare(d->pcm_handle);
                }
            } else {
                d->frames_played.fetch_add(frames, std::memory_order_relaxed);
                d->bytes_played.fetch_add(frames * d->config.channels * sizeof(float),
                                          std::memory_order_relaxed);
            }

            // Allow early termination
            d->thread_cv.wait_for(lock, std::chrono::milliseconds(1));
        }
    }

    #endif // AEGIS_HAVE_ALSA

    // ============================================================================
    // DummyOutput Implementation
    // ============================================================================

    class DummyOutput::Private {
    public:
        OutputConfig config;
        std::atomic<bool> initialized{false};
        std::atomic<bool> active{false};
        std::atomic<double> volume{1.0};
        std::atomic<uint64_t> bytes_played{0};
        std::atomic<uint64_t> frames_played{0};

        QTimer timer;
        std::vector<float> buffer;
        AudioCallback callback;
        mutable std::shared_mutex callback_mutex;
    };

    DummyOutput::DummyOutput(QObject* parent)
    : AudioOutput(parent)
    , d(std::make_unique<Private>()) {

        d->timer.setInterval(10);
        connect(&d->timer, &QTimer::timeout, this, &DummyOutput::timer_callback);
    }

    DummyOutput::~DummyOutput() = default;

    bool DummyOutput::initialize(const OutputConfig& config) {
        d->config = config;
        d->buffer.resize(config.buffer_size * config.channels);
        d->initialized.store(true, std::memory_order_release);
        return true;
    }

    void DummyOutput::shutdown() {
        stop();
        d->initialized.store(false, std::memory_order_release);
    }

    void DummyOutput::start() {
        if (!d->initialized.load(std::memory_order_acquire) ||
            d->active.load(std::memory_order_acquire)) return;

        d->active.store(true, std::memory_order_release);
        d->timer.start();
        emit state_changed(true);
    }

    void DummyOutput::stop() {
        d->active.store(false, std::memory_order_release);
        d->timer.stop();
        emit state_changed(false);
    }

    void DummyOutput::set_volume(double volume) {
        d->volume.store(std::clamp(volume, 0.0, 1.0), std::memory_order_release);
    }

    void DummyOutput::timer_callback() {
        if (!d->active.load(std::memory_order_acquire)) return;

        int frames = d->config.buffer_size;

        // Thread-safe callback invocation
        AudioCallback callback_copy;
        {
            std::shared_lock lock(d->callback_mutex);
            callback_copy = d->callback;
        }

        if (callback_copy) {
            callback_copy(d->buffer.data(), frames);
        }

        // Apply volume
        double vol = d->volume.load(std::memory_order_acquire);
        if (vol != 1.0) {
            for (auto& sample : d->buffer) {
                sample *= vol;
            }
        }

        d->frames_played.fetch_add(frames, std::memory_order_relaxed);
        d->bytes_played.fetch_add(frames * d->config.channels * sizeof(float),
                                  std::memory_order_relaxed);
    }

    // ============================================================================
    // Output Factory
    // ============================================================================

    std::unique_ptr<AudioOutput> AudioOutputFactory::create(OutputBackend backend) {
        switch (backend) {
            case OutputBackend::QtMultimedia:
                return std::make_unique<QtAudioOutput>();

            case OutputBackend::PipeWire:
                #if AEGIS_HAVE_PIPEWIRE
                return std::make_unique<PipeWireOutput>();
                #else
                return nullptr;
                #endif

            case OutputBackend::ALSA:
                #if AEGIS_HAVE_ALSA
                return std::make_unique<AlsaOutput>();
                #else
                return nullptr;
                #endif

            case OutputBackend::Dummy:
                return std::make_unique<DummyOutput>();

            default:
                return nullptr;
        }
    }

    std::unique_ptr<AudioOutput> AudioOutputFactory::createBestAvailable() {
        #if AEGIS_HAVE_PIPEWIRE
        auto pw = std::make_unique<PipeWireOutput>();
        OutputConfig test_config;
        test_config.sample_rate = 48000;
        test_config.channels = 2;
        test_config.buffer_size = 1024;
        if (pw->initialize(test_config)) {
            return pw;
        }
        #endif

        #if AEGIS_HAVE_ALSA
        auto alsa = std::make_unique<AlsaOutput>();
        if (alsa->initialize(test_config)) {
            return alsa;
        }
        #endif

        auto qt = std::make_unique<QtAudioOutput>();
        if (qt->initialize(test_config)) {
            return qt;
        }

        return std::make_unique<DummyOutput>();
    }

    QStringList AudioOutputFactory::availableBackends() {
        QStringList list;
        list << "QtMultimedia";

        #if AEGIS_HAVE_PIPEWIRE
        list << "PipeWire";
        #endif

        #if AEGIS_HAVE_ALSA
        list << "ALSA";
        #endif

        list << "Dummy";
        return list;
    }

} // namespace Aegis
