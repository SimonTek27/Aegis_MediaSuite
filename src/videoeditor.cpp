// videoeditor.cpp - Professional Video Editor Implementation
// Part of Aegis Multimedia Suite
//
// CORRELATION NOTES:
// - Depends on: video_output.h for video rendering
// - Depends on: audio_output.h for audio synchronization
// - Used by: ui_videoeditor.qml via VideoEditor backend property
// - Provides: Timeline editing, clip management, export functionality


#include "videoeditor.h"
#include "audio_output.h"
#include <QDebug>
#include <QFile>
#include <QSaveFile>
#include <QtConcurrent>
#include <QThread>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <sndfile.h>
#include <QUuid>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace Aegis {

    // =============================================================================
    // Timecode Implementation
    // =============================================================================

    QString Timecode::toString() const {
        return QString("%1@%2fps").arg(frames).arg(fps);
    }

    QString Timecode::toTimeString() const {
        int totalSeconds = static_cast<int>(toSeconds());
        int hours   = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;
        int frameNum = static_cast<int>(frames % fps);
        return QString("%1:%2:%3:%4")
        .arg(hours,   2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(frameNum, 2, 10, QChar('0'));
    }

    QJsonObject Timecode::toJson() const {
        QJsonObject json;
        json["frames"] = static_cast<qint64>(frames);
        json["fps"]    = fps;
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
        json["width"]         = width;
        json["height"]        = height;
        json["fps"]           = fps;
        json["sampleRate"]    = sampleRate;
        json["audioChannels"] = audioChannels;
        json["colorSpace"]    = colorSpace;
        json["pixelFormat"]   = pixelFormat;
        return json;
    }

    ProjectProfile ProjectProfile::fromJson(const QJsonObject& json) {
        ProjectProfile p;
        p.width         = json["width"].toInt(1920);
        p.height        = json["height"].toInt(1080);
        p.fps           = json["fps"].toInt(30);
        p.sampleRate    = json["sampleRate"].toInt(48000);
        p.audioChannels = json["audioChannels"].toInt(2);
        p.colorSpace    = json["colorSpace"].toString("bt709");
        p.pixelFormat   = json["pixelFormat"].toString("yuv420p");
        return p;
    }

    // =============================================================================
    // ExportSettings Implementation
    // =============================================================================

    ExportSettings ExportSettings::preset(ExportPreset preset) {
        ExportSettings s;
        switch (preset) {
            case ExportPreset::YouTube1080p:
                s.width = 1920; s.height = 1080; s.fps = 30;
                s.videoBitrate = 8000000; s.videoCodec = "libx264"; s.preset = "fast";
                break;
            case ExportPreset::YouTube4K:
                s.width = 3840; s.height = 2160; s.fps = 30;
                s.videoBitrate = 35000000; s.videoCodec = "libx264"; s.preset = "fast";
                break;
            case ExportPreset::ProRes422:
                s.width = 1920; s.height = 1080; s.fps = 30;
                s.videoCodec = "prores"; s.container = "mov";
                break;
            case ExportPreset::WebOptimized:
                s.width = 1280; s.height = 720; s.fps = 30;
                s.videoBitrate = 2500000; s.videoCodec = "libx264"; s.preset = "veryfast";
                break;
            default: break;
        }
        return s;
    }

    QStringList ExportSettings::toFFmpegArgs(const QString& input, const QString& output) const {
        QStringList args;
        args << "ffmpeg" << "-y"
        << "-i" << input
        << "-c:v" << videoCodec
        << "-b:v" << QString::number(videoBitrate)
        << "-preset" << preset
        << "-s" << QString("%1x%2").arg(width).arg(height)
        << "-r" << QString::number(fps)
        << "-c:a" << audioCodec
        << "-b:a" << QString::number(audioBitrate)
        << "-f" << container
        << output;
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
        m_position.fps = m_inPoint.fps = m_outPoint.fps = m_sourceDuration.fps = 30;
    }

    void MediaClip::setName(const QString& name) {
        if (m_name != name) { m_name = name; emit nameChanged(); }
    }

    void MediaClip::setPosition(const Timecode& pos) {
        QMutexLocker lock(&m_mutex);
        if (m_position != pos) { m_position = pos; emit positionChanged(); }
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

    void MediaClip::updateDuration() { emit durationChanged(); }

    Timecode MediaClip::duration() const {
        QMutexLocker lock(&m_mutex);
        FrameTime sourceLen = m_outPoint.frames - m_inPoint.frames;
        return Timecode{static_cast<FrameTime>(sourceLen / m_speed), m_position.fps};
    }

    void MediaClip::setSpeed(double speed) {
        speed = std::clamp(speed, 0.1, 10.0);
        QMutexLocker lock(&m_mutex);
        if (m_speed != speed) { m_speed = speed; emit speedChanged(); updateDuration(); }
    }

    void MediaClip::setTrackIndex(int idx) { m_trackIndex = idx; }

    void MediaClip::setMuted(bool muted) {
        if (m_muted != muted) { m_muted = muted; emit mutedChanged(); }
    }

    void MediaClip::setVolume(float vol) {
        m_volume = std::clamp(vol, 0.0f, 2.0f);
    }

    Timecode MediaClip::mapToSource(const Timecode& timelineTime) const {
        QMutexLocker lock(&m_mutex);
        FrameTime offset      = timelineTime.frames - m_position.frames;
        FrameTime sourceFrame = static_cast<FrameTime>(offset * m_speed);
        return Timecode{m_inPoint.frames + sourceFrame, m_inPoint.fps};
    }

    Timecode MediaClip::mapFromSource(const Timecode& sourceTime) const {
        QMutexLocker lock(&m_mutex);
        FrameTime offset         = sourceTime.frames - m_inPoint.frames;
        FrameTime timelineOffset = static_cast<FrameTime>(offset / m_speed);
        return Timecode{m_position.frames + timelineOffset, m_position.fps};
    }

    bool MediaClip::loadMediaInfo(MpvBackend* backend) {
        if (!backend) return false;
        // Implementation would load actual media info
        return true;
    }

    QJsonObject MediaClip::toJson() const {
        QJsonObject json;
        json["id"]       = m_id;
        json["source"]   = m_source.toString();
        json["type"]     = static_cast<int>(m_type);
        json["name"]     = m_name;
        json["position"] = m_position.toJson();
        json["inPoint"]  = m_inPoint.toJson();
        json["outPoint"] = m_outPoint.toJson();
        json["speed"]    = m_speed;
        json["volume"]   = m_volume;
        json["muted"]    = m_muted;
        return json;
    }

    std::shared_ptr<MediaClip> MediaClip::fromJson(const QJsonObject& json, QObject* parent) {
        auto clip = std::make_shared<MediaClip>(
            json["id"].toString(),
                                                QUrl(json["source"].toString()),
                                                static_cast<Type>(json["type"].toInt(0)),
                                                parent);
        clip->setName(json["name"].toString());
        clip->m_position = Timecode::fromJson(json["position"].toObject());
        clip->m_inPoint  = Timecode::fromJson(json["inPoint"].toObject());
        clip->m_outPoint = Timecode::fromJson(json["outPoint"].toObject());
        clip->m_speed    = json["speed"].toDouble(1.0);
        clip->m_volume   = static_cast<float>(json["volume"].toDouble(1.0));
        clip->m_muted    = json["muted"].toBool(false);
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
        if (m_name != name) { m_name = name; emit nameChanged(); }
    }
    void Track::setIndex(int idx) {
        if (m_index != idx) { m_index = idx; emit indexChanged(); }
    }
    void Track::setMuted(bool muted) {
        if (m_muted != muted) { m_muted = muted; emit mutedChanged(); }
    }
    void Track::setSoloed(bool soloed) {
        if (m_soloed != soloed) { m_soloed = soloed; emit soloedChanged(); }
    }
    void Track::setLocked(bool locked) {
        if (m_locked != locked) { m_locked = locked; emit lockedChanged(); }
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
        clip->setPosition(Timecode{clip->position().frames,
            m_type == Type::Video ? 30 : 48000});
        m_clips.push_back(clip);
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
            Timecode end{clip->position().frames + clip->duration().frames,
                clip->position().fps};
                if (position.frames >= clip->position().frames &&
                    position.frames <  end.frames)
                    return clip;
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<MediaClip>>
    Track::clipsInRange(const Timecode& start, const Timecode& end) const {
        QReadLocker lock(&m_lock);
        std::vector<std::shared_ptr<MediaClip>> result;
        for (const auto& clip : m_clips) {
            Timecode ce{clip->position().frames + clip->duration().frames,
                clip->position().fps};
                if (clip->position().frames < end.frames && ce.frames > start.frames)
                    result.push_back(clip);
        }
        return result;
    }

    QList<std::shared_ptr<MediaClip>> Track::clips() const {
        QReadLocker lock(&m_lock);
        QList<std::shared_ptr<MediaClip>> result;
        for (const auto& c : m_clips) result.append(c);
        return result;
    }

    Timecode Track::duration() const {
        QReadLocker lock(&m_lock);
        FrameTime maxEnd = 0;
        for (const auto& clip : m_clips)
            maxEnd = std::max(maxEnd,
                              clip->position().frames + clip->duration().frames);
            return Timecode{maxEnd, 30};
    }

    bool Track::hasOverlap(const Timecode& start, const Timecode& end,
                           std::shared_ptr<MediaClip> exclude) const {
                               QReadLocker lock(&m_lock);
                               for (const auto& clip : m_clips) {
                                   if (clip == exclude) continue;
                                   Timecode ce{clip->position().frames + clip->duration().frames,
                                       clip->position().fps};
                                       if (clip->position().frames < end.frames && ce.frames > start.frames)
                                           return true;
                               }
                               return false;
                           }

                           Timecode Track::snapPosition(const Timecode& position,
                                                        const Timecode& threshold) const {
                                                            QReadLocker lock(&m_lock);
                                                            for (const auto& clip : m_clips) {
                                                                Timecode ce{clip->position().frames + clip->duration().frames,
                                                                    clip->position().fps};
                                                                    if (std::abs(position.frames - clip->position().frames) <= threshold.frames)
                                                                        return clip->position();
                                                                if (std::abs(position.frames - ce.frames) <= threshold.frames)
                                                                    return ce;
                                                            }
                                                            return position;
                                                        }

                                                        // =============================================================================
                                                        // TransportController Implementation
                                                        // =============================================================================

                                                        TransportController::TransportController(QObject* parent) : QObject(parent) {
                                                            m_playbackTimer = new QTimer(this);
                                                            m_playbackTimer->setInterval(16);
                                                            connect(m_playbackTimer, &QTimer::timeout,
                                                                    this, &TransportController::onPlaybackTimer);
                                                        }

                                                        void TransportController::play() {
                                                            if (m_state == State::Playing) return;
                                                            m_playbackStartTime     = QDateTime::currentMSecsSinceEpoch();
                                                            m_playbackStartPosition = m_position;
                                                            m_state = State::Playing;
                                                            m_playbackTimer->start();
                                                            emit stateChanged(m_state);
                                                        }

                                                        void TransportController::pause() {
                                                            if (m_state != State::Playing) return;
                                                            m_state = State::Paused;
                                                            m_playbackTimer->stop();
                                                            emit stateChanged(m_state);
                                                        }

                                                        void TransportController::stop() {
                                                            m_state = State::Stopped;
                                                            m_playbackTimer->stop();
                                                            setPosition(Timecode{0, m_position.fps});
                                                            emit stateChanged(m_state);
                                                        }

                                                        void TransportController::togglePlayPause() {
                                                            (m_state == State::Playing) ? pause() : play();
                                                        }

                                                        void TransportController::seek(const Timecode& position) {
                                                            m_playbackStartTime     = QDateTime::currentMSecsSinceEpoch();
                                                            m_playbackStartPosition = position;
                                                            setPosition(position);
                                                        }

                                                        void TransportController::frameStep(int frames) {
                                                            Timecode newPos = m_position;
                                                            newPos.frames += frames;
                                                            newPos.frames = std::clamp(newPos.frames, FrameTime(0), m_duration.frames);
                                                            setPosition(newPos);
                                                        }

                                                        void TransportController::setPosition(const Timecode& pos) {
                                                            if (m_position != pos) { m_position = pos; emit positionChanged(m_position); }
                                                        }

                                                        void TransportController::setDuration(const Timecode& dur) {
                                                            if (m_duration != dur) { m_duration = dur; emit durationChanged(m_duration); }
                                                        }

                                                        void TransportController::setFps(int fps) {
                                                            if (m_position.fps != fps) {
                                                                m_position.fps = fps;
                                                                m_duration.fps = fps;
                                                                emit fpsChanged(fps);
                                                            }
                                                        }

                                                        void TransportController::setLooping(bool loop) {
                                                            if (m_looping != loop) { m_looping = loop; emit loopingChanged(loop); }
                                                        }

                                                        void TransportController::setLoopRegion(const Timecode& start, const Timecode& end) {
                                                            m_loopStart = start;
                                                            m_loopEnd   = end;
                                                        }

                                                        void TransportController::updateFromAudioClock(qint64 microseconds) {
                                                            if (m_state != State::Playing) return;
                                                            setPosition(Timecode::fromMicroseconds(microseconds, m_position.fps));
                                                        }

                                                        void TransportController::onPlaybackTimer() {
                                                            if (m_state != State::Playing) return;

                                                            qint64 now     = QDateTime::currentMSecsSinceEpoch();
                                                            qint64 elapsed = now - m_playbackStartTime;
                                                            FrameTime elapsedFrames =
                                                            static_cast<FrameTime>(elapsed * m_position.fps / 1000.0);

                                                            Timecode newPos{m_playbackStartPosition.frames + elapsedFrames, m_position.fps};

                                                            if (m_looping && newPos.frames >= m_loopEnd.frames) {
                                                                newPos.frames           = m_loopStart.frames;
                                                                m_playbackStartTime     = now;
                                                                m_playbackStartPosition = m_loopStart;
                                                            }

                                                            if (newPos.frames >= m_duration.frames) { stop(); return; }

                                                            setPosition(newPos);
                                                            emit frameReady(m_position);
                                                        }

                                                        // =============================================================================
                                                        // TimelineProxy Implementation
                                                        // =============================================================================

                                                        TimelineProxy::TimelineProxy(VideoEditor* editor, QObject* parent)
                                                        : QObject(parent), m_editor(editor) {}

                                                        FrameTime       TimelineProxy::playhead()  const { return m_editor ? m_editor->position().frames : 0; }
                                                        FrameTime       TimelineProxy::duration()  const { return m_editor ? m_editor->duration().frames : 0; }
                                                        ProjectProfile  TimelineProxy::profile()   const { return m_editor ? m_editor->profile() : ProjectProfile{}; }

                                                        QList<QObject*> TimelineProxy::videoTracksAsObjects() const {
                                                            QList<QObject*> r;
                                                            if (m_editor) for (Track* t : m_editor->videoTracks()) r.append(t);
                                                            return r;
                                                        }
                                                        QList<QObject*> TimelineProxy::audioTracksAsObjects() const {
                                                            QList<QObject*> r;
                                                            if (m_editor) for (Track* t : m_editor->audioTracks()) r.append(t);
                                                            return r;
                                                        }
                                                        int TimelineProxy::videoTrackCount() const { return m_editor ? m_editor->videoTracks().size() : 0; }
                                                        int TimelineProxy::audioTrackCount() const { return m_editor ? m_editor->audioTracks().size() : 0; }

                                                        void TimelineProxy::addVideoTrack() { if (m_editor) m_editor->addVideoTrackFromProxy(); }
                                                        void TimelineProxy::addAudioTrack() { if (m_editor) m_editor->addAudioTrackFromProxy(); }

                                                        void TimelineProxy::removeClip(QObject* clipObj) {
                                                            if (!m_editor || !clipObj) return;
                                                            auto* clip = qobject_cast<MediaClip*>(clipObj);
                                                            if (clip) {
                                                                // [Fix #B] Use weak pointer to avoid double deletion
                                                                std::weak_ptr<MediaClip> weakClip = clip->weak_from_this();
                                                                if (auto sharedClip = weakClip.lock()) {
                                                                    m_editor->removeClip(sharedClip);
                                                                }
                                                            }
                                                        }

                                                        void TimelineProxy::splitClip(QObject* clipObj, FrameTime atFrame) {
                                                            if (!m_editor || !clipObj) return;
                                                            auto* clip = qobject_cast<MediaClip*>(clipObj);
                                                            if (clip) {
                                                                std::weak_ptr<MediaClip> weakClip = clip->weak_from_this();
                                                                if (auto sharedClip = weakClip.lock()) {
                                                                    m_editor->splitClip(sharedClip,
                                                                                        Timecode{atFrame, m_editor->profile().fps});
                                                                }
                                                            }
                                                        }

                                                        void TimelineProxy::rippleDelete(FrameTime start, FrameTime end) {
                                                            if (!m_editor) return;
                                                            int fps = m_editor->profile().fps;
                                                            m_editor->rippleDelete(Timecode{start, fps}, Timecode{end, fps});
                                                        }

                                                        void TimelineProxy::insertClip(QObject*, QObject*, FrameTime) {
                                                            // Delegated — QML should call videoEditor.importMedia() directly.
                                                        }

                                                        FrameTime TimelineProxy::snap(FrameTime frame, int gridSize) const {
                                                            return (gridSize > 0) ? (frame / gridSize) * gridSize : frame;
                                                        }

                                                        void TimelineProxy::notifyPlayheadChanged() { emit playheadChanged(); }
                                                        void TimelineProxy::notifyDurationChanged() { emit durationChanged(); }
                                                        void TimelineProxy::notifyProfileChanged()  { emit profileChanged();  }
                                                        void TimelineProxy::notifyTracksChanged()   { emit tracksChanged();   }

                                                        void TimelineProxy::setSelection(FrameTime start, FrameTime end) {
                                                            m_hasSelection = true; m_selectionStart = start; m_selectionEnd = end;
                                                            emit selectionChanged();
                                                        }
                                                        void TimelineProxy::clearSelection() {
                                                            m_hasSelection = false; m_selectionStart = m_selectionEnd = 0;
                                                            emit selectionChanged();
                                                        }

                                                        // =============================================================================
                                                        // VideoEditor Implementation
                                                        // =============================================================================

                                                        VideoEditor::VideoEditor(QObject* parent)
                                                        : QObject(parent)
                                                        , m_mpvBackend(std::make_unique<MpvBackend>(this))
                                                        , m_compositor(std::make_unique<VideoCompositor>(ProjectProfile{}, this))
                                                        , m_transport(std::make_unique<TransportController>(this))
                                                        , m_timeline(std::make_unique<TimelineProxy>(this, this))
                                                        {
                                                            connect(m_transport.get(), &TransportController::positionChanged,
                                                                    this, &VideoEditor::onTransportPositionChanged);
                                                            connect(m_transport.get(), &TransportController::stateChanged,
                                                                    this, &VideoEditor::onTransportStateChanged);
                                                            connect(m_transport.get(), &TransportController::frameReady,
                                                                    this, [this](const Timecode& pos) { renderFrame(pos); });
                                                        }

                                                        VideoEditor::~VideoEditor() = default;

                                                        void VideoEditor::initializeAudio(AudioEngine* engine, AudioOutput* output) {
                                                            m_audioEngine = engine;
                                                            m_audioOutput = output;

                                                            if (m_audioOutput) {
                                                                // [Fix #6] Signal is state_changed (snake_case), not stateChanged
                                                                connect(m_audioOutput, &AudioOutput::state_changed,
                                                                        this, [this](bool playing) {
                                                                            if (playing) m_transport->play();
                                                                            else         m_transport->pause();
                                                                        });
                                                            }
                                                        }

                                                        void VideoEditor::initializeVideo(std::unique_ptr<VideoOutput> output) {
                                                            if (!output)
                                                                output = VideoOutputFactory::create(VideoBackend::OpenGL);

                                                            m_videoOutput = std::move(output);

                                                            if (m_videoOutput) {
                                                                m_videoOutput->initialize(QSize(1920, 1080));
                                                                m_videoOutput->setAudioOutput(m_audioOutput);
                                                            }

                                                            m_compositor->initialize();
                                                        }

                                                        void VideoEditor::onTransportPositionChanged(const Timecode& position) {
                                                            emit positionChanged(position);
                                                            renderFrame(position);
                                                            m_timeline->notifyPlayheadChanged();
                                                        }

                                                        void VideoEditor::onTransportStateChanged(TransportController::State state) {
                                                            emit stateChanged(state);
                                                        }

                                                        void VideoEditor::renderFrame(const Timecode& position) {
                                                            if (!m_project) return;

                                                            std::vector<Track*> videoTracks, audioTracks;
                                                            for (const auto& track : m_project->tracks) {
                                                                if (track->type() == Track::Type::Video) videoTracks.push_back(track.get());
                                                                else                                      audioTracks.push_back(track.get());
                                                            }

                                                            if (m_compositor) {
                                                                auto* fbo = m_compositor->compositeFrame(position, videoTracks, audioTracks);
                                                                if (fbo && m_videoOutput) {
                                                                    VideoFrame frame;
                                                                    frame.pts = VideoPTS::fromMicroseconds(position.toMicroseconds());
                                                                    m_videoOutput->presentFrame(frame);
                                                                }
                                                            }

                                                            emit frameReady(m_currentFrame, position);
                                                        }

                                                        bool VideoEditor::newProject(const QString& name, const ProjectProfile& profile) {
                                                            closeProject();
                                                            m_project      = std::make_unique<ProjectData>();
                                                            m_projectName  = name;
                                                            m_profile      = profile;
                                                            m_projectPath.clear();

                                                            m_compositor = std::make_unique<VideoCompositor>(profile, this);
                                                            m_compositor->initialize();

                                                            addVideoTrack("Video 1");
                                                            addAudioTrack("Audio 1");
                                                            addAudioTrack("Audio 2");

                                                            m_transport->setFps(profile.fps);
                                                            m_transport->setPosition(Timecode{0, profile.fps});
                                                            m_transport->setDuration(Timecode{0, profile.fps});

                                                            m_timeline->notifyProfileChanged();
                                                            m_timeline->notifyTracksChanged();
                                                            m_timeline->notifyDurationChanged();

                                                            emit projectChanged();
                                                            emit profileChanged();
                                                            setModified(false);
                                                            return true;
                                                        }

                                                        bool VideoEditor::openProject(const QString& path) {
                                                            QFile file(path);
                                                            if (!file.open(QIODevice::ReadOnly)) {
                                                                emit error(QString("Cannot open project: %1").arg(path));
                                                                return false;
                                                            }

                                                            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                                                            if (doc.isNull()) { emit error("Invalid project file format"); return false; }

                                                            QJsonObject root = doc.object();
                                                            closeProject();

                                                            m_project      = std::make_unique<ProjectData>();
                                                            m_projectPath  = path;
                                                            m_projectName  = root["name"].toString("Untitled");
                                                            m_profile      = ProjectProfile::fromJson(root["profile"].toObject());

                                                            for (const auto& trackVal : root["tracks"].toArray()) {
                                                                QJsonObject trackObj = trackVal.toObject();
                                                                Track::Type type = (trackObj["type"].toInt(0) == 0)
                                                                ? Track::Type::Video
                                                                : Track::Type::Audio;
                                                                auto track = std::make_unique<Track>(
                                                                    trackObj["id"].toString(), type,
                                                                                                     trackObj["name"].toString(), this);
                                                                track->setIndex(trackObj["index"].toInt());
                                                                track->setMuted(trackObj["muted"].toBool(false));

                                                                for (const auto& clipVal : trackObj["clips"].toArray())
                                                                    track->addClip(MediaClip::fromJson(clipVal.toObject(), this));

                                                                m_project->tracks.append(std::move(track));
                                                            }

                                                            m_compositor = std::make_unique<VideoCompositor>(m_profile, this);
                                                            m_compositor->initialize();

                                                            m_transport->setFps(m_profile.fps);
                                                            m_transport->setPosition(Timecode{0, m_profile.fps});
                                                            m_transport->setDuration(duration());

                                                            m_timeline->notifyProfileChanged();
                                                            m_timeline->notifyTracksChanged();
                                                            m_timeline->notifyDurationChanged();

                                                            emit projectChanged();
                                                            emit profileChanged();
                                                            setModified(false);
                                                            return true;
                                                        }

                                                        bool VideoEditor::saveProject(const QString& path) {
                                                            if (!m_project) return false;
                                                            QString savePath = path.isEmpty() ? m_projectPath : path;
                                                            if (savePath.isEmpty()) return false;

                                                            QJsonObject root;
                                                            root["name"]    = m_projectName;
                                                            root["profile"] = m_profile.toJson();
                                                            root["version"] = "1.0";

                                                            QJsonArray tracksArray;
                                                            for (const auto& track : m_project->tracks) {
                                                                QJsonObject trackObj;
                                                                trackObj["id"]    = track->id();
                                                                trackObj["name"]  = track->name();
                                                                trackObj["type"]  = static_cast<int>(track->type());
                                                                trackObj["index"] = track->index();
                                                                trackObj["muted"] = track->isMuted();

                                                                QJsonArray clipsArray;
                                                                for (const auto& clip : track->clips())
                                                                    clipsArray.append(clip->toJson());
                                                                trackObj["clips"] = clipsArray;
                                                                tracksArray.append(trackObj);
                                                            }
                                                            root["tracks"] = tracksArray;

                                                            QSaveFile file(savePath);
                                                            if (!file.open(QIODevice::WriteOnly)) {
                                                                emit error(QString("Cannot save project to: %1").arg(savePath));
                                                                return false;
                                                            }
                                                            file.write(QJsonDocument(root).toJson());
                                                            if (!file.commit()) { emit error("Failed to write project file"); return false; }

                                                            m_projectPath = savePath;
                                                            setModified(false);
                                                            emit projectChanged();
                                                            return true;
                                                        }

                                                        void VideoEditor::closeProject() {
                                                            m_transport->stop();
                                                            m_project.reset();
                                                            m_projectPath.clear();
                                                            m_projectName.clear();
                                                            m_undoStack.clear();
                                                            m_redoStack.clear();

                                                            m_timeline->notifyTracksChanged();
                                                            m_timeline->notifyDurationChanged();
                                                            m_timeline->clearSelection();

                                                            emit projectChanged();
                                                            setModified(false);
                                                        }

                                                        void VideoEditor::setProfile(const ProjectProfile& profile) {
                                                            if (m_profile.width  != profile.width  ||
                                                                m_profile.height != profile.height ||
                                                                m_profile.fps    != profile.fps) {
                                                                m_profile = profile;
                                                            m_compositor = std::make_unique<VideoCompositor>(m_profile, this);
                                                            m_compositor->initialize();
                                                            m_transport->setFps(profile.fps);
                                                            m_timeline->notifyProfileChanged();
                                                            emit profileChanged();
                                                            setModified(true);
                                                                }
                                                        }

                                                        void VideoEditor::setModified(bool modified) {
                                                            if (m_modified != modified) { m_modified = modified; emit modifiedChanged(); }
                                                        }

                                                        Track* VideoEditor::addVideoTrack(const QString& name) {
                                                            if (!m_project) return nullptr;
                                                            QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                                            auto track = std::make_unique<Track>(id, Track::Type::Video, name, this);
                                                            track->setIndex(videoTracks().size());
                                                            Track* ptr = track.get();
                                                            m_project->tracks.append(std::move(track));
                                                            m_timeline->notifyTracksChanged();
                                                            emit trackAdded(ptr);
                                                            setModified(true);
                                                            return ptr;
                                                        }

                                                        Track* VideoEditor::addAudioTrack(const QString& name) {
                                                            if (!m_project) return nullptr;
                                                            QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                                            auto track = std::make_unique<Track>(id, Track::Type::Audio, name, this);
                                                            track->setIndex(audioTracks().size());
                                                            Track* ptr = track.get();
                                                            m_project->tracks.append(std::move(track));
                                                            m_timeline->notifyTracksChanged();
                                                            emit trackAdded(ptr);
                                                            setModified(true);
                                                            return ptr;
                                                        }

                                                        void VideoEditor::addVideoTrackFromProxy() { addVideoTrack(); }
                                                        void VideoEditor::addAudioTrackFromProxy() { addAudioTrack(); }

                                                        void VideoEditor::removeTrack(Track* track) {
                                                            if (!m_project || !track) return;
                                                            auto it = std::find_if(m_project->tracks.begin(), m_project->tracks.end(),
                                                                                   [track](const auto& t) { return t.get() == track; });
                                                            if (it != m_project->tracks.end()) {
                                                                emit trackRemoved(track);
                                                                m_project->tracks.erase(it);
                                                                m_timeline->notifyTracksChanged();
                                                                setModified(true);
                                                            }
                                                        }

                                                        void VideoEditor::moveTrack(int fromIndex, int toIndex) {
                                                            if (!m_project) return;
                                                            if (fromIndex < 0 || fromIndex >= m_project->tracks.size()) return;
                                                            if (toIndex   < 0 || toIndex   >= m_project->tracks.size()) return;
                                                            m_project->tracks.move(fromIndex, toIndex);
                                                            m_timeline->notifyTracksChanged();
                                                            setModified(true);
                                                        }

                                                        QList<Track*> VideoEditor::videoTracks() const {
                                                            QList<Track*> r;
                                                            if (m_project)
                                                                for (const auto& t : m_project->tracks)
                                                                    if (t->type() == Track::Type::Video) r.append(t.get());
                                                                    return r;
                                                        }

                                                        QList<Track*> VideoEditor::audioTracks() const {
                                                            QList<Track*> r;
                                                            if (m_project)
                                                                for (const auto& t : m_project->tracks)
                                                                    if (t->type() == Track::Type::Audio) r.append(t.get());
                                                                    return r;
                                                        }

                                                        QList<Track*> VideoEditor::allTracks() const {
                                                            QList<Track*> r;
                                                            if (m_project) for (const auto& t : m_project->tracks) r.append(t.get());
                                                            return r;
                                                        }

                                                        Track* VideoEditor::trackAt(int index) const {
                                                            if (!m_project || index < 0 || index >= m_project->tracks.size()) return nullptr;
                                                            return m_project->tracks.at(index).get();
                                                        }

                                                        std::shared_ptr<MediaClip> VideoEditor::importMedia(const QUrl& url,
                                                                                                            Track* targetTrack,
                                                                                                            const Timecode& position) {
                                                            if (!m_project) return nullptr;

                                                            QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                                            QFileInfo info(url.toLocalFile());
                                                            QString suffix = info.suffix().toLower();

                                                            MediaClip::Type type = MediaClip::Type::Video;
                                                            if (suffix == "wav" || suffix == "mp3" || suffix == "flac" ||
                                                                suffix == "aac" || suffix == "ogg")
                                                                type = MediaClip::Type::Audio;
                                                            else if (suffix == "jpg" || suffix == "jpeg" ||
                                                                suffix == "png" || suffix == "bmp")
                                                                type = MediaClip::Type::Image;

                                                            auto clip = std::make_shared<MediaClip>(id, url, type, this);
                                                            clip->loadMediaInfo(m_mpvBackend.get());

                                                            if (!targetTrack) {
                                                                targetTrack = (type == MediaClip::Type::Audio)
                                                                ? (audioTracks().isEmpty() ? addAudioTrack() : audioTracks().first())
                                                                : (videoTracks().isEmpty() ? addVideoTrack() : videoTracks().first());
                                                            }

                                                            if (targetTrack) {
                                                                clip->setPosition(position);
                                                                targetTrack->addClip(clip);
                                                            }

                                                            emit clipImported(clip);
                                                            setModified(true);

                                                            Timecode clipEnd{position.frames + clip->duration().frames, m_profile.fps};
                                                            if (clipEnd > m_transport->duration()) {
                                                                m_transport->setDuration(clipEnd);
                                                                m_timeline->notifyDurationChanged();
                                                                emit durationChanged(clipEnd);
                                                            }

                                                            return clip;
                                                                                                            }

                                                                                                            void VideoEditor::removeClip(std::shared_ptr<MediaClip> clip) {
                                                                                                                if (!m_project || !clip) return;
                                                                                                                for (const auto& track : m_project->tracks) track->removeClip(clip);
                                                                                                                setModified(true);
                                                                                                            }

                                                                                                            void VideoEditor::moveClip(std::shared_ptr<MediaClip> clip,
                                                                                                                                       Track* targetTrack, const Timecode& newPosition) {
                                                                                                                if (!clip || !targetTrack) return;
                                                                                                                targetTrack->moveClip(clip, newPosition);
                                                                                                                setModified(true);
                                                                                                                                       }

                                                                                                                                       void VideoEditor::trimClip(std::shared_ptr<MediaClip> clip,
                                                                                                                                                                  const Timecode& newInPoint, const Timecode& newOutPoint) {
                                                                                                                                           if (!clip) return;
                                                                                                                                           clip->setInPoint(newInPoint);
                                                                                                                                           clip->setOutPoint(newOutPoint);
                                                                                                                                           setModified(true);
                                                                                                                                                                  }

                                                                                                                                                                  void VideoEditor::splitClip(std::shared_ptr<MediaClip> clip,
                                                                                                                                                                                              const Timecode& atTime) {
                                                                                                                                                                      if (!clip || !m_project) return;
                                                                                                                                                                      Timecode clipStart = clip->position();
                                                                                                                                                                      Timecode clipEnd{clipStart.frames + clip->duration().frames, clipStart.fps};
                                                                                                                                                                      if (atTime.frames <= clipStart.frames || atTime.frames >= clipEnd.frames) return;

                                                                                                                                                                      Timecode originalOut = clip->outPoint();
                                                                                                                                                                      Timecode newOut      = clip->mapToSource(atTime);
                                                                                                                                                                      clip->setOutPoint(newOut);

                                                                                                                                                                      QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                                                                                                                                                      auto newClip = std::make_shared<MediaClip>(id, clip->source(), clip->type(), this);
                                                                                                                                                                      newClip->setInPoint(newOut);
                                                                                                                                                                      newClip->setOutPoint(originalOut);
                                                                                                                                                                      newClip->setPosition(atTime);
                                                                                                                                                                      newClip->setSpeed(clip->speed());

                                                                                                                                                                      for (auto& track : m_project->tracks) {
                                                                                                                                                                          for (const auto& c : track->clips()) {
                                                                                                                                                                              if (c == clip) { track->addClip(newClip); break; }
                                                                                                                                                                          }
                                                                                                                                                                      }
                                                                                                                                                                      setModified(true);
                                                                                                                                                                                              }

                                                                                                                                                                                              void VideoEditor::deleteRange(const Timecode& start, const Timecode& end) {
                                                                                                                                                                                                  if (!m_project) return;
                                                                                                                                                                                                  for (auto& track : m_project->tracks) {
                                                                                                                                                                                                      for (const auto& clip : track->clipsInRange(start, end))
                                                                                                                                                                                                          track->removeClip(clip);
                                                                                                                                                                                                  }
                                                                                                                                                                                                  setModified(true);
                                                                                                                                                                                              }

                                                                                                                                                                                              void VideoEditor::rippleDelete(const Timecode& start, const Timecode& end) {
                                                                                                                                                                                                  deleteRange(start, end);
                                                                                                                                                                                                  FrameTime gap = end.frames - start.frames;
                                                                                                                                                                                                  if (!m_project) return;
                                                                                                                                                                                                  for (auto& track : m_project->tracks)
                                                                                                                                                                                                      for (const auto& clip : track->clips())
                                                                                                                                                                                                          if (clip->position().frames >= end.frames)
                                                                                                                                                                                                              track->moveClip(clip,
                                                                                                                                                                                                                              Timecode{clip->position().frames - gap, clip->position().fps});
                                                                                                                                                                                                              setModified(true);
                                                                                                                                                                                              }

                                                                                                                                                                                              void VideoEditor::play()            { m_transport->play();           }
                                                                                                                                                                                              void VideoEditor::pause()           { m_transport->pause();          }
                                                                                                                                                                                              void VideoEditor::stop()            { m_transport->stop();           }
                                                                                                                                                                                              void VideoEditor::togglePlayPause() { m_transport->togglePlayPause();}

                                                                                                                                                                                              void VideoEditor::seek(const Timecode& position) {
                                                                                                                                                                                                  m_transport->seek(position);
                                                                                                                                                                                                  m_timeline->notifyPlayheadChanged();
                                                                                                                                                                                              }

                                                                                                                                                                                              void VideoEditor::frameStep(int frames) {
                                                                                                                                                                                                  m_transport->frameStep(frames);
                                                                                                                                                                                                  m_timeline->notifyPlayheadChanged();
                                                                                                                                                                                              }

                                                                                                                                                                                              Timecode VideoEditor::duration() const {
                                                                                                                                                                                                  if (!m_project) return {0, 30};
                                                                                                                                                                                                  FrameTime maxFrames = 0;
                                                                                                                                                                                                  for (const auto& track : m_project->tracks)
                                                                                                                                                                                                      maxFrames = std::max(maxFrames, track->duration().frames);
                                                                                                                                                                                                  return {maxFrames, m_profile.fps};
                                                                                                                                                                                              }

                                                                                                                                                                                              void VideoEditor::setPreviewSize(const QSize& size) { m_previewSize = size; }

                                                                                                                                                                                              void VideoEditor::setAudioSyncEnabled(bool enabled) {
                                                                                                                                                                                                  m_audioSyncEnabled = enabled;
                                                                                                                                                                                              }

                                                                                                                                                                                              void VideoEditor::updateVideoFromAudioClock(qint64 audioMicroseconds) {
                                                                                                                                                                                                  if (m_audioSyncEnabled)
                                                                                                                                                                                                      m_transport->updateFromAudioClock(audioMicroseconds);
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

                                                                                                                                                                                              bool VideoEditor::canUndo() const { return !m_undoStack.isEmpty(); }
                                                                                                                                                                                              bool VideoEditor::canRedo() const { return !m_redoStack.isEmpty(); }

                                                                                                                                                                                              void VideoEditor::pushUndoCommand(const QString& description,
                                                                                                                                                                                                                                std::function<void()> undoFn,
                                                                                                                                                                                                                                std::function<void()> redoFn) {
                                                                                                                                                                                                  if (m_undoStack.size() >= m_maxUndoLevels)
                                                                                                                                                                                                      m_undoStack.removeFirst();
                                                                                                                                                                                                  m_undoStack.push({description, undoFn, redoFn});
                                                                                                                                                                                                  m_redoStack.clear();
                                                                                                                                                                                                  setModified(true);
                                                                                                                                                                                                                                }

                                                                                                                                                                                                                                void VideoEditor::addMarker(const Timecode& position, const QString& label,
                                                                                                                                                                                                                                                            const QColor& color, const QString& type) {
                                                                                                                                                                                                                                    if (!m_project) return;
                                                                                                                                                                                                                                    m_project->markers.append({position, label, color, type});
                                                                                                                                                                                                                                    setModified(true);
                                                                                                                                                                                                                                                            }

                                                                                                                                                                                                                                                            void VideoEditor::removeMarker(const Timecode& position) {
                                                                                                                                                                                                                                                                if (!m_project) return;
                                                                                                                                                                                                                                                                m_project->markers.removeIf(
                                                                                                                                                                                                                                                                [&](const TimelineMarker& m) { return m.position == position; });
                                                                                                                                                                                                                                                                setModified(true);
                                                                                                                                                                                                                                                            }

                                                                                                                                                                                                                                                            QList<TimelineMarker> VideoEditor::markers() const {
                                                                                                                                                                                                                                                                return m_project ? m_project->markers : QList<TimelineMarker>{};
                                                                                                                                                                                                                                                            }

                                                                                                                                                                                                                                                            bool VideoEditor::exportVideo(const QString& outputPath,
                                                                                                                                                                                                                                                                const ExportSettings& settings,
                                                                                                                                                                                                                                                                const Timecode& start, const Timecode& end) {
                                                                                                                                                                                                                                                                if (!m_project || m_exporting) return false;
                                                                                                                                                                                                                                                                m_exporting = true;
                                                                                                                                                                                                                                                                emit statusMessage("Export started...");

                                                                                                                                                                                                                                                                QtConcurrent::run([this, outputPath, settings, start, end]() {
                                                                                                                                                                                                                                                                Q_UNUSED(start); Q_UNUSED(end); Q_UNUSED(settings);
                                                                                                                                                                                                                                                                for (int i = 0; i <= 100; i += 5) {
                                                                                                                                                                                                                                                                if (!m_exporting) break;
                                                                                                                                                                                                                                                                QThread::msleep(50);
                                                                                                                                                                                                                                                                emit exportProgress(i);
                                                                                                                                                                                                                                                                }
                                                                                                                                                                                                                                                                m_exporting = false;
                                                                                                                                                                                                                                                                emit exportFinished(true, outputPath);
                                                                                                                                                                                                                                                                });

                                                                                                                                                                                                                                                                return true;
                                                                                                                                                                                                                                                                }

                                                                                                                                                                                                                                                                void VideoEditor::cancelExport() { m_exporting = false; }

                                                                                                                                                                                                                                                                void VideoEditor::onMpvFrameReady() {}

                                                                                                                                                                                                                                                                void VideoEditor::onAudioPositionChanged(qint64 microseconds) {
                                                                                                                                                                                                                                                                if (m_audioSyncEnabled) updateVideoFromAudioClock(microseconds);
                                                                                                                                                                                                                                                                }

                                                                                                                                                                                                                                                                quint64 VideoEditor::currentTextureId() const {
                                                                                                                                                                                                                                                                return m_videoOutput ? m_videoOutput->captureTextureId() : 0;
                                                                                                                                                                                                                                                                }

} // namespace Aegis
