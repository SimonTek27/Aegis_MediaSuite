// Aegis Audio Editor - UI Definition

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

    // Modern floating window with custom frame
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint
    width: 1600
    height: 1000
    minimumWidth: 1000
    minimumHeight: 700

    // Modern title with dynamic content
    title: {
        var base = "🎵 GoldWave Studio Professional"
        if (AudioEngine.modified) base += " • Modified"
            if (AudioEngine.currentFile) {
                var name = AudioEngine.currentFile.split('/').pop()
                base += ` — ${name}`
            }
            return base
    }

    // ============================================
    // CUSTOM TITLE BAR - Modern macOS/Windows 11 Style
    // ============================================

    Rectangle {
        id: titleBar
        height: 48
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        color: currentTheme === "modernDark" ? "#202020" : "#f0f0f0"
        z: 999

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 8

            // App Icon and Title
            RowLayout {
                spacing: 12

                Rectangle {
                    width: 32
                    height: 32
                    radius: 8
                    color: accentColor
                    Text {
                        anchors.center: parent
                        text: "GW"
                        font.bold: true
                        font.pixelSize: 16
                        color: "white"
                    }
                }

                Text {
                    text: mainWindow.title
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: currentTheme === "modernDark" ? "#ffffff" : "#000000"
                    opacity: 0.9
                }
            }

            Item { Layout.fillWidth: true }

            // Window Controls
            RowLayout {
                spacing: 8

                // Theme Toggle
                RoundButton {
                    implicitWidth: 36
                    implicitHeight: 36
                    icon.source: currentTheme === "modernDark" ? "qrc:/icons/light-mode.svg" : "qrc:/icons/dark-mode.svg"
                    icon.width: 20
                    icon.height: 20
                    flat: true
                    onClicked: toggleTheme()
                }

                // Minimize
                RoundButton {
                    implicitWidth: 36
                    implicitHeight: 36
                    icon.source: "qrc:/icons/minimize.svg"
                    flat: true
                    onClicked: mainWindow.showMinimized()
                }

                // Maximize/Restore
                RoundButton {
                    implicitWidth: 36
                    implicitHeight: 36
                    icon.source: mainWindow.visibility === Window.Maximized ?
                    "qrc:/icons/restore.svg" : "qrc:/icons/maximize.svg"
                    flat: true
                    onClicked: mainWindow.visibility === Window.Maximized ?
                    mainWindow.showNormal() : mainWindow.showMaximized()
                }

                // Close
                RoundButton {
                    implicitWidth: 36
                    implicitHeight: 36
                    icon.source: "qrc:/icons/close.svg"
                    flat: true
                    icon.color: "#ff4444"
                    onClicked: mainWindow.close()
                }
            }
        }

        // Bottom separator
        Rectangle {
            height: 1
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            color: currentTheme === "modernDark" ? "#404040" : "#e0e0e0"
        }
    }

    // ============================================
    // MODERN COLOR SYSTEM
    // ============================================

    property string currentTheme: "modernDark"
    property color accentColor: "#00C8B4"  // Modern teal accent
    property color accentHover: "#00E6D0"
    property color accentPressed: "#00A090"

    property var themes: {
        "modernDark": {
            background: "#1a1e24",
            surface: "#252b33",
            surface2: "#2f3640",
            surface3: "#3a424d",
            text: "#ffffff",
            textSecondary: "#a0a8b5",
            border: "#404854",
            success: "#00C8B4",
            warning: "#FFB74D",
            error: "#FF5252",
            info: "#64B5F6"
        },
        "modernLight": {
            background: "#f8f9fa",
            surface: "#ffffff",
            surface2: "#f1f3f5",
            surface3: "#e9ecef",
            text: "#212529",
            textSecondary: "#6c757d",
            border: "#dee2e6",
            success: "#00C8B4",
            warning: "#FFB74D",
            error: "#FF5252",
            info: "#64B5F6"
        }
    }

    // ============================================
    // MODERN TOOLBAR - Ribbon Style
    // ============================================

    Rectangle {
        id: ribbonToolbar
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 120
        color: themes[currentTheme].surface
        z: 998

        // Bottom shadow
        Rectangle {
            height: 1
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            color: themes[currentTheme].border
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 16

            // File Operations Group
            ToolGroup {
                title: "File"
                Layout.preferredWidth: 280

                RowLayout {
                    spacing: 4

                    ToolButtonEx {
                        text: "New"
                        iconSource: "qrc:/icons/new.svg"
                        shortcut: "Ctrl+N"
                        onClicked: AudioEngine.newFile()
                    }
                    ToolButtonEx {
                        text: "Open"
                        iconSource: "qrc:/icons/open.svg"
                        shortcut: "Ctrl+O"
                        onClicked: fileDialog.open()
                    }
                    ToolButtonEx {
                        text: "Save"
                        iconSource: "qrc:/icons/save.svg"
                        shortcut: "Ctrl+S"
                        enabled: AudioEngine.modified
                        onClicked: AudioEngine.save()
                    }
                    ToolButtonEx {
                        text: "Save As"
                        iconSource: "qrc:/icons/save-as.svg"
                        shortcut: "Ctrl+Shift+S"
                        onClicked: saveDialog.open()
                    }
                }
            }

            // Edit Operations Group
            ToolGroup {
                title: "Edit"
                Layout.preferredWidth: 280

                RowLayout {
                    spacing: 4

                    ToolButtonEx {
                        text: "Undo"
                        iconSource: "qrc:/icons/undo.svg"
                        shortcut: "Ctrl+Z"
                        enabled: AudioEngine.canUndo
                        onClicked: AudioEngine.undo()
                    }
                    ToolButtonEx {
                        text: "Redo"
                        iconSource: "qrc:/icons/redo.svg"
                        shortcut: "Ctrl+Y"
                        enabled: AudioEngine.canRedo
                        onClicked: AudioEngine.redo()
                    }
                    ToolButtonEx {
                        text: "Cut"
                        iconSource: "qrc:/icons/cut.svg"
                        shortcut: "Ctrl+X"
                        enabled: AudioEngine.hasSelection
                        onClicked: AudioEngine.cut()
                    }
                    ToolButtonEx {
                        text: "Copy"
                        iconSource: "qrc:/icons/copy.svg"
                        shortcut: "Ctrl+C"
                        enabled: AudioEngine.hasSelection
                        onClicked: AudioEngine.copy()
                    }
                    ToolButtonEx {
                        text: "Paste"
                        iconSource: "qrc:/icons/paste.svg"
                        shortcut: "Ctrl+V"
                        enabled: AudioEngine.canPaste
                        onClicked: AudioEngine.paste()
                    }
                }
            }

            // Transport Controls Group
            ToolGroup {
                title: "Transport"
                Layout.preferredWidth: 300

                RowLayout {
                    spacing: 8

                    // Play/Pause
                    Rectangle {
                        width: 48
                        height: 48
                        radius: 24
                        color: AudioEngine.isPlaying ? themes[currentTheme].warning : accentColor
                        Behavior on color { ColorAnimation { duration: 200 } }

                        IconButton {
                            anchors.centerIn: parent
                            iconSource: AudioEngine.isPlaying ? "qrc:/icons/pause.svg" : "qrc:/icons/play.svg"
                            iconColor: "white"
                            iconSize: 24
                            onClicked: AudioEngine.togglePlayback()
                        }

                        // Ripple effect on click
                        MouseArea {
                            anchors.fill: parent
                            onClicked: parent.clicked()
                            onPressed: ripple.start()
                        }
                    }

                    // Stop
                    Rectangle {
                        width: 40
                        height: 40
                        radius: 20
                        color: themes[currentTheme].surface3

                        IconButton {
                            anchors.centerIn: parent
                            iconSource: "qrc:/icons/stop.svg"
                            iconSize: 20
                            onClicked: AudioEngine.stop()
                        }
                    }

                    // Record
                    Rectangle {
                        width: 40
                        height: 40
                        radius: 20
                        color: AudioEngine.isRecording ? themes[currentTheme].error : themes[currentTheme].surface3

                        IconButton {
                            anchors.centerIn: parent
                            iconSource: "qrc:/icons/record.svg"
                            iconColor: AudioEngine.isRecording ? "white" : themes[currentTheme].text
                            iconSize: 20
                            onClicked: AudioEngine.toggleRecording()
                        }
                    }

                    // Position indicator
                    Rectangle {
                        Layout.fillWidth: true
                        height: 40
                        color: themes[currentTheme].surface2
                        radius: 20
                        padding: 8

                        RowLayout {
                            anchors.fill: parent
                            spacing: 8

                            Text {
                                text: formatTime(AudioEngine.playbackPosition)
                                color: themes[currentTheme].text
                                font.family: "Monospace"
                                font.bold: true
                            }

                            Text {
                                text: "/"
                                color: themes[currentTheme].textSecondary
                            }

                            Text {
                                text: formatTime(AudioEngine.duration)
                                color: themes[currentTheme].textSecondary
                                font.family: "Monospace"
                            }
                        }
                    }
                }
            }

            // Effects Group
            ToolGroup {
                title: "Effects"
                Layout.preferredWidth: 200

                RowLayout {
                    spacing: 4

                    ToolButtonEx {
                        text: "EQ"
                        iconSource: "qrc:/icons/equalizer.svg"
                        onClicked: showEffectPanel("equalizer")
                    }
                    ToolButtonEx {
                        text: "Comp"
                        iconSource: "qrc:/icons/compressor.svg"
                        onClicked: showEffectPanel("compressor")
                    }
                    ToolButtonEx {
                        text: "Reverb"
                        iconSource: "qrc:/icons/reverb.svg"
                        onClicked: showEffectPanel("reverb")
                    }
                    ToolButtonEx {
                        text: "VST"
                        iconSource: "qrc:/icons/vst.svg"
                        onClicked: vstManager.show()
                    }
                }
            }
        }
    }

    // ============================================
    // CUSTOM COMPONENTS
    // ============================================

    component ToolGroup: Rectangle {
        property string title

        color: "transparent"
        height: 104

        ColumnLayout {
            anchors.fill: parent
            spacing: 4

            Text {
                text: parent.title
                color: themes[currentTheme].textSecondary
                font.pixelSize: 11
                font.uppercase: true
                letterSpacing: 1
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: themes[currentTheme].surface2
                radius: 12

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    children: parent.children
                }
            }
        }
    }

    component ToolButtonEx: Button {
        property string iconSource
        property string iconColor: themes[currentTheme].text

        implicitWidth: 48
        implicitHeight: 48
        flat: true

        contentItem: ColumnLayout {
            spacing: 4

            Image {
                source: parent.iconSource
                sourceSize.width: 24
                sourceSize.height: 24
                Layout.alignment: Qt.AlignHCenter
                visible: status === Image.Ready
            }

            Text {
                text: parent.text
                font.pixelSize: 10
                color: themes[currentTheme].textSecondary
                Layout.alignment: Qt.AlignHCenter
                visible: parent.text.length > 0
            }
        }

        background: Rectangle {
            radius: 8
            color: parent.hovered ? themes[currentTheme].surface3 : "transparent"
            Behavior on color { ColorAnimation { duration: 150 } }
        }
    }

    component IconButton: Item {
        property string iconSource
        property color iconColor: themes[currentTheme].text
        property int iconSize: 16
        signal clicked

        width: 40
        height: 40

        Image {
            anchors.centerIn: parent
            source: parent.iconSource
            sourceSize.width: parent.iconSize
            sourceSize.height: parent.iconSize
            visible: status === Image.Ready
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
    }

    // ============================================
    // MAIN CONTENT AREA
    // ============================================

    Rectangle {
        anchors.top: ribbonToolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        color: themes[currentTheme].background

        RowLayout {
            anchors.fill: parent
            spacing: 1

            // Left Panel - Devices & Effects
            Rectangle {
                Layout.preferredWidth: 250
                Layout.fillHeight: true
                color: themes[currentTheme].surface
                visible: devicesPanelVisible || effectsPanelVisible

                StackLayout {
                    anchors.fill: parent
                    currentIndex: effectsPanelVisible ? 1 : 0

                    // Devices Panel
                    DevicesPanel {
                        visible: devicesPanelVisible
                    }

                    // Effects Panel
                    EffectsPanel {
                        visible: effectsPanelVisible
                    }
                }
            }

            // Center - Waveform Display
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: themes[currentTheme].surface2

                WaveformDisplay {
                    id: waveformDisplay
                    anchors.fill: parent
                    anchors.margins: 8

                    audioEngine: AudioEngine
                    zoomLevel: mainWindow.zoomLevel
                    showSpectrum: mainWindow.showSpectrum
                    showWaveform: mainWindow.showWaveform
                    selectionColor: accentColor
                    cursorColor: themes[currentTheme].warning
                    gridColor: themes[currentTheme].border
                }
            }

            // Right Panel - Analysis & Properties
            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: themes[currentTheme].surface

                RightPanel {
                    audioEngine: AudioEngine
                }
            }
        }
    }

    // ============================================
    // MODERN STATUS BAR
    // ============================================

    Rectangle {
        id: statusBar
        height: 40
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        color: themes[currentTheme].surface
        visible: statusBarVisible

        // Top border
        Rectangle {
            height: 1
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            color: themes[currentTheme].border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 24

            // Audio format info
            RowLayout {
                spacing: 16

                StatusBadge {
                    text: `${(AudioEngine.sampleRate / 1000).toFixed(1)} kHz`
                    iconSource: "qrc:/icons/sample-rate.svg"
                }

                StatusBadge {
                    text: `${AudioEngine.bitDepth}-bit`
                    iconSource: "qrc:/icons/bit-depth.svg"
                }

                StatusBadge {
                    text: AudioEngine.channelCount === 1 ? "Mono" : "Stereo"
                    iconSource: AudioEngine.channelCount === 1 ? "qrc:/icons/mono.svg" : "qrc:/icons/stereo.svg"
                }
            }

            Item { Layout.fillWidth: true }

            // Processing indicators
            RowLayout {
                spacing: 16
                visible: AudioEngine.isProcessing

                BusyIndicator {
                    running: true
                    implicitWidth: 20
                    implicitHeight: 20
                }

                Text {
                    text: "Processing: " + AudioEngine.progress + "%"
                    color: themes[currentTheme].textSecondary
                }
            }

            // Selection info
            Text {
                text: {
                    if (AudioEngine.hasSelection) {
                        let start = formatTime(AudioEngine.selectionStart)
                        let end = formatTime(AudioEngine.selectionEnd)
                        let duration = formatTime(AudioEngine.selectionEnd - AudioEngine.selectionStart)
                        return `Selected: ${start} - ${end} (${duration})`
                    }
                    return "No selection"
                }
                color: themes[currentTheme].textSecondary
            }

            // VST status
            StatusBadge {
                text: activeVstChain.length ? `${activeVstChain.length} VSTs` : "No VSTs"
                iconSource: "qrc:/icons/vst.svg"
                highlight: activeVstChain.length > 0
            }
        }
    }

    // ============================================
    // MODERN FILE DIALOGS
    // ============================================

    FileDialog {
        id: fileDialog
        title: "Open Audio File"
        fileMode: FileDialog.OpenFile
        nameFilters: [
            "All Audio Files (*.wav *.mp3 *.flac *.aiff *.m4a *.ogg *.wma)",
            "Wave Files (*.wav)",
            "MP3 Files (*.mp3)",
            "FLAC Files (*.flac)",
            "AIFF Files (*.aiff)"
        ]
        onAccepted: AudioEngine.openFile(fileDialog.file)
    }

    FileDialog {
        id: saveDialog
        title: "Save Audio File"
        fileMode: FileDialog.SaveFile
        nameFilters: [
            "Wave Files (*.wav)",
            "MP3 Files (*.mp3)",
            "FLAC Files (*.flac)",
            "AIFF Files (*.aiff)"
        ]
        onAccepted: AudioEngine.saveAs(saveDialog.file)
    }

    // ============================================
    // MODERN SIDE PANELS
    // ============================================

    component DevicesPanel: Rectangle {
        color: "transparent"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16

            Text {
                text: "AUDIO DEVICES"
                color: themes[currentTheme].textSecondary
                font.pixelSize: 12
                font.bold: true
                letterSpacing: 1
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                model: AudioEngine.availableDevices

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 60
                    color: themes[currentTheme].surface2
                    radius: 8

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4

                        Text {
                            text: modelData.name
                            color: themes[currentTheme].text
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: modelData.type + " • " + modelData.channels + " channels"
                            color: themes[currentTheme].textSecondary
                            font.pixelSize: 11
                        }
                    }

                    // Active indicator
                    Rectangle {
                        width: 4
                        height: parent.height
                        radius: 2
                        color: modelData.active ? accentColor : "transparent"
                        anchors.left: parent.left
                    }
                }
            }

            Button {
                text: "Configure Devices"
                Layout.fillWidth: true
                implicitHeight: 40
                onClicked: deviceConfigDialog.open()
            }
        }
    }

    component EffectsPanel: Rectangle {
        color: "transparent"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16

            Text {
                text: "EFFECTS CHAIN"
                color: themes[currentTheme].textSecondary
                font.pixelSize: 12
                font.bold: true
                letterSpacing: 1
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                model: activeVstChain

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 48
                    color: themes[currentTheme].surface2
                    radius: 8

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Rectangle {
                            width: 4
                            height: 24
                            radius: 2
                            color: modelData.active ? accentColor : themes[currentTheme].border
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: modelData.name
                                color: themes[currentTheme].text
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                text: modelData.vendor
                                color: themes[currentTheme].textSecondary
                                font.pixelSize: 10
                            }
                        }

                        // Bypass toggle
                        Switch {
                            checked: !modelData.bypassed
                            onToggled: modelData.bypassed = !checked
                        }
                    }
                }
            }

            RowLayout {
                spacing: 8

                Button {
                    text: "Add VST"
                    Layout.fillWidth: true
                    implicitHeight: 40
                    onClicked: vstManager.show()
                }

                Button {
                    text: "Clear"
                    Layout.fillWidth: true
                    implicitHeight: 40
                    onClicked: activeVstChain = []
                }
            }
        }
    }

    component RightPanel: Rectangle {
        property var audioEngine
        color: "transparent"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 20

            // File properties
            Text {
                text: "PROPERTIES"
                color: themes[currentTheme].textSecondary
                font.pixelSize: 12
                font.bold: true
                letterSpacing: 1
            }

            GridLayout {
                columns: 2
                columnSpacing: 8
                rowSpacing: 12
                Layout.fillWidth: true

                // Duration
                Text { text: "Duration:"; color: themes[currentTheme].textSecondary }
                Text {
                    text: formatTime(audioEngine ? audioEngine.duration : 0)
                    color: themes[currentTheme].text
                    font.bold: true
                }

                // Sample Rate
                Text { text: "Sample Rate:"; color: themes[currentTheme].textSecondary }
                Text {
                    text: audioEngine ? `${(audioEngine.sampleRate / 1000).toFixed(1)} kHz` : "—"
                    color: themes[currentTheme].text
                }

                // Bit Depth
                Text { text: "Bit Depth:"; color: themes[currentTheme].textSecondary }
                Text {
                    text: audioEngine ? `${audioEngine.bitDepth}-bit` : "—"
                    color: themes[currentTheme].text
                }

                // Channels
                Text { text: "Channels:"; color: themes[currentTheme].textSecondary }
                Text {
                    text: audioEngine ? (audioEngine.channelCount === 1 ? "Mono" : "Stereo") : "—"
                    color: themes[currentTheme].text
                }

                // File Size
                Text { text: "File Size:"; color: themes[currentTheme].textSecondary }
                Text {
                    text: audioEngine ? formatFileSize(audioEngine.fileSize) : "—"
                    color: themes[currentTheme].text
                }
            }

            // Level meters
            Text {
                text: "LEVEL METERS"
                color: themes[currentTheme].textSecondary
                font.pixelSize: 12
                font.bold: true
                letterSpacing: 1
            }

            Repeater {
                model: audioEngine ? audioEngine.channelCount : 2

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Channel " + (index + 1)
                        color: themes[currentTheme].textSecondary
                        font.pixelSize: 11
                    }

                    LevelMeter {
                        Layout.fillWidth: true
                        height: 24
                        value: audioEngine ? audioEngine.level[index] : 0
                        peak: audioEngine ? audioEngine.peak[index] : 0
                        clip: audioEngine ? audioEngine.clip[index] : false
                    }
                }
            }

            // Spectrum analyzer preview
            Text {
                text: "SPECTRUM"
                color: themes[currentTheme].textSecondary
                font.pixelSize: 12
                font.bold: true
                letterSpacing: 1
            }

            SpectrumPreview {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                audioEngine: audioEngine
            }

            Item { Layout.fillHeight: true }
        }
    }

    component LevelMeter: Rectangle {
        property real value: 0
        property real peak: 0
        property bool clip: false

        color: themes[currentTheme].surface3
        radius: 4
        clip: true

        Rectangle {
            width: parent.width * Math.min(parent.value, 1.0)
            height: parent.height
            radius: 4
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00C8B4" }
                GradientStop { position: 0.7; color: "#FFB74D" }
                GradientStop { position: 1.0; color: "#FF5252" }
            }
        }

        Rectangle {
            width: 2
            height: parent.height
            x: parent.width * Math.min(parent.peak, 1.0) - 1
            color: parent.clip ? "#FF5252" : "white"
        }
    }

    component SpectrumPreview: Rectangle {
        property var audioEngine
        color: "transparent"

        Canvas {
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)

                if (!audioEngine || !audioEngine.spectrumData) return

                    var data = audioEngine.spectrumData
                    var barWidth = width / data.length

                    ctx.fillStyle = accentColor
                    for (var i = 0; i < data.length; i++) {
                        var barHeight = data[i] * height
                        ctx.fillRect(i * barWidth, height - barHeight, barWidth - 1, barHeight)
                    }
            }
        }

        Timer {
            interval: 50
            running: true
            repeat: true
            onTriggered: parent.requestPaint()
        }
    }

    component StatusBadge: Rectangle {
        property string text
        property string iconSource
        property bool highlight: false

        color: "transparent"
        height: 24
        width: implicitWidth + 16

        implicitWidth: row.implicitWidth + 16

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 4

            Image {
                source: parent.parent.iconSource
                sourceSize.width: 16
                sourceSize.height: 16
                visible: status === Image.Ready
            }

            Text {
                text: parent.parent.text
                color: parent.parent.highlight ? accentColor : themes[currentTheme].textSecondary
                font.pixelSize: 12
            }
        }
    }

    // ============================================
    // WAVEFORM DISPLAY
    // ============================================

    component WaveformDisplay: Rectangle {
        property var audioEngine
        property double zoomLevel: 1.0
        property bool showSpectrum: true
        property bool showWaveform: true
        property color selectionColor
        property color cursorColor
        property color gridColor

        color: themes[currentTheme].background

        // Grid overlay
        Repeater {
            model: 10
            Rectangle {
                x: index * (parent.width / 10)
                width: 1
                height: parent.height
                color: parent.gridColor
                opacity: 0.3
            }
        }

        Repeater {
            model: 8
            Rectangle {
                y: index * (parent.height / 8)
                width: parent.width
                height: 1
                color: parent.gridColor
                opacity: 0.3
            }
        }

        // Selection overlay
        Rectangle {
            visible: audioEngine && audioEngine.hasSelection
            x: (audioEngine.selectionStart / audioEngine.duration) * parent.width
            width: ((audioEngine.selectionEnd - audioEngine.selectionStart) / audioEngine.duration) * parent.width
            height: parent.height
            color: parent.selectionColor
            opacity: 0.2
        }

        // Cursor
        Rectangle {
            x: (audioEngine.playbackPosition / audioEngine.duration) * parent.width - width/2
            y: 0
            width: 2
            height: parent.height
            color: parent.cursorColor
        }

        // Waveform canvas
        Canvas {
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)

                if (!audioEngine || !audioEngine.waveformData) return

                    var data = audioEngine.waveformData
                    var midY = height / 2

                    ctx.strokeStyle = "#00C8B4"
                    ctx.lineWidth = 1
                    ctx.beginPath()

                    for (var i = 0; i < data.length; i++) {
                        var x = (i / data.length) * width
                        var y = midY + (data[i] * midY)

                        if (i === 0) ctx.moveTo(x, y)
                            else ctx.lineTo(x, y)
                    }

                    ctx.stroke()
            }
        }

        Timer {
            interval: 50
            running: true
            repeat: true
            onTriggered: parent.requestPaint()
        }
    }

    // ============================================
    // UTILITY FUNCTIONS
    // ============================================

    function formatTime(seconds) {
        if (!seconds || seconds < 0) return "00:00.000"
            var mins = Math.floor(seconds / 60)
            var secs = Math.floor(seconds % 60)
            var ms = Math.floor((seconds % 1) * 1000)
            return `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}.${ms.toString().padStart(3, '0')}`
    }

    function formatFileSize(bytes) {
        if (!bytes) return "0 B"
            var units = ["B", "KB", "MB", "GB"]
            var i = 0
            while (bytes >= 1024 && i < units.length - 1) {
                bytes /= 1024
                i++
            }
            return bytes.toFixed(1) + " " + units[i]
    }

    function toggleTheme() {
        currentTheme = currentTheme === "modernDark" ? "modernLight" : "modernDark"
    }

    function showEffectPanel(effect) {
        effectsPanelVisible = true
        devicesPanelVisible = false
        // Load effect UI
    }

    // ============================================
    // KEYBOARD SHORTCUTS
    // ============================================

    Shortcut {
        sequence: "Space"
        onActivated: AudioEngine.togglePlayback()
    }

    Shortcut {
        sequence: "Ctrl+Space"
        onActivated: AudioEngine.toggleRecording()
    }

    Shortcut {
        sequence: "Home"
        onActivated: AudioEngine.goToStart()
    }

    Shortcut {
        sequence: "End"
        onActivated: AudioEngine.goToEnd()
    }

    Shortcut {
        sequence: "Ctrl+A"
        onActivated: AudioEngine.selectAll()
    }

    Shortcut {
        sequence: "Ctrl+Z"
        onActivated: AudioEngine.undo()
    }

    Shortcut {
        sequence: "Ctrl+Y"
        onActivated: AudioEngine.redo()
    }

    Shortcut {
        sequence: "Delete"
        onActivated: AudioEngine.deleteSelection()
    }

    Shortcut {
        sequence: "F5"
        onActivated: AudioEngine.playSelection()
    }

    Shortcut {
        sequence: "Ctrl+F"
        onActivated: showEffectPanel("equalizer")
    }

    // ============================================
    // COMPONENT CONNECTIONS
    // ============================================

    Connections {
        target: AudioEngine

        function onFileLoaded() {
            // Update UI when file loads
            zoomLevel = 1.0
        }

        function onPlaybackStarted() {
            // Update transport controls
        }

        function onPlaybackStopped() {
            // Update transport controls
        }

        function onRecordingStarted() {
            devicesPanelVisible = true
        }
    }
}
