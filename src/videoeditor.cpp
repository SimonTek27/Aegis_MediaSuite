// Temporarily disabled VideoEditor implementation to allow aegis_core to build.
// TODO: Re-port videoeditor.cpp to the new DAW / Track / Clip APIs before re-enabling.

#include "videoeditor.h"
#include <QTimer>
#include <QUuid>
#include <cmath>

namespace Aegis {

    // =========================================================================
    // VideoCompositor
    // =========================================================================

    VideoCompositor::VideoCompositor(const ProjectProfile& profile, QObject* parent)
        : QObject(parent), m_profile(profile) {}

    VideoCompositor::~VideoCompositor() = default;

    // =========================================================================
    // MediaClip
    // =========================================================================

    MediaClip::MediaClip(const QString& id, const QUrl& source, Type type, QObject* parent)
        : QObject(parent), m_id(id), m_source(source), m_type(type) {}

    Timecode MediaClip::duration() const {
        Timecode d = m_outPoint;
        d.frames -= m_inPoint.frames;
        return d;
    }
    void MediaClip::setName(const QString& name) { m_name = name; emit nameChanged(); }
    void MediaClip::setPosition(const Timecode& pos) { m_position = pos; emit positionChanged(); }
    void MediaClip::setInPoint(const Timecode& pt) { m_inPoint = pt; emit inPointChanged(); }
    void MediaClip::setOutPoint(const Timecode& pt) { m_outPoint = pt; emit outPointChanged(); }
    void MediaClip::setSpeed(double speed) { m_speed = speed; emit speedChanged(); }
    void MediaClip::setMuted(bool muted) { m_muted = muted; emit mutedChanged(); }
    void MediaClip::setVolume(float vol) { m_volume = vol; }
    void MediaClip::setTrackIndex(int idx) { m_trackIndex = idx; }
    void MediaClip::updateDuration() { emit durationChanged(); }
    Timecode MediaClip::mapToSource(const Timecode& t) const { return t; }
    Timecode MediaClip::mapFromSource(const Timecode& t) const { return t; }
    QImage MediaClip::thumbnail(const Timecode&, const QSize&) { return {}; }
    bool MediaClip::loadMediaInfo(MpvBackend*) { return false; }
    QJsonObject MediaClip::toJson() const { return {}; }
    std::shared_ptr<MediaClip> MediaClip::fromJson(const QJsonObject&, QObject*) { return {}; }

    // =========================================================================
    // VideoTrack
    // =========================================================================

    VideoTrack::VideoTrack(const QString& id, Type type, const QString& name, QObject* parent)
        : QObject(parent), m_id(id), m_type(type), m_name(name) {}

    void VideoTrack::setName(const QString& name) { m_name = name; emit nameChanged(); }
    void VideoTrack::setIndex(int idx) { m_index = idx; emit indexChanged(); }
    void VideoTrack::setMuted(bool muted) { m_muted = muted; emit mutedChanged(); }
    void VideoTrack::setSoloed(bool soloed) { m_soloed = soloed; emit soloedChanged(); }
    void VideoTrack::setLocked(bool locked) { m_locked = locked; emit lockedChanged(); }
    void VideoTrack::setVolume(float vol) { m_volume = vol; emit volumeChanged(); }
    void VideoTrack::setPan(float pan) { m_pan = pan; emit panChanged(); }

    void VideoTrack::addClip(std::shared_ptr<MediaClip> clip) {
        m_clips.push_back(clip); emit clipAdded(clip); emit clipsChanged();
    }
    void VideoTrack::removeClip(std::shared_ptr<MediaClip> clip) {
        m_clips.erase(std::remove(m_clips.begin(), m_clips.end(), clip), m_clips.end());
        emit clipRemoved(clip); emit clipsChanged();
    }
    void VideoTrack::moveClip(std::shared_ptr<MediaClip> clip, const Timecode& pos) {
        clip->setPosition(pos); emit clipsChanged();
    }
    std::shared_ptr<MediaClip> VideoTrack::clipAt(const Timecode&) const { return {}; }
    std::vector<std::shared_ptr<MediaClip>> VideoTrack::clipsInRange(const Timecode&, const Timecode&) const { return {}; }
    QList<std::shared_ptr<MediaClip>> VideoTrack::clips() const {
        return QList<std::shared_ptr<MediaClip>>(m_clips.begin(), m_clips.end());
    }
    Timecode VideoTrack::duration() const { return Timecode{0, 30}; }
    bool VideoTrack::hasOverlap(const Timecode&, const Timecode&, std::shared_ptr<MediaClip>) const { return false; }
    Timecode VideoTrack::snapPosition(const Timecode& pos, const Timecode&) const { return pos; }

    // =========================================================================
    // TransportController
    // =========================================================================

    TransportController::TransportController(QObject* parent)
        : QObject(parent)
        , m_playbackTimer(new QTimer(this))
    {
        connect(m_playbackTimer, &QTimer::timeout, this, &TransportController::onPlaybackTimer);
        m_playbackTimer->setInterval(16); // ~60fps
    }

    void TransportController::play() {
        m_state = State::Playing;
        m_playbackTimer->start();
        emit stateChanged(m_state);
    }
    void TransportController::pause() {
        m_state = State::Paused;
        m_playbackTimer->stop();
        emit stateChanged(m_state);
    }
    void TransportController::stop() {
        m_state = State::Stopped;
        m_playbackTimer->stop();
        m_position = {0, static_cast<int>(m_position.fps)};
        emit stateChanged(m_state);
        emit positionChanged(m_position);
    }
    void TransportController::togglePlayPause() {
        if (m_state == State::Playing) pause(); else play();
    }
    void TransportController::seek(const Timecode& pos) {
        m_position = pos; emit positionChanged(m_position);
    }
    void TransportController::frameStep(int frames) {
        m_position.frames += frames; emit positionChanged(m_position);
    }
    void TransportController::setPosition(const Timecode& pos) { seek(pos); }
    void TransportController::setDuration(const Timecode& dur) { m_duration = dur; emit durationChanged(m_duration); }
    void TransportController::setFps(int fps) { m_position.fps = fps; emit fpsChanged(fps); }
    void TransportController::setLooping(bool loop) { m_looping = loop; emit loopingChanged(loop); }
    void TransportController::setLoopRegion(const Timecode& s, const Timecode& e) { m_loopStart = s; m_loopEnd = e; }
    void TransportController::updateFromAudioClock(qint64) {}
    void TransportController::onPlaybackTimer() {
        ++m_position.frames;
        emit positionChanged(m_position);
        emit frameReady(m_position);
    }

    // =========================================================================
    // TimelineProxy
    // =========================================================================

    TimelineProxy::TimelineProxy(VideoEditor* editor, QObject* parent)
        : QObject(parent), m_editor(editor) {}

    FrameTime TimelineProxy::playhead() const { return 0; }
    FrameTime TimelineProxy::duration() const { return 0; }
    ProjectProfile TimelineProxy::profile() const { return {}; }
    QList<QObject*> TimelineProxy::videoTracksAsObjects() const { return {}; }
    QList<QObject*> TimelineProxy::audioTracksAsObjects() const { return {}; }
    int TimelineProxy::videoTrackCount() const { return 0; }
    int TimelineProxy::audioTrackCount() const { return 0; }

    void TimelineProxy::addVideoTrack()                    { if (m_editor) m_editor->addVideoTrackFromProxy(); }
    void TimelineProxy::addAudioTrack()                    { if (m_editor) m_editor->addAudioTrackFromProxy(); }
    void TimelineProxy::removeClip(QObject*)               {}
    void TimelineProxy::splitClip(QObject*, FrameTime)     {}
    void TimelineProxy::rippleDelete(FrameTime, FrameTime) {}
    void TimelineProxy::insertClip(QObject*, QObject*, FrameTime) {}
    FrameTime TimelineProxy::snap(FrameTime frame, int) const { return frame; }

    void TimelineProxy::notifyPlayheadChanged() { emit playheadChanged(); }
    void TimelineProxy::notifyDurationChanged() { emit durationChanged(); }
    void TimelineProxy::notifyProfileChanged()  { emit profileChanged(); }
    void TimelineProxy::notifyTracksChanged()   { emit tracksChanged(); }
    void TimelineProxy::setSelection(FrameTime s, FrameTime e) {
        m_hasSelection = true; m_selectionStart = s; m_selectionEnd = e; emit selectionChanged();
    }
    void TimelineProxy::clearSelection() {
        m_hasSelection = false; emit selectionChanged();
    }

    // =========================================================================
    // VideoEditor
    // =========================================================================

    VideoEditor::VideoEditor(QObject* parent)
        : QObject(parent)
        , m_transport(std::make_unique<TransportController>(this))
        , m_timeline(std::make_unique<TimelineProxy>(this, this))
    {
        connect(m_transport.get(), &TransportController::positionChanged,
                this, &VideoEditor::onTransportPositionChanged);
        connect(m_transport.get(), &TransportController::stateChanged,
                this, &VideoEditor::onTransportStateChanged);
    }

    VideoEditor::~VideoEditor() = default;

    void VideoEditor::initializeAudio(AudioEngine* engine, AudioOutput* output) {
        m_audioEngine = engine; m_audioOutput = output;
    }
    void VideoEditor::initializeVideo(std::unique_ptr<VideoOutput> output) {
        m_videoOutput = std::move(output);
    }

    bool VideoEditor::newProject(const QString& name, const ProjectProfile& profile) {
        m_projectName = name; m_profile = profile; m_modified = false;
        m_project = std::make_unique<ProjectData>();
        emit projectChanged(); return true;
    }
    bool VideoEditor::openProject(const QString& path) {
        Q_UNUSED(path) return false;
    }
    bool VideoEditor::saveProject(const QString& path) {
        if (!path.isEmpty()) m_projectPath = path;
        m_modified = false; emit modifiedChanged(); return false;
    }
    void VideoEditor::closeProject() {
        m_project.reset(); m_modified = false; emit projectChanged();
    }

    void VideoEditor::setProfile(const ProjectProfile& profile) {
        m_profile = profile; emit profileChanged();
    }

    VideoTrack* VideoEditor::addVideoTrack(const QString& name) {
        auto* t = new VideoTrack(QUuid::createUuid().toString(QUuid::WithoutBraces),
                                 VideoTrack::Type::Video,
                                 name.isEmpty() ? QStringLiteral("Video") : name, this);
        if (m_timeline) m_timeline->notifyTracksChanged();
        emit trackAdded(t); return t;
    }
    VideoTrack* VideoEditor::addAudioTrack(const QString& name) {
        auto* t = new VideoTrack(QUuid::createUuid().toString(QUuid::WithoutBraces),
                                 VideoTrack::Type::Audio,
                                 name.isEmpty() ? QStringLiteral("Audio") : name, this);
        if (m_timeline) m_timeline->notifyTracksChanged();
        emit trackAdded(t); return t;
    }
    void VideoEditor::addVideoTrackFromProxy() { addVideoTrack(); }
    void VideoEditor::addAudioTrackFromProxy() { addAudioTrack(); }
    void VideoEditor::removeTrack(VideoTrack* track) {
        if (track) track->deleteLater();
        emit trackRemoved(track);
    }
    void VideoEditor::moveTrack(int, int) {}
    QList<VideoTrack*> VideoEditor::videoTracks() const {
        QList<VideoTrack*> result;
        for (auto* t : findChildren<VideoTrack*>(QString(), Qt::FindDirectChildrenOnly))
            if (t->type() == VideoTrack::Type::Video) result.append(t);
        return result;
    }
    QList<VideoTrack*> VideoEditor::audioTracks() const {
        QList<VideoTrack*> result;
        for (auto* t : findChildren<VideoTrack*>(QString(), Qt::FindDirectChildrenOnly))
            if (t->type() == VideoTrack::Type::Audio) result.append(t);
        return result;
    }
    QList<VideoTrack*> VideoEditor::allTracks() const {
        return findChildren<VideoTrack*>(QString(), Qt::FindDirectChildrenOnly);
    }
    VideoTrack* VideoEditor::trackAt(int idx) const {
        auto all = allTracks();
        return (idx >= 0 && idx < all.size()) ? all.at(idx) : nullptr;
    }

    std::shared_ptr<MediaClip> VideoEditor::importMedia(const QUrl& url, VideoTrack* track, const Timecode& pos) {
        auto clip = std::make_shared<MediaClip>(
            QUuid::createUuid().toString(QUuid::WithoutBraces), url,
            MediaClip::Type::Video, this);
        clip->setPosition(pos);
        if (track) track->addClip(clip);
        emit clipImported(clip);
        return clip;
    }
    void VideoEditor::removeClip(std::shared_ptr<MediaClip> clip) {
        for (auto* t : allTracks()) t->removeClip(clip);
    }
    void VideoEditor::moveClip(std::shared_ptr<MediaClip> clip, VideoTrack* track, const Timecode& pos) {
        if (track) { removeClip(clip); clip->setPosition(pos); track->addClip(clip); }
    }
    void VideoEditor::trimClip(std::shared_ptr<MediaClip> clip, const Timecode& in, const Timecode& out) {
        clip->setInPoint(in); clip->setOutPoint(out);
    }
    void VideoEditor::splitClip(std::shared_ptr<MediaClip>, const Timecode&) {}
    void VideoEditor::deleteRange(const Timecode&, const Timecode&) {}
    void VideoEditor::rippleDelete(const Timecode&, const Timecode&) {}

    void VideoEditor::play()            { if (m_transport) m_transport->play(); }
    void VideoEditor::pause()           { if (m_transport) m_transport->pause(); }
    void VideoEditor::stop()            { if (m_transport) m_transport->stop(); }
    void VideoEditor::togglePlayPause() { if (m_transport) m_transport->togglePlayPause(); }
    void VideoEditor::seek(const Timecode& pos) { if (m_transport) m_transport->seek(pos); }
    void VideoEditor::frameStep(int f)  { if (m_transport) m_transport->frameStep(f); }
    Timecode VideoEditor::duration() const { return m_transport ? m_transport->duration() : Timecode{0,30}; }

    void VideoEditor::setPreviewSize(const QSize& size) { m_previewSize = size; }
    bool VideoEditor::exportVideo(const QString&, const ExportSettings&, const Timecode&, const Timecode&) { return false; }
    void VideoEditor::startPreviewExport(const QString&) {}
    void VideoEditor::cancelExport() {}
    void VideoEditor::connectAudioReactiveEffects() {}
    void VideoEditor::setAudioSyncEnabled(bool e) { m_audioSyncEnabled = e; }
    void VideoEditor::updateVideoFromAudioClock(qint64) {}

    void VideoEditor::undo() {}
    void VideoEditor::redo() {}
    bool VideoEditor::canUndo() const { return false; }
    bool VideoEditor::canRedo() const { return false; }
    void VideoEditor::pushUndoCommand(const QString&, std::function<void()>, std::function<void()>) {}

    void VideoEditor::addMarker(const Timecode&, const QString&, const QColor&, const QString&) {}
    void VideoEditor::removeMarker(const Timecode&) {}
    QList<TimelineMarker> VideoEditor::markers() const { return {}; }

    quint64 VideoEditor::currentTextureId() const { return 0; }

    void VideoEditor::onTransportPositionChanged(const Timecode& pos) {
        emit positionChanged(pos);
        if (m_timeline) m_timeline->notifyPlayheadChanged();
    }
    void VideoEditor::onTransportStateChanged(TransportController::State state) {
        emit stateChanged(state);
    }
    void VideoEditor::onMpvFrameReady() {}
    void VideoEditor::onAudioPositionChanged(qint64) {}

    void VideoEditor::initializePillars() {}
    void VideoEditor::renderFrame(const Timecode&) {}
    void VideoEditor::compositeAndDisplay(const Timecode&) {}
    void VideoEditor::syncAudioToVideo(const Timecode&) {}
    void VideoEditor::setModified(bool m) { m_modified = m; emit modifiedChanged(); }

} // namespace Aegis
