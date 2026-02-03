// ui_discburner.qml
// Aegis Disc Burner - Professional optical media burning interface
// Design: Nero-like dark theme with modern QML components

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import Qt.labs.platform as Platform

ApplicationWindow {
    id: burnWindow
    visible: true
    width: 1200
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    title: qsTr("Aegis Disc Burner") + (projectModified ? " *" : "")
    color: theme.background

    // Theme colors (professional dark theme)
    property var theme: {
        "background": "#1a1a1a",
        "panel": "#252525",
        "panelDark": "#1e1e1e",
        "panelDarker": "#121212",
        "border": "#333333",
        "accent": "#ff6600",
        "accentHover": "#ff8533",
        "accentPressed": "#cc5200",
        "textPrimary": "#ffffff",
        "textSecondary": "#aaaaaa",
        "textDisabled": "#666666",
        "success": "#27ae60",
        "warning": "#f39c12",
        "error": "#e74c3c",
        "info": "#3498db"
    }

    // Application state
    property int currentProjectType: 0  // 0=Audio CD, 1=Data CD, 2=Data DVD, 3=Blu-ray, 4=Copy, 5=ISO
    property bool projectModified: false
    property var projectFiles: []
    property var availableDrives: []
    property var driveCapabilities: null
    property string selectedDrive: ""
    property real usedSpaceBytes: 0
    property real totalSpaceBytes: 700 * 1024 * 1024  // Default CD capacity
    property bool isBurning: CDBurner ? CDBurner.burning : false
    property int burnProgress: 0
    property string burnStatus: ""
    property bool verificationInProgress: false

    // Audio CD specific
    property bool useCDText: true
    property string albumTitle: ""
    property string albumArtist: ""
    property int gapBetweenTracks: 2
    property bool normalizeAudio: false

    // Data disc specific
    property bool useJoliet: true
    property bool useRockRidge: false
    property bool allowDeepPaths: false
    property string volumeLabel: "AEGIS_DATA"

    // Burn settings
    property int burnSpeed: 0  // 0=Max, 1=52x, 2=32x, 3=16x, 4=8x, 5=4x, 1x
    property bool verifyAfterBurn: true
    property bool ejectAfterBurn: true
    property bool testBurn: false
    property bool closeSession: true
    property int burnCopies: 1

    // File list selection
    property int selectedFileIndex: -1
    property int totalFileCount: 0
    property real totalFileSize: 0

    // Disc capacities in bytes
    property var discCapacities: {
        "CD": 700 * 1024 * 1024,          // 700MB
        "CD80": 80 * 60 * 44100 * 4,      // 80min audio
        "DVD5": 4.7 * 1024 * 1024 * 1024, // 4.7GB
        "DVD9": 8.5 * 1024 * 1024 * 1024, // 8.5GB
        "BD25": 25 * 1024 * 1024 * 1024,  // 25GB
        "BD50": 50 * 1024 * 1024 * 1024   // 50GB
    }

    // Initialize
    Component.onCompleted: {
        refreshDrives()
        updateCapacity()
        updateProjectType(0)
    }

    // Menu Bar
    menuBar: MenuBar {
        Menu {
            title: qsTr("Project")
            Action {
                text: qsTr("New Audio CD")
                shortcut: "Ctrl+Shift+A"
                onTriggered: newAudioCDProject()
            }
            Action {
                text: qsTr("New Data CD/DVD")
                shortcut: "Ctrl+Shift+D"
                onTriggered: newDataDiscProject()
            }
            Action {
                text: qsTr("New ISO Project")
                shortcut: "Ctrl+Shift+I"
                onTriggered: newISOProject()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Save Project")
                shortcut: "Ctrl+S"
                enabled: projectFiles.length > 0 && projectModified
                onTriggered: saveProject()
            }
            Action {
                text: qsTr("Save Project As...")
                enabled: projectFiles.length > 0
                onTriggered: saveProjectAs()
            }
            Action {
                text: qsTr("Open Project...")
                shortcut: "Ctrl+O"
                onTriggered: openProject()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Import Nero Project...")
                onTriggered: importNeroProject()
            }
            Action {
                text: qsTr("Export to ISO...")
                enabled: projectFiles.length > 0
                onTriggered: exportToISO()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Exit")
                onTriggered: burnWindow.close()
            }
        }

        Menu {
            title: qsTr("Edit")
            Action {
                text: qsTr("Add Files...")
                shortcut: "Ctrl+A"
                onTriggered: addFilesDialog.open()
            }
            Action {
                text: qsTr("Add Folder...")
                shortcut: "Ctrl+Shift+A"
                onTriggered: addFolderDialog.open()
            }
            Action {
                text: qsTr("Remove Selected")
                shortcut: "Del"
                enabled: selectedFileIndex >= 0
                onTriggered: removeSelectedFile()
            }
            Action {
                text: qsTr("Clear Project")
                enabled: projectFiles.length > 0
                onTriggered: clearProject()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Preferences...")
                onTriggered: settingsDialog.open()
            }
        }

        Menu {
            title: qsTr("View")
            Action {
                text: qsTr("Toolbar")
                checkable: true
                checked: true
            }
            Action {
                text: qsTr("File Browser")
                checkable: true
                checked: true
            }
            Action {
                text: qsTr("Status Bar")
                checkable: true
                checked: true
            }
            MenuSeparator {}
            Action {
                text: qsTr("Compact View")
                onTriggered: fileList.compactView = !fileList.compactView
            }
            Action {
                text: qsTr("Refresh View")
                onTriggered: refreshFileList()
            }
        }

        Menu {
            title: qsTr("Burn")
            Action {
                text: qsTr("Burn Disc")
                shortcut: "Ctrl+B"
                enabled: projectFiles.length > 0 && !isBurning && selectedDrive !== ""
                onTriggered: startBurn()
            }
            Action {
                text: qsTr("Test Burn")
                enabled: projectFiles.length > 0 && !isBurning
                onTriggered: startTestBurn()
            }
            Action {
                text: qsTr("Burn Image...")
                onTriggered: burnImageDialog.open()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Erase Rewritable Disc...")
                onTriggered: eraseDialog.open()
            }
            Action {
                text: qsTr("Format DVD+RW...")
                enabled: driveCapabilities && driveCapabilities.canWriteDVDPlusRW
                onTriggered: formatDVDDialog.open()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Disc Info...")
                onTriggered: discInfoDialog.open()
            }
            Action {
                text: qsTr("Verify Disc...")
                onTriggered: verifyDisc()
            }
        }

        Menu {
            title: qsTr("Tools")
            Action {
                text: qsTr("Create ISO Image...")
                enabled: projectFiles.length > 0
                onTriggered: createISODialog.open()
            }
            Action {
                text: qsTr("Convert Audio Format...")
                enabled: currentProjectType === 0
                onTriggered: convertAudioDialog.open()
            }
            Action {
                text: qsTr("Calculate MD5 Checksums...")
                onTriggered: calculateChecksums()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Device Manager...")
                onTriggered: deviceManagerDialog.open()
            }
            Action {
                text: qsTr("Media Database...")
                onTriggered: mediaDatabaseDialog.open()
            }
        }

        Menu {
            title: qsTr("Help")
            Action {
                text: qsTr("User Guide")
                shortcut: "F1"
                onTriggered: showUserGuide()
            }
            Action {
                text: qsTr("Keyboard Shortcuts")
                onTriggered: showShortcutsDialog()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Check for Updates...")
                onTriggered: checkForUpdates()
            }
            MenuSeparator {}
            Action {
                text: qsTr("About Aegis Disc Burner")
                onTriggered: aboutDialog.open()
            }
        }
    }

    // Status Bar
    footer: ToolBar {
        height: 24
        background: Rectangle { color: theme.panelDark }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 20

            // Project status
            Text {
                text: projectFiles.length + " file(s), " + formatBytes(totalFileSize)
                color: theme.textSecondary
                font.pixelSize: 11
            }

            // Drive status
            Text {
                text: selectedDrive ? "Drive: " + selectedDrive.split('/').pop() : "No drive selected"
                color: selectedDrive ? theme.success : theme.textDisabled
                font.pixelSize: 11
            }

            // Media status
            Text {
                text: getMediaStatusText()
                color: getMediaStatusColor()
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            // Burn progress (when active)
            ProgressBar {
                id: statusProgressBar
                Layout.preferredWidth: 150
                visible: isBurning || verificationInProgress
                from: 0
                to: 100
                value: burnProgress
            }

            Text {
                visible: isBurning || verificationInProgress
                text: verificationInProgress ? "Verifying..." : burnStatus
                color: theme.textSecondary
                font.pixelSize: 11
            }
        }
    }

    // Main Layout
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // LEFT PANEL - Project Type & Disc Info
        Rectangle {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: theme.panelDarker
            border { color: theme.border; width: 1 }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Project Type Selector
                PanelHeader {
                    title: qsTr("PROJECT TYPE")
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 10
                    spacing: 8

                    ProjectTypeButton {
                        icon: "🎵"
                        title: qsTr("Audio CD")
                        description: qsTr("Create audio CD for standard players")
                        selected: currentProjectType === 0
                        onClicked: updateProjectType(0)
                    }

                    ProjectTypeButton {
                        icon: "💿"
                        title: qsTr("Data CD")
                        description: qsTr("Store files on CD (700MB)")
                        selected: currentProjectType === 1
                        onClicked: updateProjectType(1)
                    }

                    ProjectTypeButton {
                        icon: "📀"
                        title: qsTr("Data DVD")
                        description: qsTr("Store files on DVD (4.7GB)")
                        selected: currentProjectType === 2
                        onClicked: updateProjectType(2)
                    }

                    ProjectTypeButton {
                        icon: "🔵"
                        title: qsTr("Blu-ray")
                        description: qsTr("Store files on Blu-ray (25GB)")
                        selected: currentProjectType === 3
                        onClicked: updateProjectType(3)
                    }

                    ProjectTypeButton {
                        icon: "📋"
                        title: qsTr("Copy Disc")
                        description: qsTr("Duplicate existing disc")
                        selected: currentProjectType === 4
                        onClicked: updateProjectType(4)
                    }

                    ProjectTypeButton {
                        icon: "📁"
                        title: qsTr("Burn Image")
                        description: qsTr("Burn ISO/NRG image file")
                        selected: currentProjectType === 5
                        onClicked: updateProjectType(5)
                    }
                }

                // Disc Info Panel
                PanelHeader {
                    title: qsTr("DISC INFORMATION")
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 140
                    color: theme.panel
                    border { color: theme.border; width: 1 }
                    radius: 4

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        InfoRow {
                            label: qsTr("Drive:")
                            value: selectedDrive ? selectedDrive.split('/').pop() : qsTr("None")
                            valueColor: selectedDrive ? theme.success : theme.textDisabled
                        }

                        InfoRow {
                            label: qsTr("Media:")
                            value: getMediaTypeString()
                            valueColor: getMediaStatusColor()
                        }

                        InfoRow {
                            label: qsTr("Status:")
                            value: getDriveStatusString()
                            valueColor: getDriveStatusColor()
                        }

                        InfoRow {
                            label: qsTr("Capacity:")
                            value: formatBytes(totalSpaceBytes)
                            valueColor: theme.textSecondary
                        }

                        InfoRow {
                            label: qsTr("Free:")
                            value: formatBytes(totalSpaceBytes - usedSpaceBytes)
                            valueColor: usedSpaceBytes > totalSpaceBytes ? theme.error : theme.success
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // Quick Actions
                PanelHeader {
                    title: qsTr("QUICK ACTIONS")
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 10
                    spacing: 6

                    ActionButton {
                        text: qsTr("🔄 Refresh Drives")
                        onClicked: refreshDrives()
                        Layout.fillWidth: true
                    }

                    ActionButton {
                        text: qsTr("📊 Disc Info")
                        onClicked: discInfoDialog.open()
                        Layout.fillWidth: true
                    }

                    ActionButton {
                        text: qsTr("🗑️ Erase Disc")
                        onClicked: eraseDialog.open()
                        Layout.fillWidth: true
                        enabled: driveCapabilities && (driveCapabilities.canWriteCDRW || driveCapabilities.canWriteDVDPlusRW)
                    }
                }
            }
        }

        // CENTER PANEL - File List & Project Contents
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: theme.background

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Toolbar
                Rectangle {
                    Layout.fillWidth: true
                    height: 50
                    color: theme.panel
                    border { color: theme.border; width: 1 }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        // Add buttons
                        ToolButton {
                            text: "➕ " + qsTr("Add")
                            onClicked: addMenu.open()

                            Menu {
                                id: addMenu
                                y: parent.height
                                MenuItem {
                                    text: qsTr("Files...")
                                    onTriggered: addFilesDialog.open()
                                }
                                MenuItem {
                                    text: qsTr("Folder...")
                                    onTriggered: addFolderDialog.open()
                                }
                                MenuItem {
                                    text: qsTr("From Library...")
                                    enabled: currentProjectType === 0
                                    onTriggered: addFromLibrary()
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: qsTr("Playlist...")
                                    enabled: currentProjectType === 0
                                    onTriggered: addPlaylist()
                                }
                            }
                        }

                        ToolButton {
                            text: "🗑️ " + qsTr("Remove")
                            enabled: selectedFileIndex >= 0
                            onClicked: removeSelectedFile()
                        }

                        ToolButton {
                            text: "🔄 " + qsTr("Clear")
                            enabled: projectFiles.length > 0
                            onClicked: clearProject()
                        }

                        ToolSeparator {}

                        // Audio CD tools (only for audio projects)
                        ToolButton {
                            text: "🎚️ " + qsTr("Normalize")
                            enabled: currentProjectType === 0 && projectFiles.length > 0
                            onClicked: normalizeAudioTracks()
                            tooltip: qsTr("Normalize audio volume levels")
                        }

                        ToolButton {
                            text: "🎵 " + qsTr("Preview")
                            enabled: currentProjectType === 0 && selectedFileIndex >= 0
                            onClicked: previewSelectedTrack()
                            tooltip: qsTr("Preview selected audio track")
                        }

                        ToolButton {
                            text: "📝 " + qsTr("CD-Text")
                            enabled: currentProjectType === 0
                            onClicked: cdTextDialog.open()
                            tooltip: qsTr("Edit CD-Text information")
                        }

                        ToolSeparator {}

                        // View controls
                        ToolButton {
                            text: "📋 " + qsTr("List")
                            checkable: true
                            checked: true
                            onClicked: fileList.viewMode = 0
                        }

                        ToolButton {
                            text: "📊 " + qsTr("Details")
                            checkable: true
                            onClicked: fileList.viewMode = 1
                        }

                        Item { Layout.fillWidth: true }

                        // Project name
                        TextField {
                            Layout.preferredWidth: 200
                            placeholderText: qsTr("Project name...")
                            text: getDefaultProjectName()
                            onTextChanged: if (text) projectModified = true
                            background: Rectangle {
                                color: theme.panelDark
                                border { color: theme.border; width: 1 }
                                radius: 2
                            }
                        }
                    }
                }

                // File List Header
                Rectangle {
                    Layout.fillWidth: true
                    height: 32
                    color: theme.panelDark
                    border { color: theme.border; width: 1 }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 0

                        CheckBox {
                            Layout.preferredWidth: 32
                            checked: fileList.allSelected
                            onCheckedChanged: fileList.toggleSelectAll(checked)
                        }

                        Text {
                            Layout.preferredWidth: 300
                            text: qsTr("Name")
                            color: theme.textPrimary
                            font.bold: true
                            font.pixelSize: 11
                        }

                        Text {
                            Layout.preferredWidth: 100
                            text: qsTr("Size")
                            color: theme.textPrimary
                            font.bold: true
                            font.pixelSize: 11
                        }

                        Text {
                            Layout.preferredWidth: 100
                            text: qsTr("Type")
                            color: theme.textPrimary
                            font.bold: true
                            font.pixelSize: 11
                        }

                        Text {
                            Layout.preferredWidth: 120
                            text: qsTr("Duration")
                            color: theme.textPrimary
                            font.bold: true
                            font.pixelSize: 11
                            visible: currentProjectType === 0
                        }

                        Text {
                            Layout.preferredWidth: 150
                            text: qsTr("Modified")
                            color: theme.textPrimary
                            font.bold: true
                            font.pixelSize: 11
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Path")
                            color: theme.textPrimary
                            font.bold: true
                            font.pixelSize: 11
                        }
                    }
                }

                // File List Component
                FileListView {
                    id: fileList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    files: projectFiles
                    projectType: currentProjectType
                    onFileSelected: (index) => selectedFileIndex = index
                    onFilesDropped: (urls) => handleDroppedFiles(urls)
                    onFileDoubleClicked: (index) => handleFileDoubleClick(index)
                }

                // Capacity Meter
                CapacityMeter {
                    Layout.fillWidth: true
                    usedBytes: usedSpaceBytes
                    totalBytes: totalSpaceBytes
                    projectType: currentProjectType
                    onOverflow: (isOverflow) => showCapacityWarning(isOverflow)
                }
            }
        }

        // RIGHT PANEL - Settings & Burn Control
        Rectangle {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            color: theme.panelDarker
            border { color: theme.border; width: 1 }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Drive Selection
                PanelHeader {
                    title: qsTr("TARGET DRIVE")
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 140
                    color: theme.panel
                    border { color: theme.border; width: 1 }
                    radius: 4
                    Layout.margins: 10

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        ComboBox {
                            id: driveCombo
                            Layout.fillWidth: true
                            model: availableDrives
                            currentIndex: -1
                            textRole: "display"
                            onActivated: {
                                selectedDrive = currentText
                                updateDriveCapabilities()
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            text: qsTr("🔄 Refresh")
                            onClicked: refreshDrives()
                        }

                        // Drive status indicators
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            StatusIndicator {
                                color: driveCapabilities ? theme.success : theme.error
                                tooltip: driveCapabilities ? qsTr("Drive ready") : qsTr("Drive not ready")
                            }

                            StatusIndicator {
                                color: getMediaStatusColor()
                                tooltip: getMediaStatusText()
                            }

                            StatusIndicator {
                                color: isBurning ? theme.warning : theme.info
                                tooltip: isBurning ? qsTr("Burning in progress") : qsTr("Drive idle")
                                blinking: isBurning
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: getDriveDetails()
                            color: theme.textSecondary
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                }

                // Burn Settings
                PanelHeader {
                    title: qsTr("BURN SETTINGS")
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 10

                    clip: true
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: parent.width
                        spacing: 12

                        // Speed selection
                        SettingRow {
                            label: qsTr("Speed:")
                            ComboBox {
                                Layout.fillWidth: true
                                model: getSpeedOptions()
                                currentIndex: burnSpeed
                                onActivated: burnSpeed = currentIndex
                            }
                        }

                        // Copies
                        SettingRow {
                            label: qsTr("Copies:")
                            SpinBox {
                                from: 1
                                to: 99
                                value: burnCopies
                                onValueChanged: burnCopies = value
                            }
                        }

                        // Audio CD specific settings
                        ColumnLayout {
                            visible: currentProjectType === 0
                            spacing: 8

                            CheckBox {
                                text: qsTr("Use CD-Text")
                                checked: useCDText
                                onCheckedChanged: useCDText = checked
                                contentItem: Text {
                                    text: parent.text
                                    color: theme.textSecondary
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }

                            CheckBox {
                                text: qsTr("Normalize audio")
                                checked: normalizeAudio
                                onCheckedChanged: normalizeAudio = checked
                                contentItem: Text {
                                    text: parent.text
                                    color: theme.textSecondary
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }

                            SettingRow {
                                label: qsTr("Track gap:")
                                SpinBox {
                                    from: 0
                                    to: 10
                                    value: gapBetweenTracks
                                    onValueChanged: gapBetweenTracks = value
                                }
                                ToolButton {
                                    text: "s"
                                    font.pixelSize: 10
                                    tooltip: qsTr("Seconds between tracks")
                                }
                            }
                        }

                        // Data disc specific settings
                        ColumnLayout {
                            visible: currentProjectType >= 1 && currentProjectType <= 3
                            spacing: 8

                            TextField {
                                Layout.fillWidth: true
                                placeholderText: qsTr("Volume label")
                                text: volumeLabel
                                onTextChanged: if (text) volumeLabel = text
                            }

                            CheckBox {
                                text: qsTr("Joliet extensions")
                                checked: useJoliet
                                onCheckedChanged: useJoliet = checked
                                contentItem: Text {
                                    text: parent.text
                                    color: theme.textSecondary
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }

                            CheckBox {
                                text: qsTr("Rock Ridge (Unix)")
                                checked: useRockRidge
                                onCheckedChanged: useRockRidge = checked
                                contentItem: Text {
                                    text: parent.text
                                    color: theme.textSecondary
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }

                            CheckBox {
                                text: qsTr("Allow deep paths")
                                checked: allowDeepPaths
                                onCheckedChanged: allowDeepPaths = checked
                                contentItem: Text {
                                    text: parent.text
                                    color: theme.textSecondary
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }
                        }

                        // Burn options
                        ColumnLayout {
                            spacing: 8

                            CheckBox {
                                text: qsTr("Verify after burn")
                                checked: verifyAfterBurn
                                onCheckedChanged: verifyAfterBurn = checked
                                contentItem: Text {
                                    text: parent.text
                                    color: theme.textSecondary
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }

                            CheckBox {
                                text: qsTr("Eject after burn")
                                checked: ejectAfterBurn
                                onCheckedChanged: ejectAfterBurn = checked
                                contentItem: Text {
                                    text: parent.text
                                    color: theme.textSecondary
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }

                            CheckBox {
                                text: qsTr("Test burn only")
                                checked: testBurn
                                onCheckedChanged: testBurn = checked
                                contentItem: Text {
                                    text: parent.text
                                    color: theme.textSecondary
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }

                            CheckBox {
                                text: qsTr("Close session")
                                checked: closeSession
                                onCheckedChanged: closeSession = checked
                                contentItem: Text {
                                    text: parent.text
                                    color: theme.textSecondary
                                    leftPadding: parent.indicator.width + parent.spacing
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // Burn Control Panel
                PanelHeader {
                    title: qsTr("BURN CONTROL")
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    Layout.margins: 10
                    color: theme.panel
                    border { color: isBurning ? theme.accent : theme.border; width: 2 }
                    radius: 6

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        BurnButton {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            burning: isBurning
                            progress: burnProgress
                            enabled: canStartBurn()
                            onClicked: startBurn()
                        }

                        Button {
                            Layout.fillWidth: true
                            visible: isBurning
                            text: qsTr("Cancel Burn")
                            onClicked: cancelBurn()
                            background: Rectangle {
                                color: parent.pressed ? theme.error : "#444444"
                                radius: 4
                                border { color: theme.error; width: 1 }
                            }
                            contentItem: Text {
                                text: parent.text
                                color: theme.textPrimary
                                horizontalAlignment: Text.AlignHCenter
                                font.bold: true
                            }
                        }
                    }
                }
            }
        }
    }

    // Burn Overlay (Modal)
    BurnOverlay {
        visible: isBurning || verificationInProgress
        progress: burnProgress
        status: burnStatus
        projectType: currentProjectType
        fileCount: projectFiles.length
        onCancelRequested: cancelBurn()
    }

    // Dialogs
    Platform.FileDialog {
        id: addFilesDialog
        title: qsTr("Add Files to Project")
        fileMode: Platform.FileDialog.OpenFiles
        onAccepted: handleSelectedFiles(selectedFiles)
    }

    Platform.FileDialog {
        id: addFolderDialog
        title: qsTr("Add Folder to Project")
        fileMode: Platform.FileDialog.OpenFolder
        onAccepted: scanFolder(selectedFolder)
    }

    Platform.FileDialog {
        id: burnImageDialog
        title: qsTr("Select Disc Image")
        nameFilters: [
            qsTr("Disc images (*.iso *.nrg *.img *.bin)"),
            qsTr("ISO files (*.iso)"),
            qsTr("NRG files (*.nrg)"),
            qsTr("All files (*)")
        ]
        onAccepted: burnImage(selectedFile)
    }

    // Dialog Components
    EraseDialog {
        id: eraseDialog
        drive: selectedDrive
        capabilities: driveCapabilities
        onEraseRequested: (fast) => eraseDisc(fast)
    }

    DiscInfoDialog {
        id: discInfoDialog
        drive: selectedDrive
        capabilities: driveCapabilities
    }

    SettingsDialog {
        id: settingsDialog
    }

    AboutDialog {
        id: aboutDialog
    }

    // Connections to C++ backend
    Connections {
        target: CDBurner

        function onBurnProgress(percent, status) {
            burnProgress = percent
            burnStatus = status
        }

        function onBurnFinished(success, message) {
            isBurning = false
            verificationInProgress = false
            burnProgress = 0
            burnStatus = ""

            if (success) {
                projectModified = false
                showBurnSuccess(message)
            } else {
                showBurnError(message)
            }
        }

        function onAvailableSpace(bytesFree, bytesTotal) {
            if (bytesTotal > 0) {
                totalSpaceBytes = bytesTotal
                updateCapacity()
            }
        }

        function onLogInfo(message) {
            console.log("INFO:", message)
        }

        function onLogError(message) {
            console.error("ERROR:", message)
            showErrorMessage(message)
        }
    }

    // Helper Functions
    function refreshDrives() {
        availableDrives = CDBurner ? CDBurner.enumerateDrives() : []
        if (availableDrives.length > 0) {
            driveCombo.currentIndex = 0
            selectedDrive = availableDrives[0]
            updateDriveCapabilities()
        } else {
            selectedDrive = ""
            driveCapabilities = null
        }
    }

    function updateDriveCapabilities() {
        if (selectedDrive) {
            driveCapabilities = CDBurner.getCapabilities(selectedDrive)
            CDBurner.availableSpace(selectedDrive)
        } else {
            driveCapabilities = null
        }
    }

    function updateProjectType(type) {
        currentProjectType = type
        switch(type) {
            case 0: // Audio CD
                totalSpaceBytes = discCapacities.CD80
                break
            case 1: // Data CD
                totalSpaceBytes = discCapacities.CD
                break
            case 2: // Data DVD
                totalSpaceBytes = discCapacities.DVD5
                break
            case 3: // Blu-ray
                totalSpaceBytes = discCapacities.BD25
                break
            default:
                totalSpaceBytes = discCapacities.CD
        }
        updateCapacity()
    }

    function updateCapacity() {
        totalFileCount = projectFiles.length
        totalFileSize = usedSpaceBytes = calculateTotalSize()
    }

    function calculateTotalSize() {
        var total = 0
        for (var i = 0; i < projectFiles.length; i++) {
            total += projectFiles[i].size || 0
        }
        return total
    }

    function handleSelectedFiles(files) {
        for (var i = 0; i < files.length; i++) {
            addFile(files[i])
        }
        projectModified = true
        updateCapacity()
    }

    function addFile(filePath) {
        // This would use Qt's file info in real implementation
        var fileInfo = {
            path: filePath,
            name: filePath.split('/').pop(),
            size: Math.random() * 1024 * 1024 * 10, // Simulated size
            type: getFileType(filePath),
            modified: new Date().toLocaleDateString(),
            duration: currentProjectType === 0 ? formatTime(Math.random() * 300) : ""
        }
        projectFiles.push(fileInfo)
    }

    function getFileType(path) {
        var ext = path.split('.').pop().toLowerCase()
        if (['mp3','wav','flac','aac','ogg','m4a'].includes(ext)) return "Audio"
            if (['avi','mp4','mkv','mov','wmv'].includes(ext)) return "Video"
                if (['jpg','png','gif','bmp'].includes(ext)) return "Image"
                    if (['iso','nrg','img','bin'].includes(ext)) return "Disc Image"
                        return "Document"
    }

    function removeSelectedFile() {
        if (selectedFileIndex >= 0) {
            projectFiles.splice(selectedFileIndex, 1)
            selectedFileIndex = -1
            projectModified = true
            updateCapacity()
        }
    }

    function clearProject() {
        projectFiles = []
        selectedFileIndex = -1
        projectModified = false
        updateCapacity()
    }

    function canStartBurn() {
        return projectFiles.length > 0 &&
        !isBurning &&
        selectedDrive !== "" &&
        driveCapabilities &&
        usedSpaceBytes <= totalSpaceBytes
    }

    function startBurn() {
        if (!canStartBurn()) return

            var job = createBurnJob()
            // In real implementation: CDBurner.startBurn(job)
            isBurning = true
            burnStatus = qsTr("Preparing burn...")

            // Simulate burn progress
            simulateBurnProgress()
    }

    function createBurnJob() {
        var job = {}
        job.type = currentProjectType
        job.device = selectedDrive
        job.sourceFiles = projectFiles.map(f => f.path)
        job.volumeLabel = volumeLabel
        job.verify = verifyAfterBurn
        job.ejectAfter = ejectAfterBurn
        job.dummyMode = testBurn
        job.speed = getSelectedSpeed()

        if (currentProjectType === 0) {
            job.albumTitle = albumTitle
            job.albumArtist = albumArtist
            job.useCDText = useCDText
            job.gapBetweenTracks = gapBetweenTracks
            job.normalizeAudio = normalizeAudio
        }

        return job
    }

    function getSelectedSpeed() {
        var speeds = ["Max", "52x", "32x", "16x", "8x", "4x", "1x"]
        return speeds[burnSpeed] || "Max"
    }

    function getSpeedOptions() {
        if (!driveCapabilities) return ["Max"]

            var options = ["Max"]
            if (currentProjectType <= 1) { // CD
                if (driveCapabilities.maxSpeedCD >= 52) options.push("52x")
                    if (driveCapabilities.maxSpeedCD >= 32) options.push("32x")
                        if (driveCapabilities.maxSpeedCD >= 16) options.push("16x")
                            if (driveCapabilities.maxSpeedCD >= 8) options.push("8x")
                                options.push("4x", "1x")
            } else if (currentProjectType === 2) { // DVD
                if (driveCapabilities.maxSpeedDVD >= 16) options.push("16x")
                    if (driveCapabilities.maxSpeedDVD >= 8) options.push("8x")
                        options.push("4x", "2.4x", "1x")
            }
            return options
    }

    function cancelBurn() {
        if (isBurning) {
            CDBurner.cancelBurn()
            isBurning = false
            verificationInProgress = false
        }
    }

    function simulateBurnProgress() {
        // For demo purposes only
        var progress = 0
        var interval = setInterval(() => {
            if (!isBurning) {
                clearInterval(interval)
                return
            }
            progress += 1
            burnProgress = progress
            burnStatus = getBurnStatusText(progress)

            if (progress >= 100) {
                clearInterval(interval)
                isBurning = false
                verificationInProgress = true
                simulateVerification()
            }
        }, 100)
    }

    function simulateVerification() {
        var progress = 0
        var interval = setInterval(() => {
            if (!verificationInProgress) {
                clearInterval(interval)
                return
            }
            progress += 2
            burnProgress = progress
            burnStatus = qsTr("Verifying...") + " " + progress + "%"

            if (progress >= 100) {
                clearInterval(interval)
                verificationInProgress = false
                burnProgress = 0
                burnStatus = ""
                showBurnSuccess(qsTr("Burn completed successfully!"))
            }
        }, 80)
    }

    function getBurnStatusText(progress) {
        if (progress < 20) return qsTr("Initializing...")
            if (progress < 40) return qsTr("Writing lead-in...")
                if (progress < 60) return qsTr("Writing data...")
                    if (progress < 80) return qsTr("Writing lead-out...")
                        if (progress < 95) return qsTr("Finalizing...")
                            return qsTr("Finishing...")
    }

    function formatBytes(bytes) {
        if (bytes === 0) return "0 B"
            const k = 1024
            const sizes = ["B", "KB", "MB", "GB", "TB"]
            const i = Math.floor(Math.log(bytes) / Math.log(k))
            return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + " " + sizes[i]
    }

    function formatTime(seconds) {
        var mins = Math.floor(seconds / 60)
        var secs = Math.floor(seconds % 60)
        return mins + ":" + (secs < 10 ? "0" : "") + secs
    }

    function getMediaTypeString() {
        if (!driveCapabilities) return qsTr("Unknown")
            if (driveCapabilities.canWriteBD) return qsTr("Blu-ray")
                if (driveCapabilities.canWriteDVDR || driveCapabilities.canWriteDVDPlusR) return qsTr("DVD")
                    if (driveCapabilities.canWriteCDR) return qsTr("CD")
                        return qsTr("No media")
    }

    function getMediaStatusText() {
        if (!driveCapabilities) return qsTr("No media")
            if (driveCapabilities.canWriteCDR && !driveCapabilities.canWriteCDRW) return qsTr("CD-R")
                if (driveCapabilities.canWriteCDRW) return qsTr("CD-RW")
                    if (driveCapabilities.canWriteDVDR) return qsTr("DVD-R")
                        if (driveCapabilities.canWriteDVDPlusR) return qsTr("DVD+R")
                            if (driveCapabilities.canWriteBD) return qsTr("BD-R")
                                return qsTr("Read-only")
    }

    function getMediaStatusColor() {
        if (!driveCapabilities) return theme.error
            if (driveCapabilities.canWriteCDR || driveCapabilities.canWriteDVDR || driveCapabilities.canWriteBD)
                return theme.success
                return theme.warning
    }

    function getDriveStatusString() {
        if (!driveCapabilities) return qsTr("Not ready")
            if (isBurning) return qsTr("Burning")
                if (verificationInProgress) return qsTr("Verifying")
                    return qsTr("Ready")
    }

    function getDriveStatusColor() {
        if (!driveCapabilities) return theme.error
            if (isBurning) return theme.warning
                if (verificationInProgress) return theme.info
                    return theme.success
    }

    function getDriveDetails() {
        if (!driveCapabilities) return qsTr("Select a drive")

            var details = []
            if (driveCapabilities.canWriteCDR) details.push("CD-R")
                if (driveCapabilities.canWriteCDRW) details.push("CD-RW")
                    if (driveCapabilities.canWriteDVDR) details.push("DVD-R")
                        if (driveCapabilities.canWriteDVDPlusR) details.push("DVD+R")
                            if (driveCapabilities.canWriteBD) details.push("BD")

                                return details.join(", ")
    }

    function getDefaultProjectName() {
        switch(currentProjectType) {
            case 0: return qsTr("Audio CD Project")
            case 1: return qsTr("Data CD Project")
            case 2: return qsTr("Data DVD Project")
            case 3: return qsTr("Blu-ray Project")
            case 4: return qsTr("Disc Copy Project")
            case 5: return qsTr("Image Burn Project")
            default: return qsTr("New Project")
        }
    }

    function showBurnSuccess(message) {
        toast.show(message, theme.success)
        if (ejectAfterBurn && selectedDrive) {
            CDBurner.eject(selectedDrive)
        }
    }

    function showBurnError(message) {
        toast.show(qsTr("Burn failed: ") + message, theme.error)
    }

    function showErrorMessage(message) {
        toast.show(message, theme.error)
    }

    function showCapacityWarning(isOverflow) {
        if (isOverflow) {
            toast.show(qsTr("Warning: Project exceeds disc capacity!"), theme.warning)
        }
    }

    // Toast notification component (simplified)
    Toast {
        id: toast
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
    }
}

// Component definitions would be in separate QML files in real implementation
// Here are simplified inline versions for completeness

// PanelHeader component
Component {
    id: panelHeaderComponent
    Rectangle {
        height: 40
        color: theme.panelDark
        border { color: theme.border; width: 1 }
        Text {
            anchors.centerIn: parent
            text: title
            color: theme.textPrimary
            font.bold: true
            font.pixelSize: 11
            font.capitalization: Font.AllUppercase
        }
    }
}

// ProjectTypeButton component
Component {
    id: projectTypeButtonComponent
    Rectangle {
        property bool selected: false
        property string icon: ""
        property string title: ""
        property string description: ""

        height: 60
        radius: 4
        color: selected ? theme.accent : (mouseArea.containsMouse ? theme.panel : "transparent")
        border { color: selected ? theme.accentHover : "transparent"; width: 1 }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 10

            Text {
                text: icon
                font.pixelSize: 20
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: title
                    color: selected ? theme.textPrimary : theme.textSecondary
                    font.bold: true
                    font.pixelSize: 12
                }

                Text {
                    Layout.fillWidth: true
                    text: description
                    color: selected ? theme.textPrimary : theme.textDisabled
                    font.pixelSize: 9
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: if (onClicked) onClicked()
        }
    }
}

// ... Additional components would follow (InfoRow, ActionButton, StatusIndicator, etc.)
