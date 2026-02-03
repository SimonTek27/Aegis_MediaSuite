// ui_ui_musicnotation_editor.qml - Music notation editor

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

// Symbol font loader for musical symbols
FontLoader {
    id: bravuraFont
    source: "qrc:/fonts/Bravura.otf"
}

// Accidental symbols component
component AccidentalSymbol: Text {
    property int accidental: 0  // 0=none, 1=sharp, 2=flat, 3=natural, 4=doubleSharp, 5=doubleFlat

    font.family: bravuraFont.name
    font.pixelSize: 24
    text: {
        switch(accidental) {
            case 1: return "\uE262"  // Sharp
            case 2: return "\uE260"  // Flat
            case 3: return "\uE261"  // Natural
            case 4: return "\uE263"  // Double sharp
            case 5: return "\uE264"  // Double flat
            default: return ""
        }
    }
    color: "#333333"
}

// Note head component with proper SMuFL symbols
component NoteHead: Text {
    property int duration: 5  // NoteDuration enum value
    property bool filled: duration >= 5  // Quarter and shorter are filled

    font.family: bravuraFont.name
    font.pixelSize: parent ? parent.height * 0.8 : 20
    text: filled ? "\uE0A4" : "\uE0A3"  // Black notehead : Half notehead
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
}

// Clef component
component ClefSymbol: Text {
    property int clefType: 0  // 0=Treble, 1=Bass, 2=Alto, 3=Tenor

    font.family: bravuraFont.name
    font.pixelSize: 48
    text: {
        switch(clefType) {
            case 0: return "\uE050"  // G clef
            case 1: return "\uE062"  // F clef
            case 2: return "\uE05F"  // C clef (Alto)
            case 3: return "\uE05C"  // C clef (Tenor)
            default: return "\uE050"
        }
    }
    color: "#333333"
}

// Rest symbols
component RestSymbol: Text {
    property int duration: 5

    font.family: bravuraFont.name
    font.pixelSize: 24
    text: {
        switch(duration) {
            case 3: return "\uE4E3"  // Whole rest
            case 4: return "\uE4E4"  // Half rest
            case 5: return "\uE4E5"  // Quarter rest
            case 6: return "\uE4E6"  // Eighth rest
            case 7: return "\uE4E7"  // 16th rest
            default: return "\uE4E5"
        }
    }
    color: "#333333"
}

// Articulation marks
component ArticulationMark: Text {
    property string type: "staccato"  // staccato, accent, tenuto, marcato

    font.family: bravuraFont.name
    font.pixelSize: 16
    text: {
        switch(type) {
            case "staccato": return "\uE4A2"
            case "accent": return "\uE4A0"
            case "tenuto": return "\uE4C0"
            case "marcato": return "\uE4AC"
            case "staccatissimo": return "\uE4A6"
            case "fermata": return "\uE4C4"
            default: return ""
        }
    }
    color: "#333333"
}

// Dynamic marking
component DynamicText: Text {
    property string dynamic: "mf"  // ppp to fff, sf, fp, etc.

    font.family: bravuraFont.name
    font.pixelSize: 20
    text: {
        switch(dynamic) {
            case "pppp": return "\uE529"
            case "ppp": return "\uE52A"
            case "pp": return "\uE52B"
            case "p": return "\uE520"
            case "mp": return "\uE521"
            case "mf": return "\uE522"
            case "f": return "\uE523"
            case "ff": return "\uE524"
            case "fff": return "\uE525"
            case "ffff": return "\uE526"
            case "sfz": return "\uE53A"
            default: return dynamic
        }
    }
    color: "#333333"
}

// Time signature
component TimeSignatureView: Row {
    property int numerator: 4
    property int denominator: 4

    font.family: bravuraFont.name
    font.pixelSize: 32

    Text { text: numerator; font: parent.font }
    Text { text: denominator; font: parent.font }
}

// Slur/Tie curve
component SlurveCurve: Shape {
    property real startX: 0
    property real startY: 0
    property real endX: 100
    property real endY: 0
    property real height: 20
    property bool above: true

    ShapePath {
        strokeColor: "#333333"
        strokeWidth: 1.5
        fillColor: "transparent"

        PathQuad {
            x: startX
            y: startY
            controlX: (startX + endX) / 2
            controlY: above ? Math.min(startY, endY) - height : Math.max(startY, endY) + height
        }

        PathQuad {
            x: endX
            y: endY
            controlX: (startX + endX) / 2
            controlY: above ? Math.min(startY, endY) - height : Math.max(startY, endY) + height
        }
    }
}

// Beam group for eighth notes and shorter
component BeamGroup: Item {
    property var notes: []  // Array of note objects with x, y positions
    property int beamCount: 1  // 1 for eighth, 2 for 16th, etc.

    Repeater {
        model: beamCount

        Rectangle {
            // Calculate beam angle and position based on note stems
            x: notes.length > 0 ? notes[0].x : 0
            y: calculateBeamY(index)
            width: notes.length > 1 ? notes[notes.length - 1].x - notes[0].x : 50
            height: 3
            color: "#333333"
            rotation: calculateBeamAngle()
        }
    }

    function calculateBeamY(beamIndex) {
        // Simplified - would calculate based on stem directions and slope
        return 20 + beamIndex * 6
    }

    function calculateBeamAngle() {
        if (notes.length < 2) return 0
            var dy = notes[notes.length - 1].y - notes[0].y
            var dx = notes[notes.length - 1].x - notes[0].x
            return Math.atan2(dy, dx) * 180 / Math.PI
    }
}

// Selection rectangle
component SelectionBox: Rectangle {
    property var selectionBounds: Qt.rect(0, 0, 100, 50)

    x: selectionBounds.x
    y: selectionBounds.y
    width: selectionBounds.width
    height: selectionBounds.height

    color: "#2196F320"  // Transparent blue
    border.color: "#2196F3"
    border.width: 2
    radius: 4

    // Resize handles
    Repeater {
        model: 8  // 4 corners + 4 edges

        Rectangle {
            width: 8
            height: 8
            color: "#2196F3"
            visible: parent.visible

            property int position: index

            x: {
                switch(position % 3) {
                    case 0: return -4
                    case 1: return parent.width / 2 - 4
                    case 2: return parent.width - 4
                }
            }

            y: {
                switch(Math.floor(position / 3)) {
                    case 0: return -4
                    case 1: return parent.height / 2 - 4
                    case 2: return parent.height - 4
                }
            }
        }
    }
}

// Playback cursor with time display
component Playhead: Rectangle {
    property real xPosition: 0
    property bool playing: false

    x: xPosition
    width: 2
    height: parent ? parent.height : 0
    color: playing ? "#ff4444" : "#666666"

    // Triangle handle at top
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: 12
        height: 12
        color: parent.color
        rotation: 45
        transformOrigin: Item.Center
    }

    // Time tooltip
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 16
        width: timeLabel.width + 8
        height: timeLabel.height + 4
        color: parent.color
        radius: 4

        Text {
            id: timeLabel
            anchors.centerIn: parent
            text: formatTime(xPosition / 100)  // Rough conversion
            color: "white"
            font.pixelSize: 10

            function formatTime(seconds) {
                var mins = Math.floor(seconds / 60)
                var secs = Math.floor(seconds % 60)
                var ms = Math.floor((seconds % 1) * 100)
                return mins + ":" + (secs < 10 ? "0" : "") + secs + "." + (ms < 10 ? "0" : "") + ms
            }
        }
    }
}

// Measure number indicator
component MeasureNumber: Text {
    property int number: 1

    text: number
    font.pixelSize: 12
    color: "#666666"
    horizontalAlignment: Text.AlignHCenter
}

// Ledger lines for notes above/below staff
component LedgerLines: Item {
    property int lineCount: 1
    property bool above: true
    property real spacing: 10

    Repeater {
        model: lineCount

        Rectangle {
            y: above ? -(index + 1) * spacing : (5 + index) * spacing
            width: 20
            height: 1
            color: "#333333"
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
}

// Stem component
component Stem: Rectangle {
    property bool up: true
    property real length: 35

    width: 1
    height: length
    color: "#333333"
    anchors.horizontalCenter: parent.horizontalCenter
    y: up ? -length : 0
}

// Flag for eighth notes and shorter
component Flag: Text {
    property bool up: true
    property int duration: 6  // 6=eighth, 7=16th, 8=32nd

    font.family: bravuraFont.name
    font.pixelSize: 24
    text: up ? (duration === 6 ? "\uE240" : duration === 7 ? "\uE242" : "\uE244") :
    (duration === 6 ? "\uE241" : duration === 7 ? "\uE243" : "\uE245")
    color: "#333333"
}

// Dot augmentation
component AugmentationDot: Rectangle {
    width: 4
    height: 4
    radius: 2
    color: "#333333"
}

// Tie curve
component TieCurve: Shape {
    property real startX: 0
    property real startY: 0
    property real endX: 50
    property real endY: 0

    ShapePath {
        strokeColor: "#333333"
        strokeWidth: 1.2
        fillColor: "transparent"

        PathCurve {
            x: startX
            y: startY
        }

        PathCurve {
            x: (startX + endX) / 2
            y: startY + 8
        }

        PathCurve {
            x: endX
            y: endY
        }
    }
}

// Playback position indicator with measure/beat
component PositionIndicator: Rectangle {
    property int measure: 1
    property int beat: 1
    property int tick: 0
    property double time: 0.0

    height: 30
    color: palette.base
    border.color: palette.mid

    RowLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 12

        Label {
            text: "Meas: " + measure
            font.family: "Monospace"
        }

        Label {
            text: "Beat: " + beat
            font.family: "Monospace"
        }

        Label {
            text: "Tick: " + tick
            font.family: "Monospace"
        }

        Label {
            text: formatTime(time)
            font.family: "Monospace"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
        }
    }

    function formatTime(seconds) {
        var mins = Math.floor(seconds / 60)
        var secs = Math.floor(seconds % 60)
        var ms = Math.floor((seconds % 1) * 1000)
        return mins + ":" + (secs < 10 ? "0" : "") + secs + "." + (ms < 100 ? "0" : "") + (ms < 10 ? "0" : "") + ms
    }
}
