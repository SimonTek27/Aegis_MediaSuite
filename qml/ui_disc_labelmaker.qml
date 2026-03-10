// ui_disc_labelmaker.qml
// Aegis Disc Label Designer - Professional optical media artwork creation
// Design: Adobe Illustrator-inspired dark interface with precision tools

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Shapes
import Qt.labs.platform as Platform
import QtQuick.Effects

ApplicationWindow {
    id: labelWindow
    visible: true
    width: 1400
    height: 900
    minimumWidth: 1100
    minimumHeight: 700
    title: qsTr("Aegis Label Designer") + (LabelMaker.modified ? " *" : "")
    color: theme.background

    // Theme - Coordinated with Disc Burner but more vibrant for design work
    property var theme: {
        "background": "#1e1e1e",
        "panel": "#252526",
        "panelDark": "#1a1a1a",
        "panelLight": "#2d2d30",
        "border": "#3e3e42",
        "accent": "#007acc",
        "accentHighlight": "#1ba1e2",
        "textPrimary": "#ffffff",
        "textSecondary": "#cccccc",
        "textDisabled": "#656565",
        "canvasBg": "#333333",
        "grid": "#444444",
        "guide": "#ff6b6b",
        "selection": "#007acc",
        "ruler": "#2d2d30",
        "success": "#4ec9b0",
        "warning": "#ce9178"
    }

    // Canvas state
    property real zoomLevel: 1.0
    property real minZoom: 0.1
    property real maxZoom: 5.0
    property point canvasOffset: Qt.point(0, 0)
    property string selectedElementId: ""
    property int currentTool: 0  // 0=Select, 1=Text, 2=Image, 3=Shape, 4=TrackList
    property bool showGuides: true
    property bool snapToGrid: true
    property real gridSizeMm: 5.0

    // Template dimensions (mm)
    property var currentGeometry: LabelMaker.geometry()
    property real canvasScale: 3.78  // pixels per mm at 100% zoom (96dpi / 25.4)

    // Element creation defaults
    property var defaultTextProps: {
        "font": "Arial",
        "size": 12,
        "color": "#ffffff",
        "bold": false,
        "align": Text.AlignCenter
    }

    MenuBar {
        Menu {
            title: qsTr("File")
            Action {
                text: qsTr("New Label")
                shortcut: "Ctrl+N"
                onTriggered: newProject()
            }
            Action {
                text: qsTr("Open...")
                shortcut: "Ctrl+O"
                onTriggered: openProject()
            }
            Action {
                text: qsTr("Save")
                shortcut: "Ctrl+S"
                enabled: LabelMaker.modified
                onTriggered: LabelMaker.saveProject(LabelMaker.currentProjectPath)
            }
            Action {
                text: qsTr("Save As...")
                shortcut: "Ctrl+Shift+S"
                onTriggered: saveAsDialog.open()
            }
            MenuSeparator {}
            Menu {
                title: qsTr("Export")
                Action {
                    text: qsTr("Export as PDF...")
                    onTriggered: exportPDF()
                }
                Action {
                    text: qsTr("Export as Image...")
                    onTriggered: exportImageDialog.open()
                }
            }
            MenuSeparator {}
            Action {
                text: qsTr("Print...")
                shortcut: "Ctrl+P"
                onTriggered: LabelMaker.print()
            }
            Action {
                text: qsTr("Print Preview")
                onTriggered: LabelMaker.printPreview()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Exit")
                onTriggered: labelWindow.close()
            }
        }

        Menu {
            title: qsTr("Edit")
            Action {
                text: qsTr("Undo")
                shortcut: "Ctrl+Z"
                enabled: false  // Connect to undo stack
            }
            Action {
                text: qsTr("Cut")
                shortcut: "Ctrl+X"
                enabled: selectedElementId !== ""
            }
            Action {
                text: qsTr("Copy")
                shortcut: "Ctrl+C"
                enabled: selectedElementId !== ""
            }
            Action {
                text: qsTr("Paste")
                shortcut: "Ctrl+V"
            }
            MenuSeparator {}
            Action {
                text: qsTr("Delete")
                shortcut: "Delete"
                enabled: selectedElementId !== ""
                onTriggered: deleteSelected()
            }
            Action {
                text: qsTr("Select All")
                shortcut: "Ctrl+A"
            }
            MenuSeparator {}
            Action {
                text: qsTr("Preferences...")
                onTriggered: prefsDialog.open()
            }
        }

        Menu {
            title: qsTr("View")
            Action {
                text: qsTr("Zoom In")
                shortcut: "Ctrl++"
                onTriggered: zoomIn()
            }
            Action {
                text: qsTr("Zoom Out")
                shortcut: "Ctrl+-"
                onTriggered: zoomOut()
            }
            Action {
                text: qsTr("Fit to Window")
                shortcut: "Ctrl+0"
                onTriggered: fitToWindow()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Show Guides")
                checkable: true
                checked: showGuides
                onTriggered: showGuides = !showGuides
            }
            Action {
                text: qsTr("Snap to Grid")
                checkable: true
                checked: snapToGrid
                onTriggered: snapToGrid = !snapToGrid
            }
            Action {
                text: qsTr("Grid Settings...")
                onTriggered: gridSettingsDialog.open()
            }
        }

        Menu {
            title: qsTr("Templates")
            Action {
                text: qsTr("Standard CD (120mm)")
                onTriggered: setTemplate(0)
            }
            Action {
                text: qsTr("Mini CD (80mm)")
                onTriggered: setTemplate(1)
            }
            MenuSeparator {}
            Action {
                text: qsTr("Jewel Case...")
            }
            Action {
                text: qsTr("DVD Case...")
            }
            Action {
                text: qsTr("Blu-ray Case...")
            }
        }

        Menu {
            title: qsTr("Data")
            Action {
                text: qsTr("Fill from Library Album...")
                onTriggered: fillFromLibraryDialog.open()
            }
            Action {
                text: qsTr("Fill from Current Disc")
                enabled: Disc && Disc.discPresent
                onTriggered: LabelMaker.autoFillFromDisc(Disc)
            }
            Action {
                text: qsTr("Fill from Last Burn Job")
                enabled: false
            }
            MenuSeparator {}
            Action {
                text: qsTr("Import Track List...")
            }
        }

        Menu {
            title: qsTr("Help")
            Action {
                text: qsTr("Calibration Sheet...")
                onTriggered: LabelMaker.calibratePrinter()
            }
        }
    }

    // Main Toolbar
    ToolBar {
        id: mainToolbar
        width: parent.width
        height: 48
        background: Rectangle { color: theme.panelDark }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 4

            // Tool Group
            Rectangle {
                Layout.preferredWidth: toolGroup.width + 8
                Layout.fillHeight: true
                color: theme.panel
                radius: 4
                border { color: theme.border; width: 1 }

                Row {
                    id: toolGroup
                    anchors.centerIn: parent
                    spacing: 2

                    ToolButton {
                        text: "↖"
                        font.pixelSize: 18
                        checked: currentTool === 0
                        onClicked: currentTool = 0
                        ToolTip.text: qsTr("Select Tool (V)")
                    }
                    ToolButton {
                        text: "T"
                        font.bold: true
                        font.pixelSize: 16
                        checked: currentTool === 1
                        onClicked: currentTool = 1
                        ToolTip.text: qsTr("Text Tool (T)")
                    }
                    ToolButton {
                        text: "🖼"
                        font.pixelSize: 16
                        checked: currentTool === 2
                        onClicked: currentTool = 2
                        ToolTip.text: qsTr("Image Tool (I)")
                    }
                    ToolButton {
                        text: "▢"
                        font.pixelSize: 16
                        checked: currentTool === 3
                        onClicked: currentTool = 3
                        ToolTip.text: qsTr("Shape Tool (S)")
                    }
                    ToolButton {
                        text: "🎵"
                        font.pixelSize: 16
                        checked: currentTool === 4
                        onClicked: currentTool = 4
                        ToolTip.text: qsTr("Track List Tool (L)")
                    }
                }
            }

            ToolSeparator {}

            // Alignment Tools
            Rectangle {
                Layout.preferredWidth: alignGroup.width + 8
                Layout.fillHeight: true
                color: theme.panel
                radius: 4
                border { color: theme.border; width: 1 }
                enabled: selectedElementId !== ""

                Row {
                    id: alignGroup
                    anchors.centerIn: parent
                    spacing: 2

                    ToolButton {
                        text: "⬆"
                        onClicked: LabelMaker.centerElement(selectedElementId, false, true)
                        ToolTip.text: qsTr("Center Vertically")
                    }
                    ToolButton {
                        text: "⬅"
                        onClicked: LabelMaker.centerElement(selectedElementId, true, false)
                        ToolTip.text: qsTr("Center Horizontally")
                    }
                    ToolSeparator { height: 24; anchors.verticalCenter: parent.verticalCenter }
                    ToolButton {
                        text: "↻"
                        onClicked: rotateElement(90)
                        ToolTip.text: qsTr("Rotate 90°")
                    }
                    ToolButton {
                        text: "↺"
                        onClicked: rotateElement(-90)
                        ToolTip.text: qsTr("Rotate -90°")
                    }
                }
            }

            ToolSeparator {}

            // Zoom Controls
            Rectangle {
                Layout.preferredWidth: zoomGroup.width + 16
                Layout.fillHeight: true
                color: "transparent"

                Row {
                    id: zoomGroup
                    anchors.centerIn: parent
                    spacing: 8

                    ToolButton {
                        text: "−"
                        onClicked: zoomOut()
                    }
                    Slider {
                        from: minZoom
                        to: maxZoom
                        value: zoomLevel
                        onMoved: zoomLevel = value
                        width: 100
                    }
                    ToolButton {
                        text: "+"
                        onClicked: zoomIn()
                    }
                    Label {
                        text: Math.round(zoomLevel * 100) + "%"
                        color: theme.textSecondary
                        width: 50
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Quick Actions
            RoundButton {
                text: "🖨"
                font.pixelSize: 16
                radius: 4
                onClicked: LabelMaker.print()
                ToolTip.text: qsTr("Print Label")
            }
        }
    }

    // Main Content
    RowLayout {
        anchors.top: mainToolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0

        // LEFT PANEL - Templates & Elements
        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            color: theme.panel
            border { color: theme.border; width: 1 }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Template Selector
                Label {
                    text: qsTr("TEMPLATE")
                    color: theme.textSecondary
                    font.bold: true
                    font.pixelSize: 11
                    Layout.margins: 12
                    Layout.bottomMargin: 8
                }

                ComboBox {
                    id: templateCombo
                    Layout.fillWidth: true
                    Layout.margins: 12
                    Layout.topMargin: 0
                    model: LabelMaker.availableTemplates()
                    textRole: "name"
                    currentIndex: {
                        var templates = LabelMaker.availableTemplates()
                        for (var i = 0; i < templates.length; i++) {
                            if (templates[i].type === LabelMaker.currentTemplate) return i
                        }
                        return 0
                    }
                    onActivated: {
                        var templates = LabelMaker.availableTemplates()
                        LabelMaker.setCurrentTemplate(templates[currentIndex].type)
                        currentGeometry = LabelMaker.geometry()
                    }
                }

                // Template Info
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: infoCol.height + 24
                    color: theme.panelDark
                    border { color: theme.border; width: 1 }
                    Layout.margins: 12
                    Layout.topMargin: 0

                    Column {
                        id: infoCol
                        anchors.centerIn: parent
                        spacing: 4
                        width: parent.width - 24

                        Text {
                            text: qsTr("Size: %1 x %2 mm").arg(currentGeometry.size.width.toFixed(1))
                            .arg(currentGeometry.size.height.toFixed(1))
                            color: theme.textSecondary
                            font.pixelSize: 11
                            width: parent.width
                        }
                        Text {
                            text: currentGeometry.isDisc ? qsTr("Type: Optical Disc") : qsTr("Type: Case Insert")
                            color: theme.textSecondary
                            font.pixelSize: 11
                        }
                        Text {
                            visible: currentGeometry.isWraparound
                            text: qsTr("Spine: %1 mm").arg(currentGeometry.spineWidth)
                            color: theme.textSecondary
                            font.pixelSize: 11
                        }
                    }
                }

                // Elements List
                Label {
                    text: qsTr("LAYERS")
                    color: theme.textSecondary
                    font.bold: true
                    font.pixelSize: 11
                    Layout.margins: 12
                    Layout.bottomMargin: 8
                }

                ListView {
                    id: elementsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 12
                    Layout.topMargin: 0
                    clip: true
                    model: LabelMaker.elements()

                    delegate: Rectangle {
                        width: elementsList.width
                        height: 32
                        color: selectedElementId === modelData.id ? theme.accent :
                        (index % 2 === 0 ? theme.panelDark : "transparent")
                        radius: 2

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 8

                            Text {
                                text: modelData.type === 0 ? "T" :
                                modelData.type === 1 ? "🖼" :
                                modelData.type === 2 ? "▢" : "🎵"
                                color: theme.textSecondary
                                font.pixelSize: 12
                                Layout.preferredWidth: 20
                            }

                            Label {
                                Layout.fillWidth: true
                                text: modelData.id
                                color: selectedElementId === modelData.id ? theme.textPrimary : theme.textSecondary
                                elide: Text.ElideRight
                                font.pixelSize: 12
                            }

                            ToolButton {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                text: "👁"
                                font.pixelSize: 10
                                checked: modelData.visible
                                onClicked: {
                                    var props = {"visible": !modelData.visible}
                                    LabelMaker.updateElement(modelData.id, props)
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: selectedElementId = modelData.id
                            onDoubleClicked: selectedElementId = modelData.id
                        }
                    }
                }

                // Add Element Buttons
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 12
                    Layout.topMargin: 0
                    spacing: 8

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("Add Text")
                        onClicked: addTextElement()
                    }
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("Add Image")
                        onClicked: addImageElement()
                    }
                }
            }
        }

        // CENTER - Design Canvas
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: theme.canvasBg

            // Ruler Top
            Rectangle {
                id: rulerTop
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 20
                color: theme.ruler
                border { color: theme.border; width: 1 }

                Repeater {
                    model: Math.ceil(currentGeometry.size.width / 10) + 1
                    delegate: Rectangle {
                        x: (index * 10 * canvasScale * zoomLevel) + canvasOffset.x
                        y: 0
                        width: 1
                        height: index % 5 === 0 ? 12 : 6
                        color: theme.textDisabled
                        visible: x > 0 && x < parent.width
                    }
                }
            }

            // Ruler Left
            Rectangle {
                id: rulerLeft
                anchors.top: rulerTop.bottom
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                width: 20
                color: theme.ruler
                border { color: theme.border; width: 1 }

                Repeater {
                    model: Math.ceil(currentGeometry.size.height / 10) + 1
                    delegate: Rectangle {
                        x: 0
                        y: (index * 10 * canvasScale * zoomLevel) + canvasOffset.y
                        width: index % 5 === 0 ? 12 : 6
                        height: 1
                        color: theme.textDisabled
                        visible: y > 0 && y < parent.height
                    }
                }
            }

            // Canvas Area
            Rectangle {
                id: canvasArea
                anchors.top: rulerTop.bottom
                anchors.left: rulerLeft.right
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                color: theme.canvasBg
                clip: true

                // Grid Pattern
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    visible: snapToGrid

                    Repeater {
                        model: Math.ceil((currentGeometry.size.width / gridSizeMm) *
                        (currentGeometry.size.height / gridSizeMm))
                        delegate: Rectangle {
                            property real gridX: (index % Math.ceil(currentGeometry.size.width / gridSizeMm)) *
                            gridSizeMm * canvasScale * zoomLevel + canvasOffset.x
                            property real gridY: Math.floor(index / Math.ceil(currentGeometry.size.width / gridSizeMm)) *
                            gridSizeMm * canvasScale * zoomLevel + canvasOffset.y
                            x: gridX
                            y: gridY
                            width: 1
                            height: 1
                            color: theme.grid
                            visible: gridX > 0 && gridX < parent.width && gridY > 0 && gridY < parent.height
                        }
                    }
                }

                // Label Canvas (The actual design)
                Item {
                    id: designCanvas
                    x: canvasOffset.x + (parent.width - (currentGeometry.size.width * canvasScale * zoomLevel)) / 2
                    y: canvasOffset.y + (parent.height - (currentGeometry.size.height * canvasScale * zoomLevel)) / 2
                    width: currentGeometry.size.width * canvasScale * zoomLevel
                    height: currentGeometry.size.height * canvasScale * zoomLevel

                    // Background/Template Shape
                    Rectangle {
                        anchors.fill: parent
                        color: "white"
                        border { color: showGuides ? theme.guide : "transparent"; width: 1 }
                        radius: currentGeometry.isDisc ? width / 2 : 0

                        // Inner hole for discs
                        Rectangle {
                            visible: currentGeometry.isDisc && currentGeometry.innerDiameter > 0
                            width: (currentGeometry.innerDiameter / currentGeometry.outerDiameter) * parent.width
                            height: width
                            radius: width / 2
                            anchors.centerIn: parent
                            color: theme.canvasBg
                            border { color: showGuides ? theme.guide : "transparent"; width: 1 }
                        }

                        // Spine fold guides for cases
                        Rectangle {
                            visible: currentGeometry.isWraparound && showGuides
                            width: (currentGeometry.spineWidth / currentGeometry.size.width) * parent.width
                            height: parent.height
                            anchors.centerIn: parent
                            color: "transparent"
                            border { color: theme.guide; width: 1; style: Qt.DashLine }
                        }
                    }

                    // Elements Layer
                    Repeater {
                        model: LabelMaker.elements()
                        delegate: CanvasElement {
                            elementData: modelData
                            selected: selectedElementId === modelData.id
                            scaleFactor: zoomLevel * canvasScale
                            onClicked: {
                                selectedElementId = modelData.id
                                updatePropertiesPanel()
                            }
                            onPositionChanged: (newPos) => {
                                LabelMaker.moveElement(modelData.id, newPos)
                            }
                        }
                    }

                    // Selection marquee (when creating new elements)
                    Rectangle {
                        id: marquee
                        visible: false
                        color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.2)
                        border { color: theme.accent; width: 1 }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton

                        onPressed: (mouse) => {
                            if (currentTool === 0) {
                                // Select tool - check for element hit
                                var hit = false
                                // Hit testing logic would go here
                                if (!hit) selectedElementId = ""
                            } else {
                                // Creation tool - start marquee
                                marquee.x = mouse.x
                                marquee.y = mouse.y
                                marquee.width = 0
                                marquee.height = 0
                                marquee.visible = true
                            }
                        }

                        onPositionChanged: (mouse) => {
                            if (marquee.visible) {
                                marquee.width = mouse.x - marquee.x
                                marquee.height = mouse.y - marquee.y
                            }
                        }

                        onReleased: (mouse) => {
                            if (marquee.visible) {
                                marquee.visible = false
                                var rect = Qt.rect(
                                    Math.min(marquee.x, marquee.x + marquee.width) / (canvasScale * zoomLevel),
                                                   Math.min(marquee.y, marquee.y + marquee.height) / (canvasScale * zoomLevel),
                                                   Math.abs(marquee.width) / (canvasScale * zoomLevel),
                                                   Math.abs(marquee.height) / (canvasScale * zoomLevel)
                                )
                                createElementAt(rect)
                            }
                        }

                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                canvasContextMenu.popup()
                            }
                        }
                    }
                }

                // Zoom/Pan handling
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.MiddleButton
                    onWheel: (wheel) => {
                        if (wheel.modifiers & Qt.ControlModifier) {
                            // Zoom
                            var newZoom = zoomLevel + (wheel.angleDelta.y > 0 ? 0.1 : -0.1)
                            zoomLevel = Math.max(minZoom, Math.min(maxZoom, newZoom))
                        } else {
                            // Pan
                            canvasOffset.x += wheel.angleDelta.x / 2
                            canvasOffset.y += wheel.angleDelta.y / 2
                        }
                    }
                }
            }
        }

        // RIGHT PANEL - Properties Inspector
        Rectangle {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            color: theme.panel
            border { color: theme.border; width: 1 }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Properties Header
                Label {
                    text: selectedElementId === "" ? qsTr("PROPERTIES") : qsTr("ELEMENT PROPERTIES")
                    color: theme.textSecondary
                    font.bold: true
                    font.pixelSize: 11
                    Layout.margins: 16
                    Layout.bottomMargin: 12
                }

                // No Selection State
                Rectangle {
                    visible: selectedElementId === ""
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "transparent"

                    Column {
                        anchors.centerIn: parent
                        spacing: 12
                        width: parent.width - 32

                        Text {
                            text: "ℹ"
                            font.pixelSize: 48
                            color: theme.textDisabled
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Label {
                            text: qsTr("No element selected")
                            color: theme.textDisabled
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Label {
                            text: qsTr("Select an element on the canvas or add a new one from the toolbar")
                            color: theme.textDisabled
                            wrapMode: Text.Wrap
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: 12
                        }
                    }
                }

                // Element Properties
                ScrollView {
                    visible: selectedElementId !== ""
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: parent.width
                        spacing: 16

                        // Common Properties (Position/Size)
                        GroupBox {
                            title: qsTr("Position & Size")
                            Layout.fillWidth: true
                            Layout.margins: 16
                            Layout.topMargin: 0

                            GridLayout {
                                columns: 2
                                rowSpacing: 8
                                columnSpacing: 12
                                anchors.fill: parent

                                Label { text: qsTr("X (mm):"); color: theme.textSecondary }
                                SpinBox {
                                    id: posXBox
                                    editable: true
                                    from: -1000
                                    to: 1000
                                    onValueModified: updateElementProperty("x", value)
                                }

                                Label { text: qsTr("Y (mm):"); color: theme.textSecondary }
                                SpinBox {
                                    id: posYBox
                                    editable: true
                                    from: -1000
                                    to: 1000
                                    onValueModified: updateElementProperty("y", value)
                                }

                                Label { text: qsTr("Width:"); color: theme.textSecondary }
                                SpinBox {
                                    id: widthBox
                                    editable: true
                                    from: 1
                                    to: 1000
                                    onValueModified: updateElementProperty("width", value)
                                }

                                Label { text: qsTr("Height:"); color: theme.textSecondary }
                                SpinBox {
                                    id: heightBox
                                    editable: true
                                    from: 1
                                    to: 1000
                                    onValueModified: updateElementProperty("height", value)
                                }

                                Label { text: qsTr("Rotation:"); color: theme.textSecondary }
                                SpinBox {
                                    id: rotationBox
                                    editable: true
                                    from: -360
                                    to: 360
                                    suffix: "°"
                                    onValueModified: updateElementProperty("rotation", value)
                                }

                                Label { text: qsTr("Opacity:"); color: theme.textSecondary }
                                Slider {
                                    id: opacitySlider
                                    from: 0
                                    to: 1
                                    onMoved: updateElementProperty("opacity", value)
                                }
                            }
                        }

                        // Text Properties (Only for text elements)
                        GroupBox {
                            visible: currentElementType === 0  // Text
                            title: qsTr("Text Properties")
                            Layout.fillWidth: true
                            Layout.margins: 16

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 12

                                TextField {
                                    id: textContentField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Enter text...")
                                    onTextChanged: updateElementProperty("text", text)
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    ComboBox {
                                        id: fontCombo
                                        Layout.fillWidth: true
                                        model: Qt.fontFamilies()
                                        onActivated: updateElementProperty("font", currentText)
                                    }
                                    SpinBox {
                                        id: fontSizeBox
                                        from: 4
                                        to: 200
                                        onValueModified: updateElementProperty("fontSize", value)
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    ColorButton {
                                        id: textColorButton
                                        color: "#ffffff"
                                        onColorChanged: updateElementProperty("color", color)
                                    }
                                    CheckBox {
                                        text: qsTr("Bold")
                                        checked: false
                                        onCheckedChanged: updateElementProperty("bold", checked)
                                    }
                                    CheckBox {
                                        text: qsTr("Circular")
                                        checked: false
                                        onCheckedChanged: updateElementProperty("circularText", checked)
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Button {
                                        text: qsTr("Auto-fit")
                                        onClicked: updateElementProperty("autoFit", true)
                                    }
                                    Button {
                                        text: qsTr("Align")
                                        onClicked: alignmentMenu.open()

                                        Menu {
                                            id: alignmentMenu
                                            MenuItem { text: qsTr("Left"); onTriggered: updateElementProperty("alignment", Qt.AlignLeft) }
                                            MenuItem { text: qsTr("Center"); onTriggered: updateElementProperty("alignment", Qt.AlignCenter) }
                                            MenuItem { text: qsTr("Right"); onTriggered: updateElementProperty("alignment", Qt.AlignRight) }
                                        }
                                    }
                                }
                            }
                        }

                        // Image Properties
                        GroupBox {
                            visible: currentElementType === 1  // Image
                            title: qsTr("Image Properties")
                            Layout.fillWidth: true
                            Layout.margins: 16

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 12

                                Button {
                                    Layout.fillWidth: true
                                    text: qsTr("Change Image...")
                                    onClicked: imageFileDialog.open()
                                }

                                CheckBox {
                                    text: qsTr("Maintain Aspect Ratio")
                                    checked: true
                                    onCheckedChanged: updateElementProperty("maintainAspect", checked)
                                }

                                CheckBox {
                                    text: qsTr("Tint Color")
                                    onCheckedChanged: updateElementProperty("tintEnabled", checked)
                                }

                                ColorButton {
                                    enabled: parent.children[2].checked
                                    color: "#ffffff"
                                    onColorChanged: updateElementProperty("tint", color)
                                }
                            }
                        }

                        // Track List Properties
                        GroupBox {
                            visible: currentElementType === 3  // TrackList
                            title: qsTr("Track List Properties")
                            Layout.fillWidth: true
                            Layout.margins: 16

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 8

                                CheckBox {
                                    text: qsTr("Show Track Numbers")
                                    checked: true
                                    onCheckedChanged: updateElementProperty("showTrackNumbers", checked)
                                }
                                CheckBox {
                                    text: qsTr("Show Durations")
                                    checked: true
                                    onCheckedChanged: updateElementProperty("showDurations", checked)
                                }
                                CheckBox {
                                    text: qsTr("Show Total Duration")
                                    checked: true
                                    onCheckedChanged: updateElementProperty("totalDuration", checked)
                                }
                                TextField {
                                    Layout.fillWidth: true
                                    text: "%n. %t %d"
                                    onTextChanged: updateElementProperty("formatString", text)
                                }
                            }
                        }

                        // Actions
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: 16
                            spacing: 8

                            Button {
                                Layout.fillWidth: true
                                text: qsTr("Bring to Front")
                                onClicked: LabelMaker.bringToFront(selectedElementId)
                            }
                            Button {
                                Layout.fillWidth: true
                                text: qsTr("Send to Back")
                                onClicked: LabelMaker.sendToBack(selectedElementId)
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            Layout.margins: 16
                            text: qsTr("Delete Element")
                            background: Rectangle {
                                color: parent.down ? theme.error : "transparent"
                                border { color: theme.error; width: 1 }
                                radius: 4
                            }
                            contentItem: Text {
                                text: parent.text
                                color: theme.error
                                horizontalAlignment: Text.AlignHCenter
                            }
                            onClicked: deleteSelected()
                        }
                    }
                }
            }
        }
    }

    // Dialogs
    Platform.FileDialog {
        id: saveAsDialog
        title: qsTr("Save Label Project")
        fileMode: Platform.FileDialog.SaveFile
        defaultSuffix: "aegislabel"
            onAccepted: LabelMaker.saveProject(selectedFile)
    }

    Platform.FileDialog {
        id: openDialog
        title: qsTr("Open Label Project")
        fileMode: Platform.FileDialog.OpenFile
        nameFilters: [qsTr("Label projects (*.aegislabel)"), qsTr("All files (*)")]
        onAccepted: LabelMaker.loadProject(selectedFile)
    }

    Platform.FileDialog {
        id: imageFileDialog
        title: qsTr("Select Image")
        fileMode: Platform.FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.svg)"), qsTr("All files (*)")]
        onAccepted: updateElementProperty("source", selectedFile)
    }

    Platform.FileDialog {
        id: exportImageDialog
        title: qsTr("Export as Image")
        fileMode: Platform.FileDialog.SaveFile
        defaultSuffix: "png"
            onAccepted: LabelMaker.saveAsImage(selectedFile, "png", 300)
    }

    // Connections to C++ backend
    Connections {
        target: LabelMaker

        function onElementUpdated(id) {
            if (id === selectedElementId) {
                updatePropertiesPanel()
            }
        }

        function onCurrentTemplateChanged() {
            currentGeometry = LabelMaker.geometry()
            fitToWindow()
        }

        function onLibraryQueryFinished(success) {
            if (success) {
                toast.show(qsTr("Library data loaded successfully"), theme.success)
            } else {
                toast.show(qsTr("Failed to load library data"), theme.error)
            }
        }
    }

    // Helper functions
    function newProject() {
        LabelMaker.newProject()
        selectedElementId = ""
        zoomLevel = 1.0
        canvasOffset = Qt.point(0, 0)
    }

    function openProject() {
        openDialog.open()
    }

    function zoomIn() {
        zoomLevel = Math.min(maxZoom, zoomLevel + 0.25)
    }

    function zoomOut() {
        zoomLevel = Math.max(minZoom, zoomLevel - 0.25)
    }

    function fitToWindow() {
        var scaleX = (canvasArea.width - 40) / (currentGeometry.size.width * canvasScale)
        var scaleY = (canvasArea.height - 40) / (currentGeometry.size.height * canvasScale)
        zoomLevel = Math.min(scaleX, scaleY, 1.0)
        canvasOffset = Qt.point(0, 0)
    }

    function createElementAt(rect) {
        var id
        switch(currentTool) {
            case 1: // Text
                id = LabelMaker.addText("New Text", rect)
                break
            case 2: // Image
                id = LabelMaker.addImage("", rect)
                break
            case 3: // Shape
                id = LabelMaker.addShape(0, rect)  // Rectangle
                break
            case 4: // TrackList
                id = LabelMaker.addTrackList(rect)
                break
        }
        if (id) {
            selectedElementId = id
            currentTool = 0  // Switch back to select tool
        }
    }

    function addTextElement() {
        currentTool = 1
    }

    function addImageElement() {
        currentTool = 2
        imageFileDialog.open()
    }

    function deleteSelected() {
        if (selectedElementId !== "") {
            LabelMaker.removeElement(selectedElementId)
            selectedElementId = ""
        }
    }

    function updateElementProperty(prop, value) {
        if (selectedElementId === "") return
            var props = {}
            props[prop] = value
            LabelMaker.updateElement(selectedElementId, props)
    }

    function updatePropertiesPanel() {
        // This would refresh the property controls with current values
        // Implementation depends on full data binding
    }

    function rotateElement(deg) {
        if (selectedElementId === "") return
            var elem = LabelMaker.element(selectedElementId)
            if (elem) {
                updateElementProperty("rotation", (elem.rotation || 0) + deg)
            }
    }

    function exportPDF() {
        pdfSaveDialog.open()
    }

    property int currentElementType: {
        if (selectedElementId === "") return -1
            var elem = LabelMaker.element(selectedElementId)
            return elem ? elem.type : -1
    }

    // Toast notification
    Toast {
        id: toast
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 40
    }

    // Component for canvas elements
    // This would normally be in a separate file: CanvasElement.qml
    component CanvasElement: Item {
    property var elementData
    property bool selected: false
    property real scaleFactor: 1.0

    signal clicked()
    signal positionChanged(point newPos)

    x: (elementData.x || 0) * scaleFactor
    y: (elementData.y || 0) * scaleFactor
    width: (elementData.width || 50) * scaleFactor
    height: (elementData.height || 50) * scaleFactor
    rotation: elementData.rotation || 0
    opacity: elementData.opacity !== undefined ? elementData.opacity : 1

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border {
            color: selected ? theme.selection : "transparent"
            width: 2 / zoomLevel
        }

        // Visual representation based on type
        Loader {
            anchors.fill: parent
            sourceComponent: {
                switch(elementData.type) {
                    case 0: return textComponent
                    case 1: return imageComponent
                    case 2: return shapeComponent
                    case 3: return trackListComponent
                    default: return null
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: parent.clicked()
        drag.target: parent
        drag.axis: Drag.XAndYAxis
        onReleased: {
            positionChanged(Qt.point(x / scaleFactor, y / scaleFactor))
        }
    }

    Component {
        id: textComponent
        Text {
            text: elementData.text || "Text"
            color: elementData.color || "#000000"
            font.family: elementData.font || "Arial"
            font.pixelSize: (elementData.fontSize || 12) * scaleFactor
            font.bold: elementData.bold || false
            horizontalAlignment: elementData.alignment || Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
        }
    }

    Component {
        id: imageComponent
        Image {
            source: elementData.source || ""
            fillMode: elementData.maintainAspect ? Image.PreserveAspectFit : Image.Stretch
        }
    }

    Component {
        id: shapeComponent
        Rectangle {
            color: elementData.fillColor || "transparent"
            border {
                color: elementData.strokeColor || "#000000"
                width: elementData.strokeWidth || 1
            }
            radius: elementData.shapeType === 1 ? width/2 : 0  // Circle
        }
    }

    Component {
        id: trackListComponent
        Column {
            Repeater {
                model: elementData.tracks || []
                delegate: Text {
                    text: (elementData.showTrackNumbers ? (index + 1) + ". " : "") +
                    modelData +
                    (elementData.showDurations && elementData.durations && elementData.durations[index]
                    ? " (" + formatDuration(elementData.durations[index]) + ")"
                    : "")
                    color: elementData.color || "#000000"
                    font.family: elementData.font || "Arial"
                    font.pixelSize: (elementData.fontSize || 9) * scaleFactor
                }
            }
        }
    }

    function formatDuration(seconds) {
        var m = Math.floor(seconds / 60)
        var s = seconds % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }
}

// Color picker button component
component ColorButton: Rectangle {
    property color pickerColor: "#ffffff"
    signal colorChanged(color newColor)

    width: 32
    height: 32
    radius: 4
    color: pickerColor
    border { color: theme.border; width: 1 }

    MouseArea {
        anchors.fill: parent
        onClicked: colorDialog.open()
    }

    ColorDialog {
        id: colorDialog
        selectedColor: parent.pickerColor
        onAccepted: parent.colorChanged(selectedColor)
    }
}

} // end root element
