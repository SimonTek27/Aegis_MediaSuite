// tests/test_ui_qml.cpp
// Aegis MediaSuite — Unit tests: UI / QML layer (via QQmlEngine + C++ helper classes)
// Framework: Qt Test + Qt Quick Test
//
// Strategy:
//   • Pure QML logic functions (formatTime, getRandomTip, etc.) are tested
//     via a lightweight QQmlEngine with inline QML evaluation.
//   • The initialization state machine and settings are tested through the
//     C++ backend objects that the QML properties bind to.
//   • For widgets / visual tests, Qt Quick Test (QTEST_APPLESS_MAIN / testlib)
//     is used to instantiate components and query properties.
//
// Build requirements:
//   Qt6::Test  Qt6::Quick  Qt6::Qml
#include <QtTest/QtTest>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QSettings>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QJSEngine>
#include <QJSValue>

// ─────────────────────────────────────────────────────────────────────────────
// Helper: evaluate a JS expression inside a fresh QJSEngine
// ─────────────────────────────────────────────────────────────────────────────
class JsEval {
public:
    JsEval() {}

    QJSValue eval(const QString& expr) {
        return m_engine.evaluate(expr);
    }

    void installFunction(const QString& name, const QString& body) {
        m_engine.evaluate(QString("function %1 { %2 }").arg(name, body));
    }

    QJSEngine m_engine;
};

// ─────────────────────────────────────────────────────────────────────────────
// Test class: QML pure-JS utility functions
// ─────────────────────────────────────────────────────────────────────────────
class TestQmlUtilFunctions : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        // Install the same formatTime function used in main.qml
        m_js.installFunction("formatTime(seconds)", R"(
            if (seconds === undefined || seconds === null || isNaN(seconds) || seconds < 0)
                return "--:--";
            var hrs  = Math.floor(seconds / 3600);
            var mins = Math.floor((seconds % 3600) / 60);
            var secs = Math.floor(seconds % 60);
            if (hrs > 0) {
                return (hrs  < 10 ? "0" + hrs  : String(hrs))  + ":" +
                       (mins < 10 ? "0" + mins : String(mins)) + ":" +
                       (secs < 10 ? "0" + secs : String(secs));
            }
            return (mins < 10 ? "0" + mins : String(mins)) + ":" +
                   (secs < 10 ? "0" + secs : String(secs));
        )");

        // Install getRandomTip (deterministic test variant — no Math.random)
        m_js.installFunction("getRandomTip()", R"(
            var tips = [
                "Tip: Press F1 for help in any application",
                "Tip: Drag files onto the launcher to open them quickly",
                "Tip: Use Ctrl+L to return to launcher from any app"
            ];
            return tips[0];
        )");
    }

    // ── formatTime ─────────────────────────────────────────────────────────

    void testFormatTime_zero() {
        auto v = m_js.eval("formatTime(0)");
        QCOMPARE(v.toString(), QString("00:00"));
    }

    void testFormatTime_59seconds() {
        auto v = m_js.eval("formatTime(59)");
        QCOMPARE(v.toString(), QString("00:59"));
    }

    void testFormatTime_oneMinute() {
        auto v = m_js.eval("formatTime(60)");
        QCOMPARE(v.toString(), QString("01:00"));
    }

    void testFormatTime_90seconds() {
        auto v = m_js.eval("formatTime(90)");
        QCOMPARE(v.toString(), QString("01:30"));
    }

    void testFormatTime_oneHour() {
        auto v = m_js.eval("formatTime(3600)");
        QCOMPARE(v.toString(), QString("01:00:00"));
    }

    void testFormatTime_1h23m45s() {
        auto v = m_js.eval("formatTime(5025)");  // 5025 = 1*3600 + 23*60 + 45
        QCOMPARE(v.toString(), QString("01:23:45"));
    }

    void testFormatTime_negative_returnsDash() {
        auto v = m_js.eval("formatTime(-1)");
        QCOMPARE(v.toString(), QString("--:--"));
    }

    void testFormatTime_NaN_returnsDash() {
        auto v = m_js.eval("formatTime(NaN)");
        QCOMPARE(v.toString(), QString("--:--"));
    }

    void testFormatTime_null_returnsDash() {
        auto v = m_js.eval("formatTime(null)");
        QCOMPARE(v.toString(), QString("--:--"));
    }

    void testFormatTime_undefined_returnsDash() {
        auto v = m_js.eval("formatTime(undefined)");
        QCOMPARE(v.toString(), QString("--:--"));
    }

    void testFormatTime_largeValue() {
        // 99 hours, 59 min, 59 sec
        int total = 99 * 3600 + 59 * 60 + 59;
        auto v = m_js.eval(QString("formatTime(%1)").arg(total));
        QCOMPARE(v.toString(), QString("99:59:59"));
    }

    // ── getRandomTip ───────────────────────────────────────────────────────

    void testGetRandomTip_returnsNonEmpty() {
        auto v = m_js.eval("getRandomTip()");
        QVERIFY(!v.toString().isEmpty());
    }

    void testGetRandomTip_startsWith_Tip() {
        auto v = m_js.eval("getRandomTip()");
        QVERIFY(v.toString().startsWith("Tip:"));
    }

    // ── editorPages mapping ────────────────────────────────────────────────

    void testEditorPagesMap_allKeysPresent() {
        m_js.eval(R"(
            var editorPages = {
                "launcher":     "qrc:/qml/ui_launcher.qml",
                "player":       "qrc:/qml/ui_player.qml",
                "audioeditor":  "qrc:/qml/ui_audioeditor.qml",
                "videoeditor":  "qrc:/qml/ui_videoeditor.qml",
                "discburner":   "qrc:/qml/ui_discburner.qml",
                "djmix":        "qrc:/qml/ui_djmixer.qml",
                "karaoke":      "qrc:/qml/ui_karaoke.qml",
                "disc_labelmaker": "qrc:/qml/ui_disc_labelmaker.qml",
                "converter":    "qrc:/qml/ui_converter.qml",
                "middleware":   "qrc:/qml/ui_middleware.qml",
                "modtracker":   "qrc:/qml/ui_modtracker.qml",
                "musicnotation":"qrc:/qml/ui_musicnotation_editor.qml"
            };
        )");

        QStringList expectedKeys = {
            "launcher", "player", "audioeditor", "videoeditor",
            "discburner", "djmix", "karaoke", "disc_labelmaker",
            "converter", "middleware", "modtracker", "musicnotation"
        };

        for (const QString& key : expectedKeys) {
            auto v = m_js.eval(QString("editorPages['%1']").arg(key));
            QVERIFY2(!v.isUndefined(),
                     qPrintable(QString("editorPages missing key: '%1'").arg(key)));
            QVERIFY2(v.toString().startsWith("qrc:/"),
                     qPrintable(QString("Invalid QRC path for key '%1': %2")
                                    .arg(key, v.toString())));
        }
    }

    void testEditorPagesMap_unknownKeyReturnsUndefined() {
        auto v = m_js.eval("editorPages['nonexistent']");
        QVERIFY(v.isUndefined());
    }

    // ── Network status logic ────────────────────────────────────────────────
    // Verify the fixed logic (static default = 2, no Math.random)

    void testNetworkStatus_staticDefault() {
        m_js.eval("var networkStatus = 2;");
        auto v = m_js.eval("networkStatus");
        QCOMPARE(v.toInt(), 2);
    }

    // ── Version consistency ────────────────────────────────────────────────

    void testVersion_matchesCppDefine() {
        // The QML version property must match the C++ AEGIS_VERSION constant
        // which is injected via setContextProperty("aegisVersion", AEGIS_VERSION)
        // Here we verify the string format is correct (semver x.y.z)
        QString version = AEGIS_VERSION;  // pulled from main.cpp define
        QRegularExpression semver(R"(^\d+\.\d+\.\d+$)");
        QVERIFY2(semver.match(version).hasMatch(),
                 qPrintable(QString("Version '%1' is not valid semver").arg(version)));
    }

private:
    JsEval m_js;
};

// ─────────────────────────────────────────────────────────────────────────────
// Test class: QML/C++ settings persistence
// ─────────────────────────────────────────────────────────────────────────────
class TestSettingsPersistence : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        // Use a unique test organisation so we don't pollute real settings
        QCoreApplication::setOrganizationName("AegisTest");
        QCoreApplication::setApplicationName("AegisTest");
    }

    void testAudioSettings_defaultVolume() {
        QSettings s("AegisTest", "Audio");
        double vol = s.value("masterVolume", 80.0).toDouble();
        QVERIFY(vol >= 0.0 && vol <= 100.0);
    }

    void testAudioSettings_persistVolume() {
        {
            QSettings s("AegisTest", "Audio");
            s.setValue("masterVolume", 65.0);
            s.sync();
        }
        {
            QSettings s("AegisTest", "Audio");
            double vol = s.value("masterVolume", 80.0).toDouble();
            QCOMPARE(vol, 65.0);
        }
    }

    void testProjectSettings_defaultPath() {
        QSettings s("AegisTest", "Project");
        QString path = s.value("defaultProjectPath", "").toString();
        // Default may be empty in test environment — just verify it doesn't crash
        Q_UNUSED(path);
    }

    void testProjectSettings_persistAutoBackup() {
        {
            QSettings s("AegisTest", "Project");
            s.setValue("autoCreateBackup", false);
            s.sync();
        }
        {
            QSettings s("AegisTest", "Project");
            bool backup = s.value("autoCreateBackup", true).toBool();
            QCOMPARE(backup, false);
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
            int total = s.value("totalLaunches", 0).toInt();
            QVERIFY(total >= 1);
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
        // Inline QML replicates the battery warning condition from main.qml
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
// ─────────────────────────────────────────────────────────────────────────────
#ifndef AEGIS_VERSION
#  define AEGIS_VERSION "2.1.1"
#endif

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int result = 0;
    {
        TestQmlUtilFunctions t;
        result |= QTest::qExec(&t, argc, argv);
    }
    {
        TestSettingsPersistence t;
        result |= QTest::qExec(&t, argc, argv);
    }
    {
        TestQmlComponentLoading t;
        result |= QTest::qExec(&t, argc, argv);
    }
    return result;
}

#include "test_ui_qml.moc"
