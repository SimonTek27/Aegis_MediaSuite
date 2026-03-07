// ui_player_pro.qml - Professional Aegis Player UI - Premium Edition v1.0

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import QtQuick.Effects

Rectangle {
    id: root
    anchors.fill: parent

    //=========================================================================
    // PROPERTIES - Premium Configuration
    //=========================================================================

    // Backend references with safe initialization
    property var coreRef: typeof Core !== 'undefined' ? Core : null
    property var audioRef: typeof Audio !== 'undefined' ? Audio : null
    property var platformRef: typeof Platform !== 'undefined' ? Platform : null

    // Core UI State
    property bool isPlaying: coreRef ? coreRef.playing : false
    property real position: coreRef ? coreRef.position : 0
    property real duration: coreRef ? coreRef.duration : 0
    property real volume: coreRef ? coreRef.volume : 85
    property string currentFile: coreRef && coreRef.currentFile ? coreRef.currentFile : ""
    property bool hasVideo: coreRef ? coreRef.hasVideo : false
    property real playbackSpeed: 1.0

    // Telemetry System
    property int cpuUsage: 12
    property int gpuUsage: 34
    property int ramUsage: 1248
    property int bufferPercent: 92
    property int networkSpeed: 245
    property int frameRate: 60
    property int droppedFrames: 0
    property int temperature: 58
    property string audioCodec: "AAC LC"
    property string videoCodec: "HEVC Main 10"
    property string container: "MKV"
    property int bitrate: 45200
    property string aspectRatio: "16:9"
    property string colorSpace: "BT.2020"
    property int latency: 23
    property bool drmActive: true

    // UI State
    property bool uiVisible: true
    property bool settingsPanelVisible: false
    property bool playlistVisible: false
    property bool metadataVisible: false
    property bool isFullscreen: false
    property bool lowPowerMode: false
    property bool darkTheme: true
    property double uiOpacity: 1.0
    property double panelOpacity: 0.95

    // Layout constants - Professional Design System
    readonly property color accentColor: darkTheme ? "#ff8c00" : "#0066cc"
    readonly property color accentSecondary: darkTheme ? "#4da6ff" : "#ff8c00"
    readonly property color accentGlow: Qt.rgba(1, 0.55, 0, 0.3)
    readonly property color bgPrimary: darkTheme ? "#050505" : "#f8f9fa"
    readonly property color bgSecondary: darkTheme ? "#121212" : "#ffffff"
    readonly property color bgTertiary: darkTheme ? "#1a1a1a" : "#e9ecef"
    readonly property color borderLight: darkTheme ? "#2a2a2a" : "#dee2e6"
    readonly property color borderMedium: darkTheme ? "#333333" : "#ced4da"
    readonly property color borderGlow: Qt.rgba(1, 0.55, 0, 0.15)
    readonly property color textPrimary: darkTheme ? "#ffffff" : "#212529"
    readonly property color textSecondary: darkTheme ? "#aaaaaa" : "#6c757d"
    readonly property color textMuted: darkTheme ? "#666666" : "#adb5bd"
    readonly property color overlayBg: Qt.rgba(0, 0, 0, 0.7)

    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 20
    readonly property int spacingXxl: 24

    readonly property int fontSizeXxs: 8
    readonly property int fontSizeXs: 9
    readonly property int fontSizeSm: 10
    readonly property int fontSizeMd: 11
    readonly property int fontSizeLg: 12
    readonly property int fontSizeXl: 14
    readonly property int fontSizeXxl: 18
    readonly property int fontSizeDisplay: 32

    readonly property int controlHeight: 28
    readonly property int controlPanelHeight: 260

    //=========================================================================
    // FONTS - Premium Typography
    //=========================================================================

    FontLoader {
        id: jetBrainsMono
        source: "qrc:/fonts/JetBrainsMono-Regular.ttf"
    }

    FontLoader {
        id: interFont
        source: "qrc:/fonts/Inter-Regular.ttf"
    }

    //=========================================================================
    // INITIALIZATION & ERROR HANDLING
    //=========================================================================

    Component.onCompleted: {
        initializeBackend()
        setupWindow()
        loadUserPreferences()
    }

    function initializeBackend() {
        if (!coreRef) {
            backendTimer.start()
        }
    }

    Timer {
        id: backendTimer
        interval: 500
        repeat: true
        onTriggered: {
            if (typeof Core !== 'undefined') {
                coreRef = Core
                audioRef = Audio
                platformRef = Platform
                stop()
            }
        }
    }

    function setupWindow() {
        if (typeof Window !== 'undefined') {
            root.Window.window.minimumWidth = 1024
            root.Window.window.minimumHeight = 768
            root.Window.window.title = "Aegis Player Pro"
        }
    }

    function loadUserPreferences() {
        // Load saved preferences
        if (platformRef && platformRef.getSetting) {
            darkTheme = platformRef.getSetting("theme", true)
            volume = platformRef.getSetting("volume", 85)
        }
    }

    //=========================================================================
    // TELEMETRY SYSTEM - Real-time Monitoring
    //=========================================================================

    Timer {
        interval: 250
        running: true
        repeat: true
        onTriggered: updateTelemetry()
    }

    function updateTelemetry() {
        if (platformRef && platformRef.getSystemStats) {
            var stats = platformRef.getSystemStats()
            cpuUsage = stats.cpu || 12 + Math.floor(Math.random() * 8)
            gpuUsage = stats.gpu || 34 + Math.floor(Math.random() * 12)
            ramUsage = stats.ram || 1248 + Math.floor(Math.random() * 100)
            temperature = stats.temp || 58 + Math.floor(Math.random() * 5)
            networkSpeed = stats.network || 245 + Math.floor(Math.random() * 30)
        } else {
            // Simulated telemetry for demo
            cpuUsage = 12 + Math.floor(Math.random() * 8)
            gpuUsage = 34 + Math.floor(Math.random() * 12)
            bufferPercent = 85 + Math.floor(Math.random() * 12)
            droppedFrames = Math.floor(Math.random() * 3)
        }
    }

    //=========================================================================
    // VIDEO DISPLAY - Premium Playback Area
    //=========================================================================

    Item {
        id: videoContainer
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: uiVisible ? controlsPanel.top : parent.bottom

        // Video Output Layer
        Rectangle {
            id: videoOutput
            anchors.fill: parent
            color: "black"
            visible: hasVideo && isPlaying

            // MPV Video Surface (actual implementation)
            Item {
                id: mpvSurface
                anchors.fill: parent
                focus: true

                Keys.onPressed: handleKeyPress(event)

                // Video info overlay on hover
                Rectangle {
                    id: videoInfoOverlay
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: spacingLg
                    color: overlayBg
                    radius: 4
                    visible: videoMa.containsMouse
                    opacity: 0.8

                    Row {
                        anchors.margins: spacingSm
                        spacing: spacingMd

                        Text {
                            text: videoCodec + " | " + bitrate + " kbps"
                            color: textPrimary
                            font.family: jetBrainsMono.name
                            font.pixelSize: fontSizeXs
                        }

                        Rectangle {
                            width: 1
                            height: parent.height
                            color: borderLight
                        }

                        Text {
                            text: frameRate + " fps"
                            color: textPrimary
                            font.family: jetBrainsMono.name
                            font.pixelSize: fontSizeXs
                        }

                        Rectangle {
                            width: 1
                            height: parent.height
                            color: borderLight
                        }

                        Text {
                            text: colorSpace
                            color: textPrimary
                            font.family: jetBrainsMono.name
                            font.pixelSize: fontSizeXs
                        }
                    }
                }

                MouseArea {
                    id: videoMa
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: toggleUIVisibility()
                    onDoubleClicked: toggleFullscreen()
                }
            }
        }

        // Placeholder/Album Art Layer
        Rectangle {
            id: placeholderLayer
            anchors.fill: parent
            visible: !videoOutput.visible
            color: bgPrimary

            // Animated gradient background
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: bgSecondary }
                    GradientStop { position: 1.0; color: bgPrimary }
                }

                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.6; to: 0.8; duration: 3000 }
                    NumberAnimation { from: 0.8; to: 0.6; duration: 3000 }
                }
            }

            // Album art or waveform visualization
            Item {
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.5, 400)
                height: width

                // Waveform animation
                Repeater {
                    model: 32

                    Rectangle {
                        x: (index * parent.width / 32) + 2
                        y: parent.height * 0.5
                        width: (parent.width / 32) - 4
                        height: Math.random() * parent.height * 0.6
                        color: accentColor
                        opacity: 0.3

                        transform: Translate {
                            y: -height * 0.5
                        }

                        SequentialAnimation on height {
                            loops: Animation.Infinite
                            NumberAnimation {
                                to: Math.random() * parent.parent.height * 0.8
                                duration: 500 + Math.random() * 500
                            }
                            NumberAnimation {
                                to: Math.random() * parent.parent.height * 0.4
                                duration: 500 + Math.random() * 500
                            }
                        }
                    }
                }
            }
        }

        //=========================================================================
        // HUD OVERLAY - Premium Heads-Up Display
        //=========================================================================

        Column {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: spacingXl
            spacing: spacingMd

            // Top Telemetry Bar
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                height: 28
                color: overlayBg
                radius: 4

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: spacingSm

                    // System Metrics
                    Row {
                        spacing: spacingMd
                        Layout.alignment: Qt.AlignVCenter

                        MetricDisplay {
                            icon: "⚡"
                            value: cpuUsage + "%"
                            label: "CPU"
                            color: cpuUsage > 80 ? "red" : accentColor
                        }

                        MetricDisplay {
                            icon: "🎮"
                            value: gpuUsage + "%"
                            label: "GPU"
                        }

                        MetricDisplay {
                            icon: "🧠"
                            value: (ramUsage / 1024).toFixed(1) + "GB"
                            label: "RAM"
                        }

                        MetricDisplay {
                            icon: "🌡️"
                            value: temperature + "°C"
                            label: "TEMP"
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // Recording Indicator
                    Rectangle {
                        color: "#22c55e"
                        radius: 2
                        width: recText.width + spacingLg
                        height: parent.height
                        visible: true

                        Row {
                            anchors.centerIn: parent
                            spacing: spacingXs

                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                color: "white"
                                anchors.verticalCenter: parent.verticalCenter

                                SequentialAnimation on opacity {
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 1; to: 0.3; duration: 1000 }
                                    NumberAnimation { from: 0.3; to: 1; duration: 1000 }
                                }
                            }

                            Text {
                                id: recText
                                text: "REC READY"
                                color: "white"
                                font.family: jetBrainsMono.name
                                font.pixelSize: fontSizeSm
                                font.bold: true
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // Network & Time
                    Row {
                        spacing: spacingMd
                        Layout.alignment: Qt.AlignVCenter

                        MetricDisplay {
                            icon: "🌐"
                            value: networkSpeed + " Mbps"
                            label: "NET"
                        }

                        Rectangle {
                            width: 1
                            height: parent.height
                            color: borderLight
                        }

                        Text {
                            text: Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")
                            color: textSecondary
                            font.family: jetBrainsMono.name
                            font.pixelSize: fontSizeSm
                        }
                    }
                }
            }

            // Time Display - Large Format
            Rectangle {
                anchors.left: parent.left
                width: 200
                height: 60
                color: overlayBg
                radius: 4

                Column {
                    anchors.centerIn: parent
                    spacing: 0

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: formatTime(position)
                        color: accentColor
                        font.family: jetBrainsMono.name
                        font.pixelSize: fontSizeDisplay
                        font.bold: true
                        style: Text.Outline
                        styleColor: "black"
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "-" + formatTime(duration - position)
                        color: textSecondary
                        font.family: jetBrainsMono.name
                        font.pixelSize: fontSizeXs
                        opacity: 0.7
                    }
                }
            }
        }

        // Bottom HUD - Quick Controls
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: spacingXl
            height: 40
            color: overlayBg
            radius: 4
            visible: uiVisible && !controlsPanel.visible

            RowLayout {
                anchors.fill: parent
                anchors.margins: spacingSm

                // Quick Playback Controls
                Row {
                    spacing: spacingSm
                    Layout.alignment: Qt.AlignVCenter

                    QuickButton { text: "⏮"; onClicked: previousTrack() }
                    QuickButton {
                        text: isPlaying ? "⏸" : "▶"
                        onClicked: playPause()
                    }
                    QuickButton { text: "⏭"; onClicked: nextTrack() }
                }

                Item { Layout.fillWidth: true }

                // Quick Info
                Text {
                    text: currentFile ? currentFile.split('/').pop() : "No Media"
                    color: textPrimary
                    font.family: interFont.name
                    font.pixelSize: fontSizeMd
                    elide: Text.ElideMiddle
                    maximumLineCount: 1
                }

                Item { Layout.fillWidth: true }

                // Volume Quick Control
                Row {
                    spacing: spacingSm

                    Text {
                        text: "🔊"
                        color: accentColor
                        font.pixelSize: fontSizeLg
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Rectangle {
                        width: 80
                        height: 4
                        color: bgTertiary
                        radius: 2
                        anchors.verticalCenter: parent.verticalCenter

                        Rectangle {
                            width: parent.width * (volume / 100)
                            height: parent.height
                            color: accentColor
                            radius: 2
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: setVolume(mouse.x / width * 100)
                        }
                    }
                }
            }
        }
    }

    //=========================================================================
    // CONTROL PANEL - Professional Media Controls
    //=========================================================================

    Rectangle {
        id: controlsPanel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: uiVisible ? controlPanelHeight : 0
        color: bgSecondary
        border.color: borderLight
        border.width: 1

        Behavior on height {
            NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
        }

        // Panel Header with Drag Handle
        Rectangle {
            id: panelHeader
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 24
            color: Qt.rgba(1, 1, 1, 0.03)

            MouseArea {
                anchors.fill: parent
                drag.target: root
                drag.axis: Drag.YAxis
                drag.minimumY: Screen.height - controlPanelHeight
                drag.maximumY: Screen.height - 100

                Rectangle {
                    anchors.centerIn: parent
                    width: 40
                    height: 4
                    radius: 2
                    color: borderMedium
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: panelHeader.height + spacingLg
            anchors.leftMargin: spacingXl
            anchors.rightMargin: spacingXl
            anchors.bottomMargin: spacingXl
            spacing: spacingXl

            //=========================================================================
            // MAIN CONTROLS ROW - Info + Audio + Playback
            //=========================================================================

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                spacing: spacingLg

                //---------------------------------------------------------------------
                // INFO MODULE - Premium Media Information
                //---------------------------------------------------------------------

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * 0.65
                    color: Qt.rgba(1, 0.55, 0, 0.02)
                    border.color: borderGlow
                    border.width: 1
                    radius: 6

                    layer {
                        enabled: true
                        effect: MultiEffect {
                            shadowEnabled: true
                            shadowColor: accentGlow
                            shadowOffset: Qt.point(0, 2)
                            shadowBlur: 0.5
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: spacingLg
                        spacing: spacingXl

                        // Time Column
                        ColumnLayout {
                            spacing: spacingSm

                            Text {
                                text: formatTime(position)
                                color: accentColor
                                font.family: jetBrainsMono.name
                                font.pixelSize: fontSizeDisplay
                                font.bold: true
                            }

                            Text {
                                text: "REMAINING: -" + formatTime(duration - position)
                                color: textSecondary
                                font.family: jetBrainsMono.name
                                font.pixelSize: fontSizeXs
                            }

                            Rectangle {
                                color: bgTertiary
                                height: 24
                                width: 180
                                radius: 2

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: spacingSm
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "SRC: " + (currentFile.startsWith("http") ? "🌐 STREAM" : "💾 LOCAL")
                                    color: textPrimary
                                    font.family: jetBrainsMono.name
                                    font.pixelSize: fontSizeXs
                                    font.bold: true
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    width: 3
                                    height: parent.height
                                    color: accentColor
                                }
                            }
                        }

                        // Vertical Separator
                        Rectangle {
                            Layout.fillHeight: true
                            width: 1
                            color: borderLight
                        }

                        // File Info Column
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: spacingMd

                            Text {
                                text: currentFile ? currentFile.split('/').pop() : "No Media Loaded"
                                color: textPrimary
                                font.family: interFont.name
                                font.pixelSize: fontSizeXl
                                font.bold: true
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }

                            // Media Tags
                            Flow {
                                Layout.fillWidth: true
                                spacing: spacingSm

                                MediaTag { text: hasVideo ? "VIDEO" : "AUDIO"; color: accentSecondary }
                                MediaTag { text: videoCodec; color: textPrimary }
                                MediaTag { text: bitrate + " kbps"; color: textPrimary }
                                MediaTag { text: "AR: " + aspectRatio; color: accentSecondary }
                                MediaTag { text: colorSpace; color: textPrimary }
                                MediaTag { text: container; color: textPrimary }
                                MediaTag { text: audioCodec; color: textPrimary }
                                MediaTag { text: drmActive ? "DRM" : "No DRM"; color: drmActive ? "#ef4444" : "#22c55e" }
                            }

                            Item { Layout.fillHeight: true }

                            // Seek Bar
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: spacingXs

                                RowLayout {
                                    spacing: spacingSm

                                    Text {
                                        text: "⏵"
                                        color: accentColor
                                        font.pixelSize: fontSizeLg
                                        Layout.preferredWidth: 20
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    // Professional Seek Bar
                                    SeekBar {
                                        Layout.fillWidth: true
                                        position: root.position
                                        duration: root.duration
                                        bufferPercent: bufferPercent
                                        onSeek: seekTo(value)
                                    }
                                }

                                // Seek Info Row
                                RowLayout {
                                    Layout.leftMargin: 28
                                    Layout.fillWidth: true

                                    Text {
                                        text: "SPEED: <b>" + playbackSpeed.toFixed(1) + "x</b>"
                                        color: textPrimary
                                        font.family: jetBrainsMono.name
                                        font.pixelSize: fontSizeXs
                                    }

                                    Text {
                                        text: "BUFFER: <b>" + bufferPercent + "%</b>"
                                        color: textPrimary
                                        font.family: jetBrainsMono.name
                                        font.pixelSize: fontSizeXs
                                    }

                                    Text {
                                        text: "LATENCY: <b>" + latency + "ms</b>"
                                        color: textPrimary
                                        font.family: jetBrainsMono.name
                                        font.pixelSize: fontSizeXs
                                    }

                                    Item { Layout.fillWidth: true }

                                    Text {
                                        text: "FRAME: " + Math.round(position * frameRate) + "/" + Math.round(duration * frameRate)
                                        color: textMuted
                                        font.family: jetBrainsMono.name
                                        font.pixelSize: fontSizeXs
                                    }
                                }
                            }
                        }
                    }
                }

                //---------------------------------------------------------------------
                // AUDIO MODULE - Professional Audio Control
                //---------------------------------------------------------------------

                Rectangle {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    color: Qt.rgba(1, 0.55, 0, 0.02)
                    border.color: borderGlow
                    border.width: 1
                    radius: 6

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: spacingLg
                        spacing: spacingMd

                        // Audio Visualizer
                        AudioVisualizer {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            audioRef: audioRef
                            barCount: lowPowerMode ? 12 : 24
                            accentColor: accentColor
                        }

                        // Volume Control
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: spacingXs

                            RowLayout {
                                spacing: spacingSm

                                Text {
                                    text: "🔊"
                                    color: accentColor
                                    font.pixelSize: fontSizeLg
                                    Layout.preferredWidth: 20
                                }

                                VolumeSlider {
                                    Layout.fillWidth: true
                                    volume: root.volume
                                    onVolumeChanged: setVolume(value)
                                }
                            }

                            RowLayout {
                                Layout.leftMargin: 28

                                Text {
                                    text: "STEREO · 24bit/192kHz"
                                    color: textSecondary
                                    font.family: jetBrainsMono.name
                                    font.pixelSize: fontSizeXs
                                    opacity: 0.7
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: "EBU R128 · -23 LUFS"
                                    color: textSecondary
                                    font.family: jetBrainsMono.name
                                    font.pixelSize: fontSizeXs
                                }
                            }
                        }
                    }
                }
            }

            //=========================================================================
            // BOTTOM CONTROLS - Transport + Status + Utilities
            //=========================================================================

            RowLayout {
                Layout.fillWidth: true
                spacing: 0

                // Left Controls - Transport
                Row {
                    spacing: spacingSm
                    Layout.alignment: Qt.AlignVCenter

                    // Playlist & Media
                    TransportButton {
                        text: "☰"
                        tooltip: "Playlist"
                        onClicked: togglePlaylist()

                        Badge {
                            visible: playlistVisible
                            anchors.top: parent.top
                            anchors.right: parent.right
                            color: accentColor
                        }
                    }

                    TransportButton {
                        text: "⏏"
                        tooltip: "Eject / Open"
                        onClicked: openFileDialog()
                    }

                    Separator {}

                    TransportButton {
                        text: "⏮"
                        tooltip: "Previous Track"
                        onClicked: previousTrack()
                    }

                    // Play/Pause - Enhanced
                    Rectangle {
                        width: 44
                        height: controlHeight
                        color: Qt.rgba(1, 1, 1, 0.1)
                        border.color: accentColor
                        border.width: 1
                        radius: 4

                        Text {
                            anchors.centerIn: parent
                            text: isPlaying ? "⏸" : "▶"
                            color: accentColor
                            font.pixelSize: fontSizeXxl
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: parent.color = Qt.rgba(1, 0.55, 0, 0.2)
                            onExited: parent.color = Qt.rgba(1, 1, 1, 0.1)
                            onClicked: playPause()
                        }

                        ToolTip {
                            text: isPlaying ? "Pause" : "Play"
                            visible: parent.MouseArea.containsMouse
                            delay: 500
                        }
                    }

                    TransportButton {
                        text: "⏭"
                        tooltip: "Next Track"
                        onClicked: nextTrack()
                    }

                    TransportButton {
                        text: "⬛"
                        tooltip: "Stop"
                        onClicked: stop()
                    }

                    Separator {}

                    // Playback Mode
                    TransportButton {
                        text: "🔀"
                        tooltip: "Shuffle"
                        checkable: true
                        id: shuffleBtn
                    }

                    TransportButton {
                        text: "🔁"
                        tooltip: "Repeat"
                        checked: true
                        id: repeatBtn
                    }

                    TransportButton {
                        text: "A-B"
                        tooltip: "Set Loop A-B"
                        id: loopBtn
                    }
                }

                Item { Layout.fillWidth: true }

                // Center Status
                Row {
                    spacing: spacingSm
                    Layout.alignment: Qt.AlignVCenter

                    StatusBadge {
                        text: "LATENCY: " + latency + "ms"
                    }

                    StatusBadge {
                        text: "DROPS: " + droppedFrames
                        color: droppedFrames > 0 ? "#ef4444" : textPrimary
                    }

                    StatusBadge {
                        text: drmActive ? "DRM" : "No DRM"
                        color: drmActive ? "#ef4444" : "#22c55e"
                    }
                }

                Item { Layout.fillWidth: true }

                // Right Controls - Utilities
                Row {
                    spacing: spacingSm
                    Layout.alignment: Qt.AlignVCenter

                    TransportButton {
                        text: "ℹ"
                        tooltip: "Metadata"
                        onClicked: toggleMetadata()
                    }

                    TransportButton {
                        text: "⚙"
                        tooltip: "Settings"
                        onClicked: toggleSettings()
                    }

                    TransportButton {
                        text: "🔆"
                        tooltip: "Theme"
                        onClicked: toggleTheme()
                    }

                    Separator {}

                    TransportButton {
                        text: "⛶"
                        tooltip: "Fullscreen"
                        onClicked: toggleFullscreen()
                    }

                    TransportButton {
                        text: "⌃"
                        tooltip: "Hide Controls"
                        onClicked: toggleUIVisibility()
                    }
                }
            }

            //=========================================================================
            // ADDITIONAL INFO BAR
            //=========================================================================

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "📁 " + (currentFile ? currentFile : "No file")
                    color: textMuted
                    font.family: jetBrainsMono.name
                    font.pixelSize: fontSizeXs
                    elide: Text.ElideLeft
                    Layout.fillWidth: true
                }

                Text {
                    text: "⚡ " + cpuUsage + "% · " + gpuUsage + "%"
                    color: textMuted
                    font.family: jetBrainsMono.name
                    font.pixelSize: fontSizeXs
                }
            }
        }
    }

    //=========================================================================
    // SETTINGS PANEL (Slide-out)
    //=========================================================================

    Rectangle {
        id: settingsPanel
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: controlsPanel.top
        width: settingsPanelVisible ? 320 : 0
        color: bgSecondary
        border.color: borderLight
        border.width: 1

        Behavior on width {
            NumberAnimation { duration: 200 }
        }

        Column {
            anchors.fill: parent
            anchors.margins: spacingLg
            visible: settingsPanelVisible
            spacing: spacingLg

            Text {
                text: "SETTINGS"
                color: accentColor
                font.family: jetBrainsMono.name
                font.pixelSize: fontSizeLg
                font.bold: true
            }

            // Settings content here
        }
    }

    //=========================================================================
    // PLAYLIST PANEL (Slide-out)
    //=========================================================================

    Rectangle {
        id: playlistPanel
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.bottom: controlsPanel.top
        width: playlistVisible ? 320 : 0
        color: bgSecondary
        border.color: borderLight
        border.width: 1

        Behavior on width {
            NumberAnimation { duration: 200 }
        }

        Column {
            anchors.fill: parent
            anchors.margins: spacingLg
            visible: playlistVisible
            spacing: spacingLg

            Text {
                text: "PLAYLIST"
                color: accentColor
                font.family: jetBrainsMono.name
                font.pixelSize: fontSizeLg
                font.bold: true
            }

            // Playlist content here
        }
    }

    //=========================================================================
    // METADATA PANEL (Slide-out)
    //=========================================================================

    Rectangle {
        id: metadataPanel
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: controlsPanel.top
        width: metadataVisible ? 320 : 0
        color: bgSecondary
        border.color: borderLight
        border.width: 1

        Behavior on width {
            NumberAnimation { duration: 200 }
        }

        Column {
            anchors.fill: parent
            anchors.margins: spacingLg
            visible: metadataVisible
            spacing: spacingLg

            Text {
                text: "METADATA"
                color: accentColor
                font.family: jetBrainsMono.name
                font.pixelSize: fontSizeLg
                font.bold: true
            }

            // Metadata content here
        }
    }

    //=========================================================================
    // FILE DIALOG
    //=========================================================================

    FileDialog {
        id: fileDialog
        title: "Open Media File"
        nameFilters: ["Media Files (*.mp4 *.mkv *.avi *.mov *.mp3 *.flac *.wav)", "All Files (*)"]
        onAccepted: {
            if (coreRef) {
                var filePath = fileDialog.file.toString().replace("file:///", "")
                coreRef.loadFile(filePath)
            }
        }
    }

    //=========================================================================
    // UTILITY FUNCTIONS
    //=========================================================================

    function formatTime(seconds) {
        if (!seconds || seconds < 0 || !isFinite(seconds)) return "00:00:00"

            var h = Math.floor(seconds / 3600)
            var m = Math.floor((seconds % 3600) / 60)
            var s = Math.floor(seconds % 60)

            return (h < 10 ? "0" + h : h) + ":" +
            (m < 10 ? "0" + m : m) + ":" +
            (s < 10 ? "0" + s : s)
    }

    function playPause() {
        if (coreRef) coreRef.playPause()
    }

    function stop() {
        if (coreRef) coreRef.stop()
    }

    function previousTrack() {
        if (coreRef) coreRef.previous()
    }

    function nextTrack() {
        if (coreRef) coreRef.next()
    }

    function seekTo(value) {
        if (coreRef && duration > 0) coreRef.seek(value)
    }

    function setVolume(value) {
        if (coreRef) {
            volume = Math.max(0, Math.min(100, value))
            coreRef.volume = volume
        }
    }

    function toggleFullscreen() {
        isFullscreen = !isFullscreen
        if (typeof Window !== 'undefined') {
            root.Window.window.visibility = isFullscreen ? Window.FullScreen : Window.Windowed
        }
    }

    function toggleUIVisibility() {
        uiVisible = !uiVisible
    }

    function toggleSettings() {
        settingsPanelVisible = !settingsPanelVisible
        if (settingsPanelVisible) {
            playlistVisible = false
            metadataVisible = false
        }
    }

    function togglePlaylist() {
        playlistVisible = !playlistVisible
        if (playlistVisible) {
            settingsPanelVisible = false
            metadataVisible = false
        }
    }

    function toggleMetadata() {
        metadataVisible = !metadataVisible
        if (metadataVisible) {
            settingsPanelVisible = false
            playlistVisible = false
        }
    }

    function toggleTheme() {
        darkTheme = !darkTheme
        if (platformRef && platformRef.saveSetting) {
            platformRef.saveSetting("theme", darkTheme)
        }
    }

    function openFileDialog() {
        fileDialog.open()
    }

    function handleKeyPress(event) {
        switch(event.key) {
            case Qt.Key_Space: playPause(); break
            case Qt.Key_Left: seekTo(position - 5000); break
            case Qt.Key_Right: seekTo(position + 5000); break
            case Qt.Key_Up: setVolume(volume + 5); break
            case Qt.Key_Down: setVolume(volume - 5); break
            case Qt.Key_F: toggleFullscreen(); break
            case Qt.Key_H: toggleUIVisibility(); break
            case Qt.Key_M: toggleMetadata(); break
            case Qt.Key_P: togglePlaylist(); break
            case Qt.Key_S: toggleSettings(); break
            case Qt.Key_T: toggleTheme(); break
            case Qt.Key_Escape:
                if (settingsPanelVisible) settingsPanelVisible = false
                    if (playlistVisible) playlistVisible = false
                        if (metadataVisible) metadataVisible = false
                            break
        }
    }

    //=========================================================================
    // KEYBOARD FOCUS
    //=========================================================================

    Keys.onPressed: handleKeyPress(event)

    //=========================================================================
    // MOUSE AREA FOR FOCUS
    //=========================================================================

    MouseArea {
        anchors.fill: parent
        onClicked: root.forceActiveFocus()
    }
}

//=============================================================================
// CUSTOM COMPONENTS
//=============================================================================

// Metric Display Component
component MetricDisplay : Row {
    property string icon
    property string value
    property string label
    property color color: accentColor

    spacing: 4

    Text {
        text: icon
        color: parent.color
        font.pixelSize: fontSizeSm
    }

    Text {
        text: value
        color: textPrimary
        font.family: jetBrainsMono.name
        font.pixelSize: fontSizeSm
        font.bold: true
    }

    Text {
        text: label
        color: textSecondary
        font.family: jetBrainsMono.name
        font.pixelSize: fontSizeSm
    }
}

// Quick Button Component
component QuickButton : Rectangle {
    property string text
    signal clicked

    width: 30
    height: 30
    color: ma.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : "transparent"
    radius: 4

    Text {
        anchors.centerIn: parent
        text: parent.text
        color: textPrimary
        font.pixelSize: fontSizeLg
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onClicked: parent.clicked()
    }
}

// Media Tag Component
component MediaTag : Rectangle {
    property string text
    property color color: textPrimary

    height: 22
    width: tagText.implicitWidth + 12
    color: Qt.rgba(1, 1, 1, 0.05)
    border.color: Qt.rgba(1, 1, 1, 0.1)
    border.width: 1
    radius: 2

    Text {
        id: tagText
        anchors.centerIn: parent
        text: parent.text
        color: parent.color
        font.family: jetBrainsMono.name
        font.pixelSize: fontSizeXxs
        font.bold: true
    }
}

// Professional Seek Bar Component
component SeekBar : Rectangle {
    property real position
    property real duration
    property int bufferPercent
    signal seek(real value)

    height: 20
    color: bgTertiary
    border.color: borderMedium
    border.width: 1
    radius: 3
    clip: true

    // Buffer indicator
    Rectangle {
        height: parent.height
        width: parent.width * (bufferPercent / 100)
        color: Qt.rgba(1, 1, 1, 0.1)
        radius: parent.radius
    }

    // Position indicator
    Rectangle {
        height: parent.height
        width: parent.width * (duration > 0 ? position / duration : 0)
        color: accentColor
        radius: parent.radius

        // Gradient effect
        gradient: Gradient {
            GradientStop { position: 0.0; color: accentColor }
            GradientStop { position: 1.0; color: Qt.lighter(accentColor, 1.2) }
        }

        // Animated pulse on hover
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            running: seekMa.containsMouse
            NumberAnimation { from: 1; to: 0.8; duration: 800 }
            NumberAnimation { from: 0.8; to: 1; duration: 800 }
        }
    }

    // Text overlay
    Text {
        anchors.centerIn: parent
        text: Math.round(duration > 0 ? (position / duration) * 100 : 0) + "%"
        color: textPrimary
        font.family: jetBrainsMono.name
        font.pixelSize: fontSizeXs
        font.bold: true
        style: Text.Outline
        styleColor: "black"
    }

    // Mouse interaction
    MouseArea {
        id: seekMa
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            if (duration > 0) {
                var newPos = (mouse.x / width) * duration
                seek(newPos)
            }
        }

        // Tooltip with time
        ToolTip {
            parent: seekMa
            visible: seekMa.containsMouse
            text: formatTime((mouseX / width) * duration)
            delay: 100
        }
    }
}

// Volume Slider Component
component VolumeSlider : Rectangle {
    property real volume

    signal volumeChanged(real value)

    height: 20
    color: bgTertiary
    border.color: borderMedium
    border.width: 1
    radius: 3
    clip: true

    Rectangle {
        height: parent.height
        width: parent.width * (volume / 100)
        color: accentColor
        radius: parent.radius

        gradient: Gradient {
            GradientStop { position: 0.0; color: accentColor }
            GradientStop { position: 1.0; color: Qt.lighter(accentColor, 1.2) }
        }
    }

    Text {
        anchors.centerIn: parent
        text: Math.round(volume) + "%"
        color: textPrimary
        font.family: jetBrainsMono.name
        font.pixelSize: fontSizeXs
        font.bold: true
        style: Text.Outline
        styleColor: "black"
    }

    MouseArea {
        anchors.fill: parent
        onClicked: volumeChanged((mouse.x / width) * 100)
    }
}

// Audio Visualizer Component
component AudioVisualizer : Row {
    property var audioRef
    property int barCount: 16
    property color accentColor

    spacing: 2

    Repeater {
        model: barCount

        Rectangle {
            width: (parent.width - (barCount - 1) * parent.spacing) / barCount
            height: parent.height * getBarHeight(index)
            color: accentColor
            opacity: 0.7
            radius: 1

            Behavior on height {
                NumberAnimation { duration: 50 }
            }

            function getBarHeight(idx) {
                if (!audioRef || !audioRef.spectrumData) return 0.1

                    var spectrum = audioRef.spectrumData
                    if (!Array.isArray(spectrum) || spectrum.length === 0) return 0.1

                        var spectrumIndex = Math.floor((idx / barCount) * spectrum.length)
                        var value = Math.min(1, Math.max(0, spectrum[spectrumIndex] || 0))

                        // Add some noise for visual interest
                        var noise = Math.random() * 0.05
                        return 0.1 + (value * 0.8) + noise
            }
        }
    }
}

// Transport Button Component
component TransportButton : Rectangle {
    property string text
    property string tooltip
    property bool checkable: false
    property bool checked: false

    signal clicked

    width: controlHeight
    height: controlHeight
    color: {
        if (checkable && checked) return accentColor
            return ma.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : "transparent"
    }
    radius: 4
    border.color: checkable && checked ? accentColor : "transparent"
    border.width: checkable && checked ? 1 : 0

    Text {
        anchors.centerIn: parent
        text: parent.text
        color: (checkable && checked) ? "white" : textPrimary
        font.family: jetBrainsMono.name
        font.pixelSize: fontSizeLg
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            if (checkable) checked = !checked
                parent.clicked()
        }
    }

    ToolTip {
        text: parent.tooltip
        visible: ma.containsMouse && parent.tooltip !== ""
        delay: 500
    }
}

// Status Badge Component
component StatusBadge : Rectangle {
    property string text
    property color color: textPrimary

    height: 22
    width: statusText.implicitWidth + 12
    color: Qt.rgba(1, 1, 1, 0.05)
    border.color: borderLight
    border.width: 1
    radius: 2

    Text {
        id: statusText
        anchors.centerIn: parent
        text: parent.text
        color: parent.color
        font.family: jetBrainsMono.name
        font.pixelSize: fontSizeXs
        font.bold: true
    }
}

// Badge Component
component Badge : Rectangle {
    width: 8
    height: 8
    radius: 4
    color: accentColor
}

// Separator Component
component Separator : Rectangle {
    width: 1
    height: controlHeight - 10
    color: borderLight
    anchors.verticalCenter: parent.verticalCenter
}
