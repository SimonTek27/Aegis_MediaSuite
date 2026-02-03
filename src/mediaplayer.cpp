// mediaplayer.cpp - Media player with audio_output integration
#include "mediaplayer.h"
#include "library.h"
#include "playlist.h"
#include "settings.h"
#include "mpv_backend.h"
#include <QFileInfo>
#include <QDebug>
#include <algorithm>

namespace Aegis {

    MediaPlayer::MediaPlayer(std::unique_ptr<AudioOutput> output,
                             std::shared_ptr<Library> library,
                             QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_output(std::move(output))
    , m_audioEngine(std::make_unique<AudioEngine>(this))
    , m_mpvBackend(std::make_unique<MpvBackend>(this))
    , m_settings(Settings::instance())
    {
        // Initialize audio output if not provided
        if (!m_output) {
            initializeAudioOutput();
        }

        // Setup position update timer (10Hz for smooth UI)
        m_positionTimer.setInterval(100);
        connect(&m_positionTimer, &QTimer::timeout, this, &MediaPlayer::updatePosition);

        // Connect MPV backend signals
        connect(m_mpvBackend.get(), &MpvBackend::positionChanged,
                this, &MediaPlayer::onMpvPositionChanged);
        connect(m_mpvBackend.get(), &MpvBackend::durationChanged,
                this, &MediaPlayer::onMpvDurationChanged);
        connect(m_mpvBackend.get(), &MpvBackend::stateChanged,
                this, &MediaPlayer::onMpvStateChanged);
        connect(m_mpvBackend.get(), &MpvBackend::metadataChanged,
                this, &MediaPlayer::onMpvMetadataChanged);
        connect(m_mpvBackend.get(), &MpvBackend::finished,
                this, &MediaPlayer::onMpvFinished);
        connect(m_mpvBackend.get(), &MpvBackend::error,
                this, &MediaPlayer::onMpvError);

        // Setup MPV audio callback - CRITICAL: Connect MPV to AudioEngine/AudioOutput
        setupMpvAudioCallback();

        // Connect Tracker signals via AudioEngine
        ModTrackerPlayback *tracker = m_audioEngine->tracker();
        connect(tracker, &ModTrackerPlayback::positionChanged,
                this, &MediaPlayer::onTrackerPositionChanged);
        connect(tracker, &ModTrackerPlayback::finished,
                this, &MediaPlayer::onTrackerFinished);
        connect(tracker, &ModTrackerPlayback::error,
                this, &MediaPlayer::onTrackerError);

        // Connect AudioOutput signals
        if (m_output) {
            connect(m_output.get(), &AudioOutput::stateChanged,
                    this, &MediaPlayer::onAudioOutputStateChanged);
            connect(m_output.get(), &AudioOutput::underrunDetected,
                    this, &MediaPlayer::onAudioOutputUnderrun);
            connect(m_output.get(), &AudioOutput::error,
                    this, &MediaPlayer::onMpvError);
        }

        // Volume restoration
        m_volume = m_settings->value("Audio/Volume", 1.0).toDouble();
        if (m_output) {
            m_output->setVolume(m_volume);
        }
        m_mpvBackend->setVolume(100.0);  // MPV volume at 100%, we control via AudioOutput

        // Setup tracker audio output callback
        // Tracker audio flows through AudioEngine then to AudioOutput
    }

    MediaPlayer::~MediaPlayer() = default;

    void MediaPlayer::initializeAudioOutput() {
        // Auto-detect best backend (PipeWire preferred)
        OutputConfig config;
        config.sampleRate = 48000;
        config.channels = 2;
        config.bufferSize = 1024;
        config.latencyTargetMs = 10;
        config.realtimePriority = true;
        config.preferredBackend = OutputBackend::Auto;

        m_output = AudioOutputFactory::create(config.preferredBackend);
        if (m_output) {
            if (!m_output->initialize(config)) {
                qWarning() << "Failed to initialize audio output, trying fallback";
                m_output.reset();
                m_output = AudioOutputFactory::create(OutputBackend::QtMultimedia);
                if (m_output) {
                    m_output->initialize(config);
                }
            }
        }

        if (m_output) {
            // Setup pull-mode audio callback
            // AudioOutput calls us when it needs data
            m_output->setAudioCallback([this](float* buffer, int frames) {
                processAudioOutput(buffer, frames);
            });
        } else {
            qCritical() << "Failed to initialize any audio output backend";
        }
    }

    void MediaPlayer::setupMpvAudioCallback() {
        // Configure MPV to send raw PCM audio to our callback
        // This bypasses MPV's internal audio output and lets us control it

        // Option 1: Use audio-filter with export (if available)
        // Option 2: Use ao=null with audio-data callback (preferred)

        m_mpvBackend->setAudioCallback([this](const QByteArray& data, int sampleRate) {
            // This is called from MPV's thread - we need to be careful
            // For now, store data for processing in the audio callback

            // Convert QByteArray to float samples
            const float* samples = reinterpret_cast<const float*>(data.constData());
            int sampleCount = data.size() / sizeof(float);

            // Store in ring buffer or process immediately
            // For simplicity, we'll process in processAudioOutput()

            m_audioSampleRate = sampleRate;

            // Append to buffer (thread-safe queue would be better)
            std::lock_guard<std::mutex> lock(m_audioMutex);
            m_audioBuffer.insert(m_audioBuffer.end(), samples, samples + sampleCount);
        });
    }

    void MediaPlayer::processAudioOutput(float* buffer, int frames) {
        // This is called by AudioOutput (PipeWire/Qt) when it needs audio

        int channels = m_output ? m_output->channels() : 2;
        int samplesNeeded = frames * channels;

        // Clear output buffer
        std::fill(buffer, buffer + samplesNeeded, 0.0f);

        switch (m_activeBackend) {
            case BackendType::Mpv: {
                // Pull from MPV audio buffer
                // Apply AudioEngine effects if enabled
                std::lock_guard<std::mutex> lock(m_audioMutex);

                int samplesAvailable = std::min(static_cast<int>(m_audioBuffer.size()), samplesNeeded);
                if (samplesAvailable > 0) {
                    std::copy(m_audioBuffer.begin(), m_audioBuffer.begin() + samplesAvailable, buffer);
                    m_audioBuffer.erase(m_audioBuffer.begin(), m_audioBuffer.begin() + samplesAvailable);
                }

                // Apply AudioEngine processing (EQ, karaoke, etc.)
                if (m_audioEngine->processingEnabled() && channels == 2) {
                    m_audioEngine->processBuffer(buffer, frames, m_audioSampleRate, channels);
                }
                break;
            }

            case BackendType::Tracker: {
                // Get audio from tracker playback
                ModTrackerPlayback* tracker = m_audioEngine->tracker();
                if (tracker && tracker->isPlaying()) {
                    // Tracker generates audio into buffer
                    // AudioEngine processes it
                }
                break;
            }

            default:
                // Silence
                break;
        }

        // Apply global volume (if not handled by AudioOutput)
        if (!m_output && m_volume != 1.0) {
            for (int i = 0; i < samplesNeeded; i++) {
                buffer[i] *= m_volume;
            }
        }

        // Apply mute
        if (m_muted) {
            std::fill(buffer, buffer + samplesNeeded, 0.0f);
        }
    }

    // ================ Load & Backend Selection ================

    void MediaPlayer::load(const QUrl &url) {
        if (!url.isValid()) {
            emit error("Invalid URL");
            return;
        }

        if (!m_output || !m_output->isInitialized()) {
            emit error("Audio output not initialized");
            return;
        }

        cleanupCurrentPlayback();
        m_source = url;
        emit sourceChanged(url);

        QString path = url.toLocalFile();

        // Detect tracker files
        if (isTrackerFile(path)) {
            loadTracker(path);
        } else {
            loadMpv(url);
        }
    }

    void MediaPlayer::loadTracker(const QString &path) {
        if (!m_audioEngine->loadTrackerModule(path)) {
            emit error("Failed to load tracker module: " + path);
            setState(PlaybackState::Error);
            return;
        }

        setBackend(BackendType::Tracker);
        syncMetadataFromTracker();

        m_seekable = true;
        emit seekableChanged(true);
        setState(PlaybackState::Stopped);

        // Auto-play if setting enabled
        if (m_settings->value("Playback/PlayOnLoad", true).toBool()) {
            play();
        }
    }

    void MediaPlayer::loadMpv(const QString &url) {
        m_mpvBackend->load(url);
        setBackend(BackendType::Mpv);
        resetMetadata();

        // MPV audio will flow through our callback to AudioOutput
        // We don't start AudioOutput yet - wait for play()
    }

    void MediaPlayer::setBackend(BackendType type) {
        if (m_activeBackend == type) return;

        // Cleanup old backend state
        if (m_activeBackend == BackendType::Mpv) {
            m_mpvBackend->stop();
        } else if (m_activeBackend == BackendType::Tracker) {
            m_audioEngine->stopTracker();
        }

        m_activeBackend = type;
        emit backendChanged(type);
    }

    bool MediaPlayer::isTrackerFile(const QString &path) const {
        return m_audioEngine->isTrackerFile(path);
    }

    QStringList MediaPlayer::supportedTrackerFormats() const {
        return m_audioEngine->tracker()->supportedExtensions();
    }

    // ================ Playback Controls ================

    void MediaPlayer::play() {
        if (m_state == PlaybackState::Playing) return;

        if (!m_output || !m_output->isInitialized()) {
            emit error("Audio output not available");
            return;
        }

        switch (m_activeBackend) {
            case BackendType::Mpv:
                // Start audio output first, then MPV
                m_output->start();
                m_mpvBackend->play();
                break;

            case BackendType::Tracker:
                // Start audio output, then tracker
                m_output->start();
                m_audioEngine->playTracker();
                m_positionTimer.start();
                setState(PlaybackState::Playing);
                break;

            default:
                emit error("No media loaded");
                return;
        }
    }

    void MediaPlayer::pause() {
        if (m_state != PlaybackState::Playing) return;

        switch (m_activeBackend) {
            case BackendType::Mpv:
                m_mpvBackend->pause();
                m_output->stop();  // Stop audio output to free device
                break;

            case BackendType::Tracker:
                m_audioEngine->pauseTracker();
                m_output->stop();
                m_positionTimer.stop();
                setState(PlaybackState::Paused);
                break;

            default:
                break;
        }
    }

    void MediaPlayer::togglePause() {
        if (m_state == PlaybackState::Playing) {
            pause();
        } else {
            play();
        }
    }

    void MediaPlayer::stop() {
        if (m_state == PlaybackState::Stopped) return;

        switch (m_activeBackend) {
            case BackendType::Mpv:
                m_mpvBackend->stop();
                m_output->stop();
                break;

            case BackendType::Tracker:
                m_audioEngine->stopTracker();
                m_output->stop();
                m_positionTimer.stop();
                m_trackedPosition = 0;
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
        if (!m_seekable) return;

        switch (m_activeBackend) {
            case BackendType::Mpv:
                m_mpvBackend->seek(seconds);
                break;

            case BackendType::Tracker:
                m_audioEngine->seekTracker(seconds);
                m_trackedPosition = static_cast<qint64>(seconds * 1000);
                emit positionChanged(m_trackedPosition);
                break;

            default:
                break;
        }
    }

    // ================ State & Metadata Management ================

    void MediaPlayer::setState(PlaybackState state) {
        if (m_state == state) return;
        m_state = state;
        emit stateChanged(state);
    }

    void MediaPlayer::syncMetadataFromTracker() {
        ModTrackerPlayback *tracker = m_audioEngine->tracker();
        m_metadata = TrackMetadata();
        m_metadata.title = tracker->title();
        m_metadata.artist = tracker->artist();
        m_metadata.comment = tracker->comment();
        m_metadata.duration = static_cast<int>(tracker->duration() * 1000);
        m_metadata.channels = tracker->getNumChannels();
        m_metadata.patterns = tracker->getNumPatterns();
        m_metadata.isTracker = true;
        m_metadata.hasVideo = false;

        emit metadataChanged(m_metadata);
        emit durationChanged(m_metadata.duration);
    }

    void MediaPlayer::syncMetadataFromMpv(const QVariantMap &metadata) {
        m_metadata = TrackMetadata();
        m_metadata.title = metadata.value("title").toString();
        m_metadata.artist = metadata.value("artist").toString();
        m_metadata.album = metadata.value("album").toString();
        m_metadata.year = metadata.value("year").toInt();
        m_metadata.hasVideo = metadata.value("vid", false).toBool();
        m_metadata.isTracker = false;

        emit metadataChanged(m_metadata);
    }

    void MediaPlayer::resetMetadata() {
        m_metadata = TrackMetadata();
    }

    void MediaPlayer::cleanupCurrentPlayback() {
        stop();
        m_source.clear();
        if (m_activeBackend == BackendType::Tracker) {
            m_audioEngine->unloadTracker();
        }
    }

    // ================ Signal Handlers ================

    void MediaPlayer::onMpvPositionChanged(double position) {
        emit positionChanged(static_cast<qint64>(position * 1000));
    }

    void MediaPlayer::onMpvDurationChanged(double duration) {
        emit durationChanged(static_cast<qint64>(duration * 1000));
        m_metadata.duration = static_cast<int>(duration * 1000);
    }

    void MediaPlayer::onMpvStateChanged(int state) {
        switch (state) {
            case 0: // Idle
                setState(PlaybackState::Stopped);
                break;
            case 1: // Loading
                setState(PlaybackState::Buffering);
                break;
            case 2: // Playing
                setState(PlaybackState::Playing);
                m_positionTimer.start();
                break;
            case 3: // Paused
                setState(PlaybackState::Paused);
                m_positionTimer.stop();
                break;
        }
    }

    void MediaPlayer::onMpvMetadataChanged(const QVariantMap &metadata) {
        syncMetadataFromMpv(metadata);
    }

    void MediaPlayer::onMpvFinished() {
        m_positionTimer.stop();
        emit finished();
        loadNextPlaylistItem();
    }

    void MediaPlayer::onMpvError(const QString &message) {
        emit error("MPV: " + message);
        setState(PlaybackState::Error);
    }

    void MediaPlayer::onMpvAudioData(const QByteArray &data, int sampleRate) {
        // Handled in setupMpvAudioCallback
        Q_UNUSED(data)
        Q_UNUSED(sampleRate)
    }

    // Tracker handlers
    void MediaPlayer::onTrackerPositionChanged() {
        if (m_activeBackend != BackendType::Tracker) return;
        m_trackedPosition = static_cast<qint64>(m_audioEngine->tracker()->position() * 1000);
        emit positionChanged(m_trackedPosition);
    }

    void MediaPlayer::onTrackerFinished() {
        m_positionTimer.stop();
        setState(PlaybackState::Stopped);
        emit finished();
        loadNextPlaylistItem();
    }

    void MediaPlayer::onTrackerError(const QString &message) {
        emit error("Tracker: " + message);
        setState(PlaybackState::Error);
    }

    // Audio output handlers
    void MediaPlayer::onAudioOutputStateChanged(bool playing) {
        qDebug() << "Audio output state changed:" << playing;
    }

    void MediaPlayer::onAudioOutputUnderrun() {
        qWarning() << "Audio output underrun detected";
    }

    void MediaPlayer::updatePosition() {
        if (m_activeBackend == BackendType::Tracker) {
            onTrackerPositionChanged();
        }
        // MPV position updated via signal
    }

    // ================ Playlist Integration ================

    void MediaPlayer::setPlaylist(std::shared_ptr<Playlist> playlist) {
        m_playlist = playlist;
        m_playlistMode = (playlist != nullptr);
    }

    void MediaPlayer::playAt(int index) {
        if (!m_playlist) return;
        auto item = m_playlist->at(index);
        if (item.isValid()) {
            m_currentPlaylistIndex = index;
            load(item.url());
        }
    }

    void MediaPlayer::next() {
        loadNextPlaylistItem();
    }

    void MediaPlayer::previous() {
        if (!m_playlist || m_currentPlaylistIndex <= 0) return;
        playAt(m_currentPlaylistIndex - 1);
    }

    void MediaPlayer::loadNextPlaylistItem() {
        if (!m_playlist || !m_playlistMode) return;

        int next = m_currentPlaylistIndex + 1;
        if (next < m_playlist->count()) {
            playAt(next);
        } else if (m_settings->value("Playback/RepeatMode", false).toBool()) {
            playAt(0);
        }
    }

    // ================ Volume & Audio Settings ================

    void MediaPlayer::setVolume(double volume) {
        volume = std::clamp(volume, 0.0, 1.0);
        if (m_volume == volume) return;

        m_volume = volume;

        // Control volume via AudioOutput
        if (m_output) {
            m_output->setVolume(volume);
        }

        // Keep MPV at 100%, we control volume
        m_mpvBackend->setVolume(100.0);

        // Tracker volume via AudioEngine
        m_audioEngine->tracker()->setVolume(volume);

        m_settings->setValue("Audio/Volume", volume);
        emit volumeChanged(volume);
    }

    double MediaPlayer::volume() const {
        return m_volume;
    }

    void MediaPlayer::setMuted(bool muted) {
        if (m_muted == muted) return;
        m_muted = muted;

        // Mute via AudioOutput
        if (m_output) {
            if (muted) {
                m_output->setVolume(0.0);
            } else {
                m_output->setVolume(m_volume);
            }
        }

        emit mutedChanged(muted);
    }

    bool MediaPlayer::muted() const {
        return m_muted;
    }

    qint64 MediaPlayer::position() const {
        if (m_activeBackend == BackendType::Tracker) {
            return m_trackedPosition;
        }
        // MPV position from last signal
        return 0;
    }

    qint64 MediaPlayer::duration() const {
        return m_metadata.duration;
    }

    int MediaPlayer::currentTrackerPattern() const {
        if (m_activeBackend != BackendType::Tracker) return 0;
        return m_audioEngine->tracker()->getCurrentPattern();
    }

    int MediaPlayer::currentTrackerRow() const {
        if (m_activeBackend != BackendType::Tracker) return 0;
        return m_audioEngine->tracker()->getCurrentRow();
    }

    bool MediaPlayer::switchAudioBackend(OutputBackend backend) {
        bool wasPlaying = (m_state == PlaybackState::Playing);
        qint64 currentPos = position();

        // Stop current playback
        stop();

        // Create new output
        auto newOutput = AudioOutputFactory::create(backend);
        if (!newOutput) {
            emit error("Failed to create audio output backend");
            return false;
        }

        OutputConfig config;
        config.sampleRate = 48000;
        config.channels = 2;
        config.preferredBackend = backend;

        if (!newOutput->initialize(config)) {
            emit error("Failed to initialize audio output backend");
            return false;
        }

        // Replace output
        m_output = std::move(newOutput);

        // Reconnect signals
        connect(m_output.get(), &AudioOutput::stateChanged,
                this, &MediaPlayer::onAudioOutputStateChanged);
        connect(m_output.get(), &AudioOutput::underrunDetected,
                this, &MediaPlayer::onAudioOutputUnderrun);
        connect(m_output.get(), &AudioOutput::error,
                this, &MediaPlayer::onMpvError);

        // Restore callback
        m_output->setAudioCallback([this](float* buffer, int frames) {
            processAudioOutput(buffer, frames);
        });

        emit audioBackendChanged();

        // Resume if was playing
        if (wasPlaying && !m_source.isEmpty()) {
            load(m_source);
            seek(currentPos);
            play();
        }

        return true;
    }

    OutputBackend MediaPlayer::currentAudioBackend() const {
        return m_output ? m_output->backendType() : OutputBackend::Auto;
    }

} // namespace Aegis
