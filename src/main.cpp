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
#include "mediaplayer.h"
#include "audioeditor.h"      // Dedicated audio editing (DAW/waveform)
#include "videoeditor.h"      // Dedicated video editing (timeline/compositing)
#include "daw_engine.h"       // Digital Audio Workstation engine
#include "disc.h"
#include "discburner.h"
#include "djmix.h"
#include "karaoke.h"
#include "notation_editor.h"
#include "music_notation.h"
#include "audio_daw.h"        // Unified DAW with notation
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
    class AudioEditorPlugin;      // Waveform editing
    class VideoEditorPlugin;      // Video timeline editing
    class DAWPlugin;              // Digital Audio Workstation (tracks + notation)
    class DiscBurnerPlugin;
    class DJMixPlugin;
    class KaraokePlugin;
    class MusicNotationPlugin;
}

namespace Aegis {
    enum class AppMode {
        MediaPlayer,           // General media playback (audio/video)
        AudioEditor,           // Waveform editing, mastering, effects
        VideoEditor,           // Timeline editing, compositing, color grading
        DAW,                   // Digital Audio Workstation (multi-track + notation)
        DiscBurner,            // CD/DVD/BD burning and ripping
        DJMixer,               // Live DJ mixing with decks
        KaraokePlayer,         // Karaoke hosting and playback
        MusicNotationEditor,   // Score editing (integrated into DAW mode)
        Converter,             // Batch media conversion
        MiddlewareEditor,      // Audio middleware design
        LabelMaker             // Disc label printing
    };

    struct AppContext {
        QQmlApplicationEngine* engine = nullptr;
        QStringList arguments;
        QVariantMap config;
        AppMode mode = AppMode::MediaPlayer;
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
 * @brief Command line handler for different modes with clear separation
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
            } else if (arg == "--audioeditor" || arg == "--audio-editor") {
                result.mode = Aegis::AppMode::AudioEditor;
            } else if (arg == "--videoeditor" || arg == "--video-editor") {
                result.mode = Aegis::AppMode::VideoEditor;
            } else if (arg == "--daw") {
                result.mode = Aegis::AppMode::DAW;
            } else if (arg == "--discburner" || arg == "--disc-burner") {
                result.mode = Aegis::AppMode::DiscBurner;
            } else if (arg == "--djmix" || arg == "--dj") {
                result.mode = Aegis::AppMode::DJMixer;
            } else if (arg == "--karaoke") {
                result.mode = Aegis::AppMode::KaraokePlayer;
            } else if (arg == "--notation" || arg == "--score") {
                result.mode = Aegis::AppMode::MusicNotationEditor;
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
            // Media Player variants
            {"aegis_mediaplayer", Aegis::AppMode::MediaPlayer},
            {"aegis_player", Aegis::AppMode::MediaPlayer},
            {"aegis", Aegis::AppMode::MediaPlayer},

            // Audio Editor variants (waveform editing)
            {"aegis_audioeditor", Aegis::AppMode::AudioEditor},
            {"aegis_soundeditor", Aegis::AppMode::AudioEditor},
            {"aegis_waveeditor", Aegis::AppMode::AudioEditor},

            // Video Editor variants (timeline editing)
            {"aegis_videoeditor", Aegis::AppMode::VideoEditor},
            {"aegis_video", Aegis::AppMode::VideoEditor},
            {"aegis_cut", Aegis::AppMode::VideoEditor},

            // DAW variants (multi-track + notation)
            {"aegis_daw", Aegis::AppMode::DAW},
            {"aegis_studio", Aegis::AppMode::DAW},
            {"aegis_multitrack", Aegis::AppMode::DAW},

            // Disc Burner variants
            {"aegis_discburner", Aegis::AppMode::DiscBurner},
            {"aegis_disc", Aegis::AppMode::DiscBurner},
            {"aegis_burner", Aegis::AppMode::DiscBurner},

            // DJ Mixer variants
            {"aegis_djmix", Aegis::AppMode::DJMixer},
            {"aegis_dj", Aegis::AppMode::DJMixer},
            {"aegis_mix", Aegis::AppMode::DJMixer},

            // Karaoke variants
            {"aegis_karaoke", Aegis::AppMode::KaraokePlayer},
            {"aegis_sing", Aegis::AppMode::KaraokePlayer},
            {"aegis_kj", Aegis::AppMode::KaraokePlayer},

            // Notation variants
            {"aegis_notation", Aegis::AppMode::MusicNotationEditor},
            {"aegis_score", Aegis::AppMode::MusicNotationEditor},
            {"aegis_musescore", Aegis::AppMode::MusicNotationEditor},

            // Label Maker variants
            {"aegis_labelmaker", Aegis::AppMode::LabelMaker},
            {"aegis_label", Aegis::AppMode::LabelMaker}
        };

        return modeMap.value(baseName, Aegis::AppMode::MediaPlayer);
    }

    static Aegis::AppMode detectModeFromFile(const QString &file) {
        QFileInfo info(file);
        QString suffix = info.suffix().toLower();

        // Video project files -> Video Editor
        static const QStringList videoProjectExtensions = {
            "aegisvid", "kdenlive", "mlt", "prproj", "aep", "veg"
        };
        if (videoProjectExtensions.contains(suffix)) {
            return Aegis::AppMode::VideoEditor;
        }

        // Audio project files -> DAW
        static const QStringList dawProjectExtensions = {
            "aegisproj", "flp", "als", "ptx", "logicx", "cpr", "reaper"
        };
        if (dawProjectExtensions.contains(suffix)) {
            return Aegis::AppMode::DAW;
        }

        // Audio editor project files (waveform mastering)
        static const QStringList audioProjectExtensions = {
            "aup3", "arp", "sfk", "pkf"
        };
        if (audioProjectExtensions.contains(suffix)) {
            return Aegis::AppMode::AudioEditor;
        }

        // Notation files -> DAW or Notation Editor
        static const QStringList notationExtensions = {
            "xml", "musicxml", "mxl", "mscx", "mscz", "sib", "capx"
        };
        if (notationExtensions.contains(suffix)) {
            return Aegis::AppMode::DAW;  // DAW includes notation
        }

        // Video files -> Video Editor
        static const QStringList videoExtensions = {
            "mp4", "mkv", "avi", "mov", "webm", "wmv", "flv", "m4v"
        };
        if (videoExtensions.contains(suffix)) {
            return Aegis::AppMode::VideoEditor;
        }

        // Audio files -> Audio Editor (for editing) or Media Player (for playback)
        static const QStringList audioExtensions = {
            "wav", "flac", "mp3", "ogg", "m4a", "opus", "aac", "wma"
        };
        if (audioExtensions.contains(suffix)) {
            // Default to AudioEditor for file association, MediaPlayer for general opening
            return Aegis::AppMode::AudioEditor;
        }

        // CDG/Karaoke files -> Karaoke Player
        static const QStringList karaokeExtensions = {"cdg", "kfn", "kar", "kfn"};
        if (karaokeExtensions.contains(suffix)) {
            return Aegis::AppMode::KaraokePlayer;
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

            {"audioeditor", Aegis::AppMode::AudioEditor},
            {"audio-editor", Aegis::AppMode::AudioEditor},
            {"soundeditor", Aegis::AppMode::AudioEditor},
            {"waveeditor", Aegis::AppMode::AudioEditor},

            {"videoeditor", Aegis::AppMode::VideoEditor},
            {"video-editor", Aegis::AppMode::VideoEditor},
            {"video", Aegis::AppMode::VideoEditor},

            {"daw", Aegis::AppMode::DAW},
            {"studio", Aegis::AppMode::DAW},
            {"multitrack", Aegis::AppMode::DAW},

            {"discburner", Aegis::AppMode::DiscBurner},
            {"disc-burner", Aegis::AppMode::DiscBurner},
            {"disc", Aegis::AppMode::DiscBurner},

            {"djmix", Aegis::AppMode::DJMixer},
            {"dj", Aegis::AppMode::DJMixer},

            {"karaoke", Aegis::AppMode::KaraokePlayer},

            {"notation", Aegis::AppMode::MusicNotationEditor},
            {"score", Aegis::AppMode::MusicNotationEditor},

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

        // Register custom QML types for all modes
        registerQmlTypes();

        // Load translations
        loadTranslations(app);

        // Enable OpenGL if available
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setVersion(3, 3);
        format.setSamples(4);
        QSurfaceFormat::setDefaultFormat(format);
    }

    static void registerQmlTypes() {
        // Audio/Video core components
        qmlRegisterType<AudioEngine>("Aegis.Audio", 1, 0, "AudioEngine");
        qmlRegisterType<VideoEngine>("Aegis.Video", 1, 0, "VideoEngine");

        // Editor types
        qmlRegisterType<AudioEditor>("Aegis.AudioEditor", 1, 0, "AudioEditor");
        qmlRegisterType<VideoEditor>("Aegis.VideoEditor", 1, 0, "VideoEditor");
        qmlRegisterType<DAWEngine>("Aegis.DAW", 1, 0, "DAWEngine");

        // Notation types
        qmlRegisterType<NotationEditor>("Aegis.Notation", 1, 0, "NotationEditor");
        qmlRegisterType<Score>("Aegis.Notation", 1, 0, "Score");
        qmlRegisterType<NotationClip>("Aegis.Notation", 1, 0, "NotationClip");

        // Uncreatable types
        qmlRegisterUncreatableType<MusicNotation>("Aegis.Backend", 1, 0, "MusicNotation",
                                                  QStringLiteral("MusicNotation is provided by the backend"));
        qmlRegisterUncreatableType<AudioClip>("Aegis.Backend", 1, 0, "AudioClip",
                                              QStringLiteral("AudioClip is created through DAWEngine"));
        qmlRegisterUncreatableType<MidiClip>("Aegis.Backend", 1, 0, "MidiClip",
                                             QStringLiteral("MidiClip is created through DAWEngine"));
    }

    static void loadTranslations(QApplication &app) {
        QTranslator *translator = new QTranslator(&app);
        QString locale = QLocale::system().name();

        if (translator->load(":/translations/aegis_" + locale)) {
            app.installTranslator(translator);
        } else if (translator->load("aegis_" + locale,
            QCoreApplication::applicationDirPath() + "/translations")) {
            app.installTranslator(translator);
            } else {
                delete translator;
            }

            #ifdef KF6_VERSION
            KLocalizedString::setApplicationDomain("aegis");
        #endif
    }

    static void setupEnvironment() {
        QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        qputenv("XDG_DATA_DIRS", dataDirs.join(':').toUtf8());

        QStringList configDirs = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
        qputenv("XDG_CONFIG_DIRS", configDirs.join(':').toUtf8());

        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            if (QFile::exists("/run/wayland/wayland-0") ||
                qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
                qputenv("QT_QPA_PLATFORM", "wayland;xcb");
                }
        }

        qputenv("QT_QPA_PLATFORMTHEME", "qt5ct");
        qputenv("PULSE_PROP_media.role", "music");
        qputenv("QT_OPENGL", "desktop");
    }
};

/**
 * @brief Plugin implementations with clear separation of concerns
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

    // Media Player - General playback (audio/video)
    class MediaPlayerPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/MediaPlayer/Main.qml";
        }

        QString modeName() const override {
            return "mediaplayer";
        }
    };

    // Audio Editor - Waveform editing, mastering, restoration
    // Use case: Editing podcasts, mastering albums, noise reduction
    class AudioEditorPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/AudioEditor/Main.qml";
        }

        QString modeName() const override {
            return "audioeditor";
        }

        void handleArguments(const QStringList& args) override {
            // Audio editor specific: load files into waveform view
            for (const QString& file : args) {
                if (file.endsWith(".wav") || file.endsWith(".flac") ||
                    file.endsWith(".mp3") || file.endsWith(".ogg")) {
                    // Emit signal or call method to load file
                    }
            }
        }
    };

    // Video Editor - Timeline editing, compositing, color grading
    // Use case: Cutting videos, adding effects, color correction
    class VideoEditorPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/VideoEditor/Main.qml";
        }

        QString modeName() const override {
            return "videoeditor";
        }

        void handleArguments(const QStringList& args) override {
            // Video editor specific: import media to timeline
            for (const QString& file : args) {
                if (file.endsWith(".mp4") || file.endsWith(".mkv") ||
                    file.endsWith(".mov") || file.endsWith(".avi")) {
                    // Add to timeline
                    }
            }
        }
    };

    // DAW - Digital Audio Workstation
    // Multi-track recording, mixing, notation, MIDI
    // Use case: Music production, composition, arranging
    class DAWPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/DAW/Main.qml";
        }

        QString modeName() const override {
            return "daw";
        }

        void handleArguments(const QStringList& args) override {
            // DAW specific: create tracks from files or import projects
            for (const QString& file : args) {
                if (file.endsWith(".xml") || file.endsWith(".musicxml")) {
                    // Import as notation track
                } else if (file.endsWith(".mid") || file.endsWith(".midi")) {
                    // Import as MIDI track
                } else if (file.endsWith(".wav") || file.endsWith(".flac")) {
                    // Import as audio track
                }
            }
        }
    };

    // Disc Burner - CD/DVD/BD burning and ripping
    class DiscBurnerPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/DiscBurner/Main.qml";
        }

        QString modeName() const override {
            return "discburner";
        }
    };

    // DJ Mixer - Live performance mixing
    class DJMixPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/DJMix/Main.qml";
        }

        QString modeName() const override {
            return "djmix";
        }
    };

    // Karaoke Player - Hosting and playback
    class KaraokePlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/Karaoke/Main.qml";
        }

        QString modeName() const override {
            return "karaoke";
        }
    };

    // Label Maker - Disc label design
    class LabelMakerPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override {
            return "qrc:/qml/LabelMaker/Main.qml";
        }

        QString modeName() const override {
            return "labelmaker";
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

        QStringList availableModes() const {
            return m_plugins.keys();
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
        ApplicationInitializer::setupApplication(m_app);
        ApplicationInitializer::setupEnvironment();

        #ifdef KF6_VERSION
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

        KDBusService service(KDBusService::Unique);
        #endif

        auto args = CommandLineHandler::parse(m_app.arguments());

        if (args.helpRequested) {
            showHelp(args.mode);
            return 0;
        }

        if (args.versionRequested) {
            showVersion();
            return 0;
        }

        auto &appManager = ApplicationManager::instance();
        if (!appManager.isPrimaryInstance() && !args.files.isEmpty()) {
            if (appManager.sendToPrimary("open", args.files)) {
                qDebug() << "Files forwarded to primary instance";
                return 0;
            }
        }

        initializePlugins();

        QString modeKey = modeToString(args.mode);
        auto *plugin = Aegis::PluginRegistry::global().getPlugin(modeKey);
        if (!plugin) {
            qCritical() << "No plugin available for mode:" << modeKey;
            showHelp(args.mode);
            return 1;
        }

        Aegis::AppContext context;
        context.engine = m_engine.get();
        context.arguments = args.files;
        context.config = args.options;
        context.mode = args.mode;

        if (!plugin->initialize(context)) {
            qCritical() << "Failed to initialize plugin:" << plugin->modeName();
            return 1;
        }

        QString qmlPath = plugin->qmlEntryPoint();
        if (qmlPath.isEmpty()) {
            qCritical() << "No QML entry point defined for plugin:" << plugin->modeName();
            return 1;
        }

        QUrl qmlUrl(qmlPath);
        if (qmlUrl.scheme().isEmpty()) {
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

        if (!args.files.isEmpty()) {
            plugin->handleArguments(args.files);
        }

        QObject::connect(&m_app, &QApplication::aboutToQuit, [plugin]() {
            plugin->shutdown();
        });

        return m_app.exec();
    }

private:
    void initializePlugins() {
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::MediaPlayerPlugin>());
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::AudioEditorPlugin>());
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::VideoEditorPlugin>());
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::DAWPlugin>());
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::DiscBurnerPlugin>());
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::DJMixPlugin>());
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::KaraokePlugin>());
        Aegis::PluginRegistry::global().registerPlugin(std::make_unique<Aegis::LabelMakerPlugin>());
    }

    void showHelp(Aegis::AppMode mode) {
        QTextStream out(stdout);
        out << "Aegis Multimedia Suite - Version " << QCoreApplication::applicationVersion() << "\n\n";

        switch (mode) {
            case Aegis::AppMode::AudioEditor:
                out << "Usage: aegis --audioeditor [options] [audio-files...]\n\n"
                << "Waveform audio editor for mastering and restoration\n\n"
                << "Features:\n"
                << "  - Waveform editing with spectral view\n"
                << "  - Non-destructive effects chain\n"
                << "  - Batch processing\n"
                << "  - Noise reduction and restoration tools\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --new               Start new project\n"
                << "  --export FORMAT     Export format (wav, flac, mp3, ogg)\n"
                << "  --sample-rate RATE  Set project sample rate\n"
                << "  --bit-depth DEPTH   Set project bit depth (16, 24, 32)\n\n"
                << "Examples:\n"
                << "  aegis --audioeditor podcast.wav    Edit podcast\n"
                << "  aegis --audioeditor --new          Start new project\n";
                break;

            case Aegis::AppMode::VideoEditor:
                out << "Usage: aegis --videoeditor [options] [video-files...]\n\n"
                << "Video editing and compositing workstation\n\n"
                << "Features:\n"
                << "  - Multi-track timeline editing\n"
                << "  - Color grading and correction\n"
                << "  - Video effects and transitions\n"
                << "  - Audio mixing integrated\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --new               Start new project\n"
                << "  --resolution WxH    Set project resolution\n"
                << "  --fps FPS           Set project frame rate\n\n"
                << "Examples:\n"
                << "  aegis --videoeditor clip.mp4       Edit video\n"
                << "  aegis --videoeditor --new --resolution=1920x1080  Start HD project\n";
                break;

            case Aegis::AppMode::DAW:
                out << "Usage: aegis --daw [options] [files...]\n\n"
                << "Digital Audio Workstation for music production\n\n"
                << "Features:\n"
                << "  - Multi-track recording and mixing\n"
                << "  - Music notation and scoring\n"
                << "  - MIDI sequencing and editing\n"
                << "  - Virtual instruments and effects\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --new               Start new project\n"
                << "  --template TYPE     Project template (empty, pop, rock, orchestral)\n"
                << "  --bpm BPM           Set tempo\n"
                << "  --key KEY           Set key signature\n\n"
                << "Examples:\n"
                << "  aegis --daw song.xml               Open notation file\n"
                << "  aegis --daw --new --template=orchestral  Start orchestral project\n"
                << "  aegis --daw --bpm=140 track.wav    Import audio at 140 BPM\n";
                break;

            case Aegis::AppMode::DiscBurner:
                out << "Usage: aegis --discburner [options] [device|image]\n\n"
                << "Optical disc burning and ripping\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --list-drives       List available optical drives\n"
                << "  --rip [DIR]         Rip disc to directory\n"
                << "  --burn              Burn files/ISO to disc\n"
                << "  --verify            Verify after burning\n";
                break;

            case Aegis::AppMode::DJMixer:
                out << "Usage: aegis --djmix [options] [deck1] [deck2]\n\n"
                << "Digital DJ mixing and live performance\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --bpm BPM           Set master BPM\n"
                << "  --record [FILE]     Record mix to file\n";
                break;

            case Aegis::AppMode::KaraokePlayer:
                out << "Usage: aegis --karaoke [options] [song...]\n\n"
                << "Professional karaoke hosting system\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --database FILE     Use alternative song database\n"
                << "  --fullscreen        Start in fullscreen mode\n";
                break;

            case Aegis::AppMode::MediaPlayer:
            default:
                out << "Usage: aegis [options] [files|urls...]\n\n"
                << "Universal media player and organizer\n\n"
                << "Options:\n"
                << "  --help, -h          Show this help message\n"
                << "  --version, -v       Show version information\n"
                << "  --play              Start playback immediately\n"
                << "  --fullscreen, -f    Start in fullscreen mode\n\n"
                << "Mode Selection:\n"
                << "  --mediaplayer       Media player mode (default)\n"
                << "  --audioeditor       Audio waveform editor\n"
                << "  --videoeditor       Video timeline editor\n"
                << "  --daw               Digital Audio Workstation\n"
                << "  --discburner        Disc burning mode\n"
                << "  --djmix             DJ mixing mode\n"
                << "  --karaoke           Karaoke player mode\n\n"
                << "Examples:\n"
                << "  aegis music.mp3                    Play audio\n"
                << "  aegis --audioeditor song.wav       Edit audio\n"
                << "  aegis --videoeditor video.mp4      Edit video\n"
                << "  aegis --daw project.xml            Music production\n";
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
            case Aegis::AppMode::AudioEditor: return "audioeditor";
            case Aegis::AppMode::VideoEditor: return "videoeditor";
            case Aegis::AppMode::DAW: return "daw";
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
    QCoreApplication::setOrganizationName("Aegis");
    QCoreApplication::setOrganizationDomain("org.aegis");
    QCoreApplication::setApplicationName("aegis");

    #ifdef KF6_VERSION
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
