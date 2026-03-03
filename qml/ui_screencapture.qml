// ui_screencapture.qml - Screen capture UI for Aegis Media Suite
//
// CORRELATION NOTES:
// - Used by: main.qml when in capture mode
// - Depends on: Capture backend for screen recording
// - Provides: OpenScreen-compatible recording controls

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Aegis 1.0

Rectangle {
    id: root
    color: "#1a1a1a"

    // Properties
    property var captureBackend: null
    property bool autoZoomEnabled: false

    // Timer for updating duration display
    Timer {
        id: durationTimer
        interval: 100
        running: captureBackend && captureBackend.recording
        repeat: true
        onTriggered: {
            if (captureBackend && captureBackend.recording) {
                // Update duration display logic here
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15

        // Header
        Text {
            text: "🎥 Screen Capture"
            color: "white"
            font.pixelSize: 24
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        // Recording Controls (OpenScreen style)
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 20

            Button {
                id: recordButton
                text: captureBackend && captureBackend.recording ? "Recording..." : "Start Recording"
                enabled: captureBackend ? true : false

                onClicked: {
                    if (captureBackend) {
                        if (!captureBackend.recording) {
                            captureBackend.requestScreenCapture()
                        } else {
                            captureBackend.stopRecording()
                        }
                    }
                }

                background: Rectangle {
                    color: recordButton.enabled
                    ? (captureBackend && captureBackend.recording ? "#ff4444" : "#44ff44")
                    : "#666666"
                    radius: 25
                }

                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pixelSize: 16
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Button {
                text: "Stop"
                enabled: captureBackend && captureBackend.recording
                onClicked: {
                    if (captureBackend) {
                        captureBackend.stopRecording()
                    }
                }

                background: Rectangle {
                    color: parent.enabled ? "#ff4444" : "#666666"
                    radius: 25
                }

                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pixelSize: 16
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // Duration Display
        Label {
            text: formatTime(captureBackend ? captureBackend.recordedDuration || 0 : 0)
            color: "white"
            font.pixelSize: 24
            font.family: "monospace"
            Layout.alignment: Qt.AlignHCenter
        }

        // Auto-zoom Toggle (OpenScreen feature)
        Switch {
            text: "Auto-zoom on cursor"
            checked: autoZoomEnabled
            onCheckedChanged: autoZoomEnabled = checked

            contentItem: Text {
                text: parent.text
                color: "white"
                font.pixelSize: 14
                verticalAlignment: Text.AlignVCenter
                leftPadding: parent.indicator.width + parent.spacing
            }
        }

        // Background Selector
        GroupBox {
            title: "Background"
            Layout.fillWidth: true

            background: Rectangle {
                color: "#2a2a2a"
                border.color: "#3a3a3a"
                radius: 4
            }

            label: Text {
                text: parent.title
                color: "white"
                font.pixelSize: 12
                leftPadding: 10
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                Button {
                    text: "Gradient"
                    Layout.fillWidth: true
                    onClicked: setBackground("gradient")
                }
                Button {
                    text: "Image"
                    Layout.fillWidth: true
                    onClicked: setBackground("image")
                }
                Button {
                    text: "Color"
                    Layout.fillWidth: true
                    onClicked: setBackground("color")
                }
            }
        }

        // Export Options
        GroupBox {
            title: "Export Settings"
            Layout.fillWidth: true

            background: Rectangle {
                color: "#2a2a2a"
                border.color: "#3a3a3a"
                radius: 4
            }

            label: Text {
                text: parent.title
                color: "white"
                font.pixelSize: 12
                leftPadding: 10
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                CheckBox {
                    text: "Export as GIF"
                    checked: captureBackend ? captureBackend.exportOptions.exportGif : false
                    onCheckedChanged: {
                        if (captureBackend) {
                            var opts = captureBackend.exportOptions
                            opts.exportGif = checked
                            captureBackend.setExportOptions(opts)
                        }
                    }

                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 12
                        leftPadding: parent.indicator.width + parent.spacing
                    }
                }

                RowLayout {
                    Text {
                        text: "Resolution:"
                        color: "#aaaaaa"
                        font.pixelSize: 12
                    }

                    TextField {
                        text: captureBackend ? captureBackend.exportOptions.width : "1920"
                        placeholderText: "Width"
                        validator: IntValidator { bottom: 1; top: 7680 }
                        Layout.preferredWidth: 80
                        onEditingFinished: {
                            if (captureBackend) {
                                var opts = captureBackend.exportOptions
                                opts.width = parseInt(text)
                                captureBackend.setExportOptions(opts)
                            }
                        }
                    }

                    Text {
                        text: "x"
                        color: "white"
                    }

                    TextField {
                        text: captureBackend ? captureBackend.exportOptions.height : "1080"
                        placeholderText: "Height"
                        validator: IntValidator { bottom: 1; top: 4320 }
                        Layout.preferredWidth: 80
                        onEditingFinished: {
                            if (captureBackend) {
                                var opts = captureBackend.exportOptions
                                opts.height = parseInt(text)
                                captureBackend.setExportOptions(opts)
                            }
                        }
                    }
                }
            }
        }
    }

    // Helper functions
    function formatTime(frames) {
        if (!frames || frames < 0) return "00:00"
            let seconds = Math.floor(frames / 30)
            let mins = Math.floor(seconds / 60)
            let secs = seconds % 60
            return `${mins.toString().padStart(2,'0')}:${secs.toString().padStart(2,'0')}`
    }

    function setBackground(type) {
        if (!captureBackend) return

            var bg = captureBackend.background
            switch(type) {
                case "gradient":
                    bg.kind = BackgroundKind.Gradient
                    bg.gradientFrom = "#000000"
                    bg.gradientTo = "#333333"
                    break
                case "image":
                    bg.kind = BackgroundKind.Image
                    bg.imagePath = ""
                    break
                case "color":
                    bg.kind = BackgroundKind.SolidColor
                    bg.color = "#000000"
                    break
            }
            captureBackend.setBackground(bg)
    }

    // Connect to backend signals
    Connections {
        target: captureBackend
        function onRecordingChanged() {
            // Update UI when recording state changes
        }
        function onStreamReady(pipeline, nodeId) {
            console.log("Stream ready:", pipeline, "Node ID:", nodeId)
        }
        function onError(message) {
            console.error("Capture error:", message)
        }
    }
}
