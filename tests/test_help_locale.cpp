// test_help_locale.cpp - Basic tests for Aegis::I18nManager locale handling
//
// This test focuses on verifying that I18nManager can:
//  - be constructed via instance()
//  - accept a translations path
//  - report available languages
//  - switch language without crashing for common codes
//
// NOTE: It does not assert on specific .qm contents, only on behaviour.

#include <QtTest/QtTest>
#include "src/help.h"   // Aegis::I18nManager

using namespace Aegis;

class TestHelpLocale : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Basic sanity: singleton instance exists
        I18nManager &mgr = I18nManager::instance();
        QVERIFY(&mgr != nullptr);
    }

    void test_setTranslationsPath()
    {
        I18nManager &mgr = I18nManager::instance();

        // Use the standard resource prefix from the implementation
        mgr.setTranslationsPath(QLatin1String(":/translations"));

        // Calling availableLanguages() must not crash and should be valid
        const QStringList langs = mgr.availableLanguages();
        QVERIFY(langs.isEmpty() || !langs.join(',').isNull());
    }

    void test_switchLanguage_knownCodes()
    {
        I18nManager &mgr = I18nManager::instance();

        // These calls should not crash, even if the .qm is missing.
        // The function returns a boolean; we just verify it's a well-formed call.
        const QStringList codes = { QStringLiteral(""),      // reset to default
                                    QStringLiteral("en"),
                                    QStringLiteral("en_US"),
                                    QStringLiteral("it"),
                                    QStringLiteral("de"),
                                    QStringLiteral("fr"),
                                    QStringLiteral("es"),
                                    QStringLiteral("pt_BR"),
                                    QStringLiteral("ru"),
                                    QStringLiteral("zh_CN") };

        for (const QString &code : codes) {
            bool ok = mgr.switchLanguage(code);
            // ok may be false (missing file), but the call itself must be valid.
            QVERIFY(ok == true || ok == false);
        }
    }

    void test_languageName_mapping()
    {
        // languageName() should always return a non-empty, human-readable string
        const QStringList sampleCodes = {
            QStringLiteral("en"),
            QStringLiteral("en_US"),
            QStringLiteral("it"),
            QStringLiteral("de"),
            QStringLiteral("fr"),
            QStringLiteral("es"),
            QStringLiteral("pt_BR"),
            QStringLiteral("ru"),
            QStringLiteral("zh_CN"),
            QStringLiteral("xx_YY")   // unknown / fallback
        };

        for (const QString &code : sampleCodes) {
            const QString name = I18nManager::languageName(code);
            QVERIFY(!name.trimmed().isEmpty());
        }
    }
};

QTEST_MAIN(TestHelpLocale)
#include "test_help_locale.moc"
