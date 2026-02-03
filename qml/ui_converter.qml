// ui_converter.qml - Professional Audio/Video Converter UI
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.platform as Platform

ApplicationWindow {
    id: converterWindow
    visible: true
    width: 1400
    height: 850
    minimumWidth: 1000
    minimumHeight: 600
    title: qsTr("Aegis Media Converter") + (converting ? " [Converting...]" : "")
    color: "#0a0a0a"

    // Backend references
    property var converterBackend: Converter ? Converter : null

    // Converter state
    property bool converting: converterBackend ? converterBackend.converting : false
    property int progress: converterBackend ? converterBackend.progress : 0
    property string currentStatus: ""
    property var currentJob: null
    property var jobQueue: []
    property var jobHistory: []

    // UI state
    property int selectedPreset: 0
    property bool batchMode: false
    property var selectedFiles: []
    property string outputDirectory: StandardPaths.writableLocation(StandardPaths.MusicLocation)
    property bool advancedMode: false

    // Presets
    property var presets: [
        {
            id: "audio_mp3",
            name: qsTr("Audio - MP3"),
            description: qsTr("High quality MP3 audio"),
            icon: "🎵",
            category: "audio",
            extensions: ["mp3", "wav", "flac", "m4a", "ogg", "aac"],
            outputExt: "mp3",
            quality: 2
        },
        {
            id: "audio_flac",
            name: qsTr("Audio - FLAC"),
            description: qsTr("Lossless FLAC audio"),
            icon: "🎼",
            category: "audio",
            extensions: ["mp3", "wav", "flac", "m4a", "ogg", "aac"],
            outputExt: "flac",
            quality: 0
        },
        {
            id: "video_mp4",
            name: qsTr("Video - MP4"),
            description: qsTr("H.264 MP4 video"),
            icon: "🎬",
            category: "video",
            extensions: ["mp4", "avi", "mkv", "mov", "wmv", "flv"],
            outputExt: "mp4",
            quality: 23
        },
        {
            id: "audio_extract",
            name: qsTr("Extract Audio"),
            description: qsTr("Extract audio from video"),
            icon: "🔊",
            category: "audio",
            extensions: ["mp4", "avi", "mkv", "mov", "wmv"],
            outputExt: "mp3",
            extractAudio: true
        },
        {
            id: "mobile",
            name: qsTr("Mobile Optimized"),
            description: qsTr("Optimized for mobile devices"),
            icon: "📱",
            category: "video",
            extensions: ["mp4", "avi", "mkv", "mov", "wmv"],
            outputExt: "mp4",
            resolution: "1280x720"
        },
        {
            id: "web",
            name: qsTr("Web Optimized"),
            description: qsTr("Optimized for web streaming"),
            icon: "🌐",
            category: "video",
            extensions: ["mp4", "avi", "mkv", "mov", "wmv"],
            outputExt: "mp4",
            resolution: "1920x1080",
            bitrate: "2000k"
        }
    ]

    // Theme
    property var theme: {
        "background": "#0a0a0a",
        "panel": "#121212",
        "panelDark": "#0d0d0d",
        "panelLight": "#1a1a1a",
        "accent": "#ff8c00",
        "accentHover": "#ffa033",
        "textPrimary": "#ffffff",
        "textSecondary": "#aaaaaa",
        "textDisabled": "#666666",
        "success": "#27ae60",
        "warning": "#f39c12",
        "error": "#e74c3c",
        "info": "#3498db"
    }

    // Initialize
    Component.onCompleted: {
        loadJobHistory()
    }

    // Menu Bar
    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")

            MenuItem {
                text: qsTr("Add &Files...")
                shortcut: "Ctrl+O"
                onTriggered: fileDialog.open()
            }

            MenuItem {
                text: qsTr("Add &Folder...")
                shortcut: "Ctrl+Shift+O"
                onTriggered: folderDialog.open()
            }

            MenuItem {
                text: qsTr("&Clear All")
                shortcut: "Ctrl+Del"
                enabled: selectedFiles.length > 0
                onTriggered: clearAllFiles()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Save &Queue...")
                onTriggered: saveQueueDialog.open()
            }

            MenuItem {
                text: qsTr("Load &Queue...")
                onTriggered: loadQueueDialog.open()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("E&xit")
                shortcut: "Alt+F4"
                onTriggered: converterWindow.close()
            }
        }

        Menu {
            title: qsTr("&Edit")

            MenuItem {
                text: qsTr("&Select All")
                shortcut: "Ctrl+A"
                enabled: selectedFiles.length > 0
                onTriggered: selectAllFiles()
            }

            MenuItem {
                text: qsTr("&Deselect All")
                shortcut: "Ctrl+Shift+A"
                enabled: selectedFiles.length > 0
                onTriggered: deselectAllFiles()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Remove Selected")
                shortcut: "Del"
                enabled: fileListView.selectedIndexes.length > 0
                onTriggered: removeSelectedFiles()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Preferences...")
                onTriggered: preferencesDialog.open()
            }
        }

        Menu {
            title: qsTr("&Convert")

            MenuItem {
                text: qsTr("&Start Conversion")
                shortcut: "Ctrl+S"
                enabled: selectedFiles.length > 0 && !converting
                onTriggered: startConversion()
            }

            MenuItem {
                text: qsTr("&Stop Conversion")
                shortcut: "Ctrl+Q"
                enabled: converting
                onTriggered: stopConversion()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Batch Mode")
                checkable: true
                checked: batchMode
                onTriggered: batchMode = !batchMode
            }

            MenuItem {
                text: qsTr("&Advanced Mode")
                checkable: true
                checked: advancedMode
                onTriggered: advancedMode = !advancedMode
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Preset Manager...")
                onTriggered: presetManagerDialog.open()
            }
        }

        Menu {
            title: qsTr("&Tools")

            MenuItem {
                text: qsTr("&Audio Converter")
                onTriggered: selectedPreset = 0
            }

            MenuItem {
                text: qsTr("&Video Converter")
                onTriggered: selectedPreset = 2
            }

            MenuItem {
                text: qsTr("&Batch Processor")
                onTriggered: batchProcessorDialog.open()
            }

            MenuItem {
                text: qsTr("&Metadata Editor...")
                onTriggered: metadataDialog.open()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&Calculator")
                onTriggered: calculatorDialog.open()
            }

            MenuItem {
                text: qsTr("&Codec Info...")
                onTriggered: codecInfoDialog.open()
            }
        }

        Menu {
            title: qsTr("&Help")

            MenuItem {
                text: qsTr("&User Guide")
                shortcut: "F1"
                onTriggered: userGuideDialog.open()
            }

            MenuItem {
                text: qsTr("&Keyboard Shortcuts")
                onTriggered: shortcutsDialog.open()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("&About Aegis Converter")
                onTriggered: aboutDialog.open()
            }
        }
    }

    // Main Layout
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top Toolbar
        Rectangle {
            id: toolbar
            Layout.fillWidth: true
            height: 60
            color: theme.panelDark
            border { color: "#1a1a1a"; width: 1 }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                // Preset Selector
                ComboBox {
                    id: presetCombo
                    Layout.preferredWidth: 200
                    model: presets
                    textRole: "name"
                    currentIndex: selectedPreset
                    onActivated: selectedPreset = currentIndex

                    delegate: ItemDelegate {
                        width: parent.width
                        RowLayout {
                            spacing: 10
                            Text {
                                text: modelData.icon
                                font.pixelSize: 16
                            }
                            ColumnLayout {
                                spacing: 2
                                Text {
                                    text: modelData.name
                                    color: theme.textPrimary
                                    font.bold: true
                                }
                                Text {
                                    text: modelData.description
                                    color: theme.textDisabled
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }

                // File operations
                Button {
                    text: "📁 Add Files"
                    onClicked: fileDialog.open()
                }

                Button {
                    text: "📂 Add Folder"
                    onClicked: folderDialog.open()
                }

                Button {
                    text: "🗑 Clear"
                    enabled: selectedFiles.length > 0
                    onClicked: clearAllFiles()
                }

                Item { Layout.fillWidth: true }

                // Mode toggles
                Switch {
                    id: batchSwitch
                    text: "Batch Mode"
                    checked: batchMode
                    onCheckedChanged: batchMode = checked
                }

                Switch {
                    id: advancedSwitch
                    text: "Advanced"
                    checked: advancedMode
                    onCheckedChanged: advancedMode = checked
                }

                Item { Layout.fillWidth: true }

                // Control buttons
                Button {
                    id: startButton
                    text: converting ? "⏹ Stop" : "▶ Start"
                    background: Rectangle {
                        color: parent.pressed ? (converting ? "#8b0000" : "#006400") :
                        (converting ? theme.error : theme.success)
                        radius: 4
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                    onClicked: converting ? stopConversion() : startConversion()
                    enabled: selectedFiles.length > 0
                }

                // Progress indicator
                Rectangle {
                    visible: converting
                    Layout.preferredWidth: 200
                    height: 20
                    color: theme.panel
                    border.color: theme.border
                    border.width: 1
                    radius: 2

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * (progress / 100)
                        color: theme.accent
                        radius: 2
                    }

                    Text {
                        anchors.centerIn: parent
                        text: progress + "%"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 11
                    }
                }
            }
        }

        // Main Content
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Left Panel - File List
            Rectangle {
                Layout.preferredWidth: parent.width * 0.4
                Layout.fillHeight: true
                color: theme.panel
                border { color: "#1a1a1a"; width: 1 }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // File List Header
                    Rectangle {
                        Layout.fillWidth: true
                        height: 40
                        color: theme.panelDark

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            Text {
                                text: "📁 Files to Convert (" + selectedFiles.length + ")"
                                color: theme.textPrimary
                                font.bold: true
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: formatTotalSize()
                                color: theme.textSecondary
                                font.pixelSize: 12
                            }
                        }
                    }

                    // File List
                    ListView {
                        id: fileListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: selectedFiles
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds

                        property var selectedIndexes: []

                        delegate: Rectangle {
                            width: fileListView.width
                            height: 60
                            color: fileListView.selectedIndexes.includes(index) ?
                            Qt.rgba(1, 0.55, 0, 0.1) :
                            (index % 2 === 0 ? theme.panelLight : theme.panel)
                            border.color: fileMouse.containsMouse ? theme.accent : "transparent"
                            border.width: 1
                            radius: 4

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                // File Icon
                                Rectangle {
                                    Layout.preferredWidth: 40
                                    Layout.preferredHeight: 40
                                    radius: 6
                                    color: getFileColor(modelData.type)

                                    Text {
                                        anchors.centerIn: parent
                                        text: getFileIcon(modelData.type)
                                        font.pixelSize: 18
                                        color: "white"
                                    }
                                }

                                // File Info
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: modelData.name
                                        color: theme.textPrimary
                                        font.bold: true
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: modelData.path
                                        color: theme.textDisabled
                                        font.pixelSize: 10
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }

                                    RowLayout {
                                        Text {
                                            text: modelData.size
                                            color: theme.textSecondary
                                            font.pixelSize: 10
                                        }

                                        Item { Layout.fillWidth: true }

                                        Text {
                                            text: modelData.duration
                                            color: theme.accent
                                            font.pixelSize: 10
                                            visible: modelData.duration
                                        }
                                    }
                                }

                                // Status Indicator
                                Rectangle {
                                    visible: modelData.status !== undefined
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: getStatusColor(modelData.status)
                                }
                            }

                            MouseArea {
                                id: fileMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (mouse.modifiers & Qt.ControlModifier) {
                                        // Toggle selection
                                        var idx = fileListView.selectedIndexes.indexOf(index)
                                        if (idx === -1) {
                                            fileListView.selectedIndexes.push(index)
                                        } else {
                                            fileListView.selectedIndexes.splice(idx, 1)
                                        }
                                    } else if (mouse.modifiers & Qt.ShiftModifier) {
                                        // Range selection
                                        if (fileListView.selectedIndexes.length > 0) {
                                            var last = fileListView.selectedIndexes[fileListView.selectedIndexes.length - 1]
                                            var start = Math.min(last, index)
                                            var end = Math.max(last, index)
                                            fileListView.selectedIndexes = []
                                            for (var i = start; i <= end; i++) {
                                                fileListView.selectedIndexes.push(i)
                                            }
                                        } else {
                                            fileListView.selectedIndexes = [index]
                                        }
                                    } else {
                                        // Single selection
                                        fileListView.selectedIndexes = [index]
                                    }
                                }
                                onDoubleClicked: {
                                    previewFile(modelData.path)
                                }
                            }
                        }

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }
                    }

                    // File List Footer
                    Rectangle {
                        Layout.fillWidth: true
                        height: 50
                        color: theme.panelDark

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10

                            Button {
                                text: "Select All"
                                onClicked: selectAllFiles()
                                Layout.fillWidth: true
                            }

                            Button {
                                text: "Deselect All"
                                onClicked: deselectAllFiles()
                                Layout.fillWidth: true
                            }

                            Button {
                                text: "Remove"
                                enabled: fileListView.selectedIndexes.length > 0
                                onClicked: removeSelectedFiles()
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }

            // Center Panel - Settings
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.background

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 20

                    // Preset Configuration
                    GroupBox {
                        title: "🎯 Conversion Settings"
                        Layout.fillWidth: true

                        GridLayout {
                            columns: 2
                            columnSpacing: 20
                            rowSpacing: 10
                            width: parent.width

                            // Preset Selection
                            Label {
                                text: "Preset:"
                                color: theme.textPrimary
                                Layout.alignment: Qt.AlignRight
                            }

                            ComboBox {
                                id: presetSelector
                                model: presets
                                textRole: "name"
                                currentIndex: selectedPreset
                                onActivated: selectedPreset = currentIndex
                                Layout.fillWidth: true
                            }

                            // Output Format
                            Label {
                                text: "Output Format:"
                                color: theme.textPrimary
                                Layout.alignment: Qt.AlignRight
                            }

                            TextField {
                                id: outputFormatField
                                text: presets[selectedPreset].outputExt
                                readOnly: true
                                Layout.fillWidth: true
                            }

                            // Output Directory
                            Label {
                                text: "Output Folder:"
                                color: theme.textPrimary
                                Layout.alignment: Qt.AlignRight
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                TextField {
                                    id: outputDirField
                                    text: outputDirectory
                                    Layout.fillWidth: true
                                }

                                Button {
                                    text: "Browse"
                                    onClicked: outputDirDialog.open()
                                }
                            }

                            // Quality
                            Label {
                                text: "Quality:"
                                color: theme.textPrimary
                                Layout.alignment: Qt.AlignRight
                            }

                            Slider {
                                id: qualitySlider
                                from: 0
                                to: 10
                                value: presets[selectedPreset].quality || 5
                                stepSize: 1
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // Advanced Settings (collapsible)
                    GroupBox {
                        title: "⚙ Advanced Settings"
                        Layout.fillWidth: true
                        visible: advancedMode

                        GridLayout {
                            columns: 2
                            columnSpacing: 20
                            rowSpacing: 10
                            width: parent.width

                            // Video Resolution
                            Label {
                                text: "Resolution:"
                                color: theme.textPrimary
                                Layout.alignment: Qt.AlignRight
                            }

                            ComboBox {
                                model: ["Original", "3840x2160 (4K)", "1920x1080 (Full HD)", "1280x720 (HD)", "854x480 (SD)", "640x360 (Mobile)"]
                                currentIndex: 0
                                Layout.fillWidth: true
                            }

                            // Bitrate
                            Label {
                                text: "Bitrate:"
                                color: theme.textPrimary
                                Layout.alignment: Qt.AlignRight
                            }

                            RowLayout {
                                ComboBox {
                                    model: ["Auto", "32k", "64k", "128k", "192k", "256k", "320k"]
                                    currentIndex: 3
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: "kbps"
                                    color: theme.textSecondary
                                }
                            }

                            // Sample Rate
                            Label {
                                text: "Sample Rate:"
                                color: theme.textPrimary
                                Layout.alignment: Qt.AlignRight
                            }

                            ComboBox {
                                model: ["Original", "44100 Hz", "48000 Hz", "96000 Hz"]
                                currentIndex: 0
                                Layout.fillWidth: true
                            }

                            // Channels
                            Label {
                                text: "Channels:"
                                color: theme.textPrimary
                                Layout.alignment: Qt.AlignRight
                            }

                            ComboBox {
                                model: ["Original", "Mono", "Stereo", "5.1", "7.1"]
                                currentIndex: 0
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // Preview Area
                    GroupBox {
                        title: "👁 Preview"
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent

                            // Preview controls
                            RowLayout {
                                Button {
                                    text: "▶ Play Preview"
                                    enabled: fileListView.selectedIndexes.length === 1
                                    onClicked: previewCurrentFile()
                                }

                                Button {
                                    text: "📊 Analyze"
                                    enabled: fileListView.selectedIndexes.length === 1
                                    onClicked: analyzeCurrentFile()
                                }

                                Item { Layout.fillWidth: true }
                            }

                            // Preview display
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: "#000000"
                                border.color: theme.border
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "Preview will appear here"
                                    color: theme.textDisabled
                                    visible: !previewLoader.active
                                }

                                Loader {
                                    id: previewLoader
                                    anchors.fill: parent
                                }
                            }
                        }
                    }
                }
            }

            // Right Panel - Queue & History
            Rectangle {
                Layout.preferredWidth: parent.width * 0.3
                Layout.fillHeight: true
                color: theme.panel
                border { color: "#1a1a1a"; width: 1 }

                TabBar {
                    id: rightTabBar
                    width: parent.width

                    TabButton {
                        text: "🔄 Queue"
                        width: parent.width / 2
                    }

                    TabButton {
                        text: "📋 History"
                        width: parent.width / 2
                    }
                }

                StackLayout {
                    anchors.top: rightTabBar.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    currentIndex: rightTabBar.currentIndex

                    // Queue Tab
                    ColumnLayout {
                        spacing: 0

                        // Queue Header
                        Rectangle {
                            Layout.fillWidth: true
                            height: 40
                            color: theme.panelDark

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10

                                Text {
                                    text: "Conversion Queue (" + jobQueue.length + ")"
                                    color: theme.textPrimary
                                    font.bold: true
                                }

                                Item { Layout.fillWidth: true }

                                Button {
                                    text: "Clear"
                                    enabled: jobQueue.length > 0
                                    onClicked: clearQueue()
                                    flat: true
                                }
                            }
                        }

                        // Queue List
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: jobQueue
                            clip: true
                            spacing: 2

                            delegate: Rectangle {
                                width: parent.width
                                height: 50
                                color: index % 2 === 0 ? theme.panelLight : theme.panel

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 10

                                    // Status icon
                                    Rectangle {
                                        Layout.preferredWidth: 24
                                        Layout.preferredHeight: 24
                                        radius: 12
                                        color: getJobStatusColor(modelData.status)

                                        Text {
                                            anchors.centerIn: parent
                                            text: getJobStatusIcon(modelData.status)
                                            color: "white"
                                            font.pixelSize: 12
                                        }
                                    }

                                    // Job info
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Text {
                                            text: modelData.fileName
                                            color: theme.textPrimary
                                            font.pixelSize: 11
                                            elide: Text.ElideMiddle
                                        }

                                        Text {
                                            text: modelData.preset + " → " + modelData.outputExt
                                            color: theme.textSecondary
                                            font.pixelSize: 9
                                        }
                                    }

                                    // Progress/actions
                                    Rectangle {
                                        visible: modelData.status === "processing"
                                        Layout.preferredWidth: 50
                                        Layout.preferredHeight: 20
                                        color: theme.panelDark
                                        radius: 2

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: parent.width * (modelData.progress / 100)
                                            color: theme.accent
                                            radius: 2
                                        }
                                    }

                                    Button {
                                        text: "❌"
                                        visible: modelData.status === "pending"
                                        onClicked: removeFromQueue(index)
                                        flat: true
                                    }
                                }
                            }

                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }
                        }
                    }

                    // History Tab
                    ColumnLayout {
                        spacing: 0

                        // History Header
                        Rectangle {
                            Layout.fillWidth: true
                            height: 40
                            color: theme.panelDark

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10

                                Text {
                                    text: "Conversion History (" + jobHistory.length + ")"
                                    color: theme.textPrimary
                                    font.bold: true
                                }

                                Item { Layout.fillWidth: true }

                                Button {
                                    text: "Clear All"
                                    enabled: jobHistory.length > 0
                                    onClicked: clearHistory()
                                    flat: true
                                }
                            }
                        }

                        // History List
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: jobHistory
                            clip: true
                            spacing: 2

                            delegate: Rectangle {
                                width: parent.width
                                height: 50
                                color: index % 2 === 0 ? theme.panelLight : theme.panel

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 10

                                    // Result icon
                                    Rectangle {
                                        Layout.preferredWidth: 24
                                        Layout.preferredHeight: 24
                                        radius: 12
                                        color: modelData.success ? theme.success : theme.error

                                        Text {
                                            anchors.centerIn: parent
                                            text: modelData.success ? "✓" : "✗"
                                            color: "white"
                                            font.pixelSize: 12
                                            font.bold: true
                                        }
                                    }

                                    // History info
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Text {
                                            text: modelData.fileName
                                            color: theme.textPrimary
                                            font.pixelSize: 11
                                            elide: Text.ElideMiddle
                                        }

                                        Text {
                                            text: Qt.formatDateTime(modelData.timestamp, "MMM d hh:mm")
                                            color: theme.textSecondary
                                            font.pixelSize: 9
                                        }
                                    }

                                    // Actions
                                    Button {
                                        text: "📂"
                                        onClicked: openOutputFile(modelData.outputPath)
                                        flat: true
                                        ToolTip.text: "Open output file"
                                    }
                                }
                            }

                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }
                        }
                    }
                }
            }
        }
    }

    // Status Bar
    footer: Rectangle {
        height: 28
        color: theme.panelDark
        border { color: "#1a1a1a"; width: 1 }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 20

            // Status message
            Text {
                id: statusText
                text: converting ? "🔄 Converting: " + currentStatus : "Ready"
                color: converting ? theme.accent : theme.textSecondary
                font.pixelSize: 11
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            // Progress info
            Text {
                visible: converting
                text: progress + "%"
                color: theme.textPrimary
                font.bold: true
                font.pixelSize: 11
            }

            // File count
            Text {
                text: selectedFiles.length + " file(s)"
                color: theme.textSecondary
                font.pixelSize: 11
            }

            // Estimated time
            Text {
                visible: converting
                text: "ETA: " + formatTimeRemaining()
                color: theme.textSecondary
                font.pixelSize: 11
            }
        }
    }

    // Dialogs
    Platform.FileDialog {
        id: fileDialog
        title: "Select Files to Convert"
        fileMode: Platform.FileDialog.OpenFiles
        nameFilters: [
            "All media files (*.mp3 *.wav *.flac *.ogg *.m4a *.aac *.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm)",
            "Audio files (*.mp3 *.wav *.flac *.ogg *.m4a *.aac *.aiff)",
            "Video files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v)",
            "All files (*)"
        ]
        onAccepted: addFiles(selectedFiles)
    }

    Platform.FileDialog {
        id: folderDialog
        title: "Select Folder"
        fileMode: Platform.FileDialog.OpenFolder
        onAccepted: scanFolder(selectedFolder)
    }

    Platform.FileDialog {
        id: outputDirDialog
        title: "Select Output Directory"
        fileMode: Platform.FileDialog.OpenFolder
        onAccepted: outputDirectory = selectedFolder
    }

    // Connections to backend
    Connections {
        target: converterBackend

        function onProgressChanged() {
            progress = converterBackend.progress
        }

        function onConvertingChanged() {
            converting = converterBackend.converting
        }

        function onConversionFinished(success, message) {
            var job = currentJob
            if (job) {
                jobHistory.unshift({
                    fileName: job.fileName,
                    success: success,
                    message: message,
                    timestamp: new Date(),
                                   outputPath: job.outputPath
                })

                if (jobHistory.length > 100) {
                    jobHistory.pop()
                }

                currentJob = null

                if (success) {
                    showNotification("Conversion Complete", job.fileName + " converted successfully")
                } else {
                    showNotification("Conversion Failed", job.fileName + ": " + message)
                }
            }

            // Process next job in queue
            processNextJob()
        }
    }

    // Functions
    function addFiles(filePaths) {
        for (var i = 0; i < filePaths.length; i++) {
            var filePath = filePaths[i]
            var fileInfo = getFileInfo(filePath)
            selectedFiles.push(fileInfo)

            if (batchMode) {
                addToQueue(fileInfo)
            }
        }
    }

    function getFileInfo(filePath) {
        var fileName = filePath.split('/').pop()
        var extension = fileName.split('.').pop().toLowerCase()
        var type = getFileType(extension)

        return {
            path: filePath,
            name: fileName,
            type: type,
            size: formatFileSize(getFileSize(filePath)),
            duration: getFileDuration(filePath),
            extension: extension
        }
    }

    function getFileType(extension) {
        var audioExt = ["mp3", "wav", "flac", "ogg", "m4a", "aac", "aiff"]
        var videoExt = ["mp4", "avi", "mkv", "mov", "wmv", "flv", "webm"]

        if (audioExt.includes(extension)) return "audio"
            if (videoExt.includes(extension)) return "video"
                return "other"
    }

    function getFileIcon(type) {
        switch(type) {
            case "audio": return "🎵"
            case "video": return "🎬"
            default: return "📄"
        }
    }

    function getFileColor(type) {
        switch(type) {
            case "audio": return "#2196F3"
            case "video": return "#FF5722"
            default: return "#9C27B0"
        }
    }

    function addToQueue(fileInfo) {
        var preset = presets[selectedPreset]
        var outputPath = outputDirectory + "/" +
        fileInfo.name.replace(/\.[^/.]+$/, "") +
        "_converted." + preset.outputExt

        jobQueue.push({
            filePath: fileInfo.path,
            fileName: fileInfo.name,
            preset: preset.name,
            outputExt: preset.outputExt,
            outputPath: outputPath,
            status: "pending",
            progress: 0
        })
    }

    function startConversion() {
        if (batchMode && jobQueue.length > 0) {
            processNextJob()
        } else if (selectedFiles.length > 0) {
            // Convert selected files
            for (var i = 0; i < selectedFiles.length; i++) {
                var fileInfo = selectedFiles[i]
                addToQueue(fileInfo)
            }
            processNextJob()
        }
    }

    function processNextJob() {
        if (converting || jobQueue.length === 0) return

            // Find next pending job
            var nextJobIndex = -1
            for (var i = 0; i < jobQueue.length; i++) {
                if (jobQueue[i].status === "pending") {
                    nextJobIndex = i
                    break
                }
            }

            if (nextJobIndex === -1) {
                // All jobs done
                showNotification("Queue Complete", "All conversions finished")
                return
            }

            var job = jobQueue[nextJobIndex]
            job.status = "processing"
            currentJob = job
            currentStatus = "Converting " + job.fileName

            // Call backend converter
            if (converterBackend) {
                var presetEnum = getPresetEnum(selectedPreset)
                converterBackend.convertFile(job.filePath, job.outputPath, presetEnum)
            } else {
                // Simulate conversion for demo
                simulateConversion(job, nextJobIndex)
            }
    }

    function getPresetEnum(presetIndex) {
        // Map to C++ enum values
        switch(presetIndex) {
            case 0: return 0  // AudioMP3
            case 1: return 1  // AudioFLAC
            case 2: return 4  // VideoMP4
            case 3: return 3  // AudioOnly
            case 4: return 5  // Mobile
            case 5: return 8  // Web (would need to be added to enum)
            default: return 0
        }
    }

    function simulateConversion(job, index) {
        var progress = 0
        var interval = setInterval(function() {
            progress += 2
            job.progress = progress
            converterWindow.progress = progress

            if (progress >= 100) {
                clearInterval(interval)
                job.status = "completed"
                jobQueue.splice(index, 1)

                // Add to history
                jobHistory.unshift({
                    fileName: job.fileName,
                    success: true,
                    timestamp: new Date(),
                                   outputPath: job.outputPath
                })

                currentJob = null
                currentStatus = ""
                converterWindow.progress = 0

                // Process next job
                Qt.callLater(processNextJob)
            }
        }, 100)
    }

    function stopConversion() {
        if (converterBackend) {
            converterBackend.cancel()
        }

        if (currentJob) {
            currentJob.status = "cancelled"
            currentJob.progress = 0
        }

        converting = false
        currentStatus = "Conversion stopped"
        progress = 0
    }

    function selectAllFiles() {
        fileListView.selectedIndexes = []
        for (var i = 0; i < selectedFiles.length; i++) {
            fileListView.selectedIndexes.push(i)
        }
    }

    function deselectAllFiles() {
        fileListView.selectedIndexes = []
    }

    function removeSelectedFiles() {
        // Remove in reverse order to maintain indices
        fileListView.selectedIndexes.sort(function(a, b) { return b - a })

        for (var i = 0; i < fileListView.selectedIndexes.length; i++) {
            var index = fileListView.selectedIndexes[i]
            selectedFiles.splice(index, 1)

            // Also remove from queue if present
            for (var j = jobQueue.length - 1; j >= 0; j--) {
                if (jobQueue[j].filePath === selectedFiles[index]?.path) {
                    jobQueue.splice(j, 1)
                }
            }
        }

        fileListView.selectedIndexes = []
    }

    function clearAllFiles() {
        selectedFiles = []
        jobQueue = []
        fileListView.selectedIndexes = []
    }

    function clearQueue() {
        // Only remove pending jobs
        for (var i = jobQueue.length - 1; i >= 0; i--) {
            if (jobQueue[i].status === "pending") {
                jobQueue.splice(i, 1)
            }
        }
    }

    function clearHistory() {
        jobHistory = []
    }

    function removeFromQueue(index) {
        jobQueue.splice(index, 1)
    }

    function formatTotalSize() {
        // Calculate total size of selected files
        var totalBytes = 0
        for (var i = 0; i < selectedFiles.length; i++) {
            totalBytes += parseFileSize(selectedFiles[i].size)
        }
        return formatFileSize(totalBytes)
    }

    function parseFileSize(sizeString) {
        var match = sizeString.match(/([\d.]+)\s*([KMGTP]?B)/i)
        if (!match) return 0

            var num = parseFloat(match[1])
            var unit = match[2].toUpperCase()

            var multiplier = 1
            switch(unit) {
                case "KB": multiplier = 1024; break
                case "MB": multiplier = 1024 * 1024; break
                case "GB": multiplier = 1024 * 1024 * 1024; break
                case "TB": multiplier = 1024 * 1024 * 1024 * 1024; break
            }

            return num * multiplier
    }

    function formatFileSize(bytes) {
        if (bytes === 0) return "0 B"
            var k = 1024
            var sizes = ["B", "KB", "MB", "GB", "TB"]
            var i = Math.floor(Math.log(bytes) / Math.log(k))
            return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + " " + sizes[i]
    }

    function formatTimeRemaining() {
        if (!converting || progress === 0) return "--:--"
            var elapsed = new Date() - conversionStartTime
            var totalTime = elapsed * (100 / progress)
            var remaining = totalTime - elapsed
            return formatDuration(remaining)
    }

    function formatDuration(ms) {
        var seconds = Math.floor(ms / 1000)
        var minutes = Math.floor(seconds / 60)
        var hours = Math.floor(minutes / 60)

        if (hours > 0) {
            return hours + "h " + (minutes % 60) + "m"
        } else if (minutes > 0) {
            return minutes + "m " + (seconds % 60) + "s"
        } else {
            return seconds + "s"
        }
    }

    function getStatusColor(status) {
        switch(status) {
            case "completed": return theme.success
            case "processing": return theme.accent
            case "failed": return theme.error
            case "cancelled": return theme.warning
            default: return theme.textDisabled
        }
    }

    function getJobStatusColor(status) {
        switch(status) {
            case "pending": return "#666666"
            case "processing": return theme.accent
            case "completed": return theme.success
            case "failed": return theme.error
            case "cancelled": return theme.warning
            default: return theme.textDisabled
        }
    }

    function getJobStatusIcon(status) {
        switch(status) {
            case "pending": return "⏱"
            case "processing": return "🔄"
            case "completed": return "✓"
            case "failed": return "✗"
            case "cancelled": return "⏹"
            default: return "?"
        }
    }

    function showNotification(title, message) {
        notificationDialog.title = title
        notificationDialog.message = message
        notificationDialog.open()
    }

    function previewCurrentFile() {
        if (fileListView.selectedIndexes.length === 1) {
            var index = fileListView.selectedIndexes[0]
            previewFile(selectedFiles[index].path)
        }
    }

    function previewFile(filePath) {
        // In real implementation, this would launch the player
        console.log("Previewing file:", filePath)
    }

    function analyzeCurrentFile() {
        if (fileListView.selectedIndexes.length === 1) {
            var index = fileListView.selectedIndexes[0]
            analyzeFile(selectedFiles[index].path)
        }
    }

    function analyzeFile(filePath) {
        // In real implementation, this would analyze the file
        console.log("Analyzing file:", filePath)
    }

    function openOutputFile(filePath) {
        // In real implementation, this would open the file
        console.log("Opening output file:", filePath)
    }

    function loadJobHistory() {
        // Load from persistent storage
        // This is a stub
        jobHistory = []
    }

    function scanFolder(folderPath) {
        // In real implementation, this would scan the folder for media files
        console.log("Scanning folder:", folderPath)
    }

    // Mock functions for file info
    function getFileSize(filePath) {
        return Math.random() * 100 * 1024 * 1024  // Random size up to 100MB
    }

    function getFileDuration(filePath) {
        var duration = Math.random() * 300  // Random duration up to 5 minutes
        var minutes = Math.floor(duration / 60)
        var seconds = Math.floor(duration % 60)
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    // Dialog components (simplified)
    Dialog {
        id: notificationDialog
        title: "Notification"
        standardButtons: Dialog.Ok

        Text {
            text: notificationDialog.message || ""
            wrapMode: Text.Wrap
        }
    }

    Dialog {
        id: preferencesDialog
        title: "Preferences"
        width: 600
        height: 400
        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    property date conversionStartTime: new Date()
}
