// help.cpp - KDE-style Help System implementation for Aegis Media Suite
//
// Provides:
//   • AegisAboutDialog  — polished About dialog (KAboutDialog look-alike)
//   • AegisHelpMenu     — Help menu (KHelpMenu look-alike)
//   • LanguageDialog    — runtime language switcher
//   • I18nManager       — QTranslator lifecycle manager

#include "help.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTranslator>
#include <QVBoxLayout>
#include <QWhatsThis>
#include <QLocale>
#include <QDir>
#include <QLibraryInfo>
#include <QSvgRenderer>
#include <QPainter>
#include <QStandardPaths>

namespace Aegis {

// ============================================================================
// AboutData
// ============================================================================

AboutData AboutData::defaultData() {
    AboutData d;
    d.componentName      = QStringLiteral("aegis");
    d.displayName        = QObject::tr("Aegis Media Suite");
    d.version            = QStringLiteral("2.1.1");
    d.shortDescription   = QObject::tr(
        "A unified multimedia workstation — player, editor, DJ mixer, "
        "streaming client and disc burner in one Qt6/QML application.");
    d.licenseText        = QStringLiteral("GPL-3.0-or-later");
    d.copyrightStatement = QObject::tr("© 2024–2026 Aegis Project Contributors");
    d.homepageUrl        = QStringLiteral("https://aegis.media");
    d.bugAddress         = QStringLiteral("https://github.com/aegis-media/aegis/issues");

    d.authors = {
        { QObject::tr("Aegis Core Team"),
          QObject::tr("Lead development"),
          QStringLiteral("dev@aegis.media"),
          QStringLiteral("https://aegis.media") }
    };

    d.credits = {
        { QObject::tr("Qt Project"),
          QObject::tr("Qt6 framework"),
          QString(),
          QStringLiteral("https://www.qt.io") },
        { QObject::tr("FFmpeg Contributors"),
          QObject::tr("Media decoding/encoding"),
          QString(),
          QStringLiteral("https://ffmpeg.org") },
        { QObject::tr("MPV Project"),
          QObject::tr("Playback backend"),
          QString(),
          QStringLiteral("https://mpv.io") },
        { QObject::tr("KDE Community"),
          QObject::tr("KDE Frameworks"),
          QString(),
          QStringLiteral("https://kde.org") }
    };

    return d;
}

// ============================================================================
// AegisAboutDialog
// ============================================================================

AegisAboutDialog::AegisAboutDialog(const AboutData& data, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About %1").arg(data.displayName));
    setMinimumSize(540, 420);
    setMaximumSize(640, 520);
    setupUi(data);
}

void AegisAboutDialog::setupUi(const AboutData& data)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setSpacing(0);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // ── Header bar ──────────────────────────────────────────────────────────
    auto* headerWidget = new QWidget(this);
    headerWidget->setStyleSheet(
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #1a1a1a, stop:1 #2a1500);"
    );
    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(20, 16, 20, 16);

    // App icon (SVG rendered to 64×64)
    auto* iconLabel = new QLabel(headerWidget);
    QSvgRenderer renderer(QStringLiteral(":/assets/icons/app_icon.svg"));
    if (renderer.isValid()) {
        QPixmap pm(64, 64);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        renderer.render(&p);
        iconLabel->setPixmap(pm);
    } else {
        iconLabel->setText(QStringLiteral("🎵"));
        iconLabel->setStyleSheet("font-size: 48px;");
    }
    iconLabel->setFixedSize(64, 64);
    headerLayout->addWidget(iconLabel);

    auto* titleLayout = new QVBoxLayout;
    titleLayout->setSpacing(4);

    auto* nameLabel = new QLabel(data.displayName, headerWidget);
    nameLabel->setStyleSheet(
        "color: #ff8c00; font-size: 22px; font-weight: bold;"
        "font-family: 'JetBrains Mono', monospace;");
    titleLayout->addWidget(nameLabel);

    auto* versionLabel = new QLabel(
        tr("Version %1").arg(data.version), headerWidget);
    versionLabel->setStyleSheet("color: #888; font-size: 12px;");
    titleLayout->addWidget(versionLabel);

    auto* descLabel = new QLabel(data.shortDescription, headerWidget);
    descLabel->setStyleSheet("color: #aaa; font-size: 11px;");
    descLabel->setWordWrap(true);
    titleLayout->addWidget(descLabel);

    headerLayout->addLayout(titleLayout, 1);
    outerLayout->addWidget(headerWidget);

    // ── Tab widget ───────────────────────────────────────────────────────────
    auto* tabs = new QTabWidget(this);
    tabs->setStyleSheet(
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab { background: #1a1a1a; color: #888;"
        "  padding: 6px 14px; border: 1px solid #333; margin-right:2px; }"
        "QTabBar::tab:selected { background: #2a2a2a; color: #ff8c00;"
        "  border-bottom: 2px solid #ff8c00; }"
    );

    // — About tab
    auto* aboutWidget = new QWidget;
    auto* aboutLayout = new QVBoxLayout(aboutWidget);
    aboutLayout->setContentsMargins(16, 12, 16, 12);

    auto* copyrightLbl = new QLabel(data.copyrightStatement, aboutWidget);
    copyrightLbl->setStyleSheet("color: #ccc; font-size: 12px;");
    aboutLayout->addWidget(copyrightLbl);

    auto* homepageBtn = new QPushButton(
        tr("🌐  %1").arg(data.homepageUrl), aboutWidget);
    homepageBtn->setFlat(true);
    homepageBtn->setStyleSheet(
        "QPushButton { color: #4499ff; text-align: left; padding: 4px; }"
        "QPushButton:hover { color: #77bbff; }");
    connect(homepageBtn, &QPushButton::clicked, this, [url = data.homepageUrl]() {
        QDesktopServices::openUrl(QUrl(url));
    });
    aboutLayout->addWidget(homepageBtn);

    auto* licenseLbl = new QLabel(
        tr("License: %1").arg(data.licenseText), aboutWidget);
    licenseLbl->setStyleSheet("color: #888; font-size: 11px;");
    aboutLayout->addWidget(licenseLbl);

    // Qt version info
    auto* qtInfoLbl = new QLabel(
        tr("Built with Qt %1").arg(QT_VERSION_STR), aboutWidget);
    qtInfoLbl->setStyleSheet("color: #666; font-size: 11px;");
    aboutLayout->addWidget(qtInfoLbl);

    aboutLayout->addStretch();
    tabs->addTab(aboutWidget, tr("About"));

    // — Authors tab
    auto* authorsWidget = new QWidget;
    auto* authorsLayout = new QVBoxLayout(authorsWidget);
    authorsLayout->setContentsMargins(16, 12, 16, 12);

    auto* authorsBrowser = new QTextBrowser(authorsWidget);
    authorsBrowser->setOpenExternalLinks(true);
    authorsBrowser->setStyleSheet(
        "QTextBrowser { background: #111; color: #ccc; border: 1px solid #333;"
        " font-size: 12px; }");

    QString authorsHtml = QStringLiteral("<style>a{color:#4499ff;}"
        "h3{color:#ff8c00;margin:8px 0 4px 0;}</style>");
    authorsHtml += QStringLiteral("<h3>") + tr("Authors") + QStringLiteral("</h3>");
    for (const auto& a : data.authors) {
        authorsHtml += QStringLiteral("<b>") + a.name + QStringLiteral("</b>");
        if (!a.task.isEmpty())
            authorsHtml += QStringLiteral(" — ") + a.task;
        if (!a.email.isEmpty())
            authorsHtml += QStringLiteral(" &lt;<a href='mailto:")
                + a.email + QStringLiteral("'>") + a.email + QStringLiteral("</a>&gt;");
        authorsHtml += QStringLiteral("<br/>");
    }

    if (!data.credits.isEmpty()) {
        authorsHtml += QStringLiteral("<h3>") + tr("Credits") + QStringLiteral("</h3>");
        for (const auto& c : data.credits) {
            authorsHtml += QStringLiteral("<b>");
            if (!c.webUrl.isEmpty())
                authorsHtml += QStringLiteral("<a href='") + c.webUrl
                    + QStringLiteral("'>") + c.name + QStringLiteral("</a>");
            else
                authorsHtml += c.name;
            authorsHtml += QStringLiteral("</b>");
            if (!c.task.isEmpty())
                authorsHtml += QStringLiteral(" — ") + c.task;
            authorsHtml += QStringLiteral("<br/>");
        }
    }

    authorsBrowser->setHtml(authorsHtml);
    authorsLayout->addWidget(authorsBrowser);
    tabs->addTab(authorsWidget, tr("Authors"));

    // — License tab
    auto* licenseWidget = new QWidget;
    auto* licenseLayout = new QVBoxLayout(licenseWidget);
    licenseLayout->setContentsMargins(16, 12, 16, 12);

    auto* licenseBrowser = new QTextBrowser(licenseWidget);
    licenseBrowser->setStyleSheet(
        "QTextBrowser { background: #111; color: #ccc; border: 1px solid #333;"
        " font-family: monospace; font-size: 11px; }");
    licenseBrowser->setText(
        tr("This program is free software: you can redistribute it and/or modify\n"
           "it under the terms of the GNU General Public License as published by\n"
           "the Free Software Foundation, either version 3 of the License, or\n"
           "(at your option) any later version.\n\n"
           "This program is distributed in the hope that it will be useful,\n"
           "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
           "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
           "GNU General Public License for more details.\n\n"
           "You should have received a copy of the GNU General Public License\n"
           "along with this program. If not, see <https://www.gnu.org/licenses/>."));
    licenseLayout->addWidget(licenseBrowser);
    tabs->addTab(licenseWidget, tr("License"));

    outerLayout->addWidget(tabs, 1);

    // ── Button box ───────────────────────────────────────────────────────────
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttonBox->setStyleSheet(
        "QPushButton { background: #333; color: #ccc; border: 1px solid #555;"
        " padding: 5px 16px; border-radius: 4px; }"
        "QPushButton:hover { background: #444; }");
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::accept);
    outerLayout->addWidget(buttonBox);
}

// ============================================================================
// AegisHelpMenu
// ============================================================================

AegisHelpMenu::AegisHelpMenu(QWidget* parent, const AboutData& aboutData)
    : QObject(parent)
    , m_parent(parent)
    , m_aboutData(aboutData)
{
    createActions();
    createMenu();
}

QMenu* AegisHelpMenu::menu() const { return m_menu; }

QAction* AegisHelpMenu::actionHelpContents()   const { return m_helpAct; }
QAction* AegisHelpMenu::actionWhatsThis()      const { return m_whatsThisAct; }
QAction* AegisHelpMenu::actionReportBug()      const { return m_bugAct; }
QAction* AegisHelpMenu::actionAboutApp()       const { return m_aboutAppAct; }
QAction* AegisHelpMenu::actionAboutQt()        const { return m_aboutQtAct; }
QAction* AegisHelpMenu::actionSwitchLanguage() const { return m_langAct; }

void AegisHelpMenu::createActions()
{
    m_helpAct = new QAction(
        QIcon::fromTheme(QStringLiteral("help-contents")),
        tr("%1 Handbook").arg(m_aboutData.displayName), this);
    m_helpAct->setShortcut(QKeySequence::HelpContents);  // F1
    connect(m_helpAct, &QAction::triggered, this, &AegisHelpMenu::showHelpContents);

    m_whatsThisAct = new QAction(
        QIcon::fromTheme(QStringLiteral("help-whatsthis")),
        tr("What's This?"), this);
    m_whatsThisAct->setShortcut(Qt::SHIFT | Qt::Key_F1);
    connect(m_whatsThisAct, &QAction::triggered, this, &AegisHelpMenu::showWhatsThis);

    m_langAct = new QAction(
        QIcon::fromTheme(QStringLiteral("preferences-desktop-locale")),
        tr("Switch Language…"), this);
    connect(m_langAct, &QAction::triggered, this, &AegisHelpMenu::switchLanguage);

    m_bugAct = new QAction(
        QIcon::fromTheme(QStringLiteral("tools-report-bug")),
        tr("Report Bug…"), this);
    connect(m_bugAct, &QAction::triggered, this, &AegisHelpMenu::reportBug);

    m_aboutAppAct = new QAction(
        QIcon::fromTheme(QStringLiteral("help-about")),
        tr("About %1").arg(m_aboutData.displayName), this);
    connect(m_aboutAppAct, &QAction::triggered,
            this, &AegisHelpMenu::showAboutApplication);

    m_aboutQtAct = new QAction(
        QIcon::fromTheme(QStringLiteral("help-about")),
        tr("About Qt"), this);
    connect(m_aboutQtAct, &QAction::triggered, this, &AegisHelpMenu::showAboutQt);
}

void AegisHelpMenu::createMenu()
{
    m_menu = new QMenu(tr("&Help"), m_parent);
    m_menu->addAction(m_helpAct);
    m_menu->addAction(m_whatsThisAct);
    m_menu->addSeparator();
    m_menu->addAction(m_langAct);
    m_menu->addSeparator();
    m_menu->addAction(m_bugAct);
    m_menu->addSeparator();
    m_menu->addAction(m_aboutAppAct);
    m_menu->addAction(m_aboutQtAct);
}

void AegisHelpMenu::showHelpContents()
{
    // Try to open the bundled HTML handbook first, then fall back to the website.
    const QString localDoc = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("doc/HTML/en/aegis/index.html"));

    if (!localDoc.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(localDoc));
    } else {
        QDesktopServices::openUrl(QUrl(
            QStringLiteral("https://aegis.media/docs")));
    }
    emit helpContentsRequested();
}

void AegisHelpMenu::showWhatsThis()
{
    QWhatsThis::enterWhatsThisMode();
}

void AegisHelpMenu::reportBug()
{
    QDesktopServices::openUrl(QUrl(m_aboutData.bugAddress));
}

void AegisHelpMenu::showAboutApplication()
{
    AegisAboutDialog dlg(m_aboutData, m_parent);
    dlg.exec();
}

void AegisHelpMenu::showAboutQt()
{
    QMessageBox::aboutQt(m_parent, tr("About Qt"));
}

void AegisHelpMenu::switchLanguage()
{
    LanguageDialog dlg(m_parent);
    connect(&dlg, &LanguageDialog::languageSelected,
            &I18nManager::instance(), &I18nManager::switchLanguage);
    dlg.exec();
}

// ============================================================================
// LanguageDialog
// ============================================================================

LanguageDialog::LanguageDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Switch Application Language"));
    setMinimumSize(380, 280);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 16);

    auto* infoLabel = new QLabel(
        tr("Select the language for the Aegis user interface.\n"
           "The change takes effect immediately."), this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #aaa; font-size: 12px;");
    layout->addWidget(infoLabel);

    auto* list = new QListWidget(this);
    list->setStyleSheet(
        "QListWidget { background: #1a1a1a; color: #ccc;"
        "  border: 1px solid #333; }"
        "QListWidget::item:selected { background: #ff8c00; color: #000; }");

    // English is always the default entry
    auto* enItem = new QListWidgetItem(
        QIcon::fromTheme(QStringLiteral("flag")),
        tr("English (default)"), list);
    enItem->setData(Qt::UserRole, QStringLiteral("en"));

    // Add languages for which we have .qm files
    const auto& mgr = I18nManager::instance();
    for (const QString& code : mgr.availableLanguages()) {
        if (code == QStringLiteral("en")) continue;
        auto* item = new QListWidgetItem(
            I18nManager::languageName(code), list);
        item->setData(Qt::UserRole, code);
        if (code == mgr.currentLanguage())
            list->setCurrentItem(item);
    }
    if (!list->currentItem())
        list->setCurrentRow(0);

    layout->addWidget(list, 1);

    auto* btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->setStyleSheet(
        "QPushButton { background: #333; color: #ccc; border: 1px solid #555;"
        " padding: 5px 14px; border-radius: 4px; }"
        "QPushButton:hover { background: #444; }");
    layout->addWidget(btnBox);

    connect(btnBox, &QDialogButtonBox::accepted, this, [this, list]() {
        if (list->currentItem())
            m_selectedLanguage = list->currentItem()->data(Qt::UserRole).toString();
        onAccepted();
    });
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString LanguageDialog::selectedLanguage() const { return m_selectedLanguage; }

void LanguageDialog::onAccepted()
{
    if (!m_selectedLanguage.isEmpty())
        emit languageSelected(m_selectedLanguage);
    accept();
}

void LanguageDialog::loadAvailableLanguages()
{
    // Delegated to I18nManager::availableLanguages()
}

// ============================================================================
// I18nManager
// ============================================================================

I18nManager::I18nManager(QObject* parent)
    : QObject(parent)
    , m_appTranslator(new QTranslator(this))
    , m_qtTranslator(new QTranslator(this))
{
    // Detect system locale on first run
    m_currentLanguage = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
}

I18nManager& I18nManager::instance()
{
    static I18nManager inst;
    return inst;
}

void I18nManager::setTranslationsPath(const QString& path)
{
    m_translationsPath = path;
}

bool I18nManager::switchLanguage(const QString& languageCode)
{
    // Remove previously installed translators
    if (qApp) {
        qApp->removeTranslator(m_appTranslator);
        qApp->removeTranslator(m_qtTranslator);
    }

    if (languageCode.isEmpty() || languageCode == QStringLiteral("en")) {
        m_currentLanguage = QStringLiteral("en");
        emit languageChanged(m_currentLanguage);
        return true;
    }

    // Load application translation  (aegis_<lang>.qm)
    const QString appFile = QStringLiteral("aegis_") + languageCode;
    bool appOk = m_appTranslator->load(appFile, m_translationsPath);
    if (!appOk) {
        // Try with full locale, e.g. "it_IT"
        appOk = m_appTranslator->load(
            appFile, QStandardPaths::locate(
                QStandardPaths::GenericDataLocation,
                QStringLiteral("locale"), QStandardPaths::LocateDirectory));
    }

    // Load Qt base translation  (qt_<lang>.qm)
    const QString qtFile = QStringLiteral("qt_") + languageCode;
    bool qtOk = m_qtTranslator->load(qtFile, QLibraryInfo::path(QLibraryInfo::TranslationsPath));

    if (appOk && qApp)
        qApp->installTranslator(m_appTranslator);
    if (qtOk && qApp)
        qApp->installTranslator(m_qtTranslator);

    if (appOk) {
        m_currentLanguage = languageCode;
        emit languageChanged(m_currentLanguage);
        return true;
    }

    qWarning() << "[I18nManager] Translation not found for language:" << languageCode;
    return false;
}

QString I18nManager::currentLanguage() const { return m_currentLanguage; }

QStringList I18nManager::availableLanguages() const
{
    QStringList langs;
    langs << QStringLiteral("en");  // English is always available

    QDir dir(m_translationsPath);
    const QStringList files = dir.entryList(
        { QStringLiteral("aegis_*.qm") }, QDir::Files);
    for (const QString& f : files) {
        // aegis_it.qm  → "it"
        QString code = f;
        code.remove(QStringLiteral("aegis_"));
        code.remove(QStringLiteral(".qm"));
        if (!code.isEmpty() && !langs.contains(code))
            langs << code;
    }
    return langs;
}

QString I18nManager::languageName(const QString& code)
{
    QLocale locale(code);
    if (locale.language() == QLocale::C)
        return code;

    // Native name, capitalised
    QString name = locale.nativeLanguageName();
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return name.isEmpty() ? code : name;
}

} // namespace Aegis
