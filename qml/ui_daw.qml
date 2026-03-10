// ui_daw.qml - Aegis Digital Audio Workstation
// Fusione professionale tra audio editor e middleware audio

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Shapes
import Qt.labs.platform as Platform

ApplicationWindow {
    id: dawWindow
    visible: true
    width: 1800
    height: 1000
    minimumWidth: 1400
    minimumHeight: 800
    title: qsTr("Aegis DAW Studio") + (projectModified ? " *" : "")
    color: theme.background

    // ============================================
    // 1. TEMA PROFESSIONALE (Fusione AudioEditor + Middleware)
    // ============================================

    property var theme: {
        "background": "#1a1e24",
        "surface": "#252b33",
        "surface2": "#2f3640",
        "surface3": "#3a424d",
        "panel": "#1e1e1e",
        "panelDark": "#252526",
        "panelLight": "#2d2d30",
        "accent": "#00C8B4",          // Teal professionale
        "accentSecondary": "#007acc", // Blu FMOD-style
        "accentHover": "#00E6D0",
        "accentPressed": "#00A090",
        "textPrimary": "#ffffff",
        "textSecondary": "#a0a8b5",
        "textMuted": "#5a6270",
        "border": "#404854",
        "grid": "#2d2d30",
        "success": "#4ec9b0",
        "warning": "#ce9178",
        "error": "#f14c4c",
        "selection": "#264f78",
        "playhead": "#ff8c00"
    }

    // ============================================
    // 2. BACKEND & STATO
    // ============================================

    property var audioEngine: typeof AudioEngine !== 'undefined' ? AudioEngine : null
    property var midiEngine: typeof MidiEngine !== 'undefined' ? MidiEngine : null
    property var vstManager: typeof VSTManager !== 'undefined' ? VSTManager : null

    // Stato progetto
    property bool projectModified: false
    property string projectName: "Untitled.daw"
    property real tempo: 120.0
    property int timeSignatureNumerator: 4
    property int timeSignatureDenominator: 4
    property int currentPattern: 0
    property int currentRow: 0
    property int currentTrack: 0
    property bool isPlaying: false
    property real playheadPosition: 0
    property int snapToGrid: 16 // ticks

    // Tracks
    property var tracks: []
    property var mixerChannels: []
    property var masterChannel: ({ volume: 1.0, muted: false })

    // Performance
    property real cpuUsage: 12
    property real asioLoad: 34
    property int bufferSize: 256
    property int sampleRate: 48000
    property int latencyMs: 5

    // UI State
    property bool showMixer: true
    property bool showPianoRoll: false
    property bool showBrowser: true
    property bool showInspector: true
    property real timelineZoom: 1.0
    property int quantization: 16 // 1/16 notes

    // ============================================
    // 3. TITLE BAR PERSONALIZZATA (da audioeditor)
    // ============================================

    Rectangle {
        id: titleBar
        height: 48
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        color: theme.panelDark
        z: 999

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16

            // Logo e titolo
            RowLayout {
                spacing: 12

                Rectangle {
                    width: 32
                    height: 32
                    radius: 8
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: theme.accent }
                        GradientStop { position: 1.0; color: theme.accentSecondary }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "DAW"
                        font.bold: true
                        font.pixelSize: 14
                        color: "white"
                    }
                }

                ColumnLayout {
                    spacing: 2

                    Text {
                        text: dawWindow.title
                        color: theme.textPrimary
                        font.pixelSize: 14
                        font.bold: true
                    }

                    RowLayout {
                        spacing: 8
                        visible: isPlaying

                        Text {
                            text: formatTime(playheadPosition)
                            color: theme.playhead
                            font.family: "Monospace"
                            font.pixelSize: 11
                        }

                        Rectangle {
                            width: 4
                            height: 4
                            radius: 2
                            color: theme.success
                            visible: isPlaying

                            SequentialAnimation on opacity {
                                loops: Animation.Infinite
                                NumberAnimation { from: 1; to: 0.3; duration: 500 }
                                NumberAnimation { from: 0.3; to: 1; duration: 500 }
                            }
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Trasporto rapido
            RowLayout {
                spacing: 4

                ToolButtonEx {
                    iconSource: "qrc:/icons/metronome.svg"
                    text: ""
                    tooltip: "Metronome"
                }

                ToolButtonEx {
                    iconSource: "qrc:/icons/quantize.svg"
                    text: ""
                    tooltip: "Quantize: 1/16"
                }

                Rectangle {
                    width: 1
                    height: 24
                    color: theme.border
                }

                // BPM display
                Rectangle {
                    height: 32
                    width: 100
                    color: theme.surface2
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 4

                        Text {
                            text: "BPM"
                            color: theme.textSecondary
                            font.pixelSize: 10
                        }

                        SpinBox {
                            from: 20
                            to: 300
                            value: tempo
                            onValueModified: tempo = value
                            background: Rectangle { color: "transparent" }
                            Layout.fillWidth: true
                        }
                    }
                }

                Rectangle {
                    width: 1
                    height: 24
                    color: theme.border
                }

                // Window controls
                RoundButtonEx { text: "−"; onClicked: dawWindow.showMinimized() }
                RoundButtonEx {
                    text: dawWindow.visibility === Window.Maximized ? "□" : "□"
                    onClicked: dawWindow.visibility === Window.Maximized ?
                    dawWindow.showNormal() : dawWindow.showMaximized()
                }
                RoundButtonEx { text: "×"; onClicked: dawWindow.close() }
            }
        }
    }

    // ============================================
    // 4. TOOLBAR PRINCIPALE (Ribbon style)
    // ============================================

    Rectangle {
        id: mainToolbar
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 80
        color: theme.surface
        border.bottom: 1
        border.color: theme.border

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 16

            // File Group
            ToolGroup {
                title: "File"
                RowLayout {
                    spacing: 4
                    ToolButtonEx { iconSource: "qrc:/icons/new.svg"; text: "New" }
                    ToolButtonEx { iconSource: "qrc:/icons/open.svg"; text: "Open" }
                    ToolButtonEx { iconSource: "qrc:/icons/save.svg"; text: "Save" }
                    ToolButtonEx { iconSource: "qrc:/icons/export.svg"; text: "Export" }
                }
            }

            // Edit Group
            ToolGroup {
                title: "Edit"
                RowLayout {
                    spacing: 4
                    ToolButtonEx { iconSource: "qrc:/icons/undo.svg"; text: "Undo" }
                    ToolButtonEx { iconSource: "qrc:/icons/redo.svg"; text: "Redo" }
                    ToolButtonEx { iconSource: "qrc:/icons/cut.svg"; text: "Cut" }
                    ToolButtonEx { iconSource: "qrc:/icons/copy.svg"; text: "Copy" }
                    ToolButtonEx { iconSource: "qrc:/icons/paste.svg"; text: "Paste" }
                }
            }

            // Transport Group
            ToolGroup {
                title: "Transport"
                RowLayout {
                    spacing: 8

                    Rectangle {
                        width: 48
                        height: 48
                        radius: 24
                        color: isPlaying ? theme.warning : theme.accent

                        IconButton {
                            anchors.centerIn: parent
                            iconSource: isPlaying ? "qrc:/icons/pause.svg" : "qrc:/icons/play.svg"
                            iconColor: "white"
                            iconSize: 24
                            onClicked: togglePlayback()
                        }
                    }

                    Rectangle {
                        width: 40
                        height: 40
                        radius: 20
                        color: theme.surface3

                        IconButton {
                            anchors.centerIn: parent
                            iconSource: "qrc:/icons/stop.svg"
                            iconSize: 20
                            onClicked: stopPlayback()
                        }
                    }

                    Rectangle {
                        width: 40
                        height: 40
                        radius: 20
                        color: theme.surface3

                        IconButton {
                            anchors.centerIn: parent
                            iconSource: "qrc:/icons/record.svg"
                            iconSize: 20
                            onClicked: toggleRecording()
                        }
                    }
                }
            }

            // MIDI Group
            ToolGroup {
                title: "MIDI"
                RowLayout {
                    spacing: 4
                    ToolButtonEx { iconSource: "qrc:/icons/piano.svg"; text: "Piano Roll" }
                    ToolButtonEx { iconSource: "qrc:/icons/quantize.svg"; text: "Quantize" }
                    ToolButtonEx { iconSource: "qrc:/icons/arpeggiator.svg"; text: "Arp" }
                }
            }

            // View Group
            ToolGroup {
                title: "View"
                RowLayout {
                    spacing: 4
                    ToolButtonEx {
                        text: "Mixer"
                        highlighted: showMixer
                        onClicked: showMixer = !showMixer
                    }
                    ToolButtonEx {
                        text: "Piano"
                        highlighted: showPianoRoll
                        onClicked: showPianoRoll = !showPianoRoll
                    }
                    ToolButtonEx {
                        text: "Browser"
                        highlighted: showBrowser
                        onClicked: showBrowser = !showBrowser
                    }
                }
            }
        }
    }

    // ============================================
    // 5. MAIN LAYOUT (SplitView a 3 colonne)
    // ============================================

    SplitView {
        anchors.top: mainToolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        orientation: Qt.Horizontal

        // LEFT PANEL - Browser (Strumenti, Loop, Plugin)
        Rectangle {
            id: browserPanel
            SplitView.preferredWidth: showBrowser ? 280 : 0
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: 400
            visible: showBrowser
            color: theme.panel
            border.right: 1
            border.color: theme.border

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                TabBar {
                    id: browserTabs
                    Layout.fillWidth: true
                    background: Rectangle { color: theme.panelDark }

                    TabButton { text: "🔊 Instruments" }
                    TabButton { text: "🎛️ Effects" }
                    TabButton { text: "🎵 Loops" }
                    TabButton { text: "📁 Files" }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: browserTabs.currentIndex

                    // Instruments Tab (da middleware)
                    ScrollView {
                        clip: true
                        ColumnLayout {
                            width: parent.width
                            spacing: 2

                            Repeater {
                                model: [
                                    { name: "Kontakt", type: "Sampler", icon: "🎹" },
                                    { name: "Massive", type: "Synth", icon: "🎛️" },
                                    { name: "FM8", type: "FM Synth", icon: "📻" },
                                    { name: "Battery", type: "Drum", icon: "🥁" }
                                ]

                                delegate: BrowserItem {
                                    name: modelData.name
                                    type: modelData.type
                                    icon: modelData.icon
                                }
                            }
                        }
                    }

                    // Effects Tab
                    ScrollView {
                        clip: true
                        ColumnLayout {
                            width: parent.width
                            spacing: 2

                            Repeater {
                                model: [
                                    { name: "Compressor", type: "Dynamic", icon: "📊" },
                                    { name: "Reverb", type: "Space", icon: "🏛️" },
                                    { name: "Delay", type: "Time", icon: "⏱️" },
                                    { name: "EQ", type: "Filter", icon: "🎚️" }
                                ]

                                delegate: BrowserItem {
                                    name: modelData.name
                                    type: modelData.type
                                    icon: modelData.icon
                                }
                            }
                        }
                    }

                    // Loops Tab
                    ScrollView {
                        clip: true
                        ColumnLayout {
                            width: parent.width
                            spacing: 2

                            Repeater {
                                model: 20
                                delegate: BrowserItem {
                                    name: "Loop " + (index + 1)
                                    type: "Audio Loop"
                                    icon: "🎵"
                                }
                            }
                        }
                    }

                    // Files Tab
                    ScrollView {
                        clip: true
                        ColumnLayout {
                            width: parent.width
                            spacing: 2

                            Repeater {
                                model: recentFiles
                                delegate: BrowserItem {
                                    name: modelData.name
                                    type: modelData.type
                                    icon: getFileIcon(modelData.type)
                                }
                            }
                        }
                    }
                }

                // Quick browser info
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: theme.panelDark
                    border.top: 1
                    border.color: theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8

                        Text {
                            text: "🔍 Search"
                            color: theme.textSecondary
                            font.pixelSize: 11
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "256 items"
                            color: theme.textMuted
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }

        // CENTER PANEL - Timeline & Piano Roll
        Rectangle {
            SplitView.fillWidth: true
            color: theme.background

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Timeline Ruler (da audioeditor/videoeditor)
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: theme.panelDark
                    border.bottom: 1
                    border.color: theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 60 // spazio per track headers

                        // Time markers
                        Repeater {
                            model: 20
                            delegate: Rectangle {
                                x: index * 100 * timelineZoom
                                width: 1
                                height: index % 4 === 0 ? 12 : 6
                                color: theme.textMuted

                                Text {
                                    anchors.top: parent.bottom
                                    anchors.topMargin: 4
                                    text: index + ":" + "00"
                                    color: theme.textSecondary
                                    font.pixelSize: 9
                                    visible: index % 4 === 0
                                }
                            }
                        }

                        // Playhead
                        Rectangle {
                            id: playhead
                            x: playheadPosition * timelineZoom - 1
                            width: 2
                            height: parent.height
                            color: theme.playhead

                            Rectangle {
                                anchors.top: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 10
                                height: 6
                                color: theme.playhead
                                rotation: 180
                            }
                        }
                    }
                }

                // Tracks Area
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    RowLayout {
                        spacing: 0

                        // Track Headers
                        ColumnLayout {
                            Layout.preferredWidth: 60
                            spacing: 1

                            Repeater {
                                model: 8
                                delegate: Rectangle {
                                    width: 60
                                    height: 80
                                    color: index % 2 === 0 ? theme.surface2 : theme.surface3
                                    border.bottom: 1
                                    border.color: theme.border

                                    ColumnLayout {
                                        anchors.centerIn: parent
                                        spacing: 2

                                        Text {
                                            text: "Track " + (index + 1)
                                            color: theme.textPrimary
                                            font.bold: true
                                            font.pixelSize: 11
                                        }

                                        Rectangle {
                                            width: 40
                                            height: 4
                                            color: getTrackColor(index)
                                            radius: 2
                                        }
                                    }
                                }
                            }
                        }

                        // Timeline Grid
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: theme.background

                            GridView {
                                anchors.fill: parent
                                cellWidth: 100 * timelineZoom
                                cellHeight: 80

                                model: 160 // 8 tracks * 20 measures
                                delegate: Rectangle {
                                    width: 100 * timelineZoom - 1
                                    height: 79
                                    color: index % 8 === 0 ? theme.surface2 : "transparent"
                                    border.right: 1
                                    border.bottom: 1
                                    border.color: theme.border

                                    // Clip placeholder
                                    Rectangle {
                                        visible: Math.random() > 0.7
                                        anchors.fill: parent
                                        anchors.margins: 2
                                        color: getTrackColor(Math.floor(index / 20))
                                        opacity: 0.3
                                        radius: 4

                                        Text {
                                            anchors.centerIn: parent
                                            text: "MIDI"
                                            color: "white"
                                            font.pixelSize: 10
                                            font.bold: true
                                        }
                                    }
                                }
                            }

                            // Grid lines
                            Repeater {
                                model: 20
                                delegate: Rectangle {
                                    x: index * 100 * timelineZoom
                                    width: 1
                                    height: parent.height
                                    color: theme.border
                                    opacity: index % 4 === 0 ? 0.5 : 0.2
                                }
                            }
                        }
                    }
                }

                // Transport Controls (inferiore)
                Rectangle {
                    Layout.fillWidth: true
                    height: 50
                    color: theme.panelDark
                    border.top: 1
                    border.color: theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8

                        // Zoom controls
                        RowLayout {
                            ToolButtonEx {
                                iconSource: "qrc:/icons/zoom-out.svg"
                                text: ""
                                onClicked: timelineZoom = Math.max(0.5, timelineZoom - 0.2)
                            }

                            Text {
                                text: Math.round(timelineZoom * 100) + "%"
                                color: theme.textSecondary
                                font.family: "Monospace"
                            }

                            ToolButtonEx {
                                iconSource: "qrc:/icons/zoom-in.svg"
                                text: ""
                                onClicked: timelineZoom = Math.min(4.0, timelineZoom + 0.2)
                            }
                        }

                        Item { Layout.fillWidth: true }

                        // Snap/Quantize
                        RowLayout {
                            Text {
                                text: "Snap: 1/16"
                                color: theme.textSecondary
                                font.pixelSize: 11
                            }
                        }

                        // Position
                        Text {
                            text: formatTime(playheadPosition) + " / " + formatTime(dawDuration)
                            color: theme.textPrimary
                            font.family: "Monospace"
                            font.bold: true
                        }
                    }
                }
            }
        }

        // RIGHT PANEL - Inspector & Mixer
        Rectangle {
            id: inspectorPanel
            SplitView.preferredWidth: showInspector ? 320 : 0
            SplitView.minimumWidth: 250
            SplitView.maximumWidth: 500
            visible: showInspector
            color: theme.panel
            border.left: 1
            border.color: theme.border

            TabBar {
                id: inspectorTabs
                width: parent.width
                background: Rectangle { color: theme.panelDark }

                TabButton { text: "🎚️ Mixer" }
                TabButton { text: "📊 Inspector" }
                TabButton { text: "🎛️ Plugin" }
            }

            StackLayout {
                anchors.top: inspectorTabs.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                currentIndex: inspectorTabs.currentIndex

                // MIXER PANEL (da middleware)
                MixerPanel {
                    visible: inspectorTabs.currentIndex === 0
                    tracks: tracks
                    masterChannel: masterChannel
                }

                // INSPECTOR PANEL (da audioeditor)
                InspectorPanel {
                    visible: inspectorTabs.currentIndex === 1
                    currentTrack: currentTrack
                }

                // PLUGIN CHAIN
                PluginChainPanel {
                    visible: inspectorTabs.currentIndex === 2
                    currentTrack: currentTrack
                }
            }
        }
    }

    // ============================================
    // 6. STATUS BAR (Fusione audioeditor + middleware)
    // ============================================

    Rectangle {
        id: statusBar
        height: 32
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        color: theme.panelDark
        border.top: 1
        border.color: theme.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 24

            // Audio status
            RowLayout {
                spacing: 12

                StatusBadge {
                    text: sampleRate / 1000 + " kHz"
                    iconSource: "qrc:/icons/sample-rate.svg"
                }

                StatusBadge {
                    text: bufferSize + " samples"
                    iconSource: "qrc:/icons/buffer.svg"
                }

                StatusBadge {
                    text: latencyMs + " ms"
                    iconSource: "qrc:/icons/latency.svg"
                    highlight: latencyMs > 10
                }
            }

            // Performance
            RowLayout {
                spacing: 16

                StatusBadge {
                    text: "CPU: " + cpuUsage + "%"
                    iconSource: "qrc:/icons/cpu.svg"
                    color: cpuUsage > 80 ? theme.error : theme.textSecondary
                }

                StatusBadge {
                    text: "ASIO: " + asioLoad + "%"
                    iconSource: "qrc:/icons/asio.svg"
                }
            }

            Item { Layout.fillWidth: true }

            // MIDI
            StatusBadge {
                text: "MIDI: Active"
                iconSource: "qrc:/icons/midi.svg"
                highlight: true
            }

            // Tempo
            StatusBadge {
                text: tempo.toFixed(1) + " BPM"
                iconSource: "qrc:/icons/tempo.svg"
            }

            // Time signature
            StatusBadge {
                text: timeSignatureNumerator + "/" + timeSignatureDenominator
                iconSource: "qrc:/icons/time-sig.svg"
            }
        }
    }

    // ============================================
    // 7. COMPONENTI RIUTILIZZABILI
    // ============================================

    component ToolGroup: ColumnLayout {
        property string title

        spacing: 4

        Text {
            text: parent.title
            color: theme.textSecondary
            font.pixelSize: 10
            font.uppercase: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: theme.surface2
            radius: 4

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4
                children: parent.children
            }
        }
    }

    component ToolButtonEx: Button {
        property string iconSource
        property string tooltip: text
        property bool highlighted: false

        implicitWidth: 40
        implicitHeight: 40
        flat: true

        contentItem: ColumnLayout {
            spacing: 2

            Image {
                source: parent.iconSource
                sourceSize.width: 20
                sourceSize.height: 20
                Layout.alignment: Qt.AlignHCenter
                visible: status === Image.Ready
            }

            Text {
                text: parent.text
                color: parent.highlighted ? theme.accent : theme.textSecondary
                font.pixelSize: 9
                Layout.alignment: Qt.AlignHCenter
                visible: parent.text.length > 0
            }
        }

        background: Rectangle {
            radius: 4
            color: parent.hovered ? theme.surface3 : "transparent"
        }

        ToolTip.visible: hovered && tooltip.length > 0
        ToolTip.text: tooltip
        ToolTip.delay: 500
    }

    component RoundButtonEx: Button {
        property string label

        implicitWidth: 32
        implicitHeight: 32
        flat: true

        contentItem: Text {
            text: parent.label
            color: parent.hovered ? theme.accent : theme.textSecondary
            font.pixelSize: 16
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component BrowserItem: Rectangle {
        property string name
        property string type
        property string icon

        width: parent.width
        height: 48
        color: mouseArea.containsMouse ? theme.surface3 : "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 12

            Text {
                text: icon
                font.pixelSize: 20
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: name
                    color: theme.textPrimary
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Text {
                    text: type
                    color: theme.textSecondary
                    font.pixelSize: 10
                }
            }

            Button {
                text: "+"
                flat: true
                onClicked: addToProject(name)
                visible: mouseArea.containsMouse
            }
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: selectBrowserItem(name)
        }
    }

    component MixerPanel: Rectangle {
        property var tracks: []
        property var masterChannel: ({})

        color: "transparent"

        Flickable {
            anchors.fill: parent
            contentWidth: column.width
            contentHeight: column.height
            clip: true

            RowLayout {
                id: column
                spacing: 4

                // Track channels
                Repeater {
                    model: tracks.length

                    delegate: MixerChannel {
                        channelName: "Track " + (index + 1)
                        channelIndex: index
                        color: getTrackColor(index)
                        volume: tracks[index] ? tracks[index].volume : 1.0
                        pan: tracks[index] ? tracks[index].pan : 0.0
                        muted: tracks[index] ? tracks[index].muted : false
                        soloed: tracks[index] ? tracks[index].soloed : false
                        level: getTrackLevel(index)
                    }
                }

                // Master channel
                MixerChannel {
                    channelName: "MASTER"
                    isMaster: true
                    color: theme.accent
                    volume: masterChannel ? masterChannel.volume : 1.0
                    muted: masterChannel ? masterChannel.muted : false
                    level: getMasterLevel()
                }
            }
        }
    }

    component MixerChannel: Rectangle {
        property string channelName
        property int channelIndex: -1
        property color channelColor: theme.accentSecondary
        property real volume: 1.0
        property real pan: 0.0
        property bool muted: false
        property bool soloed: false
        property real level: 0
        property bool isMaster: false

        width: 80
        height: parent ? parent.height : 400
        color: theme.surface2
        border.color: theme.border
        border.width: 1
        radius: 4

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            // Channel name
            Text {
                text: channelName
                color: isMaster ? theme.accent : theme.textPrimary
                font.bold: true
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            // VU Meter
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.surface3
                radius: 2

                // Vertical meter
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width * 0.6
                    height: parent.height * level
                    color: getMeterColor(level)
                    radius: 2

                    // Peak indicator
                    Rectangle {
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width
                        height: 2
                        color: "white"
                        visible: level > 0.9
                    }
                }
            }

            // Volume fader
            Slider {
                orientation: Qt.Vertical
                from: 0
                to: 1.5
                value: volume
                onMoved: setTrackVolume(channelIndex, value)
                Layout.preferredHeight: 100
                Layout.alignment: Qt.AlignHCenter
            }

            // Pan knob
            Dial {
                from: -1
                to: 1
                value: pan
                onMoved: setTrackPan(channelIndex, value)
                Layout.alignment: Qt.AlignHCenter
                enabled: !isMaster
            }

            // Mute/Solo buttons
            RowLayout {
                spacing: 4
                Layout.alignment: Qt.AlignHCenter

                Rectangle {
                    width: 24
                    height: 24
                    radius: 12
                    color: muted ? theme.error : theme.surface3
                    border.color: theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "M"
                        color: "white"
                        font.pixelSize: 10
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: toggleMute(channelIndex)
                    }
                }

                Rectangle {
                    width: 24
                    height: 24
                    radius: 12
                    color: soloed ? theme.warning : theme.surface3
                    border.color: theme.border
                    visible: !isMaster

                    Text {
                        anchors.centerIn: parent
                        text: "S"
                        color: "white"
                        font.pixelSize: 10
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: toggleSolo(channelIndex)
                    }
                }
            }
        }
    }

    component InspectorPanel: Rectangle {
        property int currentTrack: 0

        color: "transparent"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 20

            Text {
                text: "TRACK PROPERTIES"
                color: theme.textSecondary
                font.bold: true
                font.pixelSize: 11
                font.uppercase: true
            }

            GridLayout {
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Layout.fillWidth: true

                Text { text: "Name:"; color: theme.textSecondary }
                TextField {
                    text: "Track " + (currentTrack + 1)
                    Layout.fillWidth: true
                }

                Text { text: "Volume:"; color: theme.textSecondary }
                Slider {
                    from: 0
                    to: 1.5
                    value: 1.0
                    Layout.fillWidth: true
                }

                Text { text: "Pan:"; color: theme.textSecondary }
                Slider {
                    from: -1
                    to: 1
                    value: 0
                    Layout.fillWidth: true
                }

                Text { text: "Output:"; color: theme.textSecondary }
                ComboBox {
                    model: ["Master", "Bus 1", "Bus 2"]
                    currentIndex: 0
                    Layout.fillWidth: true
                }

                Text { text: "Color:"; color: theme.textSecondary }
                Rectangle {
                    width: 24
                    height: 24
                    radius: 4
                    color: getTrackColor(currentTrack)

                    MouseArea {
                        anchors.fill: parent
                        onClicked: colorDialog.open()
                    }
                }
            }

            GroupBox {
                title: "Input"
                Layout.fillWidth: true

                GridLayout {
                    columns: 2
                    width: parent.width

                    Text { text: "Source:"; color: theme.textSecondary }
                    ComboBox {
                        model: ["None", "Mic 1", "Mic 2", "Line In", "MIDI"]
                        currentIndex: 0
                        Layout.fillWidth: true
                    }

                    Text { text: "Gain:"; color: theme.textSecondary }
                    SpinBox {
                        from: -20
                        to: 20
                        value: 0
                        suffix: " dB"
                    }
                }
            }

            GroupBox {
                title: "MIDI"
                Layout.fillWidth: true

                GridLayout {
                    columns: 2
                    width: parent.width

                    Text { text: "Channel:"; color: theme.textSecondary }
                    SpinBox {
                        from: 1
                        to: 16
                        value: 1
                    }

                    Text { text: "Transpose:"; color: theme.textSecondary }
                    SpinBox {
                        from: -24
                        to: 24
                        value: 0
                        suffix: " semitones"
                    }

                    Text { text: "Velocity:"; color: theme.textSecondary }
                    SpinBox {
                        from: 0
                        to: 127
                        value: 100
                    }
                }
            }
        }
    }

    component PluginChainPanel: Rectangle {
        property int currentTrack: 0

        color: "transparent"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Text {
                text: "PLUGIN CHAIN"
                color: theme.textSecondary
                font.bold: true
                font.pixelSize: 11
                font.uppercase: true
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: ListModel {
                    id: pluginsModel
                }
                spacing: 4

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 48
                    color: theme.surface3
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 12

                        Text {
                            text: modelData.type === "fx" ? "🎛️" : "🎹"
                            font.pixelSize: 16
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: modelData.name
                                color: theme.textPrimary
                                font.bold: true
                                font.pixelSize: 12
                            }

                            Text {
                                text: modelData.vendor
                                color: theme.textSecondary
                                font.pixelSize: 10
                            }
                        }

                        Switch {
                            checked: modelData.active
                            onToggled: togglePlugin(modelData.id, checked)
                        }

                        Button {
                            text: "✕"
                            flat: true
                            onClicked: removePlugin(modelData.id)
                        }
                    }
                }
            }

            Button {
                text: "Add Plugin..."
                Layout.fillWidth: true
                onClicked: pluginBrowser.open()
            }
        }

        Component.onCompleted: {
            pluginsModel.append({ name: "Compressor", vendor: "Waves", type: "fx", active: true, id: "comp1" })
            pluginsModel.append({ name: "Reverb", vendor: "Valhalla", type: "fx", active: true, id: "rev1" })
            pluginsModel.append({ name: "EQ", vendor: "FabFilter", type: "fx", active: false, id: "eq1" })
        }
    }

    component StatusBadge: Rectangle {
        property string label
        property string iconSource
        property bool highlight: false
        property color textColor: theme.textSecondary

        height: 24
        width: row.implicitWidth + 16
        color: highlight ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.1) : "transparent"
        radius: 12
        border.color: highlight ? theme.accent : "transparent"
        border.width: 1

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 4

            Image {
                source: parent.parent.iconSource
                sourceSize.width: 12
                sourceSize.height: 12
                visible: status === Image.Ready
            }

            Text {
                text: parent.parent.label
                color: parent.parent.highlight ? theme.accent : parent.parent.textColor
                font.pixelSize: 11
            }
        }
    }

    // ============================================
    // 8. FUNZIONI UTILITY
    // ============================================

    property real dawDuration: 300 // seconds

    function formatTime(seconds) {
        var mins = Math.floor(seconds / 60)
        var secs = Math.floor(seconds % 60)
        var ms = Math.floor((seconds % 1) * 100)
        return mins.toString().padStart(2, '0') + ":" +
        secs.toString().padStart(2, '0') + "." +
        ms.toString().padStart(2, '0')
    }

    function togglePlayback() {
        isPlaying = !isPlaying
        if (isPlaying) {
            transportTimer.start()
        } else {
            transportTimer.stop()
        }
    }

    function stopPlayback() {
        isPlaying = false
        playheadPosition = 0
        transportTimer.stop()
    }

    function toggleRecording() {
        showNotification("Recording", "Recording " + (isRecording ? "stopped" : "started"))
    }

    function getTrackColor(index) {
        var colors = [theme.accent, "#FFB74D", "#64B5F6", "#E57373", "#BA68C8", "#4FC3F7", "#81C784", "#FF8A65"]
        return colors[index % colors.length]
    }

    function getTrackLevel(index) {
        return 0.3 + Math.random() * 0.4 // Simulated level
    }

    function getMasterLevel() {
        return 0.4 + Math.random() * 0.3
    }

    function getMeterColor(level) {
        if (level > 0.9) return theme.error
            if (level > 0.7) return theme.warning
                return theme.success
    }

    function setTrackVolume(index, value) {
        if (tracks[index]) tracks[index].volume = value
    }

    function setTrackPan(index, value) {
        if (tracks[index]) tracks[index].pan = value
    }

    function toggleMute(index) {
        if (tracks[index]) tracks[index].muted = !tracks[index].muted
    }

    function toggleSolo(index) {
        if (tracks[index]) tracks[index].soloed = !tracks[index].soloed
    }

    function addToProject(item) {
        showNotification("Added", "Added " + item + " to project")
    }

    function selectBrowserItem(item) {
        console.log("Selected:", item)
    }

    function showNotification(title, message) {
        notificationDialog.title = title
        notificationDialog.message = message
        notificationDialog.open()
    }

    // Timers
    Timer {
        id: transportTimer
        interval: 50
        running: false
        repeat: true
        onTriggered: {
            playheadPosition += 0.05
            if (playheadPosition > dawDuration) {
                playheadPosition = 0
                isPlaying = false
                stop()
            }
        }
    }

    Timer {
        interval: 100
        running: true
        repeat: true
        onTriggered: {
            // Update meter levels (simulated)
            for (var i = 0; i < tracks.length; i++) {
                tracks[i].level = 0.2 + Math.random() * 0.3
            }
        }
    }

    // Initialization
    Component.onCompleted: {
        // Initialize tracks
        for (var i = 0; i < 8; i++) {
            tracks.push({
                volume: 1.0,
                pan: 0.0,
                muted: false,
                soloed: false,
                level: 0.1
            })
        }
    }

    // Dialogs
    Dialog {
        id: notificationDialog
        title: "Notification"
        standardButtons: Dialog.Ok

        Text {
            text: notificationDialog.message
            wrapMode: Text.Wrap
        }
    }

    Dialog {
        id: pluginBrowser
        title: "Add Plugin"
        width: 500
        height: 400
        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    ColorDialog {
        id: colorDialog
        title: "Select Track Color"
    }

    // Recent files (simulated)
    property var recentFiles: [
        { name: "Project1.daw", type: "project", icon: "📁" },
        { name: "Loop1.wav", type: "audio", icon: "🎵" },
        { name: "SynthPatch.vst", type: "plugin", icon: "🎛️" }
    ]

    function getFileIcon(type) {
        var icons = { "project": "📁", "audio": "🎵", "plugin": "🎛️", "midi": "🎹" }
        return icons[type] || "📄"
    }
}
