// Aegis DJ Mixer — Pioneer DJM-900NXS2 inspired
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    visible: true
    width: 1080
    height: 980
    minimumWidth: 900
    minimumHeight: 850
    title: "Aegis DJ Mixer"
    color: "#0f0f0f"

    Component.onCompleted: if (DJ) DJ.startSession()

    // ── Palette ──────────────────────────────────────────────────────────
    readonly property color cBody:    "#181818"
    readonly property color cPanel:   "#202020"
    readonly property color cKnob:    "#2c2c2c"
    readonly property color cRim:     "#3e3e3e"
    readonly property color cSep:     "#2e2e2e"
    readonly property color cLabel:   "#777777"
    readonly property color cText:    "#cccccc"
    readonly property color cBright:  "#eeeeee"
    readonly property color cYellow:  "#f0c200"
    readonly property color cCyan:    "#00ccff"
    readonly property color cOrange:  "#ff6600"
    readonly property color cGreen:   "#44ff88"
    readonly property color cRed:     "#ff2222"
    readonly property color cBlue:    "#1a7cff"

    // ── State ─────────────────────────────────────────────────────────────
    property real xfPos:      0.5
    property int  beatFxIdx:  0
    property int  fxFreqSel:  1
    property real masterVol:  0.8
    property real masterBpm:  DJ ? DJ.mixer.masterBpm : 120.0

    property var beatFxList: ["PINGPONG","ECHO","SPIRAL","REVERB","TRANS",
                               "PHASER","FLANGER","PITCH","SLIP ROLL","ROLL",
                               "VINYL BRAKE","HELIX","DELAY","CRUSH"]

    // ── VU timer ─────────────────────────────────────────────────────────
    property var vuLevels: [0.0, 0.0, 0.0, 0.0]
    Timer {
        interval: 60; running: true; repeat: true
        onTriggered: {
            var a = []; for (var i=0;i<4;i++) a.push(0.1 + Math.random()*0.75); vuLevels = a
        }
    }

    // ════════════════════════════════════════════════════════════════════
    // KNOB COMPONENT
    // ════════════════════════════════════════════════════════════════════
    component Knob: Item {
        id: k
        property real  value:    0.5
        property real  minV:     0.0
        property real  maxV:     1.0
        property color dot:      cCyan
        property color rim:      cRim
        property bool  lg:       false
        property string lbl:     ""
        property bool  centerDot: false  // show center mark at 0.5

        implicitWidth:  lg ? 50 : 36
        implicitHeight: lg ? 50 : 36

        property real norm: Math.max(0, Math.min(1, (value - minV) / (maxV - minV)))
        property real ang: -140 + norm * 280

        Rectangle {
            id: body
            anchors.centerIn: parent
            width: parent.width - 2; height: width; radius: width/2
            color: cKnob
            border.color: k.rim; border.width: 2

            // Raised center dome
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.55; height: width; radius: width/2
                color: "#363636"
                border.color: "#484848"; border.width: 1
            }

            // Arc
            Canvas {
                anchors.fill: parent
                property real _ang: k.ang
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0,0,width,height)
                    var cx=width/2, cy=height/2, r=width/2-4
                    var s = (-140-90)*Math.PI/180
                    var e = (k.ang - 90)*Math.PI/180
                    ctx.beginPath(); ctx.arc(cx,cy,r,s,e)
                    ctx.strokeStyle = k.dot; ctx.lineWidth = 2.5; ctx.stroke()
                }
                onAngChanged: requestPaint()
                Component.onCompleted: requestPaint()
            }

            // Pointer
            Rectangle {
                width: 3; height: 3; radius: 1.5
                color: k.dot
                x: parent.width/2 - 1.5 + (parent.width/2-7)*Math.sin(k.ang*Math.PI/180)
                y: parent.height/2 - 1.5 - (parent.height/2-7)*Math.cos(k.ang*Math.PI/180)
            }
        }

        // Label below
        Text {
            visible: lbl !== ""
            anchors.top: body.bottom; anchors.topMargin: 2
            anchors.horizontalCenter: parent.horizontalCenter
            text: lbl; color: cLabel
            font.pixelSize: 7; font.family: "monospace"
        }

        // Center mark
        Rectangle {
            visible: centerDot
            anchors.top: body.bottom; anchors.topMargin: 2
            anchors.horizontalCenter: parent.horizontalCenter
            width: 3; height: 3; radius: 1.5; color: "#555"
        }

        MouseArea {
            anchors.fill: parent
            property real _ly: 0
            onPressed: _ly = mouseY
            onPositionChanged: {
                if (!pressed) return
                var d = (_ly - mouseY) / 130.0
                _ly = mouseY
                k.value = Math.max(k.minV, Math.min(k.maxV, k.value + d*(k.maxV-k.minV)))
            }
        }
    }

    // ════════════════════════════════════════════════════════════════════
    // VU METER
    // ════════════════════════════════════════════════════════════════════
    component Vu: Rectangle {
        property real level: 0.0
        property bool horiz: false
        width: horiz ? 80 : 10
        height: horiz ? 10 : 100
        color: "#111"; radius: 2

        Repeater {
            model: 20
            Rectangle {
                property real seg: (20-index)/20.0
                property bool on2: level >= seg
                width:  horiz ? (parent.width-4)/20 : parent.width-2
                height: horiz ? parent.height-2 : (parent.height-4)/20
                x: horiz ? 2 + index*(parent.width-4)/20 : 1
                y: horiz ? 1 : 2 + index*(parent.height-4)/20
                radius: 1
                color: !on2 ? "#1a1a1a" : (seg>0.87 ? cRed : seg>0.67 ? cYellow : cGreen)
            }
        }
    }

    // ════════════════════════════════════════════════════════════════════
    // CHANNEL FADER
    // ════════════════════════════════════════════════════════════════════
    component CFader: Item {
        property real value: 0.75
        property color ac: cCyan
        implicitWidth: 38; implicitHeight: 120

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 0; width: 4; height: parent.height; radius: 2; color: "#151515"
            Rectangle { anchors.fill: parent; anchors.margins: 1; color: "#222"; radius: 2 }
        }

        // Scale
        Repeater {
            model: 11
            Row {
                x: 24; y: index*(parent.height/10)-1
                Rectangle { width: index%5===0?8:4; height: 1; color: index%5===0?"#555":"#333"; anchors.verticalCenter: parent.verticalCenter }
                Text { visible: index%5===0; text:["10","","","","","5","","","","","0"][index]; color:"#444"; font.pixelSize:6; anchors.verticalCenter: parent.verticalCenter; leftPadding:2 }
            }
        }

        Rectangle {
            id: fcap
            width: 30; height: 20; radius: 3
            y: parent.height*(1-value) - 10
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#4e4e4e"; border.color: "#6a6a6a"

            Repeater {
                model: 4
                Rectangle { x: 5+index*6; y: 5; width: 2; height: 10; radius: 1; color: "#2a2a2a" }
            }

            MouseArea {
                anchors.fill: parent
                drag.target: parent; drag.axis: Drag.YAxis
                drag.minimumY: -10; drag.maximumY: fcap.parent.height-10
                onPositionChanged: if(drag.active) fcap.parent.value = 1.0-(fcap.y+10)/fcap.parent.height
            }
        }
    }

    // ════════════════════════════════════════════════════════════════════
    // CROSSFADER
    // ════════════════════════════════════════════════════════════════════
    component XFader: Item {
        implicitWidth: 400; implicitHeight: 36

        Rectangle {
            anchors.fill: parent; color: "#111"; radius: 4; border.color: cSep

            // Track
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: 12; width: parent.width-24; height: 6; radius: 3; color: "#1a1a1a"
                Rectangle { anchors.fill: parent; anchors.margins: 1; color: "#222"; radius: 3 }
            }

            // Cap
            Rectangle {
                id: xcap
                width: 52; height: 26; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                x: 12 + xfPos*(parent.width-76)
                color: "#505050"; border.color: "#707070"

                Repeater {
                    model: 6
                    Rectangle { x: 6+index*7; y: 7; width: 2; height: 12; radius: 1; color: "#2e2e2e" }
                }

                MouseArea {
                    anchors.fill: parent
                    drag.target: parent; drag.axis: Drag.XAxis
                    drag.minimumX: 12
                    drag.maximumX: xcap.parent.width - 64
                    onPositionChanged: {
                        if (drag.active) {
                            xfPos = (xcap.x - 12) / (xcap.parent.width - 76)
                            if (DJ) DJ.mixer.crossfader = xfPos
                        }
                    }
                }
            }

            Text { anchors.left:parent.left; anchors.leftMargin:6; anchors.verticalCenter:parent.verticalCenter; text:"‹A"; color:"#555"; font.pixelSize:11; font.bold:true }
            Text { anchors.right:parent.right; anchors.rightMargin:6; anchors.verticalCenter:parent.verticalCenter; text:"B›"; color:"#555"; font.pixelSize:11; font.bold:true }
        }
    }

    // ════════════════════════════════════════════════════════════════════
    // MAIN LAYOUT
    // ════════════════════════════════════════════════════════════════════
    Rectangle {
        anchors.fill: parent
        color: cBody; radius: 6

        ColumnLayout {
            anchors.fill: parent; anchors.margins: 8; spacing: 3

            // ── TOP BAR ─────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true; height: 26; color: "transparent"

                // USB
                Row {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                    Text { text: "USB"; color: cLabel; font.pixelSize: 8; anchors.verticalCenter:parent.verticalCenter }
                    Repeater {
                        model: 2
                        Rectangle {
                            width: 32; height: 20; radius: 2; color: cKnob; border.color: cSep
                            Column {
                                anchors.centerIn: parent; spacing: 1
                                Rectangle { width: 20; height: 3; color: "#1a1a1a"; radius: 1; anchors.horizontalCenter: parent.horizontalCenter }
                                Text { text: ["A","B"][index]; color: cLabel; font.pixelSize: 8; anchors.horizontalCenter: parent.horizontalCenter }
                            }
                            // Green LED
                            Rectangle { x: 2; y: 2; width: 5; height: 5; radius: 2.5; color: cGreen; opacity: 0.9 }
                        }
                    }
                    // MIDI ON/OFF
                    Column {
                        anchors.verticalCenter: parent.verticalCenter; spacing: 1
                        Text { text: "MIDI"; color: cLabel; font.pixelSize: 6; anchors.horizontalCenter:parent.horizontalCenter }
                        Rectangle { width: 30; height: 12; radius: 2; color: "#1a2a1a"; border.color: cSep
                            Text { anchors.centerIn:parent; text: "ON/OFF"; color: "#446644"; font.pixelSize: 6 }
                        }
                    }
                }

                // Logo
                Text {
                    anchors.centerIn: parent
                    text: "Pioneer DJ"
                    font.family: "Georgia, serif"; font.pixelSize: 17; font.bold: true
                    color: cBright; font.letterSpacing: 1.5
                }

                // Power
                Row {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Text { text: "SEND / RETURN"; color: cLabel; font.pixelSize: 7; anchors.verticalCenter:parent.verticalCenter }
                    Rectangle { width: 8; height: 8; radius: 4; color: cGreen; anchors.verticalCenter:parent.verticalCenter }
                    Text { text: "POWER"; color: cLabel; font.pixelSize: 7; anchors.verticalCenter:parent.verticalCenter }
                }
            }

            // ── MAIN BODY ────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 3

                // ══ LEFT SIDEBAR ════════════════════════════════════════
                Rectangle {
                    Layout.preferredWidth: 128; Layout.fillHeight: true
                    color: cPanel; radius: 4; border.color: cSep

                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 8

                        // MIC
                        Column {
                            Layout.fillWidth: true; spacing: 4
                            Text { text: "MIC  LEVEL"; color: cLabel; font.pixelSize: 8; font.letterSpacing:1; anchors.horizontalCenter:parent.horizontalCenter }
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter; spacing: 10
                                Knob { value:0.5; lbl:"MIC 1"; dot:cYellow }
                                Knob { value:0.5; lbl:"MIC 2"; dot:cYellow }
                            }
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter; spacing: 8
                                Knob { value:0.55; lbl:"HI"; dot:cYellow }
                                Knob { value:0.5;  lbl:"LOW"; dot:cYellow }
                            }
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter; spacing: 3
                                Repeater {
                                    model: ["OFF","ON","TALK\nOVER"]
                                    Rectangle {
                                        width: index<2?24:36; height: 18; radius: 2
                                        color: index===1?"#1a4a1a":cKnob; border.color:cSep
                                        Text { anchors.centerIn:parent; text:modelData; color:index===1?cGreen:cLabel; font.pixelSize:6; horizontalAlignment:Text.AlignHCenter }
                                    }
                                }
                            }
                            Rectangle {
                                width: parent.width; height: 16; radius: 2; color: cKnob; border.color: cSep
                                Text { anchors.centerIn:parent; text:"BEAT FX"; color:cLabel; font.pixelSize:7 }
                            }
                        }

                        Rectangle { width: parent.width; height: 1; color: cSep }

                        // SOUND COLOR FX
                        Column {
                            Layout.fillWidth: true; spacing: 5
                            Text { text: "SOUND COLOR FX"; color: cLabel; font.pixelSize: 7; font.bold:true; font.letterSpacing:1; anchors.horizontalCenter:parent.horizontalCenter }
                            Grid {
                                anchors.horizontalCenter: parent.horizontalCenter
                                columns: 3; spacing: 3
                                Repeater {
                                    model: ["SPACE","DUB\nECHO","SWEEP","NOISE","CRUSH","FILTER"]
                                    Rectangle {
                                        width: 34; height: 18; radius: 2; color: cKnob; border.color: cSep
                                        Text { anchors.centerIn:parent; text:modelData; color:cLabel; font.pixelSize:6; horizontalAlignment:Text.AlignHCenter }
                                        MouseArea { anchors.fill:parent }
                                    }
                                }
                            }
                            Text { text: "PARAMETER"; color: cLabel; font.pixelSize: 7; anchors.horizontalCenter:parent.horizontalCenter }
                            Knob { anchors.horizontalCenter: parent.horizontalCenter; value:0.3; lg:true; dot:cOrange }
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter; spacing: 30
                                Text { text:"MIN"; color:cLabel; font.pixelSize:7 }
                                Text { text:"MAX"; color:cLabel; font.pixelSize:7 }
                            }
                        }

                        Rectangle { width: parent.width; height: 1; color: cSep }

                        // LINK / Headphones
                        Column {
                            Layout.fillWidth: true; spacing: 5
                            Text { text:"LINK"; color:cLabel; font.pixelSize:8; font.letterSpacing:2; anchors.horizontalCenter:parent.horizontalCenter }
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 56; height: 22; radius: 3; color: cYellow
                                Text { anchors.centerIn:parent; text:"CUE"; color:"#111"; font.pixelSize:12; font.bold:true }
                                MouseArea { anchors.fill:parent }
                            }
                            Text { text:"HEADPHONES"; color:cLabel; font.pixelSize:7; font.letterSpacing:1; anchors.horizontalCenter:parent.horizontalCenter }
                            Text { text:"MONO SPLIT  ◆  STEREO"; color:cLabel; font.pixelSize:6; anchors.horizontalCenter:parent.horizontalCenter }
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter; spacing: 10
                                Knob { value:0.5; lbl:"MIXING\nCUE↔MST"; dot:cCyan }
                                Knob { value:0.7; lbl:"LEVEL"; dot:cCyan }
                            }
                            Text { text:"PHONES"; color:cLabel; font.pixelSize:7; anchors.horizontalCenter:parent.horizontalCenter }
                            // Jack symbol
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 16; height: 16; radius: 8; color:"#0f0f0f"; border.color:"#333"
                                Rectangle { anchors.centerIn:parent; width:6; height:6; radius:3; color:"#444" }
                            }
                        }
                        Item { Layout.fillHeight:true }
                    }
                }

                // ══ 4 CHANNEL STRIPS ═════════════════════════════════════
                Repeater {
                    id: channelRep
                    model: 4

                    property var decks: DJ ? [DJ.deckA, DJ.deckA, DJ.deckB, DJ.deckB] : [null,null,null,null]
                    property var assigns: ["DECK 3/C","DECK 1/A","DECK 2/B","DECK 4/D"]
                    property var accentColors: [cOrange, cCyan, cCyan, cOrange]

                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        color: cPanel; radius: 4; border.color: cSep

                        property var deck: channelRep.decks[index]
                        property color ac: channelRep.accentColors[index]
                        property real eqHi: 0.5
                        property real eqMid: 0.5
                        property real eqLow: 0.5
                        property real trim: 0.5
                        property real color_fx: 0.5
                        property real ch_vol: 0.75
                        property real vuL: vuLevels[index] || 0.1
                        property real vuR: (vuLevels[index] || 0.1) * 0.92

                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 6; spacing: 3

                            // Channel number header
                            Rectangle {
                                Layout.fillWidth: true; height: 30; radius: 3
                                color: Qt.darker(parent.parent.ac, 3.5)
                                border.color: Qt.darker(parent.parent.ac, 1.8)

                                Row {
                                    anchors.centerIn: parent; spacing: 6
                                    Text { text: (index+1).toString(); color: cBright; font.pixelSize: 18; font.bold:true; anchors.verticalCenter:parent.verticalCenter }
                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter; spacing: 1
                                        Text { text: "DIGITAL LINE"; color: cLabel; font.pixelSize: 6 }
                                        Text { text: channelRep.assigns[index]; color: parent.parent.parent.parent.ac; font.pixelSize: 7; font.bold:true }
                                    }
                                }
                            }

                            // CLIP + TRIM row
                            Row {
                                spacing: 4
                                Rectangle { width: 26; height: 11; radius: 2; color: cRed; opacity: 0.25; border.color: cRed; border.width: 1
                                    Text { anchors.centerIn:parent; text:"CLIP"; color:cRed; font.pixelSize:6 } }
                                Text { text:"TRIM"; color:cLabel; font.pixelSize:7; anchors.verticalCenter:parent.verticalCenter }
                            }

                            // TRIM + VU meters row
                            RowLayout {
                                Layout.fillWidth: true; spacing: 4
                                Knob { value:trim; onValueChanged:trim=value; dot:parent.parent.ac }
                                Item { Layout.fillWidth:true }
                                Column {
                                    spacing: 2
                                    // VU scale
                                    Column {
                                        spacing: 0
                                        Repeater {
                                            model: ["+9","","","","0","","","","","","−∞"]
                                            Text { text:modelData; color:"#444"; font.pixelSize:5; height: 9; rightPadding:1 }
                                        }
                                    }
                                }
                                Vu { level: vuL }
                                Vu { level: vuR }
                            }

                            // ── HI EQ ──
                            Text { text:"HI"; color:cLabel; font.pixelSize:7; Layout.alignment:Qt.AlignHCenter; font.letterSpacing:1 }
                            Knob {
                                Layout.alignment: Qt.AlignHCenter; lg:true; dot:cCyan; centerDot:true
                                value: eqHi; onValueChanged: { eqHi=value; if(deck) deck.setEqHigh(value*2.0) }
                            }
                            Row {
                                Layout.alignment: Qt.AlignHCenter; spacing: 10
                                Text { text:"-26"; color:"#3a3a3a"; font.pixelSize:6 }
                                Text { text:"+6";  color:"#3a3a3a"; font.pixelSize:6 }
                            }

                            // ── MID EQ ──
                            Text { text:"MID"; color:cLabel; font.pixelSize:7; Layout.alignment:Qt.AlignHCenter; font.letterSpacing:1 }
                            Knob {
                                Layout.alignment: Qt.AlignHCenter; lg:true; dot:cCyan; centerDot:true
                                value: eqMid; onValueChanged: { eqMid=value; if(deck) deck.setEqMid(value*2.0) }
                            }
                            Row {
                                Layout.alignment: Qt.AlignHCenter; spacing: 3
                                Repeater {
                                    model: ["EQ","ISO"]
                                    Rectangle {
                                        width: 22; height: 12; radius: 2
                                        color: index===0?"#3a3a3a":cKnob; border.color:cSep
                                        Text { anchors.centerIn:parent; text:modelData; color:index===0?cBright:cLabel; font.pixelSize:6 }
                                        MouseArea { anchors.fill:parent }
                                    }
                                }
                            }

                            // ── LOW EQ ──
                            Text { text:"LOW"; color:cLabel; font.pixelSize:7; Layout.alignment:Qt.AlignHCenter; font.letterSpacing:1 }
                            Knob {
                                Layout.alignment: Qt.AlignHCenter; lg:true; dot:cCyan; centerDot:true
                                value: eqLow; onValueChanged: { eqLow=value; if(deck) deck.setEqLow(value*2.0) }
                            }
                            Row {
                                Layout.alignment: Qt.AlignHCenter; spacing: 10
                                Text { text:"-26"; color:"#3a3a3a"; font.pixelSize:6 }
                                Text { text:"+6";  color:"#3a3a3a"; font.pixelSize:6 }
                            }

                            Rectangle { Layout.fillWidth:true; height:1; color:cSep }

                            // ── COLOR knob ──
                            Text { text:"COLOR"; color:cLabel; font.pixelSize:7; Layout.alignment:Qt.AlignHCenter; font.letterSpacing:1 }
                            Knob {
                                Layout.alignment: Qt.AlignHCenter; lg:true; dot:cOrange; centerDot:true
                                value: color_fx; onValueChanged: color_fx=value
                            }
                            Row {
                                Layout.alignment: Qt.AlignHCenter; spacing: 14
                                Text { text:"LOW"; color:"#3a3a3a"; font.pixelSize:6 }
                                Text { text:"HI";  color:"#3a3a3a"; font.pixelSize:6 }
                            }

                            // ── CUE button ──
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                width: 54; height: 24; radius: 3; color: cYellow
                                Text { anchors.centerIn:parent; text:"CUE"; color:"#111"; font.pixelSize:12; font.bold:true }
                                MouseArea { anchors.fill:parent; onClicked: if(deck) deck.cue(true) }
                            }
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                width: 54; height: 14; radius: 2; color: cKnob; border.color: cSep
                                Text { anchors.centerIn:parent; text:"BEAT FX"; color:cLabel; font.pixelSize:6 }
                            }

                            Item { Layout.fillHeight:true }
                        }
                    }
                }

                // ══ RIGHT PANEL: BEAT FX + MASTER ════════════════════════
                Rectangle {
                    Layout.preferredWidth: 148; Layout.fillHeight: true
                    color: cPanel; radius: 4; border.color: cSep

                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 6

                        // MASTER LEVEL + ON/OFF
                        Row {
                            Layout.fillWidth: true; spacing: 8
                            Column {
                                spacing: 3
                                Text { text:"MASTER\nLEVEL"; color:cLabel; font.pixelSize:7; horizontalAlignment:Text.AlignHCenter; anchors.horizontalCenter:parent.horizontalCenter }
                                Knob { lg:true; dot:cCyan; value:masterVol; onValueChanged:{ masterVol=value; if(DJ) DJ.mixer.masterVolume=value*1.5 } }
                                Row {
                                    spacing:4; anchors.horizontalCenter:parent.horizontalCenter
                                    Text { text:"MIN"; color:cLabel; font.pixelSize:6 }
                                    Text { text:"MAX"; color:cLabel; font.pixelSize:6 }
                                }
                            }
                            Column {
                                spacing: 4
                                Text { text:"LEVEL"; color:cLabel; font.pixelSize:7; anchors.horizontalCenter:parent.horizontalCenter }
                                Knob { dot:cCyan; value:0.6 }
                                Text { text:"ON/OFF"; color:cLabel; font.pixelSize:7; anchors.horizontalCenter:parent.horizontalCenter }
                                // Big blue button
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 28; height: 28; radius: 14
                                    color: "#001a44"; border.color: cBlue; border.width: 2
                                    Rectangle { anchors.centerIn:parent; width:16; height:16; radius:8; color:"#0033aa" }
                                    MouseArea { anchors.fill:parent }
                                }
                                Text { text:"BEAT FX"; color:cLabel; font.pixelSize:6; anchors.horizontalCenter:parent.horizontalCenter }
                            }
                        }

                        // Beat FX LCD
                        Rectangle {
                            Layout.fillWidth: true; height: 90
                            color: "#0a1e10"; radius: 3; border.color: "#183028"; border.width:1

                            ColumnLayout {
                                anchors.fill:parent; anchors.margins:7; spacing:3

                                Text { text: beatFxList[beatFxIdx]; color:"#00ff88"; font.family:"monospace"; font.pixelSize:12; font.bold:true; Layout.alignment:Qt.AlignHCenter }

                                // BPM display
                                Rectangle {
                                    Layout.fillWidth:true; height:28; color:"#081408"; radius:2; border.color:"#143020"
                                    Row {
                                        anchors.centerIn:parent; spacing:4
                                        Text { text: masterBpm.toFixed(1); color:"#00ff88"; font.family:"monospace"; font.pixelSize:20; font.bold:true; anchors.verticalCenter:parent.verticalCenter }
                                        Text { text:"BPM"; color:"#009944"; font.pixelSize:9; anchors.verticalCenter:parent.verticalCenter }
                                    }
                                }

                                // Beat fractions
                                Row {
                                    Layout.fillWidth:true; spacing:2
                                    Repeater {
                                        model:["1/2","3/4","1","2","4"]
                                        Rectangle {
                                            width:(parent.parent.width-8)/5; height:14; radius:2
                                            color: index===2?"#1a5030":"#0a1e10"; border.color:"#1a4030"
                                            Text { anchors.centerIn:parent; text:modelData; color:index===2?"#00ff88":"#006633"; font.pixelSize:7 }
                                            MouseArea { anchors.fill:parent }
                                        }
                                    }
                                }
                            }
                        }

                        // X-PAD
                        Rectangle {
                            Layout.fillWidth: true; height: 76
                            color: "#0d1a28"; radius: 3; border.color:"#1a2a44"

                            ColumnLayout {
                                anchors.fill:parent; anchors.margins:6; spacing:3
                                Text { text:"X-PAD"; color:"#3a6aaa"; font.pixelSize:9; font.bold:true; Layout.alignment:Qt.AlignHCenter; font.letterSpacing:1 }
                                Row {
                                    Layout.fillWidth:true; spacing:2
                                    Repeater {
                                        model:["1/16","1/8","1/4","1/2"]
                                        Rectangle {
                                            width:(parent.parent.width-6)/4; height:15; radius:2
                                            color:"#0d1825"; border.color:"#1a3060"
                                            Text { anchors.centerIn:parent; text:modelData; color:"#2a6aff"; font.pixelSize:7 }
                                            MouseArea { anchors.fill:parent }
                                        }
                                    }
                                }
                                Row {
                                    Layout.fillWidth:true; spacing:2
                                    Repeater {
                                        model:["3/4","1","2","4"]
                                        Rectangle {
                                            width:(parent.parent.width-6)/4; height:15; radius:2
                                            color:"#0d1825"; border.color:"#1a3060"
                                            Text { anchors.centerIn:parent; text:modelData; color:"#2a6aff"; font.pixelSize:7 }
                                            MouseArea { anchors.fill:parent }
                                        }
                                    }
                                }
                            }
                        }

                        // BEAT nav + TAP
                        RowLayout {
                            Layout.fillWidth:true; spacing:3
                            Rectangle { Layout.fillWidth:true; height:24; radius:3; color:cKnob; border.color:cSep
                                Text { anchors.centerIn:parent; text:"◀"; color:cText; font.pixelSize:13 }
                                MouseArea { anchors.fill:parent; onClicked: if(beatFxIdx>0) beatFxIdx-- }
                            }
                            Text { text:"BEAT"; color:cLabel; font.pixelSize:7 }
                            Rectangle { Layout.fillWidth:true; height:24; radius:3; color:cKnob; border.color:cSep
                                Text { anchors.centerIn:parent; text:"▶"; color:cText; font.pixelSize:13 }
                                MouseArea { anchors.fill:parent; onClicked: if(beatFxIdx<beatFxList.length-1) beatFxIdx++ }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth:true; spacing:3
                            Rectangle { Layout.fillWidth:true; height:26; radius:3; color:cKnob; border.color:cSep
                                Column { anchors.centerIn:parent; spacing:0
                                    Text { text:"AUTO"; color:cLabel; font.pixelSize:6; anchors.horizontalCenter:parent.horizontalCenter }
                                    Text { text:"TAP";  color:cLabel; font.pixelSize:6; anchors.horizontalCenter:parent.horizontalCenter }
                                }
                            }
                            Rectangle { width:32; height:26; radius:3; color:"#004400"; border.color:"#007700"
                                Text { anchors.centerIn:parent; text:"TAP"; color:cGreen; font.pixelSize:10; font.bold:true }
                                MouseArea { anchors.fill:parent }
                            }
                            Column {
                                Layout.fillWidth:true
                                Text { text:"◆ QUANTIZE"; color:cLabel; font.pixelSize:6; anchors.horizontalCenter:parent.horizontalCenter }
                                Text { text:"UTILITY"; color:cLabel; font.pixelSize:6; anchors.horizontalCenter:parent.horizontalCenter }
                            }
                        }

                        // FX FREQUENCY
                        Column {
                            Layout.fillWidth:true; spacing:4
                            Text { text:"FX FREQUENCY"; color:cLabel; font.pixelSize:7; font.letterSpacing:1; anchors.horizontalCenter:parent.horizontalCenter }
                            Row {
                                anchors.horizontalCenter:parent.horizontalCenter; spacing:3
                                Repeater {
                                    model:["LOW","MID","HI"]
                                    Rectangle {
                                        width:38; height:20; radius:3
                                        color: fxFreqSel===index ? (index===1?"#0a3a7a":"#1a1a3a") : cKnob
                                        border.color: fxFreqSel===index ? (index===1?cBlue:"#4444aa") : cSep
                                        Text { anchors.centerIn:parent; text:modelData; color:fxFreqSel===index?cBright:cLabel; font.pixelSize:9; font.bold:fxFreqSel===index }
                                        MouseArea { anchors.fill:parent; onClicked:fxFreqSel=index }
                                    }
                                }
                            }
                        }

                        // FX type list + TIME/DEPTH knobs
                        Rectangle {
                            Layout.fillWidth:true; Layout.fillHeight:true
                            color:"#191919"; radius:3; border.color:cSep

                            ColumnLayout {
                                anchors.fill:parent; anchors.margins:6; spacing:3

                                Grid { columns:2; spacing:2; rowSpacing:1
                                    Repeater {
                                        model:["FILTER","FLANGER","TRANS","PHASER","REVERB","PITCH","SPIRAL","SLIP ROLL","PING PONG","ROLL","ECHO","VINYL BRAKE","DELAY","HELIX"]
                                        Text {
                                            text:"· "+modelData; font.pixelSize:7; font.family:"monospace"
                                            color: beatFxList[beatFxIdx]===modelData ? cGreen : "#404040"
                                            MouseArea { anchors.fill:parent; onClicked: beatFxIdx = beatFxList.indexOf(modelData) }
                                        }
                                    }
                                }

                                Rectangle { Layout.fillWidth:true; height:1; color:cSep }

                                Row {
                                    Layout.alignment:Qt.AlignHCenter; spacing:8
                                    Column { spacing:2; Knob { dot:cCyan; value:0.5 }; Text { text:"TIME"; color:cLabel; font.pixelSize:7; anchors.horizontalCenter:parent.horizontalCenter } }
                                    Column { spacing:2; Knob { dot:cCyan; value:0.6 }; Text { text:"LEVEL\nDEPTH"; color:cLabel; font.pixelSize:7; horizontalAlignment:Text.AlignHCenter; anchors.horizontalCenter:parent.horizontalCenter } }
                                }

                                // ON/OFF blue button
                                Rectangle {
                                    Layout.alignment:Qt.AlignHCenter
                                    width:34; height:34; radius:17
                                    color:"#001030"; border.color:cBlue; border.width:2
                                    Rectangle { anchors.centerIn:parent; width:20; height:20; radius:10; color:"#002266" }
                                    Text { anchors.bottom:parent.bottom; anchors.horizontalCenter:parent.horizontalCenter; anchors.bottomMargin:-12; text:"ON/OFF"; color:cLabel; font.pixelSize:6 }
                                    MouseArea { anchors.fill:parent }
                                }
                                Item { height:14 }
                            }
                        }
                    }
                }
            }

            // ── BOTTOM: FADERS + CROSSFADER ──────────────────────────────
            Rectangle {
                Layout.fillWidth:true; height: 185
                color: cPanel; radius: 4; border.color: cSep

                ColumnLayout {
                    anchors.fill:parent; anchors.margins:8; spacing:5

                    // Fader row
                    RowLayout {
                        Layout.fillWidth:true; Layout.fillHeight:true; spacing:3

                        // Left spacer matching sidebar
                        Item { Layout.preferredWidth: 118 }

                        Repeater {
                            model: 4
                            ColumnLayout {
                                Layout.fillWidth:true; spacing:3

                                CFader {
                                    Layout.alignment: Qt.AlignHCenter
                                    height: 115; ac: channelRep.accentColors[index]
                                }

                                // THRU A B
                                Row {
                                    Layout.alignment: Qt.AlignHCenter; spacing:2
                                    Repeater {
                                        model:["A","THRU","B"]
                                        Rectangle {
                                            width: modelData==="THRU"?30:18; height:16; radius:2
                                            color: modelData==="THRU"?cKnob:"#141e14"; border.color:cSep
                                            Text { anchors.centerIn:parent; text:modelData; color:modelData==="THRU"?cLabel:cGreen; font.pixelSize:7 }
                                            MouseArea { anchors.fill:parent }
                                        }
                                    }
                                }
                            }
                        }

                        // Right spacer matching right panel
                        ColumnLayout {
                            Layout.preferredWidth: 148; spacing:4

                            Text { text:"BALANCE"; color:cLabel; font.pixelSize:7; Layout.alignment:Qt.AlignHCenter }
                            Knob { Layout.alignment:Qt.AlignHCenter; dot:cCyan; value:0.5; centerDot:true }

                            Rectangle {Layout.fillWidth:true; height:1; color:cSep}

                            Text { text:"BOOTH MONITOR"; color:cLabel; font.pixelSize:7; Layout.alignment:Qt.AlignHCenter }
                            Row {
                                Layout.alignment:Qt.AlignHCenter; spacing:8
                                Knob { dot:cCyan; value:0.7 }
                                Column {
                                    spacing:3
                                    Text { text:"EQ CURVE"; color:cLabel; font.pixelSize:6; anchors.horizontalCenter:parent.horizontalCenter }
                                    Row { spacing:2
                                        Repeater { model:["LINEAR","EQ"]
                                            Rectangle { width:28;height:12;radius:2;color:index===1?"#1a3a1a":cKnob;border.color:cSep
                                                Text{anchors.centerIn:parent;text:modelData;color:index===1?cGreen:cLabel;font.pixelSize:6} } }
                                    }
                                    Text { text:"CH FADER"; color:cLabel; font.pixelSize:6; anchors.horizontalCenter:parent.horizontalCenter }
                                    Row { spacing:2; Repeater{model:3; Rectangle{width:14;height:12;radius:2;color:cKnob;border.color:cSep
                                        Canvas{anchors.fill:parent;anchors.margins:2;onPaint:{var c=getContext("2d");c.clearRect(0,0,width,height);c.strokeStyle="#555";c.lineWidth=1;c.beginPath();if(index===0){c.moveTo(0,height);c.bezierCurveTo(width*.5,height,width*.5,0,width,0)}else if(index===1){c.moveTo(0,height);c.lineTo(width,0)}else{c.moveTo(0,height);c.lineTo(width*.5,height);c.lineTo(width*.5,0);c.lineTo(width,0)};c.stroke()};Component.onCompleted:requestPaint()}}}}
                                    Text { text:"CROSS FADER"; color:cLabel; font.pixelSize:6; anchors.horizontalCenter:parent.horizontalCenter }
                                    Row { spacing:2; Repeater{model:3; Rectangle{width:14;height:12;radius:2;color:cKnob;border.color:cSep}} }
                                }
                            }
                            Item { Layout.fillHeight:true }
                        }
                    }

                    // Crossfader section
                    ColumnLayout {
                        Layout.fillWidth:true; spacing:3

                        Text { text:"CROSS FADER ASSIGN"; color:cLabel; font.pixelSize:7; font.letterSpacing:2; Layout.alignment:Qt.AlignHCenter }

                        RowLayout {
                            Layout.fillWidth:true; spacing:4
                            Item { Layout.preferredWidth:118 }

                            ColumnLayout {
                                Layout.fillWidth:true; spacing:3

                                XFader { Layout.alignment:Qt.AlignHCenter; implicitWidth: parent.width }

                                Row {
                                    Layout.alignment:Qt.AlignHCenter; spacing:3
                                    Text { text:"MAGVEL FADER"; color:"#2e2e2e"; font.pixelSize:7; font.letterSpacing:3; anchors.verticalCenter:parent.verticalCenter }
                                    Repeater {
                                        model:3
                                        Rectangle {
                                            width:16;height:14;radius:2;color:cKnob;border.color:cSep
                                            Canvas {
                                                anchors.fill:parent;anchors.margins:2
                                                onPaint:{
                                                    var c=getContext("2d");c.clearRect(0,0,width,height)
                                                    c.strokeStyle="#555";c.lineWidth=1;c.beginPath()
                                                    if(index===0){c.moveTo(0,height);c.bezierCurveTo(width*.5,height,width*.5,0,width,0)}
                                                    else if(index===1){c.moveTo(0,height);c.lineTo(width,0)}
                                                    else{c.moveTo(0,height);c.lineTo(width*.5,height);c.lineTo(width*.5,0);c.lineTo(width,0)}
                                                    c.stroke()
                                                }
                                                Component.onCompleted: requestPaint()
                                            }
                                            MouseArea { anchors.fill:parent }
                                        }
                                    }
                                }
                            }

                            Item { Layout.preferredWidth:148 }
                        }
                    }
                }
            }
        }

        // Bottom model label
        Text {
            anchors.bottom:parent.bottom; anchors.right:parent.right
            anchors.margins:10
            text:"PROFESSIONAL MIXER  DJM-900NXS2"
            color:"#2e2e2e"; font.pixelSize:8; font.letterSpacing:3; font.family:"monospace"
        }
    }
}
