// ui_player_pro.qml - Professional Aegis Player UI

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    anchors.fill: parent
    color: "#050505"

    // Backend references
    property var coreRef: typeof Core !== 'undefined' ? Core : null
    property var audioRef: typeof Audio !== 'undefined' ? Audio : null
    property var platformRef: typeof Platform !== 'undefined' ? Platform : null

    // UI State
    property bool isPlaying: coreRef ? coreRef.playing : false
    property real position: coreRef ? coreRef.position : 0
    property real duration: coreRef ? coreRef.duration : 0
    property real volume: coreRef ? coreRef.volume : 100
    property string currentFile: coreRef && coreRef.currentFile ? coreRef.currentFile : ""
    property bool hasVideo: coreRef ? coreRef.hasVideo : false

    // Telemetry state
    property int cpuUsage: 12
    property int gpuUsage: 34
    property int bufferPercent: 85
    property real playbackSpeed: 1.0

    // Layout constants
    readonly property color accentColor: "#ff8c00"
    readonly property color panelBg: "#121212"
    readonly property color borderColor: "#2a2a2a"

    FontLoader {
        id: jetBrainsMono
        source: "qrc:/fonts/JetBrainsMono-Regular.ttf"  // Fallback to system monospace if unavailable
    }

    // Telemetry updater
    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            cpuUsage = 10 + Math.floor(Math.random() * 5)
            gpuUsage = 30 + Math.floor(Math.random() * 10)
            bufferPercent = 80 + Math.floor(Math.random() * 15)
        }
    }

    // Video Display Area
    Rectangle {
        id: videoContainer
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: controlsPanel.top
        color: "#000000"
        clip: true

        // Actual video output (MPV renders here)
        Rectangle {
            id: videoOutput
            anchors.fill: parent
            color: "black"
            visible: hasVideo && isPlaying

            // In real implementation, this would be the MPV video texture
            // For now, placeholder with album art or black
        }

        // Placeholder image when no video
        Rectangle {
            id: placeholder
            anchors.fill: parent
            visible: !videoOutput.visible
            color: "#0a0a0a"

            Image {
                anchors.fill: parent
                source: "qrc:/images/placeholder_bg.jpg"  // Fallback to gradient if no image
                fillMode: Image.PreserveAspectCrop
                opacity: 0.6
                visible: status === Image.Ready
            }

            // Gradient fallback
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#1a1a1a" }
                    GradientStop { position: 1.0; color: "#000000" }
                }
                visible: !parent.visible
            }
        }

        // Telemetry Overlay
        Row {
            id: telemetryTop
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 20
            spacing: 0

            Text {
                text: "CPU: " + cpuUsage + "% | GPU: " + gpuUsage + "% | RAM: 1.2GB "
                color: "#aaaaaa"
                font.family: jetBrainsMono.name || "JetBrains Mono"
                font.pixelSize: 11
                font.bold: true
            }

            Text {
                text: "● REC: READY"
                color: "#22c55e"
                font.family: jetBrainsMono.name || "JetBrains Mono"
                font.pixelSize: 11
                font.bold: true
            }

            Item { Layout.fillWidth: true; width: parent.width * 0.6 }

            Text {
                text: "NET: 120 MBPS | " + Qt.formatDateTime(new Date(), "MMM dd yyyy - hh:mm:ss").toUpperCase()
                color: "#aaaaaa"
                font.family: jetBrainsMono.name || "JetBrains Mono"
                font.pixelSize: 11
                font.bold: true
            }
        }
    }

    // Control Panel
    Rectangle {
        id: controlsPanel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 230
        color: panelBg
        border.color: borderColor
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15

            // Top Module Row (Info + Audio)
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 125
                spacing: 15

                // Info Module (72% width)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * 0.72
                    color: Qt.rgba(1, 0.55, 0, 0.03)  // Orange tint 3%
                    border.color: Qt.rgba(1, 0.55, 0, 0.15)
                    border.width: 1
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 20

                        // Left column - Time
                        ColumnLayout {
                            spacing: 4
                            Layout.minimumWidth: 140

                            Text {
                                id: timeDisplay
                                text: formatTime(position)
                                color: accentColor
                                font.family: jetBrainsMono.name || "JetBrains Mono"
                                font.pixelSize: 32
                                font.bold: true
                                textFormat: Text.PlainText
                            }

                            Text {
                                text: "REMAINING: -" + formatTime(duration - position)
                                color: accentColor
                                opacity: 0.7
                                font.family: jetBrainsMono.name || "JetBrains Mono"
                                font.pixelSize: 9
                                font.bold: true
                            }

                            Rectangle {
                                color: Qt.rgba(1, 1, 1, 0.05)
                                border.color: "transparent"
                                height: 20
                                width: parent.width

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 6
                                    text: "SRC: " + (currentFile.startsWith("http") ? "REMOTE_HTTPS" : "LOCAL_FILE")
                                    color: "white"
                                    font.family: jetBrainsMono.name || "JetBrains Mono"
                                    font.pixelSize: 10
                                    font.bold: true
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    width: 2
                                    height: parent.height
                                    color: accentColor
                                }
                            }
                        }

                        // Separator
                        Rectangle {
                            Layout.fillHeight: true
                            width: 1
                            color: Qt.rgba(1, 1, 1, 0.1)
                        }

                        // Right column - File info & Seek
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 4

                            Text {
                                text: currentFile ? currentFile.split('/').pop() : "No file loaded"
                                color: "white"
                                font.family: jetBrainsMono.name || "JetBrains Mono"
                                font.pixelSize: 14
                                font.bold: true
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }

                            // Badges
                            Row {
                                spacing: 6
                                Layout.fillWidth: true

                                Repeater {
                                    model: [
                                        { text: hasVideo ? "VIDEO ACTIVE" : "AUDIO ONLY", color: "#ffffff" },
                                        { text: "H.265 / HEVC", color: "#ffffff" },
                                        { text: "45.2 MBPS", color: "#ffffff" },
                                        { text: "A/R: 16:9", color: "#4da6ff" }
                                    ]

                                    Rectangle {
                                        color: Qt.rgba(1, 1, 1, 0.05)
                                        border.color: modelData.color === "#4da6ff" ? Qt.rgba(0.3, 0.65, 1, 0.3) : Qt.rgba(1, 1, 1, 0.1)
                                        border.width: 1
                                        radius: 2
                                        height: 20
                                        width: badgeText.implicitWidth + 12

                                        Text {
                                            id: badgeText
                                            anchors.centerIn: parent
                                            text: modelData.text
                                            color: modelData.color
                                            font.family: jetBrainsMono.name || "JetBrains Mono"
                                            font.pixelSize: 8
                                            font.bold: true
                                        }
                                    }
                                }
                            }

                            Item { Layout.fillHeight: true }  // Spacer

                            // Seek Bar Container
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                // Custom Seek Slider
                                RowLayout {
                                    spacing: 10
                                    Layout.fillWidth: true

                                    Text {
                                        text: "⟳"
                                        color: accentColor
                                        font.pixelSize: 12
                                        Layout.preferredWidth: 14
                                        horizontalAlignment: Text.AlignHCenter

                                        // Tooltip behavior
                                        ToolTip.text: "Timeline"
                                        ToolTip.visible: ma.containsMouse
                                        ToolTip.delay: 500

                                        MouseArea {
                                            id: ma
                                            anchors.fill: parent
                                            hoverEnabled: true
                                        }
                                    }

                                    // Custom Slider with Buffer
                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 16
                                        color: "#1a1a1a"
                                        border.color: "#333333"
                                        border.width: 1
                                        radius: 2
                                        clip: true

                                        // Buffer fill
                                        Rectangle {
                                            height: parent.height
                                            width: parent.width * (bufferPercent / 100)
                                            color: Qt.rgba(1, 1, 1, 0.1)
                                        }

                                        // Position fill
                                        Rectangle {
                                            height: parent.height
                                            width: parent.width * (duration > 0 ? position / duration : 0)
                                            color: accentColor
                                        }

                                        // Text overlay
                                        Text {
                                            anchors.centerIn: parent
                                            text: "SEEK: " + Math.round(duration > 0 ? (position / duration) * 100 : 0) + "%"
                                            color: "white"
                                            font.family: jetBrainsMono.name || "JetBrains Mono"
                                            font.pixelSize: 9
                                            font.bold: true
                                            style: Text.Outline
                                            styleColor: "black"
                                        }

                                        // Interaction area
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: {
                                                if (coreRef && duration > 0) {
                                                    var newPos = (mouse.x / width) * duration
                                                    coreRef.seek(newPos)
                                                }
                                            }
                                        }
                                    }
                                }

                                // Sub-info row
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 24  // Align with slider (icon width + spacing)

                                    Row {
                                        spacing: 20
                                        Layout.fillWidth: true

                                        Text {
                                            text: "SPEED: <font color='" + accentColor + "'>" + playbackSpeed.toFixed(1) + "x</font>"
                                            color: "white"
                                            font.family: jetBrainsMono.name || "JetBrains Mono"
                                            font.pixelSize: 9
                                            font.bold: true
                                            textFormat: Text.RichText
                                        }

                                        Text {
                                            text: "BUFFER: <font color='#4da6ff'>" + bufferPercent + "%</font>"
                                            color: "white"
                                            font.family: jetBrainsMono.name || "JetBrains Mono"
                                            font.pixelSize: 9
                                            font.bold: true
                                            textFormat: Text.RichText
                                        }
                                    }

                                    Text {
                                        text: "CURR_FRAME: " + Math.round(position * 30) + " / " + Math.round(duration * 30)
                                        color: "#666666"
                                        font.family: jetBrainsMono.name || "JetBrains Mono"
                                        font.pixelSize: 9
                                    }
                                }
                            }
                        }
                    }
                }

                // Audio Module
                Rectangle {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    color: Qt.rgba(1, 0.55, 0, 0.03)
                    border.color: Qt.rgba(1, 0.55, 0, 0.15)
                    border.width: 1
                    radius: 4

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 10

                        // Visualizer
                        Row {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 2

                            Repeater {
                                id: visualizerRepeater
                                model: 16

                                Rectangle {
                                    width: (parent.width - (15 * 2)) / 16
                                    height: parent.height * (audioRef ? getBarHeight(index) : 0.1)
                                    color: accentColor
                                    opacity: 0.6

                                    // Animation
                                    Behavior on height {
                                        NumberAnimation { duration: 50 }
                                    }

                                    function getBarHeight(idx) {
                                        if (!audioRef) return 0.1
                                            var spectrum = audioRef.spectrumData
                                            if (!spectrum || spectrum.length === 0) return 0.1
                                                // Map 16 visualizer bars to spectrum data
                                                var spectrumIndex = Math.floor((idx / 16) * spectrum.length)
                                                var value = spectrum[spectrumIndex] || 0
                                                return 0.1 + (value * 0.9)  // Min 10%, max 100%
                                    }
                                }
                            }
                        }

                        // Volume Control
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            RowLayout {
                                spacing: 10

                                Text {
                                    text: "🔊"
                                    color: accentColor
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 14
                                    horizontalAlignment: Text.AlignHCenter
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 16
                                    color: "#1a1a1a"
                                    border.color: "#333333"
                                    border.width: 1
                                    radius: 2

                                    Rectangle {
                                        height: parent.height
                                        width: parent.width * (volume / 100)
                                        color: accentColor
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        text: "VOL: " + Math.round(volume) + "%"
                                        color: "white"
                                        font.family: jetBrainsMono.name || "JetBrains Mono"
                                        font.pixelSize: 9
                                        font.bold: true
                                        style: Text.Outline
                                        styleColor: "black"
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            if (coreRef) {
                                                var newVol = (mouse.x / width) * 100
                                                coreRef.volume = newVol
                                            }
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.leftMargin: 24

                                Text {
                                    text: "STEREO_OUT"
                                    color: "#888888"
                                    font.family: jetBrainsMono.name || "JetBrains Mono"
                                    font.pixelSize: 9
                                    opacity: 0.5
                                }
                            }
                        }
                    }
                }
            }

            // Button Bar
            RowLayout {
                Layout.fillWidth: true
                spacing: 0

                // Left Group
                Row {
                    spacing: 10
                    Layout.alignment: Qt.AlignVCenter

                    // Utility function for styled buttons
                    component IconButton : Rectangle {
                        property alias text: btnText.text
                        property alias tooltip: btnTip.text
                        signal clicked()

                        width: 28
                        height: 28
                        color: btnMa.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : "transparent"
                        radius: 4

                        Text {
                            id: btnText
                            anchors.centerIn: parent
                            color: "white"
                            font.family: jetBrainsMono.name || "JetBrains Mono"
                            font.pixelSize: 14
                        }

                        ToolTip {
                            id: btnTip
                            delay: 500
                            visible: btnMa.containsMouse && text !== ""
                        }

                        MouseArea {
                            id: btnMa
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: parent.clicked()
                        }
                    }

                    IconButton { text: "☰"; tooltip: "Playlist"; onClicked: {} }
                    IconButton { text: "⏏"; tooltip: "Eject"; onClicked: { if (coreRef) coreRef.stop() } }

                    Rectangle { width: 1; height: 16; color: "#333333"; anchors.verticalCenter: parent.verticalCenter }

                    IconButton { text: "⏮"; tooltip: "Previous"; onClicked: { if (coreRef) coreRef.previous() } }

                    Rectangle {
                        width: 40
                        height: 28
                        color: Qt.rgba(1, 1, 1, 0.1)
                        border.color: Qt.rgba(1, 1, 1, 0.3)
                        border.width: 1
                        radius: 4

                        Text {
                            anchors.centerIn: parent
                            text: isPlaying ? "⏸" : "▶"
                            color: "white"
                            font.pixelSize: 18
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: { if (coreRef) coreRef.playPause() }
                        }
                    }

                    IconButton { text: "⏭"; tooltip: "Next"; onClicked: { if (coreRef) coreRef.next() } }
                    IconButton { text: "⬛"; tooltip: "Stop"; onClicked: { if (coreRef) coreRef.stop() } }

                    Rectangle { width: 1; height: 16; color: "#333333"; anchors.verticalCenter: parent.verticalCenter }

                    IconButton {
                        id: shuffleBtn
                        text: "🔀"
                        tooltip: "Shuffle (" + (active ? "ON" : "OFF") + ")"
                        property bool active: false
                        color: active ? accentColor : "white"
                        onClicked: active = !active
                    }

                    IconButton {
                        id: loopBtn
                        text: "🔁"
                        tooltip: "Repeat (" + (active ? "ON" : "OFF") + ")"
                        property bool active: true
                        color: active ? accentColor : "white"
                        onClicked: active = !active
                    }

                    IconButton { text: "[A-B]"; tooltip: "Set Loop A-B"; onClicked: {} }
                }

                Item { Layout.fillWidth: true }  // Center spacer

                // Center Group (Status)
                Row {
                    spacing: 10
                    Layout.alignment: Qt.AlignVCenter

                    Rectangle {
                        color: "#1a1a1a"
                        border.color: "#333333"
                        border.width: 1
                        height: 20
                        width: tagText.implicitWidth + 16

                        Text {
                            id: tagText
                            anchors.centerIn: parent
                            text: "LATENCY: 42ms"
                            color: "white"
                            font.family: jetBrainsMono.name || "JetBrains Mono"
                            font.pixelSize: 9
                        }
                    }

                    Rectangle {
                        color: "#1a1a1a"
                        border.color: "#333333"
                        border.width: 1
                        height: 20
                        width: drmText.implicitWidth + 16

                        Text {
                            id: drmText
                            anchors.centerIn: parent
                            text: "DRM: ACTIVE"
                            color: "white"
                            font.family: jetBrainsMono.name || "JetBrains Mono"
                            font.pixelSize: 9
                        }
                    }
                }

                Item { Layout.fillWidth: true }  // Right spacer

                // Right Group
                Row {
                    spacing: 10
                    Layout.alignment: Qt.AlignVCenter

                    IconButton { text: "ℹ"; tooltip: "Metadata"; onClicked: {} }
                    IconButton { text: "⚙"; tooltip: "Settings"; onClicked: {} }
                    IconButton { text: "⛶"; tooltip: "Fullscreen"; onClicked: {} }
                }
            }
        }
    }

    // Utility function
    function formatTime(seconds) {
        if (!seconds || seconds < 0) return "00:00:00"
            var h = Math.floor(seconds / 3600)
            var m = Math.floor((seconds % 3600) / 60)
            var s = Math.floor(seconds % 60)
            return (h < 10 ? "0" + h : h) + ":" +
            (m < 10 ? "0" + m : m) + ":" +
            (s < 10 ? "0" + s : s)
    }
}
