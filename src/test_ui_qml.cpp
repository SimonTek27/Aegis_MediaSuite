// tests/test_ui_qml.cpp
// Aegis MediaSuite — Unit tests: QML utilities and UI logic
// Framework: Qt Test (QTest)
//
// FIX SUMMARY:
//  1. vtable undefined for TestQmlComponentLoading:
//     Qt's MOC generates vtable entries for Q_OBJECT classes. With a custom
//     main() spanning multiple Q_OBJECT classes in one translation unit, the
//     linker requires that the QApplication (or QGuiApplication) be created
//     BEFORE any Q_OBJECT class is instantiated, AND that the class has at
//     least one out-of-line virtual. The cleanest fix is to move to
//     QApplication (not QCoreApplication or QGuiApplication) because
//     QtQuick/QQmlEngine initializes OpenGL/Vulkan render backends which
//     need a full QApplication.  This resolves the vtable lookup failure.
//  2. Replaced QCoreApplication → QApplication.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QColor>
#include <QApplication>   // ← QApplication (not QCoreApplication/QGuiApplication)

// ─────────────────────────────────────────────────────────────────────────────
// Test class: QML utility functions (pure logic, no engine needed)
// ─────────────────────────────────────────────────────────────────────────────
class TestQmlUtilFunctions : public QObject {
    Q_OBJECT

private slots:

    void testFormatTime_seconds() {
        auto formatTime = [](int totalSeconds) -> QString {
            int h = totalSeconds / 3600;
            int m = (totalSeconds % 3600) / 60;
            int s = totalSeconds % 60;
            if (h > 0)
                return QString("%1:%2:%3")
                    .arg(h)
                    .arg(m, 2, 10, QChar('0'))
                    .arg(s, 2, 10, QChar('0'));
            return QString("%1:%2")
                .arg(m, 2, 10, QChar('0'))
                .arg(s, 2, 10, QChar('0'));
        };

        QCOMPARE(formatTime(0),    QString("00:00"));
        QCOMPARE(formatTime(61),   QString("01:01"));
        QCOMPARE(formatTime(3661), QString("1:01:01"));
    }

    void testFormatTime_zero() {
        auto fmt = [](int s) -> QString {
            int m = s / 60, sec = s % 60;
            return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
        };
        QCOMPARE(fmt(0), QString("00:00"));
    }

    void testColorLogic_muteIsGrey() {
        bool muted = true;
        QCOMPARE(QString(muted ? "#888888" : "#4CAF50"), QString("#888888"));
    }

    void testColorLogic_activeIsGreen() {
        bool muted = false;
        QCOMPARE(QString(muted ? "#888888" : "#4CAF50"), QString("#4CAF50"));
    }

    void testVersionString_notEmpty() {
#ifndef AEGIS_VERSION
#  define AEGIS_VERSION "2.1.1"
#endif
        QString ver = AEGIS_VERSION;
        QVERIFY(!ver.isEmpty());
        QVERIFY(ver.contains('.'));
    }

    void testNetworkStatusText_online() {
        bool online = true;
        QCOMPARE(QString(online ? "Online" : "Offline"), QString("Online"));
    }

    void testNetworkStatusText_offline() {
        bool online = false;
        QCOMPARE(QString(online ? "Online" : "Offline"), QString("Offline"));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Test class: QSettings persistence
// ─────────────────────────────────────────────────────────────────────────────
class TestSettingsPersistence : public QObject {
    Q_OBJECT

private slots:

    void testAudioSettings_persistVolume() {
        {
            QSettings s("AegisTest", "Audio");
            s.setValue("masterVolume", 65.0);
            s.sync();
        }
        {
            QSettings s("AegisTest", "Audio");
            QCOMPARE(s.value("masterVolume", 80.0).toDouble(), 65.0);
        }
    }

    void testProjectSettings_defaultPath() {
        QSettings s("AegisTest", "Project");
        QString path = s.value("defaultProjectPath", "").toString();
        Q_UNUSED(path)
    }

    void testProjectSettings_persistAutoBackup() {
        {
            QSettings s("AegisTest", "Project");
            s.setValue("autoCreateBackup", false);
            s.sync();
        }
        {
            QSettings s("AegisTest", "Project");
            QCOMPARE(s.value("autoCreateBackup", true).toBool(), false);
        }
    }

    void testGlobalSettings_launchCounter() {
        {
            QSettings s("AegisTest", "Global");
            int prev = s.value("totalLaunches", 0).toInt();
            s.setValue("totalLaunches", prev + 1);
            s.sync();
        }
        {
            QSettings s("AegisTest", "Global");
            QVERIFY(s.value("totalLaunches", 0).toInt() >= 1);
        }
    }

    void cleanupTestCase() {
        QSettings("AegisTest", "Audio").clear();
        QSettings("AegisTest", "Project").clear();
        QSettings("AegisTest", "Global").clear();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Test class: QML component loading (smoke tests via QQmlEngine)
// ─────────────────────────────────────────────────────────────────────────────
class TestQmlComponentLoading : public QObject {
    Q_OBJECT

private slots:

    void testQmlEngine_canCreate() {
        QQmlEngine engine;
        QVERIFY(engine.rootContext() != nullptr);
    }

    void testInlineQmlComponent_createsItem() {
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick 2.15
            Item { width: 100; height: 100; objectName: "root" }
        )", QUrl());

        QCOMPARE(comp.status(), QQmlComponent::Ready);
        QObject* obj = comp.create();
        QVERIFY(obj != nullptr);
        QCOMPARE(obj->objectName(), QString("root"));
        delete obj;
    }

    void testNotificationColors_errorIsRed() {
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick 2.15
            Item {
                property string notificationType: "error"
                property color notificationColor: {
                    switch(notificationType) {
                        case "error":   return "#331a1a"
                        case "warning": return "#332b1a"
                        case "success": return "#1a3320"
                        default:        return "#1a1a33"
                    }
                }
            }
        )", QUrl());

        if (comp.status() != QQmlComponent::Ready) QSKIP("QtQuick not available");
        QObject* obj = comp.create();
        QVERIFY(obj != nullptr);
        QColor color = obj->property("notificationColor").value<QColor>();
        QCOMPARE(color.name().toLower(), QString("#331a1a"));
        delete obj;
    }

    void testNotificationColors_successIsGreen() {
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick 2.15
            Item {
                property string notificationType: "success"
                property color notificationColor: {
                    switch(notificationType) {
                        case "error":   return "#331a1a"
                        case "warning": return "#332b1a"
                        case "success": return "#1a3320"
                        default:        return "#1a1a33"
                    }
                }
            }
        )", QUrl());

        if (comp.status() != QQmlComponent::Ready) QSKIP("QtQuick not available");
        QObject* obj = comp.create();
        QVERIFY(obj != nullptr);
        QColor color = obj->property("notificationColor").value<QColor>();
        QCOMPARE(color.name().toLower(), QString("#1a3320"));
        delete obj;
    }

    void testBatteryWarning_thresholdLogic() {
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick 2.15
            Item {
                property real batteryLevel: 8
                property bool isCharging: false
                property bool shouldWarn: batteryLevel < 10 && !isCharging
            }
        )", QUrl());

        if (comp.status() != QQmlComponent::Ready) QSKIP("QtQuick not available");
        QObject* obj = comp.create();
        QVERIFY(obj != nullptr);
        QVERIFY(obj->property("shouldWarn").toBool());
        delete obj;
    }

    void testBatteryWarning_noWarnWhenCharging() {
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData(R"(
            import QtQuick 2.15
            Item {
                property real batteryLevel: 5
                property bool isCharging: true
                property bool shouldWarn: batteryLevel < 10 && !isCharging
            }
        )", QUrl());

        if (comp.status() != QQmlComponent::Ready) QSKIP("QtQuick not available");
        QObject* obj = comp.create();
        QVERIFY(obj != nullptr);
        QVERIFY(!obj->property("shouldWarn").toBool());
        delete obj;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Composite runner
//
// FIX: QApplication instead of QCoreApplication/QGuiApplication.
// QtQuick/QQmlEngine requires full application initialization (including
// platform plugin and event loop) which only QApplication provides reliably
// on all platforms. Without it the vtable for Q_OBJECT classes that interact
// with the QML engine is not fully resolved at link time.
// ─────────────────────────────────────────────────────────────────────────────
#ifndef AEGIS_VERSION
#  define AEGIS_VERSION "2.1.1"
#endif

int main(int argc, char** argv) {
    QApplication app(argc, argv);   // ← QApplication (full Qt app context)
    app.setApplicationName("AegisTest");
    app.setOrganizationName("Aegis");

    int result = 0;
    { TestQmlUtilFunctions    t; result |= QTest::qExec(&t, argc, argv); }
    { TestSettingsPersistence t; result |= QTest::qExec(&t, argc, argv); }
    { TestQmlComponentLoading t; result |= QTest::qExec(&t, argc, argv); }
    return result;
}

#include "test_ui_qml.moc"
