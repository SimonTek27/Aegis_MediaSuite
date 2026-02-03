// ui_middleware.qml - FMOD Studio-like Professional Audio Middleware Interface
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Shapes
import Qt.labs.platform as Platform

ApplicationWindow {
    id: middlewareWindow
    visible: true
    width: 1600
    height: 900
    minimumWidth: 1200
    minimumHeight: 700
    title: "Aegis Audio Middleware" + (projectModified ? " *" : "")
    color: "#1e1e1e"

    // FMOD-style dark theme
    property var theme: {
        "background": "#1e1e1e",
        "panel": "#252526",
        "panelDark": "#2d2d30",
        "panelDarker": "#1e1e1e",
        "accent": "#007acc",  // FMOD blue
        "accentHover": "#3e9ddd",
        "accentPressed": "#005a9e",
        "textPrimary": "#d4d4d4",
        "textSecondary": "#858585",
        "textDisabled": "#3e3e42",
        "error": "#f14c4c",
        "warning": "#cca700",
        "success": "#3794ff",
        "border": "#3e3e42",
        "grid": "#2d2d30"
    }

    // Backend reference
    property var middlewareBackend: typeof AudioMiddleware !== 'undefined' ? AudioMiddleware : null

    // Project state
    property bool projectModified: false
    property string projectName: "Untitled.aegismiddleware"
    property var projectEndpoints: []
    property var projectBuses: []
    property var projectEvents: []
    property var projectParameters: []

    // UI state
    property int currentTab: 0  // 0=Events, 1=Timeline, 2=Assets, 3=Mixer
    property string selectedEndpoint: ""
    property var selectedEvents: []
    property bool isPlaying: false
    property int transportPosition: 0

    // Audio monitoring
    property var endpointLevels: ({})
    property var busLevels: ({})
    property var cpuUsage: 0

    // Initialize
    Component.onCompleted: {
        loadDefaultProject()
        startMonitoring()
    }

    // Menu Bar (FMOD style)
    menuBar: MenuBar {
        Menu {
            title: "File"
            Action {
                text: "New Project"
                shortcut: "Ctrl+N"
                onTriggered: newProject()
            }
            Action {
                text: "Open Project..."
                shortcut: "Ctrl+O"
                onTriggered: openProjectDialog.open()
            }
            Action {
                text: "Save Project"
                shortcut: "Ctrl+S"
                onTriggered: saveProject()
            }
            Action {
                text: "Save Project As..."
                shortcut: "Ctrl+Shift+S"
                onTriggered: saveProjectAsDialog.open()
            }
            MenuSeparator {}
            Action {
                text: "Build Bank..."
                onTriggered: buildBankDialog.open()
            }
            Action {
                text: "Export Project..."
                onTriggered: exportDialog.open()
            }
            MenuSeparator {}
            Action {
                text: "Exit"
                onTriggered: middlewareWindow.close()
            }
        }

        Menu {
            title: "Edit"
            Action {
                text: "Create Event"
                shortcut: "Ctrl+E"
                onTriggered: createEventDialog.open()
            }
            Action {
                text: "Create Endpoint"
                shortcut: "Ctrl+Shift+E"
                onTriggered: createEndpointDialog.open()
            }
            Action {
                text: "Create Bus"
                shortcut: "Ctrl+B"
                onTriggered: createBusDialog.open()
            }
            MenuSeparator {}
            Action {
                text: "Project Settings..."
                onTriggered: projectSettingsDialog.open()
            }
            Action {
                text: "Preferences..."
                onTriggered: preferencesDialog.open()
            }
        }

        Menu {
            title: "View"
            Action {
                text: "Master Mixer"
                shortcut: "F11"
                onTriggered: currentTab = 3
            }
            Action {
                text: "Event Editor"
                shortcut: "F12"
                onTriggered: currentTab = 0
            }
            Action {
                text: "Timeline"
                shortcut: "Ctrl+T"
                onTriggered: currentTab = 1
            }
            MenuSeparator {}
            Action {
                text: "Show Meters"
                checkable: true
                checked: true
            }
            Action {
                text: "Show 3D Preview"
                checkable: true
                checked: false
            }
            Action {
                text: "Show CPU Monitor"
                checkable: true
                checked: true
            }
        }

        Menu {
            title: "Transport"
            Action {
                text: isPlaying ? "Stop" : "Play"
                shortcut: "Space"
                onTriggered: toggleTransport()
            }
            Action {
                text: "Pause"
                shortcut: "P"
                enabled: isPlaying
                onTriggered: pauseTransport()
            }
            Action {
                text: "Rewind"
                shortcut: "R"
                onTriggered: rewindTransport()
            }
            Action {
                text: "Go to Start"
                shortcut: "Home"
                onTriggered: transportPosition = 0
            }
            Action {
                text: "Go to End"
                shortcut: "End"
                onTriggered: transportPosition = 100000
            }
        }

        Menu {
            title: "Bridging"
            Action {
                text: "Bridge to Unity"
                onTriggered: bridgeToUnity()
            }
            Action {
                text: "Bridge to Unreal"
                onTriggered: bridgeToUnreal()
            }
            Action {
                text: "Bridge to OBS"
                onTriggered: bridgeToOBS()
            }
            Action {
                text: "Bridge to Zoom"
                onTriggered: bridgeToZoom()
            }
            MenuSeparator {}
            Action {
                text: "Create Virtual Device..."
                onTriggered: createVirtualDeviceDialog.open()
            }
        }

        Menu {
            title: "Help"
            Action {
                text: "Documentation"
                shortcut: "F1"
                onTriggered: openDocumentation()
            }
            Action {
                text: "Audio Routing Guide"
                onTriggered: openRoutingGuide()
            }
            MenuSeparator {}
            Action {
                text: "About Aegis Middleware"
                onTriggered: aboutDialog.open()
            }
        }
    }

    // Main Layout
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top Toolbar (FMOD style)
        Rectangle {
            id: topToolbar
            Layout.fillWidth: true
            height: 36
            color: theme.panelDark
            border { color: theme.border; width: 1 }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 10

                // Transport controls
                Row {
                    spacing: 4
                    Layout.alignment: Qt.AlignVCenter

                    TransportButton {
                        icon: "⏮"
                        tooltip: "Go to Start"
                        onClicked: transportPosition = 0
                    }

                    TransportButton {
                        icon: "⏪"
                        tooltip: "Rewind"
                        onClicked: rewindTransport()
                    }

                    TransportButton {
                        icon: isPlaying ? "⏸" : "▶"
                        tooltip: isPlaying ? "Stop" : "Play"
                        backgroundColor: isPlaying ? theme.accent : "transparent"
                        onClicked: toggleTransport()
                    }

                    TransportButton {
                        icon: "⏹"
                        tooltip: "Stop All Events"
                        onClicked: stopAllEvents()
                    }

                    TransportButton {
                        icon: "⏩"
                        tooltip: "Fast Forward"
                        onClicked: fastForwardTransport()
                    }

                    TransportButton {
                        icon: "⏭"
                        tooltip: "Go to End"
                        onClicked: transportPosition = 100000
                    }
                }

                // Time display
                Rectangle {
                    width: 100
                    height: 24
                    color: theme.panel
                    radius: 2
                    border.color: theme.border

                    Text {
                        anchors.centerIn: parent
                        text: formatTime(transportPosition)
                        color: theme.textPrimary
                        font.family: "Consolas, monospace"
                        font.pixelSize: 11
                    }
                }

                // CPU meter
                Rectangle {
                    width: 80
                    height: 20
                    color: "transparent"
                    border.color: theme.border
                    radius: 2

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * (cpuUsage / 100)
                        color: cpuUsage > 80 ? theme.error : (cpuUsage > 50 ? theme.warning : theme.success)
                        radius: 2
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "CPU: " + Math.round(cpuUsage) + "%"
                        color: "white"
                        font.pixelSize: 9
                        font.bold: true
                    }
                }

                Item { Layout.fillWidth: true }

                // Project info
                Text {
                    text: projectName
                    color: theme.textSecondary
                    font.pixelSize: 11
                    font.italic: true
                }

                Rectangle {
                    width: 1
                    height: 20
                    color: theme.border
                }

                Text {
                    text: projectEndpoints.length + " Endpoints | " + projectEvents.length + " Events"
                    color: theme.textSecondary
                    font.pixelSize: 10
                }
            }
        }

        // Main Content Area
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Left Panel - Project Explorer (FMOD Browser style)
            Rectangle {
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                color: theme.panel
                border { color: theme.border; width: 1 }

                TabBar {
                    id: explorerTabs
                    width: parent.width
                    height: 28
                    currentIndex: 0

                    TabButton {
                        text: "Events"
                        width: 70
                        height: 28
                        background: Rectangle {
                            color: parent.checked ? theme.accent : "transparent"
                        }
                    }
                    TabButton {
                        text: "Buses"
                        width: 70
                        height: 28
                    }
                    TabButton {
                        text: "Assets"
                        width: 70
                        height: 28
                    }
                }

                StackLayout {
                    anchors.top: explorerTabs.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    currentIndex: explorerTabs.currentIndex

                    // Events tab
                    ScrollView {
                        id: eventsScroll
                        clip: true

                        ColumnLayout {
                            width: eventsScroll.width
                            spacing: 1

                            // Event categories
                            Repeater {
                                model: [
                                    { name: "Ambience", count: 4, color: "#4ec9b0" },
                                    { name: "Characters", count: 12, color: "#569cd6" },
                                    { name: "Footsteps", count: 8, color: "#c586c0" },
                                    { name: "UI", count: 6, color: "#9cdcfe" },
                                    { name: "Weapons", count: 15, color: "#ce9178" },
                                    { name: "Vehicles", count: 7, color: "#d7ba7d" }
                                ]

                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    height: 32
                                    color: mouseArea.containsMouse ? Qt.rgba(0, 0, 0, 0.1) : "transparent"

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 15
                                        spacing: 8

                                        Rectangle {
                                            Layout.preferredWidth: 4
                                            Layout.preferredHeight: 12
                                            color: modelData.color
                                            radius: 2
                                        }

                                        Text {
                                            text: modelData.name
                                            color: theme.textPrimary
                                            font.pixelSize: 12
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: modelData.count
                                            color: theme.textSecondary
                                            font.pixelSize: 10
                                            font.bold: true
                                        }
                                    }

                                    MouseArea {
                                        id: mouseArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: eventListModel.loadCategory(modelData.name)
                                    }
                                }
                            }

                            // Event list (populated when category selected)
                            Repeater {
                                model: ListModel { id: eventListModel }

                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    height: 28
                                    color: mouseArea2.containsMouse ? Qt.rgba(1, 0.55, 0, 0.05) : "transparent"

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 25
                                        spacing: 8

                                        Text {
                                            text: "▶"
                                            color: theme.textSecondary
                                            font.pixelSize: 10
                                            visible: model.isPlaying
                                        }

                                        Text {
                                            text: model.name
                                            color: theme.textPrimary
                                            font.pixelSize: 11
                                            Layout.fillWidth: true
                                        }

                                        Rectangle {
                                            width: 30
                                            height: 16
                                            color: theme.panelDark
                                            radius: 2

                                            Text {
                                                anchors.centerIn: parent
                                                text: model.type
                                                color: theme.textSecondary
                                                font.pixelSize: 8
                                                font.bold: true
                                            }
                                        }
                                    }

                                    MouseArea {
                                        id: mouseArea2
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: selectEvent(model.id)
                                        onDoubleClicked: previewEvent(model.id)
                                    }
                                }
                            }
                        }
                    }

                    // Buses tab
                    ScrollView {
                        id: busesScroll
                        clip: true

                        ColumnLayout {
                            width: busesScroll.width
                            spacing: 2

                            Repeater {
                                model: [
                                    { name: "Master", level: 0.7, muted: false, solo: false },
                                    { name: "Music", level: 0.5, muted: false, solo: false },
                                    { name: "SFX", level: 0.8, muted: false, solo: false },
                                    { name: "Dialog", level: 0.6, muted: false, solo: false },
                                    { name: "Ambience", level: 0.4, muted: false, solo: false }
                                ]

                                delegate: BusItem {
                                    Layout.fillWidth: true
                                    busName: modelData.name
                                    level: modelData.level
                                    isMuted: modelData.muted
                                    isSolo: modelData.solo
                                }
                            }
                        }
                    }

                    // Assets tab
                    ScrollView {
                        id: assetsScroll
                        clip: true

                        ColumnLayout {
                            width: assetsScroll.width
                            spacing: 2

                            Repeater {
                                model: [
                                    { name: "explosion.wav", duration: "2.3s", size: "2.4MB", type: "SFX" },
                                    { name: "bg_music.mp3", duration: "3:45", size: "8.2MB", type: "Music" },
                                    { name: "dialog_01.wav", duration: "4.1s", size: "3.8MB", type: "Voice" },
                                    { name: "footstep_grass.wav", duration: "0.8s", size: "800KB", type: "SFX" },
                                    { name: "car_engine.wav", duration: "10.2s", size: "12.1MB", type: "Ambience" }
                                ]

                                delegate: AssetItem {
                                    Layout.fillWidth: true
                                    fileName: modelData.name
                                    duration: modelData.duration
                                    fileSize: modelData.size
                                    fileType: modelData.type
                                }
                            }
                        }
                    }
                }
            }

            // Center Panel - Main Editor (Event Editor/Timeline/Mixer)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.background

                TabBar {
                    id: mainTabs
                    width: parent.width
                    height: 30
                    currentIndex: currentTab

                    TabButton {
                        text: "Event Editor"
                        width: 100
                        height: 30
                        background: Rectangle {
                            color: parent.checked ? theme.accent : theme.panel
                        }
                        onClicked: currentTab = 0
                    }
                    TabButton {
                        text: "Timeline"
                        width: 100
                        height: 30
                        onClicked: currentTab = 1
                    }
                    TabButton {
                        text: "Assets"
                        width: 100
                        height: 30
                        onClicked: currentTab = 2
                    }
                    TabButton {
                        text: "Mixer"
                        width: 100
                        height: 30
                        onClicked: currentTab = 3
                    }
                }

                StackLayout {
                    anchors.top: mainTabs.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    currentIndex: currentTab

                    // Event Editor (FMOD style)
                    Rectangle {
                        color: theme.panel

                        GridLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            columns: 2
                            columnSpacing: 20
                            rowSpacing: 15

                            // Event Properties
                            GroupBox {
                                title: "Event Properties"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 150

                                GridLayout {
                                    anchors.fill: parent
                                    columns: 2
                                    columnSpacing: 10
                                    rowSpacing: 8

                                    Label { text: "Name:"; color: theme.textPrimary }
                                    TextField { placeholderText: "Event Name"; Layout.fillWidth: true }

                                    Label { text: "Type:"; color: theme.textPrimary }
                                    ComboBox {
                                        model: ["One-shot", "Looping", "Sequence", "Switch", "Blend"]
                                        currentIndex: 0
                                        Layout.fillWidth: true
                                    }

                                    Label { text: "Max Instances:"; color: theme.textPrimary }
                                    SpinBox { value: 10; from: 1; to: 100 }

                                    Label { text: "Priority:"; color: theme.textPrimary }
                                    Slider { from: 0; to: 255; value: 128 }
                                }
                            }

                            // 3D Positioning
                            GroupBox {
                                title: "3D Positioning"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 200

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 20

                                    // 3D Visualization
                                    Rectangle {
                                        Layout.fillHeight: true
                                        Layout.preferredWidth: 200
                                        color: theme.panelDark
                                        radius: 4

                                        // 3D space visualization
                                        Canvas {
                                            anchors.fill: parent
                                            anchors.margins: 10

                                            onPaint: {
                                                var ctx = getContext('2d')
                                                ctx.clearRect(0, 0, width, height)

                                                // Draw listener (center)
                                                ctx.fillStyle = theme.accent
                                                ctx.beginPath()
                                                ctx.arc(width/2, height/2, 6, 0, Math.PI * 2)
                                                ctx.fill()

                                                // Draw sound sources
                                                ctx.fillStyle = "#ff8c00"
                                                ctx.beginPath()
                                                ctx.arc(width/3, height/3, 4, 0, Math.PI * 2)
                                                ctx.fill()

                                                // Draw lines for distance/occlusion
                                                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.3)
                                                ctx.setLineDash([2, 2])
                                                ctx.beginPath()
                                                ctx.moveTo(width/2, height/2)
                                                ctx.lineTo(width/3, height/3)
                                                ctx.stroke()
                                            }
                                        }
                                    }

                                    // 3D Controls
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 10

                                        SliderControl {
                                            label: "Min Distance"
                                            value: 1.0
                                            from: 0.1
                                            to: 100.0
                                            decimal: true
                                        }

                                        SliderControl {
                                            label: "Max Distance"
                                            value: 50.0
                                            from: 1.0
                                            to: 500.0
                                            decimal: true
                                        }

                                        SliderControl {
                                            label: "Spread"
                                            value: 0.0
                                            from: 0.0
                                            to: 360.0
                                            suffix: "°"
                                        }

                                        RowLayout {
                                            spacing: 10

                                            CheckBox {
                                                text: "Occlusion"
                                                checked: true
                                            }

                                            CheckBox {
                                                text: "Obstruction"
                                                checked: true
                                            }
                                        }
                                    }
                                }
                            }

                            // Parameters/RTPCs
                            GroupBox {
                                title: "Parameters"
                                Layout.fillWidth: true
                                Layout.rowSpan: 2
                                Layout.fillHeight: true

                                TableView {
                                    anchors.fill: parent
                                    anchors.margins: 5

                                    model: ListModel {
                                        id: parametersModel
                                    }

                                    delegate: RowLayout {
                                        width: parent.width
                                        height: 28

                                        Text {
                                            text: model.name
                                            color: theme.textPrimary
                                            font.pixelSize: 11
                                            Layout.preferredWidth: 120
                                        }

                                        Text {
                                            text: model.type
                                            color: theme.textSecondary
                                            font.pixelSize: 10
                                            Layout.preferredWidth: 80
                                        }

                                        Slider {
                                            from: model.min
                                            to: model.max
                                            value: model.defaultValue
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: model.defaultValue.toFixed(2)
                                            color: theme.textPrimary
                                            font.pixelSize: 10
                                            Layout.preferredWidth: 40
                                        }
                                    }

                                    Component.onCompleted: {
                                        parametersModel.append([
                                            { name: "Volume", type: "Global", min: 0, max: 1, defaultValue: 0.8 },
                                            { name: "Pitch", type: "Global", min: 0.5, max: 2.0, defaultValue: 1.0 },
                                            { name: "LPF", type: "Filter", min: 20, max: 20000, defaultValue: 20000 },
                                            { name: "HPF", type: "Filter", min: 20, max: 20000, defaultValue: 20 }
                                        ])
                                    }
                                }
                            }

                            // Audio Clips/Tracks
                            GroupBox {
                                title: "Audio Clips"
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                ListView {
                                    anchors.fill: parent
                                    anchors.margins: 5
                                    clip: true
                                    spacing: 2

                                    model: ListModel {
                                        id: audioClipsModel
                                    }

                                    delegate: Rectangle {
                                        width: parent.width
                                        height: 40
                                        color: index % 2 === 0 ? theme.panel : theme.panelDark

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            spacing: 10

                                            Text {
                                                text: model.name
                                                color: theme.textPrimary
                                                font.pixelSize: 12
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }

                                            Text {
                                                text: model.duration
                                                color: theme.textSecondary
                                                font.pixelSize: 10
                                                font.family: "monospace"
                                            }

                                            // Mini waveform
                                            Rectangle {
                                                Layout.preferredWidth: 100
                                                Layout.preferredHeight: 20
                                                color: theme.panelDarker
                                                radius: 2

                                                Canvas {
                                                    anchors.fill: parent
                                                    anchors.margins: 2

                                                    onPaint: {
                                                        var ctx = getContext('2d')
                                                        ctx.fillStyle = theme.accent
                                                        for (var i = 0; i < 20; i++) {
                                                            var h = 2 + Math.random() * 16
                                                            ctx.fillRect(i * 5, 10 - h/2, 4, h)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Component.onCompleted: {
                                        audioClipsModel.append([
                                            { name: "explosion_main.wav", duration: "2.3s" },
                                            { name: "explosion_tail.wav", duration: "3.1s" },
                                            { name: "rumble_low.wav", duration: "4.2s" }
                                        ])
                                    }
                                }
                            }
                        }
                    }

                    // Timeline View
                    Rectangle {
                        color: theme.panel

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            // Timeline Ruler
                            Rectangle {
                                Layout.fillWidth: true
                                height: 30
                                color: theme.panelDark
                                border { color: theme.border; width: 1 }

                                Canvas {
                                    anchors.fill: parent
                                    anchors.margins: 5

                                    onPaint: {
                                        var ctx = getContext('2d')
                                        ctx.strokeStyle = theme.textSecondary
                                        ctx.lineWidth = 1

                                        // Draw time markers
                                        for (var i = 0; i < 20; i++) {
                                            var x = i * 50
                                            ctx.beginPath()
                                            ctx.moveTo(x, 0)
                                            ctx.lineTo(x, i % 4 === 0 ? 20 : 10)
                                            ctx.stroke()

                                            if (i % 4 === 0) {
                                                ctx.fillStyle = theme.textPrimary
                                                ctx.font = "10px monospace"
                                                ctx.fillText(i + "s", x + 2, 25)
                                            }
                                        }
                                    }
                                }

                                // Playhead
                                Rectangle {
                                    id: playhead
                                    x: (parent.width - 20) * (transportPosition / 100000)
                                    width: 2
                                    height: parent.height
                                    color: theme.accent

                                    Triangle {
                                        anchors.top: parent.bottom
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 10
                                        height: 6
                                        color: theme.accent
                                        rotation: 180
                                    }
                                }
                            }

                            // Track Area
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: theme.panelDarker

                                // Audio tracks would go here
                                Text {
                                    anchors.centerIn: parent
                                    text: "Timeline Editor\nDrag audio clips here"
                                    color: theme.textDisabled
                                    font.pixelSize: 14
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }
                        }
                    }

                    // Assets View
                    Rectangle {
                        color: theme.panel

                        Text {
                            anchors.centerIn: parent
                            text: "Asset Library\nImport and manage audio files"
                            color: theme.textDisabled
                            font.pixelSize: 14
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    // Mixer View (FMOD-style mixer)
                    Rectangle {
                        color: theme.background

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 15

                            // Master Bus (leftmost)
                            MixerChannel {
                                channelName: "Master"
                                isMaster: true
                                level: busLevels["master"] || 0.5
                                muted: false
                                solo: false
                                Layout.preferredWidth: 80
                                Layout.fillHeight: true
                            }

                            Rectangle {
                                width: 1
                                height: parent.height
                                color: theme.border
                            }

                            // Regular buses
                            Repeater {
                                model: [
                                    { name: "Music", level: 0.4, muted: false, solo: false },
                                    { name: "SFX", level: 0.7, muted: false, solo: false },
                                    { name: "Dialog", level: 0.6, muted: false, solo: false },
                                    { name: "Ambience", level: 0.3, muted: false, solo: false },
                                    { name: "Reverb", level: 0.2, muted: false, solo: false, isEffect: true }
                                ]

                                delegate: MixerChannel {
                                    channelName: modelData.name
                                    level: modelData.level
                                    muted: modelData.muted
                                    solo: modelData.solo
                                    isEffect: modelData.isEffect || false
                                    Layout.preferredWidth: 80
                                    Layout.fillHeight: true
                                }
                            }

                            // Endpoints (right side)
                            Rectangle {
                                width: 1
                                height: parent.height
                                color: theme.border
                            }

                            // Endpoint channels
                            Repeater {
                                model: projectEndpoints

                                delegate: MixerChannel {
                                    channelName: modelData.name
                                    level: endpointLevels[modelData.name] || 0
                                    muted: false
                                    solo: false
                                    isEndpoint: true
                                    protocol: modelData.protocol
                                    Layout.preferredWidth: 100
                                    Layout.fillHeight: true
                                }
                            }
                        }
                    }
                }
            }

            // Right Panel - Properties/Monitoring
            Rectangle {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                color: theme.panel
                border { color: theme.border; width: 1 }

                TabBar {
                    id: rightTabs
                    width: parent.width
                    height: 28

                    TabButton {
                        text: "Properties"
                        width: 80
                        height: 28
                    }
                    TabButton {
                        text: "Routing"
                        width: 80
                        height: 28
                    }
                    TabButton {
                        text: "Monitor"
                        width: 80
                        height: 28
                    }
                }

                StackLayout {
                    anchors.top: rightTabs.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    currentIndex: rightTabs.currentIndex

                    // Properties tab
                    ScrollView {
                        clip: true

                        ColumnLayout {
                            width: parent.width
                            spacing: 10
                            padding: 15

                            GroupBox {
                                title: "Selected Endpoint"
                                Layout.fillWidth: true

                                GridLayout {
                                    width: parent.width
                                    columns: 2
                                    columnSpacing: 10
                                    rowSpacing: 8

                                    Label { text: "Name:"; color: theme.textPrimary }
                                    TextField { text: "unity_game_audio"; Layout.fillWidth: true }

                                    Label { text: "Protocol:"; color: theme.textPrimary }
                                    ComboBox {
                                        model: ["Shared Memory", "Local Socket", "UDP", "TCP", "PipeWire"]
                                        currentIndex: 0
                                        Layout.fillWidth: true
                                    }

                                    Label { text: "Sample Rate:"; color: theme.textPrimary }
                                    ComboBox {
                                        model: ["44100 Hz", "48000 Hz", "96000 Hz"]
                                        currentIndex: 1
                                        Layout.fillWidth: true
                                    }

                                    Label { text: "Channels:"; color: theme.textPrimary }
                                    ComboBox {
                                        model: ["Mono (1)", "Stereo (2)", "5.1 (6)", "7.1 (8)"]
                                        currentIndex: 1
                                        Layout.fillWidth: true
                                    }

                                    Label { text: "Status:"; color: theme.textPrimary }
                                    Text {
                                        text: "Connected"
                                        color: theme.success
                                        font.bold: true
                                    }

                                    Label { text: "Latency:"; color: theme.textPrimary }
                                    Text {
                                        text: "2.4 ms"
                                        color: theme.textSecondary
                                        font.family: "monospace"
                                    }
                                }
                            }

                            GroupBox {
                                title: "Effects Chain"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 200

                                ListView {
                                    anchors.fill: parent
                                    anchors.margins: 5
                                    model: ListModel {
                                        id: effectsModel
                                    }

                                    delegate: Rectangle {
                                        width: parent.width
                                        height: 40
                                        color: mouseArea3.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            spacing: 10

                                            Text {
                                                text: model.name
                                                color: theme.textPrimary
                                                font.pixelSize: 12
                                                Layout.fillWidth: true
                                            }

                                            Switch {
                                                checked: model.enabled
                                            }

                                            Button {
                                                text: "✕"
                                                flat: true
                                                onClicked: effectsModel.remove(index)
                                            }
                                        }

                                        MouseArea {
                                            id: mouseArea3
                                            anchors.fill: parent
                                            hoverEnabled: true
                                        }
                                    }

                                    Component.onCompleted: {
                                        effectsModel.append([
                                            { name: "Compressor", enabled: true },
                                            { name: "Limiter", enabled: true },
                                            { name: "Reverb", enabled: false },
                                            { name: "EQ", enabled: true }
                                        ])
                                    }
                                }
                            }

                            Button {
                                text: "Add Effect..."
                                Layout.fillWidth: true
                                onClicked: effectsDialog.open()
                            }
                        }
                    }

                    // Routing tab
                    ScrollView {
                        clip: true

                        ColumnLayout {
                            width: parent.width
                            spacing: 10
                            padding: 15

                            GroupBox {
                                title: "Audio Routing"
                                Layout.fillWidth: true

                                // Visual routing matrix
                                GridLayout {
                                    width: parent.width
                                    columns: 4
                                    rowSpacing: 5
                                    columnSpacing: 5

                                    // Headers
                                    Item { }
                                    Text { text: "Unity"; color: theme.accent; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                                    Text { text: "OBS"; color: theme.accent; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                                    Text { text: "Master"; color: theme.accent; font.bold: true; Layout.alignment: Qt.AlignHCenter }

                                    // Rows
                                    Text { text: "Unity"; color: theme.textPrimary; font.bold: true }
                                    CheckBox { checked: true; enabled: false } // Self
                                    CheckBox { checked: true } // Unity → OBS
                                    CheckBox { checked: true } // Unity → Master

                                    Text { text: "OBS"; color: theme.textPrimary; font.bold: true }
                                    CheckBox { checked: false } // OBS → Unity
                                    CheckBox { checked: true; enabled: false } // Self
                                    CheckBox { checked: true } // OBS → Master

                                    Text { text: "Mic"; color: theme.textPrimary; font.bold: true }
                                    CheckBox { checked: true } // Mic → Unity
                                    CheckBox { checked: true } // Mic → OBS
                                    CheckBox { checked: true } // Mic → Master
                                }
                            }

                            GroupBox {
                                title: "Application Bridges"
                                Layout.fillWidth: true

                                ColumnLayout {
                                    width: parent.width
                                    spacing: 8

                                    BridgeItem {
                                        appName: "Unity"
                                        status: "Connected"
                                        latency: "2.4ms"
                                        Layout.fillWidth: true
                                    }

                                    BridgeItem {
                                        appName: "Unreal Engine"
                                        status: "Available"
                                        latency: "N/A"
                                        Layout.fillWidth: true
                                    }

                                    BridgeItem {
                                        appName: "OBS Studio"
                                        status: "Connected"
                                        latency: "1.8ms"
                                        Layout.fillWidth: true
                                    }

                                    BridgeItem {
                                        appName: "Zoom"
                                        status: "Disconnected"
                                        latency: "N/A"
                                        Layout.fillWidth: true
                                    }
                                }
                            }

                            Button {
                                text: "Setup New Bridge..."
                                Layout.fillWidth: true
                                onClicked: bridgeWizard.open()
                            }
                        }
                    }

                    // Monitor tab
                    ScrollView {
                        clip: true

                        ColumnLayout {
                            width: parent.width
                            spacing: 15
                            padding: 15

                            // Level meters
                            GroupBox {
                                title: "Level Meters"
                                Layout.fillWidth: true

                                ColumnLayout {
                                    width: parent.width
                                    spacing: 5

                                    LevelMeter {
                                        label: "Master"
                                        level: 0.7
                                        peak: 0.8
                                        Layout.fillWidth: true
                                    }

                                    LevelMeter {
                                        label: "Music Bus"
                                        level: 0.4
                                        peak: 0.6
                                        Layout.fillWidth: true
                                    }

                                    LevelMeter {
                                        label: "SFX Bus"
                                        level: 0.8
                                        peak: 0.9
                                        Layout.fillWidth: true
                                    }

                                    LevelMeter {
                                        label: "Unity Input"
                                        level: 0.6
                                        peak: 0.7
                                        Layout.fillWidth: true
                                    }
                                }
                            }

                            // Spectrum analyzer
                            GroupBox {
                                title: "Spectrum Analyzer"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 150

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 5
                                    color: theme.panelDark

                                    Canvas {
                                        id: spectrumCanvas
                                        anchors.fill: parent

                                        onPaint: {
                                            var ctx = getContext('2d')
                                            ctx.clearRect(0, 0, width, height)

                                            // Draw frequency response
                                            ctx.strokeStyle = theme.accent
                                            ctx.lineWidth = 2
                                            ctx.beginPath()

                                            var points = 50
                                            for (var i = 0; i < points; i++) {
                                                var x = (i / points) * width
                                                var y = height - (Math.random() * 0.8 * height)
                                                if (i === 0) ctx.moveTo(x, y)
                                                    else ctx.lineTo(x, y)
                                            }
                                            ctx.stroke()

                                            // Draw frequency labels
                                            ctx.fillStyle = theme.textSecondary
                                            ctx.font = "9px monospace"
                                            ctx.fillText("20Hz", 5, height - 5)
                                            ctx.fillText("1kHz", width/2 - 15, height - 5)
                                            ctx.fillText("20kHz", width - 25, height - 5)
                                        }
                                    }
                                }
                            }

                            // Performance monitor
                            GroupBox {
                                title: "Performance"
                                Layout.fillWidth: true

                                GridLayout {
                                    width: parent.width
                                    columns: 2
                                    columnSpacing: 10
                                    rowSpacing: 8

                                    Text { text: "CPU Usage:"; color: theme.textPrimary }
                                    Text { text: Math.round(cpuUsage) + "%"; color: cpuUsage > 80 ? theme.error : theme.textPrimary; font.bold: true }

                                    Text { text: "Active Events:"; color: theme.textPrimary }
                                    Text { text: "12"; color: theme.textPrimary }

                                    Text { text: "Memory Usage:"; color: theme.textPrimary }
                                    Text { text: "45.2 MB"; color: theme.textPrimary }

                                    Text { text: "Peak Voices:"; color: theme.textPrimary }
                                    Text { text: "64"; color: theme.textPrimary }

                                    Text { text: "Total Latency:"; color: theme.textPrimary }
                                    Text { text: "4.2 ms"; color: theme.success; font.bold: true }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Status Bar
    Rectangle {
        Layout.fillWidth: true
        height: 24
        color: theme.panelDark
        border { color: theme.border; width: 1 }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 20

            Text {
                text: "Ready"
                color: theme.textSecondary
                font.pixelSize: 11
            }

            Text {
                text: "Unity Bridge: Active"
                color: theme.success
                font.pixelSize: 11
                font.bold: true
            }

            Text {
                text: "OBS: Connected"
                color: theme.success
                font.pixelSize: 11
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Text {
                text: "Sample Rate: 48000 Hz | Buffer: 256 samples | Latency: 5.3ms"
                color: theme.textSecondary
                font.pixelSize: 11
                font.family: "monospace"
            }
        }
    }

    // =============================================================================
    // CUSTOM COMPONENTS
    // =============================================================================

    // Transport Button Component
    component TransportButton: Rectangle {
        property string icon: ""
        property string tooltip: ""
        property color backgroundColor: "transparent"

        width: 28
        height: 28
        radius: 4
        color: mouseArea.containsMouse ? Qt.rgba(1, 1, 1, 0.1) : backgroundColor
        border.color: mouseArea.containsMouse ? theme.accent : "transparent"
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: parent.icon
            color: theme.textPrimary
            font.pixelSize: 14
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: if (parent.onClicked) parent.onClicked()
        }

        ToolTip {
            visible: mouseArea.containsMouse && tooltip !== ""
            text: tooltip
            delay: 500
        }
    }

    // Bus Item Component
    component BusItem: Rectangle {
        property string busName: ""
        property real level: 0
        property bool isMuted: false
        property bool isSolo: false

        height: 40
        color: mouseArea4.containsMouse ? Qt.rgba(0, 0.48, 0.8, 0.05) : "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 10

            // Mute/Solo buttons
            Row {
                spacing: 4
                Layout.preferredWidth: 40

                Rectangle {
                    width: 16
                    height: 16
                    radius: 8
                    color: isMuted ? theme.error : theme.panelDark
                    border.color: theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "M"
                        color: "white"
                        font.pixelSize: 9
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: isMuted = !isMuted
                    }
                }

                Rectangle {
                    width: 16
                    height: 16
                    radius: 8
                    color: isSolo ? theme.warning : theme.panelDark
                    border.color: theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "S"
                        color: "white"
                        font.pixelSize: 9
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: isSolo = !isSolo
                    }
                }
            }

            // Bus name
            Text {
                text: busName
                color: theme.textPrimary
                font.pixelSize: 12
                font.bold: true
                Layout.fillWidth: true
            }

            // Level bar
            Rectangle {
                Layout.preferredWidth: 60
                height: 16
                color: theme.panelDark
                radius: 2

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * level
                    color: theme.accent
                    radius: 2
                }
            }
        }

        MouseArea {
            id: mouseArea4
            anchors.fill: parent
            hoverEnabled: true
            onClicked: selectBus(busName)
        }
    }

    // Asset Item Component
    component AssetItem: Rectangle {
        property string fileName: ""
        property string duration: ""
        property string fileSize: ""
        property string fileType: ""

        height: 36
        color: index % 2 === 0 ? theme.panel : theme.panelDark

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 10

            Text {
                text: {
                    var icon = ""
                    switch(fileType) {
                        case "SFX": icon = "💥"; break
                        case "Music": icon = "🎵"; break
                        case "Voice": icon = "🎤"; break
                        case "Ambience": icon = "🌊"; break
                        default: icon = "📄"
                    }
                    return icon + " " + fileName
                }
                color: theme.textPrimary
                font.pixelSize: 11
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }

            Text {
                text: duration
                color: theme.textSecondary
                font.pixelSize: 10
                font.family: "monospace"
                Layout.preferredWidth: 50
            }

            Text {
                text: fileSize
                color: theme.textSecondary
                font.pixelSize: 10
                Layout.preferredWidth: 60
            }
        }
    }

    // Slider Control Component
    component SliderControl: ColumnLayout {
        property string label: ""
        property real value: 0.5
        property real from: 0
        property real to: 1
        property bool decimal: false
        property string suffix: ""

        spacing: 4
        Layout.fillWidth: true

        RowLayout {
            Text {
                text: label
                color: theme.textPrimary
                font.pixelSize: 11
                Layout.fillWidth: true
            }

            Text {
                text: decimal ? value.toFixed(2) + suffix : Math.round(value) + suffix
                color: theme.textSecondary
                font.pixelSize: 10
                font.family: "monospace"
            }
        }

        Slider {
            from: from
            to: to
            value: parent.value
            onValueChanged: parent.value = value
            Layout.fillWidth: true
        }
    }

    // Mixer Channel Component
    component MixerChannel: Rectangle {
        property string channelName: ""
        property real level: 0.5
        property bool muted: false
        property bool solo: false
        property bool isMaster: false
        property bool isEffect: false
        property bool isEndpoint: false
        property string protocol: ""

        color: theme.panelDark
        border.color: isMaster ? theme.accent : theme.border
        border.width: isMaster ? 2 : 1
        radius: 4

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            // Channel name
            Text {
                text: channelName
                color: isMaster ? theme.accent : theme.textPrimary
                font.pixelSize: isMaster ? 14 : 12
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            // Protocol indicator for endpoints
            Text {
                text: isEndpoint ? "(" + protocol + ")" : ""
                color: theme.textSecondary
                font.pixelSize: 9
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                visible: isEndpoint
            }

            // VU Meter
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.panelDarker
                radius: 2

                // Vertical level meter
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width * 0.8
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

                // Scale markings
                Canvas {
                    anchors.fill: parent
                    anchors.margins: 2

                    onPaint: {
                        var ctx = getContext('2d')
                        ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.2)
                        ctx.lineWidth = 1

                        // Draw dB scale
                        var marks = [-6, -12, -24, -36]
                        for (var i = 0; i < marks.length; i++) {
                            var y = parent.height * (1 - (Math.abs(marks[i]) / 60))
                            ctx.beginPath()
                            ctx.moveTo(0, y)
                            ctx.lineTo(parent.width, y)
                            ctx.stroke()
                        }
                    }
                }
            }

            // Volume slider
            Slider {
                orientation: Qt.Vertical
                from: 0
                to: 1
                value: level
                onValueChanged: level = value
                Layout.preferredHeight: 100
                Layout.alignment: Qt.AlignHCenter
            }

            // Mute/Solo buttons
            RowLayout {
                spacing: 5
                Layout.alignment: Qt.AlignHCenter

                Rectangle {
                    width: 24
                    height: 24
                    radius: 12
                    color: muted ? theme.error : theme.panel
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
                        onClicked: muted = !muted
                    }
                }

                Rectangle {
                    width: 24
                    height: 24
                    radius: 12
                    color: solo ? theme.warning : theme.panel
                    border.color: theme.border

                    Text {
                        anchors.centerIn: parent
                        text: "S"
                        color: "white"
                        font.pixelSize: 10
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: solo = !solo
                    }
                }
            }

            // Effects button (for effect buses)
            Button {
                text: "FX"
                visible: isEffect
                Layout.fillWidth: true
                onClicked: effectsDialog.open()
            }
        }
    }

    // Bridge Item Component
    component BridgeItem: Rectangle {
        property string appName: ""
        property string status: ""
        property string latency: ""

        height: 40
        radius: 4
        color: mouseArea5.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
        border.color: getStatusColor(status)
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 10

            Text {
                text: appName
                color: theme.textPrimary
                font.bold: true
                font.pixelSize: 12
                Layout.fillWidth: true
            }

            Rectangle {
                width: 10
                height: 10
                radius: 5
                color: getStatusColor(status)
            }

            Text {
                text: status
                color: getStatusColor(status)
                font.pixelSize: 10
                font.bold: true
                Layout.preferredWidth: 80
            }

            Text {
                text: latency
                color: theme.textSecondary
                font.pixelSize: 10
                font.family: "monospace"
                Layout.preferredWidth: 50
            }
        }

        MouseArea {
            id: mouseArea5
            anchors.fill: parent
            hoverEnabled: true
            onClicked: configureBridge(appName)
        }

        function getStatusColor(status) {
            switch(status) {
                case "Connected": return theme.success
                case "Available": return theme.warning
                default: return theme.error
            }
        }
    }

    // Level Meter Component
    component LevelMeter: ColumnLayout {
        property string label: ""
        property real level: 0
        property real peak: 0

        spacing: 2

        RowLayout {
            Text {
                text: label
                color: theme.textPrimary
                font.pixelSize: 10
                Layout.fillWidth: true
            }

            Text {
                text: (20 * Math.log10(level > 0.001 ? level : 0.001)).toFixed(1) + " dB"
                color: theme.textSecondary
                font.pixelSize: 9
                font.family: "monospace"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 16
            color: theme.panelDark
            radius: 2

            // Level fill
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.width * level
                color: getLevelColor(level)
                radius: 2
            }

            // Peak indicator
            Rectangle {
                x: parent.width * peak - 1
                width: 2
                height: parent.height
                color: "white"
            }
        }

        function getLevelColor(level) {
            if (level > 0.9) return "#ff4444"
                if (level > 0.7) return "#ffaa44"
                    if (level > 0.3) return "#44ff44"
                        return "#44aaff"
        }
    }

    // Triangle Component (for playhead)
    component Triangle: Shape {
        property color color: "white"

        ShapePath {
            fillColor: parent.color
            strokeWidth: 0
            startX: 0
            startY: 0
            PathLine { x: parent.width; y: parent.height / 2 }
            PathLine { x: 0; y: parent.height }
            PathLine { x: 0; y: 0 }
        }
    }

    // =============================================================================
    // DIALOGS
    // =============================================================================

    Dialog {
        id: createEventDialog
        title: "Create New Event"
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 400
        height: 300

        ColumnLayout {
            spacing: 15
            anchors.fill: parent

            TextField {
                placeholderText: "Event Name"
                Layout.fillWidth: true
            }

            ComboBox {
                model: ["One-shot", "Looping", "Sequence", "Switch", "Blend"]
                currentIndex: 0
                Layout.fillWidth: true
            }

            GridLayout {
                columns: 2
                columnSpacing: 10
                rowSpacing: 8
                Layout.fillWidth: true

                Label { text: "Initial Volume:" }
                Slider { from: 0; to: 1; value: 0.8 }

                Label { text: "Max Instances:" }
                SpinBox { from: 1; to: 100; value: 10 }

                Label { text: "Priority:" }
                SpinBox { from: 0; to: 255; value: 128 }
            }
        }
    }

    // =============================================================================
    // FUNCTIONS
    // =============================================================================

    function loadDefaultProject() {
        projectEndpoints = [
            { name: "unity_game", protocol: "Shared Memory", direction: "Duplex" },
            { name: "obs_capture", protocol: "PipeWire", direction: "Input" },
            { name: "zoom_mic", protocol: "Virtual Device", direction: "Output" }
        ]

        projectEvents = [
            { id: 1, name: "explosion", type: "One-shot", category: "SFX" },
            { id: 2, name: "bg_music", type: "Looping", category: "Music" },
            { id: 3, name: "footstep", type: "One-shot", category: "Characters" }
        ]

        projectModified = false
    }

    function toggleTransport() {
        isPlaying = !isPlaying
        if (isPlaying) {
            // Start transport
            transportTimer.start()
        } else {
            transportTimer.stop()
        }
    }

    function startMonitoring() {
        // Simulate audio levels
        levelTimer.start()
        cpuTimer.start()
    }

    Timer {
        id: transportTimer
        interval: 50
        running: false
        repeat: true
        onTriggered: {
            transportPosition += 100
            if (transportPosition > 100000) {
                transportPosition = 0
            }
        }
    }

    Timer {
        id: levelTimer
        interval: 100
        running: true
        repeat: true
        onTriggered: {
            // Update endpoint levels
            for (var i = 0; i < projectEndpoints.length; i++) {
                var ep = projectEndpoints[i]
                endpointLevels[ep.name] = 0.3 + Math.random() * 0.5
            }

            // Update bus levels
            busLevels["master"] = 0.4 + Math.random() * 0.3
            busLevels["music"] = 0.3 + Math.random() * 0.2
            busLevels["sfx"] = 0.5 + Math.random() * 0.4
        }
    }

    Timer {
        id: cpuTimer
        interval: 2000
        running: true
        repeat: true
        onTriggered: {
            cpuUsage = 20 + Math.random() * 40
        }
    }

    function formatTime(ms) {
        var totalSeconds = Math.floor(ms / 1000)
        var minutes = Math.floor(totalSeconds / 60)
        var seconds = totalSeconds % 60
        var milliseconds = ms % 1000

        return minutes.toString().padStart(2, '0') + ":" +
        seconds.toString().padStart(2, '0') + "." +
        milliseconds.toString().padStart(3, '0')
    }

    function bridgeToUnity() {
        if (middlewareBackend) {
            var success = middlewareBackend.bridgeToGameEngine("Unity", "unity_bridge")
            if (success) {
                showNotification("Unity Bridge", "Successfully connected to Unity")
                // Add to project endpoints
                projectEndpoints.push({
                    name: "unity_bridge",
                    protocol: "Shared Memory",
                    direction: "Duplex"
                })
            }
        }
    }

    function bridgeToOBS() {
        if (middlewareBackend) {
            var success = middlewareBackend.bridgeToOBS("obs_bridge")
            if (success) {
                showNotification("OBS Bridge", "Successfully connected to OBS Studio")
            }
        }
    }

    function showNotification(title, message) {
        notificationDialog.title = title
        notificationDialog.message = message
        notificationDialog.open()
    }

    // More dialogs would be defined here...
    Dialog { id: notificationDialog; standardButtons: Dialog.Ok }
    Platform.FileDialog { id: openProjectDialog }
    Platform.FileDialog { id: saveProjectAsDialog }
    Dialog { id: aboutDialog }
    // ... etc.

    // Integration with main app
    function connectBackends(refs) {
        if (refs.core) {
            // Connect to core backend
        }
        if (refs.audio) {
            // Connect to audio backend
        }
        if (refs.converter) {
            // Connect to converter backend
        }
    }

    Component.onDestruction: {
        // Clean up connections
        if (middlewareBackend) {
            middlewareBackend.stopAll()
        }
    }
}
