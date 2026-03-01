// mediaplayer.cpp - Unified media player with real-time audio processing

#include "audio.h"
#include "audio_output.h"
#include "audio_effects.h"
#include "mediaplayer.h"
#include "library.h"
#include "playlist.h"
#include "settings.h"
#include "mpv_backend.h"
#include <QFileInfo>
#include <QDebug>
#include <algorithm>
#include <atomic>
#include <memory>

namespace Aegis {

    // ============================================================================
    // Lock-Free Audio Ring Buffer
    // ============================================================================

    template<typename T, size_t Size>
    class AudioRingBuffer {
        static_assert((Size & (Size - 1)) == 0, "Size must be power of two");

    private:
        static constexpr size_t MASK = Size - 1;
        std::array<T, Size> m_buffer;
        std::atomic<size_t> m_writeIndex{0};
        std::atomic<size_t> m_readIndex{0};

        // Cache line padding to prevent false sharing
        char padding[64];

    public:
        AudioRingBuffer() {
            m_buffer.fill(T(0));
        }

        size_t write(const T* data, size_t count) {
            size_t write = m_writeIndex.load(std::memory_order_relaxed);
            size_t read = m_readIndex.load(std::memory_order_acquire);

            size_t available = Size - (write - read);
            size_t toWrite = std::min(count, available);

            for (size_t i = 0; i < toWrite; ++i) {
                m_buffer[(write + i) & MASK] = data[i];
            }

            m_writeIndex.store(write + toWrite, std::memory_order_release);
            return toWrite;
        }

        size_t read(T* data, size_t maxCount) {
            size_t read = m_readIndex.load(std::memory_order_relaxed);
            size_t write = m_writeIndex.load(std::memory_order_acquire);

            size_t available = write - read;
            size_t toRead = std::min(maxCount, available);

            for (size_t i = 0; i < toRead; ++i) {
                data[i] = m_buffer[(read + i) & MASK];
            }

            m_readIndex.store(read + toRead, std::memory_order_release);
            return toRead;
        }

        size_t available() const {
            return m_writeIndex.load(std::memory_order_acquire) -
            m_readIndex.load(std::memory_order_acquire);
        }

        void reset() {
            m_writeIndex.store(0, std::memory_order_release);
            m_readIndex.store(0, std::memory_order_release);
        }
    };

    // ============================================================================
    // MediaPlayer Private Implementation
    // ============================================================================

    class MediaPlayer::Private {
    public:
        // Dependencies
        std::shared_ptr<Library> library;
        std::shared_ptr<Playlist> playlist;
        std::unique_ptr<AudioEngine> audioEngine;
        std::unique_ptr<MpvBackend> mpvBackend;
        std::unique_ptr<AudioOutput> audioOutput;

        // State with proper memory ordering
        std::atomic<PlaybackState> state{PlaybackState::Stopped};
        std::atomic<BackendType> activeBackend{BackendType::None};
        std::atomic<double> volume{1.0};
        std::atomic<bool> muted{false};
        std::atomic<bool> seekable{true};
        std::atomic<int> currentPlaylistIndex{-1};

        // Current media
        QUrl source;
        TrackMetadata metadata;

        // Position tracking with high precision
        std::atomic<double> position{0.0};
        std::atomic<double> duration{0.0};
        QTimer positionTimer;

        // Lock-free audio buffer
        static constexpr size_t AudioBufferSize = 65536;  // 64k samples
        AudioRingBuffer<float, AudioBufferSize> audioBuffer;
        std::atomic<int> audioSampleRate{48000};

        // Settings
        Settings* settings;

        // Performance monitoring
        std::atomic<uint64_t> totalBytesPlayed{0};
        std::atomic<uint64_t> underruns{0};

        explicit Private(Settings* s) : settings(s) {}

        ~Private() = default;
    };

    // ============================================================================
    // MediaPlayer Implementation
    // ============================================================================

    MediaPlayer::MediaPlayer(std::unique_ptr<AudioOutput> output,
                             std::shared_ptr<Library> library,
                             QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(Settings::instance())) {

        d->library = library;
        d->audioEngine = std::make_unique<AudioEngine>(this);
        d->mpvBackend = std::make_unique<MpvBackend>(this);

        // Initialize audio output
        if (output) {
            d->audioOutput = std::move(output);
        } else {
            initializeAudioOutput();
        }

        // Setup position timer (60 Hz for smooth UI)
        d->positionTimer.setInterval(16);  // ~60 fps
        connect(&d->positionTimer, &QTimer::timeout, this, [this]() {
            if (d->state.load() == PlaybackState::Playing) {
                updatePosition();
            }
        });

        // Connect MPV backend signals
        connect(d->mpvBackend.get(), &MpvBackend::positionChanged,
                this, [this](double pos) {
                    d->position.store(pos, std::memory_order_release);
                    emit positionChanged(static_cast<qint64>(pos * 1000));
                });

        connect(d->mpvBackend.get(), &MpvBackend::durationChanged,
                this, [this](double dur) {
                    d->duration.store(dur, std::memory_order_release);
                    emit durationChanged(static_cast<qint64>(dur * 1000));
                });

        connect(d->mpvBackend.get(), &MpvBackend::stateChanged,
                this, [this](int state) {
                    onMpvStateChanged(state);
                });

        connect(d->mpvBackend.get(), &MpvBackend::metadataChanged,
                this, [this](const QVariantMap& md) {
                    syncMetadataFromMpv(md);
                });

        connect(d->mpvBackend.get(), &MpvBackend::finished,
                this, &MediaPlayer::onMpvFinished);

        connect(d->mpvBackend.get(), &MpvBackend::error,
                this, &MediaPlayer::onMpvError);

        // Setup MPV audio callback
        setupMpvAudioCallback();

        // Connect tracker signals
        auto* tracker = d->audioEngine->tracker();
        connect(tracker, &ModTrackerPlayback::positionChanged,
                this, [this]() {
                    if (d->activeBackend.load() == BackendType::Tracker) {
                        d->position.store(d->audioEngine->tracker()->position(),
                                          std::memory_order_release);
                        emit positionChanged(static_cast<qint64>(d->position.load() * 1000));
                    }
                });

        connect(tracker, &ModTrackerPlayback::finished,
                this, &MediaPlayer::onTrackerFinished);

        connect(tracker, &ModTrackerPlayback::error,
                this, &MediaPlayer::onTrackerError);

        // Connect audio output signals
        if (d->audioOutput) {
            connect(d->audioOutput.get(), &AudioOutput::stateChanged,
                    this, [this](bool playing) {
                        qDebug() << "Audio output state changed:" << playing;
                    });

            connect(d->audioOutput.get(), &AudioOutput::underrunDetected,
                    this, [this]() {
                        d->underruns.fetch_add(1, std::memory_order_relaxed);
                        qWarning() << "Audio underrun #" << d->underruns.load();
                    });

            connect(d->audioOutput.get(), &AudioOutput::error,
                    this, &MediaPlayer::onMpvError);
        }

        // Restore volume
        d->volume.store(d->settings->value("Audio/Volume", 1.0).toDouble(),
                        std::memory_order_release);
        if (d->audioOutput) {
            d->audioOutput->setVolume(d->volume.load());
        }
        d->mpvBackend->setVolume(100.0);  // MPV at 100%, we control via AudioOutput
    }

    MediaPlayer::~MediaPlayer() = default;

    void MediaPlayer::initializeAudioOutput() {
        OutputConfig config;
        config.sampleRate = 48000;
        config.channels = 2;
        config.bufferSize = 1024;
        config.latencyTargetMs = 10;
        config.realtimePriority = true;
        config.preferredBackend = OutputBackend::Auto;

        d->audioOutput = AudioOutputFactory::create(config.preferredBackend);
        if (!d->audioOutput || !d->audioOutput->initialize(config)) {
            qWarning() << "Primary audio backend failed, trying fallback";
            d->audioOutput = AudioOutputFactory::create(OutputBackend::QtMultimedia);
            if (d->audioOutput) {
                d->audioOutput->initialize(config);
            }
        }

        if (d->audioOutput) {
            // Setup real-time audio callback
            d->audioOutput->setAudioCallback([this](float* buffer, int frames) {
                processAudioOutput(buffer, frames);
            });
        } else {
            qCritical() << "Failed to initialize any audio output backend";
        }
    }

    void MediaPlayer::setupMpvAudioCallback() {
        d->mpvBackend->setAudioCallback([this](const QByteArray& data, int sampleRate) {
            d->audioSampleRate.store(sampleRate, std::memory_order_release);

            const float* samples = reinterpret_cast<const float*>(data.constData());
            int sampleCount = data.size() / sizeof(float);

            // Write to lock-free buffer
            size_t written = d->audioBuffer.write(samples, sampleCount);
            d->totalBytesPlayed.fetch_add(written * sizeof(float),
                                          std::memory_order_relaxed);

            if (written < static_cast<size_t>(sampleCount)) {
                qWarning() << "Audio buffer overflow, dropped"
                << (sampleCount - written) << "samples";
            }
        });
    }

    void MediaPlayer::processAudioOutput(float* buffer, int frames) {
        int channels = d->audioOutput ? d->audioOutput->channels() : 2;
        int samplesNeeded = frames * channels;

        // Clear output buffer
        std::fill(buffer, buffer + samplesNeeded, 0.0f);

        BackendType backend = d->activeBackend.load(std::memory_order_acquire);

        switch (backend) {
            case BackendType::Mpv: {
                // Read from lock-free buffer
                size_t samplesRead = d->audioBuffer.read(buffer, samplesNeeded);

                // Fill remaining with silence
                if (samplesRead < static_cast<size_t>(samplesNeeded)) {
                    std::fill(buffer + samplesRead, buffer + samplesNeeded, 0.0f);
                }

                // Apply audio engine processing
                if (d->audioEngine->processingEnabled() && channels == 2) {
                    int sampleRate = d->audioSampleRate.load(std::memory_order_acquire);
                    d->audioEngine->processBuffer(buffer, frames, sampleRate, channels);
                }
                break;
            }

            case BackendType::Tracker: {
                auto* tracker = d->audioEngine->tracker();
                if (tracker && tracker->isPlaying()) {
                    // Tracker renders directly to buffer
                    // AudioEngine processes it
                }
                break;
            }

            default:
                // Silence
                break;
        }

        // Apply volume
        if (!d->audioOutput) {
            double vol = d->volume.load(std::memory_order_acquire);
            if (vol != 1.0) {
                for (int i = 0; i < samplesNeeded; ++i) {
                    buffer[i] *= vol;
                }
            }
        }

        // Apply mute
        if (d->muted.load(std::memory_order_acquire)) {
            std::fill(buffer, buffer + samplesNeeded, 0.0f);
        }
    }

    void MediaPlayer::load(const QUrl& url) {
        if (!url.isValid()) {
            emit error(tr("Invalid URL"));
            return;
        }

        if (!d->audioOutput || !d->audioOutput->isInitialized()) {
            emit error(tr("Audio output not initialized"));
            return;
        }

        cleanupCurrentPlayback();
        d->source = url;
        emit sourceChanged(url);

        QString path = url.toLocalFile();

        if (isTrackerFile(path)) {
            loadTracker(path);
        } else {
            loadMpv(url);
        }
    }

    void MediaPlayer::loadTracker(const QString& path) {
        if (!d->audioEngine->loadTrackerModule(path)) {
            emit error(tr("Failed to load tracker module: %1").arg(path));
            setState(PlaybackState::Error);
            return;
        }

        setBackend(BackendType::Tracker);
        syncMetadataFromTracker();

        d->seekable.store(true, std::memory_order_release);
        emit seekableChanged(true);
        setState(PlaybackState::Stopped);

        if (d->settings->value("Playback/PlayOnLoad", true).toBool()) {
            play();
        }
    }

    void MediaPlayer::loadMpv(const QUrl& url) {
        d->mpvBackend->load(url.toLocalFile());
        setBackend(BackendType::Mpv);
        resetMetadata();
    }

    void MediaPlayer::setBackend(BackendType type) {
        BackendType old = d->activeBackend.exchange(type, std::memory_order_acq_rel);
        if (old != type) {
            emit backendChanged(type);
        }
    }

    bool MediaPlayer::isTrackerFile(const QString& path) const {
        return d->audioEngine->isTrackerFile(path);
    }

    QStringList MediaPlayer::supportedTrackerFormats() const {
        return d->audioEngine->tracker()->supportedExtensions();
    }

    void MediaPlayer::play() {
        if (d->state.load() == PlaybackState::Playing) return;

        if (!d->audioOutput || !d->audioOutput->isInitialized()) {
            emit error(tr("Audio output not available"));
            return;
        }

        BackendType backend = d->activeBackend.load(std::memory_order_acquire);

        switch (backend) {
            case BackendType::Mpv:
                d->audioOutput->start();
                d->mpvBackend->play();
                d->positionTimer.start();
                break;

            case BackendType::Tracker:
                d->audioOutput->start();
                d->audioEngine->playTracker();
                d->positionTimer.start();
                setState(PlaybackState::Playing);
                break;

            default:
                emit error(tr("No media loaded"));
                return;
        }
    }

    void MediaPlayer::pause() {
        if (d->state.load() != PlaybackState::Playing) return;

        BackendType backend = d->activeBackend.load(std::memory_order_acquire);

        switch (backend) {
            case BackendType::Mpv:
                d->mpvBackend->pause();
                d->audioOutput->stop();
                d->positionTimer.stop();
                break;

            case BackendType::Tracker:
                d->audioEngine->pauseTracker();
                d->audioOutput->stop();
                d->positionTimer.stop();
                setState(PlaybackState::Paused);
                break;

            default:
                break;
        }
    }

    void MediaPlayer::togglePause() {
        if (d->state.load() == PlaybackState::Playing) {
            pause();
        } else {
            play();
        }
    }

    void MediaPlayer::stop() {
        if (d->state.load() == PlaybackState::Stopped) return;

        BackendType backend = d->activeBackend.load(std::memory_order_acquire);

        switch (backend) {
            case BackendType::Mpv:
                d->mpvBackend->stop();
                d->audioOutput->stop();
                d->positionTimer.stop();
                break;

            case BackendType::Tracker:
                d->audioEngine->stopTracker();
                d->audioOutput->stop();
                d->positionTimer.stop();
                d->position.store(0.0, std::memory_order_release);
                emit positionChanged(0);
                break;

            default:
                break;
        }

        setState(PlaybackState::Stopped);
    }

    void MediaPlayer::seek(qint64 positionMs) {
        seekSeconds(positionMs / 1000.0);
    }

    void MediaPlayer::seekSeconds(double seconds) {
        if (!d->seekable.load(std::memory_order_acquire)) return;

        BackendType backend = d->activeBackend.load(std::memory_order_acquire);

        switch (backend) {
            case BackendType::Mpv:
                d->mpvBackend->seek(seconds);
                break;

            case BackendType::Tracker:
                d->audioEngine->seekTracker(seconds);
                d->position.store(seconds, std::memory_order_release);
                emit positionChanged(static_cast<qint64>(seconds * 1000));
                break;

            default:
                break;
        }
    }

    void MediaPlayer::setVolume(double volume) {
        volume = std::clamp(volume, 0.0, 1.0);
        double old = d->volume.exchange(volume, std::memory_order_acq_rel);

        if (old != volume) {
            if (d->audioOutput) {
                d->audioOutput->setVolume(volume);
            }
            d->mpvBackend->setVolume(100.0);  // MPV at 100%
            d->audioEngine->tracker()->setVolume(volume);
            d->settings->setValue("Audio/Volume", volume);
            emit volumeChanged(volume);
        }
    }

    double MediaPlayer::volume() const {
        return d->volume.load(std::memory_order_acquire);
    }

    void MediaPlayer::setMuted(bool muted) {
        bool old = d->muted.exchange(muted, std::memory_order_acq_rel);

        if (old != muted) {
            if (d->audioOutput) {
                d->audioOutput->setVolume(muted ? 0.0 : d->volume.load());
            }
            emit mutedChanged(muted);
        }
    }

    bool MediaPlayer::muted() const {
        return d->muted.load(std::memory_order_acquire);
    }

    qint64 MediaPlayer::position() const {
        return static_cast<qint64>(d->position.load(std::memory_order_acquire) * 1000);
    }

    qint64 MediaPlayer::duration() const {
        return static_cast<qint64>(d->duration.load(std::memory_order_acquire) * 1000);
    }

    PlaybackState MediaPlayer::state() const {
        return d->state.load(std::memory_order_acquire);
    }

    QUrl MediaPlayer::source() const {
        return d->source;
    }

    TrackMetadata MediaPlayer::metadata() const {
        return d->metadata;
    }

    bool MediaPlayer::seekable() const {
        return d->seekable.load(std::memory_order_acquire);
    }

    BackendType MediaPlayer::activeBackend() const {
        return d->activeBackend.load(std::memory_order_acquire);
    }

    bool MediaPlayer::isTrackerMode() const {
        return d->activeBackend.load(std::memory_order_acquire) == BackendType::Tracker;
    }

    int MediaPlayer::currentTrackerPattern() const {
        if (d->activeBackend.load() != BackendType::Tracker) return 0;
        return d->audioEngine->tracker()->getCurrentPattern();
    }

    int MediaPlayer::currentTrackerRow() const {
        if (d->activeBackend.load() != BackendType::Tracker) return 0;
        return d->audioEngine->tracker()->getCurrentRow();
    }

    void MediaPlayer::setState(PlaybackState state) {
        PlaybackState old = d->state.exchange(state, std::memory_order_acq_rel);
        if (old != state) {
            emit stateChanged(state);
        }
    }

    void MediaPlayer::syncMetadataFromTracker() {
        auto* tracker = d->audioEngine->tracker();
        d->metadata = TrackMetadata();
        d->metadata.title = tracker->title();
        d->metadata.artist = tracker->artist();
        d->metadata.duration = static_cast<int>(tracker->duration() * 1000);
        d->metadata.channels = tracker->getNumChannels();
        d->metadata.patterns = tracker->getNumPatterns();
        d->metadata.isTracker = true;
        d->metadata.hasVideo = false;

        emit metadataChanged(d->metadata);
    }

    void MediaPlayer::syncMetadataFromMpv(const QVariantMap& metadata) {
        d->metadata = TrackMetadata();
        d->metadata.title = metadata.value("title").toString();
        d->metadata.artist = metadata.value("artist").toString();
        d->metadata.album = metadata.value("album").toString();
        d->metadata.year = metadata.value("year").toInt();
        d->metadata.hasVideo = metadata.value("vid", false).toBool();
        d->metadata.isTracker = false;

        emit metadataChanged(d->metadata);
    }

    void MediaPlayer::resetMetadata() {
        d->metadata = TrackMetadata();
    }

    void MediaPlayer::cleanupCurrentPlayback() {
        stop();
        d->source.clear();
        if (d->activeBackend.load() == BackendType::Tracker) {
            d->audioEngine->unloadTracker();
        }
    }

    void MediaPlayer::updatePosition() {
        BackendType backend = d->activeBackend.load(std::memory_order_acquire);

        if (backend == BackendType::Tracker) {
            double pos = d->audioEngine->tracker()->position();
            d->position.store(pos, std::memory_order_release);
            emit positionChanged(static_cast<qint64>(pos * 1000));
        }
    }

    void MediaPlayer::onMpvStateChanged(int state) {
        switch (state) {
            case 0:  // Idle
                setState(PlaybackState::Stopped);
                break;
            case 1:  // Loading
                setState(PlaybackState::Buffering);
                break;
            case 2:  // Playing
                setState(PlaybackState::Playing);
                break;
            case 3:  // Paused
                setState(PlaybackState::Paused);
                break;
        }
    }

    void MediaPlayer::onMpvFinished() {
        d->positionTimer.stop();
        emit finished();
        loadNextPlaylistItem();
    }

    void MediaPlayer::onMpvError(const QString& message) {
        emit error(tr("MPV: %1").arg(message));
        setState(PlaybackState::Error);
    }

    void MediaPlayer::onTrackerFinished() {
        d->positionTimer.stop();
        setState(PlaybackState::Stopped);
        emit finished();
        loadNextPlaylistItem();
    }

    void MediaPlayer::onTrackerError(const QString& message) {
        emit error(tr("Tracker: %1").arg(message));
        setState(PlaybackState::Error);
    }

    void MediaPlayer::setPlaylist(std::shared_ptr<Playlist> playlist) {
        d->playlist = playlist;
    }

    void MediaPlayer::playAt(int index) {
        if (!d->playlist) return;

        auto item = d->playlist->at(index);
        if (item.isValid()) {
            d->currentPlaylistIndex.store(index, std::memory_order_release);
            load(item.url());
        }
    }

    void MediaPlayer::next() {
        loadNextPlaylistItem();
    }

    void MediaPlayer::previous() {
        if (!d->playlist) return;

        int current = d->currentPlaylistIndex.load(std::memory_order_acquire);
        if (current > 0) {
            playAt(current - 1);
        }
    }

    void MediaPlayer::loadNextPlaylistItem() {
        if (!d->playlist) return;

        int current = d->currentPlaylistIndex.load(std::memory_order_acquire);
        int next = current + 1;

        if (next < d->playlist->count()) {
            playAt(next);
        } else if (d->settings->value("Playback/RepeatMode", false).toBool()) {
            playAt(0);
        }
    }

    bool MediaPlayer::switchAudioBackend(OutputBackend backend) {
        bool wasPlaying = (d->state.load() == PlaybackState::Playing);
        double currentPos = d->position.load();

        stop();

        auto newOutput = AudioOutputFactory::create(backend);
        if (!newOutput) {
            emit error(tr("Failed to create audio output backend"));
            return false;
        }

        OutputConfig config;
        config.sampleRate = 48000;
        config.channels = 2;
        config.preferredBackend = backend;

        if (!newOutput->initialize(config)) {
            emit error(tr("Failed to initialize audio output backend"));
            return false;
        }

        // Transfer callback
        newOutput->setAudioCallback([this](float* buffer, int frames) {
            processAudioOutput(buffer, frames);
        });

        d->audioOutput = std::move(newOutput);

        if (wasPlaying && !d->source.isEmpty()) {
            load(d->source);
            seekSeconds(currentPos);
            play();
        }

        emit audioBackendChanged();
        return true;
    }

    OutputBackend MediaPlayer::currentAudioBackend() const {
        return d->audioOutput ? d->audioOutput->backendType() : OutputBackend::Auto;
    }

    QString MediaPlayer::audioBackendName() const {
        return d->audioOutput ?
        AudioOutputFactory::backendName(d->audioOutput->backendType()) :
        tr("None");
    }

    AudioEngine* MediaPlayer::audioEngine() {
        return d->audioEngine.get();
    }

    AudioOutput* MediaPlayer::audioOutput() {
        return d->audioOutput.get();
    }

} // namespace Aegis
