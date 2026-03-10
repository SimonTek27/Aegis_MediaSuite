// main.cpp - Production-ready application entry point

#include <QApplication>
#include <clocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QIcon>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QLibraryInfo>
#include <QTranslator>
#include <QLockFile>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QDateTime>
#include <QFile>
#include <QMenu>
#include <QAction>
#include <QWindow>
#include <memory>

#include "core.h"
#include "plugin_interface.h"
#include "mpv_backend.h"
#include "config_manager.h"
#include "ipc_manager.h"
#include "raii_wrappers.h"
#include "audio.h"
#include "help.h"
#include "audioeditor.h"
#include "audio_djmix.h"
#include "discburner.h"
#include "disc_labelmaker.h"
#include "videoeditor.h"
#include "audio_karaoke.h"
#include "converter.h"
#include "audio_middleware.h"
#include "library.h"
#include "platform.h"

// ============================================================================
// Application Version
// ============================================================================
// AEGIS_VERSION is defined by CMake via add_compile_definitions().
// Do not redefine it here. The value comes from project(VERSION x.y.z).
#ifndef AEGIS_VERSION
#  define AEGIS_VERSION "2.1.1"  // fallback if built outside CMake
#endif

namespace Aegis {

    // ============================================================================
    // Minimal Logger utility
    // ============================================================================
    /**
     * @brief A simple, minimal logger for internal use.
     *
     * This class provides a basic logging interface, wrapping Qt's message
     * handlers. It prefixes each message with the logger's name for context.
     */
    class Logger {
        QString m_name;
    public:
        explicit Logger(const QString& name) : m_name(name) {}
        void info(const QString& msg) const    { qInfo()    << "[" << m_name << "]" << msg; }
        void debug(const QString& msg) const   { qDebug()   << "[" << m_name << "]" << msg; }
        void warning(const QString& msg) const { qWarning() << "[" << m_name << "]" << msg; }
        void error(const QString& msg) const   { qCritical()<< "[" << m_name << "]" << msg; }
    };

    // ============================================================================
    // AppContext is defined in plugin_interface.h
    // This struct holds the application's startup context and configuration.

    // ============================================================================
    // Command Line Parser
    // ============================================================================

    /**
     * @brief Parses and interprets command-line arguments.
     *
     * This class encapsulates the logic for parsing command-line arguments using
     * QCommandLineParser and populating an AppContext structure with the results.
     */
    class CommandLineParser {
    public:
        struct Result {
            AppContext context;
            bool helpRequested{false};
            bool versionRequested{false};
            bool listSources{false};
            bool success{true};
        };

        static Result parse(const QStringList& args) {
            Result result;
            QCommandLineParser parser;
            parser.setApplicationDescription("Aegis Multimedia Suite");
            parser.addHelpOption();
            parser.addVersionOption();

            // Mode selection
            QCommandLineOption modeOption("mode", "Start in specific mode", "mode");
            parser.addOption(modeOption);

            // Window options
            QCommandLineOption minimizedOption("minimized", "Start minimized to tray");
            parser.addOption(minimizedOption);

            QCommandLineOption noTrayOption("no-tray", "Disable system tray");
            parser.addOption(noTrayOption);

            QCommandLineOption styleOption("style", "Qt style (fusion, windows, etc.)", "style");
            parser.addOption(styleOption);

            QCommandLineOption themeOption("theme", "UI theme (dark, light, system)", "theme");
            parser.addOption(themeOption);

            QCommandLineOption geometryOption("geometry", "Window size WxH", "geometry");
            parser.addOption(geometryOption);

            // Capture options
            QCommandLineOption listSourcesOption("list-sources", "List available capture sources");
            parser.addOption(listSourcesOption);

            QCommandLineOption sourceOption("source", "Select capture source", "source");
            parser.addOption(sourceOption);

            QCommandLineOption durationOption("duration", "Recording duration in seconds", "duration");
            parser.addOption(durationOption);

            QCommandLineOption outputOption("output", "Output file path", "output");
            parser.addOption(outputOption);

            // Single instance
            QCommandLineOption singleInstanceOption("single-instance", "Use single instance (default)");
            parser.addOption(singleInstanceOption);

            QCommandLineOption noSingleInstanceOption("no-single-instance", "Allow multiple instances");
            parser.addOption(noSingleInstanceOption);

            parser.process(args);

            if (parser.isSet("help")) {
                result.helpRequested = true;
                return result;
            }

            if (parser.isSet("version")) {
                result.versionRequested = true;
                return result;
            }

            // Parse context
            if (parser.isSet("mode")) {
                result.context.mode = stringToMode(parser.value("mode"));
            }

            // Directly map parsed options to the AppContext structure.
            result.context.startMinimized = parser.isSet("minimized");
            result.context.enableTray = !parser.isSet("no-tray");

            if (parser.isSet("style")) {
                result.context.style = parser.value("style");
            }

            if (parser.isSet("theme")) {
                result.context.theme = parser.value("theme");
            }

            if (parser.isSet("geometry")) {
                QString geom = parser.value("geometry");
                QStringList parts = geom.split('x');
                if (parts.size() == 2) {
                    result.context.windowSize = QSize(parts[0].toInt(), parts[1].toInt());
                }
            }

            result.listSources = parser.isSet("list-sources");

            if (parser.isSet("source")) {
                result.context.options["source"] = parser.value("source");
            }

            if (parser.isSet("duration")) {
                result.context.options["duration"] = parser.value("duration").toInt();
            }

            if (parser.isSet("output")) {
                result.context.options["output"] = parser.value("output");
            }

            // Collect positional arguments (files)
            const QStringList positional = parser.positionalArguments();
            for (const QString& arg : positional) {
                QFileInfo info(arg);
                if (info.exists() || QUrl(arg).isValid()) {
                    result.context.files.append(arg);
                }
            }

            return result;
        }

    private:
        static AppMode stringToMode(const QString& modeStr) {
            // Map from string identifiers (e.g., "player", "editor") to the AppMode enum.
            static const QHash<QString, AppMode> modeMap = {
                {"launcher",       AppMode::Launcher},
                {"mediaplayer",    AppMode::MediaPlayer},
                {"audioeditor",    AppMode::AudioEditor},
                {"videoeditor",    AppMode::VideoEditor},
                {"daw",            AppMode::DAW},
                {"modtracker",     AppMode::ModTracker},
                {"musicnotation",  AppMode::MusicNotation},
                {"middleware",     AppMode::Middleware},
                {"djmixer",        AppMode::DJMixer},
                {"karaoke",        AppMode::Karaoke},
                {"disctools",      AppMode::DiscTools},
                {"labelmaker",     AppMode::LabelMaker},
                {"converter",      AppMode::Converter},
                {"streaming",      AppMode::Streaming},
                {"capture",        AppMode::Capture},
                {"library",        AppMode::Library},
                // Legacy / compat aliases
                {"discburner",     AppMode::DiscTools},
                {"mediasuite",     AppMode::MediaSuite},
                {"screenrecorder", AppMode::ScreenRecorder},
                {"webcam",         AppMode::WebcamRecorder},
                {"dvbtuner",       AppMode::DVBTuner},
                {"ipcamera",       AppMode::IPCameraViewer},
                {"audiorecorder",  AppMode::AudioRecorder},
                {"streamingstudio",AppMode::StreamingStudio},
            };
            return modeMap.value(modeStr.toLower(), AppMode::MediaSuite);
        }
    };

    // ============================================================================
    // Application Initializer
    // ============================================================================

    /**
     * @brief Performs global application setup.
     *
     * This static class handles all one-time initialization tasks that are
     * independent of the specific mode being launched.
     */
    class ApplicationInitializer {
    public:
        static void setup(QApplication& app) {
            app.setApplicationName("Aegis");
            app.setOrganizationName("Aegis");
            app.setOrganizationDomain("org.aegis");
            app.setApplicationVersion(AEGIS_VERSION);

            // Set application icon
            app.setWindowIcon(QIcon(":/assets/icons/app_icon.svg"));

            // High DPI: auto-enabled in Qt6, no setAttribute needed.

            // Default style
            QStyle* style = QStyleFactory::create("Fusion");
            if (style) app.setStyle(style);

            // OpenGL format
            QSurfaceFormat format;
            format.setRenderableType(QSurfaceFormat::OpenGL);
            format.setProfile(QSurfaceFormat::CoreProfile);
            format.setVersion(3, 3);
            format.setSamples(4);
            QSurfaceFormat::setDefaultFormat(format);

            // Load configuration
            auto& config = ConfigManager::instance();
            config.load();

            // Setup logging
            setupLogging();

            // Create directories
            createDirectories();

            // ── Internationalisation ──────────────────────────────────────────
            // Load the translation that matches either the saved language setting
            // or the system locale.  Falls back silently to English if no .qm
            // file is found.
            auto& i18n = I18nManager::instance();
            i18n.setTranslationsPath(QStringLiteral(":/translations"));
            const QString savedLang =
                ConfigManager::instance().get<QString>(
                    "ui/language",
                    QLocale::system().name().section(QLatin1Char('_'), 0, 0));
            if (!savedLang.isEmpty() && savedLang != QLatin1String("en"))
                i18n.switchLanguage(savedLang);
        }

    private:
        static void setupLogging() {
            // Install a custom message handler to log to both console and a file.
            QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/aegis.log";

            qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context, const QString& msg) {
                QByteArray localMsg = msg.toLocal8Bit();
                QString level;

                switch (type) {
                    case QtDebugMsg: level = "DEBUG"; break;
                    case QtInfoMsg: level = "INFO"; break;
                    case QtWarningMsg: level = "WARNING"; break;
                    case QtCriticalMsg: level = "CRITICAL"; break;
                    case QtFatalMsg: level = "FATAL"; abort();
                }

                // Console output
                fprintf(stderr, "%s: %s (%s:%u)\n",
                        level.toLocal8Bit().constData(),
                        localMsg.constData(),
                        context.file ? context.file : "unknown",
                        context.line);

                // File output
                static QFile logFile(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/aegis.log");

                // Open the file only once, when the first log message arrives.
                if (!logFile.isOpen()) {
                    // Attempt to open in Append mode, creating the file if it doesn't exist.
                    if (!logFile.open(QIODevice::Append | QIODevice::Text)) {
                        // If opening fails, print an error to stderr. This is a fallback.
                        fprintf(stderr, "Failed to open log file: %s\n", logFile.errorString().toLocal8Bit().constData());
                        return; // Skip file logging for this message.
                    }
                }

                if (logFile.isOpen()) {
                    QTextStream stream(&logFile);
                    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")
                    << " [" << level << "] " << msg << "\n";
                    stream.flush();
                }
            });
        }

        static void createDirectories() {
            // Ensure required application data directories exist.
            QStringList dirs = {
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/captures",
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/screenshots",
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/dvb",
                QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/Aegis/Rips"
            };

            for (const QString& dir : dirs) {
                QDir().mkpath(dir);
            }
        }
    };

    // ============================================================================
    // Main Application Class
    // ============================================================================

    /**
     * @brief Core application class that orchestrates the entire lifecycle.
     *
     * This class creates the QApplication, parses arguments, initializes
     * backends, sets up the QML engine, and runs the main event loop.
     */

    // ============================================================================
    // HelpBridge — exposes AegisHelpMenu functionality to QML
    // ============================================================================
    /**
     * @brief Thin QObject that lets QML invoke C++ help/about dialogs.
     *
     * Registered as a context property "helpBridge" on the QML engine.
     */
    class HelpBridge : public QObject {
        Q_OBJECT
    public:
        explicit HelpBridge(QWidget* parent = nullptr)
            : QObject(nullptr)
            , m_menu(parent, AboutData::defaultData())
        {}

    public slots:
        void showHelpContents()  { m_menu.showHelpContents(); }
        void showAboutDialog()   { m_menu.showAboutApplication(); }
        void showAboutQt()       { m_menu.showAboutQt(); }
        void reportBug()         { m_menu.reportBug(); }
        void switchLanguage()    { m_menu.switchLanguage(); }

    private:
        AegisHelpMenu m_menu;
    };

    class AegisApplication : public QObject {
        Q_OBJECT

    public:
        AegisApplication(int& argc, char** argv)
        : m_app(argc, argv)
        , m_logger("Application") {
            // Qt resets LC_NUMERIC to the system locale inside QApplication's
            // constructor.  MPV requires LC_NUMERIC=C to parse floating-point
            // values in media headers; without it mpv_create() fails immediately.
            // This must be called after QApplication is constructed, not before.
            std::setlocale(LC_NUMERIC, "C");
            m_helpBridge = std::make_unique<HelpBridge>(nullptr);
        }

        int run() {
            // Parse command line
            auto parseResult = CommandLineParser::parse(m_app.arguments());

            if (parseResult.helpRequested) {
                showHelp();
                return 0;
            }

            if (parseResult.versionRequested) {
                showVersion();
                return 0;
            }

            // Initialize application
            ApplicationInitializer::setup(m_app);

            // Handle single instance
            if (!handleSingleInstance(parseResult.context)) {
                return 0;
            }

            // Create core with dependency injection
            if (!initializeCore()) {
                return 1;
            }

            // Initialize QML engine
            if (!initializeQml(parseResult.context)) {
                return 1;
            }

            // Setup system tray
            if (parseResult.context.enableTray) {
                setupTray();
            }

            // Start application
            m_logger.info("Aegis started successfully");
            return m_app.exec();
        }

    private:
        bool handleSingleInstance(const AppContext& context) {
            auto& ipc = IpcManager::instance();

            // If this is not the primary instance and there are files to open,
            // send them to the primary instance and exit.
            if (!ipc.isPrimary() && !context.files.isEmpty()) {
                IpcFileOpenMessage msg;
                msg.files = context.files;
                msg.suggestedMode = modeToString(context.mode);

                if (ipc.sendToPrimary(msg)) {
                    m_logger.info("Files forwarded to primary instance");
                    return false;
                }
            }

            // Connect IPC signals to handle file open requests from other instances.
            connect(&ipc, &IpcManager::fileOpenRequested,
                    this, &AegisApplication::handleFileOpenRequest);

            return true;
        }

        bool initializeCore() {
            try {
                // Create MPV backend
                auto mpvPtr  = std::make_unique<MpvBackend>();
                auto vidPtr  = std::make_unique<MpvBackend>();
                auto audioEngine = std::make_unique<Aegis::AudioEngine>();

                // Adapter: MpvBackend implements IAudioBackend but NOT IVideoBackend.
                // Wrap it as IVideoBackend for the video slot.
                /**
                 * @brief Adapter class to make MpvBackend conform to the IVideoBackend interface.
                 */
                class VideoBackendFromMpv final : public IVideoBackend {
                    std::unique_ptr<MpvBackend> m;
                public:
                    explicit VideoBackendFromMpv(std::unique_ptr<MpvBackend> b) : m(std::move(b)) {}
                    Result<void> load(const QUrl& u) override { return m->open(u) ? Result<void>::success() : Result<void>::error("MpvBackend::open failed"); }
                    Result<void> play()  override { m->play();  return Result<void>::success(); }
                    Result<void> pause() override { m->pause(); return Result<void>::success(); }
                    Result<void> stop()  override { m->stop();  return Result<void>::success(); }
                    Result<void> seek(double p) override { m->seek(static_cast<qint64>(p * 1000.0)); return Result<void>::success(); }
                    QImage currentFrame() const override { return {}; }
                    QSize  videoSize()    const override { return {}; }
                    bool   hasVideo()     const override { return m->hasVideo(); }
                };

                // Adapter: AudioEngine does not inherit IAudioEngine; wrap it.
                /**
                 * @brief Adapter class to make AudioEngine conform to the IAudioEngine interface.
                 */
                class AudioEngineAdapter final : public IAudioEngine {
                    Aegis::AudioEngine* m;
                public:
                    explicit AudioEngineAdapter(Aegis::AudioEngine* e) : m(e) {}
                    Result<void> processBuffer(float* buf, int frames, int sr, int ch) override {
                        m->processBuffer(buf, frames, sr, ch);
                        return Result<void>::success();
                    }
                    Result<std::vector<float>> analyzeSpectrum(int /*size*/) override {
                        return Result<std::vector<float>>(std::vector<float>{});
                    }
                    double rmsLevel()  const override { return m->momentaryLoudness(); }
                    double peakLevel() const override { return m->momentaryLoudness(); }
                    void   setBpm(double)  override {}
                    double bpm()     const override { return 0.0; }
                };

                Aegis::AudioEngine* rawEngine = audioEngine.get();
                m_audioEngine = rawEngine;  // cache for use in initializeQml
                std::unique_ptr<IAudioBackend> audioBackend(std::move(mpvPtr));
                std::unique_ptr<IVideoBackend> videoBackend =
                std::make_unique<VideoBackendFromMpv>(std::move(vidPtr));
                std::unique_ptr<IAudioEngine> audioEngineIface =
                std::make_unique<AudioEngineAdapter>(rawEngine);
                audioEngine.release(); // ownership transferred to QObject parent tree

                m_core = std::make_unique<Core>(
                    std::move(audioBackend),
                                                std::move(videoBackend),
                                                std::move(audioEngineIface)
                );
                rawEngine->setParent(m_core.get());

                // Restore volume from config
                double volume = ConfigManager::instance().get<double>(Config::AudioVolume, 80.0);
                m_core->setVolume(volume);

                connect(m_core.get(), &Core::error, this, [this](const QString& msg) {
                    m_logger.error("Core error: " + msg);
                });

                return true;

            } catch (const std::exception& e) {
                m_logger.error("Core initialization failed: " + QString::fromStdString(e.what()));
                return false;
            }
        }

        bool initializeQml(const AppContext& context) {
            m_engine = std::make_unique<QQmlApplicationEngine>();

            // Register C++ types with QML
            qmlRegisterType<Core>("Aegis.Core", 1, 0, "Core");
            qmlRegisterUncreatableType<PlaybackState>("Aegis.Core", 1, 0, "PlaybackState",
                                                      "Enum type");

            // Set context properties, making C++ objects and data available to QML.
            QQmlContext* ctx = m_engine->rootContext();
            ctx->setContextProperty("aegisVersion", AEGIS_VERSION);
            ctx->setContextProperty("core", m_core.get());
            ctx->setContextProperty("config", &ConfigManager::instance());
            ctx->setContextProperty("commandLineFiles", QVariant::fromValue(context.files));
            ctx->setContextProperty("commandLineOptions", context.options);
            // Expose i18n manager to QML for language switching from UI
            ctx->setContextProperty("i18nManager", &I18nManager::instance());
            // Expose help bridge (About/Help dialogs) to QML
            ctx->setContextProperty("helpBridge", m_helpBridge.get());

            // ── Per-mode backend objects ────────────────────────────────────
            // Create all backend objects that are needed. Some are created for
            // all modes (AudioEditor, Library, etc.) so that main.qml's startup
            // health-checks succeed regardless of which mode launched.

            // Shared backends (needed by main.qml's typeof checks always)
            m_library    = std::make_unique<Aegis::Library>(
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                    + "/library.db");
            m_platform   = std::make_unique<Platform>();
            m_cdBurner   = std::make_unique<Aegis::CDBurner>();
            m_videoEditor= std::make_unique<Aegis::VideoEditor>();
            m_converter  = m_audioEngine
                ? std::make_unique<Aegis::Converter>(m_audioEngine)
                : nullptr;
            m_middleware = m_audioEngine
                ? std::make_unique<Aegis::AudioMiddleware>(m_audioEngine)
                : nullptr;

            // AudioEditor — registered as both "AudioEditor" (for main.qml) and
            // "AudioEngine" (for ui_audioeditor.qml which uses that name)
            m_audioEditor = std::make_unique<Aegis::AudioEditor>(m_audioEngine);
            ctx->setContextProperty("AudioEditor", m_audioEditor.get());
            ctx->setContextProperty("AudioEngine", m_audioEditor.get());

            // DJ Mixer
            m_djMixer = std::make_unique<Aegis::DJMixer>();
            ctx->setContextProperty("DJ", m_djMixer.get());

            // Disc / Burning / Label
            m_labelMaker = std::make_unique<Aegis::DiscLabelMaker>();
            ctx->setContextProperty("CDBurner",   m_cdBurner.get());
            ctx->setContextProperty("Disc",        m_cdBurner.get());
            ctx->setContextProperty("LabelMaker",  m_labelMaker.get());

            // Video editor
            ctx->setContextProperty("VideoEditor", m_videoEditor.get());

            // Library / Platform
            ctx->setContextProperty("Library",     m_library.get());
            ctx->setContextProperty("Platform",    m_platform.get());

            // Converter / Middleware
            if (m_converter)  ctx->setContextProperty("Converter",       m_converter.get());
            if (m_middleware) ctx->setContextProperty("AudioMiddleware",  m_middleware.get());

            // Karaoke — needs AudioEngine + an MpvBackend; create a dedicated one
            if (m_audioEngine) {
                auto karaokeBackend = std::make_unique<MpvBackend>();
                m_karaoke = std::make_unique<Aegis::KaraokeController>(
                    m_audioEngine, karaokeBackend.release());
                ctx->setContextProperty("Karaoke", m_karaoke.get());
            }

            // Audio raw engine (some QML checks typeof Audio)
            if (m_audioEngine)
                ctx->setContextProperty("Audio", m_audioEngine);

            // Load appropriate QML based on the selected mode.
            QString qmlPath = getQmlPath(context.mode);
            QUrl qmlUrl(qmlPath);

            if (!qmlUrl.isValid() || qmlUrl.scheme().isEmpty()) {
                qmlUrl = QUrl("qrc:/qml/main.qml");
            }

            m_engine->load(qmlUrl);

            if (m_engine->rootObjects().isEmpty()) {
                m_logger.error("Failed to load QML: " + qmlUrl.toString());
                return false;
            }

            // Handle initial files passed on the command line.
            if (!context.files.isEmpty()) {
                for (const QString& file : context.files) {
                    m_core->enqueue(QUrl::fromLocalFile(file));
                }

                if (ConfigManager::instance().get<bool>(Config::PlaybackAutoPlay, true)) {
                    QTimer::singleShot(100, m_core.get(), &Core::play);
                }
            }

            return true;
        }

        QString getQmlPath(AppMode mode) const {
            // Map each application mode to its corresponding QML resource file.
            static const QHash<AppMode, QString> qmlMap = {
                {AppMode::Launcher,        "qrc:/qml/ui_launcher.qml"},
                {AppMode::MediaSuite,      "qrc:/qml/main.qml"},
                {AppMode::MediaPlayer,     "qrc:/qml/ui_player.qml"},
                {AppMode::AudioEditor,     "qrc:/qml/ui_audioeditor.qml"},
                {AppMode::VideoEditor,     "qrc:/qml/ui_videoeditor.qml"},
                {AppMode::DAW,             "qrc:/qml/ui_daw.qml"},
                {AppMode::DJMixer,         "qrc:/qml/ui_djmixer.qml"},
                {AppMode::Karaoke,         "qrc:/qml/ui_karaoke.qml"},
                {AppMode::ModTracker,      "qrc:/qml/ui_modtracker.qml"},
                {AppMode::MusicNotation,   "qrc:/qml/ui_musicnotation_editor.qml"},
                {AppMode::Middleware,      "qrc:/qml/ui_middleware.qml"},
                {AppMode::DiscTools,       "qrc:/qml/ui_discburner.qml"},
                {AppMode::DiscBurner,      "qrc:/qml/ui_discburner.qml"},
                {AppMode::LabelMaker,      "qrc:/qml/ui_disc_labelmaker.qml"},
                {AppMode::Converter,       "qrc:/qml/ui_converter.qml"},
                {AppMode::Capture,         "qrc:/qml/ui_screencapture.qml"},
                {AppMode::Library,         "qrc:/qml/ui_launcher.qml"},
                {AppMode::Streaming,       "qrc:/qml/ui_player.qml"},
                // Legacy capture sub-modes
                {AppMode::ScreenRecorder,  "qrc:/qml/ui_screencapture.qml"},
                {AppMode::WebcamRecorder,  "qrc:/qml/ui_screencapture.qml"},
                {AppMode::DVBTuner,        "qrc:/qml/ui_screencapture.qml"},
                {AppMode::IPCameraViewer,  "qrc:/qml/ui_screencapture.qml"},
                {AppMode::AudioRecorder,   "qrc:/qml/ui_screencapture.qml"},
                {AppMode::StreamingStudio, "qrc:/qml/ui_player.qml"},
            };
            return qmlMap.value(mode, "qrc:/qml/main.qml");
        }

        void setupTray() {
            // Create the system tray icon and menu.
            QIcon trayIcon = QIcon(":/assets/icons/app_icon.svg");
            if (trayIcon.isNull()) trayIcon = m_app.windowIcon();
            m_tray = std::make_unique<QSystemTrayIcon>(trayIcon, &m_app);

            QMenu* menu = new QMenu();
            QAction* showAction = menu->addAction(tr("Show"));
            QAction* playPauseAction = menu->addAction(tr("Play/Pause"));
            QAction* nextAction = menu->addAction(tr("Next"));
            QAction* quitAction = menu->addAction(tr("Quit"));

            connect(showAction, &QAction::triggered, this, [this]() {
                if (!m_engine->rootObjects().isEmpty()) {
                    auto* window = qobject_cast<QWindow*>(m_engine->rootObjects().first());
                    if (window) {
                        window->show();
                        window->raise();
                    }
                }
            });

            connect(playPauseAction, &QAction::triggered, m_core.get(), &Core::playPause);
            connect(nextAction, &QAction::triggered, m_core.get(), &Core::next);
            connect(quitAction, &QAction::triggered, &m_app, &QApplication::quit);

            m_tray->setContextMenu(menu);
            m_tray->show();
        }

        void handleFileOpenRequest(const QStringList& files, const QString& mode) {
            m_logger.info("Received files from another instance");

            for (const QString& file : files) {
                m_core->enqueue(QUrl::fromLocalFile(file));
            }

            // Show window
            if (!m_engine->rootObjects().isEmpty()) {
                auto* window = qobject_cast<QWindow*>(m_engine->rootObjects().first());
                if (window) {
                    window->show();
                    window->raise();
                }
            }

            if (ConfigManager::instance().get<bool>(Config::PlaybackAutoPlay, true)) {
                m_core->play();
            }
        }

        void showHelp() {
            // CLI help (--help flag): print to stdout.
            QTextStream out(stdout);
            out << "Aegis Multimedia Suite v" << AEGIS_VERSION << "\n\n";
            out << "Usage: aegis [OPTIONS] [FILES...]\n\n";
            out << "Options:\n";
            out << "  --mode=<n>            Start in specific mode\n";
            out << "                        (launcher|mediaplayer|audioeditor|\n";
            out << "                         videoeditor|daw|djmixer|karaoke|\n";
            out << "                         modtracker|musicnotation|middleware|\n";
            out << "                         disctools|labelmaker|converter|\n";
            out << "                         capture|streaming)\n";
            out << "  --minimized           Start minimized to tray\n";
            out << "  --no-tray             Disable system tray\n";
            out << "  --style=<n>           Qt style (fusion, windows, etc.)\n";
            out << "  --theme=<n>           UI theme (dark, light, system)\n";
            out << "  --geometry=WxH        Window size\n";
            out << "  --list-sources        List capture sources\n";
            out << "  --source=<id>         Select capture source\n";
            out << "  --duration=<sec>      Recording duration\n";
            out << "  --output=<file>       Output file path\n";
            out << "  -h, --help            Show this help\n";
            out << "  -v, --version         Show version\n";
            out << "\nKeyboard shortcuts (in-app):\n";
            out << "  F1                    Open user handbook\n";
            out << "  Shift+F1              WhatsThis mode\n";
            out << "  Ctrl+L                Return to launcher\n";
            out << "  Ctrl+1..9             Switch application module\n";
        }

        /// Show the GUI About dialog (called from tray or QML via help menu).
        void showAboutDialog() {
            auto aboutData = AboutData::defaultData();
            AegisAboutDialog dlg(aboutData);
            dlg.exec();
        }

        void showVersion() {
            QTextStream out(stdout);
            out << "Aegis Multimedia Suite v" << AEGIS_VERSION << "\n";
            out << "Built with Qt " << QT_VERSION_STR << "\n";
        }

        static QString modeToString(AppMode mode) {
            // Inverse mapping of stringToMode, converting an AppMode enum to a string.
            switch (mode) {
                case AppMode::Launcher:        return "launcher";
                case AppMode::MediaSuite:      return "mediasuite";
                case AppMode::MediaPlayer:     return "mediaplayer";
                case AppMode::AudioEditor:     return "audioeditor";
                case AppMode::VideoEditor:     return "videoeditor";
                case AppMode::DAW:             return "daw";
                case AppMode::DJMixer:         return "djmixer";
                case AppMode::Karaoke:         return "karaoke";
                case AppMode::ModTracker:      return "modtracker";
                case AppMode::MusicNotation:   return "musicnotation";
                case AppMode::Middleware:      return "middleware";
                case AppMode::DiscTools:       return "disctools";
                case AppMode::DiscBurner:      return "disctools";
                case AppMode::LabelMaker:      return "labelmaker";
                case AppMode::Converter:       return "converter";
                case AppMode::Capture:         return "capture";
                case AppMode::Library:         return "library";
                case AppMode::Streaming:       return "streaming";
                case AppMode::ScreenRecorder:  return "screenrecorder";
                case AppMode::WebcamRecorder:  return "webcam";
                case AppMode::DVBTuner:        return "dvbtuner";
                case AppMode::IPCameraViewer:  return "ipcamera";
                case AppMode::AudioRecorder:   return "audiorecorder";
                case AppMode::StreamingStudio: return "streaming";
                default:                       return "launcher";
            }
        }

        QApplication m_app;
        std::unique_ptr<QQmlApplicationEngine> m_engine;
        std::unique_ptr<Core> m_core;
        std::unique_ptr<QSystemTrayIcon> m_tray;
        std::unique_ptr<HelpBridge> m_helpBridge;
        Aegis::AudioEngine* m_audioEngine = nullptr;  // owned by m_core, pointer cached here

        // Per-mode backend objects (created in initializeQml, owned here)
        std::unique_ptr<Aegis::AudioEditor>       m_audioEditor;
        std::unique_ptr<Aegis::DJMixer>           m_djMixer;
        std::unique_ptr<Aegis::CDBurner>          m_cdBurner;
        std::unique_ptr<Aegis::DiscLabelMaker>    m_labelMaker;
        std::unique_ptr<Aegis::VideoEditor>       m_videoEditor;
        std::unique_ptr<Aegis::KaraokeController> m_karaoke;
        std::unique_ptr<Aegis::Converter>         m_converter;
        std::unique_ptr<Aegis::AudioMiddleware>   m_middleware;
        std::unique_ptr<Aegis::Library>           m_library;
        std::unique_ptr<Platform>                 m_platform;  // Platform is global namespace
        Logger m_logger;
    };

} // namespace Aegis


// ============================================================================
// Main Entry Point
// ============================================================================
//
// A single binary serves every mode.  Mode resolution order:
//   1. --mode=<n> on the command line  (always wins)
//   2. argv[0] basename with the "aegis-" prefix stripped, e.g.:
//        aegis-mediaplayer  ->  mediaplayer
//        aegis-daw          ->  daw
//        aegis              ->  launcher  (fallback)
//   3. Default: launcher
//
// Install symlinks to expose per-mode entry points without extra binaries:
//   ln -s aegis /usr/bin/aegis-mediaplayer
//   ln -s aegis /usr/bin/aegis-audioeditor   … etc.
// ============================================================================

int main(int argc, char* argv[]) {
    // ── 1. Check for an explicit --mode flag ──────────────────────────────
    bool hasExplicitMode = false;
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]).startsWith("--mode")) {
            hasExplicitMode = true;
            break;
        }
    }

    // ── 2. Derive mode from argv[0] basename ─────────────────────────────
    std::vector<std::string> patchedStorage;
    std::vector<char*>       patchedPtrs;

    if (!hasExplicitMode) {
        QString bin = QFileInfo(QString::fromLocal8Bit(argv[0])).fileName();
        if (bin.startsWith(QLatin1String("aegis-")))
            bin = bin.mid(6);

        static const QHash<QString, QString> basenameMap = {
            {"aegis",         "mediasuite"},
            {"launcher",      "launcher"},
            {"mediaplayer",   "mediaplayer"},
            {"audioeditor",   "audioeditor"},
            {"videoeditor",   "videoeditor"},
            {"daw",           "daw"},
            {"disctools",     "disctools"},
            {"djmixer",       "djmixer"},
            {"karaoke",       "karaoke"},
            {"modtracker",    "modtracker"},
            {"musicnotation", "musicnotation"},
            {"middleware",    "middleware"},
            {"labelmaker",    "labelmaker"},
            {"converter",     "converter"},
            {"capture",       "capture"},
            {"library",       "library"},
            {"streaming",     "streaming"},
        };

        QString modeName = basenameMap.value(bin, "launcher");

        // Patch the argument list by inserting a --mode option before the original arguments.
        patchedStorage.push_back(std::string(argv[0]));
        patchedStorage.push_back(("--mode=" + modeName).toStdString());
        for (int i = 1; i < argc; ++i)
            patchedStorage.push_back(std::string(argv[i]));
        for (auto& s : patchedStorage)
            patchedPtrs.push_back(s.data());

        int    pargc = static_cast<int>(patchedPtrs.size());
        char** pargv = patchedPtrs.data();

        try {
            Aegis::AegisApplication app(pargc, pargv);
            return app.run();
        } catch (const std::exception& e) {
            qCritical() << "Fatal error:" << e.what();
            return 1;
        }
    }

    // ── 3. --mode already present — pass argv through unchanged ──────────
    try {
        Aegis::AegisApplication app(argc, argv);
        return app.run();
    } catch (const std::exception& e) {
        qCritical() << "Fatal error:" << e.what();
        return 1;
    }
}

#include "main.moc"
