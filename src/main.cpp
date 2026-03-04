// main.cpp - Production-ready application entry point
#include <QApplication>
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
#include <memory>

#include "core_refactored.h"
#include "mpv_backend_impl.h"
#include "config_manager.h"
#include "ipc_manager.h"
#include "raii_wrappers.h"

// ============================================================================
// Application Version
// ============================================================================
#define AEGIS_VERSION "1.0.0"

namespace Aegis {

    // ============================================================================
    // Application Context
    // ============================================================================

    struct AppContext {
        QStringList files;
        QVariantMap options;
        AppMode mode{AppMode::MediaSuite};
        bool startMinimized{false};
        bool enableTray{true};
        QString style{"fusion"};
        QString theme{"dark"};
        QSize windowSize{1280, 720};
        QPoint windowPosition;
    };

    // ============================================================================
    // Command Line Parser
    // ============================================================================

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
            static const QHash<QString, AppMode> modeMap = {
                {"mediasuite", AppMode::MediaSuite},
                {"mediaplayer", AppMode::MediaPlayer},
                {"audioeditor", AppMode::AudioEditor},
                {"videoeditor", AppMode::VideoEditor},
                {"daw", AppMode::DAW},
                {"discburner", AppMode::DiscBurner},
                {"screenrecorder", AppMode::ScreenRecorder},
                {"webcam", AppMode::WebcamRecorder},
                {"dvbtuner", AppMode::DVBTuner},
                {"ipcamera", AppMode::IPCameraViewer},
                {"audiorecorder", AppMode::AudioRecorder},
                {"streaming", AppMode::StreamingStudio}
            };

            return modeMap.value(modeStr.toLower(), AppMode::MediaSuite);
        }
    };

    // ============================================================================
    // Application Initializer
    // ============================================================================

    class ApplicationInitializer {
    public:
        static void setup(QApplication& app) {
            app.setApplicationName("Aegis");
            app.setOrganizationName("Aegis");
            app.setOrganizationDomain("org.aegis");
            app.setApplicationVersion(AEGIS_VERSION);

            // Set application icon
            app.setWindowIcon(QIcon(":/icons/aegis.png"));

            // High DPI support
            app.setAttribute(Qt::AA_EnableHighDpiScaling);
            app.setAttribute(Qt::AA_UseHighDpiPixmaps);

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
        }

    private:
        static void setupLogging() {
            QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + "/aegis.log";

                qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context,
                                          const QString& msg) {
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
                    static QFile logFile(QStandardPaths::writableLocation(
                        QStandardPaths::AppLocalDataLocation) + "/aegis.log");

                    if (!logFile.isOpen()) {
                        logFile.open(QIODevice::Append | QIODevice::Text);
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

    class AegisApplication : public QObject {
        Q_OBJECT

    public:
        AegisApplication(int& argc, char** argv)
        : m_app(argc, argv)
        , m_logger("Application") {
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

            if (!ipc.isPrimary() && !context.files.isEmpty()) {
                // Send files to primary instance
                IpcFileOpenMessage msg;
                msg.files = context.files;
                msg.suggestedMode = modeToString(context.mode);

                if (ipc.sendToPrimary(msg)) {
                    m_logger.info("Files forwarded to primary instance");
                    return false;
                }
            }

            // Connect IPC signals
            connect(&ipc, &IpcManager::fileOpenRequested,
                    this, &AegisApplication::handleFileOpenRequest);

            return true;
        }

        bool initializeCore() {
            try {
                // Create MPV backend
                auto mpvResult = BackendFactory<MpvBackend>::create();
                if (mpvResult.isError()) {
                    m_logger.error("Failed to create MPV backend: " + mpvResult.error());
                    return false;
                }

                // For now, use same backend for video (simplified)
                auto videoResult = BackendFactory<MpvBackend>::create();
                if (videoResult.isError()) {
                    m_logger.error("Failed to create video backend: " + videoResult.error());
                    return false;
                }

                // Create audio engine
                auto audioEngine = std::make_unique<AudioEngine>();

                // Create core with dependencies
                m_core = std::make_unique<Core>(
                    std::move(mpvResult.value()),
                                                std::move(videoResult.value()),
                                                std::move(audioEngine)
                );

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

            // Register types
            qmlRegisterType<Core>("Aegis.Core", 1, 0, "Core");
            qmlRegisterUncreatableType<PlaybackState>("Aegis.Core", 1, 0, "PlaybackState",
                                                      "Enum type");

            // Set context properties
            QQmlContext* ctx = m_engine->rootContext();
            ctx->setContextProperty("aegisVersion", AEGIS_VERSION);
            ctx->setContextProperty("core", m_core.get());
            ctx->setContextProperty("config", &ConfigManager::instance());
            ctx->setContextProperty("commandLineFiles", QVariant::fromValue(context.files));
            ctx->setContextProperty("commandLineOptions", context.options);

            // Load appropriate QML
            QString qmlPath = getQmlPath(context.mode);
            QUrl qmlUrl = QUrl::fromLocalFile(qmlPath);

            if (!qmlUrl.isValid() || qmlUrl.scheme().isEmpty()) {
                qmlUrl = QUrl("qrc:/qml/MediaSuite/Main.qml");
            }

            m_engine->load(qmlUrl);

            if (m_engine->rootObjects().isEmpty()) {
                m_logger.error("Failed to load QML: " + qmlUrl.toString());
                return false;
            }

            // Handle initial files
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
            static const QHash<AppMode, QString> qmlMap = {
                {AppMode::MediaSuite, "qrc:/qml/MediaSuite/Main.qml"},
                {AppMode::MediaPlayer, "qrc:/qml/MediaPlayer/Main.qml"},
                {AppMode::AudioEditor, "qrc:/qml/AudioEditor/Main.qml"},
                {AppMode::VideoEditor, "qrc:/qml/VideoEditor/Main.qml"},
                {AppMode::DAW, "qrc:/qml/DAW/Main.qml"},
                {AppMode::DiscBurner, "qrc:/qml/DiscBurner/Main.qml"},
                {AppMode::ScreenRecorder, "qrc:/qml/Capture/ScreenRecorder.qml"},
                {AppMode::WebcamRecorder, "qrc:/qml/Capture/WebcamRecorder.qml"},
                {AppMode::DVBTuner, "qrc:/qml/Capture/DVBTuner.qml"},
                {AppMode::IPCameraViewer, "qrc:/qml/Capture/IPCamera.qml"},
                {AppMode::AudioRecorder, "qrc:/qml/Capture/AudioRecorder.qml"},
                {AppMode::StreamingStudio, "qrc:/qml/Capture/StreamingStudio.qml"}
            };

            return qmlMap.value(mode, "qrc:/qml/MediaSuite/Main.qml");
        }

        void setupTray() {
            m_tray = std::make_unique<QSystemTrayIcon>(QIcon(":/icons/aegis-tray.png"), &m_app);

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
            QTextStream out(stdout);
            out << "Aegis Multimedia Suite v" << AEGIS_VERSION << "\n\n";
            out << "Usage: aegis [OPTIONS] [FILES...]\n\n";
            out << "Options:\n";
            out << "  --mode=<name>         Start in specific mode\n";
            out << "  --minimized           Start minimized to tray\n";
            out << "  --no-tray             Disable system tray\n";
            out << "  --style=<name>        Qt style (fusion, windows, etc.)\n";
            out << "  --theme=<name>        UI theme (dark, light, system)\n";
            out << "  --geometry=WxH        Window size\n";
            out << "  --list-sources        List capture sources\n";
            out << "  --source=<id>         Select capture source\n";
            out << "  --duration=<sec>      Recording duration\n";
            out << "  --output=<file>       Output file path\n";
            out << "  -h, --help            Show this help\n";
            out << "  -v, --version         Show version\n";
        }

        void showVersion() {
            QTextStream out(stdout);
            out << "Aegis Multimedia Suite v" << AEGIS_VERSION << "\n";
            out << "Built with Qt " << QT_VERSION_STR << "\n";
        }

        static QString modeToString(AppMode mode) {
            switch (mode) {
                case AppMode::MediaSuite: return "mediasuite";
                case AppMode::MediaPlayer: return "mediaplayer";
                case AppMode::AudioEditor: return "audioeditor";
                case AppMode::VideoEditor: return "videoeditor";
                case AppMode::DAW: return "daw";
                case AppMode::DiscBurner: return "discburner";
                case AppMode::ScreenRecorder: return "screenrecorder";
                case AppMode::WebcamRecorder: return "webcam";
                case AppMode::DVBTuner: return "dvbtuner";
                case AppMode::IPCameraViewer: return "ipcamera";
                case AppMode::AudioRecorder: return "audiorecorder";
                case AppMode::StreamingStudio: return "streaming";
                default: return "unknown";
            }
        }

        QApplication m_app;
        std::unique_ptr<QQmlApplicationEngine> m_engine;
        std::unique_ptr<Core> m_core;
        std::unique_ptr<QSystemTrayIcon> m_tray;
        Logger m_logger;
    };

} // namespace Aegis

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    try {
        Aegis::AegisApplication app(argc, argv);
        return app.run();
    } catch (const std::exception& e) {
        qCritical() << "Fatal error:" << e.what();
        return 1;
    }
}

#include "main_refactored.moc"
