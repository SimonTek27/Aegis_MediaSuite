// core.h - Clean Architecture Core with Dependency Injection

#pragma once

#include <QObject>
#include <QUrl>
#include <QTimer>
#include <memory>
#include <vector>
#include <optional>
#include <concepts>
#include "raii_wrappers.h"
#include "mediaplayer.h"

namespace Aegis {

    // ============================================================================
    // Pure Interfaces (Dependency Inversion)
    // ============================================================================

    class IAudioBackend : public QObject {
        Q_OBJECT
    public:
        explicit IAudioBackend(QObject* parent = nullptr) : QObject(parent) {}
        ~IAudioBackend() override = default;

        virtual Result<void> load(const QString& path) = 0;
        virtual Result<void> play() = 0;
        virtual Result<void> pause() = 0;
        virtual Result<void> stop() = 0;
        virtual Result<void> seek(double position) = 0;
        virtual Result<void> setVolume(double volume) = 0;

        virtual PlaybackState state() const = 0;
        virtual double position() const = 0;
        virtual double duration() const = 0;
        virtual TrackMetadata metadata() const = 0;
        virtual bool hasVideo() const = 0;

        virtual void setAudioCallback(std::function<void(const QByteArray&, int)> cb) = 0;

    signals:
        void stateChanged(PlaybackState state);
        void positionChanged(double position);
        void durationChanged(double duration);
        void metadataChanged(const TrackMetadata& metadata);
        void finished();
        void error(const QString& message);
    };

    class IVideoBackend {
    public:
        virtual ~IVideoBackend() = default;

        virtual Result<void> load(const QUrl& url) = 0;
        virtual Result<void> play() = 0;
        virtual Result<void> pause() = 0;
        virtual Result<void> stop() = 0;
        virtual Result<void> seek(double position) = 0;
        virtual QImage currentFrame() const = 0;
        virtual QSize videoSize() const = 0;
        virtual bool hasVideo() const = 0;
    };

    class IAudioEngine {
    public:
        virtual ~IAudioEngine() = default;

        virtual Result<void> processBuffer(float* buffer, int frames,
                                           int sampleRate, int channels) = 0;
                                           virtual Result<std::vector<float>> analyzeSpectrum(int size) = 0;
                                           virtual double rmsLevel() const = 0;
                                           virtual double peakLevel() const = 0;
                                           virtual void setBpm(double bpm) = 0;
                                           virtual double bpm() const = 0;
    };

    // ============================================================================
    // Backend Factory with Availability Checking
    // ============================================================================

    template<typename T>
    concept BackendConcept = requires {
        typename T::Capabilities;
        { T::name() } -> std::convertible_to<QString>;
        { T::isAvailable() } -> std::convertible_to<bool>;
    };

    template<BackendConcept T>
    class BackendFactory {
    public:
        using Capabilities = typename T::Capabilities;

        static Result<std::unique_ptr<T>> create(QObject* parent = nullptr) {
            if (!T::isAvailable()) {
                return Result<std::unique_ptr<T>>::error(
                    QString("%1 backend not available").arg(T::name()));
            }

            try {
                return Result<std::unique_ptr<T>>::success(
                    std::make_unique<T>(parent));
            } catch (const std::exception& e) {
                return Result<std::unique_ptr<T>>::error(
                    QString("Failed to create %1 backend: %2")
                    .arg(T::name(), e.what()));
            }
        }
    };

    // ============================================================================
    // Playlist Item with Rich Metadata
    // ============================================================================

    class PlaylistItem {
    public:
        enum class Type {
            LocalFile,
            Stream,
            DiscTrack,
            Radio,
            Podcast
        };

    private:
        QUrl m_url;
        QString m_title;
        QString m_artist;
        QString m_album;
        std::optional<double> m_duration;
        Type m_type{Type::LocalFile};
        QVariantMap m_metadata;

    public:
        PlaylistItem() = default;
        explicit PlaylistItem(const QUrl& url, Type type = Type::LocalFile);

        const QUrl& url() const { return m_url; }
        const QString& title() const { return m_title; }
        const QString& artist() const { return m_artist; }
        const QString& album() const { return m_album; }
        std::optional<double> duration() const { return m_duration; }
        Type type() const { return m_type; }
        const QVariantMap& metadata() const { return m_metadata; }

        void setTitle(const QString& t) { m_title = t; }
        void setArtist(const QString& a) { m_artist = a; }
        void setAlbum(const QString& a) { m_album = a; }
        void setDuration(double d) { m_duration = d; }
        void setMetadata(const QVariantMap& md) { m_metadata = md; }

        bool isValid() const { return m_url.isValid(); }
        QString displayName() const;
    };

    // ============================================================================
    // Core Class with Dependency Injection
    // ============================================================================

    class Core : public QObject {
        Q_OBJECT
        Q_PROPERTY(PlaybackState state READ state NOTIFY stateChanged)
        Q_PROPERTY(double position READ position NOTIFY positionChanged)
        Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
        Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
        Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
        Q_PROPERTY(int playlistCount READ playlistCount NOTIFY playlistChanged)

    public:
        // Constructor with dependency injection
        explicit Core(
            std::unique_ptr<IAudioBackend> audioBackend,
            std::unique_ptr<IVideoBackend> videoBackend,
            std::unique_ptr<IAudioEngine> audioEngine,
            QObject* parent = nullptr
        ) : QObject(parent)
        , m_audioBackend(std::move(audioBackend))
        , m_videoBackend(std::move(videoBackend))
        , m_audioEngine(std::move(audioEngine)) {

            if (!m_audioBackend || !m_videoBackend || !m_audioEngine) {
                throw std::invalid_argument("All backends must be provided");
            }

            setupConnections();
            setupPositionTimer();
        }

        ~Core() override = default;

        // Disable copying
        Core(const Core&) = delete;
        Core& operator=(const Core&) = delete;

        // ================ Playback Control ================

        Q_INVOKABLE Result<void> load(const QUrl& url) {
            if (!url.isValid()) {
                return Result<void>::error("Invalid URL");
            }

            stop();

            QString path = url.toLocalFile();
            if (path.isEmpty()) {
                path = url.toString();  // Handle streams
            }

            auto result = m_audioBackend->load(path);
            if (result.isError()) {
                return result;
            }

            if (hasVideo()) {
                auto videoResult = m_videoBackend->load(url);
                if (videoResult.isError()) {
                    qWarning() << "Video load failed:" << videoResult.error();
                }
            }

            m_currentPlaylistIndex = -1;
            emit sourceChanged(url);

            return Result<void>::success();
        }

        Q_INVOKABLE Result<void> play() {
            if (m_playlist.empty() && m_currentPlaylistIndex < 0) {
                return Result<void>::error("No media loaded");
            }

            if (m_currentPlaylistIndex < 0) {
                setPlaylistIndex(0);
            }

            auto result = m_audioBackend->play();
            if (result.isError()) {
                return result;
            }

            if (hasVideo()) {
                m_videoBackend->play();
            }

            m_positionTimer.start();
            return Result<void>::success();
        }

        Q_INVOKABLE Result<void> pause() {
            auto result = m_audioBackend->pause();
            if (result.isError()) {
                return result;
            }

            if (hasVideo()) {
                m_videoBackend->pause();
            }

            m_positionTimer.stop();
            return Result<void>::success();
        }

        Q_INVOKABLE Result<void> playPause() {
            if (state() == PlaybackState::Playing) {
                return pause();
            } else {
                return play();
            }
        }

        Q_INVOKABLE Result<void> stop() {
            m_audioBackend->stop();
            if (hasVideo()) {
                m_videoBackend->stop();
            }
            m_positionTimer.stop();
            m_position.store(0.0);
            emit positionChanged(0.0);

            return Result<void>::success();
        }

        Q_INVOKABLE Result<void> seek(double position) {
            position = std::clamp(position, 0.0, m_duration.load());

            auto result = m_audioBackend->seek(position);
            if (result.isError()) {
                return result;
            }

            if (hasVideo()) {
                m_videoBackend->seek(position);
            }

            m_position.store(position);
            emit positionChanged(position);

            return Result<void>::success();
        }

        Q_INVOKABLE Result<void> setVolume(double volume) {
            volume = std::clamp(volume, 0.0, 100.0);

            auto result = m_audioBackend->setVolume(volume);
            if (result.isError()) {
                return result;
            }

            m_volume.store(volume);
            emit volumeChanged(volume);

            return Result<void>::success();
        }

        // ================ Playlist Management ================

        Q_INVOKABLE void enqueue(const QUrl& url) {
            PlaylistItem item(url);
            m_playlist.push_back(item);
            emit playlistChanged();
        }

        Q_INVOKABLE void enqueue(const PlaylistItem& item) {
            m_playlist.push_back(item);
            emit playlistChanged();
        }

        Q_INVOKABLE Result<void> enqueueAndPlay(const QUrl& url) {
            enqueue(url);
            return play();
        }

        Q_INVOKABLE Result<void> removeFromPlaylist(int index) {
            if (index < 0 || index >= static_cast<int>(m_playlist.size())) {
                return Result<void>::error("Invalid playlist index");
            }

            m_playlist.erase(m_playlist.begin() + index);

            if (index == m_currentPlaylistIndex.load()) {
                stop();
                m_currentPlaylistIndex.store(-1);
            } else if (index < m_currentPlaylistIndex.load()) {
                m_currentPlaylistIndex.store(m_currentPlaylistIndex.load() - 1);
            }

            emit playlistChanged();
            return Result<void>::success();
        }

        Q_INVOKABLE void clearPlaylist() {
            m_playlist.clear();
            m_currentPlaylistIndex.store(-1);
            emit playlistChanged();
        }

        Q_INVOKABLE Result<void> setPlaylistIndex(int index) {
            if (index < 0 || index >= static_cast<int>(m_playlist.size())) {
                return Result<void>::error("Invalid playlist index");
            }

            return loadTrack(index);
        }

        Q_INVOKABLE Result<void> next() {
            int next = m_currentPlaylistIndex.load() + 1;
            if (next < static_cast<int>(m_playlist.size())) {
                return setPlaylistIndex(next);
            } else if (m_repeatMode == RepeatMode::Playlist) {
                return setPlaylistIndex(0);
            }
            return Result<void>::success();  // End of playlist
        }

        Q_INVOKABLE Result<void> previous() {
            int prev = m_currentPlaylistIndex.load() - 1;
            if (prev >= 0) {
                return setPlaylistIndex(prev);
            }
            return Result<void>::success();
        }

        // ================ State Getters ================

        PlaybackState state() const { return m_state.load(); }
        double position() const { return m_position.load(); }
        double duration() const { return m_duration.load(); }
        double volume() const { return m_volume.load(); }
        bool hasVideo() const { return m_videoBackend->hasVideo(); }

        int playlistCount() const { return static_cast<int>(m_playlist.size()); }
        int currentPlaylistIndex() const { return m_currentPlaylistIndex.load(); }

        std::optional<PlaylistItem> currentItem() const {
            int idx = m_currentPlaylistIndex.load();
            if (idx >= 0 && idx < static_cast<int>(m_playlist.size())) {
                return m_playlist[static_cast<size_t>(idx)];
            }
            return std::nullopt;
        }

        const std::vector<PlaylistItem>& playlist() const { return m_playlist; }

        // QML-friendly playlist
        QVariantList playlistVariant() const;

        // ================ Audio Analysis ================

        IAudioEngine* audioEngine() { return m_audioEngine.get(); }
        const IAudioEngine* audioEngine() const { return m_audioEngine.get(); }

        Result<std::vector<float>> analyzeSpectrum(int size = 1024) {
            return m_audioEngine->analyzeSpectrum(size);
        }

        // ================ Repeat/Shuffle ================

        enum class RepeatMode { None, Track, Playlist };
        Q_ENUM(RepeatMode)

        void setRepeatMode(RepeatMode mode) { m_repeatMode = mode; }
        RepeatMode repeatMode() const { return m_repeatMode; }

        void setShuffle(bool enabled) { m_shuffle = enabled; }
        bool shuffle() const { return m_shuffle; }

    signals:
        void stateChanged(PlaybackState state);
        void positionChanged(double position);
        void durationChanged(double duration);
        void volumeChanged(double volume);
        void hasVideoChanged(bool hasVideo);
        void sourceChanged(const QUrl& source);
        void playlistChanged();
        void currentTrackChanged(const PlaylistItem& item);
        void playlistFinished();
        void error(const QString& message);

        // FIX: Moved private helper declarations before private slots
    private:
        void dequeue(int index);
        QString currentTitle() const;
        void loadTrackImpl(int index);
        void updateMetadata();
        void onBackendPositionChanged(double pos);

    private slots:
        void onAudioBackendStateChanged(PlaybackState state) {
            m_state.store(state);
            emit stateChanged(state);

            if (state == PlaybackState::Playing) {
                m_positionTimer.start();
            } else {
                m_positionTimer.stop();
            }
        }

        void onAudioBackendPositionChanged(double pos) {
            m_position.store(pos);
            emit positionChanged(pos);
        }

        void onAudioBackendDurationChanged(double dur) {
            m_duration.store(dur);
            emit durationChanged(dur);

            if (auto item = currentItem()) {
                const_cast<PlaylistItem&>(*item).setDuration(dur);
            }
        }

        void onAudioBackendFinished() {
            if (m_repeatMode == RepeatMode::Track) {
                seek(0);
                play();
            } else {
                next().onError([this](const QString& err) {
                    qWarning() << "Auto-advance failed:" << err;
                    emit playlistFinished();
                });
            }
        }

    private:
        Result<void> loadTrack(int index) {
            if (index < 0 || index >= static_cast<int>(m_playlist.size())) {
                return Result<void>::error("Invalid track index");
            }

            const auto& item = m_playlist[static_cast<size_t>(index)];

            auto result = m_audioBackend->load(item.url().toLocalFile());
            if (result.isError()) {
                return result;
            }

            if (hasVideo()) {
                m_videoBackend->load(item.url());
            }

            m_audioBackend->setVolume(m_volume.load());
            m_currentPlaylistIndex.store(index);
            emit currentTrackChanged(item);
            emit sourceChanged(item.url());

            return Result<void>::success();
        }

        void setupConnections() {
            // Forward backend signals
            connect(m_audioBackend.get(), &IAudioBackend::stateChanged,
                    this, &Core::onAudioBackendStateChanged);
            connect(m_audioBackend.get(), &IAudioBackend::positionChanged,
                    this, &Core::onAudioBackendPositionChanged);
            connect(m_audioBackend.get(), &IAudioBackend::durationChanged,
                    this, &Core::onAudioBackendDurationChanged);
            connect(m_audioBackend.get(), &IAudioBackend::finished,
                    this, &Core::onAudioBackendFinished);
            connect(m_audioBackend.get(), &IAudioBackend::error,
                    this, &Core::error);
        }

        void setupPositionTimer() {
            m_positionTimer.setInterval(16);  // ~60fps
            connect(&m_positionTimer, &QTimer::timeout, [this]() {
                if (state() == PlaybackState::Playing) {
                    m_position.store(m_audioBackend->position());
                    emit positionChanged(m_position.load());
                }
            });
        }

        // Dependencies (injected)
        std::unique_ptr<IAudioBackend> m_audioBackend;
        std::unique_ptr<IVideoBackend> m_videoBackend;
        std::unique_ptr<IAudioEngine> m_audioEngine;

        // State
        std::vector<PlaylistItem> m_playlist;
        std::atomic<int> m_currentPlaylistIndex{-1};
        std::atomic<PlaybackState> m_state{PlaybackState::Stopped};
        std::atomic<double> m_position{0.0};
        std::atomic<double> m_duration{0.0};
        std::atomic<double> m_volume{100.0};

        RepeatMode m_repeatMode{RepeatMode::None};
        bool m_shuffle{false};

        QTimer m_positionTimer;
    };

} // namespace Aegis
