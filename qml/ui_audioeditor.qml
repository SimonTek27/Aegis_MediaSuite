// Aegis Audio Editor - UI Definition
// This file defines the main application window with Aegis Audio Editor-style interface
// Note: This is a recreation of Aegis Audio Editor's UI design, not an official product

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import QtMultimedia
import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
import Aegis.AudioEditor 1.0
import Aegis.Analysis 1.0

ApplicationWindow {
    id: mainWindow
    visible: true

    // Aegis Audio Editor typically uses fixed size with resizable window
    width: 1600
    height: 1000
    minimumWidth: 800
    minimumHeight: 600

    // Window title with modified indicator and filename
    title: {
        var baseTitle = "Aegis Audio Editor"
        if (AudioEditor.modified) {
            baseTitle += " *"  // Asterisk indicates unsaved changes
        }
        if (AudioEditor.currentFile) {
            var fileName = AudioEditor.currentFile.split('/').pop()
            baseTitle += " - " + fileName
        }
        return baseTitle
    }

    // ============================================
    // MENU BAR - Aegis Audio Editor Style Layout
    // ============================================

    menuBar: MenuBar {
        id: mainMenuBar

        // FILE MENU - Aegis Audio Editor
        Menu {
            title: qsTr("&File")

            MenuItem {
                text: qsTr("&New...")
                shortcut: "Ctrl+N"
                onTriggered: AudioEditor.newFile()
            }

            MenuItem {
                text: qsTr("&Open...")
                shortcut: "Ctrl+O"
                onTriggered: fileDialog.open()
            }

            MenuSeparator {}

            Menu {
                title: qsTr("&Open Special")  // Aegis Audio Editor's wording
                MenuItem { text: qsTr("&Append...") }
                MenuItem { text: qsTr("&Paste New...") }
                MenuItem { text: qsTr("&Record New...") }
            }

            MenuItem {
                text: qsTr("&Close")
                shortcut: "Ctrl+W"
                onTriggered: AudioEditor.closeFile()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Save")
                shortcut: "Ctrl+S"
                enabled: AudioEditor.modified
                onTriggered: AudioEditor.save()
            }

            MenuItem {
                text: qsTr("Save &As...")
                shortcut: "Ctrl+Shift+S"
                onTriggered: saveDialog.open()
            }

            MenuItem {
                text: qsTr("Save &Selection As...")  // Aegis Audio Editor's wording
                onTriggered: saveSelectionDialog.open()
            }

            MenuItem {
                text: qsTr("Save &Copy As...")  // Aegis Audio Editor's wording
            }

            MenuSeparator {}

            Menu {
                title: qsTr("&File Information")  // Aegis Audio Editor's wording
                MenuItem { text: qsTr("&Summary Information...") }
                MenuItem { text: qsTr("C&D Information...") }
                MenuItem { text: qsTr("Edit Cue &Points...") }
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Revert to Saved")
                enabled: AudioEditor.modified
                onTriggered: AudioEditor.revert()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Exit")
                shortcut: "Alt+F4"
                onTriggered: Qt.quit()
            }

            MenuItem {
                text: qsTr("E&xit and Return to [Application]")  // Aegis Audio Editor's exact wording
                visible: false  // Only shown when called from another app
            }
        }

        // EDIT MENU - Aegis Audio Editor's exact order
        Menu {
            title: qsTr("&Edit")

            MenuItem {
                text: qsTr("&Undo")
                shortcut: "Ctrl+Z"
                enabled: AudioEditor.canUndo
                onTriggered: AudioEditor.undo()
            }

            MenuItem {
                text: qsTr("&Redo")
                shortcut: "Ctrl+Y"
                enabled: AudioEditor.canRedo
                onTriggered: AudioEditor.redo()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Cu&t")
                shortcut: "Ctrl+X"
                enabled: AudioEditor.hasSelection
                onTriggered: AudioEditor.cut()
            }

            MenuItem {
                text: qsTr("&Copy")
                shortcut: "Ctrl+C"
                enabled: AudioEditor.hasSelection
                onTriggered: AudioEditor.copy()
            }

            MenuItem {
                text: qsTr("&Paste")
                shortcut: "Ctrl+V"
                enabled: AudioEditor.canPaste
                onTriggered: AudioEditor.paste()
            }

            MenuItem {
                text: qsTr("Paste &New")  // Aegis Audio Editor's exact wording
                shortcut: "Ctrl+Shift+V"
                enabled: AudioEditor.canPaste
                onTriggered: AudioEditor.pasteNew()
            }

            MenuItem {
                text: qsTr("Paste &at Cursor")  // Aegis Audio Editor's exact wording
                shortcut: "Ctrl+B"
                enabled: AudioEditor.canPaste
                onTriggered: AudioEditor.pasteAtCursor()
            }

            MenuItem {
                text: qsTr("&Mix...")
                shortcut: "Ctrl+M"
                enabled: AudioEditor.canPaste
                onTriggered: AudioEditor.mix()
            }

            MenuItem {
                text: qsTr("&Replace")
                shortcut: "Ctrl+R"
                enabled: AudioEditor.canPaste && AudioEditor.hasSelection
                onTriggered: AudioEditor.replace()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Delete")
                shortcut: "Del"
                enabled: AudioEditor.hasSelection
                onTriggered: AudioEditor.deleteSelection()
            }

            MenuItem {
                text: qsTr("De&lete Silence...")  // Aegis Audio Editor's exact wording
                onTriggered: AudioEditor.deleteSilence()
            }

            MenuItem {
                text: qsTr("Tri&m")  // Aegis Audio Editor's exact wording
                shortcut: "Ctrl+T"
                enabled: AudioEditor.hasSelection
                onTriggered: AudioEditor.trim()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Select All")
                shortcut: "Ctrl+A"
                onTriggered: AudioEditor.selectAll()
            }

            MenuItem {
                text: qsTr("Select &None")  // Aegis Audio Editor's exact wording
                shortcut: "Ctrl+Shift+A"
                onTriggered: AudioEditor.selectNone()
            }

            MenuSeparator {}

            Menu {
                title: qsTr("&Insert")  // Aegis Audio Editor's exact wording
                MenuItem { text: qsTr("&Silence...") }
                MenuItem { text: qsTr("&Noise...") }
                MenuItem { text: qsTr("&Tone...") }
                MenuItem { text: qsTr("&File...") }
            }

            Menu {
                title: qsTr("&Overwrite")  // Aegis Audio Editor's exact wording
                MenuItem { text: qsTr("&Silence...") }
                MenuItem { text: qsTr("&Noise...") }
                MenuItem { text: qsTr("&Tone...") }
                MenuItem { text: qsTr("&File...") }
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Chan&nel")  // Aegis Audio Editor's exact wording
                Menu {
                    MenuItem { text: qsTr("&Left") }
                    MenuItem { text: qsTr("&Right") }
                    MenuItem { text: qsTr("&Both") }
                    MenuSeparator {}
                    MenuItem { text: qsTr("&Swap") }
                    MenuItem { text: qsTr("M&aximize") }
                }
            }

            MenuItem {
                text: qsTr("Co&nvert")  // Aegis Audio Editor's exact wording
                Menu {
                    MenuItem { text: qsTr("T&o Mono...") }
                    MenuItem { text: qsTr("To S&tereo...") }
                    MenuSeparator {}
                    MenuItem { text: qsTr("&Sample Rate...") }
                    MenuItem { text: qsTr("&Format...") }
                }
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Find &Clip...")  // Aegis Audio Editor's exact wording
                shortcut: "Ctrl+F"
                onTriggered: AudioEditor.findClip()
            }

            MenuItem {
                text: qsTr("Find &Next Clip")  // Aegis Audio Editor's exact wording
                shortcut: "F3"
                onTriggered: AudioEditor.findNextClip()
            }
        }

        // EFFECT MENU - Aegis Audio Editor's exact order
        Menu {
            title: qsTr("&Effect")

            MenuItem {
                text: qsTr("&Doppler...")
                onTriggered: effectDialog.showEffect("doppler")
            }

            MenuItem {
                text: qsTr("&Dynamics...")
                onTriggered: effectDialog.showEffect("dynamics")
            }

            MenuItem {
                text: qsTr("&Echo...")
                onTriggered: effectDialog.showEffect("echo")
            }

            MenuItem {
                text: qsTr("E&qualizer...")
                onTriggered: effectDialog.showEffect("equalizer")
            }

            MenuItem {
                text: qsTr("&Filter...")
                onTriggered: effectDialog.showEffect("filter")
            }

            MenuItem {
                text: qsTr("F&lange...")
                onTriggered: effectDialog.showEffect("flange")
            }

            MenuItem {
                text: qsTr("In&terpolate...")
                onTriggered: effectDialog.showEffect("interpolate")
            }

            MenuItem {
                text: qsTr("Me&chanize...")
                onTriggered: effectDialog.showEffect("mechanize")
            }

            MenuItem {
                text: qsTr("&Noise Reduction...")
                onTriggered: effectDialog.showEffect("noiseReduction")
            }

            MenuItem {
                text: qsTr("&Pan...")
                onTriggered: effectDialog.showEffect("pan")
            }

            MenuItem {
                text: qsTr("&Parametric Equalizer...")
                onTriggered: effectDialog.showEffect("parametricEq")
            }

            MenuItem {
                text: qsTr("Pitch...")  // No ampersand in Aegis Audio Editor
                onTriggered: effectDialog.showEffect("pitch")
            }

            MenuItem {
                text: qsTr("&Reverse")
                onTriggered: AudioEditor.reverse()
            }

            MenuItem {
                text: qsTr("S&ilence")
                onTriggered: AudioEditor.silence()
            }

            MenuItem {
                text: qsTr("&Time Warp...")
                onTriggered: effectDialog.showEffect("timeWarp")
            }

            MenuItem {
                text: qsTr("&Volume...")
                onTriggered: effectDialog.showEffect("volume")
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Combine...")
                onTriggered: effectDialog.showEffect("combine")
            }

            MenuItem {
                text: qsTr("&Shape...")
                onTriggered: effectDialog.showEffect("shape")
            }

            MenuItem {
                text: qsTr("S&urround...")
                onTriggered: effectDialog.showEffect("surround")
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Expression Evaluator...")
                onTriggered: effectDialog.showEffect("expression")
            }

            MenuItem {
                text: qsTr("&Convolution...")
                onTriggered: effectDialog.showEffect("convolution")
            }

            MenuItem {
                text: qsTr("&VST Plugin...")
                onTriggered: vstDialog.open()
            }
        }

        // TOOLS MENU - Aegis Audio Editor's exact order
        Menu {
            title: qsTr("&Tools")

            MenuItem {
                text: qsTr("&CD Reader...")
                onTriggered: cdReaderDialog.open()
            }

            MenuItem {
                text: qsTr("C&DX...")  // Aegis Audio Editor's exact wording
                onTriggered: cddxDlg.open()
            }

            MenuItem {
                text: qsTr("&Expression Evaluator...")
                onTriggered: expressionDialog.open()
            }

            MenuItem {
                text: qsTr("&File Merger...")
                onTriggered: fileMergerDialog.open()
            }

            MenuItem {
                text: qsTr("&Batch Processing...")
                onTriggered: batchProcessingDialog.open()
            }

            MenuItem {
                text: qsTr("&Control Properties...")
                onTriggered: controlPropertiesDialog.open()
            }

            MenuItem {
                text: qsTr("Play&back Rate...")
                onTriggered: playbackRateDialog.open()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Macro...")
                onTriggered: macroDialog.open()
            }

            MenuItem {
                text: qsTr("Quick &Macro")  // Aegis Audio Editor's exact wording
                Menu {
                    MenuItem { text: qsTr("&Record Quick Macro...") }
                    MenuItem { text: qsTr("Play &Quick Macro...") }
                    MenuItem { text: qsTr("Edit Quick &Macros...") }
                }
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Marker")
                Menu {
                    MenuItem { text: qsTr("&Set Start Marker") }
                    MenuItem { text: qsTr("Set &End Marker") }
                    MenuItem { text: qsTr("&Go to Start Marker") }
                    MenuItem { text: qsTr("Go to &End Marker") }
                    MenuSeparator {}
                    MenuItem { text: qsTr("&Clear Markers") }
                    MenuItem { text: qsTr("Auto &Markers...") }
                }
            }
        }

        // OPTIONS MENU - Aegis Audio Editor's exact order
        Menu {
            title: qsTr("&Options")

            MenuItem {
                text: qsTr("&Toolbar...")
                onTriggered: toolbarDialog.open()
            }

            MenuItem {
                text: qsTr("&Colors...")
                onTriggered: colorsDialog.open()
            }

            MenuItem {
                text: qsTr("&Font...")
                onTriggered: fontDialog.open()
            }

            MenuItem {
                text: qsTr("&Keyboard...")
                onTriggered: keyboardDialog.open()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Playback Options...")
                onTriggered: playbackOptionsDialog.open()
            }

            MenuItem {
                text: qsTr("&Recording Options...")
                onTriggered: recordingOptionsDialog.open()
            }

            MenuItem {
                text: qsTr("&Status Bar")
                checkable: true
                checked: statusBarVisible
                onTriggered: statusBarVisible = checked
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Always on Top")
                checkable: true
                checked: mainWindow.flags & Qt.WindowStaysOnTopHint
                onTriggered: {
                    if (checked) {
                        mainWindow.flags |= Qt.WindowStaysOnTopHint
                    } else {
                        mainWindow.flags &= ~Qt.WindowStaysOnTopHint
                    }
                }
            }

            MenuItem {
                text: qsTr("&Minimize to Tray")
                checkable: true
                checked: false
                onTriggered: AudioEditor.minimizeToTray = checked
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&File Associations...")
                onTriggered: fileAssociationsDialog.open()
            }
        }

        // WINDOW MENU - Aegis Audio Editor's exact order
        Menu {
            title: qsTr("&Window")

            MenuItem {
                text: qsTr("&Cascade")
                onTriggered: windowManager.cascade()
            }

            MenuItem {
                text: qsTr("&Tile Horizontally")
                onTriggered: windowManager.tileHorizontally()
            }

            MenuItem {
                text: qsTr("Tile &Vertically")
                onTriggered: windowManager.tileVertically()
            }

            MenuItem {
                text: qsTr("&Arrange Icons")
                onTriggered: windowManager.arrangeIcons()
            }

            MenuSeparator {}

            Repeater {
                model: windowManager.windowList
                MenuItem {
                    text: modelData.title
                    checkable: true
                    checked: modelData.active
                    onTriggered: windowManager.activateWindow(modelData.id)
                }
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Close All")
                onTriggered: windowManager.closeAll()
            }
        }

        // HELP MENU - Aegis Audio Editor's exact order
        Menu {
            title: qsTr("&Help")

            MenuItem {
                text: qsTr("&Contents")
                shortcut: "F1"
                onTriggered: helpSystem.showContents()
            }

            MenuItem {
                text: qsTr("&Index...")
                onTriggered: helpSystem.showIndex()
            }

            MenuItem {
                text: qsTr("&Search...")
                onTriggered: helpSystem.showSearch()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Tip of the Day...")
                onTriggered: tipDialog.open()
            }

            MenuItem {
                text: qsTr("&Register...")
                onTriggered: registerDialog.open()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Check for &Updates...")
                onTriggered: updateChecker.check()
            }

            MenuItem {
                text: qsTr("&About Aegis Audio Editor...")
                onTriggered: aboutDialog.open()
            }
        }
    }

    // ============================================
    // COLOR THEME SYSTEM
    // ============================================

    // Classic Aegis Audio Editor color schemes - dark theme as default
    property string currentTheme: "classicDark"

    // Theme definitions matching Aegis Audio Editor's visual style
    property var themes: {
        "classicDark": {
            // Main background colors
            window: "#1a1a1a",
            windowText: "#e0e0e0",
            base: "#2a2a2a",
            alternateBase: "#252525",
            text: "#d0d0d0",

            // Control colors
            button: "#3a3a3a",
            buttonText: "#ffffff",
            highlight: "#0066cc",  // Classic blue highlight
            highlightedText: "#ffffff",

            // 3D effect colors
            mid: "#404040",
            midlight: "#505050",
            shadow: "#1a1a1a",

            // Special UI elements
            toolbar: "#333333",
            waveformBg: "#000000",
            selection: "#3399ff",  // Light blue selection
            cursor: "#ffff00",     // Yellow playback cursor
            ruler: "#808080"
        },
        "classicLight": {
            // Light theme similar to older Windows applications
            window: "#f0f0f0",
            windowText: "#000000",
            base: "#ffffff",
            alternateBase: "#f5f5f5",
            text: "#000000",

            // Control colors
            button: "#e0e0e0",
            buttonText: "#000000",
            highlight: "#0066cc",  // Consistent highlight color
            highlightedText: "#ffffff",

            // 3D effect colors
            mid: "#c0c0c0",
            midlight: "#d0d0d0",
            shadow: "#a0a0a0",

            // Special UI elements
            toolbar: "#e8e8e8",
            waveformBg: "#ffffff",
            selection: "#3399ff",
            cursor: "#ff0000",     // Red cursor for light theme
            ruler: "#606060"
        }
    }

    // Apply selected theme to window palette
    palette: themes[currentTheme]

    // ============================================
    // AUDIO VISUALIZATION COLORS
    // ============================================

    // Waveform and meter colors matching Aegis Audio Editor style
    property color waveformColor: "#00cc00"        // Green waveforms
    property color waveformCenterLine: "#808080"   // Gray center line
    property color selectionColor: themes[currentTheme].selection
    property color cursorColor: themes[currentTheme].cursor

    // Level meter colors (gradient from green to red)
    property color meterLow: "#00ff00"      // Green
    property color meterMid: "#ffff00"      // Yellow
    property color meterHigh: "#ff0000"     // Red
    property color meterClip: "#ff00ff"     // Magenta for clipping

    // Spectrum analyzer colors (blue to red gradient)
    property color spectrumStart: "#0000ff"  // Blue
    property color spectrumMid: "#00ffff"    // Cyan
    property color spectrumEnd: "#ff0000"    // Red

    // ============================================
    // APPLICATION STATE PROPERTIES
    // ============================================

    // Reference to AudioEditor C++ backend
    property alias audioEditor: AudioEditor

    // Audio playback/recording state
    property bool isPlaying: AudioEditor.isPlaying
    property bool isRecording: AudioEditor.isRecording
    property bool isPaused: AudioEditor.isPaused
    property double playbackPosition: AudioEditor.playbackPosition
    property double selectionStart: AudioEditor.selectionStart
    property double selectionEnd: AudioEditor.selectionEnd

    // Audio file properties (linked to backend)
    property double sampleRate: AudioEditor.sampleRate
    property int channels: AudioEditor.channelCount
    property int bitDepth: AudioEditor.bitDepth
    property double duration: AudioEditor.duration

    // View properties
    property double zoomLevel: 1.0          // Zoom multiplier
    property bool showSpectrum: true        // Spectrum analyzer visibility
    property bool showWaveform: true        // Waveform visibility
    property bool showControls: true        // Control panel visibility

    // ============================================
    // EFFECTS AND PROCESSING
    // ============================================

    // Effect presets storage
    property var effectPresets: AudioEditor.effectPresets || []

    // VST plugin management
    property var vstPlugins: []
    property var activeVstChain: []
    property bool vstLoaded: false
    property bool vstScanning: false

    // Batch processing
    property bool batchMode: false
    property var batchQueue: []
    property int batchProgress: 0
    property int batchTotal: 0

    // ============================================
    // HARDWARE AND DEVICES
    // ============================================

    // CD burning capabilities
    property bool isBurning: CDBurner ? CDBurner.burning : false
    property var availableDrives: []

    // Audio device properties
    property var audioDevices: AudioEditor.availableDevices || []
    property string inputDevice: AudioEditor.inputDevice || ""
    property string outputDevice: AudioEditor.outputDevice || ""

    // ============================================
    // UI STATE FLAGS
    // ============================================

    // Toolbar and panel visibility
    property bool toolbarVisible: true
    property bool statusBarVisible: true
    property bool effectsPanelVisible: false
    property bool devicesPanelVisible: false

    // Visual feedback states
    property bool showGrid: true
    property bool showRuler: true
    property bool showMarkers: true

    // ============================================
    // WINDOW SETTINGS
    // ============================================

    // Window state persistence
    property var windowState: ({
        x: 100,
        y: 100,
        width: 1600,
        height: 1000,
        maximized: false
    })

    // Recent files list (max 10 files)
    property var recentFiles: AudioEditor.recentFiles || []

    // User preferences
    property bool autoSaveEnabled: false
    property int autoSaveInterval: 300000  // 5 minutes in milliseconds
    property bool showTooltips: true
    property bool confirmDeletion: true

    // ============================================
    // FILE DIALOGS
    // ============================================

    FileDialog {
        id: fileDialog
        title: qsTr("Open Audio File")
        fileMode: FileDialog.OpenFile
        nameFilters: [
            qsTr("Audio Files (*.wav *.mp3 *.ogg *.flac *.aac *.m4a *.wma *.aiff *.au)"),
            qsTr("Wave Files (*.wav)"),
            qsTr("MP3 Files (*.mp3)"),
            qsTr("All Files (*)")
        ]
        onAccepted: AudioEditor.openFile(fileDialog.selectedFile)
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save Audio File")
        fileMode: FileDialog.SaveFile
        nameFilters: [
            qsTr("Wave Files (*.wav)"),
            qsTr("MP3 Files (*.mp3)"),
            qsTr("Ogg Vorbis Files (*.ogg)"),
            qsTr("All Files (*)")
        ]
        onAccepted: AudioEditor.saveAs(saveDialog.selectedFile)
    }

    FileDialog {
        id: saveSelectionDialog
        title: qsTr("Save Selection As")
        fileMode: FileDialog.SaveFile
        nameFilters: fileDialog.nameFilters
        onAccepted: AudioEditor.saveSelection(saveSelectionDialog.selectedFile)
    }

    // ============================================
    // SIGNAL CONNECTIONS
    // ============================================

    Connections {
        target: AudioEditor

        function onPlaybackPositionChanged(position) {
            playbackPosition = position;
        }

        function onSelectionChanged(start, end) {
            selectionStart = start;
            selectionEnd = end;
        }

        function onFileLoaded() {
            sampleRate = AudioEditor.sampleRate;
            channels = AudioEditor.channelCount;
            bitDepth = AudioEditor.bitDepth;
            duration = AudioEditor.duration;
        }

        function onModifiedChanged() {
            mainWindow.title = mainWindow.title;
        }
    }

    // ============================================
    // FUNCTIONS
    // ============================================

    function toggleTheme() {
        if (currentTheme === "classicDark") {
            currentTheme = "classicLight";
        } else {
            currentTheme = "classicDark";
        }
        palette = themes[currentTheme];
    }

    function resetZoom() {
        zoomLevel = 1.0;
    }

    function fitToWindow() {
        if (duration > 0) {
            zoomLevel = mainWindow.width / (duration * 100);
        }
    }

    // Aegis Audio Editor-style keyboard shortcuts for common actions
    Shortcut {
        sequence: "Space"
        onActivated: AudioEditor.togglePlayback()
    }

    Shortcut {
        sequence: "Ctrl+Home"
        onActivated: AudioEditor.goToStart()
    }

    Shortcut {
        sequence: "Ctrl+End"
        onActivated: AudioEditor.goToEnd()
    }

    Shortcut {
        sequence: "Home"
        onActivated: AudioEditor.goToSelectionStart()
    }

    Shortcut {
        sequence: "End"
        onActivated: AudioEditor.goToSelectionEnd()
    }
}
