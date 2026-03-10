// ui_karaoke.qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

Rectangle {
    id: root
    color: "#1a1a2e"

    KaraokeController {
        id: karaoke
        onSongChanged: updateNowPlaying()
        onQueueChanged: queueModel.update()
        onSingerChanged: singersModel.update()
    }

    // Split view layout
    RowLayout {
        anchors.fill: parent
        spacing: 5

        // Left panel - Queue and singers
        Rectangle {
            Layout.preferredWidth: 350
            Layout.fillHeight: true
            color: "#16213e"
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                // Controls
                RowLayout {
                    Button {
                        text: karaoke.active ? "Stop Karaoke" : "Start Karaoke"
                        onClicked: karaoke.active ? karaoke.stopKaraoke() : karaoke.startKaraoke()
                        background: Rectangle {
                            color: parent.down ? "#0f3460" : (karaoke.active ? "#e94560" : "#0f3460")
                            radius: 4
                        }
                    }

                    Button {
                        text: karaoke.paused ? "Resume" : "Pause"
                        enabled: karaoke.active
                        onClicked: karaoke.togglePause()
                    }

                    Button {
                        text: "Next"
                        enabled: karaoke.active && karaoke.queueSize > 0
                        onClicked: karaoke.nextSong()
                    }
                }

                // Current rotation
                GroupBox {
                    title: "Rotation #" + karaoke.rotationNumber
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200

                    ListView {
                        id: singersView
                        anchors.fill: parent
                        model: ListModel { id: singersModel }
                        delegate: Rectangle {
                            width: singersView.width
                            height: 40
                            color: model.isCurrent ? "#0f3460" : "transparent"
                            border.color: model.color

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 5

                                Rectangle {
                                    width: 30
                                    height: 30
                                    radius: 15
                                    color: model.color

                                    Text {
                                        anchors.centerIn: parent
                                        text: model.position
                                        color: "white"
                                        font.bold: true
                                    }
                                }

                                Text {
                                    text: model.displayName
                                    color: "white"
                                    font.pixelSize: 14
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: model.songsSung
                                    color: "#e94560"
                                    font.bold: true
                                }
                            }
                        }

                        function update() {
                            clear()
                            var singers = karaoke.singers()
                            for (var i = 0; i < singers.length; i++) {
                                var singer = singers[i]
                                singer.isCurrent = (singer.id === karaoke.currentSinger)
                                append(singer)
                            }
                        }
                    }
                }

                // Queue
                GroupBox {
                    title: "Queue (" + karaoke.queueSize + " songs)"
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: queueView
                        anchors.fill: parent
                        model: ListModel { id: queueModel }
                        clip: true

                        delegate: Rectangle {
                            width: queueView.width
                            height: 60
                            color: model.isPlaying ? "#0f3460" : (model.isCompleted ? "#1a1a1a" : "transparent")
                            border.color: model.isPlaying ? "#e94560" : "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 5

                                ColumnLayout {
                                    Text {
                                        text: model.title
                                        color: "white"
                                        font.bold: model.isPlaying
                                        font.pixelSize: 14
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: model.artist + " • " + formatDuration(model.duration)
                                        color: "#8a8a8a"
                                        font.pixelSize: 12
                                    }
                                }

                                ColumnLayout {
                                    Text {
                                        text: model.singerName || "Open"
                                        color: "#e94560"
                                        font.pixelSize: 12
                                    }

                                    Text {
                                        text: model.keyChange !== "0" ? model.keyChange : ""
                                        color: "#00b894"
                                        font.bold: true
                                        visible: model.keyChange !== "0"
                                    }
                                }
                            }
                        }

                        function update() {
                            clear()
                            var queue = karaoke.queue()
                            for (var i = 0; i < queue.length; i++) {
                                append(queue[i])
                            }
                        }
                    }
                }
            }
        }

        // Center panel - Main display
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"

            // CDG/Video display
            Item {
                id: videoDisplay
                anchors.fill: parent

                // This would be connected to CDG decoder output
                Image {
                    id: cdgImage
                    anchors.centerIn: parent
                    width: Math.min(parent.width, 600)
                    height: Math.min(parent.height, 432)
                    fillMode: Image.PreserveAspectFit
                }

                // Lyrics overlay
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 120
                    color: Qt.rgba(0, 0, 0, 0.7)

                    Text {
                        id: lyricsText
                        anchors.centerIn: parent
                        color: "yellow"
                        font.pixelSize: 32
                        font.bold: true
                        text: karaoke.currentLyricLine
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                // Now playing info
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 60
                    color: Qt.rgba(0, 0, 0, 0.7)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10

                        ColumnLayout {
                            Text {
                                text: karaoke.currentSongTitle || "No song playing"
                                color: "white"
                                font.pixelSize: 18
                                font.bold: true
                            }

                            Text {
                                text: karaoke.currentSingerName ? "Now singing: " + karaoke.currentSingerName : "Open mic"
                                color: "#e94560"
                                font.pixelSize: 14
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: formatTime(karaoke.position) + " / " + formatTime(karaoke.duration)
                            color: "white"
                            font.pixelSize: 16
                        }
                    }
                }
            }

            // Pitch monitor
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: 200
                height: 100
                color: Qt.rgba(0, 0, 0, 0.5)
                radius: 8
                visible: pitchMonitor.checked

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10

                    Text {
                        text: karaoke.currentNote || "A4"
                        color: "white"
                        font.pixelSize: 24
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }

                    // Pitch indicator
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 20
                        color: "#333"
                        radius: 10

                        Rectangle {
                            id: pitchIndicator
                            width: 40
                            height: parent.height
                            radius: 10
                            color: Math.abs(karaoke.currentCents) < 20 ? "#00b894" :
                            Math.abs(karaoke.currentCents) < 50 ? "#fdcb6e" : "#e17055"
                            x: (parent.width - width) / 2 + (karaoke.currentCents / 100) * (parent.width / 2)
                            Behavior on x { NumberAnimation { duration: 100 } }
                        }
                    }

                    Text {
                        text: karaoke.currentCents ? karaoke.currentCents.toFixed(0) + " cents" : ""
                        color: "#b2bec3"
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // Right panel - Song browser
        Rectangle {
            Layout.preferredWidth: 350
            Layout.fillHeight: true
            color: "#16213e"
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10

                // Search box
                TextField {
                    id: searchBox
                    placeholderText: "Search songs..."
                    Layout.fillWidth: true
                    onTextChanged: searchTimer.restart()

                    Timer {
                        id: searchTimer
                        interval: 300
                        onTriggered: searchResultsModel.update(searchBox.text)
                    }
                }

                // Search results
                ListView {
                    id: searchResults
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: ListModel { id: searchResultsModel }
                    clip: true

                    delegate: Rectangle {
                        width: searchResults.width
                        height: 70
                        color: mouseArea.containsMouse ? "#0f3460" : "transparent"

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: contextMenu.popup()
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 5

                            Text {
                                text: model.title
                                color: "white"
                                font.bold: true
                                font.pixelSize: 14
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            RowLayout {
                                Text {
                                    text: model.artist
                                    color: "#8a8a8a"
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: formatDuration(model.duration)
                                    color: "#00b894"
                                    font.pixelSize: 12
                                }
                            }
                        }

                        Menu {
                            id: contextMenu
                            MenuItem {
                                text: "Queue for current singer"
                                onClicked: karaoke.queueSong(model.id, karaoke.currentSinger)
                            }
                            MenuItem {
                                text: "Queue for..."
                                onClicked: singerDialog.open()
                            }
                            MenuItem {
                                text: "Song details"
                                onClicked: songDetailsDialog.open()
                            }
                        }
                    }

                    function update(query) {
                        clear()
                        var results = karaoke.searchSongs(query, 50)
                        for (var i = 0; i < results.length; i++) {
                            append(results[i])
                        }
                    }
                }

                // Singer quick-add
                Button {
                    text: "+ Add Singer"
                    Layout.fillWidth: true
                    onClicked: addSingerDialog.open()
                }
            }
        }
    }

    // Dialogs
    AddSingerDialog { id: addSingerDialog }
    SelectSingerDialog { id: singerDialog }
    SongDetailsDialog { id: songDetailsDialog }

    // Helper functions
    function formatTime(seconds) {
        var mins = Math.floor(seconds / 60)
        var secs = Math.floor(seconds % 60)
        return mins.toString().padStart(2, '0') + ':' + secs.toString().padStart(2, '0')
    }

    function formatDuration(seconds) {
        if (seconds < 3600) {
            return Math.floor(seconds / 60) + ':' + Math.floor(seconds % 60).toString().padStart(2, '0')
        } else {
            var hours = Math.floor(seconds / 3600)
            var mins = Math.floor((seconds % 3600) / 60)
            return hours + ':' + mins.toString().padStart(2, '0')
        }
    }
}
