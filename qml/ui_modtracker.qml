// ui_modtracker.qml - Aegis Mod Tracker Interface
//
// This file implements a professional MOD tracker interface, inspired by classic
// trackers like ProTracker, FastTracker II, and MilkyTracker. It includes a pattern
// editor, instrument list, channel VU meters, and a waveform/spectrum display.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width: 1400
    height: 900
    title: tracker.modified ? "Aegis Tracker* - " + tracker.title : "Aegis Tracker - " + tracker.title
    color: "#1e1e1e"

    // Tracker backend
    ModTracker {
        id: tracker
        onError: (msg) => statusBar.showMessage("Error: " + msg, 5000)
        onPlaybackPositionChanged: (pattern, row) => {
            patternView.currentPattern = pattern
            patternView.currentRow = row
        }
    }

    // Keyboard shortcuts
    Shortcut {
        sequence: "Space"
        onActivated: tracker.playing ? tracker.stop() : tracker.playFromCurrentRow()
    }
    Shortcut {
        sequence: "F5"
        onActivated: tracker.play()
    }
    Shortcut {
        sequence: "F8"
        onActivated: tracker.stop()
    }
    Shortcut {
        sequence: "F9"
        onActivated: tracker.playFrom(patternView.currentPattern, patternView.currentRow)
    }
    Shortcut {
        sequence: "Ctrl+Z"
        onActivated: tracker.undo()
    }
    Shortcut {
        sequence: "Ctrl+Shift+Z"
        onActivated: tracker.redo()
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("File")
            MenuItem {
                text: qsTr("New Module")
                shortcut: "Ctrl+N"
                onTriggered: newModuleDialog.open()
            }
            MenuItem {
                text: qsTr("Open...")
                shortcut: "Ctrl+O"
                onTriggered: openDialog.open()
            }
            MenuItem {
                text: qsTr("Save")
                shortcut: "Ctrl+S"
                onTriggered: saveDialog.open()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Export...")
                onTriggered: exportDialog.open()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Exit")
                onTriggered: Qt.quit()
            }
        }
        Menu {
            title: qsTr("Edit")
            MenuItem {
                text: qsTr("Cut")
                shortcut: "Ctrl+X"
                onTriggered: patternView.cutSelection()
            }
            MenuItem {
                text: qsTr("Copy")
                shortcut: "Ctrl+C"
                onTriggered: patternView.copySelection()
            }
            MenuItem {
                text: qsTr("Paste")
                shortcut: "Ctrl+V"
                onTriggered: patternView.paste()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Insert Row")
                shortcut: "Insert"
                onTriggered: tracker.insertRow(patternView.currentPattern, patternView.currentRow)
            }
            MenuItem {
                text: qsTr("Delete Row")
                shortcut: "Delete"
                onTriggered: tracker.deleteRow(patternView.currentPattern, patternView.currentRow)
            }
        }
        Menu {
            title: qsTr("Playback")
            MenuItem {
                text: qsTr("Play")
                shortcut: "F5"
                onTriggered: tracker.play()
            }
            MenuItem {
                text: qsTr("Play from Cursor")
                shortcut: "F9"
                onTriggered: tracker.playFrom(patternView.currentPattern, patternView.currentRow)
            }
            MenuItem {
                text: qsTr("Stop")
                shortcut: "F8"
                onTriggered: tracker.stop()
            }
        }
    }

    // Main layout
    RowLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        // Left panel: Pattern Sequence & Instruments
        ColumnLayout {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            spacing: 4

            // Pattern Sequence (Order List)
            GroupBox {
                title: qsTr("Pattern Sequence")
                Layout.fillHeight: true
                Layout.fillWidth: true

                ListView {
                    id: sequenceList
                    anchors.fill: parent
                    clip: true
                    model: 128 // Max MOD sequence length
                    highlight: Rectangle { color: "#2d5a8a"; radius: 2 }
                    highlightMoveDuration: 0

                    delegate: Rectangle {
                        width: sequenceList.width
                        height: 20
                        color: index % 2 === 0 ? "#2a2a2a" : "#323232"

                        Row {
                            spacing: 8
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 4

                            Text {
                                text: index.toString(16).toUpperCase().padStart(2, '0')
                                color: "#888"
                                font.family: "Monospace"
                                width: 24
                            }
                            TextField {
                                text: tracker.getPatternInSequence(index).toString(16).toUpperCase().padStart(2, '0')
                                color: "#fff"
                                font.family: "Monospace"
                                background: Rectangle { color: "transparent" }
                                width: 30
                                maximumLength: 2
                                onEditingFinished: {
                                    let val = parseInt(text, 16)
                                    if (!isNaN(val)) tracker.setPatternInSequence(index, val)
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: sequenceList.currentIndex = index
                        }
                    }
                }
            }

            // Instruments
            GroupBox {
                title: qsTr("Instruments")
                Layout.fillHeight: true
                Layout.fillWidth: true

                ListView {
                    id: instrumentList
                    anchors.fill: parent
                    clip: true
                    model: 31 // Classic MOD has 31 instruments

                    delegate: Rectangle {
                        width: instrumentList.width
                        height: 24
                        color: index === instrumentList.currentIndex ? "#2d5a8a" : (index % 2 === 0 ? "#2a2a2a" : "#323232")
                        border.color: index === instrumentList.currentIndex ? "#4a9eff" : "transparent"

                        Row {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 8

                            Text {
                                text: (index + 1).toString().padStart(2, '0')
                                color: "#888"
                                font.family: "Monospace"
                                width: 24
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: tracker.getInstrumentName(index) || "Empty"
                                color: "#fff"
                                font.family: "Monospace"
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                width: parent.width - 40
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: instrumentList.currentIndex = index
                            onDoubleClicked: sampleEditor.open(index)
                        }
                    }
                }
            }
        }

        // Center: Pattern Editor
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#252526"
            border.color: "#3c3c3c"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Pattern Header (Channel headers)
                Row {
                    id: channelHeader
                    Layout.fillWidth: true
                    height: 24
                    spacing: 0

                    // Row number column
                    Rectangle {
                        width: 40
                        height: parent.height
                        color: "#333"
                        Text {
                            text: "Row"
                            color: "#aaa"
                            anchors.centerIn: parent
                            font.bold: true
                        }
                    }

                    Repeater {
                        model: tracker.numChannels
                        Rectangle {
                            width: (channelHeader.width - 40) / tracker.numChannels
                            height: parent.height
                            color: index % 2 === 0 ? "#2d2d30" : "#252526"
                            border.color: "#3c3c3c"

                            Text {
                                text: "Ch " + (index + 1)
                                color: tracker.isChannelMuted(index) ? "#666" : "#4a9eff"
                                anchors.centerIn: parent
                                font.bold: true
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.LeftButton) tracker.toggleMuteChannel(index)
                                        else tracker.soloChannel(index)
                                }
                            }
                        }
                    }
                }

                // Pattern Grid
                PatternGrid {
                    id: patternView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    tracker: tracker
                    currentPattern: tracker.currentPattern

                    onNoteEntered: (row, channel, note) => tracker.setNote(patternView.currentPattern, row, channel, note)
                    onRowSelected: (row) => tracker.setCurrentRow(row)
                }

                // Hex/Dec toggle and zoom
                Row {
                    Layout.fillWidth: true
                    height: 24
                    spacing: 8
                    anchors.margins: 4

                    Button {
                        text: patternView.hexMode ? "HEX" : "DEC"
                        onClicked: patternView.hexMode = !patternView.hexMode
                        checkable: true
                        checked: patternView.hexMode
                    }

                    Slider {
                        from: 8
                        to: 32
                        value: patternView.rowHeight
                        onValueChanged: patternView.rowHeight = value
                        width: 100
                    }
                }
            }
        }

        // Right panel: Info & Visuals
        ColumnLayout {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            spacing: 4

            // Oscilloscope
            GroupBox {
                title: qsTr("Oscilloscope")
                Layout.fillWidth: true
                Layout.preferredHeight: 150

                WaveformScope {
                    anchors.fill: parent
                    data: tracker.waveformData
                }
            }

            // Spectrum
            GroupBox {
                title: qsTr("Spectrum")
                Layout.fillWidth: true
                Layout.preferredHeight: 150

                SpectrumAnalyzer {
                    anchors.fill: parent
                    data: tracker.spectrumData
                }
            }

            // Channel VU Meters
            GroupBox {
                title: qsTr("Channels")
                Layout.fillWidth: true
                Layout.fillHeight: true

                Column {
                    anchors.fill: parent
                    spacing: 2

                    Repeater {
                        model: tracker.numChannels
                        ChannelVU {
                            width: parent.width
                            height: (parent.height - (tracker.numChannels - 1) * 2) / tracker.numChannels
                            channel: index
                            level: tracker.getChannelLevel(index)
                            active: !tracker.isChannelMuted(index)
                        }
                    }
                }
            }

            // Pattern Info
            GroupBox {
                title: qsTr("Info")
                Layout.fillWidth: true
                Layout.preferredHeight: 120

                GridLayout {
                    anchors.fill: parent
                    columns: 2

                    Label { text: "Title:"; color: "#aaa" }
                    Label { text: tracker.title; color: "#fff"; elide: Text.ElideRight }

                    Label { text: "Channels:"; color: "#aaa" }
                    Label { text: tracker.numChannels; color: "#fff" }

                    Label { text: "Patterns:"; color: "#aaa" }
                    Label { text: tracker.numPatterns; color: "#fff" }

                    Label { text: "BPM:"; color: "#aaa" }
                    SpinBox {
                        value: tracker.bpm
                        from: 32
                        to: 255
                        onValueModified: tracker.setBpm(value)
                    }

                    Label { text: "Speed:"; color: "#aaa" }
                    SpinBox {
                        value: tracker.speed
                        from: 1
                        to: 31
                        onValueModified: tracker.setSpeed(value)
                    }
                }
            }
        }
    }

    // Transport controls at bottom
    ToolBar {
        anchors.bottom: parent.bottom
        width: parent.width

        RowLayout {
            anchors.fill: parent
            spacing: 8

            ToolButton {
                text: "⏮"
                font.pixelSize: 20
                onClicked: tracker.previousPattern()
            }
            ToolButton {
                text: tracker.playing ? "⏹" : "▶"
                font.pixelSize: 20
                onClicked: tracker.playing ? tracker.stop() : tracker.play()
            }
            ToolButton {
                text: "⏭"
                font.pixelSize: 20
                onClicked: tracker.nextPattern()
            }

            ToolSeparator {}

            Label {
                text: "Pat: " + patternView.currentPattern.toString(16).toUpperCase().padStart(2, '0')
                color: "#fff"
                font.family: "Monospace"
            }
            Label {
                text: "Row: " + patternView.currentRow.toString(16).toUpperCase().padStart(2, '0')
                color: "#fff"
                font.family: "Monospace"
            }

            Item { Layout.fillWidth: true }

            Slider {
                from: 0
                to: 1
                value: tracker.volume
                onValueChanged: tracker.volume = value
                Layout.preferredWidth: 150
            }
        }
    }

    // Status bar
    Rectangle {
        id: statusBar
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40 // Above transport
        width: parent.width
        height: 24
        color: "#007acc"
        visible: statusText.text !== ""

        // Use a timer to automatically hide the status message after a duration.
        property var hideTimer: null

        function showMessage(msg, duration) {
            statusText.text = msg
            if (hideTimer) clearTimeout(hideTimer)
                // In QML, we use a Timer element instead of setTimeout.
                if (!hideTimer) {
                    hideTimer = Qt.createQmlObject('import QtQuick 2.0; Timer {}', statusBar);
                }
                hideTimer.interval = duration;
            hideTimer.repeat = false;
            hideTimer.onTriggered = function() { statusText.text = ""; };
            hideTimer.start();
        }

        Text {
            id: statusText
            anchors.centerIn: parent
            color: "#fff"
        }
    }

    // Dialogs
    FileDialog {
        id: openDialog
        title: "Open Module"
        nameFilters: ["Module files (*.mod *.xm *.it *.s3m)", "All files (*)"]
        onAccepted: tracker.load(selectedFile)
    }

    FileDialog {
        id: saveDialog
        title: "Save Module"
        fileMode: FileDialog.SaveFile
        nameFilters: ["ProTracker MOD (*.mod)", "FastTracker XM (*.xm)", "Impulse Tracker IT (*.it)"]
        onAccepted: tracker.save(selectedFile)
    }

    Dialog {
        id: newModuleDialog
        title: "New Module"
        modal: true

        ColumnLayout {
            anchors.fill: parent

            Label { text: "Channels:" }
            ComboBox {
                id: channelCombo
                model: [4, 8, 16, 32]
                currentIndex: 0
            }

            Label { text: "Pattern Length:" }
            SpinBox {
                id: patternLengthSpin
                value: 64
                from: 1
                to: 256
            }
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: tracker.newModule(channelCombo.currentValue, patternLengthSpin.value)
    }

    // Pattern Grid Component
    // This component encapsulates the complex pattern editing logic, keeping the main file clean.
    component PatternGrid : Rectangle {
        property var tracker
        property int currentPattern: 0
        property int currentRow: 0
        property int currentChannel: 0
        property int rowHeight: 16
        property bool hexMode: true

        signal noteEntered(int row, int channel, var note)
        signal rowSelected(int row)

        color: "#1e1e1e"
        clip: true

        // Virtual ListView for performance with large patterns
        ListView {
            id: listView
            anchors.fill: parent
            model: tracker.getPatternRows(currentPattern)
            highlightRangeMode: ListView.ApplyRange
            preferredHighlightBegin: height / 2 - rowHeight / 2
            preferredHighlightEnd: height / 2 + rowHeight / 2

            delegate: Rectangle {
                width: parent.width
                height: rowHeight
                color: {
                    if (index === patternView.currentRow) return "#094771" // Cursor
                        if (index === tracker.playRow && patternView.currentPattern === tracker.playPattern) return "#264f78" // Playhead
                            if (index % 4 === 0) return "#2a2d2e" // Beat line
                                return "#1e1e1e"
                }

                Row {
                    spacing: 0

                    // Row number
                    Rectangle {
                        width: 40
                        height: parent.height
                        color: index === patternView.currentRow ? "#0078d4" : "transparent"

                        Text {
                            text: hexMode ? index.toString(16).toUpperCase().padStart(2, '0') : index.toString().padStart(3, '0')
                            color: index % 16 === 0 ? "#ff6b6b" : (index % 4 === 0 ? "#4ec9b0" : "#808080")
                            font.family: "Monospace"
                            font.pixelSize: 12
                            anchors.centerIn: parent
                        }
                    }

                    // Note columns
                    Repeater {
                        model: tracker.numChannels
                        Rectangle {
                            width: (parent.parent.width - 40) / tracker.numChannels
                            height: parent.height
                            color: index === patternView.currentChannel && patternView.currentRow === modelData ? "#0078d4" : "transparent"
                            border.color: "#3c3c3c"
                            border.width: 1

                            // Note data display
                            Row {
                                anchors.centerIn: parent
                                spacing: 2

                                Text {
                                    text: tracker.getNoteDisplay(currentPattern, index, modelData, "note")
                                    color: "#dcdcaa"
                                    font.family: "Monospace"
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                                Text {
                                    text: tracker.getNoteDisplay(currentPattern, index, modelData, "inst")
                                    color: "#9cdcfe"
                                    font.family: "Monospace"
                                    font.pixelSize: 11
                                }
                                Text {
                                    text: tracker.getNoteDisplay(currentPattern, index, modelData, "effect")
                                    color: "#ce9178"
                                    font.family: "Monospace"
                                    font.pixelSize: 11
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    patternView.currentRow = index
                                    patternView.currentChannel = modelData
                                    patternView.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }
        }

        // Keyboard input handling
        focus: true
        Keys.onPressed: (event) => {
            if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
                // Number input for instruments/effects
                handleNumberInput(event.text)
            } else if (event.key >= Qt.Key_A && event.key <= Qt.Key_Z) {
                // Note input (QWERTY-to-note mapping)
                handleNoteInput(event.key, event.modifiers & Qt.Key_Shift)
            } else {
                switch(event.key) {
                    case Qt.Key_Left:
                        currentChannel = Math.max(0, currentChannel - 1)
                        break
                    case Qt.Key_Right:
                        currentChannel = Math.min(tracker.numChannels - 1, currentChannel + 1)
                        break
                    case Qt.Key_Up:
                        currentRow = Math.max(0, currentRow - 1)
                        rowSelected(currentRow)
                        break
                    case Qt.Key_Down:
                        currentRow = Math.min(63, currentRow + 1)
                        rowSelected(currentRow)
                        break
                    case Qt.Key_Delete:
                        tracker.clearNote(currentPattern, currentRow, currentChannel)
                        break
                    case Qt.Key_Insert:
                        tracker.insertRow(currentPattern, currentRow)
                        break
                    case Qt.Key_Backspace:
                        tracker.deleteRow(currentPattern, currentRow)
                        break
                }
            }
        }

        function handleNoteInput(key, isShift) {
            // QWERTY keyboard to note mapping (Tracker style)
            // 2="C#", 3="D#", 5="F#", 6="G#", 7="A#"
            // Q="C", W="D", E="E", R="F", T="G", Y="A", U="B"
            // S="C+", D="D+", etc (one octave higher)

            let note = -1
            let octave = isShift ? 4 : 3

            switch(key) {
                case Qt.Key_Z: note = 0; break // C
                case Qt.Key_S: note = 1; break // C#
                case Qt.Key_X: note = 2; break // D
                case Qt.Key_D: note = 3; break // D#
                case Qt.Key_C: note = 4; break // E
                case Qt.Key_V: note = 5; break // F
                case Qt.Key_G: note = 6; break // F#
                case Qt.Key_B: note = 7; break // G
                case Qt.Key_H: note = 8; break // G#
                case Qt.Key_N: note = 9; break // A
                case Qt.Key_J: note = 10; break // A#
                case Qt.Key_M: note = 11; break // B
                case Qt.Key_Comma: note = 12; break // C (next octave)
            }

            if (note >= 0) {
                let period = amigaPeriod(note + octave * 12)
                tracker.setNote(currentPattern, currentRow, currentChannel,
                                {period: period, instrument: currentInstrument, effect: 0, effectValue: 0})
                currentRow = Math.min(63, currentRow + 1)
            }
        }

        function handleNumberInput(text) {
            // Handle hex input for effects/instruments
        }

        function amigaPeriod(note) {
            // Amiga period table calculation
            const periods = [3424, 3232, 3048, 2880, 2712, 2560, 2416, 2280, 2152, 2032, 1920, 1812]
            return periods[note % 12] >> Math.floor(note / 12)
        }

        function cutSelection() {
            clipboard = tracker.cutPatternData(currentPattern, currentRow, currentChannel)
        }

        function copySelection() {
            clipboard = tracker.copyPatternData(currentPattern, currentRow, currentChannel)
        }

        function paste() {
            tracker.pastePatternData(currentPattern, currentRow, currentChannel, clipboard)
        }
    }

    // VU Meter Component
    component ChannelVU : Rectangle {
        property int channel
        property real level
        property bool active

        height: 20
        color: "#252526"
        border.color: "#3c3c3c"

        Row {
            anchors.fill: parent
            anchors.margins: 2
            spacing: 2

            Text {
                text: "Ch" + (channel + 1)
                color: active ? "#fff" : "#666"
                font.pixelSize: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 30
            }

            Rectangle {
                width: parent.width - 35
                height: parent.height - 4
                color: "#1e1e1e"
                border.color: "#444"

                Rectangle {
                    width: parent.width * Math.min(1.0, level)
                    height: parent.height
                    color: level > 0.9 ? "#ff4444" : (level > 0.7 ? "#ffaa00" : "#00ff00")
                    opacity: active ? 1.0 : 0.3
                }
            }
        }
    }

    // Waveform Visualizer
    component WaveformScope : Canvas {
        property var data

        onPaint: {
            var ctx = getContext("2d")
            ctx.fillStyle = "#1e1e1e"
            ctx.fillRect(0, 0, width, height)

            if (!data || data.length === 0) return

                ctx.strokeStyle = "#4ec9b0"
                ctx.lineWidth = 2
                ctx.beginPath()

                var step = data.length / width
                var yScale = height / 2

                for (var x = 0; x < width; x++) {
                    var idx = Math.floor(x * step)
                    var y = height/2 - data[idx] * yScale
                    if (x === 0) ctx.moveTo(x, y)
                        else ctx.lineTo(x, y)
                }
                ctx.stroke()
        }
    }

    // Spectrum Analyzer
    component SpectrumAnalyzer : Canvas {
        property var data

        onPaint: {
            var ctx = getContext("2d")
            ctx.fillStyle = "#1e1e1e"
            ctx.fillRect(0, 0, width, height)

            if (!data || data.length === 0) return

                var barWidth = width / data.length
                for (var i = 0; i < data.length; i++) {
                    var h = data[i] * height
                    var hue = 120 + (i / data.length) * 240 // Green to Blue
                    ctx.fillStyle = "hsl(" + hue + ", 70%, 50%)"
                    ctx.fillRect(i * barWidth, height - h, barWidth - 1, h)
                }
        }
    }
}
