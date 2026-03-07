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

// ============================================================================
// Application Version
// ============================================================================
#undef AEGIS_VERSION  // FIX: Undefine before defining to avoid redefinition warning
#define AEGIS_VERSION "1.0.0"

namespace Aegis {

    // ============================================================================
    // Minimal Logger utility
    // ============================================================================
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

            // FIX: Use the AppContext fields directly
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
                // Primary executables
                {"launcher",       AppMode::Launcher},
                {"mediasuite",     AppMode::MediaSuite},
                {"mediaplayer",    AppMode::MediaPlayer},
                {"audioeditor",    AppMode::AudioEditor},
                {"videoeditor",    AppMode::VideoEditor},
                {"daw",            AppMode::DAW},
                {"disctools",      AppMode::DiscTools},
                {"djmixer",        AppMode::DJMixer},
                {"karaoke",        AppMode::Karaoke},
                {"modtracker",     AppMode::ModTracker},
                {"musicnotation",  AppMode::MusicNotation},
                {"middleware",     AppMode::Middleware},
                {"labelmaker",     AppMode::LabelMaker},
                {"converter",      AppMode::Converter},
                {"capture",        AppMode::Capture},
                {"library",        AppMode::Library},
                {"streaming",      AppMode::Streaming},
                // Legacy / compat aliases
                {"discburner",     AppMode::DiscTools},
                {"screenrecorder", AppMode::ScreenRecorder},
                {"webcam",         AppMode::WebcamRecorder},
                {"dvbtuner",       AppMode::DVBTuner},
                {"ipcamera",       AppMode::IPCameraViewer},
                {"audiorecorder",  AppMode::AudioRecorder},
                {"streamingstudio",AppMode::StreamingStudio},
            };
            return modeMap.value(modeStr.toLower(), AppMode::Launcher);
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
                        // FIX: Check return value
                        if (!logFile.open(QIODevice::Append | QIODevice::Text)) {
                            // Handle error if needed
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
            // Qt resets LC_NUMERIC to the system locale inside QApplication's
            // constructor.  MPV requires LC_NUMERIC=C to parse floating-point
            // values in media headers; without it mpv_create() fails immediately.
            // This must be called after QApplication is constructed, not before.
            std::setlocale(LC_NUMERIC, "C");
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

                // Extract from Result using rvalue overload (value() && returns T&&)
                auto mpvPtr = std::move(mpvResult).value();
                auto vidPtr = std::move(videoResult).value();

                // Adapter: MpvBackend implements IAudioBackend but NOT IVideoBackend.
                // Wrap it as IVideoBackend for the video slot.
                class VideoBackendFromMpv final : public IVideoBackend {
                    std::unique_ptr<MpvBackend> m;
                public:
                    explicit VideoBackendFromMpv(std::unique_ptr<MpvBackend> b) : m(std::move(b)) {}
                    Result<void> load(const QUrl& u) override { return m->load(u.toLocalFile()); }
                    Result<void> play()  override { return m->play(); }
                    Result<void> pause() override { return m->pause(); }
                    Result<void> stop()  override { return m->stop(); }
                    Result<void> seek(double p) override { return m->seek(p); }
                    QImage currentFrame() const override { return {}; }
                    QSize  videoSize()    const override { return {}; }
                    bool   hasVideo()     const override { return m->hasVideo(); }
                };

                // Adapter: AudioEngine does not inherit IAudioEngine; wrap it.
                class AudioEngineAdapter final : public IAudioEngine {
                    AudioEngine* m;
                public:
                    explicit AudioEngineAdapter(AudioEngine* e) : m(e) {}
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

                AudioEngine* rawEngine = audioEngine.get();
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

            // Register types
            qmlRegisterType<Core>("Aegis.Core", 1, 0, "Core");
            qmlRegisterUncreatableType<PlaybackState>("Aegis.Core", 1, 0, "PlaybackState",
                                                      "Enum type");

            // Set context properties
            QQmlContext* ctx = m_engine->rootContext();
            ctx->setContextProperty("aegisVersion", AEGIS_VERSION);
            ctx->setContextProperty("core", m_core.get());
            ctx->setContextProperty("config", &ConfigManager::instance());
            // FIX: These fields exist in AppContext now
            ctx->setContextProperty("commandLineFiles", QVariant::fromValue(context.files));
            ctx->setContextProperty("commandLineOptions", context.options);

            // Load appropriate QML
            QString qmlPath = getQmlPath(context.mode);
            QUrl qmlUrl(qmlPath);

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
            return qmlMap.value(mode, "qrc:/qml/ui_launcher.qml");
        }

        void setupTray() {
            // Use the SVG app icon for the tray; fall back to the window icon if unavailable.
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
            {"aegis",         "launcher"},
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
