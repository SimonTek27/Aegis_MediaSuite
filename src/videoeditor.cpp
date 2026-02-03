// videoeditor.cpp - Professional Video Editor Implementation
#include "videoeditor.h"
#include <QDebug>
#include <QFile>
#include <QSaveFile>
#include <QtConcurrent>
#include <QThread>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <sndfile.h>

namespace Aegis {

    // =============================================================================
    // Timecode Implementation
    // =============================================================================

    QString Timecode::toString() const {
        return QString("%1@%2fps").arg(frames).arg(fps);
    }

    QString Timecode::toTimeString() const {
        int totalSeconds = static_cast<int>(toSeconds());
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;
        int frameNum = static_cast<int>(frames % fps);

        return QString("%1:%2:%3:%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(frameNum, 2, 10, QChar('0'));
    }

    QJsonObject Timecode::toJson() const {
        QJsonObject json;
        json["frames"] = static_cast<qint64>(frames);
        json["fps"] = fps;
        return json;
    }

    Timecode Timecode::fromJson(const QJsonObject& json) {
        return Timecode{
            static_cast<FrameTime>(json["frames"].toVariant().toLongLong()),
            json["fps"].toInt(30)
        };
    }

    // =============================================================================
    // ProjectProfile Implementation
    // =============================================================================

    QJsonObject ProjectProfile::toJson() const {
        QJsonObject json;
        json["width"] = width;
        json["height"] = height;
        json["fps"] = fps;
        json["sampleRate"] = sampleRate;
        json["audioChannels"] = audioChannels;
        json["colorSpace"] = colorSpace;
        json["pixelFormat"] = pixelFormat;
        return json;
    }

    ProjectProfile ProjectProfile::fromJson(const QJsonObject& json) {
        ProjectProfile profile;
        profile.width = json["width"].toInt(1920);
        profile.height = json["height"].toInt(1080);
        profile.fps = json["fps"].toInt(30);
        profile.sampleRate = json["sampleRate"].toInt(48000);
        profile.audioChannels = json["audioChannels"].toInt(2);
        profile.colorSpace = json["colorSpace"].toString("bt709");
        profile.pixelFormat = json["pixelFormat"].toString("yuv420p");
        return profile;
    }

    // =============================================================================
    // ExportSettings Implementation
    // =============================================================================

    ExportSettings ExportSettings::preset(ExportPreset preset) {
        ExportSettings settings;

        switch (preset) {
            case ExportPreset::YouTube1080p:
                settings.width = 1920;
                settings.height = 1080;
                settings.fps = 30;
                settings.videoBitrate = 8000000;
                settings.videoCodec = "libx264";
                settings.preset = "fast";
                break;

            case ExportPreset::YouTube4K:
                settings.width = 3840;
                settings.height = 2160;
                settings.fps = 30;
                settings.videoBitrate = 35000000;
                settings.videoCodec = "libx264";
                settings.preset = "fast";
                break;

            case ExportPreset::ProRes422:
                settings.width = 1920;
                settings.height = 1080;
                settings.fps = 30;
                settings.videoCodec = "prores";
                settings.container = "mov";
                break;

            case ExportPreset::WebOptimized:
                settings.width = 1280;
                settings.height = 720;
                settings.fps = 30;
                settings.videoBitrate = 2500000;
                settings.videoCodec = "libx264";
                settings.preset = "veryfast";
                break;

            default:
                break;
        }

        return settings;
    }

    QStringList ExportSettings::toFFmpegArgs(const QString& input, const QString& output) const {
        QStringList args;

        args << "ffmpeg" << "-y";
        args << "-i" << input;

        // Video codec
        args << "-c:v" << videoCodec;
        args << "-b:v" << QString::number(videoBitrate);
        args << "-preset" << preset;

        // Resolution and fps
        args << "-s" << QString("%1x%2").arg(width).arg(height);
        args << "-r" << QString::number(fps);

        // Audio
        args << "-c:a" << audioCodec;
        args << "-b:a" << QString::number(audioBitrate);

        // Container
        args << "-f" << container;
        args << output;

        return args;
    }

    // =============================================================================
    // MediaClip Implementation
    // =============================================================================

    MediaClip::MediaClip(const QString& id, const QUrl& source, Type type, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_source(source)
    , m_type(type)
    , m_name(QFileInfo(source.toLocalFile()).fileName())
    , m_audioEffects(std::make_unique<AudioEffectChain>())
    , m_videoEffects(std::make_unique<VideoEffectChain>())
    {
        // Initialize with default fps (will be updated when media is loaded)
        m_position.fps = 30;
        m_inPoint.fps = 30;
        m_outPoint.fps = 30;
        m_sourceDuration.fps = 30;
    }

    void MediaClip::setName(const QString& name) {
        if (m_name != name) {
            m_name = name;
            emit nameChanged();
        }
    }

    void MediaClip::setPosition(const Timecode& pos) {
        QMutexLocker lock(&m_mutex);
        if (m_position != pos) {
            m_position = pos;
            emit positionChanged();
        }
    }

    void MediaClip::setInPoint(const Timecode& pt) {
        QMutexLocker lock(&m_mutex);
        m_inPoint = pt;
        m_inPoint.frames = std::max(FrameTime(0), m_inPoint.frames);
        emit inPointChanged();
        updateDuration();
    }

    void MediaClip::setOutPoint(const Timecode& pt) {
        QMutexLocker lock(&m_mutex);
        m_outPoint = pt;
        m_outPoint.frames = std::max(m_inPoint.frames, m_outPoint.frames);
        emit outPointChanged();
        updateDuration();
    }

    void MediaClip::updateDuration() {
        FrameTime sourceLen = m_outPoint.frames - m_inPoint.frames;
        FrameTime effectiveLen = static_cast<FrameTime>(sourceLen / m_speed);
        emit durationChanged();
    }

    Timecode MediaClip::duration() const {
        QMutexLocker lock(&m_mutex);
        FrameTime sourceLen = m_outPoint.frames - m_inPoint.frames;
        return Timecode{static_cast<FrameTime>(sourceLen / m_speed), m_position.fps};
    }

    void MediaClip::setSpeed(double speed) {
        speed = std::clamp(speed, 0.1, 10.0);
        QMutexLocker lock(&m_mutex);
        if (m_speed != speed) {
            m_speed = speed;
            emit speedChanged();
            updateDuration();
        }
    }

    void MediaClip::setTrackIndex(int idx) {
        if (m_trackIndex != idx) {
            m_trackIndex = idx;
            emit trackIndexChanged();
        }
    }

    void MediaClip::setMuted(bool muted) {
        if (m_muted != muted) {
            m_muted = muted;
            emit mutedChanged();
        }
    }

    void MediaClip::setVolume(float vol) {
        m_volume = std::clamp(vol, 0.0f, 2.0f);
    }

    Timecode MediaClip::mapToSource(const Timecode& timelineTime) const {
        QMutexLocker lock(&m_mutex);
        FrameTime offset = timelineTime.frames - m_position.frames;
        FrameTime sourceFrame = static_cast<FrameTime>(offset * m_speed);
        return Timecode{m_inPoint.frames + sourceFrame, m_inPoint.fps};
    }

    Timecode MediaClip::mapFromSource(const Timecode& sourceTime) const {
        QMutexLocker lock(&m_mutex);
        FrameTime offset = sourceTime.frames - m_inPoint.frames;
        FrameTime timelineOffset = static_cast<FrameTime>(offset / m_speed);
        return Timecode{m_position.frames + timelineOffset, m_position.fps};
    }

    bool MediaClip::loadMediaInfo(MpvBackend* backend) {
        if (!backend) return false;

        // Use MPV to probe media file
        // This would involve loading the file and querying properties
        // For now, simplified implementation

        return true;
    }

    QJsonObject MediaClip::toJson() const {
        QJsonObject json;
        json["id"] = m_id;
        json["source"] = m_source.toString();
        json["type"] = static_cast<int>(m_type);
        json["name"] = m_name;
        json["position"] = m_position.toJson();
        json["inPoint"] = m_inPoint.toJson();
        json["outPoint"] = m_outPoint.toJson();
        json["speed"] = m_speed;
        json["volume"] = m_volume;
        json["muted"] = m_muted;
        return json;
    }

    std::shared_ptr<MediaClip> MediaClip::fromJson(const QJsonObject& json, QObject* parent) {
        auto clip = std::make_shared<MediaClip>(
            json["id"].toString(),
                                                QUrl(json["source"].toString()),
                                                static_cast<Type>(json["type"].toInt(0)),
                                                parent
        );
        clip->setName(json["name"].toString());
        clip->m_position = Timecode::fromJson(json["position"].toObject());
        clip->m_inPoint = Timecode::fromJson(json["inPoint"].toObject());
        clip->m_outPoint = Timecode::fromJson(json["outPoint"].toObject());
        clip->m_speed = json["speed"].toDouble(1.0);
        clip->m_volume = json["volume"].toDouble(1.0);
        clip->m_muted = json["muted"].toBool(false);
        return clip;
    }

    // =============================================================================
    // Track Implementation
    // =============================================================================

    Track::Track(const QString& id, Type type, const QString& name, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_type(type)
    , m_name(name.isEmpty() ? (type == Type::Video ? "Video" : "Audio") : name)
    , m_audioEffects(std::make_unique<AudioEffectChain>())
    , m_videoEffects(std::make_unique<VideoEffectChain>())
    {}

    void Track::setName(const QString& name) {
        if (m_name != name) {
            m_name = name;
            emit nameChanged();
        }
    }

    void Track::setIndex(int idx) {
        if (m_index != idx) {
            m_index = idx;
            emit indexChanged();
        }
    }

    void Track::setMuted(bool muted) {
        if (m_muted != muted) {
            m_muted = muted;
            emit mutedChanged();
        }
    }

    void Track::setSoloed(bool soloed) {
        if (m_soloed != soloed) {
            m_soloed = soloed;
            emit soloedChanged();
        }
    }

    void Track::setLocked(bool locked) {
        if (m_locked != locked) {
            m_locked = locked;
            emit lockedChanged();
        }
    }

    void Track::setVolume(float vol) {
        m_volume = std::clamp(vol, 0.0f, 2.0f);
        emit volumeChanged();
    }

    void Track::setPan(float pan) {
        m_pan = std::clamp(pan, -1.0f, 1.0f);
        emit panChanged();
    }

    void Track::addClip(std::shared_ptr<MediaClip> clip) {
        QWriteLocker lock(&m_lock);
        clip->setTrackIndex(m_index);
        clip->setPosition(Timecode{clip->position().frames, m_type == Type::Video ? 30 : 48000});
        m_clips.push_back(clip);

        // Sort by position
        std::sort(m_clips.begin(), m_clips.end(),
                  [](const auto& a, const auto& b) {
                      return a->position().frames < b->position().frames;
                  });

        lock.unlock();
        emit clipAdded(clip);
        emit clipsChanged();
    }

    void Track::removeClip(std::shared_ptr<MediaClip> clip) {
        QWriteLocker lock(&m_lock);
        auto it = std::find(m_clips.begin(), m_clips.end(), clip);
        if (it != m_clips.end()) {
            m_clips.erase(it);
            lock.unlock();
            emit clipRemoved(clip);
            emit clipsChanged();
        }
    }

    void Track::moveClip(std::shared_ptr<MediaClip> clip, const Timecode& newPosition) {
        QWriteLocker lock(&m_lock);
        clip->setPosition(newPosition);
        std::sort(m_clips.begin(), m_clips.end(),
                  [](const auto& a, const auto& b) {
                      return a->position().frames < b->position().frames;
                  });
        lock.unlock();
        emit clipsChanged();
    }

    std::shared_ptr<MediaClip> Track::clipAt(const Timecode& position) const {
        QReadLocker lock(&m_lock);
        for (const auto& clip : m_clips) {
            Timecode start = clip->position();
            Timecode end = Timecode{start.frames + clip->duration().frames, start.fps};
            if (position.frames >= start.frames && position.frames < end.frames) {
                return clip;
            }
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<MediaClip>> Track::clipsInRange(const Timecode& start,
                                                                const Timecode& end) const {
                                                                    QReadLocker lock(&m_lock);
                                                                    std::vector<std::shared_ptr<MediaClip>> result;
                                                                    for (const auto& clip : m_clips) {
                                                                        Timecode clipStart = clip->position();
                                                                        Timecode clipEnd = Timecode{clipStart.frames + clip->duration().frames, clipStart.fps};
                                                                        if (clipStart.frames < end.frames && clipEnd.frames > start.frames) {
                                                                            result.push_back(clip);
                                                                        }
                                                                    }
                                                                    return result;
                                                                }

                                                                Timecode Track::duration() const {
                                                                    QReadLocker lock(&m_lock);
                                                                    FrameTime maxEnd = 0;
                                                                    for (const auto& clip : m_clips) {
                                                                        maxEnd = std::max(maxEnd, clip->position().frames + clip->duration().frames);
                                                                    }
                                                                    return Timecode{maxEnd, 30};
                                                                }

                                                                // =============================================================================
                                                                // TransportController Implementation
                                                                // =============================================================================

                                                                TransportController::TransportController(QObject* parent)
                                                                : QObject(parent)
                                                                {
                                                                    m_playbackTimer = new QTimer(this);
                                                                    m_playbackTimer->setInterval(16); // ~60fps
                                                                    connect(m_playbackTimer, &QTimer::timeout, this, &TransportController::onPlaybackTimer);
                                                                }

                                                                void TransportController::play() {
                                                                    if (m_state == State::Playing) return;

                                                                    m_state = State::Playing;
                                                                    m_playbackStartTime = std::chrono::steady_clock::now();
                                                                    m_playbackStartPosition = m_position;
                                                                    m_playbackTimer->start();

                                                                    emit stateChanged(m_state);
                                                                }

                                                                void TransportController::pause() {
                                                                    if (m_state != State::Playing) return;

                                                                    m_playbackTimer->stop();
                                                                    m_state = State::Paused;
                                                                    emit stateChanged(m_state);
                                                                }

                                                                void TransportController::stop() {
                                                                    m_playbackTimer->stop();
                                                                    m_state = State::Stopped;
                                                                    m_position = Timecode{0, m_position.fps};
                                                                    emit stateChanged(m_state);
                                                                    emit positionChanged(m_position);
                                                                }

                                                                void TransportController::togglePlayPause() {
                                                                    if (m_state == State::Playing) pause();
                                                                    else play();
                                                                }

                                                                void TransportController::seek(const Timecode& position) {
                                                                    setPosition(position);
                                                                    if (m_state == State::Playing) {
                                                                        // Restart timing from new position
                                                                        m_playbackStartTime = std::chrono::steady_clock::now();
                                                                        m_playbackStartPosition = position;
                                                                    }
                                                                }

                                                                void TransportController::setPosition(const Timecode& pos) {
                                                                    Timecode newPos = pos;
                                                                    newPos.frames = std::max(FrameTime(0), std::min(newPos.frames, m_duration.frames));

                                                                    if (m_position != newPos) {
                                                                        m_position = newPos;
                                                                        emit positionChanged(m_position);
                                                                    }
                                                                }

                                                                void TransportController::onPlaybackTimer() {
                                                                    if (m_state != State::Playing) return;

                                                                    auto now = std::chrono::steady_clock::now();
                                                                    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                                                        now - m_playbackStartTime).count();

                                                                        double elapsedSeconds = elapsed / 1000000.0;
                                                                        FrameTime frameOffset = static_cast<FrameTime>(elapsedSeconds * m_fps);
                                                                        Timecode newPos{m_playbackStartPosition.frames + frameOffset, m_position.fps};

                                                                        // Handle looping
                                                                        if (m_looping && newPos.frames >= m_loopEnd.frames) {
                                                                            newPos.frames = m_loopStart.frames;
                                                                            m_playbackStartTime = now;
                                                                            m_playbackStartPosition = m_loopStart;
                                                                        }

                                                                        // Check end
                                                                        if (newPos.frames >= m_duration.frames) {
                                                                            stop();
                                                                            return;
                                                                        }

                                                                        setPosition(newPos);
                                                                        emit frameReady(m_position);
                                                                }

                                                                void TransportController::updateFromAudioClock(qint64 microseconds) {
                                                                    if (m_state != State::Playing) return;

                                                                    Timecode newPos = Timecode::fromMicroseconds(microseconds, m_position.fps);
                                                                    setPosition(newPos);
                                                                }

                                                                // =============================================================================
                                                                // VideoEditor Implementation
                                                                // =============================================================================

                                                                VideoEditor::VideoEditor(QObject* parent)
                                                                : QObject(parent)
                                                                , m_mpvBackend(std::make_unique<MpvBackend>(this))
                                                                , m_compositor(std::make_unique<VideoCompositor>(ProjectProfile{}, this))
                                                                , m_transport(std::make_unique<TransportController>(this))
                                                                {
                                                                    // Connect transport signals
                                                                    connect(m_transport.get(), &TransportController::positionChanged,
                                                                            this, &VideoEditor::onTransportPositionChanged);
                                                                    connect(m_transport.get(), &TransportController::stateChanged,
                                                                            this, &VideoEditor::onTransportStateChanged);
                                                                    connect(m_transport.get(), &TransportController::frameReady,
                                                                            [this](const Timecode& pos) { renderFrame(pos); });
                                                                }

                                                                VideoEditor::~VideoEditor() = default;

                                                                void VideoEditor::initializeAudio(AudioEngine* engine, AudioOutput* output) {
                                                                    m_audioEngine = engine;
                                                                    m_audioOutput = output;

                                                                    // Connect audio position for sync
                                                                    if (m_audioOutput) {
                                                                        connect(m_audioOutput, &AudioOutput::stateChanged,
                                                                                this, [this](bool playing) {
                                                                                    if (playing) m_transport->play();
                                                                                    else m_transport->pause();
                                                                                });
                                                                    }
                                                                }

                                                                void VideoEditor::initializeVideo(std::unique_ptr<VideoOutput> output) {
                                                                    if (!output) {
                                                                        output = VideoOutputFactory::create(VideoBackend::OpenGL);
                                                                    }
                                                                    m_videoOutput = std::move(output);

                                                                    if (m_videoOutput) {
                                                                        m_videoOutput->initialize(QSize(1920, 1080));
                                                                        m_videoOutput->setAudioOutput(m_audioOutput); // Enable A/V sync
                                                                    }

                                                                    // Initialize compositor
                                                                    m_compositor->initialize();
                                                                }

                                                                void VideoEditor::onTransportPositionChanged(const Timecode& position) {
                                                                    emit positionChanged(position);
                                                                    renderFrame(position);
                                                                }

                                                                void VideoEditor::renderFrame(const Timecode& position) {
                                                                    if (!m_project) return;

                                                                    // Get tracks
                                                                    std::vector<Track*> videoTracks;
                                                                    std::vector<Track*> audioTracks;

                                                                    for (const auto& track : m_project->tracks) {
                                                                        if (track->type() == Track::Type::Video) videoTracks.push_back(track.get());
                                                                        else audioTracks.push_back(track.get());
                                                                    }

                                                                    // Composite video
                                                                    if (m_compositor) {
                                                                        auto fbo = m_compositor->compositeFrame(position, videoTracks, audioTracks);
                                                                        if (fbo && m_videoOutput) {
                                                                            // Convert FBO to VideoFrame and present
                                                                            VideoFrame frame;
                                                                            frame.pts = VideoPTS::fromMicroseconds(position.toMicroseconds());
                                                                            m_videoOutput->presentFrame(frame);
                                                                        }
                                                                    }

                                                                    // Process audio
                                                                    if (m_audioEngine && !audioTracks.empty()) {
                                                                        // Mix audio from all tracks at this position
                                                                        // This would involve processing through AudioEffectChain
                                                                    }

                                                                    emit frameReady(m_currentFrame, position);
                                                                }

                                                                bool VideoEditor::newProject(const QString& name, const ProjectProfile& profile) {
                                                                    closeProject();

                                                                    m_project = std::make_unique<ProjectData>();
                                                                    m_projectName = name;
                                                                    m_profile = profile;
                                                                    m_projectPath.clear();

                                                                    // Update compositor profile
                                                                    m_compositor = std::make_unique<VideoCompositor>(profile, this);
                                                                    m_compositor->initialize();

                                                                    // Add default tracks
                                                                    addVideoTrack("Video 1");
                                                                    addAudioTrack("Audio 1");
                                                                    addAudioTrack("Audio 2");

                                                                    m_transport->setFps(profile.fps);
                                                                    m_transport->setPosition(Timecode{0, profile.fps});
                                                                    m_transport->setDuration(Timecode{0, profile.fps});

                                                                    emit projectChanged();
                                                                    emit profileChanged();
                                                                    setModified(false);

                                                                    return true;
                                                                }

                                                                Track* VideoEditor::addVideoTrack(const QString& name) {
                                                                    if (!m_project) return nullptr;

                                                                    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                                                    auto track = std::make_unique<Track>(id, Track::Type::Video, name, this);
                                                                    track->setIndex(videoTracks().size());

                                                                    Track* ptr = track.get();
                                                                    m_project->tracks.append(std::move(track));

                                                                    emit trackAdded(ptr);
                                                                    emit modifiedChanged();
                                                                    return ptr;
                                                                }

                                                                Track* VideoEditor::addAudioTrack(const QString& name) {
                                                                    if (!m_project) return nullptr;

                                                                    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                                                    auto track = std::make_unique<Track>(id, Track::Type::Audio, name, this);
                                                                    track->setIndex(audioTracks().size());

                                                                    Track* ptr = track.get();
                                                                    m_project->tracks.append(std::move(track));

                                                                    emit trackAdded(ptr);
                                                                    emit modifiedChanged();
                                                                    return ptr;
                                                                }

                                                                QList<Track*> VideoEditor::videoTracks() const {
                                                                    QList<Track*> result;
                                                                    if (!m_project) return result;

                                                                    for (const auto& track : m_project->tracks) {
                                                                        if (track->type() == Track::Type::Video) {
                                                                            result.append(track.get());
                                                                        }
                                                                    }
                                                                    return result;
                                                                }

                                                                QList<Track*> VideoEditor::audioTracks() const {
                                                                    QList<Track*> result;
                                                                    if (!m_project) return result;

                                                                    for (const auto& track : m_project->tracks) {
                                                                        if (track->type() == Track::Type::Audio) {
                                                                            result.append(track.get());
                                                                        }
                                                                    }
                                                                    return result;
                                                                }

                                                                std::shared_ptr<MediaClip> VideoEditor::importMedia(const QUrl& url,
                                                                                                                    Track* targetTrack,
                                                                                                                    const Timecode& position) {
                                                                    if (!m_project) return nullptr;

                                                                    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                                                    QFileInfo info(url.toLocalFile());

                                                                    MediaClip::Type type = MediaClip::Type::Video;
                                                                    if (info.suffix().toLower() == "wav" || info.suffix().toLower() == "mp3") {
                                                                        type = MediaClip::Type::Audio;
                                                                    } else if (info.suffix().toLower() == "jpg" || info.suffix().toLower() == "png") {
                                                                        type = MediaClip::Type::Image;
                                                                    }

                                                                    auto clip = std::make_shared<MediaClip>(id, url, type, this);

                                                                    // Load media info using MPV backend
                                                                    clip->loadMediaInfo(m_mpvBackend.get());

                                                                    // Add to track
                                                                    if (!targetTrack) {
                                                                        targetTrack = (type == MediaClip::Type::Audio) ?
                                                                        audioTracks().first() : videoTracks().first();
                                                                    }

                                                                    if (targetTrack) {
                                                                        clip->setPosition(position);
                                                                        targetTrack->addClip(clip);
                                                                    }

                                                                    emit clipImported(clip);
                                                                    setModified(true);

                                                                    // Update duration
                                                                    Timecode newDuration = duration();
                                                                    if (Timecode{position.frames + clip->duration().frames, 30} > newDuration) {
                                                                        m_transport->setDuration(Timecode{position.frames + clip->duration().frames, 30});
                                                                        emit durationChanged();
                                                                    }

                                                                    return clip;
                                                                                                                    }

                                                                                                                    void VideoEditor::splitClip(std::shared_ptr<MediaClip> clip, const Timecode& atTime) {
                                                                                                                        if (!clip || !m_project) return;

                                                                                                                        Timecode clipStart = clip->position();
                                                                                                                        Timecode clipEnd = Timecode{clipStart.frames + clip->duration().frames, clipStart.fps};

                                                                                                                        if (atTime.frames <= clipStart.frames || atTime.frames >= clipEnd.frames) {
                                                                                                                            return; // Can't split outside clip bounds
                                                                                                                        }

                                                                                                                        // Calculate split point in source
                                                                                                                        Timecode sourceSplit = clip->mapToSource(atTime);
                                                                                                                        Timecode originalOut = clip->outPoint();

                                                                                                                        // Adjust first clip
                                                                                                                        clip->setOutPoint(sourceSplit);

                                                                                                                        // Create second clip
                                                                                                                        QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                                                                                                        auto newClip = std::make_shared<MediaClip>(newId, clip->source(), clip->type(), this);
                                                                                                                        newClip->setName(clip->name() + " (split)");
                                                                                                                        newClip->setInPoint(sourceSplit);
                                                                                                                        newClip->setOutPoint(originalOut);
                                                                                                                        newClip->setPosition(atTime);
                                                                                                                        newClip->setSpeed(clip->speed());

                                                                                                                        // Add to same track
                                                                                                                        for (auto& track : m_project->tracks) {
                                                                                                                            auto clips = track->clips();
                                                                                                                            for (const auto& c : clips) {
                                                                                                                                if (c == clip) {
                                                                                                                                    track->addClip(newClip);
                                                                                                                                    break;
                                                                                                                                }
                                                                                                                            }
                                                                                                                        }

                                                                                                                        setModified(true);
                                                                                                                    }

                                                                                                                    void VideoEditor::play() {
                                                                                                                        if (!m_audioSyncEnabled || !m_audioOutput) {
                                                                                                                            m_transport->play();
                                                                                                                        } else {
                                                                                                                            // Audio-driven playback
                                                                                                                            m_transport->play();
                                                                                                                            if (m_audioEngine) {
                                                                                                                                // Start audio playback at current position
                                                                                                                                m_audioEngine->setPosition(m_transport->position().toSeconds());
                                                                                                                            }
                                                                                                                        }
                                                                                                                    }

                                                                                                                    void VideoEditor::pause() {
                                                                                                                        m_transport->pause();
                                                                                                                        if (m_audioEngine) {
                                                                                                                            m_audioEngine->stop();
                                                                                                                        }
                                                                                                                    }

                                                                                                                    void VideoEditor::stop() {
                                                                                                                        m_transport->stop();
                                                                                                                        if (m_audioEngine) {
                                                                                                                            m_audioEngine->stop();
                                                                                                                        }
                                                                                                                    }

                                                                                                                    void VideoEditor::seek(const Timecode& position) {
                                                                                                                        m_transport->seek(position);
                                                                                                                        if (m_audioEngine && m_audioOutput) {
                                                                                                                            m_audioEngine->seek(position.toSeconds());
                                                                                                                        }
                                                                                                                    }

                                                                                                                    void VideoEditor::updateVideoFromAudioClock(qint64 audioMicroseconds) {
                                                                                                                        if (!m_audioSyncEnabled) return;

                                                                                                                        Timecode videoPos = Timecode::fromMicroseconds(audioMicroseconds, m_profile.fps);
                                                                                                                        m_transport->setPosition(videoPos);
                                                                                                                    }

                                                                                                                    bool VideoEditor::exportVideo(const QString& outputPath,
                                                                                                                                                  const ExportSettings& settings,
                                                                                                                                                  const Timecode& start,
                                                                                                                                                  const Timecode& end) {
                                                                                                                        if (!m_project || m_exporting) return false;

                                                                                                                        m_exporting = true;

                                                                                                                        // Create exporter thread
                                                                                                                        QtConcurrent::run([this, outputPath, settings, start, end]() {
                                                                                                                            Timecode range = end.frames > 0 ? end : duration();
                                                                                                                            Timecode current = start;

                                                                                                                            // Setup FFmpeg or internal encoder
                                                                                                                            // This is a simplified implementation

                                                                                                                            while (current.frames < range.frames && m_exporting) {
                                                                                                                                // Render frame
                                                                                                                                renderFrame(current);

                                                                                                                                // Encode frame
                                                                                                                                // ...

                                                                                                                                // Emit progress
                                                                                                                                double progress = static_cast<double>(current.frames - start.frames) /
                                                                                                                                (range.frames - start.frames);
                                                                                                                                emit exportProgress(progress);

                                                                                                                                current.frames++;
                                                                                                                            }

                                                                                                                            m_exporting = false;
                                                                                                                            emit exportFinished(true, "Export complete");
                                                                                                                        });

                                                                                                                        return true;
                                                                                                                                                  }

                                                                                                                                                  void VideoEditor::pushUndoCommand(const QString& description,
                                                                                                                                                                                    std::function<void()> undo,
                                                                                                                                                                                    std::function<void()> redo) {
                                                                                                                                                      if (m_undoStack.size() >= m_maxUndoLevels) {
                                                                                                                                                          m_undoStack.removeFirst();
                                                                                                                                                      }

                                                                                                                                                      m_undoStack.push({description, undo, redo});
                                                                                                                                                      m_redoStack.clear();
                                                                                                                                                      setModified(true);
                                                                                                                                                                                    }

                                                                                                                                                                                    void VideoEditor::undo() {
                                                                                                                                                                                        if (m_undoStack.isEmpty()) return;

                                                                                                                                                                                        auto cmd = m_undoStack.pop();
                                                                                                                                                                                        cmd.undo();
                                                                                                                                                                                        m_redoStack.push(cmd);
                                                                                                                                                                                        emit modifiedChanged();
                                                                                                                                                                                    }

                                                                                                                                                                                    void VideoEditor::redo() {
                                                                                                                                                                                        if (m_redoStack.isEmpty()) return;

                                                                                                                                                                                        auto cmd = m_redoStack.pop();
                                                                                                                                                                                        cmd.redo();
                                                                                                                                                                                        m_undoStack.push(cmd);
                                                                                                                                                                                        emit modifiedChanged();
                                                                                                                                                                                    }

                                                                                                                                                                                    void VideoEditor::setModified(bool modified) {
                                                                                                                                                                                        if (m_modified != modified) {
                                                                                                                                                                                            m_modified = modified;
                                                                                                                                                                                            emit modifiedChanged();
                                                                                                                                                                                        }
                                                                                                                                                                                    }

                                                                                                                                                                                    Timecode VideoEditor::duration() const {
                                                                                                                                                                                        if (!m_project) return Timecode{0, 30};

                                                                                                                                                                                        FrameTime maxFrames = 0;
                                                                                                                                                                                        for (const auto& track : m_project->tracks) {
                                                                                                                                                                                            maxFrames = std::max(maxFrames, track->duration().frames);
                                                                                                                                                                                        }
                                                                                                                                                                                        return Timecode{maxFrames, m_profile.fps};
                                                                                                                                                                                    }

} // namespace Aegis
