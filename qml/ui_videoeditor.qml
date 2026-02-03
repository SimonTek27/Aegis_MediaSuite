// UI_VideoEditor.qml - Main QML interface for Aegis Video Editor
// Kdenlive-inspired multi-track video editing interface
// Combined TimelineView and VideoEditorMain components

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width: 1600
    height: 900
    title: videoEditor.hasProject ? ("Aegis Video Editor - " + videoEditor.projectPath) : "Aegis Video Editor"
    
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
                
                // Panel Header
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
                
                // Import Button
                Button {
                    Layout.fillWidth: true
                    Layout.margins: 4
                    text: qsTr("+ Import Media")
                    onClicked: importDialog.open()
                }
                
                // Media List
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
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 8
                            
                            // Thumbnail
                            Rectangle {
                                Layout.preferredWidth: 80
                                Layout.preferredHeight: 45
                                color: "#333"
                                
                                Image {
                                    anchors.fill: parent
                                    source: model.thumbnail
                                    fillMode: Image.PreserveAspectFit
                                }
                            }
                            
                            // Info
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                
                                Text {
                                    text: model.name
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                    color: ListView.isCurrentItem ? palette.highlightedText : palette.text
                                }
                                
                                Text {
                                    text: model.duration
                                    font.pixelSize: 11
                                    color: ListView.isCurrentItem ? palette.highlightedText : palette.text
                                    opacity: 0.7
                                }
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            onClicked: mediaList.currentIndex = index
                            onDoubleClicked: timelineView.addClipToCurrentTrack(model.source)
                        }
                    }
                }
            }
        }
        
        // Center - Video Monitor + Timeline
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            
            // Video Monitor
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: parent.height * 0.45
                color: "#1a1a1a"
                
                // Video Display
                Image {
                    id: videoDisplay
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height * 16/9) - 20
                    height: width * 9/16
                    fillMode: Image.PreserveAspectFit
                    source: "image://videoPreview/" + timelineView.playhead
                    
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.color: "#444"
                        border.width: 1
                    }
                }
                
                // Overlay Info
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
                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8
                    width: resolutionBadge.width + 16
                    height: 24
                    color: "#cc000000"
                    radius: 4
                    
                    Text {
                        id: resolutionBadge
                        anchors.centerIn: parent
                        text: videoEditor.resolution.width + "x" + videoEditor.resolution.height + " @ " + videoEditor.fps + "fps"
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
                
                // Timeline Component (integrated)
                Rectangle {
                    id: timelineView
                    anchors.fill: parent
                    color: "#2d2d2d"
                    
                    // Properties
                    property var timeline: videoEditor.timeline
                    property int playhead: timeline ? timeline.playhead : 0
                    property int duration: timeline ? timeline.duration : 0
                    property int pixelsPerFrame: 4
                    property int trackHeight: 60
                    property int headerWidth: 120
                    property var selectedClip: null
                    property string timecodeString: timeline ? formatTimecode(timeline.playhead) : "00:00:00:00"
                    
                    // Signals
                    signal clipClicked(var clip)
                    signal clipDoubleClicked(var clip)
                    
                    // Format timecode
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
                    
                    // Add clip to current track
                    function addClipToCurrentTrack(source) {
                        if (!timeline) return
                        var clip = videoEditor.importMedia(source)
                        if (clip && timeline.videoTracks.length > 0) {
                            var track = timeline.videoTracks[0]
                            timeline.insertClip(clip, track, playhead)
                        }
                    }
                    
                    // Edit operations
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
                        var start = selectedClip.position
                        var end = start + selectedClip.duration
                        timeline.rippleDelete(start, end)
                        selectedClip = null
                    }
                    
                    // Layout calculations
                    property int contentWidth: duration * pixelsPerFrame + 200
                    property int viewportWidth: width - headerWidth
                    property int totalTrackHeight: (timeline ? (timeline.videoTrackCount + timeline.audioTrackCount) : 0) * (trackHeight + 4) + 40
                    
                    // Ruler
                    Rectangle {
                        id: ruler
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 30
                        color: "#3d3d3d"
                        
                        // Time markers
                        Canvas {
                            id: rulerCanvas
                            anchors.fill: parent
                            anchors.leftMargin: headerWidth
                            
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                ctx.fillStyle = "#888"
                                ctx.font = "10px sans-serif"
                                ctx.textAlign = "center"
                                
                                var startFrame = Math.floor(xToFrame(0))
                                var endFrame = Math.floor(xToFrame(width))
                                var frameStep = Math.max(1, Math.floor(30 / pixelsPerFrame))
                                
                                for (var f = startFrame; f <= endFrame; f += frameStep) {
                                    var x = frameToX(f) - headerWidth + hScroll.position * (contentWidth - viewportWidth)
                                    if (x >= 0 && x < width) {
                                        var isSecond = f % timeline.profile.fps === 0
                                        ctx.fillRect(x, isSecond ? 0 : 15, 1, isSecond ? 30 : 15)
                                        if (isSecond) {
                                            ctx.fillText(formatTimecode(f), x, 12)
                                        }
                                    }
                                }
                            }
                            
                            // Redraw when scrolled
                            Connections {
                                target: hScroll
                                function onPositionChanged() { rulerCanvas.requestPaint() }
                            }
                            
                            // Redraw when zoom changes
                            Connections {
                                target: timelineView
                                function onPixelsPerFrameChanged() { rulerCanvas.requestPaint() }
                            }
                        }
                        
                        // Playhead line in ruler
                        Rectangle {
                            x: frameToX(playhead) - 1
                            y: 0
                            width: 2
                            height: parent.height
                            color: "#ff4444"
                            visible: x >= headerWidth
                        }
                    }
                    
                    // Track Headers + Timeline Content
                    SplitView {
                        anchors.top: ruler.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        orientation: Qt.Horizontal
                        
                        // Track Headers Panel
                        Rectangle {
                            SplitView.preferredWidth: headerWidth
                            color: "#353535"
                            border.color: "#444"
                            border.width: 1
                            
                            Column {
                                width: parent.width
                                y: -vScroll.position * (totalTrackHeight - trackArea.height)
                                
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
                                
                                // Video Track Headers
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
                                                text: modelData.muted ? "M" : "V"
                                                flat: true
                                                onClicked: modelData.muted = !modelData.muted
                                            }
                                            
                                            Label {
                                                Layout.fillWidth: true
                                                text: modelData.name
                                                color: "#ddd"
                                                elide: Text.ElideRight
                                            }
                                            
                                            Button {
                                                Layout.preferredWidth: 20
                                                Layout.preferredHeight: 20
                                                text: "L"
                                                flat: true
                                                checked: modelData.locked
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
                                
                                // Audio Track Headers
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
                                                text: modelData.muted ? "M" : "A"
                                                flat: true
                                                onClicked: modelData.muted = !modelData.muted
                                            }
                                            
                                            Label {
                                                Layout.fillWidth: true
                                                text: modelData.name
                                                color: "#ddd"
                                                elide: Text.ElideRight
                                            }
                                            
                                            Button {
                                                Layout.preferredWidth: 20
                                                Layout.preferredHeight: 20
                                                text: "L"
                                                flat: true
                                                checked: modelData.locked
                                                onClicked: modelData.locked = !modelData.locked
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
                            
                            // Tracks Content
                            Flickable {
                                id: trackFlickable
                                anchors.fill: parent
                                contentWidth: contentWidth
                                contentHeight: totalTrackHeight
                                
                                // Sync with scrollbars
                                contentX: hScroll.position * (contentWidth - width)
                                contentY: vScroll.position * (contentHeight - height)
                                
                                onContentXChanged: {
                                    hScroll.position = contentX / (contentWidth - width)
                                }
                                onContentYChanged: {
                                    vScroll.position = contentY / (contentHeight - height)
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
                                            color: modelData.locked ? "#3a3020" : "#2a2a2a"
                                            border.color: "#444"
                                            border.width: 1
                                            
                                            // Clips on this track
                                            Repeater {
                                                model: modelData.clips
                                                
                                                Rectangle {
                                                    x: frameToX(modelData.position)
                                                    y: 2
                                                    width: modelData.duration * pixelsPerFrame
                                                    height: trackHeight
                                                    color: selectedClip === modelData ? "#4488ff" : "#4a6fa5"
                                                    border.color: selectedClip === modelData ? "#88bbff" : "#5a7fb5"
                                                    border.width: selectedClip === modelData ? 2 : 1
                                                    radius: 3
                                                    
                                                    // Clip content
                                                    ColumnLayout {
                                                        anchors.fill: parent
                                                        anchors.margins: 4
                                                        spacing: 2
                                                        
                                                        // Clip name
                                                        Text {
                                                            text: modelData.name
                                                            color: "white"
                                                            font.bold: true
                                                            font.pixelSize: 11
                                                            elide: Text.ElideRight
                                                            Layout.fillWidth: true
                                                        }
                                                        
                                                        // Thumbnail strip (simplified)
                                                        Rectangle {
                                                            Layout.fillWidth: true
                                                            Layout.fillHeight: true
                                                            color: "#333"
                                                            opacity: 0.5
                                                        }
                                                        
                                                        // Duration
                                                        Text {
                                                            text: formatTimecode(modelData.duration)
                                                            color: "#ccc"
                                                            font.pixelSize: 9
                                                        }
                                                    }
                                                    
                                                    // In/Out point handles
                                                    Rectangle {
                                                        anchors.left: parent.left
                                                        anchors.top: parent.top
                                                        anchors.bottom: parent.bottom
                                                        width: 6
                                                        color: "#88bbff"
                                                        opacity: 0.5
                                                        visible: selectedClip === modelData
                                                        
                                                        MouseArea {
                                                            anchors.fill: parent
                                                            cursorShape: Qt.SizeHorCursor
                                                            drag.target: parent.parent
                                                            drag.axis: Drag.XAxis
                                                            // TODO: Implement trim start
                                                        }
                                                    }
                                                    
                                                    Rectangle {
                                                        anchors.right: parent.right
                                                        anchors.top: parent.top
                                                        anchors.bottom: parent.bottom
                                                        width: 6
                                                        color: "#88bbff"
                                                        opacity: 0.5
                                                        visible: selectedClip === modelData
                                                        
                                                        MouseArea {
                                                            anchors.fill: parent
                                                            cursorShape: Qt.SizeHorCursor
                                                            // TODO: Implement trim end
                                                        }
                                                    }
                                                    
                                                    MouseArea {
                                                        anchors.fill: parent
                                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                        
                                                        onClicked: (mouse) => {
                                                            selectedClip = modelData
                                                            clipClicked(modelData)
                                                            if (mouse.button === Qt.RightButton) {
                                                                clipContextMenu.popup()
                                                            }
                                                        }
                                                        
                                                        onDoubleClicked: {
                                                            clipDoubleClicked(modelData)
                                                        }
                                                        
                                                        onPositionChanged: (mouse) => {
                                                            if (drag.active) {
                                                                var newFrame = snapToGrid(xToFrame(parent.x + mouse.x - mouse.buttonDownX))
                                                                if (!modelData.track.hasOverlap(newFrame, newFrame + modelData.duration, modelData)) {
                                                                    timeline.moveClip(modelData, modelData.track, newFrame)
                                                                }
                                                            }
                                                        }
                                                        
                                                        drag.target: parent
                                                        drag.axis: Drag.XAxis
                                                        drag.smoothed: false
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
                                            color: modelData.locked ? "#3a3020" : "#2a2a2a"
                                            border.color: "#444"
                                            border.width: 1
                                            
                                            // Audio clips/waveforms
                                            Repeater {
                                                model: modelData.clips
                                                
                                                Rectangle {
                                                    x: frameToX(modelData.position)
                                                    y: 2
                                                    width: modelData.duration * pixelsPerFrame
                                                    height: trackHeight
                                                    color: selectedClip === modelData ? "#44aa66" : "#3a8a50"
                                                    border.color: selectedClip === modelData ? "#88ddaa" : "#4a9a60"
                                                    border.width: selectedClip === modelData ? 2 : 1
                                                    radius: 3
                                                    
                                                    // Waveform visualization (placeholder)
                                                    Canvas {
                                                        anchors.fill: parent
                                                        anchors.margins: 2
                                                        
                                                        onPaint: {
                                                            var ctx = getContext("2d")
                                                            ctx.clearRect(0, 0, width, height)
                                                            ctx.fillStyle = "#2a5a3a"
                                                            
                                                            // Draw simple waveform pattern
                                                            var bars = Math.floor(width / 4)
                                                            for (var i = 0; i < bars; i++) {
                                                                var h = Math.random() * height * 0.8 + height * 0.1
                                                                var y = (height - h) / 2
                                                                ctx.fillRect(i * 4, y, 2, h)
                                                            }
                                                        }
                                                    }
                                                    
                                                    // Clip label
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
                                                            selectedClip = modelData
                                                            clipClicked(modelData)
                                                        }
                                                        onDoubleClicked: clipDoubleClicked(modelData)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                
                                // Playhead line
                                Rectangle {
                                    x: frameToX(playhead)
                                    y: 0
                                    width: 2
                                    height: parent.contentHeight
                                    color: "#ff4444"
                                    z: 100
                                    
                                    // Playhead handle
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
                                    x: frameToX(timeline ? timeline.selectionStart : 0)
                                    y: 0
                                    width: timeline ? (timeline.selectionEnd - timeline.selectionStart) * pixelsPerFrame : 0
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
                                        var frame = snapToGrid(xToFrame(mouse.x))
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
                        size: viewportWidth / contentWidth
                    }
                    
                    // Vertical Scrollbar
                    ScrollBar {
                        id: vScroll
                        anchors.right: parent.right
                        anchors.top: ruler.bottom
                        anchors.bottom: hScroll.top
                        orientation: Qt.Vertical
                        size: trackArea.height / totalTrackHeight
                    }
                    
                    // Zoom controls
                    RowLayout {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 4
                        spacing: 4
                        z: 200
                        
                        Button {
                            text: "-"
                            flat: true
                            onClicked: pixelsPerFrame = Math.max(1, pixelsPerFrame - 1)
                        }
                        
                        Label {
                            text: Math.round(pixelsPerFrame * 100 / 4) + "%"
                            color: "#aaa"
                            font.pixelSize: 11
                        }
                        
                        Button {
                            text: "+"
                            flat: true
                            onClicked: pixelsPerFrame = Math.min(20, pixelsPerFrame + 1)
                        }
                    }
                    
                    // Clip Context Menu
                    Menu {
                        id: clipContextMenu
                        
                        MenuItem {
                            text: qsTr("Split at Playhead")
                            onTriggered: splitAtPlayhead()
                        }
                        
                        MenuItem {
                            text: qsTr("Copy")
                            onTriggered: {
                                // TODO: Implement copy
                            }
                        }
                        
                        MenuItem {
                            text: qsTr("Delete")
                            onTriggered: deleteSelection()
                        }
                        
                        MenuItem {
                            text: qsTr("Ripple Delete")
                            onTriggered: rippleDeleteSelection()
                        }
                        
                        MenuSeparator {}
                        
                        MenuItem {
                            text: qsTr("Properties")
                            onTriggered: {
                                propertiesStack.currentIndex = 1
                            }
                        }
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
                
                // Panel Header
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
                
                // Properties Stack
                StackLayout {
                    id: propertiesStack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: 0
                    
                    // No Selection
                    Rectangle {
                        color: palette.base
                        
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Select a clip to edit properties")
                            color: palette.text
                            opacity: 0.5
                        }
                    }
                    
                    // Clip Properties
                    ScrollView {
                        clip: true
                        
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                            
                            // Clip Info
                            GroupBox {
                                Layout.fillWidth: true
                                Layout.margins: 8
                                title: qsTr("Clip Info")
                                
                                GridLayout {
                                    anchors.fill: parent
                                    columns: 2
                                    rowSpacing: 4
                                    
                                    Label { text: qsTr("Name:") }
                                    TextField {
                                        Layout.fillWidth: true
                                        text: timelineView.selectedClip ? timelineView.selectedClip.name : ""
                                    }
                                    
                                    Label { text: qsTr("Source:") }
                                    Label {
                                        Layout.fillWidth: true
                                        text: timelineView.selectedClip ? timelineView.selectedClip.source : ""
                                        elide: Text.ElideMiddle
                                        font.pixelSize: 11
                                    }
                                    
                                    Label { text: qsTr("Duration:") }
                                    Label {
                                        text: timelineView.selectedClip ? formatDuration(timelineView.selectedClip.duration) : ""
                                    }
                                }
                            }
                            
                            // Transform
                            GroupBox {
                                Layout.fillWidth: true
                                Layout.margins: 8
                                title: qsTr("Transform")
                                
                                ColumnLayout {
                                    anchors.fill: parent
                                    
                                    RowLayout {
                                        Label { text: qsTr("Position:") }
                                        SpinBox { from: -10000; to: 10000; value: 0 }
                                        SpinBox { from: -10000; to: 10000; value: 0 }
                                    }
                                    
                                    RowLayout {
                                        Label { text: qsTr("Scale:") }
                                        Slider { from: 0; to: 500; value: 100 }
                                        Label { text: "100%" }
                                    }
                                    
                                    RowLayout {
                                        Label { text: qsTr("Rotation:") }
                                        Slider { from: -180; to: 180; value: 0 }
                                        Label { text: "0°" }
                                    }
                                    
                                    RowLayout {
                                        Label { text: qsTr("Opacity:") }
                                        Slider { from: 0; to: 100; value: 100 }
                                        Label { text: "100%" }
                                    }
                                }
                            }
                            
                            // Speed
                            GroupBox {
                                Layout.fillWidth: true
                                Layout.margins: 8
                                title: qsTr("Time")
                                
                                RowLayout {
                                    anchors.fill: parent
                                    
                                    Label { text: qsTr("Speed:") }
                                    Slider { from: 10; to: 1000; value: 100 }
                                    Label { text: "100%" }
                                }
                            }
                            
                            // Effects Stack
                            GroupBox {
                                Layout.fillWidth: true
                                Layout.margins: 8
                                title: qsTr("Effects")
                                
                                ColumnLayout {
                                    anchors.fill: parent
                                    
                                    ListView {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 120
                                        model: timelineView.selectedClip ? timelineView.selectedClip.effects : []
                                        
                                        delegate: Rectangle {
                                            width: parent.width
                                            height: 28
                                            color: "transparent"
                                            
                                            RowLayout {
                                                anchors.fill: parent
                                                
                                                CheckBox {
                                                    checked: true
                                                }
                                                
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: modelData.name
                                                }
                                                
                                                Button {
                                                    text: "x"
                                                    flat: true
                                                    onClicked: timelineView.selectedClip.removeEffect(index)
                                                }
                                            }
                                        }
                                    }
                                    
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
    }
    
    // Dialogs
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
                Label { text: qsTr("Resolution:") }
                ComboBox {
                    model: ["1920x1080 (1080p)", "1280x720 (720p)", "3840x2160 (4K)", "2560x1440 (2K)"]
                    currentIndex: 0
                }
            }
            
            RowLayout {
                Label { text: qsTr("Frame Rate:") }
                ComboBox {
                    model: ["24 fps", "25 fps", "30 fps", "60 fps"]
                    currentIndex: 2
                }
            }
            
            RowLayout {
                Label { text: qsTr("Sample Rate:") }
                ComboBox {
                    model: ["44100 Hz", "48000 Hz", "96000 Hz"]
                    currentIndex: 1
                }
            }
        }
        
        onAccepted: {
            var profile = {
                width: 1920,
                height: 1080,
                fps: 30,
                sampleRate: 48000
            }
            videoEditor.newProject(profile)
        }
    }
    
    // Effects Menu
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
    
    function formatDuration(frames) {
        var seconds = Math.floor(frames / videoEditor.fps)
        var mins = Math.floor(seconds / 60)
        var secs = seconds % 60
        var frms = frames % videoEditor.fps
        return mins.toString().padStart(2, '0') + ':' + 
               secs.toString().padStart(2, '0') + ':' + 
               frms.toString().padStart(2, '0')
    }
    
    function addEffect(effectId) {
        if (timelineView.selectedClip) {
            timelineView.selectedClip.addEffect(effectId)
        }
    }
}