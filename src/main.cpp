// main.cpp
// Aegis Multimedia Suite - Application Entry Point with Capture Integration

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QLibraryInfo>
#include <QTranslator>
#include <QLockFile>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTextStream>
#include <QSurfaceFormat>
#include <QUrl>
#include <QUuid>
#include <QFile>
#include <QHash>
#include <QTimer>
#include <QSystemTrayIcon>
#include <QQuickWindow>
#include <QIcon>
#include <QSettings>
#include <memory>

// Core components
#include "core.h"
#include "audio.h"
#include "video.h"
#include "capture.h"

// Plugin interface
#include "plugin_interface.h"

// ============================================================
// Application version — single source of truth
// ============================================================
#define AEGIS_VERSION "1.0.0"

namespace Aegis {

    // Application modes - EXPANDED with capture modes
    enum class AppMode {
        MediaSuite,              // Launcher mode
        MediaPlayer,             // Mediaplayer
        AudioEditor,             // Audio Editor
        VideoEditor,             // Video Editor
        DAW,                     // DAW
        MiddlewareEditor,        // Middleware Editor based on DAW technology
        MusicNotationEditor,     // Notation Editor based on DAW technology
        DJMixer,                 // DJ App
        KaraokePlayer,           // Karaoke player
        ScreenRecorder,          // Screen/Window capture
        WebcamRecorder,          // Webcam capture
        CaptureCardRecorder,     // HDMI/SDI capture
        DVBTuner,                // DVB-T/S/C television
        IPCameraViewer,          // Network camera viewer
        AudioRecorder,           // Audio-only recording
        StreamingStudio,         // Live streaming mode
        DiscBurner,              // Disc Burner utility
        Converter,               // File converter
        LabelMaker,              // Label maker utility
    };

    // Application configuration
    struct AppConfig {
        AppMode startupMode = AppMode::MediaSuite;
        QStringList files;
        QVariantMap options;
        bool singleInstance = true;
        bool startMinimized = false;
        bool enableTray = true;
        QString style = "fusion";
        QString theme = "dark";
        QSize windowSize = QSize(1280, 720);
        QPoint windowPosition;
        bool rememberWindowGeometry = true;
    };

} // namespace Aegis

// ============================================================
// ApplicationManager (Singleton with IPC)
// ============================================================
class ApplicationManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isPrimaryInstance READ isPrimaryInstance NOTIFY primaryInstanceChanged)
    Q_PROPERTY(QString instanceId READ instanceId CONSTANT)
    Q_PROPERTY(QStringList activeModes READ activeModes NOTIFY activeModesChanged)

public:
    static ApplicationManager& instance() {
        static ApplicationManager manager;
        return manager;
    }

    bool isPrimaryInstance() const { return m_isPrimary; }
    QString instanceId() const { return m_instanceId; }
    QStringList activeModes() const { return m_activeModes; }

    // Send message to primary instance
    bool sendToPrimary(const QString &message, const QStringList &files = {}) {
        if (m_isPrimary) {
            // Already primary, handle locally
            handleMessage(message, files);
            return true;
        }

        // TODO: Implement actual IPC (QSharedMemory, QLocalSocket, D-Bus)
        qDebug() << "IPC not fully implemented, running as independent instance";

        // For now, just log and return false
        emit messageSent(message, files);
        return false;
    }

    // Register an active mode window
    void registerModeWindow(Aegis::AppMode mode, QWindow *window) {
        QString modeStr = modeToString(mode);
        m_modeWindows[modeStr] = window;
        if (!m_activeModes.contains(modeStr)) {
            m_activeModes.append(modeStr);
            emit activeModesChanged();
        }

        connect(window, &QWindow::destroyed, this, [this, modeStr]() {
            m_modeWindows.remove(modeStr);
            m_activeModes.removeAll(modeStr);
            emit activeModesChanged();
        });
    }

    // Get window for a specific mode
    QWindow* getModeWindow(const QString &mode) const {
        return m_modeWindows.value(mode, nullptr);
    }

    // Activate a specific mode (raise window)
    void activateMode(const QString &mode) {
        QWindow *win = m_modeWindows.value(mode, nullptr);
        if (win) {
            win->raise();
            win->requestActivate();
        }
    }

signals:
    void primaryInstanceChanged();
    void activeModesChanged();
    void messageReceived(const QString &message, const QStringList &files);
    void messageSent(const QString &message, const QStringList &files);
    void fileOpenRequested(const QStringList &files, Aegis::AppMode mode);

private slots:
    void handleMessage(const QString &message, const QStringList &files) {
        if (message == "open" && !files.isEmpty()) {
            emit fileOpenRequested(files, detectModeFromFiles(files));
        }
        emit messageReceived(message, files);
    }

private:
    ApplicationManager()
    : m_instanceId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        QString lockPath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (lockPath.isEmpty()) {
            lockPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        }

        QDir().mkpath(lockPath);
        lockPath += "/aegis_" + QCoreApplication::applicationName() + ".lock";

        m_lockFile = new QLockFile(lockPath);
        m_lockFile->setStaleLockTime(30000); // 30 seconds
        m_isPrimary = m_lockFile->tryLock();

        if (!m_isPrimary) {
            qDebug() << "Secondary instance detected, ID:" << m_instanceId;
        } else {
            qDebug() << "Primary instance started, ID:" << m_instanceId;
        }

        // Set up single-instance handling
        if (m_isPrimary) {
            // TODO: Set up IPC server
        }
    }

    ~ApplicationManager() {
        if (m_lockFile) {
            m_lockFile->unlock();
            delete m_lockFile;
        }
    }

    Aegis::AppMode detectModeFromFiles(const QStringList &files) {
        if (files.isEmpty()) return Aegis::AppMode::MediaSuite;

        QString firstFile = files.first();
        QFileInfo info(firstFile);
        QString suffix = info.suffix().toLower();

        // Capture-related file types
        static const QStringList captureExtensions = {
            "cap", "capture", "recording", "screenrec"
        };
        if (captureExtensions.contains(suffix)) return Aegis::AppMode::ScreenRecorder;

        static const QStringList dvbExtensions = {
            "ts", "m2ts", "mts", "trp"
        };
        if (dvbExtensions.contains(suffix)) return Aegis::AppMode::DVBTuner;

        // Video project files
        static const QStringList videoProjectExtensions = {
            "aegisvid", "kdenlive", "mlt", "prproj", "aep", "veg"
        };
        if (videoProjectExtensions.contains(suffix)) return Aegis::AppMode::VideoEditor;

        // Audio project files
        static const QStringList dawProjectExtensions = {
            "aegisproj", "flp", "als", "ptx", "logicx", "cpr", "reaper"
        };
        if (dawProjectExtensions.contains(suffix)) return Aegis::AppMode::DAW;

        // Audio files
        static const QStringList audioExtensions = {
            "wav", "flac", "mp3", "ogg", "m4a", "opus", "aac", "wma"
        };
        if (audioExtensions.contains(suffix)) return Aegis::AppMode::AudioEditor;

        // Video files
        static const QStringList videoExtensions = {
            "mp4", "mkv", "avi", "mov", "webm", "wmv", "flv", "m4v"
        };
        if (videoExtensions.contains(suffix)) return Aegis::AppMode::MediaPlayer;

        // Karaoke files
        static const QStringList karaokeExtensions = {"cdg", "kfn", "kar", "ksf"};
        if (karaokeExtensions.contains(suffix)) return Aegis::AppMode::KaraokePlayer;

        // Disc image files
        static const QStringList discExtensions = {
            "iso", "img", "nrg", "bin", "cue", "mds", "dmg"
        };
        if (discExtensions.contains(suffix)) return Aegis::AppMode::DiscBurner;

        return Aegis::AppMode::MediaSuite;
    }

    QString modeToString(Aegis::AppMode mode) const {
        switch (mode) {
            case Aegis::AppMode::MediaSuite: return "mediasuite";
            case Aegis::AppMode::MediaPlayer: return "mediaplayer";
            case Aegis::AppMode::AudioEditor: return "audioeditor";
            case Aegis::AppMode::VideoEditor: return "videoeditor";
            case Aegis::AppMode::DAW: return "daw";
            case Aegis::AppMode::DiscBurner: return "discburner";
            case Aegis::AppMode::DJMixer: return "djmixer";
            case Aegis::AppMode::KaraokePlayer: return "karaoke";
            case Aegis::AppMode::MusicNotationEditor: return "notation";
            case Aegis::AppMode::Converter: return "converter";
            case Aegis::AppMode::MiddlewareEditor: return "middleware";
            case Aegis::AppMode::LabelMaker: return "labelmaker";
            case Aegis::AppMode::ScreenRecorder: return "screenrecorder";
            case Aegis::AppMode::WebcamRecorder: return "webcamrecorder";
            case Aegis::AppMode::CaptureCardRecorder: return "capturecard";
            case Aegis::AppMode::DVBTuner: return "dvbtuner";
            case Aegis::AppMode::IPCameraViewer: return "ipcamera";
            case Aegis::AppMode::AudioRecorder: return "audiorecorder";
            case Aegis::AppMode::StreamingStudio: return "streaming";
            default: return "unknown";
        }
    }

    bool m_isPrimary = true;
    QString m_instanceId;
    QLockFile *m_lockFile = nullptr;
    QStringList m_activeModes;
    QHash<QString, QWindow*> m_modeWindows;
};

// ============================================================
// CommandLineHandler - Enhanced for capture modes
// ============================================================
class CommandLineHandler {
public:
    struct ParsedArgs {
        Aegis::AppMode mode = Aegis::AppMode::MediaSuite;
        QStringList files;
        QVariantMap options;
        Aegis::AppConfig config;
        bool helpRequested = false;
        bool versionRequested = false;
        bool listSources = false;        // ADDED: List capture sources
        QString captureSource;            // ADDED: Specific capture source
        int captureDuration = 0;          // ADDED: Capture duration in seconds
        QString captureOutput;             // ADDED: Output file path
    };

    static ParsedArgs parse(const QStringList &args) {
        ParsedArgs result;

        if (args.size() <= 1) {
            result.mode = detectModeFromBinary(args.value(0));
            return result;
        }

        for (int i = 1; i < args.size(); ++i) {
            const QString &arg = args[i];

            if (arg == "--help" || arg == "-h") {
                result.helpRequested = true;
            } else if (arg == "--version" || arg == "-v") {
                result.versionRequested = true;
            } else if (arg == "--list-sources" || arg == "-l") {
                result.listSources = true;
            } else if (arg.startsWith("--source=")) {
                result.captureSource = arg.mid(9);
            } else if (arg.startsWith("--duration=")) {
                result.captureDuration = arg.mid(11).toInt();
            } else if (arg.startsWith("--output=")) {
                result.captureOutput = arg.mid(9);
            } else if (arg.startsWith("--mode=")) {
                result.mode = stringToMode(arg.mid(7));
            } else if (arg == "--single-instance") {
                result.config.singleInstance = true;
            } else if (arg == "--no-single-instance") {
                result.config.singleInstance = false;
            } else if (arg == "--minimized") {
                result.config.startMinimized = true;
            } else if (arg == "--no-tray") {
                result.config.enableTray = false;
            } else if (arg.startsWith("--style=")) {
                result.config.style = arg.mid(8);
            } else if (arg.startsWith("--theme=")) {
                result.config.theme = arg.mid(8);
            } else if (arg.startsWith("--geometry=")) {
                QString geom = arg.mid(11);
                QStringList parts = geom.split('x');
                if (parts.size() == 2) {
                    result.config.windowSize = QSize(parts[0].toInt(), parts[1].toInt());
                }
            } else if (arg.startsWith("--")) {
                // Generic option
                int eq = arg.indexOf('=');
                if (eq > 0) {
                    result.options[arg.mid(2, eq - 2)] = arg.mid(eq + 1);
                }
            } else if (!arg.startsWith("-")) {
                // File argument
                if (QFile::exists(arg) || QUrl(arg).isValid()) {
                    result.files.append(arg);
                }
            }
        }

        // Auto-detect mode from first file if not specified
        if (result.mode == Aegis::AppMode::MediaSuite && !result.files.isEmpty()) {
            result.mode = detectModeFromFile(result.files.first());
        }

        return result;
    }

private:
    static Aegis::AppMode detectModeFromBinary(const QString &argv0) {
        if (argv0.isEmpty()) return Aegis::AppMode::MediaSuite;

        QString baseName = QFileInfo(argv0).baseName().toLower();

        static const QHash<QString, Aegis::AppMode> modeMap = {
            // Launcher
            {"aegis",              Aegis::AppMode::MediaSuite},
            {"aegis_launcher",     Aegis::AppMode::MediaSuite},

            // Media Player
            {"aegis_mediaplayer",  Aegis::AppMode::MediaPlayer},
            {"aegis_player",       Aegis::AppMode::MediaPlayer},

            // Audio Editor
            {"aegis_audioeditor",  Aegis::AppMode::AudioEditor},
            {"aegis_soundeditor",  Aegis::AppMode::AudioEditor},

            // Video Editor
            {"aegis_videoeditor",  Aegis::AppMode::VideoEditor},
            {"aegis_editor",       Aegis::AppMode::VideoEditor},

            // DAW
            {"aegis_daw",          Aegis::AppMode::DAW},
            {"aegis_studio",       Aegis::AppMode::DAW},

            // Capture modes - ADDED
            {"aegis_screenrec",    Aegis::AppMode::ScreenRecorder},
            {"aegis_recorder",     Aegis::AppMode::ScreenRecorder},
            {"aegis_capture",      Aegis::AppMode::ScreenRecorder},

            {"aegis_webcam",       Aegis::AppMode::WebcamRecorder},
            {"aegis_camera",       Aegis::AppMode::WebcamRecorder},

            {"aegis_capturecard",  Aegis::AppMode::CaptureCardRecorder},
            {"aegis_hdmi",         Aegis::AppMode::CaptureCardRecorder},

            {"aegis_dvb",          Aegis::AppMode::DVBTuner},
            {"aegis_tv",           Aegis::AppMode::DVBTuner},
            {"aegis_television",   Aegis::AppMode::DVBTuner},

            {"aegis_ipcam",        Aegis::AppMode::IPCameraViewer},
            {"aegis_networkcam",   Aegis::AppMode::IPCameraViewer},

            {"aegis_audiorec",     Aegis::AppMode::AudioRecorder},
            {"aegis_microphone",   Aegis::AppMode::AudioRecorder},

            {"aegis_stream",       Aegis::AppMode::StreamingStudio},
            {"aegis_live",         Aegis::AppMode::StreamingStudio},

            // Other modes
            {"aegis_discburner",   Aegis::AppMode::DiscBurner},
            {"aegis_djmix",        Aegis::AppMode::DJMixer},
            {"aegis_karaoke",      Aegis::AppMode::KaraokePlayer},
            {"aegis_notation",     Aegis::AppMode::MusicNotationEditor},
            {"aegis_labelmaker",   Aegis::AppMode::LabelMaker}
        };

        return modeMap.value(baseName, Aegis::AppMode::MediaSuite);
    }

    static Aegis::AppMode detectModeFromFile(const QString &file) {
        QFileInfo info(file);
        QString suffix = info.suffix().toLower();

        // Capture recordings
        if (suffix == "screenrec" || suffix == "cap")
            return Aegis::AppMode::ScreenRecorder;

        // DVB recordings
        if (suffix == "ts" || suffix == "mts" || suffix == "m2ts")
            return Aegis::AppMode::DVBTuner;

        // Video project files
        static const QStringList videoProjectExtensions = {
            "aegisvid", "kdenlive", "mlt", "prproj", "aep", "veg"
        };
        if (videoProjectExtensions.contains(suffix))
            return Aegis::AppMode::VideoEditor;

        // DAW project files
        static const QStringList dawProjectExtensions = {
            "aegisproj", "flp", "als", "ptx", "logicx", "cpr", "reaper"
        };
        if (dawProjectExtensions.contains(suffix))
            return Aegis::AppMode::DAW;

        // Audio files
        static const QStringList audioExtensions = {
            "wav", "flac", "mp3", "ogg", "m4a", "opus", "aac", "wma"
        };
        if (audioExtensions.contains(suffix))
            return Aegis::AppMode::AudioEditor;

        // Video files
        static const QStringList videoExtensions = {
            "mp4", "mkv", "avi", "mov", "webm", "wmv", "flv", "m4v"
        };
        if (videoExtensions.contains(suffix))
            return Aegis::AppMode::MediaPlayer;

        // Karaoke files
        static const QStringList karaokeExtensions = {"cdg", "kfn", "kar", "ksf"};
        if (karaokeExtensions.contains(suffix))
            return Aegis::AppMode::KaraokePlayer;

        // Disc images
        static const QStringList discExtensions = {
            "iso", "img", "nrg", "bin", "cue", "mds", "dmg"
        };
        if (discExtensions.contains(suffix))
            return Aegis::AppMode::DiscBurner;

        return Aegis::AppMode::MediaSuite;
    }

    static Aegis::AppMode stringToMode(const QString &modeStr) {
        QString lower = modeStr.toLower();

        static const QHash<QString, Aegis::AppMode> modeMap = {
            {"mediasuite",      Aegis::AppMode::MediaSuite},
            {"launcher",        Aegis::AppMode::MediaSuite},

            {"player",          Aegis::AppMode::MediaPlayer},
            {"mediaplayer",     Aegis::AppMode::MediaPlayer},

            {"audioeditor",     Aegis::AppMode::AudioEditor},
            {"audio-editor",    Aegis::AppMode::AudioEditor},

            {"videoeditor",     Aegis::AppMode::VideoEditor},
            {"video-editor",    Aegis::AppMode::VideoEditor},

            {"daw",             Aegis::AppMode::DAW},
            {"studio",          Aegis::AppMode::DAW},

            {"discburner",      Aegis::AppMode::DiscBurner},
            {"burner",          Aegis::AppMode::DiscBurner},

            {"djmixer",         Aegis::AppMode::DJMixer},
            {"dj",              Aegis::AppMode::DJMixer},

            {"karaoke",         Aegis::AppMode::KaraokePlayer},

            {"notation",        Aegis::AppMode::MusicNotationEditor},
            {"score",           Aegis::AppMode::MusicNotationEditor},

            {"converter",       Aegis::AppMode::Converter},
            {"convert",         Aegis::AppMode::Converter},

            {"labelmaker",      Aegis::AppMode::LabelMaker},
            {"label",           Aegis::AppMode::LabelMaker},

            // Capture modes - ADDED
            {"screenrecorder",  Aegis::AppMode::ScreenRecorder},
            {"screen",          Aegis::AppMode::ScreenRecorder},
            {"record",          Aegis::AppMode::ScreenRecorder},

            {"webcam",          Aegis::AppMode::WebcamRecorder},
            {"camera",          Aegis::AppMode::WebcamRecorder},

            {"capturecard",     Aegis::AppMode::CaptureCardRecorder},
            {"hdmi",            Aegis::AppMode::CaptureCardRecorder},

            {"dvbtuner",        Aegis::AppMode::DVBTuner},
            {"dvb",             Aegis::AppMode::DVBTuner},
            {"tv",              Aegis::AppMode::DVBTuner},

            {"ipcamera",        Aegis::AppMode::IPCameraViewer},
            {"ipcam",           Aegis::AppMode::IPCameraViewer},
            {"networkcam",      Aegis::AppMode::IPCameraViewer},

            {"audiorecorder",   Aegis::AppMode::AudioRecorder},
            {"microphone",      Aegis::AppMode::AudioRecorder},

            {"streaming",       Aegis::AppMode::StreamingStudio},
            {"live",            Aegis::AppMode::StreamingStudio}
        };

        return modeMap.value(lower, Aegis::AppMode::MediaSuite);
    }
};

// ============================================================
// ApplicationInitializer - Enhanced with capture setup
// ============================================================
class ApplicationInitializer {
public:
    static void setupApplication(QApplication &app) {
        app.setApplicationName("Aegis");
        app.setOrganizationName("Aegis");
        app.setOrganizationDomain("org.aegis");
        app.setApplicationVersion(AEGIS_VERSION);

        // Set application icon
        app.setWindowIcon(QIcon(":/icons/aegis.png"));

        // High DPI support
        #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        app.setAttribute(Qt::AA_EnableHighDpiScaling);
        app.setAttribute(Qt::AA_UseHighDpiPixmaps);
        #endif

        // Set default style
        QStyle *style = QStyleFactory::create("Fusion");
        if (style) {
            app.setStyle(style);
        }

        // Register QML types
        registerQmlTypes();

        // Load translations
        loadTranslations(app);

        // Set up OpenGL surface format
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setVersion(3, 3);
        format.setSamples(4);
        format.setAlphaBufferSize(8);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        QSurfaceFormat::setDefaultFormat(format);

        // Set up application-wide settings
        QCoreApplication::setOrganizationName("Aegis");
        QCoreApplication::setApplicationName("Aegis");
    }

    static void setupEnvironment() {
        // Create application data directories
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataPath);

        // Create capture directories
        QString capturePath = dataPath + "/captures";
        QDir().mkpath(capturePath);

        QString screenshotsPath = dataPath + "/screenshots";
        QDir().mkpath(screenshotsPath);

        QString dvbPath = dataPath + "/dvb";
        QDir().mkpath(dvbPath);

        QString ipcamPath = dataPath + "/ipcameras";
        QDir().mkpath(ipcamPath);

        // Set environment variables for GStreamer
        qputenv("GST_PLUGIN_PATH", QFile::encodeName(dataPath + "/gstreamer/plugins"));

        // Log environment setup
        qDebug() << "Application data path:" << dataPath;
        qDebug() << "Capture path:" << capturePath;
    }

    static void registerQmlTypes() {
        // Register core types
        qmlRegisterType<Aegis::AudioEngine>("Aegis.Audio", 1, 0, "AudioEngine");
        qmlRegisterType<Aegis::VideoEngine>("Aegis.Video", 1, 0, "VideoEngine");

        // Register Capture class - ADDED
        qmlRegisterType<Capture>("Aegis.Capture", 1, 0, "Capture");

        // Register enums
        qmlRegisterUncreatableType<Capture>("Aegis.Capture", 1, 0, "CaptureSourceType",
                                            "Enum type for capture sources");

        // Register structures
        qRegisterMetaType<CaptureSourceInfo>();
        qRegisterMetaType<RecordingStats>();
        qRegisterMetaType<ZoomKeyframe>();
        qRegisterMetaType<BackgroundConfig>();
        qRegisterMetaType<CaptureExportOptions>();
    }

    static void loadTranslations(QApplication &app) {
        QString locale = QLocale::system().name();
        auto *translator = new QTranslator(&app);

        // Try to load from system translations path
        QString translationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
        if (translator->load("aegis_" + locale, translationsPath)) {
            app.installTranslator(translator);
        } else {
            // Try from application directory
            QString appPath = QCoreApplication::applicationDirPath();
            if (translator->load("aegis_" + locale, appPath + "/translations")) {
                app.installTranslator(translator);
            } else {
                delete translator;
            }
        }
    }

    static void setupLogging() {
        // Set up logging rules from environment or settings
        QString logRules = qEnvironmentVariable("AEGIS_LOG", "*.debug=false");
        QLoggingCategory::setFilterRules(logRules);

        // Install custom message handler for file logging
        static bool installed = false;
        if (!installed) {
            qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &msg) {
                // Write to console
                QByteArray localMsg = msg.toLocal8Bit();
                switch (type) {
                    case QtDebugMsg:
                        fprintf(stderr, "Debug: %s (%s:%u, %s)\n", localMsg.constData(),
                                context.file, context.line, context.function);
                        break;
                    case QtInfoMsg:
                        fprintf(stderr, "Info: %s\n", localMsg.constData());
                        break;
                    case QtWarningMsg:
                        fprintf(stderr, "Warning: %s\n", localMsg.constData());
                        break;
                    case QtCriticalMsg:
                        fprintf(stderr, "Critical: %s\n", localMsg.constData());
                        break;
                    case QtFatalMsg:
                        fprintf(stderr, "Fatal: %s\n", localMsg.constData());
                        abort();
                }

                // Also write to log file
                QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                + "/aegis.log";
        QFile logFile(logPath);
        if (logFile.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream stream(&logFile);
            stream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << " ";
            stream << msg << "\n";
            logFile.close();
        }

        fflush(stderr);
            });
            installed = true;
        }
    }
};

// ============================================================
// Plugin base & concrete implementations - ENHANCED with capture plugins
// ============================================================
namespace Aegis {

    class BasePlugin : public AppModePlugin {
    public:
        QString displayName() const override { return m_displayName; }
        bool initialize(const AppContext& context) override {
            m_context = context;
            return true;
        }
        void shutdown() override {}
        void handleArguments(const QStringList& args) override {
            m_context.arguments = args;
        }
        QString qmlEntryPoint() const override { return m_qmlPath; }
        QString modeName() const override { return m_modeName; }

    protected:
        QString m_modeName;
        QString m_displayName;
        QString m_qmlPath;
        AppContext m_context;
    };

    // Media Player Plugin
    class MediaPlayerPlugin : public BasePlugin {
    public:
        MediaPlayerPlugin() {
            m_modeName = "mediaplayer";
            m_displayName = "Media Player";
            m_qmlPath = "qrc:/qml/MediaPlayer/Main.qml";
        }
    };

    // Audio Editor Plugin
    class AudioEditorPlugin : public BasePlugin {
    public:
        AudioEditorPlugin() {
            m_modeName = "audioeditor";
            m_displayName = "Audio Editor";
            m_qmlPath = "qrc:/qml/AudioEditor/Main.qml";
        }
    };

    // Video Editor Plugin
    class VideoEditorPlugin : public BasePlugin {
    public:
        VideoEditorPlugin() {
            m_modeName = "videoeditor";
            m_displayName = "Video Editor";
            m_qmlPath = "qrc:/qml/VideoEditor/Main.qml";
        }
    };

    // DAW Plugin
    class DAWPlugin : public BasePlugin {
    public:
        DAWPlugin() {
            m_modeName = "daw";
            m_displayName = "Digital Audio Workstation";
            m_qmlPath = "qrc:/qml/DAW/Main.qml";
        }
    };

    // Disc Burner Plugin
    class DiscBurnerPlugin : public BasePlugin {
    public:
        DiscBurnerPlugin() {
            m_modeName = "discburner";
            m_displayName = "Disc Burner";
            m_qmlPath = "qrc:/qml/DiscBurner/Main.qml";
        }
    };

    // DJ Mixer Plugin
    class DJMixerPlugin : public BasePlugin {
    public:
        DJMixerPlugin() {
            m_modeName = "djmixer";
            m_displayName = "DJ Mixer";
            m_qmlPath = "qrc:/qml/DJMixer/Main.qml";
        }
    };

    // Karaoke Player Plugin
    class KaraokePlugin : public BasePlugin {
    public:
        KaraokePlugin() {
            m_modeName = "karaoke";
            m_displayName = "Karaoke Player";
            m_qmlPath = "qrc:/qml/Karaoke/Main.qml";
        }
    };

    // Music Notation Editor Plugin
    class NotationPlugin : public BasePlugin {
    public:
        NotationPlugin() {
            m_modeName = "notation";
            m_displayName = "Music Notation Editor";
            m_qmlPath = "qrc:/qml/Notation/Main.qml";
        }
    };

    // Converter Plugin
    class ConverterPlugin : public BasePlugin {
    public:
        ConverterPlugin() {
            m_modeName = "converter";
            m_displayName = "Media Converter";
            m_qmlPath = "qrc:/qml/Converter/Main.qml";
        }
    };

    // Label Maker Plugin
    class LabelMakerPlugin : public BasePlugin {
    public:
        LabelMakerPlugin() {
            m_modeName = "labelmaker";
            m_displayName = "Label Maker";
            m_qmlPath = "qrc:/qml/LabelMaker/Main.qml";
        }
    };

    // ============================================================
    // CAPTURE PLUGINS - ADDED
    // ============================================================

    // Screen Recorder Plugin
    class ScreenRecorderPlugin : public BasePlugin {
    public:
        ScreenRecorderPlugin() {
            m_modeName = "screenrecorder";
            m_displayName = "Screen Recorder";
            m_qmlPath = "qrc:/qml/Capture/ScreenRecorder.qml";
        }

        bool initialize(const AppContext& context) override {
            BasePlugin::initialize(context);

            // Create and configure capture instance
            Capture *capture = new Capture();
            capture->setExportOptions(context.captureOptions);

            // Store in context for QML access
            const_cast<AppContext&>(context).engine->rootContext()->setContextProperty("capture", capture);

            return true;
        }
    };

    // Webcam Recorder Plugin
    class WebcamRecorderPlugin : public BasePlugin {
    public:
        WebcamRecorderPlugin() {
            m_modeName = "webcamrecorder";
            m_displayName = "Webcam Recorder";
            m_qmlPath = "qrc:/qml/Capture/WebcamRecorder.qml";
        }

        bool initialize(const AppContext& context) override {
            BasePlugin::initialize(context);

            Capture *capture = new Capture();
            capture->setExportOptions(context.captureOptions);

            // Pre-select webcam source if available
            auto sources = capture->listSources(CaptureSourceType::Webcam);
            if (!sources.isEmpty()) {
                const_cast<AppContext&>(context).engine->rootContext()->setContextProperty("defaultSource",
                                                                                           QVariant::fromValue(sources.first()));
            }

            const_cast<AppContext&>(context).engine->rootContext()->setContextProperty("capture", capture);

            return true;
        }
    };

    // Capture Card Plugin
    class CaptureCardPlugin : public BasePlugin {
    public:
        CaptureCardPlugin() {
            m_modeName = "capturecard";
            m_displayName = "Capture Card";
            m_qmlPath = "qrc:/qml/Capture/CaptureCard.qml";
        }

        bool initialize(const AppContext& context) override {
            BasePlugin::initialize(context);

            Capture *capture = new Capture();
            capture->setExportOptions(context.captureOptions);

            const_cast<AppContext&>(context).engine->rootContext()->setContextProperty("capture", capture);

            return true;
        }
    };

    // DVB Tuner Plugin
    class DVBTunerPlugin : public BasePlugin {
    public:
        DVBTunerPlugin() {
            m_modeName = "dvbtuner";
            m_displayName = "DVB Television";
            m_qmlPath = "qrc:/qml/Capture/DVBTuner.qml";
        }

        bool initialize(const AppContext& context) override {
            BasePlugin::initialize(context);

            Capture *capture = new Capture();

            // Set up DVB-specific options
            CaptureExportOptions dvbOptions = context.captureOptions;
            dvbOptions.videoCodec = "copy";  // Passthrough for DVB
            dvbOptions.audioCodec = "copy";
            capture->setExportOptions(dvbOptions);

            const_cast<AppContext&>(context).engine->rootContext()->setContextProperty("capture", capture);

            return true;
        }
    };

    // IP Camera Viewer Plugin
    class IPCameraPlugin : public BasePlugin {
    public:
        IPCameraPlugin() {
            m_modeName = "ipcamera";
            m_displayName = "IP Camera Viewer";
            m_qmlPath = "qrc:/qml/Capture/IPCamera.qml";
        }

        bool initialize(const AppContext& context) override {
            BasePlugin::initialize(context);

            Capture *capture = new Capture();

            // Load saved cameras
            auto cameras = capture->listIPCameras();

            const_cast<AppContext&>(context).engine->rootContext()->setContextProperty("capture", capture);
            const_cast<AppContext&>(context).engine->rootContext()->setContextProperty("ipCameras",
                                                                                       QVariant::fromValue(cameras));

            return true;
        }
    };

    // Audio Recorder Plugin
    class AudioRecorderPlugin : public BasePlugin {
    public:
        AudioRecorderPlugin() {
            m_modeName = "audiorecorder";
            m_displayName = "Audio Recorder";
            m_qmlPath = "qrc:/qml/Capture/AudioRecorder.qml";
        }

        bool initialize(const AppContext& context) override {
            BasePlugin::initialize(context);

            Capture *capture = new Capture();

            // Set audio-only options
            CaptureExportOptions audioOptions = context.captureOptions;
            audioOptions.videoCodec = "none";
            capture->setExportOptions(audioOptions);

            const_cast<AppContext&>(context).engine->rootContext()->setContextProperty("capture", capture);

            return true;
        }
    };

    // Streaming Studio Plugin
    class StreamingStudioPlugin : public BasePlugin {
    public:
        StreamingStudioPlugin() {
            m_modeName = "streaming";
            m_displayName = "Streaming Studio";
            m_qmlPath = "qrc:/qml/Capture/StreamingStudio.qml";
        }

        bool initialize(const AppContext& context) override {
            BasePlugin::initialize(context);

            Capture *capture = new Capture();

            // Set streaming-optimized options
            CaptureExportOptions streamOptions = context.captureOptions;
            streamOptions.bitrate = 4000;  // 4 Mbps for streaming
            streamOptions.fps = 30;
            capture->setExportOptions(streamOptions);

            const_cast<AppContext&>(context).engine->rootContext()->setContextProperty("capture", capture);

            return true;
        }
    };

} // namespace Aegis

// ============================================================
// MainApplication Class
// ============================================================
class MainApplication {
public:
    MainApplication(int &argc, char **argv)
    : m_app(argc, argv)
    , m_engine(nullptr)
    , m_capture(nullptr)
    , m_trayIcon(nullptr)
    {}

    int run() {
        // Parse command line
        auto args = CommandLineHandler::parse(m_app.arguments());

        // Handle help/version
        if (args.helpRequested) {
            showHelp();
            return 0;
        }

        if (args.versionRequested) {
            showVersion();
            return 0;
        }

        // Initialize application
        ApplicationInitializer::setupApplication(m_app);
        ApplicationInitializer::setupEnvironment();
        ApplicationInitializer::setupLogging();

        // Handle source listing
        if (args.listSources) {
            listCaptureSources();
            return 0;
        }

        // Single instance handling
        auto &appManager = ApplicationManager::instance();
        if (args.config.singleInstance && !appManager.isPrimaryInstance()) {
            // Try to send files to primary instance
            if (!args.files.isEmpty()) {
                if (appManager.sendToPrimary("open", args.files)) {
                    qDebug() << "Files forwarded to primary instance";
                    return 0;
                }
            }
        }

        // Initialize plugins
        initializePlugins();

        // Create engine
        m_engine = std::make_unique<QQmlApplicationEngine>();

        // Set up context properties
        setupContextProperties(args);

        // Load the appropriate QML for the mode
        if (!loadModeQml(args.mode, args)) {
            return 1;
        }

        // Set up system tray if enabled
        if (args.config.enableTray) {
            setupSystemTray();
        }

        // Connect to application manager signals
        connect(&appManager, &ApplicationManager::fileOpenRequested,
                this, &MainApplication::handleFileOpenRequest);

        // Set up aboutToQuit handler
        QObject::connect(&m_app, &QApplication::aboutToQuit, [this]() {
            cleanup();
        });

        // Show main window
        if (!args.config.startMinimized && m_engine->rootObjects().size() > 0) {
            QWindow *mainWindow = qobject_cast<QWindow*>(m_engine->rootObjects().first());
            if (mainWindow) {
                // Apply geometry if specified
                if (args.config.windowSize.isValid()) {
                    mainWindow->resize(args.config.windowSize);
                }
                if (args.config.windowPosition.isValid()) {
                    mainWindow->setPosition(args.config.windowPosition);
                }
                mainWindow->show();

                // Register with application manager
                appManager.registerModeWindow(args.mode, mainWindow);
            }
        }

        // Execute application
        return m_app.exec();
    }

private:
    void initializePlugins() {
        auto &registry = Aegis::PluginRegistry::instance();

        // Core plugins
        registry.registerPlugin(std::make_unique<Aegis::MediaPlayerPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::AudioEditorPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::VideoEditorPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::DAWPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::DiscBurnerPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::DJMixerPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::KaraokePlugin>());
        registry.registerPlugin(std::make_unique<Aegis::NotationPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::ConverterPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::LabelMakerPlugin>());

        // Capture plugins - ADDED
        registry.registerPlugin(std::make_unique<Aegis::ScreenRecorderPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::WebcamRecorderPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::CaptureCardPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::DVBTunerPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::IPCameraPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::AudioRecorderPlugin>());
        registry.registerPlugin(std::make_unique<Aegis::StreamingStudioPlugin>());
    }

    void setupContextProperties(const CommandLineParser::ParsedArgs &args) {
        if (!m_engine) return;

        auto *context = m_engine->rootContext();

        // Basic properties
        context->setContextProperty("aegisVersion", AEGIS_VERSION);
        context->setContextProperty("applicationManager", &ApplicationManager::instance());
        context->setContextProperty("commandLineFiles", QVariant::fromValue(args.files));
        context->setContextProperty("commandLineOptions", args.options);

        // Create capture instance
        m_capture = new Capture();

        // Configure capture options
        CaptureExportOptions captureOpts;
        captureOpts.outputDir = args.captureOutput;
        if (!args.captureOutput.isEmpty()) {
            QFileInfo info(args.captureOutput);
            if (info.suffix() == "gif") captureOpts.exportGif = true;
            if (info.suffix() == "mp4") captureOpts.exportMp4 = true;
        }
        m_capture->setExportOptions(captureOpts);

        context->setContextProperty("capture", m_capture);

        // Pre-select source if specified
        if (!args.captureSource.isEmpty()) {
            auto sources = m_capture->listSources(CaptureSourceType::Unknown);
            for (const auto &source : sources) {
                if (source.id == args.captureSource || source.name.contains(args.captureSource)) {
                    context->setContextProperty("preselectedSource", QVariant::fromValue(source));
                    break;
                }
            }
        }

        // Set capture duration
        if (args.captureDuration > 0) {
            context->setContextProperty("captureDuration", args.captureDuration);
        }
    }

    bool loadModeQml(Aegis::AppMode mode, const CommandLineParser::ParsedArgs &args) {
        QString modeKey = modeToString(mode);

        auto *plugin = Aegis::PluginRegistry::instance().get(modeKey);
        if (!plugin) {
            qCritical() << "No plugin available for mode:" << modeKey;
            return false;
        }

        // Create app context
        Aegis::AppContext context;
        context.engine = m_engine.get();
        context.arguments = args.files;
        context.config = args.options;
        context.captureOptions = m_capture->exportOptions();

        // Initialize plugin
        if (!plugin->initialize(context)) {
            qCritical() << "Failed to initialize plugin:" << plugin->modeName();
            return false;
        }

        // Load QML
        QString qmlPath = plugin->qmlEntryPoint();
        if (qmlPath.isEmpty()) {
            qCritical() << "No QML entry point for plugin:" << plugin->modeName();
            return false;
        }

        QUrl qmlUrl(qmlPath);
        if (!qmlUrl.isValid() || qmlUrl.scheme().isEmpty()) {
            qmlUrl = QUrl::fromLocalFile(qmlPath);
        }

        m_engine->load(qmlUrl);

        if (m_engine->rootObjects().isEmpty()) {
            qCritical() << "Failed to load QML:" << qmlUrl.toString();

            // Try fallback
            QString fallback = QString("qrc:/qml/%1/Main.qml").arg(plugin->modeName());
            m_engine->load(QUrl(fallback));

            if (m_engine->rootObjects().isEmpty()) {
                qCritical() << "Fallback QML also failed";
                return false;
            }
        }

        // Handle any files
        if (!args.files.isEmpty()) {
            plugin->handleArguments(args.files);
        }

        // Store plugin for cleanup
        m_currentPlugin = plugin;

        return true;
    }

    void setupSystemTray() {
        m_trayIcon = new QSystemTrayIcon(QIcon(":/icons/aegis-tray.png"), &m_app);

        auto *trayMenu = new QMenu();

        // Add mode actions
        QAction *showAction = trayMenu->addAction("Show");
        QAction *screenRecorderAction = trayMenu->addAction("Screen Recorder");
        QAction *webcamAction = trayMenu->addAction("Webcam");
        QAction *dvbAction = trayMenu->addAction("DVB TV");
        trayMenu->addSeparator();
        QAction *quitAction = trayMenu->addAction("Quit");

        m_trayIcon->setContextMenu(trayMenu);

        // Connect actions
        connect(showAction, &QAction::triggered, [this]() {
            if (m_engine && !m_engine->rootObjects().isEmpty()) {
                auto *window = qobject_cast<QWindow*>(m_engine->rootObjects().first());
                if (window) {
                    window->show();
                    window->raise();
                }
            }
        });

        connect(screenRecorderAction, &QAction::triggered, [this]() {
            ApplicationManager::instance().sendToPrimary("open", {"mode:screenrecorder"});
        });

        connect(webcamAction, &QAction::triggered, [this]() {
            ApplicationManager::instance().sendToPrimary("open", {"mode:webcam"});
        });

        connect(dvbAction, &QAction::triggered, [this]() {
            ApplicationManager::instance().sendToPrimary("open", {"mode:dvb"});
        });

        connect(quitAction, &QAction::triggered, &m_app, &QApplication::quit);

        m_trayIcon->show();
    }

    void listCaptureSources() {
        Capture tempCapture;

        QTextStream out(stdout);
        out << "Available capture sources:\n\n";

        auto allSources = tempCapture.listSources(CaptureSourceType::Unknown);

        QMap<CaptureSourceType, QList<CaptureSourceInfo>> grouped;
        for (const auto &source : allSources) {
            grouped[source.type].append(source);
        }

        for (auto it = grouped.begin(); it != grouped.end(); ++it) {
            QString typeName;
            switch (it.key()) {
                case CaptureSourceType::Screen: typeName = "SCREEN"; break;
                case CaptureSourceType::Window: typeName = "WINDOW"; break;
                case CaptureSourceType::Webcam: typeName = "WEBCAM"; break;
                case CaptureSourceType::CaptureCard: typeName = "CAPTURE CARD"; break;
                case CaptureSourceType::DVB: typeName = "DVB"; break;
                case CaptureSourceType::IPCamera: typeName = "IP CAMERA"; break;
                case CaptureSourceType::AudioInput: typeName = "AUDIO INPUT"; break;
                case CaptureSourceType::AudioMonitor: typeName = "AUDIO MONITOR"; break;
                default: typeName = "OTHER"; break;
            }

            out << "\n[" << typeName << "]\n";
            for (const auto &source : it.value()) {
                out << "  " << source.id << "\n";
                out << "    Name: " << source.name << "\n";
                out << "    Backend: " << source.backend << "\n";

                if (!source.videoStreams.isEmpty()) {
                    out << "    Video: ";
                    QStringList resList;
                    for (const auto &video : source.videoStreams) {
                        for (const auto &res : video.resolutions) {
                            resList << QString("%1x%2").arg(res.width()).arg(res.height());
                        }
                    }
                    out << resList.join(", ") << "\n";
                }

                if (!source.audioStreams.isEmpty()) {
                    out << "    Audio: " << source.audioStreams.first().name << "\n";
                }

                out << "\n";
            }
        }
    }

    void handleFileOpenRequest(const QStringList &files, Aegis::AppMode mode) {
        if (!m_engine) return;

        // Find plugin for this mode
        QString modeKey = modeToString(mode);
        auto *plugin = Aegis::PluginRegistry::instance().get(modeKey);

        if (plugin) {
            plugin->handleArguments(files);
        }

        // Show window
        if (!m_engine->rootObjects().isEmpty()) {
            auto *window = qobject_cast<QWindow*>(m_engine->rootObjects().first());
            if (window) {
                window->show();
                window->raise();
            }
        }
    }

    void cleanup() {
        if (m_capture) {
            if (m_capture->recording()) {
                m_capture->stopRecording();
            }
            delete m_capture;
            m_capture = nullptr;
        }

        if (m_currentPlugin) {
            m_currentPlugin->shutdown();
        }
    }

    void showHelp() {
        QTextStream out(stdout);
        out << "Aegis Multimedia Suite v" << AEGIS_VERSION << "\n\n";
        out << "Usage: aegis [OPTIONS] [FILES...]\n\n";

        out << "Core Modes:\n";
        out << "  --mediaplayer         Media Player\n";
        out << "  --audioeditor         Audio/Waveform Editor\n";
        out << "  --videoeditor         Video Timeline Editor\n";
        out << "  --daw                 Digital Audio Workstation\n";
        out << "  --discburner          Disc Burner & Ripper\n";
        out << "  --djmixer             DJ Mixer\n";
        out << "  --karaoke             Karaoke Player\n";
        out << "  --notation            Music Notation Editor\n";
        out << "  --converter           Media Converter\n";
        out << "  --labelmaker          Disc Label Maker\n\n";

        out << "Capture Modes:\n";
        out << "  --screenrecorder      Screen/Window Recorder\n";
        out << "  --webcam              Webcam Recorder\n";
        out << "  --capturecard         HDMI/SDI Capture Card\n";
        out << "  --dvbtuner            DVB Television Tuner\n";
        out << "  --ipcamera            IP Camera Viewer\n";
        out << "  --audiorecorder       Audio-only Recorder\n";
        out << "  --streaming           Live Streaming Studio\n\n";

        out << "Capture Options:\n";
        out << "  -l, --list-sources    List available capture sources\n";
        out << "  --source=<id>         Select specific capture source\n";
        out << "  --duration=<sec>      Recording duration (0 = indefinite)\n";
        out << "  --output=<file>       Output file path\n\n";

        out << "General Options:\n";
        out << "  --mode=<name>         Start in specific mode\n";
        out << "  --single-instance     Use single instance (default)\n";
        out << "  --no-single-instance  Allow multiple instances\n";
        out << "  --minimized           Start minimized to tray\n";
        out << "  --no-tray             Disable system tray\n";
        out << "  --style=<name>        Qt style (fusion, windows, etc.)\n";
        out << "  --theme=<name>        UI theme (dark, light, system)\n";
        out << "  --geometry=WxH        Window size\n";
        out << "  -h, --help            Show this help\n";
        out << "  -v, --version         Show version\n";
    }

    void showVersion() {
        QTextStream out(stdout);
        out << "Aegis Multimedia Suite v" << AEGIS_VERSION << "\n";
        out << "Built with Qt " << QT_VERSION_STR << "\n";
        out << "GStreamer: " << gst_version_string() << "\n";
    }

    static QString modeToString(Aegis::AppMode mode) {
        switch (mode) {
            case Aegis::AppMode::MediaSuite:          return "mediasuite";
            case Aegis::AppMode::MediaPlayer:         return "mediaplayer";
            case Aegis::AppMode::AudioEditor:         return "audioeditor";
            case Aegis::AppMode::VideoEditor:         return "videoeditor";
            case Aegis::AppMode::DAW:                 return "daw";
            case Aegis::AppMode::DiscBurner:          return "discburner";
            case Aegis::AppMode::DJMixer:             return "djmixer";
            case Aegis::AppMode::KaraokePlayer:       return "karaoke";
            case Aegis::AppMode::MusicNotationEditor: return "notation";
            case Aegis::AppMode::Converter:           return "converter";
            case Aegis::AppMode::MiddlewareEditor:    return "middleware";
            case Aegis::AppMode::LabelMaker:          return "labelmaker";
            case Aegis::AppMode::ScreenRecorder:      return "screenrecorder";
            case Aegis::AppMode::WebcamRecorder:      return "webcamrecorder";
            case Aegis::AppMode::CaptureCardRecorder: return "capturecard";
            case Aegis::AppMode::DVBTuner:            return "dvbtuner";
            case Aegis::AppMode::IPCameraViewer:      return "ipcamera";
            case Aegis::AppMode::AudioRecorder:       return "audiorecorder";
            case Aegis::AppMode::StreamingStudio:     return "streaming";
            default:                                   return "unknown";
        }
    }

    QApplication m_app;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    Capture *m_capture = nullptr;
    Aegis::AppModePlugin *m_currentPlugin = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
};

// ============================================================
// main() Entry Point
// ============================================================
int main(int argc, char *argv[]) {
    // Set application attributes before QApplication creation
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    MainApplication app(argc, argv);
    return app.run();
}

#include "main.moc"
