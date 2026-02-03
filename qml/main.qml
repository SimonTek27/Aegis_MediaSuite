// main.qml - Aegis Media Suite Core


import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.settings
import Qt.labs.platform as Platform

ApplicationWindow {
    id: rootWindow
    visible: true
    width: 1400
    height: 850
    minimumWidth: 1000
    minimumHeight: 600
    color: "#0a0a0a"
    title: "Aegis Media Suite" + (projectModified ? " *" : "")
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint |
    Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint |
    Qt.WindowCloseButtonHint

    // ============================================
    // 1. BACKEND CONNECTOR & SYSTEM MANAGER
    // ============================================

    // Backend references with type checking
    property var coreBackend: null
    property var audioBackend: null
    property var libraryBackend: null
    property var platformBackend: null
    property var discBackend: null
    property var burnerBackend: null
    property var audioEditorBackend: null  // Fixed: Renamed from editorBackend
    property var videoEditorBackend: null   // Added: Separate video editor
    property var djBackend: null
    property var karaokeBackend: null
    property var labelBackend: null
    property var converterBackend: null
    property var middlewareBackend: null    // Added: Audio middleware
    property var modtrackerBackend: null    // Added: Mod tracker backend
    property var musicBackend: null         // Added: Music notation backend

    // System state
    property bool backendReady: false
    property string connectionStatus: "Initializing..."
    property int initializationStep: 0
    property var failedComponents: []
    property var initializationErrors: []
    property bool criticalError: false
    property bool projectModified: false    // Added: Track project modifications

    // Application state
    property string currentMode: "launcher"
    property var modeHistory: []
    property int maxHistorySize: 15
    property var recentApps: []
    property var appUsageStats: ({})
    property var globalHotkeys: []
    property bool systemTrayEnabled: false
    property bool fullscreenMode: false

    // Session data
    property var sessionStartTime: new Date()
    property int sessionDuration: 0
    property var recentFiles: []
    property var recentPlaylists: []
    property var favoriteFiles: []
    property var clipboardData: null
    property var currentProject: null       // Added: Current project data

    // System metrics
    property real cpuUsage: 0
    property real memoryUsage: 0
    property real diskUsage: 0
    property int networkStatus: 0 // 0=offline, 1=slow, 2=good
    property bool lowPowerMode: false
    property real batteryLevel: 100         // Added: Battery monitoring
    property bool isCharging: true

    // Media playback state (shared across apps)
    property bool isPlaying: false
    property real mediaPosition: 0
    property real mediaDuration: 0
    property real mediaVolume: 100
    property string currentMediaFile: ""
    property bool mediaHasVideo: false

    // ============================================
    // 2. PERSISTENT SETTINGS MANAGER
    // ============================================

    Settings {
        id: globalSettings
        category: "Global"

        property string lastWorkingMode: "launcher"
        property bool enableHardwareAccel: true
        property bool lowLatencyMode: false
        property bool autoUpdates: true
        property bool analyticsEnabled: true
        property string uiTheme: "dark"
        property real uiScale: 1.0
        property string language: "en_US"
        property bool minimizeToTray: false
        property bool startMinimized: false
        property bool confirmExit: true      // Added: Exit confirmation
        property bool autoSave: true         // Added: Auto-save projects
        property int autoSaveInterval: 5     // Added: Auto-save interval (minutes)

        // Window state
        property int windowWidth: 1400
        property int windowHeight: 850
        property int windowX: -1
        property int windowY: -1
        property int windowState: 0

        // Recent data (limited to 100 items each)
        property var recentFiles: []
        property var recentPlaylists: []
        property var recentProjects: []
        property var favoriteApps: ["player", "editor", "burner", "converter", "launcher", "middleware"]

        // Usage statistics
        property var appUsageCount: ({})
        property int totalLaunches: 0
        property var lastUsedDate: ""

        // Hotkeys
        property var customHotkeys: ({})

        // Performance settings
        property bool enableVSync: true
        property int textureQuality: 2      // 0=Low, 1=Medium, 2=High
        property bool enableHardwareDecoding: true

        // Audio settings
        property string audioOutputDevice: "default"
        property string audioInputDevice: "default"
        property int audioBufferSize: 2048
        property bool audioExclusiveMode: false
    }

    Settings {
        id: audioSettings
        category: "Audio"
        property real masterVolume: 100
        property real outputVolume: 100
        property real inputVolume: 100
        property string outputDevice: "default"
        property string inputDevice: "default"
        property int sampleRate: 44100
        property int bufferSize: 2048
        property bool exclusiveMode: false
        property bool muteOnMinimize: false
        property string audioDriver: "default"  // Added: Audio driver selection
        property int latencyTarget: 50          // Added: Target latency in ms
    }

    Settings {
        id: videoSettings
        category: "Video"
        property string renderer: "auto"
        property bool hardwareDecoding: true
        property int frameSkip: 0
        property bool keepAspectRatio: true
        property bool deinterlace: false
        property int maxCacheSize: 512 // MB
        property bool highQualityScaling: true  // Added: HQ scaling
        property string subtitleEncoding: "UTF-8" // Added: Subtitle encoding
    }

    Settings {
        id: networkSettings
        category: "Network"
        property bool enableStreaming: true
        property int cacheSize: 100 // MB
        property bool proxyEnabled: false
        property string proxyHost: ""
        property int proxyPort: 8080
        property int downloadLimit: 0 // 0=unlimited
        property int uploadLimit: 0
        property bool useUPnP: true            // Added: UPnP support
        property int streamingQuality: 2       // Added: Streaming quality preset
    }

    Settings {
        id: projectSettings
        category: "Project"
        property string defaultProjectPath: StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/Aegis Projects"
        property bool autoCreateBackup: true
        property int backupInterval: 10       // Minutes between backups
        property int maxBackups: 5            // Maximum backup files to keep
        property string defaultTemplate: "empty"
    }

    // ============================================
    // 3. INITIALIZATION SYSTEM
    // ============================================

    Component.onCompleted: {
        console.log("🚀 Aegis Media Suite v2.1.1 Initializing...")
        console.log("Platform:", Qt.platform.os, "| Qt Version:", Qt.version)
        console.log("Screen:", Screen.width + "x" + Screen.height + "@" + Screen.pixelDensity.toFixed(1) + "dpi")

        // Check system requirements
        checkSystemRequirements()

        // Restore window state
        restoreWindowState()

        // Increment launch counter
        globalSettings.totalLaunches = (globalSettings.totalLaunches || 0) + 1
        globalSettings.lastUsedDate = new Date().toISOString()

        // Start initialization sequence
        initializeSystem()
    }

    onClosing: function(close) {
        handleWindowClosing(close)
    }

    function checkSystemRequirements() {
        var requirements = {
            "Qt Version": Qt.version >= "6.4.0",
            "OpenGL": true, // Would check actual OpenGL support
            "Audio": true,  // Would check audio device availability
            "Disk Space": true // Would check free disk space
        }

        for (var req in requirements) {
            if (!requirements[req]) {
                console.warn("⚠️ Requirement not met:", req)
            }
        }
    }

    function restoreWindowState() {
        if (globalSettings.windowWidth > 0 && globalSettings.windowHeight > 0) {
            rootWindow.width = globalSettings.windowWidth
            rootWindow.height = globalSettings.windowHeight
        }

        if (globalSettings.windowX >= 0 && globalSettings.windowY >= 0) {
            rootWindow.x = globalSettings.windowX
            rootWindow.y = globalSettings.windowY
        }

        if (globalSettings.windowState === 2) {
            rootWindow.visibility = Window.Maximized
        } else if (globalSettings.windowState === 3) {
            rootWindow.visibility = Window.FullScreen
            fullscreenMode = true
        }

        if (globalSettings.startMinimized) {
            Qt.callLater(() => rootWindow.visibility = Window.Minimized)
        }
    }

    function saveWindowState() {
        if (rootWindow.visibility === Window.Windowed) {
            globalSettings.windowWidth = rootWindow.width
            globalSettings.windowHeight = rootWindow.height
            globalSettings.windowX = rootWindow.x
            globalSettings.windowY = rootWindow.y
            globalSettings.windowState = 0
        } else if (rootWindow.visibility === Window.Maximized) {
            globalSettings.windowState = 2
        } else if (rootWindow.visibility === Window.FullScreen) {
            globalSettings.windowState = 3
        }
    }

    function handleWindowClosing(close) {
        if (globalSettings.minimizeToTray && systemTray.visible) {
            close.accepted = false
            rootWindow.hide()
            showNotification("info", "Minimized to Tray",
                             "Aegis is running in the background. Click the tray icon to restore.")
        } else {
            // Check for active operations
            var activeOps = checkActiveOperations()
            if (activeOps.length > 0) {
                close.accepted = false
                showOperationWarning(activeOps)
                return
            }

            // Save everything before closing
            saveWindowState()
            autoSave()

            // Save session data
            saveSessionData()

            // Stop all background processes
            shutdownBackends()

            // Show exit confirmation if needed
            if (globalSettings.confirmExit) {
                close.accepted = false
                exitConfirmDialog.open()
            }
        }
    }

    function checkActiveOperations() {
        var activeOps = []

        if (burnerBackend && burnerBackend.burning) {
            activeOps.push("Disc burning is in progress")
        }

        if (converterBackend && converterBackend.converting) {
            activeOps.push("File conversion is in progress")
        }

        if (projectModified) {
            activeOps.push("Unsaved project changes")
        }

        return activeOps
    }

    function showOperationWarning(operations) {
        var message = "Cannot exit while operations are active:\n\n"
        operations.forEach(function(op) {
            message += "• " + op + "\n"
        })
        message += "\nPlease complete or cancel these operations first."

        showError("Cannot Exit", message)
    }

    function shutdownBackends() {
        if (coreBackend) coreBackend.stop()
            if (audioBackend) audioBackend.stop()
                if (converterBackend && converterBackend.converting) {
                    converterBackend.cancel()
                }
                if (burnerBackend && burnerBackend.burning) {
                    burnerBackend.cancel()
                }
                if (middlewareBackend) middlewareBackend.stopAll()
                    if (modtrackerBackend) modtrackerBackend.stop()
    }

    function saveSessionData() {
        // Save recent files
        globalSettings.recentFiles = recentFiles.slice(0, 100)

        // Save app usage
        globalSettings.appUsageCount = appUsageStats

        // Save project state if needed
        if (currentProject && projectModified) {
            autoSaveProject()
        }

        console.log("💾 Session data saved")
    }

    function initializeSystem() {
        console.log("🔧 Starting system initialization...")
        console.log("System locale:", Qt.locale().name)
        console.log("Available screens:", Qt.application.screens.length)

        var initializationSteps = [
            {
                name: "Core Engine",
                check: () => typeof Core !== 'undefined',
                assign: (val) => coreBackend = val,
                critical: true,
                timeout: 5000,
                dependencies: []
            },
            {
                name: "Audio Engine",
                check: () => typeof Audio !== 'undefined',
                assign: (val) => audioBackend = val,
                critical: true,
                timeout: 3000,
                dependencies: ["Core Engine"]
            },
            {
                name: "Library Database",
                check: () => typeof Library !== 'undefined',
                assign: (val) => libraryBackend = val,
                critical: false,
                timeout: 4000,
                dependencies: ["Core Engine"]
            },
            {
                name: "Platform Services",
                check: () => typeof Platform !== 'undefined',
                assign: (val) => platformBackend = val,
                critical: false,
                timeout: 2000,
                dependencies: []
            },
            {
                name: "Disc Services",
                check: () => typeof Disc !== 'undefined',
                assign: (val) => discBackend = val,
                critical: false,
                timeout: 3000,
                dependencies: []
            },
            {
                name: "Burner Engine",
                check: () => typeof CDBurner !== 'undefined',
                assign: (val) => burnerBackend = val,
                critical: false,
                timeout: 5000,
                dependencies: ["Disc Services"]
            },
            {
                name: "Audio Editor Engine",
                check: () => typeof AudioEditor !== 'undefined',
                assign: (val) => audioEditorBackend = val,
                critical: false,
                timeout: 4000,
                dependencies: ["Audio Engine"]
            },
            {
                name: "Video Editor Engine",
                check: () => typeof VideoEditor !== 'undefined',
                assign: (val) => videoEditorBackend = val,
                critical: false,
                timeout: 4000,
                dependencies: ["Core Engine"]
            },
            {
                name: "DJ Engine",
                check: () => typeof DJ !== 'undefined',
                assign: (val) => djBackend = val,
                critical: false,
                timeout: 3000,
                dependencies: ["Audio Engine"]
            },
            {
                name: "Karaoke Engine",
                check: () => typeof Karaoke !== 'undefined',
                assign: (val) => karaokeBackend = val,
                critical: false,
                timeout: 3000,
                dependencies: ["Audio Engine"]
            },
            {
                name: "Label Engine",
                check: () => typeof LabelMaker !== 'undefined',
                assign: (val) => labelBackend = val,
                critical: false,
                timeout: 2000,
                dependencies: []
            },
            {
                name: "Converter Engine",
                check: () => typeof Converter !== 'undefined',
                assign: (val) => converterBackend = val,
                critical: false,
                timeout: 3000,
                dependencies: ["Audio Engine"]
            },
            {
                name: "Middleware Engine",
                check: () => typeof AudioMiddleware !== 'undefined',
                assign: (val) => middlewareBackend = val,
                critical: false,
                timeout: 3000,
                dependencies: ["Audio Engine"]
            },
            {
                name: "Mod Tracker Engine",
                check: () => typeof ModTracker !== 'undefined',
                assign: (val) => modtrackerBackend = val,
                critical: false,
                timeout: 3000,
                dependencies: ["Audio Engine"]
            }
        ]

        initializationStep = 0
        connectionStatus = "Initializing system components..."
        initializationErrors = []
        criticalError = false
        failedComponents = []

        function executeStep(index) {
            if (index >= initializationSteps.length) {
                finalizeInitialization()
                return
            }

            var step = initializationSteps[index]

            // Check dependencies
            var depsMissing = step.dependencies.filter(function(dep) {
                return failedComponents.includes(dep)
            })

            if (depsMissing.length > 0) {
                console.warn("⏭️ Skipping", step.name, "due to missing dependencies:", depsMissing.join(", "))
                failedComponents.push(step.name)
                setTimeout(() => executeStep(index + 1), 100)
                return
            }

            initializationStep = index + 1
            connectionStatus = "Loading " + step.name + "..."

            Qt.callLater(function() {
                var timer = setTimeout(function() {
                    if (!step.check()) {
                        var error = {
                            component: step.name,
                            message: "Timeout after " + step.timeout + "ms",
                            critical: step.critical,
                            timestamp: new Date().toISOString()
                        }

                        console.warn("⏰", step.name, "timeout")
                        initializationErrors.push(error)

                        if (step.critical) {
                            criticalError = true
                        } else {
                            failedComponents.push(step.name)
                        }

                        // Continue to next step
                        executeStep(index + 1)
                    }
                }, step.timeout)

                if (step.check()) {
                    clearTimeout(timer)
                    console.log("✅", step.name, "loaded successfully")

                    try {
                        step.assign(window[step.name.replace(/\s/g, '')] || window[step.name])

                        // Apply settings if available
                        if (step.name === "Audio Engine" && audioBackend) {
                            applyAudioSettings()
                        }

                        // Initialize backend if needed
                        if (step.name === "Middleware Engine" && middlewareBackend) {
                            middlewareBackend.initialize()
                        }

                        // Small delay for visual feedback
                        setTimeout(() => executeStep(index + 1), 100)
                    } catch (e) {
                        console.error("❌ Error assigning", step.name + ":", e)
                        var error = {
                            component: step.name,
                            message: "Assignment error: " + e.message,
                            critical: step.critical
                        }
                        initializationErrors.push(error)
                        setTimeout(() => executeStep(index + 1), 100)
                    }
                } else {
                    clearTimeout(timer)
                    console.warn("❌", step.name, "not available")

                    var error = {
                        component: step.name,
                        message: "Component not found",
                        critical: step.critical
                    }
                    initializationErrors.push(error)

                    if (step.critical) {
                        criticalError = true
                        showCriticalError(step.name + " failed to load")
                    } else {
                        failedComponents.push(step.name)
                    }

                    setTimeout(() => executeStep(index + 1), 100)
                }
            })
        }

        executeStep(0)
    }

    function applyAudioSettings() {
        if (audioBackend) {
            try {
                audioBackend.setVolume(audioSettings.masterVolume)
                audioBackend.setOutputDevice(audioSettings.outputDevice)
                audioBackend.setSampleRate(audioSettings.sampleRate)
                audioBackend.setBufferSize(audioSettings.bufferSize)

                // Apply additional audio settings
                if (audioBackend.setLatency) {
                    audioBackend.setLatency(audioSettings.latencyTarget)
                }
                if (audioBackend.setDriver) {
                    audioBackend.setDriver(audioSettings.audioDriver)
                }
            } catch (e) {
                console.warn("⚠️ Failed to apply audio settings:", e.message)
            }
        }
    }

    function finalizeInitialization() {
        console.log("=".repeat(50))
        console.log("🎉 Initialization Complete")
        console.log("=".repeat(50))

        var summary = {
            "Core": !!coreBackend,
            "Audio": !!audioBackend,
            "Library": !!libraryBackend,
            "Platform": !!platformBackend,
            "Disc": !!discBackend,
            "AudioEditor": !!audioEditorBackend,
            "VideoEditor": !!videoEditorBackend,
            "Converter": !!converterBackend,
            "Burner": !!burnerBackend,
            "DJ": !!djBackend,
            "Karaoke": !!karaokeBackend,
            "Label": !!labelBackend,
            "Middleware": !!middlewareBackend,
            "ModTracker": !!modtrackerBackend
        }

        console.table(summary)

        if (failedComponents.length > 0) {
            console.warn("⚠️ Missing optional components:", failedComponents.join(", "))
            showNotification("warning", "Optional Components Missing",
                             "Some optional features are unavailable: " + failedComponents.join(", "))
        }

        if (initializationErrors.length > 0) {
            console.warn("⚠️ Errors during initialization:", initializationErrors.length)
        }

        if (criticalError) {
            connectionStatus = "Critical error - Some features unavailable"
            showError("System Error",
                      "Critical components failed to load. Some features may be unavailable.\n\n" +
                      "Failed: " + initializationErrors.filter(e => e.critical).map(e => e.component).join(", "))

            // Still continue in degraded mode
            setTimeout(() => {
                backendReady = true
                startDegradedMode()
            }, 2000)
        } else {
            backendReady = true
            connectionStatus = "Ready"
            console.log("✅ All systems operational")

            // Start background services
            startBackgroundServices()

            // Load launcher
            loadLauncher()
        }
    }

    function startDegradedMode() {
        console.log("⚠️ Starting in degraded mode")

        // Start minimal background services
        sessionTimer.start()
        systemMonitorTimer.start()
        loadRecentData()

        // Load launcher
        loadLauncher()
    }

    function startBackgroundServices() {
        console.log("🔄 Starting background services...")

        // Start session timer
        sessionTimer.start()

        // Start system monitor
        systemMonitorTimer.start()

        // Start battery monitor
        batteryTimer.start()

        // Initialize clipboard
        initClipboard()

        // Load recent data
        loadRecentData()

        // Setup auto-save
        if (globalSettings.autoSave) {
            autoSaveTimer.interval = globalSettings.autoSaveInterval * 60000
            autoSaveTimer.start()
        }

        // Start backend health monitoring
        backendHealthTimer.start()

        // Initialize system tray
        initSystemTray()

        // Check for updates
        if (globalSettings.autoUpdates) {
            checkForUpdates()
        }

        console.log("✅ Background services started")
    }

    function initSystemTray() {
        if (Platform.SystemTrayIcon.supported) {
            systemTrayEnabled = true
            systemTray.visible = true
            updateRecentFilesMenu()
        }
    }

    function loadRecentData() {
        recentFiles = globalSettings.recentFiles || []
        recentPlaylists = globalSettings.recentPlaylists || []
        recentProjects = globalSettings.recentProjects || []
        appUsageStats = globalSettings.appUsageCount || {}

        // Sort recent files by last access
        recentFiles.sort((a, b) => new Date(b.lastAccessed) - new Date(a.lastAccessed))

        // Limit to 50 items
        if (recentFiles.length > 50) {
            recentFiles = recentFiles.slice(0, 50)
            globalSettings.recentFiles = recentFiles
        }

        console.log("📁 Loaded", recentFiles.length, "recent files")
        console.log("🎵 Loaded", recentPlaylists.length, "recent playlists")
        console.log("📋 Loaded", recentProjects.length, "recent projects")
    }

    function addRecentFile(filePath, type) {
        var fileInfo = {
            path: filePath,
            name: filePath.split('/').pop().split('\\').pop(),
            type: type || getFileType(filePath),
            lastAccessed: new Date().toISOString(),
            app: currentMode,
            size: getFileSize(filePath)
        }

        // Remove if already exists
        recentFiles = recentFiles.filter(f => f.path !== filePath)

        // Add to beginning
        recentFiles.unshift(fileInfo)

        // Limit size
        if (recentFiles.length > 100) {
            recentFiles.pop()
        }

        // Save to settings
        globalSettings.recentFiles = recentFiles

        // Update tray menu
        updateRecentFilesMenu()

        // Emit signal for UI updates
        recentFileAdded(fileInfo)

        console.log("📝 Added to recent files:", fileInfo.name)
    }

    function getFileType(filePath) {
        var ext = filePath.split('.').pop().toLowerCase()
        var audioExt = ['mp3', 'wav', 'flac', 'aac', 'ogg', 'm4a', 'aiff', 'wma', 'opus']
        var videoExt = ['mp4', 'avi', 'mkv', 'mov', 'wmv', 'flv', 'webm', 'm4v', 'mpeg', 'ts']
        var imageExt = ['jpg', 'jpeg', 'png', 'gif', 'bmp', 'tiff', 'svg', 'webp']
        var projectExt = ['aegis', 'aep', 'aegisedit', 'aegisburn', 'aegisconv', 'aegismiddleware']
        var documentExt = ['pdf', 'doc', 'docx', 'txt', 'rtf']

        if (audioExt.includes(ext)) return "audio"
            if (videoExt.includes(ext)) return "video"
                if (imageExt.includes(ext)) return "image"
                    if (projectExt.includes(ext)) return "project"
                        if (documentExt.includes(ext)) return "document"
                            if (ext === 'iso' || ext === 'nrg' || ext === 'img') return "discimage"
                                return "file"
    }

    function getFileSize(filePath) {
        // This would use Qt's file system API in real implementation
        return "N/A"
    }

    // ============================================
    // 4. APPLICATION MODE MANAGEMENT
    // ============================================

    // Main mode switching function
    function switchMode(mode, qmlSource, options = {}) {
        if (currentMode === mode && !options.forceReload) return

            console.log("🔄 Switching to mode:", mode, "from:", currentMode)

            // Store previous mode
            var previousMode = currentMode

            // Track app usage
            trackAppUsage(mode)

            // Save to history
            if (currentMode !== "launcher" && currentMode !== mode) {
                modeHistory.push({
                    mode: currentMode,
                    timestamp: new Date().toISOString(),
                                 data: options.historyData || {}
                })

                if (modeHistory.length > maxHistorySize) {
                    modeHistory.shift()
                }
            }

            // Update settings
            globalSettings.lastWorkingMode = mode
            currentMode = mode

            // Add to recent apps
            addToRecentApps(mode)

            // Transition effect
            if (!options.silent) {
                themeLoader.opacity = 0
                modeSwitchTimer.modeSource = qmlSource
                modeSwitchTimer.contextProperties = options.context || {}
                modeSwitchTimer.previousMode = previousMode
                modeSwitchTimer.start()
            } else {
                // Direct load
                themeLoader.setSource(qmlSource, getContextProperties(mode, options.context))
            }

            // Emit mode changed signal
            modeChanged(mode, previousMode)
    }

    function trackAppUsage(appId) {
        var usage = globalSettings.appUsageCount || {}
        usage[appId] = (usage[appId] || 0) + 1
        globalSettings.appUsageCount = usage

        // Update runtime stats
        appUsageStats = usage

        // Emit signal
        appUsageChanged(appId, usage[appId])
    }

    function addToRecentApps(appId) {
        // Remove if already exists
        recentApps = recentApps.filter(a => a.id !== appId)

        // Add to beginning
        recentApps.unshift({
            id: appId,
            name: getAppName(appId),
                           timestamp: new Date().toISOString(),
                           icon: getAppIcon(appId)
        })

        // Limit to 10 items
        if (recentApps.length > 10) {
            recentApps.pop()
        }
    }

    function getAppName(appId) {
        var names = {
            "launcher": "Launcher",
            "player": "Media Player",
            "audioeditor": "Audio Editor",
            "videoeditor": "Video Editor",
            "burner": "Disc Burner",
            "labelmaker": "Label Designer",
            "karaoke": "Karaoke",
            "djmixer": "DJ Mixer",
            "converter": "Media Converter",
            "modtracker": "Tracker",
            "middleware": "Audio Middleware",
            "music": "Music Notation"
        }
        return names[appId] || appId
    }

    function getAppIcon(appId) {
        var icons = {
            "launcher": "🚀",
            "player": "🎵",
            "audioeditor": "✏️",
            "videoeditor": "🎬",
            "burner": "📀",
            "labelmaker": "📝",
            "karaoke": "🎤",
            "djmixer": "🎧",
            "modtracker": "🎚️",
            "converter": "🔄",
            "middleware": "🔌",
            "music": "🎼"
        }
        return icons[appId] || "📱"
    }

    function getContextProperties(mode, additionalContext = {}) {
        var baseContext = {
            "coreRef": coreBackend,
            "audioRef": audioBackend,
            "libraryRef": libraryBackend,
            "platformRef": platformBackend,
            "discRef": discBackend,
            "burnerRef": burnerBackend,
            "audioeditorRef": audioEditorBackend,
            "videoeditorRef": videoEditorBackend,
            "djRef": djBackend,
            "karaokeRef": karaokeBackend,
            "labelRef": labelBackend,
            "converterRef": converterBackend,
            "middlewareRef": middlewareBackend,
            "modtrackerRef": modtrackerBackend,
            "appWindow": rootWindow,
            "recentFiles": recentFiles,
            "recentPlaylists": recentPlaylists,
            "recentProjects": recentProjects,
            "appUsageStats": appUsageStats,
            "globalSettings": globalSettings,
            "audioSettings": audioSettings,
            "videoSettings": videoSettings,
            "networkSettings": networkSettings
        }

        // Add mode-specific context
        switch(mode) {
            case "player":
                baseContext.isPlaying = Qt.binding(function() { return rootWindow.isPlaying })
                baseContext.position = Qt.binding(function() { return rootWindow.mediaPosition })
                baseContext.duration = Qt.binding(function() { return rootWindow.mediaDuration })
                baseContext.volume = Qt.binding(function() { return rootWindow.mediaVolume })
                baseContext.currentFile = Qt.binding(function() { return rootWindow.currentMediaFile })
                baseContext.hasVideo = Qt.binding(function() { return rootWindow.mediaHasVideo })
                break
            case "audioeditor":
            case "videoeditor":
                baseContext.clipboardData = Qt.binding(function() { return rootWindow.clipboardData })
                baseContext.projectModified = Qt.binding(function() { return rootWindow.projectModified })
                break
            case "burner":
                baseContext.isBurning = Qt.binding(function() {
                    return burnerBackend ? burnerBackend.burning : false
                })
                break
            case "converter":
                baseContext.isConverting = Qt.binding(function() {
                    return converterBackend ? converterBackend.converting : false
                })
                baseContext.convertProgress = Qt.binding(function() {
                    return converterBackend ? converterBackend.progress : 0
                })
                break
            case "middleware":
                baseContext.middlewareActive = Qt.binding(function() {
                    return middlewareBackend ? middlewareBackend.active : false
                })
                break
            case "modtracker":
                baseContext.trackerPlaying = Qt.binding(function() {
                    return modtrackerBackend ? modtrackerBackend.playing : false
                })
                break
        }

        // Merge additional context
        for (var key in additionalContext) {
            baseContext[key] = additionalContext[key]
        }

        return baseContext
    }

    // Public API for mode switching
    function showLauncher() {
        switchMode("launcher", "qrc:/qml/ui_launcher.qml", {
            historyData: { from: currentMode }
        })
    }

    function showPlayer() {
        switchMode("player", "qrc:/qml/ui_player.qml")
    }

    function showAudioEditor() {
        switchMode("audioeditor", "qrc:/qml/ui_audioeditor.qml")
    }

    function showVideoEditor() {
        switchMode("videoeditor", "qrc:/qml/ui_videoeditor.qml")
    }

    function showBurner() {
        switchMode("burner", "qrc:/qml/ui_discburner.qml")
    }

    function showLabelMaker() {
        switchMode("labelmaker", "qrc:/qml/ui_disc_labelmaker.qml")
    }

    function showKaraoke() {
        switchMode("karaoke", "qrc:/qml/ui_karaoke.qml")
    }

    function showDJMix() {
        switchMode("djmixer", "qrc:/qml/ui_djmixer.qml")
    }

    function showConverter() {
        switchMode("converter", "qrc:/qml/ui_converter.qml")
    }

    function showMiddleware() {
        switchMode("middleware", "qrc:/qml/ui_middleware.qml")
    }

    function showModTracker() {
        switchMode("modtracker", "qrc:/qml/ui_modtracker.qml")
    }

    function goBack() {
        if (modeHistory.length > 0) {
            var lastState = modeHistory.pop()
            console.log("↩️ Going back to:", lastState.mode)

            switch(lastState.mode) {
                case "launcher": showLauncher(); break
                case "player": showPlayer(); break
                case "audioeditor": showAudioEditor(); break
                case "videoeditor": showVideoEditor(); break
                case "burner": showBurner(); break
                case "labelmaker": showLabelMaker(); break
                case "karaoke": showKaraoke(); break
                case "djmixer": showDJMix(); break
                case "converter": showConverter(); break
                case "middleware": showMiddleware(); break
                case "modtracker": showModTracker(); break
                default: showLauncher()
            }
        } else {
            showLauncher()
        }
    }

    function loadLauncher() {
        console.log("🏠 Loading Launcher...")
        showLauncher()
    }

    // ============================================
    // 5. UI LOADER & TRANSITIONS
    // ============================================

    Timer {
        id: modeSwitchTimer
        interval: 200
        property string modeSource: ""
        property var contextProperties: ({})
        property string previousMode: ""
        onTriggered: {
            if (modeSource) {
                var context = getContextProperties(currentMode, contextProperties)
                themeLoader.setSource(modeSource, context)
            }
        }
    }

    Loader {
        id: themeLoader
        anchors.fill: parent
        asynchronous: true
        opacity: status === Loader.Ready ? 1 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: 250
                easing.type: Easing.InOutQuad
            }
        }

        onLoaded: {
            console.log("✅ UI loaded for mode:", currentMode, "Status:", status)

            // Ensure proper anchoring
            if (item && item.anchors) {
                item.anchors.fill = themeLoader
            }

            // Set focus to loaded UI
            if (item) {
                item.forceActiveFocus()
            }

            // Inject backend connections if supported
            if (item && typeof item.connectBackends === 'function') {
                item.connectBackends({
                    core: coreBackend,
                    audio: audioBackend,
                    library: libraryBackend,
                    platform: platformBackend,
                    disc: discBackend,
                    burner: burnerBackend,
                    audioeditor: audioEditorBackend,
                    videoeditor: videoEditorBackend,
                    dj: djBackend,
                    karaoke: karaokeBackend,
                    label: labelBackend,
                    converter: converterBackend,
                    middleware: middlewareBackend,
                    modtracker: modtrackerBackend
                })
            }

            // Inject project data if supported
            if (item && typeof item.loadProject === 'function' && currentProject) {
                item.loadProject(currentProject)
            }

            // Signal UI is ready
            uiReady()
        }

        onStatusChanged: {
            switch(status) {
                case Loader.Null:
                    console.debug("Loader: Null state")
                    break
                case Loader.Ready:
                    console.debug("Loader: Ready")
                    errorDisplay.visible = false
                    break
                case Loader.Loading:
                    console.debug("Loader: Loading", source)
                    break
                case Loader.Error:
                    console.error("❌ Loader: Failed to load", source, errorString)
                    showError("UI Load Error",
                              "Failed to load " + currentMode + " interface:\n" +
                              errorString + "\n\n" +
                              "Source: " + source)
                    // Fallback to launcher
                    setTimeout(() => showLauncher(), 2000)
                    break
            }
        }
    }

    // ============================================
    // 6. SYSTEM SERVICES & TIMERS
    // ============================================

    Timer {
        id: sessionTimer
        interval: 60000 // 1 minute
        running: true
        repeat: true
        onTriggered: {
            sessionDuration += 1
            // Auto-save every 5 minutes or as configured
            if (sessionDuration % globalSettings.autoSaveInterval === 0) {
                autoSave()
            }
            // Update session time display
            sessionTimeUpdated(sessionDuration)
        }
    }

    Timer {
        id: systemMonitorTimer
        interval: 5000 // 5 seconds
        running: backendReady
        repeat: true
        onTriggered: {
            updateSystemMetrics()
            checkNetworkStatus()
            checkBatteryStatus()
        }
    }

    Timer {
        id: batteryTimer
        interval: 30000 // 30 seconds
        running: true
        repeat: true
        onTriggered: checkBatteryStatus()
    }

    Timer {
        id: autoSaveTimer
        interval: globalSettings.autoSaveInterval * 60000 // Configurable minutes
        running: globalSettings.autoSave && backendReady
        repeat: true
        onTriggered: autoSave()
    }

    Timer {
        id: backendHealthTimer
        interval: 30000 // 30 seconds
        running: backendReady
        repeat: true
        onTriggered: checkBackendHealth()
    }

    Timer {
        id: updateCheckTimer
        interval: 3600000 // 1 hour
        running: globalSettings.autoUpdates && backendReady
        repeat: true
        onTriggered: checkForUpdates()
    }

    function updateSystemMetrics() {
        // Simulated system metrics - in real app would use system APIs
        cpuUsage = 10 + Math.random() * 40
        memoryUsage = 30 + Math.random() * 50
        diskUsage = 20 + Math.random() * 30

        // Check for low power mode
        lowPowerMode = cpuUsage > 80 || memoryUsage > 90 || batteryLevel < 20

        // Emit signal for UI updates
        systemMetricsUpdated(cpuUsage, memoryUsage, diskUsage)

        // Update low power mode if needed
        if (lowPowerMode) {
            enableLowPowerMode()
        }
    }

    function checkBatteryStatus() {
        // Simulated battery check
        batteryLevel = Math.max(0, batteryLevel - 0.1)
        isCharging = batteryLevel < 30 ? true : false

        if (batteryLevel < 10 && !isCharging) {
            showNotification("warning", "Low Battery",
                             "Battery level is critical (" + Math.round(batteryLevel) + "%). Save your work.")
        }
    }

    function checkNetworkStatus() {
        // Simulated network check
        var status = Math.random()
        if (status < 0.1) {
            networkStatus = 0 // offline
        } else if (status < 0.4) {
            networkStatus = 1 // slow
        } else {
            networkStatus = 2 // good
        }

        networkStatusChanged(networkStatus)
    }

    function checkBackendHealth() {
        var health = {}

        if (coreBackend && typeof coreBackend.ping === 'function') {
            health.core = coreBackend.ping()
        }

        if (audioBackend && typeof audioBackend.isActive === 'function') {
            health.audio = audioBackend.isActive()
        }

        if (converterBackend && typeof converterBackend.isActive === 'function') {
            health.converter = converterBackend.isActive()
        }

        if (middlewareBackend && typeof middlewareBackend.isActive === 'function') {
            health.middleware = middlewareBackend.isActive()
        }

        // Log any issues
        for (var key in health) {
            if (!health[key]) {
                console.warn("⚠️ Backend health issue:", key)
                showNotification("warning", "Backend Issue",
                                 key + " backend is not responding properly")
            }
        }
    }

    function enableLowPowerMode() {
        if (!globalSettings.lowLatencyMode) {
            console.log("🔋 Enabling low power mode optimizations")
            // Reduce update frequencies
            systemMonitorTimer.interval = 10000 // 10 seconds
            backendHealthTimer.interval = 60000 // 1 minute

            // Notify user
            showNotification("info", "Low Power Mode",
                             "Power saving optimizations enabled")
        }
    }

    function autoSave() {
        console.log("💾 Auto-saving session...")
        saveWindowState()

        // Auto-save project if modified
        if (projectModified && currentProject) {
            autoSaveProject()
        }

        // Save other session data
        if (libraryBackend) {
            libraryBackend.flushCache()
        }

        // Backup recent files
        saveRecentDataBackup()
    }

    function autoSaveProject() {
        if (currentProject) {
            var backupName = currentProject.name + "_autosave_" +
            Qt.formatDateTime(new Date(), "yyyyMMdd_hhmmss")

            // Save backup
            saveProjectBackup(currentProject, backupName)

            console.log("📁 Project auto-saved:", backupName)
            showNotification("info", "Auto-saved",
                             "Project backup created: " + backupName)
        }
    }

    function saveRecentDataBackup() {
        var backup = {
            timestamp: new Date().toISOString(),
            recentFiles: recentFiles,
            recentPlaylists: recentPlaylists,
            recentProjects: recentProjects,
            appUsageStats: appUsageStats
        }

        // This would save to disk in real implementation
        console.log("📋 Recent data backup created")
    }

    function initClipboard() {
        // Initialize system clipboard
        clipboardData = {
            text: "",
            files: [],
            audioData: null,
            imageData: null,
            timestamp: null
        }
    }

    function checkForUpdates() {
        console.log("🔍 Checking for updates...")
        // This would check for updates in real implementation
        // For now, simulate update check
        setTimeout(function() {
            var hasUpdate = Math.random() > 0.7
            if (hasUpdate) {
                showNotification("info", "Update Available",
                                 "A new version of Aegis Media Suite is available")
            }
        }, 2000)
    }

    // ============================================
    // 7. OVERLAYS & SYSTEM UI
    // ============================================

    // Initialization Overlay
    Rectangle {
        id: initializationOverlay
        anchors.fill: parent
        color: "#0a0a0a"
        visible: !backendReady
        z: 1000

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 30

            // Animated Logo
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 120
                height: 120
                radius: 60
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#ff8c00" }
                    GradientStop { position: 1.0; color: "#ff5500" }
                }

                Text {
                    anchors.centerIn: parent
                    text: "🎵"
                    font.pixelSize: 60
                }

                RotationAnimation on rotation {
                    running: initializationOverlay.visible
                    loops: Animation.Infinite
                    from: 0
                    to: 360
                    duration: 8000
                    easing.type: Easing.Linear
                }

                SequentialAnimation on scale {
                    running: initializationOverlay.visible
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 1.05; duration: 2000; easing.type: Easing.InOutQuad }
                    NumberAnimation { from: 1.05; to: 1.0; duration: 2000; easing.type: Easing.InOutQuad }
                }
            }

            // Title
            ColumnLayout {
                spacing: 5
                Layout.alignment: Qt.AlignHCenter

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "AEGIS MEDIA SUITE"
                    color: "#ff8c00"
                    font.pixelSize: 32
                    font.bold: true
                    font.family: "JetBrains Mono"
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "v2.1.1 Professional"
                    color: "#666666"
                    font.pixelSize: 12
                    font.family: "JetBrains Mono"
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: Qt.platform.os.charAt(0).toUpperCase() + Qt.platform.os.slice(1) + " Edition"
                    color: "#444444"
                    font.pixelSize: 10
                    font.family: "JetBrains Mono"
                }
            }

            // Status Text
            Text {
                id: initStatusText
                Layout.alignment: Qt.AlignHCenter
                text: connectionStatus
                color: "#aaaaaa"
                font.pixelSize: 14
                font.family: "JetBrains Mono"
            }

            // Progress Bar
            Rectangle {
                Layout.preferredWidth: 400
                Layout.preferredHeight: 4
                color: "#333333"
                radius: 2

                Rectangle {
                    width: parent.width * (initializationStep / 15)
                    height: parent.height
                    color: "#ff8c00"
                    radius: 2

                    Behavior on width {
                        NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
                    }
                }
            }

            // Progress Text
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Step " + initializationStep + " of 15"
                color: "#666666"
                font.pixelSize: 11
                font.family: "JetBrains Mono"
            }

            // Loading Indicator
            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: 4

                Repeater {
                    model: 3
                    delegate: Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: "#ff8c00"
                        opacity: 0.3 + (index * 0.2)

                        SequentialAnimation on opacity {
                            running: initializationOverlay.visible
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.3; to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
                            NumberAnimation { from: 1.0; to: 0.3; duration: 800; easing.type: Easing.InOutQuad }
                            PauseAnimation { duration: index * 200 }
                        }
                    }
                }
            }

            // Failed Components
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: failedComponents.length > 0 ?
                "⚠️ Optional components missing: " + failedComponents.join(", ") : ""
                color: "#ff5500"
                font.pixelSize: 11
                font.family: "JetBrains Mono"
                visible: failedComponents.length > 0
            }

            // System Info
            ColumnLayout {
                spacing: 5
                Layout.alignment: Qt.AlignHCenter
                visible: initializationStep > 5

                Text {
                    text: "CPU: " + Math.round(cpuUsage) + "% | RAM: " + Math.round(memoryUsage) + "%"
                    color: "#444444"
                    font.pixelSize: 10
                    font.family: "JetBrains Mono"
                }

                Text {
                    text: "Battery: " + Math.round(batteryLevel) + "% " + (isCharging ? "⚡" : "")
                    color: batteryLevel < 30 ? "#ff5500" : "#444444"
                    font.pixelSize: 10
                    font.family: "JetBrains Mono"
                }
            }

            // Tip
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: getRandomTip()
                color: "#444444"
                font.pixelSize: 11
                font.family: "JetBrains Mono"
                font.italic: true
            }
        }
    }

    // Error Display
    Rectangle {
        id: errorDisplay
        anchors.fill: parent
        color: "#0a0a0a"
        visible: false
        z: 9999

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 25
            width: Math.min(parent.width * 0.7, 600)

            // Error Icon
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "⚠️"
                font.pixelSize: 60
                color: "#ff5500"
            }

            // Error Title
            Text {
                id: errorTitle
                Layout.alignment: Qt.AlignHCenter
                color: "#ff5500"
                text: "ERROR"
                font.pixelSize: 28
                font.bold: true
                font.family: "JetBrains Mono"
            }

            // Error Message
            Text {
                id: errorMessage
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                color: "#888888"
                text: "An error occurred"
                font.pixelSize: 14
                font.family: "JetBrains Mono"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            // Error Details
            Text {
                id: errorDetails
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                color: "#666666"
                text: ""
                font.pixelSize: 11
                font.family: "JetBrains Mono"
                wrapMode: Text.Wrap
                visible: text !== ""
            }

            // Action Buttons
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 15

                Button {
                    text: "Return to Launcher"
                    onClicked: {
                        errorDisplay.visible = false
                        showLauncher()
                    }

                    background: Rectangle {
                        color: parent.pressed ? "#333333" : "#444444"
                        border.color: "#ff5500"
                        border.width: 1
                        radius: 4
                    }

                    contentItem: Text {
                        text: parent.text
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: "JetBrains Mono"
                    }
                }

                Button {
                    text: "Retry"
                    onClicked: {
                        errorDisplay.visible = false
                        initializeSystem()
                    }

                    background: Rectangle {
                        color: parent.pressed ? "#333333" : "#444444"
                        border.color: "#ff8c00"
                        border.width: 1
                        radius: 4
                    }

                    contentItem: Text {
                        text: parent.text
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: "JetBrains Mono"
                    }
                }

                Button {
                    text: "Report Issue"
                    onClicked: reportIssue()

                    background: Rectangle {
                        color: parent.pressed ? "#333333" : "#444444"
                        radius: 4
                    }

                    contentItem: Text {
                        text: parent.text
                        color: "#aaaaaa"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: "JetBrains Mono"
                    }
                }
            }

            // Debug Info
            Text {
                Layout.alignment: Qt.AlignHCenter
                color: "#444444"
                text: "Debug: Mode=" + currentMode + " | Backend=" + backendReady +
                " | Loader=" + themeLoader.status + " | Session=" + sessionDuration + "m"
                font.pixelSize: 10
                font.family: "JetBrains Mono"
                visible: false // Enable for debugging
            }
        }
    }

    // Notification System
    Rectangle {
        id: notificationArea
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 20
        width: 350
        height: notificationColumn.height
        color: "transparent"
        visible: notificationColumn.children.length > 0
        z: 5000

        Column {
            id: notificationColumn
            width: parent.width
            spacing: 10

            function addNotification(type, title, message, duration = 5000) {
                var component = Qt.createComponent("Notification.qml")
                if (component.status === Component.Ready) {
                    var notification = component.createObject(notificationColumn, {
                        "notificationType": type,
                        "notificationTitle": title,
                        "notificationMessage": message,
                        "duration": duration
                    })
                    notification.destroyOnFinish()
                } else {
                    // Fallback simple notification
                    console.log("📢", title + ":", message)
                }
            }
        }
    }

    // System Tray
    Platform.SystemTrayIcon {
        id: systemTray
        visible: systemTrayEnabled && Platform.SystemTrayIcon.supported
        icon.source: "qrc:/icons/tray.png"
        tooltip: "Aegis Media Suite v2.1.1"

        menu: Platform.Menu {
            Platform.MenuItem {
                text: "Show Launcher"
                onTriggered: {
                    rootWindow.show()
                    rootWindow.raise()
                    rootWindow.requestActivate()
                    showLauncher()
                }
            }

            Platform.MenuItem {
                text: "Show Player"
                onTriggered: {
                    rootWindow.show()
                    rootWindow.raise()
                    rootWindow.requestActivate()
                    showPlayer()
                }
            }

            Platform.MenuItem {
                text: "Show Middleware"
                onTriggered: {
                    rootWindow.show()
                    rootWindow.raise()
                    rootWindow.requestActivate()
                    showMiddleware()
                }
            }

            Platform.MenuSeparator {}

            Platform.MenuItem {
                text: "Recent Files"
                enabled: recentFiles.length > 0

                Platform.Menu {
                    id: recentFilesMenu
                    Component.onCompleted: {
                        updateRecentFilesMenu()
                    }
                }
            }

            Platform.MenuItem {
                text: "Quick Actions"
                Platform.Menu {
                    Platform.MenuItem {
                        text: "Take Screenshot"
                        onTriggered: takeScreenshot()
                    }
                    Platform.MenuItem {
                        text: "Start Recording"
                        onTriggered: startRecording()
                    }
                    Platform.MenuItem {
                        text: "Audio Settings"
                        onTriggered: showAudioSettings()
                    }
                }
            }

            Platform.MenuSeparator {}

            Platform.MenuItem {
                text: "Exit"
                onTriggered: Qt.quit()
            }
        }

        onActivated: function(reason) {
            if (reason === Platform.SystemTrayIcon.Trigger) {
                rootWindow.show()
                rootWindow.raise()
                rootWindow.requestActivate()
            }
        }
    }

    function updateRecentFilesMenu() {
        recentFilesMenu.clear()

        var count = Math.min(recentFiles.length, 10)
        for (var i = 0; i < count; i++) {
            var file = recentFiles[i]
            var item = recentFilesMenu.addItem(file.name)
            item.triggered.connect((function(filePath) {
                return function() {
                    openRecentFile(filePath)
                }
            })(file.path))
        }

        if (count === 0) {
            var emptyItem = recentFilesMenu.addItem("No recent files")
            emptyItem.enabled = false
        }
    }

    function openRecentFile(filePath) {
        var type = getFileType(filePath)
        switch(type) {
            case "audio":
            case "video":
                showPlayer()
                if (coreBackend) {
                    coreBackend.loadFile(filePath)
                }
                break
            case "project":
                // Open in appropriate editor based on file extension
                if (filePath.endsWith(".aegisedit")) {
                    showAudioEditor()
                } else if (filePath.endsWith(".aegisburn")) {
                    showBurner()
                } else if (filePath.endsWith(".aegisconv")) {
                    showConverter()
                } else if (filePath.endsWith(".aegismiddleware")) {
                    showMiddleware()
                }
                break
            default:
                // Try to open with default application
                Qt.openUrlExternally("file:///" + filePath)
        }
    }

    // ============================================
    // 8. UTILITY FUNCTIONS
    // ============================================

    function showError(title, message, details = "") {
        errorTitle.text = title
        errorMessage.text = message
        errorDetails.text = details
        errorDisplay.visible = true
        console.error("❌", title + ":", message, details ? "\n" + details : "")
    }

    function showCriticalError(message) {
        showError("Critical Error", message,
                  "Application may not function correctly.\nPlease restart the application.")
    }

    function showNotification(type, title, message) {
        notificationColumn.addNotification(type, title, message)
    }

    function getRandomTip() {
        var tips = [
            "Tip: Press F1 for help in any application",
            "Tip: Drag files onto the launcher to open them quickly",
            "Tip: Use Ctrl+L to return to launcher from any app",
            "Tip: Right-click files for quick actions",
            "Tip: Customize hotkeys in Settings",
            "Tip: Export projects to share with others",
            "Tip: Use batch processing for multiple files",
            "Tip: Use the Converter for quick format changes",
            "Tip: Middleware can route audio between applications",
            "Tip: Auto-save keeps backups of your projects",
            "Tip: Use Ctrl+Tab to switch between recent apps",
            "Tip: Right-click the tray icon for quick access",
            "Tip: Project templates are available in the editor"
        ]
        return tips[Math.floor(Math.random() * tips.length)]
    }

    function reportIssue() {
        var issueData = {
            timestamp: new Date().toISOString(),
            version: "2.1.1",
            platform: Qt.platform.os,
            qtVersion: Qt.version,
            mode: currentMode,
            errors: initializationErrors,
            failedComponents: failedComponents,
            systemInfo: {
                cpu: cpuUsage,
                memory: memoryUsage,
                screen: Screen.width + "x" + Screen.height
            }
        }

        console.log("📋 Issue Report:", JSON.stringify(issueData, null, 2))

        // In real app, this would open issue reporter or send to server
        showNotification("info", "Issue Report", "Issue details copied to console.")

        // Copy to clipboard
        if (clipboardData) {
            clipboardData.text = JSON.stringify(issueData, null, 2)
            clipboardData.timestamp = new Date()
        }
    }

    function formatTime(seconds) {
        if (seconds === undefined || seconds === null || isNaN(seconds) || seconds < 0)
            return "--:--"

            var hrs = Math.floor(seconds / 3600)
            var mins = Math.floor((seconds % 3600) / 60)
            var secs = Math.floor(seconds % 60)

            if (hrs > 0) {
                return (hrs < 10 ? "0" + hrs : hrs) + ":" +
                (mins < 10 ? "0" + mins : mins) + ":" +
                (secs < 10 ? "0" + secs : secs)
            } else {
                return (mins < 10 ? "0" + mins : mins) + ":" +
                (secs < 10 ? "0" + secs : secs)
            }
    }

    function formatBytes(bytes) {
        if (bytes === 0 || bytes === undefined) return "0 B"
            var k = 1024
            var sizes = ["B", "KB", "MB", "GB", "TB", "PB"]
            var i = Math.floor(Math.log(bytes) / Math.log(k))
            return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + " " + sizes[i]
    }

    function formatDate(dateString) {
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

    function takeScreenshot() {
        // This would capture the screen in real implementation
        showNotification("info", "Screenshot", "Screenshot captured")
    }

    function startRecording() {
        // This would start audio/video recording
        showNotification("info", "Recording", "Recording started")
    }

    function showAudioSettings() {
        // This would show audio settings dialog
        showNotification("info", "Audio Settings", "Audio settings dialog opened")
    }

    function saveProjectBackup(project, backupName) {
        // This would save project backup in real implementation
        console.log("💾 Saving project backup:", backupName)
        return true
    }

    // ============================================
    // 9. GLOBAL KEYBOARD SHORTCUTS
    // ============================================

    // Media Controls
    Shortcut {
        sequences: ["Space", "Media Play", "Media Pause", "Media Stop"]
        enabled: backendReady && coreBackend
        onActivated: {
            if (!coreBackend) return

                switch(sequence) {
                    case "Space":
                        coreBackend.playPause()
                        break
                    case "Media Play":
                        coreBackend.play()
                        break
                    case "Media Pause":
                        coreBackend.pause()
                        break
                    case "Media Stop":
                        coreBackend.stop()
                        break
                }
        }
    }

    // Application Navigation
    Shortcut {
        sequence: "Ctrl+L"
        onActivated: showLauncher()
    }

    Shortcut {
        sequence: "Ctrl+1"
        onActivated: showPlayer()
    }

    Shortcut {
        sequence: "Ctrl+2"
        onActivated: showAudioEditor()
    }

    Shortcut {
        sequence: "Ctrl+3"
        onActivated: showVideoEditor()
    }

    Shortcut {
        sequence: "Ctrl+4"
        onActivated: showBurner()
    }

    Shortcut {
        sequence: "Ctrl+5"
        onActivated: showLabelMaker()
    }

    Shortcut {
        sequence: "Ctrl+6"
        onActivated: showKaraoke()
    }

    Shortcut {
        sequence: "Ctrl+7"
        onActivated: showDJMix()
    }

    Shortcut {
        sequence: "Ctrl+8"
        onActivated: showConverter()
    }

    Shortcut {
        sequence: "Ctrl+9"
        onActivated: showMiddleware()
    }

    Shortcut {
        sequence: "Ctrl+0"
        onActivated: showModTracker()
    }

    // Navigation
    Shortcut {
        sequence: "Alt+Left"
        onActivated: goBack()
    }

    Shortcut {
        sequence: "Alt+Right"
        onActivated: {
            // Forward navigation (would need forward history)
            console.log("Forward navigation not implemented")
        }
    }

    Shortcut {
        sequence: "Ctrl+Tab"
        onActivated: cycleThroughRecentApps()
    }

    function cycleThroughRecentApps() {
        if (recentApps.length > 1) {
            var currentIndex = recentApps.findIndex(app => app.id === currentMode)
            var nextIndex = (currentIndex + 1) % recentApps.length
            var nextApp = recentApps[nextIndex]

            switch(nextApp.id) {
                case "launcher": showLauncher(); break
                case "player": showPlayer(); break
                case "audioeditor": showAudioEditor(); break
                case "videoeditor": showVideoEditor(); break
                case "burner": showBurner(); break
                case "labelmaker": showLabelMaker(); break
                case "karaoke": showKaraoke(); break
                case "djmixer": showDJMix(); break
                case "converter": showConverter(); break
                case "middleware": showMiddleware(); break
                case "modtracker": showModTracker(); break
            }
        }
    }

    // Window Management
    Shortcut {
        sequence: "F11"
        onActivated: {
            if (rootWindow.visibility === Window.FullScreen) {
                rootWindow.visibility = Window.Windowed
                fullscreenMode = false
            } else {
                rootWindow.visibility = Window.FullScreen
                fullscreenMode = true
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+F"
        onActivated: {
            if (rootWindow.visibility === Window.FullScreen) {
                rootWindow.visibility = Window.Windowed
                fullscreenMode = false
            } else {
                rootWindow.visibility = Window.FullScreen
                fullscreenMode = true
            }
        }
    }

    Shortcut {
        sequence: "Esc"
        onActivated: {
            if (rootWindow.visibility === Window.FullScreen) {
                rootWindow.visibility = Window.Windowed
                fullscreenMode = false
            } else if (errorDisplay.visible) {
                errorDisplay.visible = false
            } else if (debugOverlay.visible) {
                debugOverlay.visible = false
            }
        }
    }

    // System
    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: Qt.quit()
    }

    Shortcut {
        sequence: "Ctrl+W"
        onActivated: {
            if (currentMode !== "launcher") {
                showLauncher()
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+S"
        onActivated: {
            if (currentMode === "audioeditor" || currentMode === "videoeditor" ||
                currentMode === "modtracker" || currentMode === "middleware") {
                // Trigger save in current app
                if (themeLoader.item && themeLoader.item.saveProject) {
                    themeLoader.item.saveProject()
                }
                }
        }
    }

    Shortcut {
        sequence: "Ctrl+Shift+S"
        onActivated: {
            if (currentMode === "audioeditor" || currentMode === "videoeditor" ||
                currentMode === "modtracker" || currentMode === "middleware") {
                // Trigger save as in current app
                if (themeLoader.item && themeLoader.item.saveProjectAs) {
                    themeLoader.item.saveProjectAs()
                }
                }
        }
    }

    // Help
    Shortcut {
        sequence: "F1"
        onActivated: showHelp()
    }

    // Debug
    Shortcut {
        sequence: "Ctrl+D"
        onActivated: debugOverlay.visible = !debugOverlay.visible
    }

    // Screenshot
    Shortcut {
        sequence: "Ctrl+Shift+S"
        onActivated: takeScreenshot()
    }

    // ============================================
    // 10. DEBUG & DEVELOPMENT TOOLS
    // ============================================

    // Debug Overlay
    Rectangle {
        id: debugOverlay
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        width: 320
        height: 240
        color: Qt.rgba(0, 0, 0, 0.85)
        border.color: "#ff8c00"
        border.width: 1
        visible: false
        z: 2000

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 5

            Text {
                text: "🔍 DEBUG INFO"
                color: "#ff8c00"
                font.bold: true
                font.family: "JetBrains Mono"
                font.pixelSize: 12
            }

            Text {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: {
                    var info = ""
                    info += "Mode: " + currentMode + "\n"
                    info += "Backend: " + (backendReady ? "✅ Ready" : "⏳ Initializing") + "\n"
                    info += "Session: " + sessionDuration + " min\n"
                    info += "Loader: " + themeLoader.status + "\n"
                    info += "FPS: " + (frameTime > 0 ? Math.round(1000 / frameTime) : "N/A") + "\n"
                    info += "CPU: " + cpuUsage.toFixed(1) + "%\n"
                    info += "RAM: " + memoryUsage.toFixed(1) + "%\n"
                    info += "Battery: " + Math.round(batteryLevel) + "%\n"
                    info += "Network: " + ["🔴", "🟡", "🟢"][networkStatus] + "\n"
                    info += "Recent Files: " + recentFiles.length + "\n"
                    info += "Failed: " + failedComponents.length + "\n"
                    info += "Middleware: " + (middlewareBackend ? "✅" : "❌") + "\n"
                    info += "Converter: " + (converterBackend ? "✅" : "❌") + "\n"
                    info += "Screen: " + Screen.width + "x" + Screen.height + "\n"
                    info += "Scale: " + Screen.pixelDensity.toFixed(2) + "\n"
                    return info
                }
                color: "#aaaaaa"
                font.family: "JetBrains Mono"
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }

            RowLayout {
                Button {
                    text: "Reload UI"
                    onClicked: {
                        switchMode(currentMode, themeLoader.source, { forceReload: true })
                    }
                    Layout.fillWidth: true
                }

                Button {
                    text: "Clear Cache"
                    onClicked: {
                        recentFiles = []
                        recentPlaylists = []
                        recentProjects = []
                        globalSettings.recentFiles = []
                        globalSettings.recentPlaylists = []
                        globalSettings.recentProjects = []
                        showNotification("info", "Cache Cleared", "All caches cleared")
                    }
                    Layout.fillWidth: true
                }
            }
        }
    }

    // Frame time calculation
    property real frameTime: 0
    property real lastFrameTime: Date.now()

    Timer {
        interval: 1000
        running: debugOverlay.visible
        repeat: true
        onTriggered: {
            var now = Date.now()
            frameTime = now - lastFrameTime
            lastFrameTime = now
        }
    }

    // ============================================
    // 11. SIGNALS
    // ============================================

    signal uiReady()
    signal appUsageChanged(string appId, int count)
    signal systemMetricsUpdated(real cpu, real memory, real disk)
    signal recentFileAdded(var fileInfo)
    signal modeChanged(string newMode, string oldMode)
    signal networkStatusChanged(int status)
    signal sessionTimeUpdated(int minutes)
    signal projectModifiedChanged(bool modified)
    signal mediaStateChanged(bool playing, real position, real duration)

    onCurrentModeChanged: {
        modeChanged(currentMode, modeHistory.length > 0 ?
        (modeHistory[modeHistory.length-1] ? modeHistory[modeHistory.length-1].mode : "") : "")
    }

    onProjectModifiedChanged: {
        console.log("📝 Project modification state:", modified)
    }

    onIsPlayingChanged: {
        mediaStateChanged(isPlaying, mediaPosition, mediaDuration)
    }

    // ============================================
    // 12. CLEANUP & SHUTDOWN
    // ============================================

    // Exit confirmation dialog
    Dialog {
        id: exitConfirmDialog
        title: "Exit Aegis Media Suite?"
        standardButtons: Dialog.Yes | Dialog.No
        width: 400

        ColumnLayout {
            spacing: 10

            Text {
                text: "Are you sure you want to exit?\n\n" +
                "Session duration: " + sessionDuration + " minutes\n" +
                "Recent files: " + recentFiles.length + "\n" +
                "Active applications: " + recentApps.length
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            CheckBox {
                id: dontAskAgainCheck
                text: "Don't ask again"
                onCheckedChanged: {
                    globalSettings.confirmExit = !checked
                }
            }
        }

        onAccepted: Qt.quit()
    }

    // Help function
    function showHelp() {
        var helpMessage = "Aegis Media Suite v2.1.1 Help\n\n" +
        "• F1 - Show this help\n" +
        "• Ctrl+L - Return to Launcher\n" +
        "• Ctrl+1-9 - Switch applications\n" +
        "• Ctrl+Tab - Cycle through apps\n" +
        "• Alt+Left - Go back\n" +
        "• F11 - Toggle fullscreen\n" +
        "• Ctrl+D - Debug overlay\n" +
        "• Space - Play/Pause media\n\n" +
        "Visit help.aegis.media for documentation."

        showNotification("info", "Help", helpMessage)
    }

    // Project management functions
    function createNewProject(type, template) {
        var project = {
            id: "project_" + Date.now(),
            name: "Untitled " + type,
            type: type,
            template: template,
            created: new Date().toISOString(),
            modified: new Date().toISOString(),
            data: {}
        }

        currentProject = project
        projectModified = true

        console.log("📁 Created new project:", project.name)
        return project
    }

    function loadProject(filePath) {
        // This would load project from file in real implementation
        var project = {
            id: "loaded_project",
            name: filePath.split('/').pop().split('\\').pop(),
            type: getFileType(filePath),
            path: filePath,
            loaded: new Date().toISOString()
        }

        currentProject = project
        projectModified = false
        addRecentFile(filePath, "project")

        console.log("📁 Loaded project:", project.name)
        return project
    }

    function saveProject(filePath) {
        if (!currentProject) return false

            // This would save project to file in real implementation
            currentProject.modified = new Date().toISOString()
            projectModified = false

            console.log("💾 Saved project:", currentProject.name)
            showNotification("success", "Project Saved",
                             currentProject.name + " saved successfully")

            return true
    }

    // Audio middleware integration
    function setupAudioBridge(sourceApp, targetApp, protocol) {
        if (!middlewareBackend) {
            showError("Middleware Not Available",
                      "Audio middleware is not loaded. Please check initialization.")
            return false
        }

        var endpointName = sourceApp + "_to_" + targetApp
        var success = middlewareBackend.createEndpoint(endpointName, protocol, StreamDirection.Duplex)

        if (success) {
            showNotification("success", "Audio Bridge Created",
                             "Bridge created: " + sourceApp + " → " + targetApp)
            return true
        } else {
            showError("Bridge Creation Failed",
                      "Failed to create audio bridge between " + sourceApp + " and " + targetApp)
            return false
        }
    }
}

// ============================================
// NOTIFICATION COMPONENT
// ============================================

// Notification.qml (inline component)
Component {
    id: notificationComponent

    Rectangle {
        id: notification
        width: parent.width
        height: 70
        radius: 6
        color: {
            switch(notificationType) {
                case "error": return "#331a1a"
                case "warning": return "#332b1a"
                case "success": return "#1a3320"
                case "info":
                default: return "#1a1a33"
            }
        }
        border.color: {
            switch(notificationType) {
                case "error": return "#ff4444"
                case "warning": return "#ffaa44"
                case "success": return "#44ff88"
                case "info":
                default: return "#4488ff"
            }
        }
        border.width: 1

        property string notificationType: "info"
        property string notificationTitle: ""
        property string notificationMessage: ""
        property int duration: 5000

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 15

            // Icon
            Text {
                text: {
                    switch(notificationType) {
                        case "error": return "❌"
                        case "warning": return "⚠️"
                        case "success": return "✅"
                        case "info":
                        default: return "ℹ️"
                    }
                }
                font.pixelSize: 24
                Layout.alignment: Qt.AlignTop
            }

            // Content
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    text: notificationTitle
                    color: "white"
                    font.bold: true
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Text {
                    text: notificationMessage
                    color: "#aaaaaa"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }

            // Close button
            Button {
                text: "✕"
                flat: true
                onClicked: notification.destroy()
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
            }
        }

        // Auto-dismiss timer
        Timer {
            id: dismissTimer
            interval: notification.duration
            onTriggered: notification.destroy()
        }

        Component.onCompleted: {
            dismissTimer.start()
            opacity = 0
            y = -height

            // Slide in animation
            SequentialAnimation {
                NumberAnimation { target: notification; property: "opacity"; to: 1.0; duration: 300 }
                NumberAnimation { target: notification; property: "y"; to: 0; duration: 300; easing.type: Easing.OutBack }
            }.start()
        }

        function destroyOnFinish() {
            // Slide out animation
            SequentialAnimation {
                NumberAnimation { target: notification; property: "opacity"; to: 0; duration: 300 }
                NumberAnimation { target: notification; property: "y"; to: -height; duration: 300; easing.type: Easing.InBack }
                ScriptAction { script: notification.destroy() }
            }.start()
        }

        MouseArea {
            anchors.fill: parent
            onClicked: notification.destroyOnFinish()
        }
    }
}
