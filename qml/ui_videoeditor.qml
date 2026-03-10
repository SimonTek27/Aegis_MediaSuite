// ui_videoeditor.qml - Main QML interface for Aegis Video Editor
// Kdenlive-inspired multi-track video editing interface
//

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

// Remove unused imports: Qt5Compat.GraphicalEffects

ApplicationWindow {
    id: root
    visible: true
    width: 1920
    height: 1080
    minimumWidth: 1280
    minimumHeight: 720
    title: videoEditor.hasProject ?
    ("Aegis Pro - " + videoEditor.projectName + (videoEditor.modified ? " ●" : "")) :
    "Aegis Pro Video Editor"

    // Material Design styling
    Material.theme: Material.Dark
    Material.accent: Material.Purple
    Material.primary: Material.BlueGrey

    // Global properties
    property real zoomLevel: 1.0
    property bool snapEnabled: true
    property bool rippleEnabled: true
    property bool autoSelect: true
    property string workspaceMode: "standard" // "standard", "color", "audio", "effects"

    // -------------------------------------------------------------------------
    // TimelineView — inline multi-track timeline component
    // -------------------------------------------------------------------------
    component TimelineView : Rectangle {
        // Input properties
        property real zoom: 1.0
        property bool snapEnabled: false
        property bool rippleEnabled: false

        // Read-only state exposed to parent
        property real duration: 0
        property real playhead: 0
        property real viewportWidth: width
        property int  videoTracks: 0
        property int  audioTracks: 0
        property string timecodeString: "00:00:00:00"
        property var selectedClip: null

        // Functions called by toolbar / menus
        function cutSelection()              { console.log("timeline: cutSelection") }
        function copySelection()             { console.log("timeline: copySelection") }
        function pasteClipboard()            { console.log("timeline: pasteClipboard") }
        function splitAtPlayhead()           { console.log("timeline: splitAtPlayhead") }
        function deleteSelection()           { console.log("timeline: deleteSelection") }
        function rippleDeleteSelection()     { console.log("timeline: rippleDeleteSelection") }
        function addClipToCurrentTrack(src)  { console.log("timeline: addClip", src) }
        function addEffectToSelectedClip(id) { console.log("timeline: addEffect", id) }
        function addTransition(id)           { console.log("timeline: addTransition", id) }
        function toggleAudioEffect(id, on)   { console.log("timeline: toggleAudioFx", id, on) }
        function removeEffect(id)            { console.log("timeline: removeEffect", id) }
        function addKeyframe()               { console.log("timeline: addKeyframe") }
        function removeAllKeyframes()        { console.log("timeline: removeAllKeyframes") }

        color: "#111111"
        clip: true

        Text {
            anchors.centerIn: parent
            text: "Timeline — connect videoeditorRef to enable editing"
            color: "#555555"
            font.pixelSize: 13
        }
    }

    // Menu Bar professionale
    menuBar: MenuBar {
        background: Rectangle {
            color: Material.background
            border.color: Material.accent
            border.width: 1
        }

        Menu {
            title: "🎬 &File"
            font.pixelSize: 13

            MenuItem {
                text: "📁 &New Project"
                shortcut: "Ctrl+N"
                icon.source: "qrc:/icons/new.svg"
                onTriggered: newProjectDialog.open()
            }
            MenuItem {
                text: "📂 &Open Project..."
                shortcut: "Ctrl+O"
                icon.source: "qrc:/icons/open.svg"
                onTriggered: openProjectDialog.open()
            }
            MenuItem {
                text: "💾 &Save"
                shortcut: "Ctrl+S"
                icon.source: "qrc:/icons/save.svg"
                onTriggered: videoEditor.saveProject()
            }
            MenuItem {
                text: "💾 Save &As..."
                shortcut: "Ctrl+Shift+S"
                onTriggered: saveProjectDialog.open()
            }
            MenuSeparator {}
            MenuItem {
                text: "📥 &Import Media..."
                shortcut: "Ctrl+I"
                icon.source: "qrc:/icons/import.svg"
                onTriggered: importDialog.open()
            }
            MenuItem {
                text: "📤 &Export..."
                shortcut: "Ctrl+E"
                icon.source: "qrc:/icons/export.svg"
                onTriggered: renderDialog.open()
            }
            MenuSeparator {}
            MenuItem {
                text: "⚙ Project Settings..."
                onTriggered: projectSettingsDialog.open()
            }
            MenuSeparator {}
            MenuItem {
                text: "🚪 Exit"
                shortcut: "Alt+F4"
                onTriggered: Qt.quit()
            }
        }

        Menu {
            title: "✂️ &Edit"
            MenuItem {
                text: "↩ &Undo"
                shortcut: "Ctrl+Z"
                enabled: videoEditor.canUndo
                icon.source: "qrc:/icons/undo.svg"
                onTriggered: videoEditor.undo()
            }
            MenuItem {
                text: "↪ &Redo"
                shortcut: "Ctrl+Shift+Z"
                enabled: videoEditor.canRedo
                icon.source: "qrc:/icons/redo.svg"
                onTriggered: videoEditor.redo()
            }
            MenuSeparator {}
            MenuItem {
                text: "✂️ &Cut"
                shortcut: "Ctrl+X"
                onTriggered: timelineView.cutSelection()
            }
            MenuItem {
                text: "📋 &Copy"
                shortcut: "Ctrl+C"
                onTriggered: timelineView.copySelection()
            }
            MenuItem {
                text: "📋 &Paste"
                shortcut: "Ctrl+V"
                onTriggered: timelineView.pasteClipboard()
            }
            MenuSeparator {}
            MenuItem {
                text: "🔪 Split at Playhead"
                shortcut: "S"
                icon.source: "qrc:/icons/split.svg"
                onTriggered: timelineView.splitAtPlayhead()
            }
            MenuItem {
                text: "🗑 Delete"
                shortcut: "Del"
                onTriggered: timelineView.deleteSelection()
            }
            MenuItem {
                text: "↪ Ripple Delete"
                shortcut: "Shift+Del"
                onTriggered: timelineView.rippleDeleteSelection()
            }
        }

        Menu {
            title: "🎥 &Timeline"
            MenuItem {
                text: "➕ Add Video Track"
                shortcut: "Ctrl+Shift+V"
                onTriggered: videoEditor.timeline?.addVideoTrack()
            }
            MenuItem {
                text: "➕ Add Audio Track"
                shortcut: "Ctrl+Shift+A"
                onTriggered: videoEditor.timeline?.addAudioTrack()
            }
            MenuSeparator {}
            MenuItem {
                text: "🔍 Zoom In"
                shortcut: "Ctrl++"
                onTriggered: zoomLevel = Math.min(4.0, zoomLevel + 0.2)
            }
            MenuItem {
                text: "🔍 Zoom Out"
                shortcut: "Ctrl+-"
                onTriggered: zoomLevel = Math.max(0.2, zoomLevel - 0.2)
            }
            MenuItem {
                text: "🔄 Fit to Timeline"
                shortcut: "Ctrl+0"
                onTriggered: fitTimelineToView()
            }
        }

        Menu {
            title: "🎨 &Effects"
            MenuItem {
                text: "🌈 Color Correction"
                onTriggered: workspaceMode = "color"
            }
            MenuItem {
                text: "🔊 Audio Effects"
                onTriggered: workspaceMode = "audio"
            }
            MenuItem {
                text: "✨ Transitions"
                onTriggered: transitionsPanel.visible = !transitionsPanel.visible
            }
            MenuItem {
                text: "📊 Keyframe Editor"
                onTriggered: keyframePanel.visible = !keyframePanel.visible
            }
        }

        Menu {
            title: "👁 &View"
            MenuItem {
                text: "🖥 Full Screen"
                shortcut: "F11"
                onTriggered: root.visibility = root.visibility === ApplicationWindow.FullScreen ?
                ApplicationWindow.Windowed : ApplicationWindow.FullScreen
            }
            MenuItem {
                text: "🔍 Show Waveforms"
                checkable: true
                checked: true
            }
            MenuItem {
                text: "📊 Show Keyframes"
                checkable: true
                checked: true
            }
            MenuSeparator {}
            MenuItem {
                text: "⬅ Left Panel"
                checkable: true
                checked: true
                onCheckedChanged: leftPanel.visible = checked
            }
            MenuItem {
                text: "➡ Right Panel"
                checkable: true
                checked: true
                onCheckedChanged: rightPanel.visible = checked
            }
        }

        Menu {
            title: "❓ &Help"
            MenuItem {
                text: "📖 User Guide"
                onTriggered: Qt.openUrlExternally("https://docs.aegis.com")
            }
            MenuItem {
                text: "🎓 Tutorials"
                onTriggered: tutorialsDialog.open()
            }
            MenuSeparator {}
            MenuItem {
                text: "ℹ About Aegis Pro"
                onTriggered: aboutDialog.open()
            }
        }
    }

    // Toolbar principale
    ToolBar {
        id: mainToolbar
        width: parent.width
        height: 48
        Material.elevation: 4

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 4

            ToolButton {
                text: "📁"
                ToolTip.text: "New Project"
                ToolTip.visible: hovered
                onClicked: newProjectDialog.open()
            }
            ToolButton {
                text: "📂"
                ToolTip.text: "Open"
                ToolTip.visible: hovered
                onClicked: openProjectDialog.open()
            }
            ToolButton {
                text: "💾"
                ToolTip.text: "Save"
                ToolTip.visible: hovered
                onClicked: videoEditor.saveProject()
            }
            Rectangle {
                width: 1
                height: 24
                color: Material.divider
            }
            ToolButton {
                text: "↩"
                enabled: videoEditor.canUndo
                ToolTip.text: "Undo"
                ToolTip.visible: hovered
                onClicked: videoEditor.undo()
            }
            ToolButton {
                text: "↪"
                enabled: videoEditor.canRedo
                ToolTip.text: "Redo"
                ToolTip.visible: hovered
                onClicked: videoEditor.redo()
            }
            Rectangle {
                width: 1
                height: 24
                color: Material.divider
            }
            ToolButton {
                text: "🔪"
                ToolTip.text: "Split (S)"
                ToolTip.visible: hovered
                onClicked: timelineView.splitAtPlayhead()
            }
            ToolButton {
                text: "🔗"
                ToolTip.text: "Ripple Mode"
                ToolTip.visible: hovered
                checkable: true
                checked: rippleEnabled
                onCheckedChanged: rippleEnabled = checked
            }
            Rectangle {
                width: 1
                height: 24
                color: Material.divider
            }
            ToolButton {
                text: "🔍-"
                ToolTip.text: "Zoom Out"
                ToolTip.visible: hovered
                onClicked: zoomLevel = Math.max(0.2, zoomLevel - 0.2)
            }
            Slider {
                Layout.preferredWidth: 150
                from: 0.2
                to: 4.0
                value: zoomLevel
                onValueChanged: zoomLevel = value
                ToolTip.visible: pressed
                ToolTip.text: value.toFixed(1) + "x"
            }
            ToolButton {
                text: "🔍+"
                ToolTip.text: "Zoom In"
                ToolTip.visible: hovered
                onClicked: zoomLevel = Math.min(4.0, zoomLevel + 0.2)
            }
            Item { Layout.fillWidth: true }
            Label {
                text: videoEditor.resolution.width + "x" + videoEditor.resolution.height + "  " +
                videoEditor.fps + "fps"
                color: Material.accent
                font.bold: true
                visible: videoEditor.hasProject
            }
        }
    }

    // Main Layout con splitter professionali
    SplitView {
        anchors.top: mainToolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        orientation: Qt.Horizontal

        // Left Panel - Media Browser
        Pane {
            id: leftPanel
            SplitView.preferredWidth: 280
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: 500
            padding: 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Tab bar per media browser
                TabBar {
                    id: mediaTabBar
                    Layout.fillWidth: true
                    Material.background: "transparent"

                    TabButton { text: "📁 Media" }
                    TabButton { text: "🎨 Effects" }
                    TabButton { text: "✨ Transitions" }
                    TabButton { text: "🔊 Audio" }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: mediaTabBar.currentIndex

                    // Media Panel
                    ColumnLayout {
                        spacing: 8
                        Layout.margins: 8

                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: mediaSearchField
                                Layout.fillWidth: true
                                placeholderText: "🔍 Search media..."
                                selectByMouse: true
                            }
                            Button {
                                text: "📥"
                                ToolTip.text: "Import Media"
                                ToolTip.visible: hovered
                                onClicked: importDialog.open()
                            }
                        }

                        ListView {
                            id: mediaListView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: projectMediaModel
                            spacing: 4

                            delegate: Rectangle {
                                width: mediaListView.width
                                height: 70
                                color: mediaListView.currentIndex === index ?
                                Material.accent : "transparent"
                                radius: 4

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 8

                                    Rectangle {
                                        Layout.preferredWidth: 50
                                        Layout.preferredHeight: 50
                                        color: modelData.thumbnailColor || Material.color(Material.Grey, Material.Shade800)
                                        radius: 4

                                        Text {
                                            anchors.centerIn: parent
                                            text: modelData.type === "video" ? "🎬" :
                                            modelData.type === "audio" ? "🎵" : "🖼"
                                            font.pixelSize: 24
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Label {
                                            text: modelData.name
                                            font.bold: true
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Label {
                                            text: modelData.duration + "  •  " + modelData.resolution
                                            color: Material.secondaryText
                                            font.pixelSize: 11
                                        }
                                    }

                                    ToolButton {
                                        text: "⋮"
                                        onClicked: mediaContextMenu.open()
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: mediaListView.currentIndex = index
                                    onDoubleClicked: timelineView.addClipToCurrentTrack(modelData.source)
                                }
                            }
                        }
                    }

                    // Effects Panel
                    GridView {
                        Layout.margins: 8
                        model: effectModel
                        cellWidth: 100
                        cellHeight: 100
                        clip: true

                        delegate: Rectangle {
                            width: 92
                            height: 92
                            color: Material.color(Material.Grey, Material.Shade800)
                            radius: 8

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 4

                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: modelData.icon
                                    font.pixelSize: 32
                                }
                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: modelData.name
                                    font.pixelSize: 11
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: timelineView.addEffectToSelectedClip(modelData.id)
                            }
                        }
                    }

                    // Transitions Panel
                    GridView {
                        Layout.margins: 8
                        model: transitionModel
                        cellWidth: 100
                        cellHeight: 80
                        clip: true

                        delegate: Rectangle {
                            width: 92
                            height: 72
                            color: Material.color(Material.Grey, Material.Shade800)
                            radius: 8

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2

                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: modelData.icon
                                    font.pixelSize: 24
                                }
                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: modelData.name
                                    font.pixelSize: 10
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: timelineView.addTransition(modelData.id)
                            }
                        }
                    }

                    // Audio Effects Panel
                    ListView {
                        Layout.margins: 8
                        model: audioEffectModel
                        spacing: 4
                        clip: true

                        delegate: Rectangle {
                            width: parent.width
                            height: 50
                            color: Material.color(Material.Grey, Material.Shade800)
                            radius: 4

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8

                                Text {
                                    text: modelData.icon
                                    font.pixelSize: 20
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                }
                                Switch {
                                    onToggled: timelineView.toggleAudioEffect(modelData.id, checked)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Center Area - Preview e Timeline
        ColumnLayout {
            SplitView.fillWidth: true
            spacing: 0

            // Preview con controlli avanzati
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.height * 0.3
                color: Material.background
                border.color: Material.divider

                // Video output con overlay
                Rectangle {
                    anchors.fill: parent
                    color: "black"

                    // Video display placeholder
                    Rectangle {
                        anchors.fill: parent
                        color: "#0a0a0a"

                        // Center play icon when no video
                        Text {
                            anchors.centerIn: parent
                            text: "🎬"
                            font.pixelSize: 64
                            opacity: 0.3
                            visible: !videoEditor.isPlaying
                        }
                    }

                    // Overlay controls
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 40
                        color: Qt.rgba(0, 0, 0, 0.7)

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            Label {
                                text: timelineView.timecodeString
                                color: "white"
                                font.family: "Courier"
                                font.bold: true
                            }

                            Slider {
                                Layout.fillWidth: true
                                from: 0
                                to: timelineView.duration
                                value: timelineView.playhead
                                onMoved: videoEditor.seek(value)
                            }

                            Label {
                                text: formatDuration(timelineView.duration)
                                color: "white"
                                font.family: "Courier"
                            }
                        }
                    }

                    // Timecode badge
                    Rectangle {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.margins: 16
                        width: 120
                        height: 40
                        color: Qt.rgba(0, 0, 0, 0.7)
                        radius: 20

                        Label {
                            anchors.centerIn: parent
                            text: timelineView.timecodeString
                            color: "white"
                            font.family: "Courier"
                            font.pixelSize: 16
                            font.bold: true
                        }
                    }

                    // Resolution badge
                    Rectangle {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 16
                        height: 30
                        width: resolutionLabel.width + 24
                        color: Material.accent
                        radius: 15
                        visible: videoEditor.hasProject

                        Label {
                            id: resolutionLabel
                            anchors.centerIn: parent
                            text: videoEditor.resolution.width + "x" + videoEditor.resolution.height
                            color: "white"
                            font.bold: true
                        }
                    }
                }
            }

            // Transport controls avanzati
            Rectangle {
                Layout.fillWidth: true
                height: 60
                color: Material.background
                border.color: Material.divider

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    // Playback controls
                    RowLayout {
                        spacing: 4

                        ToolButton {
                            text: "⏮"
                            font.pixelSize: 20
                            onClicked: videoEditor.seek(0)
                        }
                        ToolButton {
                            text: "⏪"
                            font.pixelSize: 20
                            onClicked: videoEditor.frameStep(-10)
                        }
                        ToolButton {
                            text: videoEditor.isPlaying ? "⏸" : "▶"
                            font.pixelSize: 24
                            highlighted: videoEditor.isPlaying
                            onClicked: videoEditor.togglePlayPause()
                        }
                        ToolButton {
                            text: "⏩"
                            font.pixelSize: 20
                            onClicked: videoEditor.frameStep(10)
                        }
                        ToolButton {
                            text: "⏭"
                            font.pixelSize: 20
                            onClicked: videoEditor.seek(timelineView.duration)
                        }
                    }

                    Rectangle {
                        width: 1
                        height: 30
                        color: Material.divider
                    }

                    // Jog/Shuttle
                    RowLayout {
                        Dial {
                            id: jogDial
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            live: true
                            onMoved: videoEditor.frameStep(Math.round(value * 100))
                        }
                        Label {
                            text: "Jog"
                            color: Material.secondaryText
                        }
                    }

                    Rectangle {
                        width: 1
                        height: 30
                        color: Material.divider
                    }

                    // Time display
                    Label {
                        text: timelineView.timecodeString + " / " + formatDuration(timelineView.duration)
                        font.family: "Courier"
                        font.pixelSize: 16
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    // Volume control
                    RowLayout {
                        ToolButton {
                            text: "🔊"
                            font.pixelSize: 18
                        }
                        Slider {
                            Layout.preferredWidth: 100
                            from: 0
                            to: 200
                            value: 100
                        }
                    }

                    // Timeline zoom
                    RowLayout {
                        ToolButton {
                            text: "−"
                            font.pixelSize: 18
                            onClicked: zoomLevel = Math.max(0.2, zoomLevel - 0.2)
                        }
                        Label {
                            text: Math.round(zoomLevel * 100) + "%"
                            font.bold: true
                        }
                        ToolButton {
                            text: "+"
                            font.pixelSize: 18
                            onClicked: zoomLevel = Math.min(4.0, zoomLevel + 0.2)
                        }
                    }
                }
            }

            // Timeline professionale
            TimelineView {
                id: timelineView
                Layout.fillWidth: true
                Layout.fillHeight: true
                zoom: zoomLevel
                snapEnabled: root.snapEnabled
                rippleEnabled: root.rippleEnabled
            }
        }

        // Right Panel - Properties & Effects
        Pane {
            id: rightPanel
            SplitView.preferredWidth: 320
            SplitView.minimumWidth: 250
            SplitView.maximumWidth: 600
            padding: 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Properties tabs
                TabBar {
                    id: propertiesTabBar
                    Layout.fillWidth: true
                    Material.background: "transparent"

                    TabButton { text: "📊 Properties" }
                    TabButton { text: "🎨 Effects" }
                    TabButton { text: "🔧 Keyframes" }
                    TabButton { text: "ℹ Metadata" }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: propertiesTabBar.currentIndex

                    // Properties Panel
                    ScrollView {
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: parent.width
                            spacing: 16
                            Layout.margins: 16

                            GroupBox {
                                Layout.fillWidth: true
                                title: "📹 Clip Properties"
                                visible: timelineView.selectedClip

                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 8

                                    // Use the externally defined PropertyItem component.
                                    PropertyItem {
                                        label: "Name"
                                        value: timelineView.selectedClip?.name || ""
                                        onValueEdited: (newValue) => { if (timelineView.selectedClip) timelineView.selectedClip.name = newValue }
                                    }

                                    PropertyItem {
                                        label: "Position"
                                        value: formatDuration(timelineView.selectedClip?.position || 0)
                                        readOnly: true
                                    }

                                    PropertyItem {
                                        label: "Duration"
                                        value: formatDuration(timelineView.selectedClip?.duration || 0)
                                        onValueEdited: (newValue) => { if (timelineView.selectedClip) timelineView.selectedClip.duration = parseDuration(newValue) }
                                    }

                                    PropertySlider {
                                        label: "Speed"
                                        from: 10
                                        to: 400
                                        value: timelineView.selectedClip?.speed * 100 || 100
                                        suffix: "%"
                                        onEditingFinished: if (timelineView.selectedClip) timelineView.selectedClip.speed = value / 100
                                    }

                                    PropertySlider {
                                        label: "Volume"
                                        from: 0
                                        to: 200
                                        value: timelineView.selectedClip?.volume * 100 || 100
                                        suffix: "%"
                                        onEditingFinished: if (timelineView.selectedClip) timelineView.selectedClip.volume = value / 100
                                    }

                                    PropertySlider {
                                        label: "Opacity"
                                        from: 0
                                        to: 100
                                        value: timelineView.selectedClip?.opacity * 100 || 100
                                        suffix: "%"
                                        onEditingFinished: if (timelineView.selectedClip) timelineView.selectedClip.opacity = value / 100
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Button {
                                            text: "Reset"
                                            flat: true
                                            Layout.fillWidth: true
                                        }
                                        Button {
                                            text: "Apply"
                                            highlighted: true
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }

                            GroupBox {
                                Layout.fillWidth: true
                                title: "🎚 Transform"

                                GridLayout {
                                    columns: 2
                                    columnSpacing: 16
                                    rowSpacing: 8
                                    anchors.fill: parent

                                    Label { text: "X:" }
                                    SpinBox {
                                        from: -1000
                                        to: 1000
                                        value: timelineView.selectedClip?.transform?.x || 0
                                    }

                                    Label { text: "Y:" }
                                    SpinBox {
                                        from: -1000
                                        to: 1000
                                        value: timelineView.selectedClip?.transform?.y || 0
                                    }

                                    Label { text: "Scale:" }
                                    SpinBox {
                                        from: 0
                                        to: 500
                                        value: timelineView.selectedClip?.transform?.scale * 100 || 100
                                    }

                                    Label { text: "Rotation:" }
                                    SpinBox {
                                        from: 0
                                        to: 360
                                        value: timelineView.selectedClip?.transform?.rotation || 0
                                    }
                                }
                            }
                        }
                    }

                    // Effects List
                    ListView {
                        Layout.margins: 8
                        model: timelineView.selectedClip?.effects || []
                        spacing: 4
                        clip: true

                        delegate: Rectangle {
                            width: parent.width
                            height: 60
                            color: Material.color(Material.Grey, Material.Shade800)
                            radius: 4

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        text: modelData.name
                                        font.bold: true
                                    }
                                    Label {
                                        text: modelData.type
                                        color: Material.secondaryText
                                        font.pixelSize: 11
                                    }
                                }

                                Switch {
                                    checked: modelData.enabled
                                    onToggled: modelData.enabled = checked
                                }

                                ToolButton {
                                    text: "⚙"
                                    onClicked: effectSettingsDialog.open()
                                }

                                ToolButton {
                                    text: "✕"
                                    onClicked: timelineView.removeEffect(modelData.id)
                                }
                            }
                        }

                        footer: Button {
                            text: "+ Add Effect"
                            flat: true
                            Layout.fillWidth: true
                            onClicked: effectsMenu.popup()
                        }
                    }

                    // Keyframe Editor
                    ColumnLayout {
                        spacing: 8
                        Layout.margins: 8

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                text: "Keyframes"
                                font.bold: true
                            }

                            Item { Layout.fillWidth: true }

                            ToolButton {
                                text: "➕"
                                onClicked: timelineView.addKeyframe()
                            }
                            ToolButton {
                                text: "🗑"
                                onClicked: timelineView.removeAllKeyframes()
                            }
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: ["Position", "Scale", "Rotation", "Opacity"]
                            spacing: 2

                            delegate: CheckBox {
                                text: modelData
                                checked: true
                            }
                        }

                        Button {
                            text: "Open Keyframe Editor"
                            highlighted: true
                            Layout.fillWidth: true
                            onClicked: keyframePanel.visible = !keyframePanel.visible
                        }
                    }

                    // Metadata
                    ColumnLayout {
                        spacing: 8
                        Layout.margins: 8

                        GridLayout {
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8
                            Layout.fillWidth: true

                            Label { text: "File:"; font.bold: true }
                            Label { text: timelineView.selectedClip?.file || "N/A"; elide: Text.ElideRight; Layout.fillWidth: true }

                            Label { text: "Type:"; font.bold: true }
                            Label { text: timelineView.selectedClip?.type || "N/A" }

                            Label { text: "Codec:"; font.bold: true }
                            Label { text: timelineView.selectedClip?.codec || "N/A" }

                            Label { text: "Bitrate:"; font.bold: true }
                            Label { text: timelineView.selectedClip?.bitrate || "N/A" }

                            Label { text: "FPS:"; font.bold: true }
                            Label { text: timelineView.selectedClip?.fps || "N/A" }

                            Label { text: "Aspect:"; font.bold: true }
                            Label { text: timelineView.selectedClip?.aspect || "N/A" }
                        }

                        Button {
                            text: "Properties"
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }

    // Status bar professionale
    StatusBar {
        id: statusBar
        height: 32

        RowLayout {
            anchors.fill: parent
            spacing: 16

            Label {
                text: "●"
                color: videoEditor.isPlaying ? Material.Green : Material.Red
                font.pixelSize: 16
            }

            Label {
                text: videoEditor.isPlaying ? "Playing" : "Stopped"
                font.bold: true
            }

            Rectangle {
                width: 1
                height: 16
                color: Material.divider
            }

            Label {
                text: "📊 " + (timelineView.selectedClip ? "Clip selected" : "No selection")
                color: timelineView.selectedClip ? Material.accent : Material.secondaryText
            }

            Rectangle {
                width: 1
                height: 16
                color: Material.divider
            }

            Label {
                text: "🧲 Snap: " + (snapEnabled ? "ON" : "OFF")
                color: snapEnabled ? Material.Green : Material.secondaryText
            }

            Rectangle {
                width: 1
                height: 16
                color: Material.divider
            }

            Label {
                text: "📊 " + (timelineView.videoTracks || 0) + "V / " + (timelineView.audioTracks || 0) + "A"
            }

            Rectangle {
                width: 1
                height: 16
                color: Material.divider
            }

            Label {
                text: "💾 " + (videoEditor.modified ? "Modified" : "Saved")
                color: videoEditor.modified ? Material.Yellow : Material.Green
            }

            Item { Layout.fillWidth: true }

            Label {
                text: "CPU: 23% • RAM: 2.4GB/8GB • GPU: 45%"
                color: Material.secondaryText
                font.pixelSize: 11
            }
        }
    }

    // Dialogs professionali
    FileDialog {
        id: importDialog
        title: "Import Media"
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            "All Media (*.mp4 *.mov *.avi *.mkv *.webm *.mp3 *.wav *.flac *.jpg *.png)",
            "Video Files (*.mp4 *.mov *.avi *.mkv *.webm)",
            "Audio Files (*.mp3 *.wav *.flac *.aac)",
            "Image Files (*.jpg *.jpeg *.png *.bmp)"
        ]
        onAccepted: {
            for (var i = 0; i < selectedFiles.length; i++) {
                videoEditor.importMedia(selectedFiles[i])
            }
        }
    }

    Dialog {
        id: newProjectDialog
        title: "New Project"
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 500

        ColumnLayout {
            spacing: 16
            Layout.preferredWidth: parent.width

            Label {
                text: "🎬 Create New Project"
                font.pixelSize: 20
                font.bold: true
            }

            GridLayout {
                columns: 2
                columnSpacing: 16
                rowSpacing: 12
                Layout.fillWidth: true

                Label { text: "Project Name:" }
                TextField {
                    id: projectNameField
                    text: "My Movie"
                    Layout.fillWidth: true
                    selectByMouse: true
                }

                Label { text: "Resolution:" }
                ComboBox {
                    id: resolutionCombo
                    model: [
                        "1920x1080 (1080p)",
                        "3840x2160 (4K UHD)",
                        "1280x720 (720p)",
                        "2560x1440 (2K)",
                        "7680x4320 (8K)"
                    ]
                    currentIndex: 0
                    Layout.fillWidth: true
                }

                Label { text: "Frame Rate:" }
                ComboBox {
                    id: fpsCombo
                    model: ["24 fps", "25 fps", "30 fps", "50 fps", "60 fps", "120 fps"]
                    currentIndex: 2
                    Layout.fillWidth: true
                }

                Label { text: "Audio Sample Rate:" }
                ComboBox {
                    id: sampleRateCombo
                    model: ["44100 Hz", "48000 Hz", "96000 Hz", "192000 Hz"]
                    currentIndex: 1
                    Layout.fillWidth: true
                }

                Label { text: "Audio Channels:" }
                ComboBox {
                    model: ["Stereo", "5.1 Surround", "7.1 Surround"]
                    currentIndex: 0
                    Layout.fillWidth: true
                }
            }
        }

        onAccepted: {
            var res = resolutionCombo.currentText.split("x")
            var width = parseInt(res[0])
            var height = parseInt(res[1].split(" ")[0])
            var fps = parseInt(fpsCombo.currentText)
            var sr = parseInt(sampleRateCombo.currentText)

            var profile = {
                width: width,
                height: height,
                fps: fps,
                sampleRate: sr
            }

            videoEditor.newProject(projectNameField.text, profile)
        }
    }

    Dialog {
        id: aboutDialog
        title: "About Aegis Media Suite"
        standardButtons: Dialog.Ok
        width: 400

        ColumnLayout {
            spacing: 16
            Layout.preferredWidth: parent.width

            Label {
                text: "🎬 Aegis Video Editor"
                font.pixelSize: 28
                font.bold: true
                color: Material.accent
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: "Version 1.0.0"
                font.pixelSize: 14
                color: Material.secondaryText
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: "Aegis Video Editor Ultimate\n\n\n© 2025 Aegis Technologies"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    // Models per effetti
    ListModel {
        id: effectModel
        ListElement { name: "Color Correction"; icon: "🎨"; id: "color" }
        ListElement { name: "Brightness"; icon: "☀️"; id: "brightness" }
        ListElement { name: "Blur"; icon: "💫"; id: "blur" }
        ListElement { name: "Sharpen"; icon: "✨"; id: "sharpen" }
        ListElement { name: "Vignette"; icon: "⬤"; id: "vignette" }
        ListElement { name: "Chroma Key"; icon: "🟢"; id: "chromakey" }
    }

    ListModel {
        id: transitionModel
        ListElement { name: "Crossfade"; icon: "🔄"; id: "crossfade" }
        ListElement { name: "Fade"; icon: "🌅"; id: "fade" }
        ListElement { name: "Slide"; icon: "⏩"; id: "slide" }
        ListElement { name: "Wipe"; icon: "🧹"; id: "wipe" }
    }

    ListModel {
        id: audioEffectModel
        ListElement { name: "Equalizer"; icon: "🎚️"; id: "eq" }
        ListElement { name: "Compressor"; icon: "📊"; id: "compressor" }
        ListElement { name: "Reverb"; icon: "🏛️"; id: "reverb" }
        ListElement { name: "Noise Reduction"; icon: "🔇"; id: "noise" }
    }

    // Helper functions
    function formatDuration(frames) {
        var fps = videoEditor.fps || 30
        var seconds = Math.floor(frames / fps)
        var hours = Math.floor(seconds / 3600)
        var minutes = Math.floor((seconds % 3600) / 60)
        var secs = seconds % 60
        var frms = frames % fps

        if (hours > 0) {
            return hours.toString().padStart(2, '0') + ':' +
            minutes.toString().padStart(2, '0') + ':' +
            secs.toString().padStart(2, '0') + '.' +
            frms.toString().padStart(2, '0')
        } else {
            return minutes.toString().padStart(2, '0') + ':' +
            secs.toString().padStart(2, '0') + ':' +
            frms.toString().padStart(2, '0')
        }
    }

    function fitTimelineToView() {
        if (timelineView.duration > 0) {
            zoomLevel = timelineView.viewportWidth / (timelineView.duration * 4)
        }
    }

    // These components are now defined externally, for example in a separate
    // file "PropertyComponents.qml". This improves reusability and keeps the main file clean.
    // The import statement would be added at the top:
    // import "PropertyComponents.qml" as Components
    // And then used as: Components.PropertyItem { ... }
}
