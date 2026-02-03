// ui_launcher.qml - Professional Aegis Media Suite Launcher

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.settings
import Qt.labs.platform as Platform
import QtQuick.Window

ApplicationWindow {
    id: launcherWindow
    visible: true
    width: 1400
    height: 850
    minimumWidth: 1000
    minimumHeight: 600
    title: "Aegis Media Suite - Launcher"
    color: "#0a0a0a"
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint |
    Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint |
    Qt.WindowCloseButtonHint

    // Theme
    property var theme: {
        "background": "#0a0a0a",
        "panel": "#121212",
        "panelDark": "#0d0d0d",
        "panelLight": "#1a1a1a",
        "accent": "#ff8c00",
        "accentHover": "#ffa033",
        "accentPressed": "#cc5200",
        "textPrimary": "#ffffff",
        "textSecondary": "#aaaaaa",
        "textDisabled": "#666666",
        "success": "#27ae60",
        "warning": "#f39c12",
        "error": "#e74c3c",
        "info": "#3498db",
        "border": "#2a2a2a",
        "card": "#1e1e1e"
    }

    // Persistent data with improved defaults
    Settings {
        id: launcherSettings
        category: "Launcher"

        property var recentFiles: []
        property var recentPlaylists: []
        property var recentProjects: []
        property var favoriteApps: ["player", "converter", "middleware", "audioeditor", "burner", "musicnotation"]
        property bool showWelcome: true
        property string lastUsedApp: "player"
        property var appUsageCount: ({})
        property int layoutMode: 0 // 0=Grid, 1=List, 2=Compact
        property bool showTips: true
        property int launchCount: 0
        property var pinnedFiles: []
        property string viewMode: "grid" // grid, list, detailed
    }

    // Data models with improved structure
    ListModel { id: recentFilesModel }
    ListModel { id: recentPlaylistsModel }
    ListModel { id: recentProjectsModel }
    ListModel { id: pinnedFilesModel }
    ListModel { id: appModel }
    ListModel { id: quickActionsModel }

    // System state
    property bool isLoading: false
    property string searchQuery: ""
    property string selectedCategory: "all"
    property var filteredApps: []
    property var stats: ({})
    property int totalFileSize: 0
    property date lastUpdated: new Date()

    // Initialize with enhanced startup
    Component.onCompleted: {
        console.log("🏠 Launcher initializing...")
        launcherSettings.launchCount = (launcherSettings.launchCount || 0) + 1

        loadRecentData()
        loadApps()
        loadQuickActions()
        calculateStats()

        if (launcherSettings.showWelcome && launcherSettings.launchCount <= 3) {
            welcomeDialog.open()
        }

        // Start auto-refresh timer
        refreshTimer.start()

        // Check for updates
        checkForUpdates()

        console.log("✅ Launcher ready. Total launches:", launcherSettings.launchCount)
    }

    Component.onDestruction: {
        saveLauncherState()
    }

    // Keyboard shortcuts
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: searchField.forceActiveFocus()
    }

    Shortcut {
        sequence: "F5"
        onActivated: refreshAllData()
    }

    Shortcut {
        sequence: "Esc"
        onActivated: {
            if (searchQuery !== "") {
                searchQuery = ""
            } else if (contextMenu.visible) {
                contextMenu.close()
            }
        }
    }

    // Auto-refresh timer
    Timer {
        id: refreshTimer
        interval: 30000 // 30 seconds
        running: true
        repeat: true
        onTriggered: refreshData()
    }

    // ============================================
    // MAIN LAYOUT
    // ============================================

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // HEADER - Enhanced with more functionality
        Rectangle {
            id: header
            Layout.fillWidth: true
            height: 70
            color: theme.panelDark
            border { color: theme.border; width: 1 }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 20

                // Logo and title with version
                Row {
                    spacing: 15
                    Layout.alignment: Qt.AlignVCenter

                    // Animated logo
                    Rectangle {
                        width: 40
                        height: 40
                        radius: 8
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#ff8c00" }
                            GradientStop { position: 1.0; color: "#ff5500" }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "🎵"
                            font.pixelSize: 24
                        }

                        RotationAnimation on rotation {
                            running: true
                            loops: Animation.Infinite
                            from: 0
                            to: 360
                            duration: 20000
                            easing.type: Easing.Linear
                        }
                    }

                    Column {
                        spacing: 2
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            text: "AEGIS MEDIA SUITE"
                            color: theme.accent
                            font.pixelSize: 20
                            font.bold: true
                            font.family: "JetBrains Mono"
                        }

                        Text {
                            text: "Professional Multimedia Platform v2.1"
                            color: theme.textSecondary
                            font.pixelSize: 10
                            font.family: "JetBrains Mono"
                        }
                    }
                }

                // Search bar with advanced filters
                Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    radius: 18
                    color: theme.panel
                    border.color: searchField.activeFocus ? theme.accent : theme.border
                    border.width: 2

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 15
                        anchors.rightMargin: 15
                        spacing: 10

                        Text {
                            text: "🔍"
                            color: theme.textSecondary
                            font.pixelSize: 14
                        }

                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            placeholderText: "Search apps, files, tools..."
                            color: theme.textPrimary
                            background: Rectangle { color: "transparent" }
                            font.pixelSize: 13
                            onTextChanged: {
                                searchQuery = text
                                filterApps()
                            }
                            onAccepted: {
                                if (filteredApps.length > 0) {
                                    launchApp(filteredApps[0].id)
                                }
                            }

                            Keys.onEscapePressed: text = ""
                        }

                        // Filter button
                        Button {
                            text: "⚙"
                            flat: true
                            onClicked: filterMenu.open()

                            Menu {
                                id: filterMenu
                                MenuItem {
                                    text: "Show All"
                                    checkable: true
                                    checked: selectedCategory === "all"
                                    onTriggered: selectedCategory = "all"
                                }
                                MenuItem {
                                    text: "Audio Tools"
                                    checkable: true
                                    checked: selectedCategory === "audio"
                                    onTriggered: selectedCategory = "audio"
                                }
                                MenuItem {
                                    text: "Video Tools"
                                    checkable: true
                                    checked: selectedCategory === "video"
                                    onTriggered: selectedCategory = "video"
                                }
                                MenuItem {
                                    text: "Productivity"
                                    checkable: true
                                    checked: selectedCategory === "productivity"
                                    onTriggered: selectedCategory = "productivity"
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: "Sort by: Name"
                                    onTriggered: sortApps("name")
                                }
                                MenuItem {
                                    text: "Sort by: Usage"
                                    onTriggered: sortApps("usage")
                                }
                                MenuItem {
                                    text: "Sort by: Recent"
                                    onTriggered: sortApps("recent")
                                }
                            }
                        }
                    }
                }

                // User info and notifications
                Row {
                    spacing: 12
                    Layout.alignment: Qt.AlignVCenter

                    // Notification indicator
                    Rectangle {
                        width: 36
                        height: 36
                        radius: 18
                        color: theme.panel
                        border.color: theme.border

                        Text {
                            anchors.centerIn: parent
                            text: "🔔"
                            font.pixelSize: 16
                        }

                        // Notification badge
                        Rectangle {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            width: 12
                            height: 12
                            radius: 6
                            color: theme.accent
                            visible: hasNotifications
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: notificationPanel.open()
                        }
                    }

                    // User profile
                    Rectangle {
                        width: 36
                        height: 36
                        radius: 18
                        color: theme.accent
                        border.color: theme.accentHover

                        Text {
                            anchors.centerIn: parent
                            text: "👤"
                            font.pixelSize: 18
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: userMenu.open()
                        }

                        Menu {
                            id: userMenu
                            MenuItem {
                                text: "Profile Settings"
                                onTriggered: openSettings()
                            }
                            MenuItem {
                                text: "Switch User"
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: "Sign Out"
                            }
                        }
                    }
                }
            }
        }

        // MAIN CONTENT - Three column layout
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // LEFT PANEL - Quick Actions & Stats
            Rectangle {
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                color: theme.panel
                border { color: theme.border; width: 1 }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Quick Actions Header
                    Rectangle {
                        Layout.fillWidth: true
                        height: 50
                        color: theme.panelDark

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 15

                            Text {
                                text: "QUICK ACTIONS"
                                color: theme.textSecondary
                                font.bold: true
                                font.pixelSize: 11
                                font.capitalization: Font.AllUppercase
                                font.family: "JetBrains Mono"
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: "+"
                                flat: true
                                onClicked: customizeQuickActions()
                                ToolTip.text: "Customize quick actions"
                            }
                        }
                    }

                    // Quick Actions List
                    ListView {
                        id: quickActionsView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: quickActionsModel
                        spacing: 2
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: QuickActionItem {
                            width: quickActionsView.width
                            action: modelData
                        }
                    }

                    // Stats Panel
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 180
                        color: theme.panelDark
                        border { color: theme.border; width: 1 }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 15
                            spacing: 10

                            Text {
                                text: "SYSTEM STATS"
                                color: theme.textSecondary
                                font.bold: true
                                font.pixelSize: 11
                                font.capitalization: Font.AllUppercase
                                font.family: "JetBrains Mono"
                            }

                            StatItem {
                                label: "Total Files"
                                value: stats.totalFiles || "0"
                                icon: "📁"
                                trend: "+12"
                                Layout.fillWidth: true
                            }

                            StatItem {
                                label: "Total Playtime"
                                value: stats.totalPlaytime || "0h"
                                icon: "⏱️"
                                trend: "+8h"
                                Layout.fillWidth: true
                            }

                            StatItem {
                                label: "Disk Usage"
                                value: formatBytes(totalFileSize)
                                icon: "💾"
                                progress: totalFileSize / (1024 * 1024 * 1024) // Show as GB
                                Layout.fillWidth: true
                            }

                            StatItem {
                                label: "Last Updated"
                                value: formatTimeAgo(lastUpdated)
                                icon: "🕒"
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // Recent Projects
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        color: theme.panel

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 15
                            spacing: 8

                            Text {
                                text: "RECENT PROJECTS"
                                color: theme.textSecondary
                                font.bold: true
                                font.pixelSize: 11
                                font.capitalization: Font.AllUppercase
                                font.family: "JetBrains Mono"
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                model: recentProjectsModel
                                spacing: 4
                                clip: true

                                delegate: Rectangle {
                                    width: parent.width
                                    height: 30
                                    color: mouseArea.containsMouse ? theme.panelLight : "transparent"
                                    radius: 4

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 5
                                        spacing: 8

                                        Text {
                                            text: getProjectIcon(modelData.type)
                                            font.pixelSize: 12
                                        }

                                        Text {
                                            text: modelData.name
                                            color: theme.textPrimary
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }

                                    MouseArea {
                                        id: mouseArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: openProject(modelData.path)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // CENTER PANEL - Apps Grid with Enhanced View
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.background

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // View Controls
                    Rectangle {
                        Layout.fillWidth: true
                        height: 50
                        color: theme.panel
                        border { color: theme.border; width: 1 }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 15
                            spacing: 10

                            Text {
                                text: "APPLICATIONS"
                                color: theme.textPrimary
                                font.bold: true
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }

                            // View mode toggle
                            Row {
                                spacing: 2
                                Layout.alignment: Qt.AlignVCenter

                                Button {
                                    text: "☰"
                                    checked: launcherSettings.viewMode === "list"
                                    checkable: true
                                    onClicked: launcherSettings.viewMode = "list"
                                    ToolTip.text: "List view"
                                }

                                Button {
                                    text: "⏹"
                                    checked: launcherSettings.viewMode === "grid"
                                    checkable: true
                                    onClicked: launcherSettings.viewMode = "grid"
                                    ToolTip.text: "Grid view"
                                }

                                Button {
                                    text: "📋"
                                    checked: launcherSettings.viewMode === "detailed"
                                    checkable: true
                                    onClicked: launcherSettings.viewMode = "detailed"
                                    ToolTip.text: "Detailed view"
                                }
                            }

                            // Sort dropdown
                            ComboBox {
                                model: ["Most Used", "Recently Used", "Alphabetical", "Category"]
                                currentIndex: 0
                                width: 150
                                onActivated: sortApps(currentText.toLowerCase())
                            }

                            Button {
                                text: "🔄 Refresh"
                                onClicked: refreshAllData()
                                ToolTip.text: "Refresh all data"
                            }
                        }
                    }

                    // Apps Display Area
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "transparent"

                        // Grid View
                        GridView {
                            id: appsGridView
                            anchors.fill: parent
                            anchors.margins: 20
                            cellWidth: 180
                            cellHeight: 180
                            visible: launcherSettings.viewMode === "grid"
                            model: filteredApps.length > 0 ? filteredApps : appModel
                            clip: true
                            cacheBuffer: 1000

                            delegate: AppCard {
                                width: appsGridView.cellWidth - 10
                                height: appsGridView.cellHeight - 10
                                appData: modelData
                                onClicked: launchApp(modelData.id)
                                onPinnedChanged: toggleFavorite(modelData.id)
                            }
                        }

                        // List View
                        ListView {
                            id: appsListView
                            anchors.fill: parent
                            anchors.margins: 20
                            visible: launcherSettings.viewMode === "list"
                            model: filteredApps.length > 0 ? filteredApps : appModel
                            spacing: 2
                            clip: true
                            cacheBuffer: 1000

                            delegate: AppListItem {
                                width: appsListView.width
                                appData: modelData
                                onClicked: launchApp(modelData.id)
                                onPinnedChanged: toggleFavorite(modelData.id)
                            }
                        }

                        // Detailed View
                        TableView {
                            id: appsTableView
                            anchors.fill: parent
                            anchors.margins: 20
                            visible: launcherSettings.viewMode === "detailed"
                            model: filteredApps.length > 0 ? filteredApps : appModel
                            clip: true
                            columnSpacing: 1
                            rowSpacing: 1

                            columnWidthProvider: function(column) {
                                switch(column) {
                                    case 0: return 40  // Icon
                                    case 1: return 200 // Name
                                    case 2: return 300 // Description
                                    case 3: return 100 // Category
                                    case 4: return 80  // Usage
                                    case 5: return 120 // Last Used
                                    default: return 100
                                }
                            }

                            delegate: Rectangle {
                                implicitWidth: 100
                                implicitHeight: 40
                                color: row % 2 === 0 ? theme.panel : theme.panelDark

                                Text {
                                    anchors.centerIn: parent
                                    text: {
                                        switch(column) {
                                            case 0: return modelData.icon
                                            case 1: return modelData.name
                                            case 2: return modelData.description
                                            case 3: return modelData.category
                                            case 4: return (launcherSettings.appUsageCount[modelData.id] || 0) + " uses"
                                            case 5: return formatTimeAgo(getLastUsed(modelData.id))
                                            default: return ""
                                        }
                                    }
                                    color: theme.textPrimary
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    width: parent.width - 10
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        if (column === 0) {
                                            launchApp(modelData.id)
                                        }
                                    }
                                }
                            }
                        }

                        // Empty state
                        Rectangle {
                            anchors.centerIn: parent
                            width: 400
                            height: 200
                            color: "transparent"
                            visible: appModel.count === 0

                            Column {
                                anchors.centerIn: parent
                                spacing: 20

                                Text {
                                    text: "📱"
                                    font.pixelSize: 48
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }

                                Text {
                                    text: "No Applications Available"
                                    color: theme.textSecondary
                                    font.pixelSize: 16
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }

                                Text {
                                    text: "Applications will appear here after initialization"
                                    color: theme.textDisabled
                                    font.pixelSize: 12
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                            }
                        }

                        // Search empty state
                        Rectangle {
                            anchors.centerIn: parent
                            width: 400
                            height: 200
                            color: "transparent"
                            visible: searchQuery !== "" && filteredApps.length === 0

                            Column {
                                anchors.centerIn: parent
                                spacing: 20

                                Text {
                                    text: "🔍"
                                    font.pixelSize: 48
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }

                                Text {
                                    text: "No Results Found"
                                    color: theme.textSecondary
                                    font.pixelSize: 16
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }

                                Text {
                                    text: "No applications match \"" + searchQuery + "\""
                                    color: theme.textDisabled
                                    font.pixelSize: 12
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }

                                Button {
                                    text: "Clear Search"
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    onClicked: searchQuery = ""
                                }
                            }
                        }
                    }
                }
            }

            // RIGHT PANEL - Recent Files & Pinned Items
            Rectangle {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                color: theme.panel
                border { color: theme.border; width: 1 }

                TabBar {
                    id: rightTabBar
                    width: parent.width
                    height: 40
                    background: Rectangle { color: theme.panelDark }

                    TabButton {
                        text: "📁 Recent Files"
                        width: parent.width / 3
                    }
                    TabButton {
                        text: "📌 Pinned"
                        width: parent.width / 3
                    }
                    TabButton {
                        text: "📊 Analytics"
                        width: parent.width / 3
                    }
                }

                StackLayout {
                    anchors.top: rightTabBar.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    currentIndex: rightTabBar.currentIndex

                    // Recent Files Tab
                    ColumnLayout {
                        spacing: 0

                        // Toolbar
                        Rectangle {
                            Layout.fillWidth: true
                            height: 40
                            color: theme.panelDark

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 5

                                Button {
                                    text: "Open File"
                                    onClicked: fileDialog.open()
                                    Layout.fillWidth: true
                                }

                                Button {
                                    text: "Clear All"
                                    onClicked: clearRecentFiles()
                                    enabled: recentFilesModel.count > 0
                                }
                            }
                        }

                        // File List
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true

                            ColumnLayout {
                                width: parent.width
                                spacing: 2

                                Repeater {
                                    model: recentFilesModel

                                    delegate: FileItem {
                                        Layout.fillWidth: true
                                        fileInfo: modelData
                                        showActions: true
                                        onClicked: openFile(modelData.path)
                                        onPinned: togglePinnedFile(modelData)
                                        onRemove: removeRecentFile(index)
                                    }
                                }

                                // Empty state
                                Rectangle {
                                    width: parent.width
                                    height: 100
                                    color: "transparent"
                                    visible: recentFilesModel.count === 0

                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 10

                                        Text {
                                            text: "📄"
                                            font.pixelSize: 32
                                            anchors.horizontalCenter: parent.horizontalCenter
                                        }

                                        Text {
                                            text: "No Recent Files"
                                            color: theme.textDisabled
                                            anchors.horizontalCenter: parent.horizontalCenter
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Pinned Items Tab
                    ColumnLayout {
                        spacing: 0

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true

                            ColumnLayout {
                                width: parent.width
                                spacing: 2

                                Repeater {
                                    model: pinnedFilesModel

                                    delegate: FileItem {
                                        Layout.fillWidth: true
                                        fileInfo: modelData
                                        isPinned: true
                                        onClicked: openFile(modelData.path)
                                        onUnpin: removePinnedFile(modelData.path)
                                    }
                                }

                                // Empty state
                                Rectangle {
                                    width: parent.width
                                    height: 100
                                    color: "transparent"
                                    visible: pinnedFilesModel.count === 0

                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 10

                                        Text {
                                            text: "📌"
                                            font.pixelSize: 32
                                            anchors.horizontalCenter: parent.horizontalCenter
                                        }

                                        Text {
                                            text: "No Pinned Files"
                                            color: theme.textDisabled
                                            anchors.horizontalCenter: parent.horizontalCenter
                                        }

                                        Text {
                                            text: "Pin important files for quick access"
                                            color: theme.textDisabled
                                            font.pixelSize: 10
                                            anchors.horizontalCenter: parent.horizontalCenter
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Analytics Tab
                    ScrollView {
                        clip: true

                        ColumnLayout {
                            width: parent.width
                            spacing: 15
                            padding: 15

                            // Usage Statistics
                            GroupBox {
                                title: "📊 Usage Statistics"
                                Layout.fillWidth: true

                                GridLayout {
                                    columns: 2
                                    columnSpacing: 20
                                    rowSpacing: 10
                                    width: parent.width

                                    StatItem {
                                        label: "Total Sessions"
                                        value: launcherSettings.launchCount
                                        icon: "🚀"
                                        Layout.fillWidth: true
                                    }

                                    StatItem {
                                        label: "Apps Launched"
                                        value: Object.keys(launcherSettings.appUsageCount || {}).length
                                        icon: "📱"
                                        Layout.fillWidth: true
                                    }

                                    StatItem {
                                        label: "Files Opened"
                                        value: recentFilesModel.count
                                        icon: "📄"
                                        Layout.fillWidth: true
                                    }

                                    StatItem {
                                        label: "Total Playtime"
                                        value: "142h 15m"
                                        icon: "⏱️"
                                        Layout.fillWidth: true
                                    }
                                }
                            }

                            // Most Used Apps
                            GroupBox {
                                title: "🏆 Most Used Applications"
                                Layout.fillWidth: true

                                ColumnLayout {
                                    width: parent.width
                                    spacing: 5

                                    Repeater {
                                        model: getTopApps(5)

                                        delegate: UsageItem {
                                            appName: modelData.name
                                            usageCount: modelData.usage
                                            usagePercent: modelData.percent
                                            icon: modelData.icon
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }

                            // Activity Timeline
                            GroupBox {
                                title: "📈 Recent Activity"
                                Layout.fillWidth: true

                                ColumnLayout {
                                    width: parent.width
                                    spacing: 8

                                    Repeater {
                                        model: getRecentActivity(5)

                                        delegate: ActivityItem {
                                            action: modelData.action
                                            target: modelData.target
                                            timestamp: modelData.timestamp
                                            icon: modelData.icon
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // FOOTER - Status Bar
        Rectangle {
            Layout.fillWidth: true
            height: 28
            color: theme.panelDark
            border { color: theme.border; width: 1 }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 15
                anchors.rightMargin: 15
                spacing: 20

                // Status message
                Text {
                    id: statusText
                    text: "Ready"
                    color: theme.textSecondary
                    font.pixelSize: 11
                    Layout.fillWidth: true
                }

                // App count
                Text {
                    text: filteredApps.length > 0 ?
                    filteredApps.length + " apps" :
                    appModel.count + " apps"
                    color: theme.textSecondary
                    font.pixelSize: 11
                }

                // Memory usage
                Text {
                    text: "Memory: " + Math.round(memoryUsage) + "%"
                    color: memoryUsage > 80 ? theme.warning : theme.textSecondary
                    font.pixelSize: 11
                }

                // Battery
                Text {
                    text: "⚡ " + Math.round(batteryLevel) + "%"
                    color: batteryLevel < 20 ? theme.error : theme.textSecondary
                    font.pixelSize: 11
                    visible: batteryLevel >= 0
                }

                // Time
                Text {
                    text: Qt.formatDateTime(new Date(), "hh:mm AP")
                    color: theme.textSecondary
                    font.pixelSize: 11
                    font.family: "monospace"
                }
            }
        }
    }

    // ============================================
    // DIALOGS
    // ============================================

    // Welcome Dialog
    Dialog {
        id: welcomeDialog
        title: "Welcome to Aegis Media Suite"
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 500
        height: 400
        modal: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 20

            // Header
            ColumnLayout {
                spacing: 5
                Layout.alignment: Qt.AlignHCenter

                Text {
                    text: "🎵 Welcome!"
                    font.pixelSize: 24
                    font.bold: true
                    color: theme.accent
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: "Aegis Media Suite v2.1"
                    color: theme.textSecondary
                    font.pixelSize: 12
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            // Features
            ColumnLayout {
                spacing: 10
                Layout.fillWidth: true

                FeatureItem {
                    icon: "🎵"
                    title: "Professional Audio Tools"
                    description: "Studio-grade audio editing and processing"
                }

                FeatureItem {
                    icon: "🎬"
                    title: "Video Production Suite"
                    description: "Complete video editing and effects pipeline"
                }

                FeatureItem {
                    icon: "🔌"
                    title: "Audio Middleware"
                    description: "Route audio between applications in real-time"
                }

                FeatureItem {
                    icon: "🔄"
                    title: "Smart Conversion"
                    description: "Batch convert media files with presets"
                }
            }

            // Options
            CheckBox {
                id: showWelcomeCheck
                text: "Show welcome screen on startup"
                checked: launcherSettings.showWelcome
                onCheckedChanged: launcherSettings.showWelcome = checked
                Layout.alignment: Qt.AlignHCenter
            }
        }

        onAccepted: {
            console.log("Welcome dialog accepted")
        }
    }

    // File Dialog
    Platform.FileDialog {
        id: fileDialog
        title: "Open Media File"
        fileMode: Platform.FileDialog.OpenFiles
        nameFilters: [
            "All media files (*.mp3 *.wav *.flac *.ogg *.m4a *.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm)",
            "Audio files (*.mp3 *.wav *.flac *.ogg *.m4a *.aac *.aiff *.wma)",
            "Video files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v)",
            "Project files (*.aegis *.aep *.aegisedit *.aegisburn *.aegisconv)",
            "All files (*)"
        ]
        onAccepted: {
            selectedFiles.forEach(function(file) {
                addRecentFile(file)
            })
        }
    }

    // Context Menu
    Menu {
        id: contextMenu
        MenuItem {
            text: "Open"
            onTriggered: if (contextMenu.fileInfo) openFile(contextMenu.fileInfo.path)
        }
        MenuItem {
            text: "Open With..."
            onTriggered: openWithDialog.open()
        }
        MenuItem {
            text: "Pin to Launcher"
            onTriggered: if (contextMenu.fileInfo) togglePinnedFile(contextMenu.fileInfo)
        }
        MenuSeparator {}
        MenuItem {
            text: "Show in Folder"
            onTriggered: showInFolder(contextMenu.fileInfo.path)
        }
        MenuItem {
            text: "Properties"
            onTriggered: showFileProperties(contextMenu.fileInfo)
        }
        MenuSeparator {}
        MenuItem {
            text: "Remove from List"
            onTriggered: if (contextMenu.fileInfo) removeRecentFileByPath(contextMenu.fileInfo.path)
        }
    }

    // ============================================
    // COMPONENTS
    // ============================================

    // App Card Component (Grid View)
    component AppCard: Rectangle {
        property var appData: null
        property bool isPinned: launcherSettings.favoriteApps.includes(appData.id)
        signal clicked()
        signal pinnedChanged(bool pinned)

        width: 170
        height: 170
        radius: 12
        color: cardMouse.containsMouse ? theme.card : theme.panel
        border.color: isPinned ? theme.accent : (cardMouse.containsMouse ? theme.accent : "transparent")
        border.width: 2

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 10

            // App Icon
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 60
                height: 60
                radius: 12
                color: appData.color || theme.accent

                Text {
                    anchors.centerIn: parent
                    text: appData.icon || "📱"
                    font.pixelSize: 24
                }

                // Usage badge
                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    width: 20
                    height: 20
                    radius: 10
                    color: theme.accent
                    visible: appData.usageCount > 0

                    Text {
                        anchors.centerIn: parent
                        text: appData.usageCount > 9 ? "9+" : appData.usageCount
                        color: "white"
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
            }

            // App Info
            ColumnLayout {
                spacing: 4
                Layout.fillWidth: true

                Text {
                    text: appData.name
                    color: theme.textPrimary
                    font.bold: true
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: appData.description
                    color: theme.textSecondary
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.maximumHeight: 30
                }
            }

            // Category tag
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: Math.min(implicitWidth, parent.width)
                height: 20
                radius: 4
                color: getCategoryColor(appData.category)

                Text {
                    anchors.centerIn: parent
                    text: appData.category
                    color: "white"
                    font.pixelSize: 9
                    font.bold: true
                    padding: 4
                }
            }
        }

        // Pin button
        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 10
            width: 24
            height: 24
            radius: 12
            color: isPinned ? theme.accent : theme.panel
            border.color: theme.border

            Text {
                anchors.centerIn: parent
                text: isPinned ? "📌" : "📎"
                font.pixelSize: 12
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    isPinned = !isPinned
                    pinnedChanged(isPinned)
                }
            }
        }

        MouseArea {
            id: cardMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.clicked()
            onDoubleClicked: parent.clicked()
        }

        // Launch animation
        SequentialAnimation on scale {
            running: false
            id: launchAnimation
            NumberAnimation { to: 0.95; duration: 100 }
            NumberAnimation { to: 1.0; duration: 100 }
        }

        function launch() {
            launchAnimation.start()
            clicked()
        }
    }

    // App List Item Component (List View)
    component AppListItem: Rectangle {
        property var appData: null
        property bool isPinned: launcherSettings.favoriteApps.includes(appData.id)
        signal clicked()
        signal pinnedChanged(bool pinned)

        height: 60
        radius: 8
        color: listMouse.containsMouse ? theme.card : "transparent"
        border.color: isPinned ? theme.accent : "transparent"
        border.width: 2

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 15

            // Icon
            Rectangle {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                radius: 8
                color: appData.color || theme.accent

                Text {
                    anchors.centerIn: parent
                    text: appData.icon || "📱"
                    font.pixelSize: 18
                }
            }

            // Info
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true

                Text {
                    text: appData.name
                    color: theme.textPrimary
                    font.bold: true
                    font.pixelSize: 14
                }

                Text {
                    text: appData.description
                    color: theme.textSecondary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            // Usage
            Text {
                text: appData.usageCount > 0 ? appData.usageCount + " uses" : "Never used"
                color: theme.textDisabled
                font.pixelSize: 10
                Layout.preferredWidth: 80
            }

            // Pin button
            Button {
                text: isPinned ? "📌" : "📎"
                flat: true
                onClicked: {
                    isPinned = !isPinned
                    pinnedChanged(isPinned)
                }
            }
        }

        MouseArea {
            id: listMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.clicked()
        }
    }

    // Quick Action Item Component
    component QuickActionItem: Rectangle {
        property var action: null

        height: 50
        radius: 8
        color: qaMouse.containsMouse ? theme.panelLight : "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12

            Text {
                text: action.icon || "⚡"
                font.pixelSize: 16
            }

            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true

                Text {
                    text: action.title
                    color: theme.textPrimary
                    font.bold: true
                    font.pixelSize: 12
                }

                Text {
                    text: action.description
                    color: theme.textSecondary
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
        }

        MouseArea {
            id: qaMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: executeAction(action)
        }
    }

    // File Item Component
    component FileItem: Rectangle {
        property var fileInfo: null
        property bool isPinned: false
        property bool showActions: false
        signal clicked()
        signal pinned()
        signal unpin()
        signal remove()

        height: 60
        radius: 4
        color: fileMouse.containsMouse ? theme.panelLight : "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 12

            // File icon
            Rectangle {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                radius: 6
                color: getFileColor(fileInfo.type)

                Text {
                    anchors.centerIn: parent
                    text: getFileIcon(fileInfo.type)
                    font.pixelSize: 18
                    color: "white"
                }
            }

            // File info
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true

                Text {
                    text: fileInfo.name
                    color: theme.textPrimary
                    font.bold: true
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }

                Text {
                    text: fileInfo.path
                    color: theme.textDisabled
                    font.pixelSize: 10
                    elide: Text.ElideLeft
                }

                RowLayout {
                    Text {
                        text: fileInfo.size || "N/A"
                        color: theme.textSecondary
                        font.pixelSize: 10
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: formatTimeAgo(fileInfo.lastAccessed)
                        color: theme.textDisabled
                        font.pixelSize: 9
                    }
                }
            }

            // Action buttons
            Row {
                spacing: 5
                visible: showActions

                Button {
                    text: isPinned ? "📌" : "📎"
                    flat: true
                    onClicked: isPinned ? unpin() : pinned()
                    ToolTip.text: isPinned ? "Unpin" : "Pin"
                }

                Button {
                    text: "✕"
                    flat: true
                    onClicked: remove()
                    ToolTip.text: "Remove"
                }
            }
        }

        MouseArea {
            id: fileMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.clicked()
            onPressAndHold: {
                contextMenu.fileInfo = fileInfo
                contextMenu.popup()
            }
        }
    }

    // Stat Item Component
    component StatItem: Rectangle {
        property string label: ""
        property string value: ""
        property string icon: ""
        property string trend: ""
        property real progress: 0

        height: 40
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            spacing: 10

            Text {
                text: icon
                font.pixelSize: 16
            }

            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true

                Text {
                    text: label
                    color: theme.textSecondary
                    font.pixelSize: 10
                }

                RowLayout {
                    Text {
                        text: value
                        color: theme.textPrimary
                        font.bold: true
                        font.pixelSize: 14
                    }

                    Text {
                        text: trend ? "(" + trend + ")" : ""
                        color: trend.startsWith("+") ? theme.success : theme.error
                        font.pixelSize: 10
                        visible: trend !== ""
                    }

                    Item { Layout.fillWidth: true }
                }

                // Progress bar
                Rectangle {
                    Layout.fillWidth: true
                    height: 3
                    color: theme.panel
                    visible: progress > 0

                    Rectangle {
                        width: parent.width * Math.min(progress, 1)
                        height: parent.height
                        color: theme.accent
                    }
                }
            }
        }
    }

    // Usage Item Component
    component UsageItem: Rectangle {
        property string appName: ""
        property int usageCount: 0
        property real usagePercent: 0
        property string icon: ""

        height: 40
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            spacing: 10

            Text {
                text: icon
                font.pixelSize: 16
            }

            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true

                RowLayout {
                    Text {
                        text: appName
                        color: theme.textPrimary
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }

                    Text {
                        text: usageCount + " uses"
                        color: theme.textSecondary
                        font.pixelSize: 10
                    }
                }

                // Usage bar
                Rectangle {
                    Layout.fillWidth: true
                    height: 6
                    color: theme.panel
                    radius: 3

                    Rectangle {
                        width: parent.width * (usagePercent / 100)
                        height: parent.height
                        color: theme.accent
                        radius: 3
                    }
                }
            }
        }
    }

    // Activity Item Component
    component ActivityItem: Rectangle {
        property string action: ""
        property string target: ""
        property string timestamp: ""
        property string icon: ""

        height: 40
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            spacing: 10

            Text {
                text: icon
                font.pixelSize: 16
            }

            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true

                Text {
                    text: action + " " + target
                    color: theme.textPrimary
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Text {
                    text: formatTimeAgo(timestamp)
                    color: theme.textDisabled
                    font.pixelSize: 10
                }
            }
        }
    }

    // Feature Item Component (for welcome dialog)
    component FeatureItem: RowLayout {
        property string icon: ""
        property string title: ""
        property string description: ""

        spacing: 10

        Text {
            text: icon
            font.pixelSize: 20
        }

        ColumnLayout {
            spacing: 2

            Text {
                text: title
                color: theme.textPrimary
                font.bold: true
                font.pixelSize: 13
            }

            Text {
                text: description
                color: theme.textSecondary
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    // ============================================
    // FUNCTIONS
    // ============================================

    function loadRecentData() {
        console.log("📁 Loading recent data...")

        // Clear models
        recentFilesModel.clear()
        recentPlaylistsModel.clear()
        recentProjectsModel.clear()
        pinnedFilesModel.clear()

        // Load from settings
        var files = launcherSettings.recentFiles || []
        var playlists = launcherSettings.recentPlaylists || []
        var projects = launcherSettings.recentProjects || []
        var pinned = launcherSettings.pinnedFiles || []

        // Populate models
        files.slice(0, 50).forEach(file => recentFilesModel.append(file))
        playlists.slice(0, 20).forEach(playlist => recentPlaylistsModel.append(playlist))
        projects.slice(0, 10).forEach(project => recentProjectsModel.append(project))
        pinned.forEach(file => pinnedFilesModel.append(file))

        console.log("✅ Loaded", files.length, "recent files,", pinned.length, "pinned files")
    }

    function loadApps() {
        console.log("📱 Loading applications...")
        appModel.clear()

        var apps = [
            {
                id: "player",
                name: "Media Player",
                icon: "🎵",
                description: "Professional audio/video playback with advanced controls",
                category: "media",
                color: "#2196F3",
                usageCount: launcherSettings.appUsageCount?.player || 0,
                keywords: ["play", "video", "audio", "media", "stream"]
            },
            {
                id: "converter",
                name: "Media Converter",
                icon: "🔄",
                description: "Batch convert audio/video formats with presets",
                category: "tools",
                color: "#9C27B0",
                usageCount: launcherSettings.appUsageCount?.converter || 0,
                keywords: ["convert", "format", "batch", "encode"]
            },
            {
                id: "middleware",
                name: "Audio Middleware",
                icon: "🔌",
                description: "Route audio between applications in real-time",
                category: "audio",
                color: "#FF9800",
                usageCount: launcherSettings.appUsageCount?.middleware || 0,
                keywords: ["audio", "route", "bridge", "middleware", "stream"]
            },
            {
                id: "audioeditor",
                name: "Audio Editor",
                icon: "✏️",
                description: "Professional audio editing and effects",
                category: "audio",
                color: "#4CAF50",
                usageCount: launcherSettings.appUsageCount?.audioeditor || 0,
                keywords: ["edit", "audio", "effects", "mix", "record"]
            },
            {
                id: "videoeditor",
                name: "Video Editor",
                icon: "🎬",
                description: "Advanced video editing and production",
                category: "video",
                color: "#F44336",
                usageCount: launcherSettings.appUsageCount?.videoeditor || 0,
                keywords: ["video", "edit", "effects", "production", "timeline"]
            },
            {
                id: "burner",
                name: "Disc Burner",
                icon: "📀",
                description: "Burn CDs, DVDs, and Blu-ray discs",
                category: "tools",
                color: "#FF9800",
                usageCount: launcherSettings.appUsageCount?.burner || 0,
                keywords: ["burn", "disc", "cd", "dvd", "bluray"]
            },
            {
                id: "labelmaker",
                name: "Label Designer",
                icon: "📝",
                description: "Design professional disc labels and covers",
                category: "design",
                color: "#9C27B0",
                usageCount: launcherSettings.appUsageCount?.labelmaker || 0,
                keywords: ["design", "label", "cover", "artwork"]
            },
            {
                id: "karaoke",
                name: "Karaoke",
                icon: "🎤",
                description: "Karaoke singing with scoring and effects",
                category: "entertainment",
                color: "#E91E63",
                usageCount: launcherSettings.appUsageCount?.karaoke || 0,
                keywords: ["karaoke", "sing", "music", "entertainment"]
            },
            {
                id: "djmixer",
                name: "DJ Mixer",
                icon: "🎧",
                description: "Professional DJ mixing and effects",
                category: "audio",
                color: "#00BCD4",
                usageCount: launcherSettings.appUsageCount?.djmixer || 0,
                keywords: ["dj", "mix", "music", "effects", "turntable"]
            },
            {
                id: "modtracker",
                name: "Tracker",
                icon: "🎚️",
                description: "MOD tracker music composition",
                category: "audio",
                color: "#3F51B5",
                usageCount: launcherSettings.appUsageCount?.modtracker || 0,
                keywords: ["tracker", "music", "compose", "mod", "midi"]
            }
        ]

        apps.forEach(app => appModel.append(app))
        filteredApps = apps.slice() // Copy for filtering

        // Update favorites
        updateFavorites()

        console.log("✅ Loaded", apps.length, "applications")
    }

    function loadQuickActions() {
        quickActionsModel.clear()

        var actions = [
            {
                id: "open_file",
                title: "Open File",
                icon: "📁",
                description: "Open media file",
                action: function() { fileDialog.open() }
            },
            {
                id: "quick_convert",
                title: "Quick Convert",
                icon: "🔄",
                description: "Convert file format",
                action: function() {
                    rootWindow.showConverter()
                    // Would open converter with file dialog
                }
            },
            {
                id: "record_audio",
                title: "Record Audio",
                icon: "🎙️",
                description: "Start audio recording",
                action: function() { startRecording() }
            },
            {
                id: "take_screenshot",
                title: "Take Screenshot",
                icon: "📸",
                description: "Capture screen",
                action: function() { takeScreenshot() }
            },
            {
                id: "audio_settings",
                title: "Audio Settings",
                icon: "⚙️",
                description: "Adjust audio preferences",
                action: function() { openAudioSettings() }
            }
        ]

        actions.forEach(action => quickActionsModel.append(action))
    }

    function calculateStats() {
        stats = {
            totalFiles: recentFilesModel.count,
            totalPlaytime: "142h 15m", // This would be calculated from history
            diskUsage: formatBytes(totalFileSize),
            favoriteApps: launcherSettings.favoriteApps.length
        }

        // Calculate total file size (simulated)
        totalFileSize = recentFilesModel.count * 1024 * 1024 * 5 // 5MB average

        lastUpdated = new Date()
    }

    function filterApps() {
        if (!searchQuery && selectedCategory === "all") {
            filteredApps = []
            return
        }

        var filtered = []
        for (var i = 0; i < appModel.count; i++) {
            var app = appModel.get(i)
            var matches = true

            // Search filter
            if (searchQuery) {
                var query = searchQuery.toLowerCase()
                matches = app.name.toLowerCase().includes(query) ||
                app.description.toLowerCase().includes(query) ||
                app.keywords.some(keyword => keyword.includes(query))
            }

            // Category filter
            if (matches && selectedCategory !== "all") {
                matches = app.category === selectedCategory
            }

            if (matches) {
                filtered.push(app)
            }
        }

        filteredApps = filtered
        console.log("🔍 Filtered to", filtered.length, "apps")
    }

    function sortApps(criteria) {
        var apps = []
        for (var i = 0; i < appModel.count; i++) {
            apps.push(appModel.get(i))
        }

        apps.sort(function(a, b) {
            switch(criteria) {
                case "name":
                    return a.name.localeCompare(b.name)
                case "usage":
                    return (b.usageCount || 0) - (a.usageCount || 0)
                case "recent":
                    // This would use last used timestamp
                    return 0
                case "category":
                    return a.category.localeCompare(b.category)
                default:
                    return 0
            }
        })

        appModel.clear()
        apps.forEach(app => appModel.append(app))

        console.log("📊 Sorted apps by", criteria)
    }

    function launchApp(appId) {
        console.log("🚀 Launching app:", appId)

        // Update usage count
        var usage = launcherSettings.appUsageCount || {}
        usage[appId] = (usage[appId] || 0) + 1
        launcherSettings.appUsageCount = usage
        launcherSettings.lastUsedApp = appId

        // Update recent apps
        addToRecentApps(appId)

        // Launch app via main window
        switch(appId) {
            case "player": rootWindow.showPlayer(); break
            case "converter": rootWindow.showConverter(); break
            case "middleware": rootWindow.showMiddleware(); break
            case "audioeditor": rootWindow.showAudioEditor(); break
            case "videoeditor": rootWindow.showVideoEditor(); break
            case "burner": rootWindow.showBurner(); break
            case "labelmaker": rootWindow.showLabelMaker(); break
            case "karaoke": rootWindow.showKaraoke(); break
            case "djmixer": rootWindow.showDJMix(); break
            case "modtracker": rootWindow.showModTracker(); break
            default:
                console.warn("Unknown app:", appId)
                return
        }

        // Minimize launcher or keep it running
        launcherWindow.visibility = Window.Minimized

        // Show notification
        showNotification("🚀", "Launching " + getAppName(appId))
    }

    function addToRecentApps(appId) {
        var recent = launcherSettings.favoriteApps || []
        recent = recent.filter(id => id !== appId)
        recent.unshift(appId)

        // Keep only 10 most recent
        if (recent.length > 10) {
            recent.pop()
        }

        launcherSettings.favoriteApps = recent
    }

    function toggleFavorite(appId) {
        var favorites = launcherSettings.favoriteApps || []
        var index = favorites.indexOf(appId)

        if (index === -1) {
            favorites.push(appId)
            showNotification("📌", getAppName(appId) + " added to favorites")
        } else {
            favorites.splice(index, 1)
            showNotification("📎", getAppName(appId) + " removed from favorites")
        }

        launcherSettings.favoriteApps = favorites
        updateFavorites()
    }

    function updateFavorites() {
        // Update favorite status in app model
        for (var i = 0; i < appModel.count; i++) {
            var app = appModel.get(i)
            app.isFavorite = launcherSettings.favoriteApps.includes(app.id)
        }
    }

    function addRecentFile(filePath) {
        var fileInfo = {
            path: filePath,
            name: filePath.split('/').pop().split('\\').pop(),
            type: getFileType(filePath),
            size: formatFileSize(getFileSize(filePath)),
            duration: getFileDuration(filePath),
            lastAccessed: new Date().toISOString()
        }

        // Add to recent files
        var recent = launcherSettings.recentFiles || []
        recent = recent.filter(f => f.path !== filePath)
        recent.unshift(fileInfo)
        if (recent.length > 50) recent.pop()

            launcherSettings.recentFiles = recent
            loadRecentData()

            showNotification("📁", "Added to recent files: " + fileInfo.name)
    }

    function togglePinnedFile(fileInfo) {
        var pinned = launcherSettings.pinnedFiles || []
        var index = pinned.findIndex(f => f.path === fileInfo.path)

        if (index === -1) {
            pinned.push(fileInfo)
            showNotification("📌", "Pinned: " + fileInfo.name)
        } else {
            pinned.splice(index, 1)
            showNotification("📎", "Unpinned: " + fileInfo.name)
        }

        launcherSettings.pinnedFiles = pinned
        loadRecentData()
    }

    function removeRecentFile(index) {
        var recent = launcherSettings.recentFiles || []
        if (index >= 0 && index < recent.length) {
            var file = recent[index]
            recent.splice(index, 1)
            launcherSettings.recentFiles = recent
            loadRecentData()

            showNotification("🗑️", "Removed: " + file.name)
        }
    }

    function removeRecentFileByPath(filePath) {
        var recent = launcherSettings.recentFiles || []
        recent = recent.filter(f => f.path !== filePath)
        launcherSettings.recentFiles = recent
        loadRecentData()
    }

    function removePinnedFile(filePath) {
        var pinned = launcherSettings.pinnedFiles || []
        pinned = pinned.filter(f => f.path !== filePath)
        launcherSettings.pinnedFiles = pinned
        loadRecentData()
    }

    function clearRecentFiles() {
        launcherSettings.recentFiles = []
        loadRecentData()
        showNotification("🗑️", "All recent files cleared")
    }

    function openFile(filePath) {
        var type = getFileType(filePath)
        switch(type) {
            case "audio":
            case "video":
                rootWindow.showPlayer()
                if (rootWindow.coreRef) {
                    rootWindow.coreRef.loadFile(filePath)
                }
                break
            case "project":
                // Open in appropriate editor
                if (filePath.endsWith(".aegisedit")) {
                    rootWindow.showAudioEditor()
                } else if (filePath.endsWith(".aegisburn")) {
                    rootWindow.showBurner()
                } else if (filePath.endsWith(".aegisconv")) {
                    rootWindow.showConverter()
                } else if (filePath.endsWith(".aegismiddleware")) {
                    rootWindow.showMiddleware()
                }
                break
            default:
                Qt.openUrlExternally("file:///" + filePath)
        }

        launcherWindow.visibility = Window.Minimized
    }

    function openProject(projectPath) {
        // Load project and open appropriate editor
        var project = rootWindow.loadProject(projectPath)
        if (project) {
            showNotification("📁", "Opening project: " + project.name)
        }
    }

    function refreshAllData() {
        isLoading = true
        statusText.text = "Refreshing data..."

        setTimeout(function() {
            loadRecentData()
            loadApps()
            calculateStats()
            isLoading = false
            statusText.text = "Ready"

            showNotification("🔄", "Data refreshed successfully")
        }, 500)
    }

    function refreshData() {
        // Light refresh - just update stats
        calculateStats()
    }

    function saveLauncherState() {
        // Save current view state
        console.log("💾 Saving launcher state...")
    }

    function checkForUpdates() {
        // Check for launcher updates
        console.log("🔍 Checking for updates...")
    }

    function openSettings() {
        // Open launcher settings
        showNotification("⚙️", "Opening settings...")
    }

    function customizeQuickActions() {
        // Open quick actions customization
        showNotification("🔧", "Customize quick actions")
    }

    function startRecording() {
        rootWindow.startRecording()
    }

    function takeScreenshot() {
        rootWindow.takeScreenshot()
    }

    function openAudioSettings() {
        rootWindow.showAudioSettings()
    }

    function executeAction(action) {
        if (action && action.action) {
            action.action()
        }
    }

    function showInFolder(filePath) {
        // Show file in system file manager
        Qt.openUrlExternally("file:///" + filePath.substring(0, filePath.lastIndexOf('/')))
    }

    function showFileProperties(fileInfo) {
        // Show file properties dialog
        console.log("Properties for:", fileInfo.name)
    }

    function getTopApps(count) {
        var apps = []
        for (var i = 0; i < appModel.count; i++) {
            var app = appModel.get(i)
            apps.push({
                name: app.name,
                usage: app.usageCount || 0,
                percent: ((app.usageCount || 0) / getTotalUses()) * 100,
                      icon: app.icon
            })
        }

        apps.sort((a, b) => b.usage - a.usage)
        return apps.slice(0, count)
    }

    function getTotalUses() {
        var total = 0
        for (var i = 0; i < appModel.count; i++) {
            total += appModel.get(i).usageCount || 0
        }
        return total || 1 // Avoid division by zero
    }

    function getRecentActivity(count) {
        // Simulated recent activity
        return [
            { action: "Opened", target: "Media Player", timestamp: new Date(Date.now() - 300000).toISOString(), icon: "🎵" },
            { action: "Converted", target: "audio.mp3", timestamp: new Date(Date.now() - 600000).toISOString(), icon: "🔄" },
            { action: "Edited", target: "project.aegisedit", timestamp: new Date(Date.now() - 900000).toISOString(), icon: "✏️" },
            { action: "Burned", target: "Data Disc", timestamp: new Date(Date.now() - 1200000).toISOString(), icon: "📀" },
            { action: "Pinned", target: "video.mp4", timestamp: new Date(Date.now() - 1500000).toISOString(), icon: "📌" }
        ].slice(0, count)
    }

    function getLastUsed(appId) {
        // This would get actual last used timestamp
        return new Date(Date.now() - Math.random() * 86400000 * 7).toISOString() // Random within last week
    }

    function getAppName(appId) {
        for (var i = 0; i < appModel.count; i++) {
            if (appModel.get(i).id === appId) {
                return appModel.get(i).name
            }
        }
        return appId
    }

    function getFileType(filePath) {
        var ext = filePath.split('.').pop().toLowerCase()
        var audioExt = ['mp3','wav','flac','aac','ogg','m4a','aiff','wma']
        var videoExt = ['mp4','avi','mkv','mov','wmv','flv','webm','m4v']
        var imageExt = ['jpg','jpeg','png','gif','bmp','tiff','svg','webp']
        var projectExt = ['aegis','aep','aegisedit','aegisburn','aegisconv','aegismiddleware']

        if (audioExt.includes(ext)) return "audio"
            if (videoExt.includes(ext)) return "video"
                if (imageExt.includes(ext)) return "image"
                    if (projectExt.includes(ext)) return "project"
                        return "file"
    }

    function getFileIcon(type) {
        switch(type) {
            case "audio": return "🎵"
            case "video": return "🎬"
            case "image": return "🖼️"
            case "project": return "📁"
            default: return "📄"
        }
    }

    function getFileColor(type) {
        switch(type) {
            case "audio": return "#2196F3"
            case "video": return "#F44336"
            case "image": return "#4CAF50"
            case "project": return "#9C27B0"
            default: return "#607D8B"
        }
    }

    function getProjectIcon(type) {
        switch(type) {
            case "audioeditor": return "✏️"
            case "burner": return "📀"
            case "converter": return "🔄"
            case "middleware": return "🔌"
            default: return "📁"
        }
    }

    function getCategoryColor(category) {
        switch(category) {
            case "audio": return "#2196F3"
            case "video": return "#F44336"
            case "media": return "#9C27B0"
            case "tools": return "#FF9800"
            case "design": return "#4CAF50"
            case "entertainment": return "#E91E63"
            default: return "#607D8B"
        }
    }

    function formatFileSize(bytes) {
        if (bytes < 1024) return bytes + " B"
            if (bytes < 1024*1024) return (bytes/1024).toFixed(1) + " KB"
                if (bytes < 1024*1024*1024) return (bytes/(1024*1024)).toFixed(1) + " MB"
                    return (bytes/(1024*1024*1024)).toFixed(1) + " GB"
    }

    function formatTimeAgo(dateString) {
        var date = new Date(dateString)
        var now = new Date()
        var diff = now - date

        var minutes = Math.floor(diff / 60000)
        var hours = Math.floor(minutes / 60)
        var days = Math.floor(hours / 24)

        if (days > 7) {
            return date.toLocaleDateString()
        } else if (days > 0) {
            return days + " day" + (days > 1 ? "s" : "") + " ago"
        } else if (hours > 0) {
            return hours + " hour" + (hours > 1 ? "s" : "") + " ago"
        } else if (minutes > 0) {
            return minutes + " minute" + (minutes > 1 ? "s" : "") + " ago"
        } else {
            return "Just now"
        }
    }

    function formatBytes(bytes) {
        if (bytes === 0) return "0 B"
            var k = 1024
            var sizes = ["B", "KB", "MB", "GB", "TB"]
            var i = Math.floor(Math.log(bytes) / Math.log(k))
            return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + " " + sizes[i]
    }

    function showNotification(icon, message) {
        console.log("📢", icon, message)
        // Could add toast notification here
    }

    // Mock functions for file info
    function getFileSize(path) {
        return Math.random() * 1024 * 1024 * 100 // Random size up to 100MB
    }

    function getFileDuration(path) {
        var duration = Math.random() * 300 // Random duration up to 5 minutes
        var minutes = Math.floor(duration / 60)
        var seconds = Math.floor(duration % 60)
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    // System properties from main window
    property real memoryUsage: rootWindow.memoryUsage || 0
    property real batteryLevel: rootWindow.batteryLevel || 100
    property bool hasNotifications: false
}
