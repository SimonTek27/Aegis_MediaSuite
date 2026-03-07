// settings.h — Minimal Settings singleton for Aegis MediaSuite
#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>
#include <QString>

namespace Aegis {

class Settings : public QObject {
    Q_OBJECT
public:
    static Settings* instance() {
        static Settings s_instance;
        return &s_instance;
    }

    QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const {
        return m_settings.value(key, defaultValue);
    }
    void setValue(const QString& key, const QVariant& value) {
        m_settings.setValue(key, value);
    }
    void sync() { m_settings.sync(); }

signals:
    void settingChanged(const QString& key);

private:
    explicit Settings(QObject* parent = nullptr)
        : QObject(parent), m_settings("Aegis", "MediaSuite") {}

    QSettings m_settings;
};

} // namespace Aegis
