// plugin_interface.h - Fixed Plugin Interface for Aegis
#pragma once

#include <QObject>
#include <QtQml/QQmlApplicationEngine>
#include <QQmlContext>
#include <QVariantMap>
#include <memory>
#include <type_traits>
#include <unordered_map>

namespace Aegis {

// Forward declarations
class Library;
class Core;

// Forward declaration for AppMode enum (defined in main.cpp)
enum class AppMode;

/**
 * @brief Application context passed to plugins during initialization
 */
struct AppContext {
    QQmlApplicationEngine* engine{nullptr};
    QStringList arguments;
    QVariantMap config;
    std::shared_ptr<Library> library;
    std::shared_ptr<Core> core;
    AppMode mode;  // ADDED: Mode field (was missing but used in main.cpp)
};

/**
 * @brief Base plugin interface for all Aegis application modes
 * 
 * All plugins must inherit from this class and implement the required
 * virtual methods to provide their specific functionality.
 */
class AppModePlugin : public QObject {
    Q_OBJECT
public:
    explicit AppModePlugin(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~AppModePlugin() = default;

    // ============== Required Interface ==============

    /**
     * @brief Get the mode identifier (unique name)
     * @return Mode identifier string
     */
    virtual QString modeName() const = 0;

    /**
     * @brief Get the human-readable display name
     * @return Display name for UI
     */
    virtual QString displayName() const = 0;

    /**
     * @brief Get the QML entry point for this mode
     * @return QML file path or URL
     */
    virtual QString qmlEntryPoint() const = 0;

    /**
     * @brief Initialize the plugin with application context
     * @param ctx Application context
     * @return True if initialization succeeded
     */
    virtual bool initialize(const AppContext& ctx) = 0;

    /**
     * @brief Shutdown the plugin and cleanup resources
     */
    virtual void shutdown() = 0;

    // ============== Optional Interface ==============

    /**
     * @brief Handle command line arguments
     * @param args List of arguments
     */
    virtual void handleArguments(const QStringList& args) { Q_UNUSED(args) }

    /**
     * @brief Check if plugin supports video playback
     * @return True if video is supported
     */
    virtual bool hasVideo() const { return false; }

    /**
     * @brief Check if plugin supports editing features
     * @return True if editing is available
     */
    virtual bool hasEditing() const { return false; }

    /**
     * @brief Check if plugin supports recording
     * @return True if recording is supported
     */
    virtual bool hasRecording() const { return false; }

protected:
    /**
     * @brief Helper to safely expose objects to QML with ownership tracking
     */
    template<typename T>
    void exportToQml(const AppContext& ctx, const QString &name, T* obj) {
        if (ctx.engine && ctx.engine->rootContext()) {
            ctx.engine->rootContext()->setContextProperty(name, obj);
        }
    }
};

/**
 * @brief Plugin registry for managing available plugins
 * 
 * Maintains a registry of all available plugins and provides
 * methods for plugin discovery and instantiation.
 */
class PluginRegistry {
public:
    /**
     * @brief Get the singleton instance
     * @return PluginRegistry instance
     */
    static PluginRegistry& instance() {
        static PluginRegistry registry;
        return registry;
    }

    /**
     * @brief Register a plugin type
     * @tparam PluginType Plugin class type (must inherit from AppModePlugin)
     */
    template<typename PluginType>
    void registerPlugin() {
        static_assert(std::is_base_of_v<AppModePlugin, PluginType>,
                      "Must derive from AppModePlugin");
        auto plugin = std::make_unique<PluginType>();
        m_plugins[plugin->modeName()] = std::move(plugin);
    }

    /**
     * @brief Register a plugin instance
     * @param plugin Plugin instance to register
     */
    void registerPlugin(std::unique_ptr<AppModePlugin> plugin) {
        if (plugin) {
            m_plugins[plugin->modeName()] = std::move(plugin);
        }
    }

    /**
     * @brief Get a plugin by mode name
     * @param mode Mode identifier
     * @return Plugin instance or nullptr if not found
     */
    AppModePlugin* get(const QString& mode) const {
        auto it = m_plugins.find(mode);
        return (it != m_plugins.end()) ? it->second.get() : nullptr;
    }

    /**
     * @brief Get list of available modes
     * @return List of mode identifiers
     */
    QStringList availableModes() const {
        QStringList modes;
        for (const auto& [name, _] : m_plugins) {
            modes.append(name);
        }
        return modes;
    }

    /**
     * @brief Auto-detect mode from file extension or hint
     * @param hint File path or mode hint
     * @return Detected mode identifier
     */
    QString detectMode(const QString &hint) const {
        // Default detection logic
        if (hint.endsWith(".cdg") || hint.endsWith(".kar")) {
            return "karaoke";
        }
        if (hint.endsWith(".iso") || hint.endsWith(".cue")) {
            return "discburner";
        }
        return "mediaplayer";
    }

private:
    PluginRegistry() = default;
    std::unordered_map<QString, std::unique_ptr<AppModePlugin>> m_plugins;
};

} // namespace Aegis

// Metatype declarations for Qt
Q_DECLARE_METATYPE(Aegis::AppContext)
