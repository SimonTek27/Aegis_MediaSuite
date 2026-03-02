// notation_editor.h - Interactive Music Notation Editor
// MuseScore-like editing interface with real-time playback.
// Compatible with Aegis DAW Engine.

#pragma once

#include "audio_daw.h"
#include <QWidget>
#include <QScrollArea>
#include <QTimer>
#include <QTime>           // Required for QTime m_playbackStartTime
#include <QUndoStack>
#include <QTransform>
#include <QMap>

namespace Aegis {

    // =============================================================================
    // Forward Declarations
    // =============================================================================

    class AudioEngine;
    class AudioOutput;

    // =============================================================================
    // Type Alias
    // NOTE: Duration does NOT contain a nested Type enum.
    //       Use DurationType directly (defined in audio_daw.h).
    // =============================================================================

    using NoteDuration = DurationType;

    // =============================================================================
    // EditTool
    // =============================================================================

    enum class EditTool {
        Select,
        NoteInput,
        RestInput,
        Slur,
        Crescendo,
        Decrescendo,
        Text,
        Lyrics,
        Tempo,
        Dynamic,
        Clef,
        KeySignature,
        TimeSignature
    };

    // =============================================================================
    // NoteInputState
    // =============================================================================

    struct NoteInputState {
        // duration.type uses DurationType; dots stored in duration.dots.
        Duration     duration{DurationType::Quarter, 0};
        int          voice         = 0;
        bool         tieMode       = false;
        bool         slurMode      = false;
        bool         dotted        = false;
        bool         doubleDotted  = false;
        Accidental   accidental    = Accidental::None;
        Articulation articulation  = Articulation::None;
        int          octave        = 4;
        bool         insertMode    = false;

        void toggleDot() {
            if (!dotted) {
                dotted = true;  doubleDotted = false; duration.dots = 1;
            } else if (!doubleDotted) {
                dotted = false; doubleDotted = true;  duration.dots = 2;
            } else {
                dotted = false; doubleDotted = false; duration.dots = 0;
            }
        }
    };

    // =============================================================================
    // ScoreView - Main notation display widget
    // =============================================================================

    class ScoreView : public QWidget {
        Q_OBJECT
    public:
        explicit ScoreView(QWidget* parent = nullptr);
        ~ScoreView() override = default;

        void setScore(Score* score);
        Score* score() const { return m_score; }

        // View control
        void   zoomIn();
        void   zoomOut();
        void   zoomToFit();
        void   zoomToWidth();
        void   setZoom(double zoom);
        double zoom() const { return m_zoom; }

        // Navigation
        void gotoMeasure(int measureNumber);
        void gotoSelection();
        void pageUp();
        void pageDown();

        // Edit state
        void     setEditTool(EditTool tool);
        EditTool editTool()  const { return m_currentTool; }

        NoteInputState&       inputState()       { return m_inputState; }
        const NoteInputState& inputState() const { return m_inputState; }

        // Use DurationType (the canonical enum) for duration setting.
        void setInputDuration(DurationType dur);

        // Selection
        void selectAll();
        void selectNone();
        bool hasSelection() const { return !m_selectedNotes.isEmpty() || !m_selectedMeasures.isEmpty(); }
        void deleteSelection();
        void copy();
        void cut();
        void paste();

        // Note input
        void inputNote(PitchClass pc);
        void inputRest();
        void inputChord(const QVector<PitchClass>& pitches);
        void toggleTie();
        void toggleSlur();
        void addArticulation(Articulation art);
        void transposeSelection(int semitones);

        // Playback visual feedback
        void setPlaybackPosition(int tick);
        void highlightPlayingNotes(const QVector<Note*>& notes);

        // Pillar integration
        void setAudioEngine(AudioEngine* engine) { m_audioEngine = engine; }
        void setAudioOutput(AudioOutput* output)  { m_audioOutput = output; }

    signals:
        void selectionChanged();
        void measureSelected(Measure* measure);
        void noteSelected(Note* note);
        void positionClicked(int tick, int staff);
        void zoomChanged(double zoom);
        void toolChanged(EditTool tool);

    protected:
        void paintEvent(QPaintEvent* event)                override;
        void mousePressEvent(QMouseEvent* event)           override;
        void mouseMoveEvent(QMouseEvent* event)            override;
        void mouseReleaseEvent(QMouseEvent* event)         override;
        void mouseDoubleClickEvent(QMouseEvent* event)     override;
        void wheelEvent(QWheelEvent* event)                override;
        void keyPressEvent(QKeyEvent* event)               override;
        void resizeEvent(QResizeEvent* event)              override;

    private:
        void updateTransform();
        QPointF screenToScore(const QPoint& pos) const;
        QPoint  scoreToScreen(const QPointF& pos) const;

        void drawSelection(QPainter* painter);
        void drawPlaybackCursor(QPainter* painter);
        void drawNoteInputCursor(QPainter* painter);

        Note*    noteAt(const QPointF& pos);
        Measure* measureAt(const QPointF& pos);
        Staff*   staffAt(const QPointF& pos);
        int      tickAt(const QPointF& pos, Measure** outMeasure = nullptr);

        void beginNoteInput();
        void endNoteInput();
        void updateInputPitch(int staffLine);

        Score*         m_score       = nullptr;
        ScoreRenderer  m_renderer;
        AudioEngine*   m_audioEngine = nullptr;
        AudioOutput*   m_audioOutput = nullptr;

        // View state
        double     m_zoom    = 1.0;
        double     m_offsetX = 0.0;
        double     m_offsetY = 0.0;
        QTransform m_transform;

        // Interaction
        EditTool       m_currentTool = EditTool::Select;
        NoteInputState m_inputState;
        bool           m_dragging    = false;
        QPoint         m_dragStart;
        QPointF        m_selectionStart;
        QRectF         m_selectionRect;

        // Current position
        Measure* m_currentMeasure    = nullptr;
        Staff*   m_currentStaff      = nullptr;
        int      m_currentTick       = 0;
        int      m_currentStaffLine  = 0;

        // Selection
        QVector<Note*>    m_selectedNotes;
        QVector<Measure*> m_selectedMeasures;

        // Playback
        int    m_playbackTick = -1;
        QTimer m_playbackTimer;

        // Layout cache
        QVector<QRectF> m_measureRects;
    };

    // =============================================================================
    // NotationEditor - Main editor widget
    // =============================================================================

    class NotationEditor : public QWidget {
        Q_OBJECT
    public:
        explicit NotationEditor(QWidget* parent = nullptr);
        ~NotationEditor() override = default;

        // File operations
        bool newScore(const QString& title = QString());
        bool openScore(const QString& path);
        bool saveScore(const QString& path = QString());
        bool exportScore(const QString& path, const QString& format);

        // Score access
        Score*      currentScore() const { return m_score.get(); }
        ScoreView*  view()         const { return m_view; }

        // Playback
        void play();
        void pause();
        void stop();
        void togglePlay();
        void seek(int tick);
        void setLoop(int startTick, int endTick);
        void clearLoop();
        bool isPlaying() const { return m_playing; }

        // Undo / redo
        void undo();
        void redo();
        bool canUndo() const;
        bool canRedo() const;

        // Pillar integration
        void setAudioEngine(AudioEngine* engine) {
            m_audioEngine = engine;
            if (m_view) m_view->setAudioEngine(engine);
        }
        void setAudioOutput(AudioOutput* output) {
            m_audioOutput = output;
            if (m_view) m_view->setAudioOutput(output);
        }

        // Tools
        void setTool(EditTool tool);
        void setNoteDuration(DurationType dur);
        void toggleDot();
        void toggleTie();
        void toggleSlur();
        void setAccidental(Accidental acc);
        void setArticulation(Articulation art);
        void addDynamic(int dynValue);

    signals:
        void scoreModified(bool modified);
        void playbackStateChanged(bool playing);
        void selectionChanged();
        void statusMessage(const QString& message);
        // Emitted just before the playback position wraps back to loop start.
        void aboutToLoop();

    private slots:
        void onViewSelectionChanged();
        void onPlaybackTimer();
        void onAudioEngineFinished();

    private:
        void setupUI();
        void createActions();
        void createToolbars();
        void connectSignals();
        void updateWindowTitle();
        void setModified(bool modified);
        void preparePlayback();
        void stopPlayback();

        std::unique_ptr<Score> m_score;
        QString                m_filePath;
        bool                   m_modified = false;

        ScoreView*   m_view;
        QScrollArea* m_scrollArea;
        QUndoStack*  m_undoStack;

        // Pillar dependencies
        AudioEngine* m_audioEngine = nullptr;
        AudioOutput* m_audioOutput = nullptr;

        // Playback state
        bool   m_playing          = false;
        int    m_playbackStartTick = 0;
        int    m_loopStartTick    = -1;
        int    m_loopEndTick      = -1;
        QTime  m_playbackStartTime;   // QTime is fully declared via <QTime>
        QTimer m_playbackTimer;

        // Audio rendering buffer
        QByteArray m_audioBuffer;
        int        m_audioBufferPos = 0;
    };

    // =============================================================================
    // NoteInputController - MIDI / keyboard note entry
    // =============================================================================

    class NoteInputController : public QObject {
        Q_OBJECT
    public:
        explicit NoteInputController(NotationEditor* editor, QObject* parent = nullptr);

        void setMidiInputEnabled(bool enabled)      { m_midiEnabled    = enabled; }
        bool isMidiInputEnabled() const             { return m_midiEnabled; }
        void setComputerKeyboardInput(bool enabled) { m_keyboardEnabled = enabled; }

        bool handleKeyPress(int key, Qt::KeyboardModifiers modifiers);
        void onMidiNoteOn(int channel, int pitch, int velocity);
        void onMidiNoteOff(int channel, int pitch);

    signals:
        void noteEntered(const Pitch& pitch, const Duration& duration);
        void chordEntered(const QVector<Pitch>& pitches);

    private:
        NotationEditor*       m_editor;
        bool                  m_midiEnabled     = false;
        bool                  m_keyboardEnabled = true;
        QMap<int, PitchClass> m_keyMap;
        QVector<int>          m_activeMidiNotes;
    };

    // =============================================================================
    // PaletteWidget - Notation element tool palette
    // =============================================================================

    class PaletteWidget : public QWidget {
        Q_OBJECT
    public:
        explicit PaletteWidget(QWidget* parent = nullptr);

    signals:
        void elementSelected(const QString& category, const QString& element);
        void accidentalSelected(Accidental acc);
        void articulationSelected(Articulation art);
        void dynamicSelected(int dynValue);
        void clefSelected(ClefType clef);
    };

} // namespace Aegis
