import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    visible: true
    width: 1200
    height: 800
    title: "Aegis DJ Mixer"

    // Initialize mixer
    Component.onCompleted: DJ.startSession()

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // Decks
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20

            // Deck A
            DeckWidget {
                Layout.fillWidth: true
                Layout.fillHeight: true
                deck: DJ.deckA
                color: "#2196F3"  // Blue
            }

            // Mixer Section
            MixerWidget {
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                mixer: DJ.mixer
            }

            // Deck B
            DeckWidget {
                Layout.fillWidth: true
                Layout.fillHeight: true
                deck: DJ.deckB
                color: "#FF5722"  // Orange
            }
        }
    }
}

// Component: DeckWidget.qml (inline for example)
component DeckWidget: Rectangle {
    property var deck
    property color accentColor: "#2196F3"

    color: "#1a1a1a"
    border.color: accentColor
    border.width: 2

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        // Track Info
        Text {
            text: deck.trackName
            color: "#ffffff"
            font.bold: true
            font.pixelSize: 16
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        // Waveform
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            color: "#0a0a0a"
            border.color: "#333333"

            // Simplified waveform visualization
            Canvas {
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.fillStyle = accentColor

                    // Draw waveform bars
                    for (var i = 0; i < 50; i++) {
                        var h = Math.random() * height * 0.8
                        ctx.fillRect(i * (width/50), (height-h)/2, (width/50)-2, h)
                    }

                    // Playhead
                    var pos = deck.waveformPosition * width
                    ctx.fillStyle = "#ffffff"
                    ctx.fillRect(pos-1, 0, 2, height)
                }
            }
        }

        // Transport
        RowLayout {
            spacing: 10
            Button { text: "CUE"; onClicked: deck.cue() }
            Button {
                text: deck.state === 1 ? "⏸" : "▶"  // Playing or not
                onClicked: deck.state === 1 ? deck.pause() : deck.play()
            }
            Button { text: "⏹"; onClicked: deck.stop() }
        }

        // Pitch Slider
        Slider {
            Layout.fillWidth: true
            from: -0.5
            to: 0.5
            value: deck.pitch
            onValueChanged: deck.pitch = value
        }
        Text {
            text: "Pitch: " + (deck.pitch * 100).toFixed(1) + "%"
            color: "#ffffff"
            font.family: "monospace"
        }

        // EQ
        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            Slider { orientation: Qt.Vertical; from: 0; to: 2; value: 1 }
            Slider { orientation: Qt.Vertical; from: 0; to: 2; value: 1 }
            Slider { orientation: Qt.Vertical; from: 0; to: 2; value: 1 }
        }
        RowLayout {
            Text { text: "LOW"; color: "#aaaaaa"; font.pixelSize: 9 }
            Text { text: "MID"; color: "#aaaaaa"; font.pixelSize: 9 }
            Text { text: "HI"; color: "#aaaaaa"; font.pixelSize: 9 }
        }
    }
}

// Component: MixerWidget.qml (inline)
component MixerWidget: Rectangle {
    property var mixer

    color: "#2a2a2a"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 20

        // Crossfader
        Text {
            text: "CROSSFADER"
            color: "#ffffff"
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        Slider {
            Layout.fillWidth: true
            from: -1
            to: 1
            value: mixer.crossfader
            onValueChanged: mixer.crossfader = value
        }

        RowLayout {
            Text { text: "A"; color: "#2196F3"; font.bold: true }
            Item { Layout.fillWidth: true }
            Text { text: "B"; color: "#FF5722"; font.bold: true }
        }

        // Master Volume
        Text {
            text: "MASTER"
            color: "#ffffff"
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        Slider {
            Layout.fillWidth: true
            from: 0
            to: 1.5
            value: mixer.masterVolume
            onValueChanged: mixer.masterVolume = value
        }

        // Sync Controls
        Button {
            Layout.fillWidth: true
            text: "SYNC A"
            onClicked: mixer.syncToDeck(1)
        }
        Button {
            Layout.fillWidth: true
            text: "SYNC B"
            onClicked: mixer.syncToDeck(2)
        }
        Button {
            Layout.fillWidth: true
            text: "AUTO SYNC"
            onClicked: mixer.autoSync()
        }

        // BPM Display
        Text {
            text: "BPM: " + mixer.masterBpm.toFixed(1)
            color: "#00ff00"
            font.family: "monospace"
            font.bold: true
            font.pixelSize: 24
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
