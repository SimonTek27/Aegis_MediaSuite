// help.h - KDE-style Help System for Aegis Media Suite
//
// Provides KAboutData integration, KHelpMenu-compatible dialogs,
// and a full context-sensitive help browser using Qt's Assistant protocol.
// Falls back gracefully when KDE Frameworks are not available.

#pragma once

#include <QObject>
#include <QDialog>
#include <QUrl>
#include <QString>
#include <QStringList>
#include <QTranslator>

// Forward declarations
class QWidget;
class QAction;
class QMenu;

namespace Aegis {

// ============================================================================
// AboutData — KAboutData-compatible metadata
// ============================================================================

/**
 * @brief Application metadata, mirroring KAboutData's interface.
 *
 * Populated once at startup and passed to AegisHelpMenu and AegisAboutDialog.
 */
struct AboutData {
    QString componentName;      ///< Internal name, e.g. "aegis"
    QString displayName;        ///< Shown to users, e.g. "Aegis Media Suite"
    QString version;            ///< SemVer string, e.g. "2.1.1"
    QString shortDescription;   ///< One-line description
    QString licenseText;        ///< SPDX identifier, e.g. "GPL-3.0-or-later"
    QString copyrightStatement; ///< e.g. "© 2024–2026 Aegis Contributors"
    QString homepageUrl;        ///< Project website
    QString bugAddress;         ///< Bug-tracker URL or mailto:

    struct AuthorInfo {
        QString name;
        QString task;    ///< Role / contribution
        QString email;
        QString webUrl;
    };
    QList<AuthorInfo> authors;
    QList<AuthorInfo> credits;  ///< Additional contributors

    /// Build a default AboutData for Aegis Media Suite.
    static AboutData defaultData();
};

// ============================================================================
// AegisAboutDialog — About dialog in KDE style
// ============================================================================

/**
 * @brief Modal "About" dialog that mimics KAboutDialog.
 *
 * Shows application logo, version, description, authors and license.
 * All strings are wrapped in tr() for i18n.
 */
class AegisAboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AegisAboutDialog(const AboutData& data,
                              QWidget* parent = nullptr);
    ~AegisAboutDialog() override = default;

private:
    void setupUi(const AboutData& data);
};

// ============================================================================
// AegisHelpMenu — Drop-in menu identical to KHelpMenu
// ============================================================================

/**
 * @brief Provides a "Help" QMenu in KDE style.
 *
 * Call menu() to obtain a fully populated QMenu that can be appended
 * to any QMenuBar.  Actions emitted:
 *
 *   - showHelpContents()   → opens the user handbook (HTML or Qt Assistant)
 *   - showWhatsThis()      → activates Qt's WhatsThis mode
 *   - reportBug()          → opens the bug-tracker URL
 *   - showAboutApplication() → opens AegisAboutDialog
 *   - showAboutQt()        → opens QMessageBox::aboutQt
 *   - switchLanguage()     → opens LanguageDialog
 */
class AegisHelpMenu : public QObject {
    Q_OBJECT
public:
    explicit AegisHelpMenu(QWidget* parent,
                           const AboutData& aboutData);
    ~AegisHelpMenu() override = default;

    /// Returns the fully populated Help QMenu. Ownership stays with this object.
    QMenu* menu() const;

    /// Convenience: return individual actions for toolbar use.
    QAction* actionHelpContents() const;
    QAction* actionWhatsThis()    const;
    QAction* actionReportBug()    const;
    QAction* actionAboutApp()     const;
    QAction* actionAboutQt()      const;
    QAction* actionSwitchLanguage() const;

public slots:
    void showHelpContents();
    void showWhatsThis();
    void reportBug();
    void showAboutApplication();
    void showAboutQt();
    void switchLanguage();

signals:
    void helpContentsRequested();

private:
    void createActions();
    void createMenu();

    QWidget*   m_parent      {nullptr};
    AboutData  m_aboutData;
    QMenu*     m_menu        {nullptr};
    QAction*   m_helpAct     {nullptr};
    QAction*   m_whatsThisAct{nullptr};
    QAction*   m_bugAct      {nullptr};
    QAction*   m_aboutAppAct {nullptr};
    QAction*   m_aboutQtAct  {nullptr};
    QAction*   m_langAct     {nullptr};
};

// ============================================================================
// LanguageDialog — runtime language switcher
// ============================================================================

/**
 * @brief Simple dialog that lists available UI translations.
 *
 * On accept it installs the chosen QTranslator and signals the
 * application to retranslate all visible strings via
 * QEvent::LanguageChange.
 */
class LanguageDialog : public QDialog {
    Q_OBJECT
public:
    explicit LanguageDialog(QWidget* parent = nullptr);
    ~LanguageDialog() override = default;

    /// Language code selected by the user (e.g. "it", "de", "fr").
    QString selectedLanguage() const;

signals:
    /// Emitted with the BCP-47 language tag when the user accepts.
    void languageSelected(const QString& languageCode);

private slots:
    void onAccepted();

private:
    void loadAvailableLanguages();
    QString m_selectedLanguage;
};

// ============================================================================
// I18nManager — translation loader & language switcher
// ============================================================================

/**
 * @brief Singleton that loads/unloads QTranslators at runtime.
 *
 * Usage:
 * @code
 *   auto& i18n = I18nManager::instance();
 *   i18n.setTranslationsPath(":/translations");
 *   i18n.switchLanguage("it");          // loads aegis_it.qm + qt_it.qm
 * @endcode
 *
 * Translation files must follow the naming convention:
 *   aegis_<lang>.qm   (application strings)
 *   qt_<lang>.qm      (Qt base translations)
 */
class I18nManager : public QObject {
    Q_OBJECT
public:
    static I18nManager& instance();

    /// Set the directory (or Qt resource prefix) that contains .qm files.
    void setTranslationsPath(const QString& path);

    /// Load and install translations for @p languageCode (e.g. "it").
    /// Pass an empty string to restore the default (English) strings.
    bool switchLanguage(const QString& languageCode);

    /// Currently active language code.
    QString currentLanguage() const;

    /// List of available language codes derived from installed .qm files.
    QStringList availableLanguages() const;

    /// Human-readable name for a language code.
    static QString languageName(const QString& code);

signals:
    void languageChanged(const QString& newLanguage);

private:
    explicit I18nManager(QObject* parent = nullptr);

    QString m_translationsPath { ":/translations" };
    QString m_currentLanguage;
    QTranslator* m_appTranslator { nullptr };
    QTranslator* m_qtTranslator  { nullptr };
};

} // namespace Aegis
