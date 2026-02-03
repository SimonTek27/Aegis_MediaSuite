// main.cpp
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
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
#include <QTextStream>
#include <QFile>
#include <QHash>
#include <memory>

// Core components
#include "core.h"
#include "audio.h"
#include "video.h"

// Plugin interface
#include "plugin_interface.h"

// Optional components (loaded dynamically based on mode)
#include "library.h"
#include "audioeditor.h"
#include "disc.h"
#include "discburner.h"
#include "djmix.h"
#include "karaoke.h"
#include "disc_labelmaker.h"
#include "capture.h"

#ifdef KF6_VERSION
#include <KAboutData>
#include <KLocalizedString>
#include <KDBusService>
#include <KCoreAddons>
#endif

// Forward declarations for plugin implementations
namespace Aegis {
    class MediaPlayerPlugin;
    class EditorPlugin;
    class DiscBurnerPlugin;
    class DJMixPlugin;
    class KaraokePlugin;
}

namespace Aegis {
    enum class AppMode {
        MediaPlayer,
        SoundEditor,
        DiscBurner,
        DJMixer,
        KaraokePlayer,
        LabelMaker
    };

    struct AppContext {
        QQmlApplicationEngine* engine = nullptr;
        QStringList arguments;
        QVariantMap config;
    };
}

/**
 * @brief Application singleton instance manager
 */
class ApplicationManager : public QObject {
    Q_OBJECT
public:
    static ApplicationManager& instance() {
        static ApplicationManager manager;
        return manager;
    }

    bool isPrimaryInstance() const { return m_isPrimary; }
    QString instanceId() const { return m_instanceId; }

    bool sendToPrimary(const QString &message, const QStringList &files = {}) {
        Q_UNUSED(message);
        Q_UNUSED(files);
        // TODO: Implement D-Bus or socket-based IPC for KDE/Linux
        qDebug() << "IPC not implemented, running as independent instance";
        return false;
    }

signals:
    void receivedMessage(const QString &message, const QStringList &files);

private:
    ApplicationManager() : m_instanceId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
        QString lockPath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (lockPath.isEmpty()) {
            lockPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        }
        lockPath += "/aegis_" + QCoreApplication::applicationName() + ".lock";

        m_lockFile = new QLockFile(lockPath);
        m_lockFile->setStaleLockTime(0);

        m_isPrimary = m_lockFile->tryLock();
        if (!m_isPrimary) {
            qDebug() << "Secondary instance detected";
        }
    }

    ~ApplicationManager() {
        if (m_lockFile) {
            m_lockFile->unlock();
            delete m_lockFile;
        }
    }

    bool m_isPrimary = true;
    QString m_instanceId;
    QLockFile *m_lockFile = nullptr;
};

/**
 * @brief Command line handler for different modes
 */
class CommandLineHandler {
public:
    struct ParsedArgs {
        Aegis::AppMode mode = Aegis::AppMode::MediaPlayer;
        QStringList files;
        QVariantMap options;
        bool helpRequested = false;
        bool versionRequested = false;
    };

    static ParsedArgs parse(const QStringList &args) {
        ParsedArgs result;

        if (args.size() <= 1) {
            result.mode = detectModeFromBinary(args.value(0));
            return result;
        }

        // First pass for mode and flags
        for (int i = 1; i < args.size(); ++i) {
            const QString &arg = args[i];

            if (arg == "--help" || arg == "-h") {
                result.helpRequested = true;
            } else if (arg == "--version" || arg == "-v") {
                result.versionRequested = true;
            } else if (arg.startsWith("--mode=")) {
                QString modeStr = arg.mid(7);
                result.mode = stringToMode(modeStr);
            } else if (arg == "--mediaplayer") {
                result.mode = Aegis::AppMode::MediaPlayer;
            } else if (arg == "--editor") {
                result.mode = Aegis::AppMode::SoundEditor;
            } else if (arg == "--discburner") {
                result.mode = Aegis::AppMode::DiscBurner;
            } else if (arg == "--djmix") {
                result.mode = Aegis::AppMode::DJMixer;
            } else if (arg == "--karaoke") {
                result.mode = Aegis::AppMode::KaraokePlayer;
            } else if (arg == "--labelmaker") {
                result.mode = Aegis::AppMode::LabelMaker;
            } else if (arg.startsWith("-")) {
                // Other options
                if (arg.startsWith("--")) {
                    int eq = arg.indexOf('=');
                    if (eq > 0) {
                        QString key = arg.mid(2, eq - 2);
                        QString value = arg.mid(eq + 1);
                        result.options[key] = value;
                    }
                }
            } else if (QFile::exists(arg) || QUrl(arg).isValid()) {
                result.files.append(arg);
            }
        }

        // Auto-detect mode from first file if not specified
        if (result.mode == Aegis::AppMode::MediaPlayer && !result.files.isEmpty()) {
            result.mode = detectModeFromFile(result.files.first());
        }

        return result;
    }

private:
    static Aegis::AppMode detectModeFromBinary(const QString &argv0) {
        if (argv0.isEmpty()) {
            return Aegis::AppMode::MediaPlayer;
        }

        QString baseName = QFileInfo(argv0).baseName().toLower();

        static const QHash<QString, Aegis::AppMode> modeMap = {
            {"aegis_mediaplayer", Aegis::AppMode::MediaPlayer},
            {"aegis_player", Aegis::AppMode::MediaPlayer},
            {"aegis", Aegis::AppMode::MediaPlayer},

            {"aegis_editor", Aegis::AppMode::SoundEditor},
            {"aegis_soundeditor", Aegis::AppMode::SoundEditor},

            {"aegis_discburner", Aegis::AppMode::DiscBurner},
            {"aegis_disc", Aegis::AppMode::DiscBurner},

            {"aegis_djmix", Aegis::AppMode::DJMixer},
            {"aegis_mix", Aegis::AppMode::DJMixer},

            {"aegis_karaoke", Aegis::AppMode::KaraokePlayer},
            {"aegis_sing", Aegis::AppMode::KaraokePlayer},

            {"aegis_labelmaker", Aegis::AppMode::LabelMaker},
            {"aegis_label", Aegis::AppMode::LabelMaker}
        };

        return modeMap.value(baseName, Aegis::AppMode::MediaPlayer);
    }

    static Aegis::AppMode detectModeFromFile(const QString &file) {
        QFileInfo info(file);
        QString suffix = info.suffix().toLower();

        // Audio files -> Media Player
        static const QStringList audioExtensions = {"mp3", "flac", "ogg", "wav", "m4a", "opus", "aac"};
        if (audioExtensions.contains(suffix)) {
            return Aegis::AppMode::MediaPlayer;
        }

        // Video files -> Media Player
        static const QStringList videoExtensions = {"mp4", "mkv", "avi", "mov", "webm", "wmv", "flv"};
        if (videoExtensions.contains(suffix)) {
            return Aegis::AppMode::MediaPlayer;
        }

        // CDG/Karaoke files -> Karaoke Player
        static const QStringList karaokeExtensions = {"cdg", "zip", "kfn", "kar", "kfn"};
        if (karaokeExtensions.contains(suffix)) {
            return Aegis::AppMode::KaraokePlayer;
        }

        // Project files -> Editor
        static const QStringList projectExtensions = {"aegisproj", "audacity", "flp", "als", "ptx"};
        if (projectExtensions.contains(suffix)) {
            return Aegis::AppMode::SoundEditor;
        }

        // ISO/Disc images -> Disc Burner
        static const QStringList discExtensions = {"iso", "img", "nrg", "bin", "cue", "mds", "dmg"};
        if (discExtensions.contains(suffix)) {
            return Aegis::AppMode::DiscBurner;
        }

        // Playlist files -> Media Player
        static const QStringList playlistExtensions = {"m3u", "m3u8", "pls", "xspf", "asx", "wpl"};
        if (playlistExtensions.contains(suffix)) {
            return Aegis::AppMode::MediaPlayer;
        }

        return Aegis::AppMode::MediaPlayer;
    }

    static Aegis::AppMode stringToMode(const QString &modeStr) {
        static const QHash<QString, Aegis::AppMode> modeMap = {
            {"player", Aegis::AppMode::MediaPlayer},
            {"media", Aegis::AppMode::MediaPlayer},
            {"mediaplayer", Aegis::AppMode::MediaPlayer},

            {"editor", Aegis::AppMode::SoundEditor},
            {"soundeditor", Aegis::AppMode::SoundEditor},
            {"audioeditor", Aegis::AppMode::SoundEditor},

            {"discburner", Aegis::AppMode::DiscBurner},
            {"disc", Aegis::AppMode::DiscBurner},
            {"burner", Aegis::AppMode::DiscBurner},

            {"djmix", Aegis::AppMode::DJMixer},
            {"dj", Aegis::AppMode::DJMixer},
            {"mix", Aegis::AppMode::DJMixer},

            {"karaoke", Aegis::AppMode::KaraokePlayer},
            {"sing", Aegis::AppMode::KaraokePlayer},
            {"kj", Aegis::AppMode::KaraokePlayer},

            {"labelmaker", Aegis::AppMode::LabelMaker},
            {"label", Aegis::AppMode::LabelMaker}
        };

        return modeMap.value(modeStr.toLower(), Aegis::AppMode::MediaPlayer);
    }
};

/**
 * @brief Application initializer with proper setup
 */
class ApplicationInitializer {
public:
    static void setupApplication(QApplication &app) {
        // Set application attributes
        app.setApplicationName("aegis");
        app.setOrganizationName("Aegis");
        app.setOrganizationDomain("org.aegis");
        app.setApplicationVersion("1.0.0");

        // Enable high DPI scaling
        app.setAttribute(Qt::AA_EnableHighDpiScaling);
        app.setAttribute(Qt::AA_UseHighDpiPixmaps);

        // Set style
        QStyle *style = QStyleFactory::create("Fusion");
        if (style) {
            app.setStyle(style);
        }

        // Load translations
        loadTranslations(app);

        // Enable OpenGL if available
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setVersion(3, 3);
        format.setSamples(4); // MSAA
        QSurfaceFormat::setDefaultFormat(format);
    }

    static void loadTranslations(QApplication &app) {
        QTranslator *translator = new QTranslator(&app);
        QString locale = QLocale::system().name();

        // Try resources first
        if (translator->load(":/translations/aegis_" + locale)) {
            app.installTranslator(translator);
        }
        // Try application directory
        else if (translator->load("aegis_" + locale,
            QCoreApplication::applicationDirPath() + "/translations")) {
            app.installTranslator(translator);
            }
            // Try system translations
            else if (translator->load("aegis_" + locale,
                QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
                app.installTranslator(translator);
                } else {
                    delete translator;
                }

                #ifdef KF6_VERSION
                // Load KDE translations
                KLocalizedString::setApplicationDomain("aegis");
            #endif
    }

    static void setupEnvironment() {
        // Set XDG directories
        QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        qputenv("XDG_DATA_DIRS", dataDirs.join(':').toUtf8());

        QStringList configDirs = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
        qputenv("XDG_CONFIG_DIRS", configDirs.join(':').toUtf8());

        // Enable Wayland if available
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            // Auto-detect Wayland
            if (QFile::exists("/run/wayland/wayland-0") ||
                qEnvironmentVariableIsSet("WAYLAND_DISPLAY") ||
                qEnvironmentVariableIsSet("XDG_SESSION_TYPE") &&
                QString(qgetenv("XDG_SESSION_TYPE")).contains("wayland")) {
                qputenv("QT_QPA_PLATFORM", "wayland;xcb");
                }
        }

        // Enable MPRIS
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")) {
            qputenv("QT_QPA_PLATFORMTHEME", "qt5ct");
        }

        // Set audio backend preference
        qputenv("PULSE_PROP_media.role", "music");

        // Set reasonable OpenGL defaults
        qputenv("QT_OPENGL", "desktop");
    }
};

/**
 * @brief Stub plugin implementations for compilation
 */
namespace Aegis {

    class BasePlugin : public PluginInterface {
    public:
        virtual ~BasePlugin() = default;

        bool initialize(const AppContext& context) override {
            m_context = context;
            return true;
        }

        void shutdown() override {}

        void handleArguments(const QStringList& args) override {
            Q_UNUSED(args);
        }

        QString qmlEntryPoint() const override {
            return QString();
        }

        QString modeName() const override {
            return QString();
        }

    protected:
        AppContext m_context;
    };

    class MediaPlayerPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/MediaPlayer/Main.qml";
        }

        QString modeName() const override {
            return "mediaplayer";
        }
    };

    class EditorPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/SoundEditor/Main.qml";
        }

        QString modeName() const override {
            return "editor";
        }
    };

    class DiscBurnerPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/DiscBurner/Main.qml";
        }

        QString modeName() const override {
            return "discburner";
        }
    };

    class DJMixPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/DJMix/Main.qml";
        }

        QString modeName() const override {
            return "djmix";
        }
    };

    class KaraokePlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/Karaoke/Main.qml";
        }

        QString modeName() const override {
            return "karaoke";
        }
    };

    class PluginRegistry {
    public:
        static PluginRegistry& global() {
            static PluginRegistry registry;
            return registry;
        }

        void registerPlugin(std::unique_ptr<PluginInterface> plugin) {
            m_plugins[plugin->modeName()] = std::move(plugin);
        }

        PluginInterface* getPlugin(const QString& modeName) {
            auto it = m_plugins.find(modeName);
            return (it != m_plugins.end()) ? it->get() : nullptr;
        }

    private:
        QHash<QString, std::unique_ptr<PluginInterface>> m_plugins;
    };
}

/**
 * @brief Main application class
 */
class AegisApplication {
public:
    AegisApplication(int &argc, char **argv)
    : m_app(argc, argv)
    , m_engine(new QQmlApplicationEngine()) {
    }

    int run() {
        // Setup application
        ApplicationInitializer::setupApplication(m_app);
        ApplicationInitializer::setupEnvironment();

        #ifdef KF6_VERSION
        // Initialize KDE About Data
        KAboutData about("aegis",
                         i18n("Aegis Multimedia Suite"),
                         "1.0.0",
                         i18n("Universal multimedia application suite"),
                         KAboutLicense::GPL_V3,
                         i18n("Copyright 2024, Aegis Project"));
        about.addAuthor(i18n("Aegis Team"),
                        i18n("Development"),
                        "team@aegis.example.com");
        KAboutData::setApplicationData(about);

        // KDE DBus service
        KDBusService service(KDBusService::Unique);
        #endif

        // Parse command line
        auto args = CommandLineHandler::parse(m_app.arguments());

        // Handle special cases
        if (args.helpRequested) {
            showHelp(args.mode);
            return 0;
        }

        if (args.versionRequested) {
            showVersion();
            return 0;
        }

        // Check if we're the primary instance
        auto &appManager = ApplicationManager::instance();
        if (!appManager.isPrimaryInstance() && !args.files.isEmpty()) {
            // Forward to primary instance and exit
            if (appManager.sendToPrimary("open", args.files)) {
                qDebug() << "Files forwarded to primary instance";
                return 0;
            }
        }

        // Initialize plugin system
        initializePlugins();

        // Get the appropriate plugin for the mode
        QString modeKey = modeToString(args.mode);
        auto *plugin = Aegis::PluginRegistry::global().getPlugin(modeKey);
        if (!plugin) {
            qCritical() << "No plugin available for mode:" << modeKey;
            showHelp(args.mode);
            return 1;
        }

        // Setup application context
        Aegis::AppContext context;
        context.engine = m_engine.get();
        context.arguments = args.files;
        context.config = args.options;

        // Initialize plugin
        if (!plugin->initialize(context)) {
            qCritical() << "Failed to initialize plugin:" << plugin->modeName();
            return 1;
        }

        // Get QML entry point
        QString qmlPath = plugin->qmlEntryPoint();
        if (qmlPath.isEmpty()) {
            qCritical() << "No QML entry point defined for plugin:" << plugin->modeName();
            return 1;
        }

        // Load QML
        QUrl qmlUrl(qmlPath);
        if (qmlUrl.scheme().isEmpty()) {
            // Assume it's a file path
            qmlUrl = QUrl::fromLocalFile(qmlPath);
        }

        m_engine->load(qmlUrl);

        if (m_engine->rootObjects().isEmpty()) {
            qCritical() << "Failed to load QML interface from:" << qmlUrl.toString();

            // Try fallback
            QString fallback = QString("qrc:/qml/%1/Main.qml").arg(plugin->modeName());
            m_engine->load(QUrl(fallback));

            if (m_engine->rootObjects().isEmpty()) {
                qCritical() << "Failed to load fallback QML interface";
                return 1;
            }
        }

        // Handle files passed via command line
        if (!args.files.isEmpty()) {
            plugin->handleArguments(args.files);
        }

        // Connect cleanup
        QObject::connect(&m_app, &QApplication::aboutToQuit, [plugin]() {
            plugin->shutdown();
        });

        // Run event loop
        return m_app.exec();
    }

private:
    void initializePlugins() {
        // Register all available plugins
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::MediaPlayerPlugin>());
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::EditorPlugin>());
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::DiscBurnerPlugin>());
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::DJMixPlugin>());
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::KaraokePlugin>());
    }

    void showHelp(Aegis::AppMode mode) {
        QTextStream out(stdout);
        out << "Aegis Multimedia Suite - Version " << QCoreApplication::applicationVersion() << "\n\n";

        switch (mode) {
            case Aegis::AppMode::SoundEditor:
                out << "Usage: aegis --editor [options] [files...]\n\n"
                << "Audio editing and mastering workstation\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --new               Start new project\n"
                << "  --export FORMAT     Export format (wav, flac, mp3, ogg)\n"
                << "  --sample-rate RATE  Set project sample rate (default: 44100)\n"
                << "  --bit-depth DEPTH   Set project bit depth (16, 24, 32)\n\n"
                << "Examples:\n"
                << "  aegis --editor song.wav              Edit audio file\n"
                << "  aegis --editor --new --sample-rate=48000  Start new 48kHz project\n";
                break;

            case Aegis::AppMode::DiscBurner:
                out << "Usage: aegis --discburner [options] [device|image]\n\n"
                << "Optical disc burning and ripping\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --list-drives       List available optical drives\n"
                << "  --rip [DIR]         Rip disc to directory (default: current)\n"
                << "  --burn              Burn files/ISO to disc\n"
                << "  --image FILE        Create ISO image from files\n"
                << "  --verify            Verify after burning\n"
                << "  --speed SPEED       Burning speed (1x, 2x, 4x, 8x, 16x, max)\n\n"
                << "Examples:\n"
                << "  aegis --discburner --list-drives          List drives\n"
                << "  aegis --discburner /dev/sr0 --rip ~/Music Rip CD to Music directory\n"
                << "  aegis --discburner concert.iso --burn     Burn ISO to disc\n";
                break;

            case Aegis::AppMode::DJMixer:
                out << "Usage: aegis --djmix [options] [deck1] [deck2]\n\n"
                << "Digital DJ mixing and live performance\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --bpm BPM           Set master BPM (default: 128)\n"
                << "  --key KEY           Set master key (A-G)\n"
                << "  --record [FILE]     Record mix to file\n"
                << "  --controller DEV    Use MIDI controller device\n"
                << "  --sync              Enable beat sync\n\n"
                << "Examples:\n"
                << "  aegis --djmix track1.mp3 track2.mp3  Load two tracks\n"
                << "  aegis --djmix --bpm=140 --record=mix.mp3  Record 140BPM mix\n";
                break;

            case Aegis::AppMode::KaraokePlayer:
                out << "Usage: aegis --karaoke [options] [song...]\n\n"
                << "Professional karaoke hosting system\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --rotation FILE     Load rotation from file\n"
                << "  --database FILE     Use alternative song database\n"
                << "  --scan DIR          Scan directory for karaoke files\n"
                << "  --openkj            Import OpenKJ database\n"
                << "  --record [FILE]     Record performance to file\n"
                << "  --key CHANGE        Default key change (+2, -1, 0)\n"
                << "  --fullscreen        Start in fullscreen mode\n\n"
                << "Examples:\n"
                << "  aegis --karaoke --openkj                Import OpenKJ library\n"
                << "  aegis --karaoke song1.cdg song2.zip     Queue karaoke songs\n"
                << "  aegis --karaoke --scan ~/Karaoke        Scan karaoke library\n";
                break;

            case Aegis::AppMode::MediaPlayer:
            default:
                out << "Usage: aegis [options] [files|urls...]\n\n"
                << "Universal media player and organizer\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --play              Start playback immediately\n"
                << "  --pause             Start in paused state\n"
                << "  --fullscreen, -f    Start in fullscreen mode\n"
                << "  --playlist FILE     Load playlist file\n"
                << "  --shuffle           Shuffle playlist\n"
                << "  --repeat [one|all|none]  Set repeat mode\n"
                << "  --audio-device DEV  Use specific audio device\n"
                << "  --video-output DEV  Use specific video output\n"
                << "  --visualization TYPE  Enable visualization (spectrum, waves)\n\n"
                << "Mode Selection:\n"
                << "  --mediaplayer       Media player mode (default)\n"
                << "  --editor            Audio editor mode\n"
                << "  --discburner        Disc burning mode\n"
                << "  --djmix             DJ mixing mode\n"
                << "  --karaoke           Karaoke player mode\n\n"
                << "Examples:\n"
                << "  aegis music.mp3                    Play audio file\n"
                << "  aegis video.mp4 --fullscreen       Play video fullscreen\n"
                << "  aegis https://stream.url --play    Stream internet radio\n"
                << "  aegis --playlist=party.m3u --shuffle Shuffled playlist\n"
                << "  aegis --editor song.wav            Edit audio file\n";
                break;
        }

        out << "\nFor more information, visit: https://aegis.example.com\n";
    }

    void showVersion() {
        QTextStream out(stdout);
        out << "Aegis Multimedia Suite " << QCoreApplication::applicationVersion() << "\n"
        << "Copyright (C) 2024 Aegis Project\n"
        << "License: GPLv3+\n\n"
        << "Qt Version: " << qVersion() << "\n"
        << "Build Date: " << __DATE__ << " " << __TIME__ << "\n"
        << "Architecture: " << QSysInfo::currentCpuArchitecture() << "\n"
        << "Operating System: " << QSysInfo::prettyProductName() << "\n";

        #ifdef KF6_VERSION
        out << "KDE Frameworks: " << KCoreAddons::versionString() << "\n";
        #endif
    }

    QString modeToString(Aegis::AppMode mode) {
        switch (mode) {
            case Aegis::AppMode::SoundEditor: return "editor";
            case Aegis::AppMode::DiscBurner: return "discburner";
            case Aegis::AppMode::DJMixer: return "djmix";
            case Aegis::AppMode::KaraokePlayer: return "karaoke";
            case Aegis::AppMode::LabelMaker: return "labelmaker";
            case Aegis::AppMode::MediaPlayer:
            default: return "mediaplayer";
        }
    }

    QApplication m_app;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
};

int main(int argc, char *argv[]) {
    // Set organization and application names early for QSettings
    QCoreApplication::setOrganizationName("Aegis");
    QCoreApplication::setOrganizationDomain("org.aegis");
    QCoreApplication::setApplicationName("aegis");

    #ifdef KF6_VERSION
    // Enable KDE integration
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    KLocalizedString::setApplicationDomain("aegis");
    #endif

    try {
        AegisApplication app(argc, argv);
        return app.run();
    } catch (const std::exception &e) {
        qCritical() << "Fatal error:" << e.what();
        QMessageBox::critical(nullptr,
                              QObject::tr("Fatal Error"),
                              QObject::tr("A fatal error occurred:\n%1").arg(e.what()));
        return 1;
    } catch (...) {
        qCritical() << "Unknown fatal error";
        QMessageBox::critical(nullptr,
                              QObject::tr("Fatal Error"),
                              QObject::tr("An unknown fatal error occurred"));
        return 1;
    }
}

#include "main.moc"
