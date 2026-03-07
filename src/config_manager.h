// config_manager.h - Unified configuration management
#pragma once

#include <QObject>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QStandardPaths>
#include <QHash>
#include <QMutex>
#include <QStack>
#include <shared_mutex>
#include <optional>
#include <any>

namespace Aegis {

// ============================================================================
// Type-safe configuration value
// ============================================================================

class ConfigValue {
public:
    enum class Type {
        Null,
        Bool,
        Int,
        Double,
        String,
        StringList,
        ByteArray,
        Color,
        Font,
        Rect,
        Size,
        Point,
        DateTime
    };

private:
    Type m_type{Type::Null};
    QVariant m_value;

public:
    ConfigValue() = default;
    
    template<typename T>
    explicit ConfigValue(const T& value) : m_value(value) {
        deduceType();
    }

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }

    template<typename T>
    std::optional<T> get() const {
        if (m_value.canConvert<T>()) {
            return m_value.value<T>();
        }
        return std::nullopt;
    }

    QVariant toVariant() const { return m_value; }

private:
    void deduceType() {
        int type = m_value.userType();
        if (type == QMetaType::Bool) m_type = Type::Bool;
        else if (type == QMetaType::Int) m_type = Type::Int;
        else if (type == QMetaType::Double) m_type = Type::Double;
        else if (type == QMetaType::QString) m_type = Type::String;
        else if (type == QMetaType::QStringList) m_type = Type::StringList;
        else if (type == QMetaType::QByteArray) m_type = Type::ByteArray;
        else if (type == QMetaType::QColor) m_type = Type::Color;
        else if (type == QMetaType::QFont) m_type = Type::Font;
        else if (type == QMetaType::QRect) m_type = Type::Rect;
        else if (type == QMetaType::QSize) m_type = Type::Size;
        else if (type == QMetaType::QPoint) m_type = Type::Point;
        else if (type == QMetaType::QDateTime) m_type = Type::DateTime;
        else m_type = Type::Null;
    }
};

// ============================================================================
// Configuration Manager with Change Notification
// ============================================================================

class ConfigManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList keys READ keys NOTIFY configChanged)

public:
    static ConfigManager& instance() {
        static ConfigManager manager;
        return manager;
    }

    ~ConfigManager() override {
        save();
    }

    // ================ Basic Access ================

    template<typename T>
    T get(const QString& key, const T& defaultValue = T()) const {
        std::shared_lock lock(m_mutex);
        
        auto it = m_config.find(key);
        if (it != m_config.end()) {
            auto value = it->template get<T>();
            if (value) return *value;
        }
        
        return defaultValue;
    }

    template<typename T>
    void set(const QString& key, const T& value) {
        {
            std::unique_lock lock(m_mutex);
            m_config[key] = ConfigValue(value);
            m_modified = true;
        }
        
        emit configChanged(key);
        emit configValueChanged(key, QVariant::fromValue(value));
    }

    bool contains(const QString& key) const {
        std::shared_lock lock(m_mutex);
        return m_config.contains(key);
    }

    void remove(const QString& key) {
        {
            std::unique_lock lock(m_mutex);
            m_config.remove(key);
            m_modified = true;
        }
        emit configChanged(key);
    }

    QStringList keys() const {
        std::shared_lock lock(m_mutex);
        return m_config.keys();
    }

    // ================ Schema-based Access ================

    struct Schema {
        enum class Type {
            String,
            Integer,
            Float,
            Boolean,
            Color,
            Font,
            Enum,
            Path,
            List
        };

        QString key;
        Type type;
        QVariant defaultValue;
        QString description;
        QVariantList constraints;  // Min/max, allowed values, etc.
        bool required{false};
    };

    void registerSchema(const QList<Schema>& schemas) {
        std::unique_lock lock(m_mutex);
        for (const auto& schema : schemas) {
            m_schemas[schema.key] = schema;
            
            // Set default if not present
            if (!m_config.contains(schema.key) && !schema.defaultValue.isNull()) {
                m_config[schema.key] = ConfigValue(schema.defaultValue);
            }
        }
    }

    std::optional<Schema> schema(const QString& key) const {
        std::shared_lock lock(m_mutex);
        return m_schemas.contains(key) 
            ? std::optional<Schema>(m_schemas[key]) 
            : std::nullopt;
    }

    // ================ Persistence ================

    void load(const QString& path = QString()) {
        QString configPath = path;
        if (configPath.isEmpty()) {
            configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                         + "/aegis.conf";
        }

        QFile file(configPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        
        if (error.error != QJsonParseError::NoError) {
            qWarning() << "Failed to parse config:" << error.errorString();
            return;
        }

        QJsonObject obj = doc.object();
        
        {
            std::unique_lock lock(m_mutex);
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                m_config[it.key()] = ConfigValue(it.value().toVariant());
            }
            m_modified = false;
            m_configPath = configPath;
        }

        emit configLoaded();
    }

    void save(const QString& path = QString()) {
        std::shared_lock lock(m_mutex);
        if (!m_modified) return;

        QString savePath = path.isEmpty() ? m_configPath : path;
        if (savePath.isEmpty()) {
            savePath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                       + "/aegis.conf";
            QDir().mkpath(QFileInfo(savePath).path());
        }

        QJsonObject obj;
        for (auto it = m_config.begin(); it != m_config.end(); ++it) {
            obj[it.key()] = QJsonValue::fromVariant(it.value().toVariant());
        }

        QFile file(savePath);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "Failed to open config for writing:" << savePath;
            return;
        }

        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        m_modified = false;
    }

    // ================ Synchronization ================

    void beginGroup(const QString& prefix) {
        m_groupStack.push(m_currentGroup);
        if (!m_currentGroup.isEmpty()) {
            m_currentGroup = prefix;
        } else {
            m_currentGroup = prefix + "/";
        }
    }

    void endGroup() {
        if (!m_groupStack.isEmpty()) {
            m_currentGroup = m_groupStack.pop();
        }
    }

    QString groupKey(const QString& key) const {
        return m_currentGroup.isEmpty() ? key : m_currentGroup + key;
    }

    // ================ Signals ================

signals:
    void configChanged(const QString& key);
    void configValueChanged(const QString& key, const QVariant& value);
    void configLoaded();
    void configSaved();

private:
    ConfigManager(QObject* parent = nullptr) : QObject(parent) {
        // Auto-save every 5 minutes
        m_autoSaveTimer.setInterval(5 * 60 * 1000);
        connect(&m_autoSaveTimer, &QTimer::timeout, this, [this]() { save(); });
        m_autoSaveTimer.start();
    }

    mutable std::shared_mutex m_mutex;
    QHash<QString, ConfigValue> m_config;
    QHash<QString, Schema> m_schemas;
    QString m_configPath;
    QString m_currentGroup;
    QStack<QString> m_groupStack;
    bool m_modified{false};
    QTimer m_autoSaveTimer;
};

// ============================================================================
// Convenience Wrappers
// ============================================================================

template<typename T>
class ConfigProperty {
public:
    ConfigProperty(const QString& key, const T& defaultValue = T())
        : m_key(key)
        , m_default(defaultValue) {}

    T get() const {
        return ConfigManager::instance().get<T>(m_key, m_default);
    }

    void set(const T& value) const {
        ConfigManager::instance().set(m_key, value);
    }

    operator T() const { return get(); }
    ConfigProperty& operator=(const T& value) { set(value); return *this; }

private:
    QString m_key;
    T m_default;
};

// Predefined configuration keys
namespace Config {
    inline const QString AudioVolume = "audio/volume";
    inline const QString AudioMuted = "audio/muted";
    inline const QString AudioDevice = "audio/output_device";
    inline const QString AudioBufferSize = "audio/buffer_size";
    
    inline const QString PlaybackRepeatMode = "playback/repeat_mode";
    inline const QString PlaybackShuffle = "playback/shuffle";
    inline const QString PlaybackAutoPlay = "playback/autoplay";
    
    inline const QString LibraryPath = "library/path";
    inline const QString LibraryAutoScan = "library/auto_scan";
    inline const QString LibraryWatchFolders = "library/watch_folders";
    
    inline const QString UiTheme = "ui/theme";
    inline const QString UiLanguage = "ui/language";
    inline const QString UiWindowGeometry = "ui/window_geometry";
    inline const QString UiTrayIcon = "ui/tray_icon";
    
    inline const QString NetworkProxy = "network/proxy";
    inline const QString NetworkCacheSize = "network/cache_size";
    
    inline const QString CaptureOutputDir = "capture/output_directory";
    inline const QString CaptureVideoCodec = "capture/video_codec";
    inline const QString CaptureAudioCodec = "capture/audio_codec";
    inline const QString CaptureBitrate = "capture/bitrate";
    inline const QString CaptureFps = "capture/fps";
}

} // namespace Aegis