// notation_editor.cpp - Notation Editor Implementation
#include "notation_editor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QMessageBox>
#include <QFileDialog>
#include <QPainter>
#include <QScrollBar>
#include <QtMath>

namespace Aegis {

    // =============================================================================
    // ScoreView Implementation
    // =============================================================================

    ScoreView::ScoreView(QWidget* parent)
    : QWidget(parent)
    , m_renderer(nullptr) {

        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);

        m_playbackTimer.setInterval(50);  // 20Hz update
        connect(&m_playbackTimer, &QTimer::timeout, this, [this]() {
            if (m_playbackTick >= 0) {
                update();  // Redraw to show playback cursor
            }
        });
    }

    void ScoreView::setScore(Score* score) {
        m_score = score;
        m_renderer = ScoreRenderer(score);
        if (score) {
            m_renderer.doLayout();
        }
        update();
    }

    void ScoreView::zoomIn() {
        setZoom(m_zoom * 1.25);
    }

    void ScoreView::zoomOut() {
        setZoom(m_zoom / 1.25);
    }

    void ScoreView::zoomToFit() {
        if (!m_score) return;
        // Calculate zoom to fit width
        QRectF bounds = rect();
        double scoreWidth = 800;  // Approximate
        setZoom(bounds.width() / scoreWidth);
    }

    void ScoreView::zoomToWidth() {
        zoomToFit();
    }

    void ScoreView::setZoom(double zoom) {
        m_zoom = qBound(0.25, zoom, 4.0);
        updateTransform();
        emit zoomChanged(m_zoom);
        update();
    }

    void ScoreView::setEditTool(EditTool tool) {
        m_currentTool = tool;
        setCursor(tool == EditTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        emit toolChanged(tool);
        update();
    }

    void ScoreView::setInputDuration(NoteDuration dur) {
        m_inputState.duration.type = dur;
        m_inputState.duration.dots = 0;
        update();
    }

    void ScoreView::inputNote(PitchClass pc) {
        if (!m_score || !m_currentMeasure) return;

        Pitch pitch(pc, m_inputState.octave, m_inputState.accidental);
        Note note(pitch, m_inputState.duration);
        note.voice = m_inputState.voice;
        note.tickPosition = m_currentTick;

        // Add to measure
        m_currentMeasure->addNote(note, m_inputState.voice);

        // Advance position
        int durationTicks = static_cast<int>(m_inputState.duration.toQuarterNotes() * 480);
        m_currentTick += durationTicks;

        // Auto-advance to next measure if needed
        if (m_currentTick >= m_currentMeasure->lengthTicks()) {
            // Find next measure
        }

        setModified(true);
        update();
    }

    void ScoreView::inputRest() {
        if (!m_score || !m_currentMeasure) return;

        Rest rest(m_inputState.duration);
        rest.tickPosition = m_currentTick;
        rest.voice = m_inputState.voice;

        m_currentMeasure->addNote(rest, m_inputState.voice);

        int durationTicks = static_cast<int>(m_inputState.duration.toQuarterNotes() * 480);
        m_currentTick += durationTicks;

        setModified(true);
        update();
    }

    void ScoreView::paintEvent(QPaintEvent* event) {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);

        if (!m_score) {
            painter.setPen(Qt::gray);
            painter.drawText(rect(), Qt::AlignCenter, tr("No score loaded"));
            return;
        }

        painter.setTransform(m_transform);

        QRectF scoreRect = m_transform.inverted().mapRect(QRectF(rect()));
        m_renderer.render(&painter, scoreRect);

        // Draw playback cursor
        if (m_playbackTick >= 0) {
            drawPlaybackCursor(&painter);
        }

        // Draw selection
        if (hasSelection()) {
            drawSelection(&painter);
        }

        // Draw note input cursor
        if (m_currentTool == EditTool::NoteInput || m_currentTool == EditTool::RestInput) {
            drawNoteInputCursor(&painter);
        }
    }

    void ScoreView::drawPlaybackCursor(QPainter* painter) {
        painter->save();
        painter->setPen(QPen(EngravingSettings::defaults().playbackColor, 2));

        // Find measure containing playback tick
        Measure* m = m_score->measureAtTick(m_playbackTick);
        if (m) {
            int localTick = m_playbackTick - m->startTick();
            int x = m->tickToPixel(localTick);
            // Draw vertical line
            painter->drawLine(x, 0, x, height());
        }

        painter->restore();
    }

    void ScoreView::drawSelection(QPainter* painter) {
        painter->save();
        painter->setBrush(EngravingSettings::defaults().selectionColor);
        painter->setPen(Qt::NoPen);

        for (Note* note : m_selectedNotes) {
            // Draw selection rectangle around note
            // Simplified: just highlight
        }

        painter->restore();
    }

    void ScoreView::drawNoteInputCursor(QPainter* painter) {
        painter->save();
        painter->setPen(QPen(Qt::blue, 1, Qt::DashLine));

        // Draw vertical line at input position
        if (m_currentMeasure) {
            int x = m_currentMeasure->tickToPixel(m_currentTick);
            painter->drawLine(x, 0, x, height());
        }

        painter->restore();
    }

    void ScoreView::mousePressEvent(QMouseEvent* event) {
        QPointF scorePos = screenToScore(event->pos());

        if (event->button() == Qt::LeftButton) {
            switch (m_currentTool) {
                case EditTool::Select:
                    // Select note or measure
                    if (Note* n = noteAt(scorePos)) {
                        m_selectedNotes = {n};
                        emit noteSelected(n);
                    } else if (Measure* m = measureAt(scorePos)) {
                        m_selectedMeasures = {m};
                        m_currentMeasure = m;
                        emit measureSelected(m);
                    }
                    break;

                case EditTool::NoteInput:
                    // Place note at position
                    if (Staff* s = staffAt(scorePos)) {
                        m_currentStaff = s;
                        m_currentMeasure = measureAt(scorePos);
                        if (m_currentMeasure) {
                            m_currentTick = tickAt(scorePos);
                            // Calculate staff line from Y
                            inputNote(PitchClass::C);  // Default, would calculate from Y
                        }
                    }
                    break;

                case EditTool::RestInput:
                    inputRest();
                    break;

                default:
                    break;
            }
            update();
        }

        m_dragStart = event->pos();
    }

    void ScoreView::mouseMoveEvent(QMouseEvent* event) {
        if (m_dragging) {
            // Update selection rectangle
            m_selectionRect = QRectF(m_dragStart, event->pos()).normalized();
            update();
        }
    }

    void ScoreView::mouseReleaseEvent(QMouseEvent* event) {
        if (m_dragging) {
            m_dragging = false;
            // Finalize selection
            update();
        }
    }

    void ScoreView::wheelEvent(QWheelEvent* event) {
        if (event->modifiers() & Qt::ControlModifier) {
            // Zoom
            if (event->angleDelta().y() > 0) {
                zoomIn();
            } else {
                zoomOut();
            }
            event->accept();
        } else {
            // Scroll
            QWidget::wheelEvent(event);
        }
    }

    void ScoreView::keyPressEvent(QKeyEvent* event) {
        // Note input via keyboard
        if (m_currentTool == EditTool::NoteInput) {
            switch (event->key()) {
                case Qt::Key_C: inputNote(PitchClass::C); break;
                case Qt::Key_D: inputNote(PitchClass::D); break;
                case Qt::Key_E: inputNote(PitchClass::E); break;
                case Qt::Key_F: inputNote(PitchClass::F); break;
                case Qt::Key_G: inputNote(PitchClass::G); break;
                case Qt::Key_A: inputNote(PitchClass::A); break;
                case Qt::Key_B: inputNote(PitchClass::B); break;
                case Qt::Key_0: m_inputState.octave = 0; break;
                case Qt::Key_1: m_inputState.octave = 1; break;
                case Qt::Key_2: m_inputState.octave = 2; break;
                case Qt::Key_3: m_inputState.octave = 3; break;
                case Qt::Key_4: m_inputState.octave = 4; break;
                case Qt::Key_5: m_inputState.octave = 5; break;
                case Qt::Key_6: m_inputState.octave = 6; break;
                case Qt::Key_7: m_inputState.octave = 7; break;
                case Qt::Key_8: m_inputState.octave = 8; break;
                case Qt::Key_Period: m_inputState.toggleDot(); break;
                case Qt::Key_Escape: setEditTool(EditTool::Select); break;
                default:
                    QWidget::keyPressEvent(event);
            }
            update();
        } else {
            QWidget::keyPressEvent(event);
        }
    }

    QPointF ScoreView::screenToScore(const QPoint& pos) const {
        return m_transform.inverted().map(QPointF(pos));
    }

    QPoint ScoreView::scoreToScreen(const QPointF& pos) const {
        return m_transform.map(pos).toPoint();
    }

    void ScoreView::updateTransform() {
        m_transform = QTransform();
        m_transform.translate(m_offsetX, m_offsetY);
        m_transform.scale(m_zoom, m_zoom);
    }

    Note* ScoreView::noteAt(const QPointF& pos) {
        if (!m_score) return nullptr;
        // Hit test against note bounding boxes
        // Simplified: return nullptr for now
        return nullptr;
    }

    Measure* ScoreView::measureAt(const QPointF& pos) {
        if (!m_score || m_score->staves.isEmpty()) return nullptr;
        // Find measure by X coordinate
        // Simplified
        return nullptr;
    }

    Staff* ScoreView::score::staffAt(const QPointF& pos) {
        if (!m_score) return nullptr;
        return m_score->staffAtY(pos.y());
    }

    int ScoreView::tickAt(const QPointF& pos, Measure** outMeasure) {
        if (Measure* m = measureAt(pos)) {
            if (outMeasure) *outMeasure = m;
            return m->pixelToTick(pos.x());
        }
        return 0;
    }

    void ScoreView::setPlaybackPosition(int tick) {
        m_playbackTick = tick;
        update();
    }

    // =============================================================================
    // NotationEditor Implementation
    // =============================================================================

    NotationEditor::NotationEditor(QWidget* parent)
    : QWidget(parent)
    , m_view(new ScoreView(this))
    , m_undoStack(new QUndoStack(this)) {

        setupUI();
        createActions();
        createToolbars();

        newScore();
    }

    NotationEditor::~NotationEditor() = default;

    void NotationEditor::setupUI() {
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(0);
        mainLayout->setContentsMargins(0, 0, 0, 0);

        // Create scroll area for score view
        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setWidget(m_view);
        m_scrollArea->setWidgetResizable(true);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        mainLayout->addWidget(m_scrollArea);
    }

    void NotationEditor::createActions() {
        // File actions
        // Edit actions
        // View actions
        // Playback actions

        connect(m_view, &ScoreView::selectionChanged, this, &NotationEditor::onViewSelectionChanged);
    }

    void NotationEditor::createToolbars() {
        // Note input toolbar
        QToolBar* noteToolbar = new QToolBar(tr("Note Input"), this);

        // Duration buttons
        QStringList durations = {"Whole", "Half", "Quarter", "Eighth", "16th", "32nd"};
        for (int i = 0; i < durations.size(); ++i) {
            QPushButton* btn = new QPushButton(durations[i], this);
            connect(btn, &QPushButton::clicked, [this, i]() {
                setNoteDuration(static_cast<NoteDuration>(static_cast<int>(NoteDuration::Whole) + i));
            });
            noteToolbar->addWidget(btn);
        }

        // Tool buttons
        noteToolbar->addSeparator();

        QPushButton* selectBtn = new QPushButton(tr("Select"), this);
        connect(selectBtn, &QPushButton::clicked, [this]() { setTool(EditTool::Select); });
        noteToolbar->addWidget(selectBtn);

        QPushButton* noteBtn = new QPushButton(tr("Note Input"), this);
        connect(noteBtn, &QPushButton::clicked, [this]() { setTool(EditTool::NoteInput); });
        noteToolbar->addWidget(noteBtn);

        // Playback controls
        noteToolbar->addSeparator();

        QPushButton* playBtn = new QPushButton(tr("Play"), this);
        connect(playBtn, &QPushButton::clicked, this, &NotationEditor::play);
        noteToolbar->addWidget(playBtn);

        QPushButton* stopBtn = new QPushButton(tr("Stop"), this);
        connect(stopBtn, &QPushButton::clicked, this, &NotationEditor::stop);
        noteToolbar->addWidget(stopBtn);

        // Add to parent widget's layout (would need proper parent)
    }

    bool NotationEditor::newScore(const QString& title) {
        m_score = std::make_unique<Score>(this);
        m_score->setTitle(title.isEmpty() ? tr("Untitled Score") : title);

        // Create default staff
        Staff* staff = m_score->addStaff(tr("Piano"));
        staff->setDefaultClef(Clef{ClefType::Treble});

        // Add empty measure
        Measure* m = staff->addMeasure(1);
        m->timeSigs.append(m_score->defaultTimeSignature());

        m_view->setScore(m_score.get());
        setModified(false);
        m_filePath.clear();
        updateWindowTitle();

        return true;
    }

    bool NotationEditor::openScore(const QString& path) {
        if (path.endsWith(".xml") || path.endsWith(".musicxml")) {
            m_score = std::make_unique<Score>(this);
            if (!m_score->loadMusicXML(path)) {
                QMessageBox::critical(this, tr("Error"), tr("Failed to load MusicXML file"));
                return false;
            }
        } else if (path.endsWith(".mid") || path.endsWith(".midi")) {
            m_score = std::make_unique<Score>(this);
            if (!m_score->loadMIDI(path)) {
                QMessageBox::critical(this, tr("Error"), tr("Failed to load MIDI file"));
                return false;
            }
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Unsupported file format"));
            return false;
        }

        m_filePath = path;
        m_view->setScore(m_score.get());
        setModified(false);
        updateWindowTitle();

        return true;
    }

    bool NotationEditor::saveScore(const QString& path) {
        QString savePath = path.isEmpty() ? m_filePath : path;
        if (savePath.isEmpty()) {
            // Show save dialog
            savePath = QFileDialog::getSaveFileName(this, tr("Save Score"), QString(),
                                                    tr("MusicXML files (*.xml);;All files (*)"));
            if (savePath.isEmpty()) return false;
        }

        if (!m_score->saveMusicXML(savePath)) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to save score"));
            return false;
        }

        m_filePath = savePath;
        setModified(false);
        updateWindowTitle();
        return true;
    }

    void NotationEditor::play() {
        if (!m_score || !m_audioEngine) return;

        preparePlayback();

        m_playing = true;
        m_playbackStartTick = 0;
        m_playbackTimer.start();

        emit playbackStateChanged(true);

        if (m_audioOutput) {
            m_audioOutput->start();
        }
    }

    void NotationEditor::pause() {
        m_playing = false;
        m_playbackTimer.stop();

        if (m_audioOutput) {
            m_audioOutput->stop();
        }

        emit playbackStateChanged(false);
    }

    void NotationEditor::stop() {
        m_playing = false;
        m_playbackTimer.stop();
        m_view->setPlaybackPosition(-1);

        stopPlayback();

        if (m_audioOutput) {
            m_audioOutput->stop();
        }

        emit playbackStateChanged(false);
    }

    void NotationEditor::preparePlayback() {
        // Convert score to audio/MIDI events
        // This would generate PCM data or MIDI events for the AudioEngine

        if (!m_score) return;

        // Simplified: create a basic sine wave for each note
        // In real implementation, would use AudioEngine's synthesis capabilities
        // or sample playback

        m_audioBuffer.clear();
        int sampleRate = m_audioOutput ? m_audioOutput->sampleRate() : 48000;

        // Calculate total duration
        double totalSeconds = m_score->totalTicks() / 480.0 * (60.0 / 120.0);  // At 120 BPM
        int totalSamples = static_cast<int>(totalSeconds * sampleRate) * 2;  // Stereo

        m_audioBuffer.resize(totalSamples * sizeof(float));
        float* samples = reinterpret_cast<float*>(m_audioBuffer.data());

        // Generate audio (simplified placeholder)
        // Real implementation would use proper synthesis
        for (int i = 0; i < totalSamples; ++i) {
            samples[i] = 0.0f;  // Silence for now
        }

        m_audioBufferPos = 0;
    }

    void NotationEditor::stopPlayback() {
        m_audioBufferPos = 0;
    }

    void NotationEditor::onPlaybackTimer() {
        if (!m_playing) return;

        // Update playback position
        int elapsedMs = QTime::currentTime().msecsSinceStartOfDay() - m_playbackStartTime;
        double seconds = elapsedMs / 1000.0;
        int tick = static_cast<int>(seconds * 480 * (120.0 / 60.0));  // At 120 BPM

        if (m_loopEndTick > 0 && tick >= m_loopEndTick) {
            tick = m_loopStartTick;
            // Reset audio
        }

        if (tick >= m_score->totalTicks()) {
            stop();
            return;
        }

        m_view->setPlaybackPosition(tick);
    }

    void NotationEditor::setTool(EditTool tool) {
        m_view->setEditTool(tool);
    }

    void NotationEditor::setNoteDuration(NoteDuration dur) {
        m_view->setInputDuration(dur);
    }

    void NotationEditor::toggleDot() {
        m_view->inputState().toggleDot();
    }

    void NotationEditor::setModified(bool modified) {
        m_modified = modified;
        emit scoreModified(modified);
    }

    void NotationEditor::updateWindowTitle() {
        QString title = m_score ? m_score->title() : tr("No Score");
        if (m_modified) title.prepend("* ");
        // Would set parent window title
    }

    void NotationEditor::onViewSelectionChanged() {
        emit selectionChanged();
    }

    // =============================================================================
    // NoteInputController Implementation
    // =============================================================================

    NoteInputController::NoteInputController(NotationEditor* editor, QObject* parent)
    : QObject(parent), m_editor(editor) {

        // Initialize key map (computer keyboard to pitch)
        // Like MuseScore: ASDFGHJK map to white keys
        m_keyMap[Qt::Key_A] = PitchClass::C;
        m_keyMap[Qt::Key_W] = PitchClass::CSharp;
        m_keyMap[Qt::Key_S] = PitchClass::D;
        m_keyMap[Qt::Key_E] = PitchClass::DSharp;
        m_keyMap[Qt::Key_D] = PitchClass::E;
        m_keyMap[Qt::Key_F] = PitchClass::F;
        m_keyMap[Qt::Key_T] = PitchClass::FSharp;
        m_keyMap[Qt::Key_G] = PitchClass::G;
        m_keyMap[Qt::Key_Y] = PitchClass::GSharp;
        m_keyMap[Qt::Key_H] = PitchClass::A;
        m_keyMap[Qt::Key_U] = PitchClass::ASharp;
        m_keyMap[Qt::Key_J] = PitchClass::B;
    }

    bool NoteInputController::handleKeyPress(int key, Qt::KeyboardModifiers modifiers) {
        if (!m_keyboardEnabled) return false;

        if (m_keyMap.contains(key)) {
            PitchClass pc = m_keyMap[key];

            // Adjust octave with modifiers
            int octave = 4;
            if (modifiers & Qt::ShiftModifier) octave++;
            if (modifiers & Qt::ControlModifier) octave--;

            emit noteEntered(Pitch(pc, octave), Duration());
            return true;
        }

        return false;
    }

    void NoteInputController::onMidiNoteOn(int channel, int pitch, int velocity) {
        Q_UNUSED(channel)
        Q_UNUSED(velocity)

        if (!m_midiEnabled) return;

        m_activeMidiNotes.append(pitch);

        // If this is the first note, start timing
        // If additional notes come in quickly, form a chord
    }

    void NoteInputController::onMidiNoteOff(int channel, int pitch) {
        Q_UNUSED(channel)

        m_activeMidiNotes.removeAll(pitch);

        if (m_activeMidiNotes.isEmpty()) {
            // All notes released - commit chord or single note
        }
    }

} // namespace Aegis
