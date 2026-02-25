// ui_videoeditor.qml - Main QML interface for Aegis Video Editor
// Kdenlive-inspired multi-track video editing interface
//
// FIXES APPLIED:
//   [Bug #2] Flickable.contentWidth: contentWidth → timelineView.contentWidth (loop circolare)
//   [Bug #3] videoEditor.resolution/fps/isPlaying → ora esposti come Q_PROPERTY in C++
//   [Bug #4] newProject(profile) → newProject("Untitled Project", profile) (firma corretta)
//   [Bug #6] Null-check su timeline all'inizio di rulerCanvas.onPaint

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width: 1600
    height: 900
    title: videoEditor.hasProject ?
           ("Aegis Video Editor - " + videoEditor.projectName + (videoEditor.modified ? " *" : "")) :
           "Aegis Video Editor"

    // Menu Bar
    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            MenuItem {
                text: qsTr("&New Project")
                shortcut: "Ctrl+N"
                onTriggered: newProjectDialog.open()
            }
            MenuItem {
                text: qsTr("&Open Project...")
                shortcut: "Ctrl+O"
                onTriggered: openProjectDialog.open()
            }
            MenuItem {
                text: qsTr("&Save Project")
                shortcut: "Ctrl+S"
                onTriggered: videoEditor.saveProject()
            }
            MenuItem {
                text: qsTr("Save Project &As...")
                shortcut: "Ctrl+Shift+S"
                onTriggered: saveProjectDialog.open()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("&Import Media...")
                shortcut: "Ctrl+I"
                onTriggered: importDialog.open()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("E&xit")
                onTriggered: Qt.quit()
            }
        }

        Menu {
            title: qsTr("&Edit")
            MenuItem {
                text: qsTr("&Undo")
                shortcut: "Ctrl+Z"
                enabled: videoEditor.canUndo
                onTriggered: videoEditor.undo()
            }
            MenuItem {
                text: qsTr("&Redo")
                shortcut: "Ctrl+Shift+Z"
                enabled: videoEditor.canRedo
                onTriggered: videoEditor.redo()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("&Split at Playhead")
                shortcut: "S"
                onTriggered: timelineView.splitAtPlayhead()
            }
            MenuItem {
                text: qsTr("&Delete")
                shortcut: "Delete"
                onTriggered: timelineView.deleteSelection()
            }
            MenuItem {
                text: qsTr("Ripple &Delete")
                shortcut: "Shift+Delete"
                onTriggered: timelineView.rippleDeleteSelection()
            }
        }

        Menu {
            title: qsTr("&Project")
            MenuItem {
                text: qsTr("Project &Settings...")
                onTriggered: projectSettingsDialog.open()
            }
            MenuItem {
                text: qsTr("&Render...")
                shortcut: "Ctrl+M"
                onTriggered: renderDialog.open()
            }
        }

        Menu {
            title: qsTr("&Playback")
            MenuItem {
                // [FIX Bug #3] videoEditor.isPlaying ora è una Q_PROPERTY esposta in C++
                text: videoEditor.isPlaying ? qsTr("&Pause") : qsTr("&Play")
                shortcut: "Space"
                onTriggered: videoEditor.togglePlayPause()
            }
            MenuItem {
                text: qsTr("&Stop")
                shortcut: "K"
                onTriggered: videoEditor.stop()
            }
            MenuItem {
                text: qsTr("Previous &Frame")
                shortcut: "Left"
                onTriggered: videoEditor.frameStep(-1)
            }
            MenuItem {
                text: qsTr("&Next Frame")
                shortcut: "Right"
                onTriggered: videoEditor.frameStep(1)
            }
            MenuItem {
                text: qsTr("Go to &Start")
                shortcut: "Home"
                onTriggered: videoEditor.seek(0)
            }
            MenuItem {
                text: qsTr("Go to &End")
                shortcut: "End"
                onTriggered: videoEditor.seek(timelineView.duration)
            }
        }
    }

    // Main Layout
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Left Panel - Project Bin
        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            color: palette.base
            border.color: palette.mid
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    height: 28
                    color: palette.alternateBase

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Project Bin")
                        font.bold: true
                    }
                }

                Button {
                    Layout.fillWidth: true
                    Layout.margins: 4
                    text: qsTr("+ Import Media")
                    onClicked: importDialog.open()
                }

                ListView {
                    id: mediaList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: projectMediaModel

                    delegate: Rectangle {
                        width: mediaList.width
                        height: 60
                        color: ListView.isCurrentItem ? palette.highlight : "transparent"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 2

                            Text {
                                text: modelData.name ?? ""
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                text: modelData.duration ?? ""
                                color: palette.mid
                                font.pixelSize: 10
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: mediaList.currentIndex = index
                            onDoubleClicked: timelineView.addClipToCurrentTrack(modelData.source)
                        }
                    }
                }
            }
        }

        // Center - Preview + Timeline
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Video Preview
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: parent.height * 0.45
                color: "black"

                // Video output placeholder (MPV renders here)
                Rectangle {
                    anchors.fill: parent
                    color: "black"
                }

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.color: "#444"
                    border.width: 1
                }

                // Timecode overlay
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 8
                    width: timecodeDisplay.width + 16
                    height: 28
                    color: "#cc000000"
                    radius: 4

                    Text {
                        id: timecodeDisplay
                        anchors.centerIn: parent
                        text: timelineView.timecodeString
                        color: "white"
                        font.family: "monospace"
                        font.pixelSize: 14
                    }
                }

                // Resolution Badge
                // [FIX Bug #3] videoEditor.resolution e videoEditor.fps ora sono Q_PROPERTY
                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8
                    width: resolutionBadge.width + 16
                    height: 24
                    color: "#cc000000"
                    radius: 4
                    visible: videoEditor.hasProject

                    Text {
                        id: resolutionBadge
                        anchors.centerIn: parent
                        text: videoEditor.resolution.width + "x" + videoEditor.resolution.height +
                              " @ " + videoEditor.fps + "fps"
                        color: "white"
                        font.pixelSize: 12
                    }
                }
            }

            // Transport Controls
            Rectangle {
                Layout.fillWidth: true
                height: 44
                color: palette.window
                border.color: palette.mid
                border.width: 1

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 8

                    Button {
                        text: "|<<"
                        flat: true
                        onClicked: videoEditor.seek(0)
                    }

                    Button {
                        text: "<"
                        flat: true
                        onClicked: videoEditor.frameStep(-1)
                    }

                    Button {
                        // [FIX Bug #3] videoEditor.isPlaying è ora una Q_PROPERTY C++
                        text: videoEditor.isPlaying ? "||" : ">"
                        flat: true
                        font.bold: true
                        font.pixelSize: 16
                        onClicked: videoEditor.togglePlayPause()
                    }

                    Button {
                        text: ">"
                        flat: true
                        onClicked: videoEditor.frameStep(1)
                    }

                    Button {
                        text: ">>"
                        flat: true
                        onClicked: videoEditor.seek(timelineView.duration)
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 24
                        color: palette.mid
                    }

                    // Volume slider (placeholder)
                    Slider {
                        Layout.preferredWidth: 200
                        from: 0
                        to: 100
                        value: 100
                    }
                }
            }

            // Timeline
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: palette.base

                Rectangle {
                    id: timelineView
                    anchors.fill: parent
                    color: "#2d2d2d"

                    // [FIX Bug #5] timeline è ora un TimelineProxy* esposto da VideoEditor
                    property var timeline: videoEditor.timeline

                    // Leggi playhead e duration dal proxy (che delega a VideoEditor)
                    property int playhead: timeline ? timeline.playhead : 0
                    property int duration: timeline ? timeline.duration : 0

                    property int pixelsPerFrame: 4
                    property int trackHeight: 60
                    property int headerWidth: 120
                    property var selectedClip: null
                    property string timecodeString: timeline ? formatTimecode(timeline.playhead) : "00:00:00:00"

                    // Computed layout properties
                    property int contentWidth: duration * pixelsPerFrame + 200
                    property int viewportWidth: width - headerWidth
                    property int totalTrackHeight: (timeline ? (timeline.videoTrackCount + timeline.audioTrackCount) : 0) * (trackHeight + 4) + 40

                    signal clipClicked(var clip)
                    signal clipDoubleClicked(var clip)

                    function formatTimecode(frames) {
                        if (!timeline) return "00:00:00:00"
                        var fps = timeline.profile.fps
                        var totalSeconds = Math.floor(frames / fps)
                        var hours = Math.floor(totalSeconds / 3600)
                        var minutes = Math.floor((totalSeconds % 3600) / 60)
                        var seconds = totalSeconds % 60
                        var frameNum = frames % fps
                        return pad(hours) + ":" + pad(minutes) + ":" + pad(seconds) + ":" + pad(frameNum)
                    }

                    function pad(n) {
                        return n.toString().padStart(2, '0')
                    }

                    function frameToX(frame) {
                        return frame * pixelsPerFrame + headerWidth - hScroll.position * (contentWidth - viewportWidth)
                    }

                    function xToFrame(x) {
                        return Math.floor((x - headerWidth + hScroll.position * (contentWidth - viewportWidth)) / pixelsPerFrame)
                    }

                    function snapToGrid(frame) {
                        if (!timeline) return frame
                        return timeline.snap(frame, 10)
                    }

                    function addClipToCurrentTrack(source) {
                        if (!timeline) return
                        var clip = videoEditor.importMedia(source)
                        if (clip && timeline.videoTracks.length > 0) {
                            var track = timeline.videoTracks[0]
                            timeline.insertClip(clip, track, playhead)
                        }
                    }

                    function splitAtPlayhead() {
                        if (!timeline || !selectedClip) return
                        timeline.splitClip(selectedClip, playhead)
                    }

                    function deleteSelection() {
                        if (!timeline || !selectedClip) return
                        timeline.removeClip(selectedClip)
                        selectedClip = null
                    }

                    function rippleDeleteSelection() {
                        if (!timeline || !selectedClip) return
                        var start = selectedClip.position.frames
                        var end = start + selectedClip.duration.frames
                        timeline.rippleDelete(start, end)
                        selectedClip = null
                    }

                    // ── Ruler ──
                    Rectangle {
                        id: ruler
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 30
                        color: "#3d3d3d"

                        Canvas {
                            id: rulerCanvas
                            anchors.fill: parent
                            anchors.leftMargin: headerWidth

                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)

                                // [FIX Bug #6] Guard: se timeline è null non disegnare nulla
                                if (!timelineView.timeline) return

                                ctx.fillStyle = "#888"
                                ctx.font = "10px sans-serif"
                                ctx.textAlign = "center"

                                // Leggi fps in modo sicuro con fallback
                                var fps = (timelineView.timeline.profile && timelineView.timeline.profile.fps > 0)
                                          ? timelineView.timeline.profile.fps
                                          : 30

                                var startFrame = Math.floor(timelineView.xToFrame(0))
                                var endFrame = Math.floor(timelineView.xToFrame(width))
                                var frameStep = Math.max(1, Math.floor(30 / timelineView.pixelsPerFrame))

                                for (var f = startFrame; f <= endFrame; f += frameStep) {
                                    var x = timelineView.frameToX(f) - headerWidth +
                                            hScroll.position * (timelineView.contentWidth - timelineView.viewportWidth)
                                    if (x >= 0 && x < width) {
                                        var isSecond = f % fps === 0
                                        ctx.fillRect(x, isSecond ? 0 : 15, 1, isSecond ? 30 : 15)
                                        if (isSecond) {
                                            ctx.fillText(timelineView.formatTimecode(f), x, 12)
                                        }
                                    }
                                }
                            }

                            Connections {
                                target: hScroll
                                function onPositionChanged() { rulerCanvas.requestPaint() }
                            }
                            Connections {
                                target: timelineView
                                function onPixelsPerFrameChanged() { rulerCanvas.requestPaint() }
                            }
                        }

                        // Playhead line in ruler
                        Rectangle {
                            x: timelineView.frameToX(timelineView.playhead) - 1
                            y: 0
                            width: 2
                            height: parent.height
                            color: "#ff4444"
                            visible: x >= headerWidth
                        }
                    }

                    // ── Track Headers + Timeline Content ──
                    SplitView {
                        anchors.top: ruler.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: hScroll.top
                        orientation: Qt.Horizontal

                        // Track Headers Panel
                        Rectangle {
                            SplitView.preferredWidth: headerWidth
                            color: "#353535"
                            border.color: "#444"
                            border.width: 1
                            clip: true

                            Column {
                                width: parent.width
                                y: -vScroll.position * Math.max(0, timelineView.totalTrackHeight - trackArea.height)

                                // Video Tracks Header
                                Rectangle {
                                    width: parent.width
                                    height: 24
                                    color: "#404040"

                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("VIDEO")
                                        color: "#aaa"
                                        font.bold: true
                                        font.pixelSize: 10
                                    }

                                    Button {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "+"
                                        flat: true
                                        padding: 2
                                        onClicked: if (timeline) timeline.addVideoTrack()
                                    }
                                }

                                Repeater {
                                    model: timeline ? timeline.videoTracks : []

                                    Rectangle {
                                        width: parent.width
                                        height: trackHeight + 4
                                        color: "#2d2d2d"
                                        border.color: "#444"
                                        border.width: 1

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 4
                                            spacing: 4

                                            Button {
                                                Layout.preferredWidth: 24
                                                Layout.preferredHeight: 24
                                                text: modelData.muted ? "M" : "m"
                                                flat: true
                                                onClicked: modelData.muted = !modelData.muted
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData.name
                                                color: "#ccc"
                                                elide: Text.ElideRight
                                                font.pixelSize: 11
                                            }

                                            Button {
                                                Layout.preferredWidth: 24
                                                Layout.preferredHeight: 24
                                                text: "🔒"
                                                flat: true
                                                visible: modelData.locked
                                                onClicked: modelData.locked = !modelData.locked
                                            }
                                        }
                                    }
                                }

                                // Audio Tracks Header
                                Rectangle {
                                    width: parent.width
                                    height: 24
                                    color: "#404040"

                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("AUDIO")
                                        color: "#aaa"
                                        font.bold: true
                                        font.pixelSize: 10
                                    }

                                    Button {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "+"
                                        flat: true
                                        padding: 2
                                        onClicked: if (timeline) timeline.addAudioTrack()
                                    }
                                }

                                Repeater {
                                    model: timeline ? timeline.audioTracks : []

                                    Rectangle {
                                        width: parent.width
                                        height: trackHeight + 4
                                        color: "#2d2d2d"
                                        border.color: "#444"
                                        border.width: 1

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 4
                                            spacing: 4

                                            Button {
                                                Layout.preferredWidth: 24
                                                Layout.preferredHeight: 24
                                                text: modelData.muted ? "M" : "m"
                                                flat: true
                                                onClicked: modelData.muted = !modelData.muted
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData.name
                                                color: "#ccc"
                                                elide: Text.ElideRight
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Timeline Tracks Area
                        Rectangle {
                            id: trackArea
                            color: "#252525"
                            clip: true

                            Flickable {
                                id: trackFlickable
                                anchors.fill: parent
                                // [FIX Bug #2] Era: contentWidth: contentWidth (loop circolare su se stesso)
                                // Ora usa la proprietà del rettangolo parent timelineView
                                contentWidth: timelineView.contentWidth
                                contentHeight: timelineView.totalTrackHeight

                                // Sincronizzazione con le scrollbar
                                contentX: hScroll.position * Math.max(0, contentWidth - width)
                                contentY: vScroll.position * Math.max(0, contentHeight - height)

                                onContentXChanged: {
                                    var range = contentWidth - width
                                    if (range > 0) hScroll.position = contentX / range
                                }
                                onContentYChanged: {
                                    var range = contentHeight - height
                                    if (range > 0) vScroll.position = contentY / range
                                }

                                Column {
                                    x: 0
                                    y: 0
                                    width: parent.width

                                    // Video Tracks Spacer
                                    Rectangle {
                                        width: parent.width
                                        height: 24
                                        color: "transparent"
                                    }

                                    // Video Tracks
                                    Repeater {
                                        model: timeline ? timeline.videoTracks : []

                                        Rectangle {
                                            width: timelineView.contentWidth
                                            height: trackHeight + 4
                                            color: modelData.locked ? "#1a1a1a" : "#252525"
                                            border.color: "#333"
                                            border.width: 1

                                            // Clips in this track
                                            Repeater {
                                                model: modelData.clips ? modelData.clips : []

                                                Rectangle {
                                                    x: timelineView.frameToX(modelData.position.frames) - timelineView.headerWidth
                                                    y: 2
                                                    width: modelData.duration.frames * timelineView.pixelsPerFrame
                                                    height: trackHeight
                                                    color: timelineView.selectedClip === modelData ? "#0077cc" : "#2196F3"
                                                    radius: 2
                                                    clip: true

                                                    Text {
                                                        anchors.left: parent.left
                                                        anchors.top: parent.top
                                                        anchors.margins: 4
                                                        text: modelData.name
                                                        color: "white"
                                                        font.bold: true
                                                        font.pixelSize: 10
                                                        elide: Text.ElideRight
                                                        width: parent.width - 8
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        onClicked: {
                                                            timelineView.selectedClip = modelData
                                                            timelineView.clipClicked(modelData)
                                                        }
                                                        onDoubleClicked: timelineView.clipDoubleClicked(modelData)
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // Audio Tracks Spacer
                                    Rectangle {
                                        width: parent.width
                                        height: 24
                                        color: "transparent"
                                    }

                                    // Audio Tracks
                                    Repeater {
                                        model: timeline ? timeline.audioTracks : []

                                        Rectangle {
                                            width: timelineView.contentWidth
                                            height: trackHeight + 4
                                            color: modelData.locked ? "#1a1a1a" : "#1e2a1e"
                                            border.color: "#333"
                                            border.width: 1

                                            Repeater {
                                                model: modelData.clips ? modelData.clips : []

                                                Rectangle {
                                                    x: timelineView.frameToX(modelData.position.frames) - timelineView.headerWidth
                                                    y: 2
                                                    width: modelData.duration.frames * timelineView.pixelsPerFrame
                                                    height: trackHeight
                                                    color: timelineView.selectedClip === modelData ? "#007744" : "#4CAF50"
                                                    radius: 2
                                                    clip: true

                                                    Text {
                                                        anchors.left: parent.left
                                                        anchors.top: parent.top
                                                        anchors.margins: 4
                                                        text: modelData.name
                                                        color: "white"
                                                        font.bold: true
                                                        font.pixelSize: 10
                                                        elide: Text.ElideRight
                                                        width: parent.width - 8
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        onClicked: {
                                                            timelineView.selectedClip = modelData
                                                            timelineView.clipClicked(modelData)
                                                        }
                                                        onDoubleClicked: timelineView.clipDoubleClicked(modelData)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // Playhead line
                                Rectangle {
                                    x: timelineView.frameToX(timelineView.playhead)
                                    y: 0
                                    width: 2
                                    height: parent.contentHeight
                                    color: "#ff4444"
                                    z: 100

                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.top: parent.top
                                        width: 12
                                        height: 16
                                        color: "#ff4444"
                                        radius: 2
                                    }
                                }

                                // Selection highlight
                                Rectangle {
                                    x: timelineView.frameToX(timeline ? timeline.selectionStart : 0)
                                    y: 0
                                    width: timeline ? (timeline.selectionEnd - timeline.selectionStart) * timelineView.pixelsPerFrame : 0
                                    height: parent.contentHeight
                                    color: "#4488ff"
                                    opacity: 0.1
                                    visible: timeline && timeline.hasSelection
                                    z: 50
                                }

                                // Click to seek
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    z: -1
                                    onClicked: (mouse) => {
                                        var frame = timelineView.snapToGrid(timelineView.xToFrame(mouse.x))
                                        videoEditor.seek(frame)
                                    }
                                }
                            }
                        }
                    }

                    // Horizontal Scrollbar
                    ScrollBar {
                        id: hScroll
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: headerWidth
                        orientation: Qt.Horizontal
                        size: timelineView.viewportWidth > 0 ? Math.min(1.0, timelineView.viewportWidth / timelineView.contentWidth) : 1.0
                    }

                    // Vertical Scrollbar
                    ScrollBar {
                        id: vScroll
                        anchors.right: parent.right
                        anchors.top: ruler.bottom
                        anchors.bottom: hScroll.top
                        orientation: Qt.Vertical
                        size: trackArea.height > 0 ? Math.min(1.0, trackArea.height / Math.max(1, timelineView.totalTrackHeight)) : 1.0
                    }
                }
            }
        }

        // Right Panel - Effects/Properties
        Rectangle {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            color: palette.base
            border.color: palette.mid
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    height: 28
                    color: palette.alternateBase

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Properties")
                        font.bold: true
                    }
                }

                StackLayout {
                    id: propertiesStack
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // Default: no selection
                    Item {
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("No clip selected")
                            color: palette.mid
                        }
                    }

                    // Clip Properties
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        GroupBox {
                            Layout.fillWidth: true
                            Layout.margins: 8
                            title: qsTr("Clip")

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 4

                                Label { text: qsTr("Name:") }
                                TextField {
                                    Layout.fillWidth: true
                                    text: timelineView.selectedClip ? timelineView.selectedClip.name : ""
                                }

                                Label { text: qsTr("Speed:") }
                                SpinBox {
                                    Layout.fillWidth: true
                                    from: 10
                                    to: 1000
                                    value: timelineView.selectedClip ? timelineView.selectedClip.speed * 100 : 100
                                    suffix: "%"
                                }

                                Label { text: qsTr("Volume:") }
                                Slider {
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 200
                                    value: timelineView.selectedClip ? timelineView.selectedClip.volume * 100 : 100
                                }
                            }
                        }

                        GroupBox {
                            Layout.fillWidth: true
                            Layout.margins: 8
                            title: qsTr("Effects")

                            ColumnLayout {
                                anchors.fill: parent

                                Button {
                                    Layout.fillWidth: true
                                    text: qsTr("+ Add Effect")
                                    onClicked: effectsMenu.popup()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Dialogs ────────────────────────────────────────────────────────────────

    FileDialog {
        id: importDialog
        title: qsTr("Import Media")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("All Media") + " (*.mp4 *.mov *.avi *.mkv *.webm *.mp3 *.wav *.flac *.aac *.jpg *.jpeg *.png)",
            qsTr("Video Files") + " (*.mp4 *.mov *.avi *.mkv *.webm)",
            qsTr("Audio Files") + " (*.mp3 *.wav *.flac *.aac *.ogg *.m4a)",
            qsTr("Image Files") + " (*.jpg *.jpeg *.png *.bmp *.tiff *.webp)"
        ]
        onAccepted: {
            for (var i = 0; i < selectedFiles.length; i++) {
                videoEditor.importMedia(selectedFiles[i])
            }
        }
    }

    FileDialog {
        id: openProjectDialog
        title: qsTr("Open Project")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Aegis Projects") + " (*.aegis)"]
        onAccepted: videoEditor.openProject(selectedFile)
    }

    FileDialog {
        id: saveProjectDialog
        title: qsTr("Save Project As")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Aegis Projects") + " (*.aegis)"]
        onAccepted: videoEditor.saveProject(selectedFile)
    }

    FileDialog {
        id: renderDialog
        title: qsTr("Render Project")
        fileMode: FileDialog.SaveFile
        nameFilters: [
            qsTr("MP4 Video") + " (*.mp4)",
            qsTr("MOV Video") + " (*.mov)",
            qsTr("MKV Video") + " (*.mkv)"
        ]
        onAccepted: videoEditor.startRender(selectedFile)
    }

    Dialog {
        id: newProjectDialog
        title: qsTr("New Project")
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            spacing: 16

            RowLayout {
                Label { text: qsTr("Name:") }
                TextField {
                    id: projectNameField
                    text: "Untitled Project"
                    Layout.fillWidth: true
                }
            }

            RowLayout {
                Label { text: qsTr("Resolution:") }
                ComboBox {
                    id: resolutionCombo
                    model: ["1920x1080 (1080p)", "1280x720 (720p)", "3840x2160 (4K)", "2560x1440 (2K)"]
                    currentIndex: 0
                }
            }

            RowLayout {
                Label { text: qsTr("Frame Rate:") }
                ComboBox {
                    id: fpsCombo
                    model: ["24 fps", "25 fps", "30 fps", "60 fps"]
                    currentIndex: 2
                }
            }

            RowLayout {
                Label { text: qsTr("Sample Rate:") }
                ComboBox {
                    id: sampleRateCombo
                    model: ["44100 Hz", "48000 Hz", "96000 Hz"]
                    currentIndex: 1
                }
            }
        }

        onAccepted: {
            // Parse resolution
            var resMap = {"0": [1920,1080], "1": [1280,720], "2": [3840,2160], "3": [2560,1440]}
            var res = resMap[resolutionCombo.currentIndex.toString()] || [1920,1080]
            var fpsMap = {"0": 24, "1": 25, "2": 30, "3": 60}
            var fps = fpsMap[fpsCombo.currentIndex.toString()] || 30
            var srMap = {"0": 44100, "1": 48000, "2": 96000}
            var sr = srMap[sampleRateCombo.currentIndex.toString()] || 48000

            var profile = {
                width:  res[0],
                height: res[1],
                fps:    fps,
                sampleRate: sr
            }

            // [FIX Bug #4] Era: videoEditor.newProject(profile) — mancava il parametro name
            // Firma C++: newProject(const QString& name, const ProjectProfile& profile)
            videoEditor.newProject(projectNameField.text, profile)
        }
    }

    Dialog {
        id: projectSettingsDialog
        title: qsTr("Project Settings")
        standardButtons: Dialog.Ok | Dialog.Cancel

        Label {
            text: qsTr("Project settings would appear here.")
        }
    }

    // Effects context menu
    Menu {
        id: effectsMenu

        MenuItem { text: qsTr("Color Correction"); onTriggered: addEffect("colorcorrection") }
        MenuItem { text: qsTr("Brightness/Contrast"); onTriggered: addEffect("brightness") }
        MenuItem { text: qsTr("Blur"); onTriggered: addEffect("blur") }
        MenuItem { text: qsTr("Sharpen"); onTriggered: addEffect("sharpen") }
        MenuSeparator {}
        MenuItem { text: qsTr("Crop"); onTriggered: addEffect("crop") }
        MenuItem { text: qsTr("Vignette"); onTriggered: addEffect("vignette") }
        MenuSeparator {}
        MenuItem { text: qsTr("Chroma Key"); onTriggered: addEffect("chromakey") }
    }

    // Clip right-click menu
    Menu {
        id: clipContextMenu

        MenuItem {
            text: qsTr("Split at Playhead")
            onTriggered: timelineView.splitAtPlayhead()
        }
        MenuItem {
            text: qsTr("Delete")
            onTriggered: timelineView.deleteSelection()
        }
        MenuItem {
            text: qsTr("Ripple Delete")
            onTriggered: timelineView.rippleDeleteSelection()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Properties")
            onTriggered: propertiesStack.currentIndex = 1
        }
    }

    // ── Helper functions ────────────────────────────────────────────────────

    function formatDuration(frames) {
        // [FIX Bug #3] Usa videoEditor.fps (Q_PROPERTY) invece di videoEditor.profile.fps
        var fps = videoEditor.fps > 0 ? videoEditor.fps : 30
        var seconds = Math.floor(frames / fps)
        var mins = Math.floor(seconds / 60)
        var secs = seconds % 60
        var frms = frames % fps
        return mins.toString().padStart(2, '0') + ':' +
               secs.toString().padStart(2, '0') + ':' +
               frms.toString().padStart(2, '0')
    }

    function addEffect(effectId) {
        if (timelineView.selectedClip) {
            if (typeof timelineView.selectedClip.addEffect === "function") {
                timelineView.selectedClip.addEffect(effectId)
            }
        }
    }
}
