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
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

namespace Aegis {

    // =============================================================================
    // ScoreView Implementation
    // =============================================================================

    ScoreView::ScoreView(QWidget* parent)
        : QWidget(parent)
        , m_renderer(nullptr)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);

        m_playbackTimer.setInterval(50); // 20 Hz update
        connect(&m_playbackTimer, &QTimer::timeout, this, [this]() {
            if (m_playbackTick >= 0) update();
        });
    }

    void ScoreView::setScore(Score* score) {
        m_score = score;
        if (score) {
            m_renderer = ScoreRenderer(score);
            m_renderer.doLayout();
        }
        update();
    }

    void ScoreView::zoomIn()      { setZoom(m_zoom * 1.25); }
    void ScoreView::zoomOut()     { setZoom(m_zoom / 1.25); }
    void ScoreView::zoomToFit()   {
        if (!m_score) return;
        setZoom(static_cast<double>(rect().width()) / 800.0);
    }
    void ScoreView::zoomToWidth() { zoomToFit(); }

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

    void ScoreView::setInputDuration(DurationType dur) {
        m_inputState.duration.type = dur;
        m_inputState.duration.dots = 0;
        m_inputState.dotted       = false;
        m_inputState.doubleDotted = false;
        update();
    }

    void ScoreView::inputNote(PitchClass pc) {
        if (!m_score || !m_currentMeasure) return;

        Note note;
        note.pitch       = Pitch(pc, m_inputState.octave, m_inputState.accidental);
        note.duration    = m_inputState.duration;
        note.voice       = m_inputState.voice;
        note.tickPosition= m_currentTick;
        note.velocity    = 80;

        m_currentMeasure->addNote(note, m_inputState.voice);

        int durationTicks = static_cast<int>(m_inputState.duration.toQuarterNotes() * 480);
        m_currentTick += durationTicks;

        // Auto-advance to next measure if needed
        if (m_currentTick >= m_currentMeasure->lengthTicks() && m_currentStaff) {
            for (int i = 0; i < m_currentStaff->measures.size(); ++i) {
                if (m_currentStaff->measures[i] == m_currentMeasure) {
                    if (i + 1 < m_currentStaff->measures.size()) {
                        m_currentMeasure = m_currentStaff->measures[i + 1];
                        m_currentTick    = 0;
                    }
                    break;
                }
            }
        }
        update();
    }

    void ScoreView::inputRest() {
        if (!m_score || !m_currentMeasure) return;

        Note rest;
        rest.isRest      = true;
        rest.duration    = m_inputState.duration;
        rest.tickPosition= m_currentTick;
        rest.voice       = m_inputState.voice;
        rest.velocity    = 0;

        m_currentMeasure->addNote(rest, m_inputState.voice);

        int durationTicks = static_cast<int>(m_inputState.duration.toQuarterNotes() * 480);
        m_currentTick += durationTicks;

        if (m_currentTick >= m_currentMeasure->lengthTicks() && m_currentStaff) {
            for (int i = 0; i < m_currentStaff->measures.size(); ++i) {
                if (m_currentStaff->measures[i] == m_currentMeasure) {
                    if (i + 1 < m_currentStaff->measures.size()) {
                        m_currentMeasure = m_currentStaff->measures[i + 1];
                        m_currentTick    = 0;
                    }
                    break;
                }
            }
        }
        update();
    }

    void ScoreView::inputChord(const QVector<PitchClass>& pitches) {
        if (!m_score || !m_currentMeasure || pitches.isEmpty()) return;

        Note mainNote;
        mainNote.pitch       = Pitch(pitches[0], m_inputState.octave, m_inputState.accidental);
        mainNote.duration    = m_inputState.duration;
        mainNote.voice       = m_inputState.voice;
        mainNote.tickPosition= m_currentTick;
        mainNote.velocity    = 80;

        for (int i = 1; i < pitches.size(); ++i)
            mainNote.chordPitches.append(Pitch(pitches[i], m_inputState.octave, m_inputState.accidental));

        m_currentMeasure->addNote(mainNote, m_inputState.voice);

        int durationTicks = static_cast<int>(m_inputState.duration.toQuarterNotes() * 480);
        m_currentTick += durationTicks;
        update();
    }

    void ScoreView::toggleTie() {
        for (Note* note : m_selectedNotes) {
            switch (note->tie) {
                case TieType::None:  note->tie = TieType::Start; break;
                case TieType::Start: note->tie = TieType::Stop;  break;
                default:             note->tie = TieType::None;  break;
            }
        }
        update();
    }

    void ScoreView::toggleSlur() {
        for (Note* note : m_selectedNotes) {
            if (note->slurs.isEmpty())
                note->slurs.append(SlurType::Start);
            else if (note->slurs.last() == SlurType::Start)
                note->slurs.append(SlurType::Stop);
            else
                note->slurs.clear();
        }
        update();
    }

    void ScoreView::addArticulation(Articulation art) {
        for (Note* note : m_selectedNotes) {
            if (!note->articulations.contains(art))
                note->articulations.append(art);
            else
                note->articulations.removeAll(art);
        }
        update();
    }

    void ScoreView::transposeSelection(int semitones) {
        for (Note* note : m_selectedNotes) {
            if (!note->isRest) {
                int newMidi = qBound(0, note->pitch.midiNote + semitones, 127);
                note->pitch.fromMidi(newMidi);
            }
        }
        update();
    }

    void ScoreView::selectAll() {
        m_selectedNotes.clear();
        m_selectedMeasures.clear();
        if (!m_score) return;

        // Iterate using raw pointer loop to avoid copying unique_ptr
        for (Staff* staff : m_score->staves) {
            for (Measure* measure : staff->measures) {
                m_selectedMeasures.append(measure);
                for (auto& note : measure->notes)
                    m_selectedNotes.append(&note);
            }
        }
        emit selectionChanged();
        update();
    }

    void ScoreView::selectNone() {
        m_selectedNotes.clear();
        m_selectedMeasures.clear();
        emit selectionChanged();
        update();
    }

    void ScoreView::deleteSelection() {
        if (m_selectedNotes.isEmpty()) return;
        for (Note* note : m_selectedNotes) {
            if (note->measure) {
                auto& notes = note->measure->notes;
                for (int i = 0; i < notes.size(); ++i) {
                    if (&notes[i] == note) {
                        note->measure->removeNote(i);
                        break;
                    }
                }
            }
        }
        m_selectedNotes.clear();
        emit selectionChanged();
        update();
    }

    void ScoreView::copy()  { /* TODO: clipboard */ }
    void ScoreView::cut()   { copy(); deleteSelection(); }
    void ScoreView::paste() { /* TODO: paste from clipboard */ }

    void ScoreView::gotoMeasure(int measureNumber) {
        if (!m_score || m_score->staves.isEmpty()) return;
        Staff* staff = m_score->staves[0];
        for (Measure* measure : staff->measures) {
            if (measure->measureNumber() == measureNumber) {
                m_offsetX = -measure->xPosition * m_zoom;
                updateTransform();
                update();
                break;
            }
        }
    }

    void ScoreView::gotoSelection() {
        if (m_selectedMeasures.isEmpty() && m_selectedNotes.isEmpty()) return;
        if (!m_selectedMeasures.isEmpty())
            m_offsetX = -m_selectedMeasures[0]->xPosition * m_zoom + width() / 2.0;
        updateTransform();
        update();
    }

    void ScoreView::pageUp()   { m_offsetY += height() * 0.8; updateTransform(); update(); }
    void ScoreView::pageDown() { m_offsetY -= height() * 0.8; updateTransform(); update(); }

    void ScoreView::highlightPlayingNotes(const QVector<Note*>& notes) {
        m_selectedNotes = notes;
        update();
    }

    // -------------------------------------------------------------------------
    // Paint
    // -------------------------------------------------------------------------

    void ScoreView::paintEvent(QPaintEvent* event) {
        Q_UNUSED(event)
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

        if (m_playbackTick >= 0)
            drawPlaybackCursor(&painter);

        if (hasSelection())
            drawSelection(&painter);

        if (m_currentTool == EditTool::NoteInput || m_currentTool == EditTool::RestInput)
            drawNoteInputCursor(&painter);
    }

    void ScoreView::drawPlaybackCursor(QPainter* painter) {
        painter->save();
        painter->setPen(QPen(EngravingSettings::defaults().playbackColor, 2));
        Measure* m = m_score->measureAtTick(m_playbackTick);
        if (m) {
            int localTick = m_playbackTick - m->startTick();
            int x         = m->tickToPixel(localTick);
            painter->drawLine(x, 0, x, height());
        }
        painter->restore();
    }

    void ScoreView::drawSelection(QPainter* painter) {
        painter->save();
        painter->setBrush(EngravingSettings::defaults().selectionColor);
        painter->setPen(Qt::NoPen);

        for (Note* note : m_selectedNotes) {
            if (note->measure) {
                double x = note->measure->xPosition
                         + note->measure->tickToPixel(note->tickPosition);
                // calculateNoteY is now public in ScoreRenderer
                double y = m_renderer.calculateNoteY(*note);
                painter->drawRect(QRectF(x - 10, y - 10, 20, 20));
            }
        }
        painter->restore();
    }

    void ScoreView::drawNoteInputCursor(QPainter* painter) {
        painter->save();
        painter->setPen(QPen(Qt::blue, 1, Qt::DashLine));
        if (m_currentMeasure) {
            int x = m_currentMeasure->tickToPixel(m_currentTick);
            painter->drawLine(x, 0, x, height());
        }
        painter->restore();
    }

    // -------------------------------------------------------------------------
    // Mouse / Keyboard
    // -------------------------------------------------------------------------

    void ScoreView::mousePressEvent(QMouseEvent* event) {
        QPointF scorePos = screenToScore(event->pos());

        if (event->button() == Qt::LeftButton) {
            switch (m_currentTool) {
                case EditTool::Select: {
                    if (!(event->modifiers() & Qt::ControlModifier)) {
                        m_selectedNotes.clear();
                        m_selectedMeasures.clear();
                    }
                    if (Note* n = noteAt(scorePos)) {
                        m_selectedNotes.append(n);
                        emit noteSelected(n);
                    } else if (Measure* m = measureAt(scorePos)) {
                        m_selectedMeasures.append(m);
                        m_currentMeasure = m;
                        emit measureSelected(m);
                    }
                    break;
                }
                case EditTool::NoteInput:
                    if (Staff* s = staffAt(scorePos)) {
                        m_currentStaff   = s;
                        m_currentMeasure = measureAt(scorePos);
                        if (m_currentMeasure) {
                            m_currentTick = tickAt(scorePos);
                            inputNote(PitchClass::C);
                        }
                    }
                    break;
                case EditTool::RestInput:
                    if (Measure* m = measureAt(scorePos)) {
                        m_currentMeasure = m;
                        m_currentTick    = tickAt(scorePos);
                        inputRest();
                    }
                    break;
                default:
                    break;
            }
            emit selectionChanged();
            update();
        }

        m_dragStart = event->pos();
        if (event->button() == Qt::LeftButton && m_currentTool == EditTool::Select)
            m_dragging = true;
    }

    void ScoreView::mouseMoveEvent(QMouseEvent* event) {
        if (m_dragging) {
            m_selectionRect = QRectF(m_dragStart, event->pos()).normalized();
            update();
        }
    }

    void ScoreView::mouseReleaseEvent(QMouseEvent* event) {
        Q_UNUSED(event)
        if (m_dragging) {
            m_dragging      = false;
            m_selectionRect = QRectF();
            update();
        }
    }

    void ScoreView::mouseDoubleClickEvent(QMouseEvent* event) { Q_UNUSED(event) }

    void ScoreView::wheelEvent(QWheelEvent* event) {
        if (event->modifiers() & Qt::ControlModifier) {
            event->angleDelta().y() > 0 ? zoomIn() : zoomOut();
            event->accept();
        } else {
            QWidget::wheelEvent(event);
        }
    }

    void ScoreView::keyPressEvent(QKeyEvent* event) {
        if (m_currentTool == EditTool::NoteInput) {
            switch (event->key()) {
                case Qt::Key_C: inputNote(PitchClass::C);  break;
                case Qt::Key_D: inputNote(PitchClass::D);  break;
                case Qt::Key_E: inputNote(PitchClass::E);  break;
                case Qt::Key_F: inputNote(PitchClass::F);  break;
                case Qt::Key_G: inputNote(PitchClass::G);  break;
                case Qt::Key_A: inputNote(PitchClass::A);  break;
                case Qt::Key_B: inputNote(PitchClass::B);  break;
                case Qt::Key_0: m_inputState.octave = 0;   break;
                case Qt::Key_1: m_inputState.octave = 1;   break;
                case Qt::Key_2: m_inputState.octave = 2;   break;
                case Qt::Key_3: m_inputState.octave = 3;   break;
                case Qt::Key_4: m_inputState.octave = 4;   break;
                case Qt::Key_5: m_inputState.octave = 5;   break;
                case Qt::Key_6: m_inputState.octave = 6;   break;
                case Qt::Key_7: m_inputState.octave = 7;   break;
                case Qt::Key_8: m_inputState.octave = 8;   break;
                case Qt::Key_Period:
                    m_inputState.toggleDot();
                    break;
                case Qt::Key_Minus:
                    m_inputState.accidental =
                        (m_inputState.accidental == Accidental::Flat)
                            ? Accidental::DoubleFlat : Accidental::Flat;
                    break;
                case Qt::Key_Equal:
                case Qt::Key_Plus:
                    m_inputState.accidental =
                        (m_inputState.accidental == Accidental::Sharp)
                            ? Accidental::DoubleSharp : Accidental::Sharp;
                    break;
                case Qt::Key_9:
                    m_inputState.accidental = Accidental::Natural;
                    break;
                case Qt::Key_Escape:
                    setEditTool(EditTool::Select);
                    break;
                default:
                    QWidget::keyPressEvent(event);
            }
            update();
        } else {
            QWidget::keyPressEvent(event);
        }
    }

    void ScoreView::resizeEvent(QResizeEvent* event) {
        QWidget::resizeEvent(event);
        if (m_score) m_renderer.doLayout();
    }

    // -------------------------------------------------------------------------
    // Coordinate helpers
    // -------------------------------------------------------------------------

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
        for (Staff* staff : m_score->staves) {
            if (pos.y() < staff->yPosition || pos.y() > staff->yPosition + staff->height())
                continue;
            for (Measure* measure : staff->measures) {
                if (pos.x() < measure->xPosition || pos.x() > measure->xPosition + measure->width)
                    continue;
                for (auto& note : measure->notes) {
                    double noteX = measure->xPosition + measure->tickToPixel(note.tickPosition);
                    double noteY = staff->yPosition + 40; // simplified
                    if (qAbs(pos.x() - noteX) < 20 && qAbs(pos.y() - noteY) < 20)
                        return &note;
                }
            }
        }
        return nullptr;
    }

    Measure* ScoreView::measureAt(const QPointF& pos) {
        if (!m_score || m_score->staves.isEmpty()) return nullptr;
        for (Staff* staff : m_score->staves) {
            if (pos.y() < staff->yPosition || pos.y() > staff->yPosition + staff->height())
                continue;
            for (Measure* measure : staff->measures) {
                if (pos.x() >= measure->xPosition && pos.x() < measure->xPosition + measure->width)
                    return measure;
            }
        }
        return nullptr;
    }

    Staff* ScoreView::staffAt(const QPointF& pos) {
        return m_score ? m_score->staffAtY(pos.y()) : nullptr;
    }

    int ScoreView::tickAt(const QPointF& pos, Measure** outMeasure) {
        if (Measure* m = measureAt(pos)) {
            if (outMeasure) *outMeasure = m;
            return m->pixelToTick(pos.x() - m->xPosition);
        }
        return 0;
    }

    void ScoreView::setPlaybackPosition(int tick) {
        m_playbackTick = tick;
        update();
    }

    // -------------------------------------------------------------------------
    // Stubs
    // -------------------------------------------------------------------------
    void ScoreView::beginNoteInput()           {}
    void ScoreView::endNoteInput()             {}
    void ScoreView::updateInputPitch(int)      {}

    // =============================================================================
    // NotationEditor Implementation
    // =============================================================================

    NotationEditor::NotationEditor(QWidget* parent)
        : QWidget(parent)
        , m_view(new ScoreView(this))
        , m_undoStack(new QUndoStack(this))
    {
        setupUI();
        createActions();
        createToolbars();
        connectSignals();
        newScore();
    }

    void NotationEditor::setupUI() {
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(0);
        mainLayout->setContentsMargins(0, 0, 0, 0);

        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setWidget(m_view);
        m_scrollArea->setWidgetResizable(true);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        mainLayout->addWidget(m_scrollArea);
    }

    void NotationEditor::createActions() {
        connect(m_view, &ScoreView::selectionChanged,
                this,  &NotationEditor::onViewSelectionChanged);
        connect(m_view, &ScoreView::measureSelected, this, [this](Measure* m) {
            emit statusMessage(tr("Measure %1 selected").arg(m ? m->measureNumber() : 0));
        });
        connect(m_view, &ScoreView::noteSelected, this, [this](Note* n) {
            emit statusMessage(n ? tr("Note %1 selected").arg(n->pitch.toString())
                                 : tr("No note"));
        });
    }

    void NotationEditor::createToolbars() {
        QToolBar* noteToolbar = new QToolBar(tr("Note Input"), this);

        // Duration buttons — use DurationType directly
        QStringList labels = {"Whole", "Half", "Quarter", "Eighth", "16th", "32nd"};
        QVector<DurationType> durTypes = {
            DurationType::Whole,
            DurationType::Half,
            DurationType::Quarter,
            DurationType::Eighth,
            DurationType::Sixteenth,
            DurationType::ThirtySecond
        };

        for (int i = 0; i < labels.size(); ++i) {
            QPushButton* btn = new QPushButton(labels[i], this);
            DurationType dt  = durTypes[i];  // capture by value
            connect(btn, &QPushButton::clicked, [this, dt]() {
                setNoteDuration(dt);
            });
            noteToolbar->addWidget(btn);
        }

        noteToolbar->addSeparator();

        QPushButton* selectBtn = new QPushButton(tr("Select"), this);
        connect(selectBtn, &QPushButton::clicked, [this]() { setTool(EditTool::Select); });
        noteToolbar->addWidget(selectBtn);

        QPushButton* noteBtn = new QPushButton(tr("Note Input"), this);
        connect(noteBtn, &QPushButton::clicked, [this]() { setTool(EditTool::NoteInput); });
        noteToolbar->addWidget(noteBtn);

        QPushButton* restBtn = new QPushButton(tr("Rest Input"), this);
        connect(restBtn, &QPushButton::clicked, [this]() { setTool(EditTool::RestInput); });
        noteToolbar->addWidget(restBtn);

        noteToolbar->addSeparator();

        QPushButton* playBtn = new QPushButton(tr("Play"), this);
        connect(playBtn, &QPushButton::clicked, this, &NotationEditor::play);
        noteToolbar->addWidget(playBtn);

        QPushButton* stopBtn = new QPushButton(tr("Stop"), this);
        connect(stopBtn, &QPushButton::clicked, this, &NotationEditor::stop);
        noteToolbar->addWidget(stopBtn);

        if (QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(this->layout()))
            layout->insertWidget(0, noteToolbar);
    }

    void NotationEditor::connectSignals() {
        connect(&m_playbackTimer, &QTimer::timeout,
                this,            &NotationEditor::onPlaybackTimer);
    }

    bool NotationEditor::newScore(const QString& title) {
        m_score = std::make_unique<Score>(this);
        m_score->setTitle(title.isEmpty() ? tr("Untitled Score") : title);

        Staff* staff = m_score->addStaff(tr("Piano"));
        Clef treble;
        treble.type = ClefType::Treble;
        staff->setDefaultClef(treble);

        Measure* m = staff->addMeasure(1);
        TimeSignature ts;
        m->timeSigs.append(ts);
        m->setLengthTicks(ts.numerator * (480 * 4 / ts.denominator));

        m_view->setScore(m_score.get());
        setModified(false);
        m_filePath.clear();
        updateWindowTitle();
        return true;
    }

    bool NotationEditor::openScore(const QString& path) {
        auto score = std::make_unique<Score>(this);
        bool loaded = false;

        if (path.endsWith(".xml",      Qt::CaseInsensitive) ||
            path.endsWith(".musicxml", Qt::CaseInsensitive)) {
            loaded = score->loadMusicXML(path);
        } else if (path.endsWith(".mid",  Qt::CaseInsensitive) ||
                   path.endsWith(".midi", Qt::CaseInsensitive)) {
            loaded = score->loadMIDI(path);
        }

        if (!loaded) {
            QMessageBox::critical(this, tr("Error"),
                                  tr("Failed to load file: %1").arg(path));
            return false;
        }

        m_score    = std::move(score);
        m_filePath = path;
        m_view->setScore(m_score.get());
        setModified(false);
        updateWindowTitle();
        return true;
    }

    bool NotationEditor::saveScore(const QString& path) {
        QString savePath = path.isEmpty() ? m_filePath : path;
        if (savePath.isEmpty()) {
            savePath = QFileDialog::getSaveFileName(
                this, tr("Save Score"), {},
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

    bool NotationEditor::exportScore(const QString& path, const QString& format) {
        if (!m_score) return false;
        if (format.compare("midi", Qt::CaseInsensitive) == 0 ||
            format.compare("mid",  Qt::CaseInsensitive) == 0)
            return m_score->saveMIDI(path);
        return false;
    }

    void NotationEditor::play() {
        if (!m_score) return;
        preparePlayback();
        m_playing           = true;
        m_playbackStartTick = 0;
        m_playbackStartTime = QTime::currentTime();
        m_playbackTimer.start(50);
        emit playbackStateChanged(true);
    }

    void NotationEditor::pause() {
        m_playing = false;
        m_playbackTimer.stop();
        emit playbackStateChanged(false);
    }

    void NotationEditor::stop() {
        m_playing = false;
        m_playbackTimer.stop();
        m_view->setPlaybackPosition(-1);
        stopPlayback();
        emit playbackStateChanged(false);
    }

    void NotationEditor::togglePlay() {
        m_playing ? pause() : play();
    }

    void NotationEditor::seek(int tick) {
        m_playbackStartTick = tick;
        m_view->setPlaybackPosition(tick);
    }

    void NotationEditor::setLoop(int startTick, int endTick) {
        m_loopStartTick = startTick;
        m_loopEndTick   = endTick;
    }
    void NotationEditor::clearLoop() {
        m_loopStartTick = -1;
        m_loopEndTick   = -1;
    }

    void NotationEditor::undo() {
        m_undoStack->undo();
        setModified(!m_undoStack->isClean());
    }
    void NotationEditor::redo() {
        m_undoStack->redo();
        setModified(!m_undoStack->isClean());
    }
    bool NotationEditor::canUndo() const { return m_undoStack->canUndo(); }
    bool NotationEditor::canRedo() const { return m_undoStack->canRedo(); }

    void NotationEditor::setTool(EditTool tool)    { m_view->setEditTool(tool); }
    void NotationEditor::setNoteDuration(DurationType dur) { m_view->setInputDuration(dur); }
    void NotationEditor::toggleDot()               { m_view->inputState().toggleDot(); }
    void NotationEditor::toggleTie()               { m_view->toggleTie(); }
    void NotationEditor::toggleSlur()              { m_view->toggleSlur(); }
    void NotationEditor::setAccidental(Accidental acc)     { m_view->inputState().accidental = acc; }
    void NotationEditor::setArticulation(Articulation art) { m_view->addArticulation(art); }
    void NotationEditor::addDynamic(int dynValue)  { Q_UNUSED(dynValue) /* TODO */ }

    void NotationEditor::preparePlayback() {
        if (!m_score) return;
        m_audioBuffer    = m_score->renderToPCM(48000);
        m_audioBufferPos = 0;
    }
    void NotationEditor::stopPlayback() { m_audioBufferPos = 0; }

    void NotationEditor::onPlaybackTimer() {
        if (!m_playing || !m_score) return;

        int    elapsedMs = m_playbackStartTime.msecsTo(QTime::currentTime());
        double seconds   = elapsedMs / 1000.0;
        double tempo     = m_score->tempo() > 0 ? m_score->tempo() : 120.0;
        int    tick      = m_playbackStartTick
                         + static_cast<int>(seconds * 480 * (tempo / 60.0));

        // Handle loop
        if (m_loopEndTick > 0 && tick >= m_loopEndTick) {
            tick                = m_loopStartTick;
            m_playbackStartTick = tick;
            m_playbackStartTime = QTime::currentTime();
            emit aboutToLoop();   // defined in this class, not Transport
        }

        // End of score
        if (tick >= m_score->totalTicks()) {
            stop();
            return;
        }

        m_view->setPlaybackPosition(tick);
    }

    void NotationEditor::onAudioEngineFinished() { stop(); }
    void NotationEditor::onViewSelectionChanged() { emit selectionChanged(); }

    void NotationEditor::setModified(bool modified) {
        m_modified = modified;
        emit scoreModified(modified);
    }

    void NotationEditor::updateWindowTitle() {
        QString title = m_score ? m_score->title() : tr("No Score");
        if (m_modified) title.prepend("* ");
        if (!m_filePath.isEmpty()) title += " - " + m_filePath;
        emit statusMessage(title);
    }

    // =============================================================================
    // NoteInputController Implementation
    // =============================================================================

    NoteInputController::NoteInputController(NotationEditor* editor, QObject* parent)
        : QObject(parent), m_editor(editor)
    {
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
        if (!m_keyboardEnabled || !m_keyMap.contains(key)) return false;
        PitchClass pc    = m_keyMap[key];
        int        octave = 4;
        if (modifiers & Qt::ShiftModifier)   octave++;
        if (modifiers & Qt::ControlModifier) octave--;
        emit noteEntered(Pitch(pc, octave), Duration{});
        return true;
    }

    void NoteInputController::onMidiNoteOn(int channel, int pitch, int velocity) {
        Q_UNUSED(channel) Q_UNUSED(velocity)
        if (!m_midiEnabled) return;
        m_activeMidiNotes.append(pitch);
    }

    void NoteInputController::onMidiNoteOff(int channel, int pitch) {
        Q_UNUSED(channel)
        m_activeMidiNotes.removeAll(pitch);
    }

    // =============================================================================
    // PaletteWidget Implementation
    // =============================================================================

    PaletteWidget::PaletteWidget(QWidget* parent) : QWidget(parent) {
        // TODO: implement notation palette UI
    }

} // namespace Aegis
