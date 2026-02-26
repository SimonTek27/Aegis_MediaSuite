// main.cpp
// Aegis Multimedia Suite - Application Entry Point
// Version: 2.1.1
// Fixed: karaokeExtensions duplicate "kfn", version inconsistency, Qt deprecated API
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
#include "audioeditor.h"
#include "videoeditor.h"
#include "daw_engine.h"
#include "disc.h"
#include "discburner.h"
#include "djmix.h"
#include "karaoke.h"
#include "notation_editor.h"
#include "music_notation.h"
#include "audio_daw.h"
#include "disc_labelmaker.h"
#include "capture.h"

#ifdef KF6_VERSION
#include <KAboutData>
#include <KLocalizedString>
#include <KDBusService>
#include <KCoreAddons>
#endif

// ============================================================
// Application version — single source of truth
// ============================================================
#define AEGIS_VERSION "2.1.1"

namespace Aegis {

    enum class AppMode {
        MediaPlayer,
        AudioEditor,
        VideoEditor,
        DAW,
        DiscBurner,
        DJMixer,
        KaraokePlayer,
        MusicNotationEditor,
        Converter,
        MiddlewareEditor,
        LabelMaker
    };

    struct AppContext {
        QQmlApplicationEngine* engine = nullptr;
        QStringList arguments;
        QVariantMap config;
        AppMode mode = AppMode::MediaPlayer;
    };

} // namespace Aegis

// ============================================================
// ApplicationManager
// ============================================================
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
    ApplicationManager()
        : m_instanceId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
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

// ============================================================
// CommandLineHandler
// ============================================================
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

        for (int i = 1; i < args.size(); ++i) {
            const QString &arg = args[i];

            if (arg == "--help" || arg == "-h") {
                result.helpRequested = true;
            } else if (arg == "--version" || arg == "-v") {
                result.versionRequested = true;
            } else if (arg.startsWith("--mode=")) {
                result.mode = stringToMode(arg.mid(7));
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
                if (arg.startsWith("--")) {
                    int eq = arg.indexOf('=');
                    if (eq > 0) {
                        result.options[arg.mid(2, eq - 2)] = arg.mid(eq + 1);
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
        if (argv0.isEmpty()) return Aegis::AppMode::MediaPlayer;

        QString baseName = QFileInfo(argv0).baseName().toLower();

        static const QHash<QString, Aegis::AppMode> modeMap = {
            {"aegis_mediaplayer", Aegis::AppMode::MediaPlayer},
            {"aegis_player",      Aegis::AppMode::MediaPlayer},
            {"aegis",             Aegis::AppMode::MediaPlayer},

            {"aegis_audioeditor", Aegis::AppMode::AudioEditor},
            {"aegis_soundeditor", Aegis::AppMode::AudioEditor},
            {"aegis_waveeditor",  Aegis::AppMode::AudioEditor},

            {"aegis_videoeditor", Aegis::AppMode::VideoEditor},
            {"aegis_video",       Aegis::AppMode::VideoEditor},
            {"aegis_cut",         Aegis::AppMode::VideoEditor},

            {"aegis_daw",        Aegis::AppMode::DAW},
            {"aegis_studio",     Aegis::AppMode::DAW},
            {"aegis_multitrack", Aegis::AppMode::DAW},

            {"aegis_discburner", Aegis::AppMode::DiscBurner},
            {"aegis_disc",       Aegis::AppMode::DiscBurner},
            {"aegis_burner",     Aegis::AppMode::DiscBurner},

            {"aegis_djmix", Aegis::AppMode::DJMixer},
            {"aegis_dj",    Aegis::AppMode::DJMixer},
            {"aegis_mix",   Aegis::AppMode::DJMixer},

            {"aegis_karaoke", Aegis::AppMode::KaraokePlayer},
            {"aegis_sing",    Aegis::AppMode::KaraokePlayer},
            {"aegis_kj",      Aegis::AppMode::KaraokePlayer},

            {"aegis_notation", Aegis::AppMode::MusicNotationEditor},
            {"aegis_score",    Aegis::AppMode::MusicNotationEditor},
            {"aegis_musescore",Aegis::AppMode::MusicNotationEditor},

            {"aegis_labelmaker", Aegis::AppMode::LabelMaker},
            {"aegis_label",      Aegis::AppMode::LabelMaker}
        };

        return modeMap.value(baseName, Aegis::AppMode::MediaPlayer);
    }

    static Aegis::AppMode detectModeFromFile(const QString &file) {
        QFileInfo info(file);
        QString suffix = info.suffix().toLower();

        static const QStringList videoProjectExtensions = {
            "aegisvid", "kdenlive", "mlt", "prproj", "aep", "veg"
        };
        if (videoProjectExtensions.contains(suffix)) return Aegis::AppMode::VideoEditor;

        static const QStringList dawProjectExtensions = {
            "aegisproj", "flp", "als", "ptx", "logicx", "cpr", "reaper"
        };
        if (dawProjectExtensions.contains(suffix)) return Aegis::AppMode::DAW;

        static const QStringList audioProjectExtensions = {
            "aup3", "arp", "sfk", "pkf"
        };
        if (audioProjectExtensions.contains(suffix)) return Aegis::AppMode::AudioEditor;

        static const QStringList notationExtensions = {
            "xml", "musicxml", "mxl", "mscx", "mscz", "sib", "capx"
        };
        if (notationExtensions.contains(suffix)) return Aegis::AppMode::DAW;

        static const QStringList videoExtensions = {
            "mp4", "mkv", "avi", "mov", "webm", "wmv", "flv", "m4v"
        };
        if (videoExtensions.contains(suffix)) return Aegis::AppMode::VideoEditor;

        static const QStringList audioExtensions = {
            "wav", "flac", "mp3", "ogg", "m4a", "opus", "aac", "wma"
        };
        if (audioExtensions.contains(suffix)) return Aegis::AppMode::AudioEditor;

        // FIX: removed duplicate "kfn", added "ksf" (Karaoke Song File)
        static const QStringList karaokeExtensions = {"cdg", "kfn", "kar", "ksf"};
        if (karaokeExtensions.contains(suffix)) return Aegis::AppMode::KaraokePlayer;

        static const QStringList discExtensions = {
            "iso", "img", "nrg", "bin", "cue", "mds", "dmg"
        };
        if (discExtensions.contains(suffix)) return Aegis::AppMode::DiscBurner;

        static const QStringList playlistExtensions = {
            "m3u", "m3u8", "pls", "xspf", "asx", "wpl"
        };
        if (playlistExtensions.contains(suffix)) return Aegis::AppMode::MediaPlayer;

        return Aegis::AppMode::MediaPlayer;
    }

    static Aegis::AppMode stringToMode(const QString &modeStr) {
        static const QHash<QString, Aegis::AppMode> modeMap = {
            {"player",      Aegis::AppMode::MediaPlayer},
            {"media",       Aegis::AppMode::MediaPlayer},
            {"mediaplayer", Aegis::AppMode::MediaPlayer},

            {"audioeditor",  Aegis::AppMode::AudioEditor},
            {"audio-editor", Aegis::AppMode::AudioEditor},
            {"soundeditor",  Aegis::AppMode::AudioEditor},
            {"waveeditor",   Aegis::AppMode::AudioEditor},

            {"videoeditor",  Aegis::AppMode::VideoEditor},
            {"video-editor", Aegis::AppMode::VideoEditor},
            {"video",        Aegis::AppMode::VideoEditor},

            {"daw",        Aegis::AppMode::DAW},
            {"studio",     Aegis::AppMode::DAW},
            {"multitrack", Aegis::AppMode::DAW},

            {"discburner",  Aegis::AppMode::DiscBurner},
            {"disc-burner", Aegis::AppMode::DiscBurner},
            {"disc",        Aegis::AppMode::DiscBurner},

            {"djmix", Aegis::AppMode::DJMixer},
            {"dj",    Aegis::AppMode::DJMixer},

            {"karaoke", Aegis::AppMode::KaraokePlayer},

            {"notation", Aegis::AppMode::MusicNotationEditor},
            {"score",    Aegis::AppMode::MusicNotationEditor},

            {"labelmaker", Aegis::AppMode::LabelMaker},
            {"label",      Aegis::AppMode::LabelMaker}
        };

        return modeMap.value(modeStr.toLower(), Aegis::AppMode::MediaPlayer);
    }
};

// ============================================================
// ApplicationInitializer
// ============================================================
class ApplicationInitializer {
public:
    static void setupApplication(QApplication &app) {
        app.setApplicationName("aegis");
        app.setOrganizationName("Aegis");
        app.setOrganizationDomain("org.aegis");

        // FIX: version is now consistent with qml/main.qml (AEGIS_VERSION = "2.1.1")
        app.setApplicationVersion(AEGIS_VERSION);

        // FIX: AA_EnableHighDpiScaling / AA_UseHighDpiPixmaps are deprecated in Qt6
        // and removed entirely in Qt6.2+. Guard them for Qt5-only builds.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        app.setAttribute(Qt::AA_EnableHighDpiScaling);
        app.setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

        QStyle *style = QStyleFactory::create("Fusion");
        if (style) {
            app.setStyle(style);
        }

        registerQmlTypes();
        loadTranslations(app);

        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setVersion(3, 3);
        format.setSamples(4);
        QSurfaceFormat::setDefaultFormat(format);
    }

    static void setupEnvironment() {
        // Ensure writable app data directory exists
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataPath);
    }

    static void registerQmlTypes() {
        qmlRegisterType<AudioEngine>("Aegis.Audio", 1, 0, "AudioEngine");
        qmlRegisterType<VideoEngine>("Aegis.Video", 1, 0, "VideoEngine");
        qmlRegisterType<AudioEditor>("Aegis.AudioEditor", 1, 0, "AudioEditor");
        qmlRegisterType<VideoEditor>("Aegis.VideoEditor", 1, 0, "VideoEditor");
        qmlRegisterType<DAWEngine>("Aegis.DAW", 1, 0, "DAWEngine");
    }

    static void loadTranslations(QApplication &app) {
        QString locale = QLocale::system().name();
        auto *translator = new QTranslator(&app);
        QString translationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
        if (translator->load("aegis_" + locale, translationsPath)) {
            app.installTranslator(translator);
        } else {
            delete translator;
        }
    }
};

// ============================================================
// Plugin base & concrete implementations
// ============================================================
namespace Aegis {

    class BasePlugin : public AppModePlugin {
    public:
        bool initialize(const AppContext& context) override {
            m_context = context;
            return true;
        }
        void shutdown() override {}
        void handleArguments(const QStringList& args) override { Q_UNUSED(args) }
        QString qmlEntryPoint() const override { return {}; }
        QString modeName() const override { return {}; }

    protected:
        AppContext m_context;
    };

    class MediaPlayerPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override { return "qrc:/qml/MediaPlayer/Main.qml"; }
        QString modeName() const override { return "mediaplayer"; }
    };

    class AudioEditorPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override { return "qrc:/qml/AudioEditor/Main.qml"; }
        QString modeName() const override { return "audioeditor"; }
    };

    class VideoEditorPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override { return "qrc:/qml/VideoEditor/Main.qml"; }
        QString modeName() const override { return "videoeditor"; }
    };

    class DAWPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override { return "qrc:/qml/DAW/Main.qml"; }
        QString modeName() const override { return "daw"; }
    };

    class DiscBurnerPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override { return "qrc:/qml/DiscBurner/Main.qml"; }
        QString modeName() const override { return "discburner"; }
    };

    class DJMixPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override { return "qrc:/qml/DJMix/Main.qml"; }
        QString modeName() const override { return "djmix"; }
    };

    class KaraokePlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override { return "qrc:/qml/Karaoke/Main.qml"; }
        QString modeName() const override { return "karaoke"; }
    };

    class LabelMakerPlugin : public BasePlugin {
    public:
        QString qmlEntryPoint() const override { return "qrc:/qml/LabelMaker/Main.qml"; }
        QString modeName() const override { return "labelmaker"; }
    };

} // namespace Aegis

// ============================================================
// AegisApplication
// ============================================================
class AegisApplication {
public:
    AegisApplication(int &argc, char **argv)
        : m_app(argc, argv)
        , m_engine(new QQmlApplicationEngine())
    {}

    int run() {
        ApplicationInitializer::setupApplication(m_app);
        ApplicationInitializer::setupEnvironment();

#ifdef KF6_VERSION
        KAboutData about(
            "aegis",
            i18n("Aegis Multimedia Suite"),
            AEGIS_VERSION,
            i18n("Universal multimedia application suite"),
            KAboutLicense::GPL_V3,
            i18n("Copyright 2024, Aegis Project"));
        about.addAuthor(i18n("Aegis Team"), i18n("Development"), "team@aegis.example.com");
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
        context.engine    = m_engine.get();
        context.arguments = args.files;
        context.config    = args.options;
        context.mode      = args.mode;

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
            QString fallback = QString("qrc:/qml/%1/Main.qml").arg(plugin->modeName());
            m_engine->load(QUrl(fallback));
            if (m_engine->rootObjects().isEmpty()) {
                qCritical() << "Failed to load fallback QML interface";
                return 1;
            }
        }

        // Export version to QML so it is always in sync with the C++ define
        m_engine->rootContext()->setContextProperty("aegisVersion", AEGIS_VERSION);

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

    void showHelp(Aegis::AppMode /*mode*/) {
        QTextStream out(stdout);
        out << "Aegis Multimedia Suite v" << AEGIS_VERSION << "\n\n";
        out << "Usage: aegis [OPTIONS] [FILES...]\n\n";
        out << "Mode options:\n";
        out << "  --mediaplayer       Media Player (default)\n";
        out << "  --audioeditor       Audio/Waveform Editor\n";
        out << "  --videoeditor       Video Timeline Editor\n";
        out << "  --daw               Digital Audio Workstation\n";
        out << "  --discburner        Disc Burner & Ripper\n";
        out << "  --djmix             DJ Mixer\n";
        out << "  --karaoke           Karaoke Player\n";
        out << "  --notation          Music Notation Editor\n";
        out << "  --labelmaker        Disc Label Maker\n";
        out << "  --mode=<name>       Select mode by name\n\n";
        out << "General options:\n";
        out << "  -h, --help          Show this help\n";
        out << "  -v, --version       Show version\n";
    }

    void showVersion() {
        QTextStream out(stdout);
        out << "Aegis Multimedia Suite v" << AEGIS_VERSION << "\n";
        out << "Built with Qt " << QT_VERSION_STR << "\n";
    }

    static QString modeToString(Aegis::AppMode mode) {
        switch (mode) {
            case Aegis::AppMode::MediaPlayer:         return "mediaplayer";
            case Aegis::AppMode::AudioEditor:         return "audioeditor";
            case Aegis::AppMode::VideoEditor:         return "videoeditor";
            case Aegis::AppMode::DAW:                 return "daw";
            case Aegis::AppMode::DiscBurner:          return "discburner";
            case Aegis::AppMode::DJMixer:             return "djmix";
            case Aegis::AppMode::KaraokePlayer:       return "karaoke";
            case Aegis::AppMode::MusicNotationEditor: return "notation";
            case Aegis::AppMode::LabelMaker:          return "labelmaker";
            default:                                   return "mediaplayer";
        }
    }

    QApplication m_app;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
};

// ============================================================
// main()
// ============================================================
int main(int argc, char *argv[]) {
    AegisApplication app(argc, argv);
    return app.run();
}
