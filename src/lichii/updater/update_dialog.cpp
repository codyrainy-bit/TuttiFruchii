// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <filesystem>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "common/logging.h"
#include "common/scm_rev.h"
#include "frontend_common/update_installer.h"
#include "qt_common/abstract/frontend.h"
#ifdef _WIN32
#include "qt_common/util/compress.h"
#endif
#include "update_dialog.h"

#include "common/httplib.h"

#ifdef LICHII_BUNDLED_OPENSSL
#include <openssl/cert.h>
#endif

#undef GetSaveFileName

namespace {

// The installer helper TuttiFruchii ships on Windows, and the name it had in Lichii.
constexpr char HelperName[] = "TuttiFruchii-updater.exe";
constexpr char OldHelperName[] = "lichii-updater.exe";

// The prompt keeps its own dark look whatever the theme, like the console's.
const QString PromptStyle = QStringLiteral(R"(
QWidget#UpdateCard {
    background-color: #16171b; border: 1px solid #2a2c31; border-radius: 22px;
}
QLabel#UpdateEyebrow {
    color: #7f9fff; font-size: 12px; font-weight: 700; letter-spacing: 2px; background: transparent;
}
QLabel#UpdateTitle { color: #f2f3f5; font-size: 26px; font-weight: 600; background: transparent; }
QLabel#UpdateSubtitle { color: #9b9da4; font-size: 14px; background: transparent; }
QLabel#UpdateStatus { color: #c4c5ca; font-size: 14px; background: transparent; }
QLabel#UpdateButtonText { font-size: 15px; background: transparent; }
QTextBrowser#UpdateNotes {
    background-color: #101114; color: #c4c5ca; border: 1px solid #222428; border-radius: 14px;
    padding: 12px 14px; font-size: 14px; selection-background-color: #4169e1;
}
QProgressBar#UpdateProgress {
    background-color: #222428; border: 0; border-radius: 5px;
    min-height: 10px; max-height: 10px;
}
QProgressBar#UpdateProgress::chunk {
    border-radius: 5px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4169e1, stop:1 #9db8ff);
}
QPushButton#UpdateButton {
    background-color: #222428; color: #e6e7ea; border: 2px solid transparent;
    border-radius: 24px; min-height: 44px; max-height: 44px; padding: 0;
}
QPushButton#UpdateButton:hover { background-color: #2a2c31; }
QPushButton#UpdateButton:focus { border: 2px solid #ffffff; }
QPushButton#UpdateButton[primary="true"] { background-color: #4169e1; }
QPushButton#UpdateButton[primary="true"]:hover { background-color: #5479ea; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 0; }
QScrollBar::handle:vertical { background: #34363c; border-radius: 4px; min-height: 24px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)");

/// "build-8" as "v8", as releases are named; a local build has no tag.
QString CurrentVersionText() {
    const QString tag = QString::fromLatin1(Common::g_build_release_tag);
    if (tag.startsWith(QStringLiteral("build-"))) {
        return QStringLiteral("v%1").arg(tag.mid(6));
    }
    return tag.isEmpty() ? QObject::tr("a development build") : tag;
}

QString ReadableSize(u64 bytes) {
    if (bytes == 0) {
        return {};
    }
    return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1000.0 * 1000.0), 0, 'f', 1);
}

} // namespace

UpdateDialog::UpdateDialog(const Common::Net::Release& release, QWidget* parent,
                           GameHome::ButtonStyle buttons, bool test)
    : QDialog(parent), m_release(release), m_test(test), m_buttons(buttons) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setWindowTitle(tr("System Update"));
    // GameList sends the controller's buttons to a modal window that asks for them.
    setProperty("controllerNavigation", true);
    setStyleSheet(PromptStyle);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->addStretch(1);
    auto* center = new QHBoxLayout;
    center->addStretch(1);
    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("UpdateCard"));
    m_card->setAttribute(Qt::WA_StyledBackground);
    m_card->setMinimumWidth(520);
    m_card->setMaximumWidth(660);
    auto* shadow = new QGraphicsDropShadowEffect(m_card);
    shadow->setBlurRadius(70);
    shadow->setOffset(0, 18);
    shadow->setColor(QColor(0, 0, 0, 170));
    m_card->setGraphicsEffect(shadow);
    center->addWidget(m_card, 3);
    center->addStretch(1);
    outer->addLayout(center);
    outer->addStretch(1);

    auto* card = new QVBoxLayout(m_card);
    card->setContentsMargins(32, 30, 32, 26);
    card->setSpacing(18);

    auto* header = new QHBoxLayout;
    header->setSpacing(18);
    auto* icon = new QLabel(m_card);
    QIcon app_icon(QStringLiteral(":/img/lichii.ico"));
    if (app_icon.isNull()) {
        app_icon = QGuiApplication::windowIcon();
    }
    icon->setPixmap(app_icon.pixmap(64, 64));
    icon->setFixedSize(64, 64);
    icon->setAttribute(Qt::WA_TranslucentBackground);
    header->addWidget(icon, 0, Qt::AlignTop);
    auto* heading = new QVBoxLayout;
    heading->setSpacing(4);
    m_eyebrow = new QLabel(m_card);
    m_eyebrow->setObjectName(QStringLiteral("UpdateEyebrow"));
    m_title = new QLabel(m_card);
    m_title->setObjectName(QStringLiteral("UpdateTitle"));
    m_title->setWordWrap(true);
    m_subtitle = new QLabel(m_card);
    m_subtitle->setObjectName(QStringLiteral("UpdateSubtitle"));
    m_subtitle->setWordWrap(true);
    heading->addWidget(m_eyebrow);
    heading->addWidget(m_title);
    heading->addWidget(m_subtitle);
    header->addLayout(heading, 1);
    card->addLayout(header);

    m_notes = new QTextBrowser(m_card);
    m_notes->setObjectName(QStringLiteral("UpdateNotes"));
    m_notes->setOpenExternalLinks(true);
    m_notes->setFocusPolicy(Qt::NoFocus);
    m_notes->setMinimumHeight(150);
    m_notes->setMaximumHeight(300);
    card->addWidget(m_notes);

    m_status = new QLabel(m_card);
    m_status->setObjectName(QStringLiteral("UpdateStatus"));
    m_status->setWordWrap(true);
    card->addWidget(m_status);
    m_progress = new QProgressBar(m_card);
    m_progress->setObjectName(QStringLiteral("UpdateProgress"));
    m_progress->setRange(0, 100);
    m_progress->setTextVisible(false);
    card->addWidget(m_progress);

    m_action_bar = new QWidget(m_card);
    auto* actions = new QHBoxLayout(m_action_bar);
    actions->setContentsMargins(0, 6, 0, 0);
    actions->setSpacing(12);
    card->addWidget(m_action_bar);

    // Our releases have one file per platform; on Linux the AppImage when running one.
    const auto assets = release.GetPlatformAssets();
    if (!assets.empty()) {
        m_asset = assets.front();
        for (const auto& asset : assets) {
            if (Common::Net::CurrentLinuxPackage() == Common::Net::LinuxPackage::AppImage &&
                asset.filename.ends_with(".AppImage")) {
                m_asset = asset;
            }
        }
        m_has_asset = true;
    }

    ShowPrompt();
}

UpdateDialog::~UpdateDialog() = default;

Common::Net::Release UpdateDialog::TestRelease() {
    Common::Net::Release release{};
    release.title = "TuttiFruchii v10";
    release.tag = "build-10-preview";
    release.html_url = "https://github.com/codyrainy-bit/TuttiFruchii/releases";
    release.published = static_cast<u64>(QDateTime::currentSecsSinceEpoch());
    release.body =
        "This is a **preview** of the update prompt. Nothing is downloaded or installed.\n\n"
        "## What's new\n"
        "- **Home View**: pick games with a controller, Switch 2 style\n"
        "- Full-size cover and banner art from the LaunchBox Games Database\n"
        "- Button hints for PlayStation, Xbox and Switch controllers\n\n"
        "## Fixes\n"
        "- Covers no longer look soft on large screens\n"
        "- The menu pills are no longer cut off\n";
    release.id = 0;
    release.prerelease = false;
    return release;
}

void UpdateDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // Over the whole window, which it dims.
    if (QWidget* owner = parentWidget() ? parentWidget()->window() : nullptr) {
        resize(owner->size());
        move(owner->mapToGlobal(QPoint(0, 0)));
    }
    for (QPushButton* button : m_action_buttons) {
        if (button->property("primary").toBool()) {
            button->setFocus(Qt::OtherFocusReason);
        }
    }
}

void UpdateDialog::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(5, 6, 8, 205));
}

void UpdateDialog::SetActions(std::vector<Action> actions) {
    for (QPushButton* button : m_action_buttons) {
        // Hidden now: the deletion waits for the event loop this was called from.
        button->hide();
        button->deleteLater();
    }
    m_action_buttons.clear();
    m_actions = std::move(actions);

    auto* layout = static_cast<QHBoxLayout*>(m_action_bar->layout());
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item;
    }
    layout->addStretch(1);
    QPushButton* focus = nullptr;
    for (size_t i = 0; i < m_actions.size(); ++i) {
        const Action& action = m_actions[i];
        auto* button = new QPushButton(m_action_bar);
        button->setObjectName(QStringLiteral("UpdateButton"));
        button->setProperty("primary", action.primary);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setAutoDefault(false);
        auto* inner = new QHBoxLayout(button);
        inner->setContentsMargins(12, 0, 20, 0);
        inner->setSpacing(10);
        QWidget* glyph = GameHome::CreateGlyph(m_buttons, action.role, button);
        auto* text = new QLabel(action.text, button);
        text->setObjectName(QStringLiteral("UpdateButtonText"));
        text->setStyleSheet(action.primary ? QStringLiteral("color: #ffffff; font-weight: 600;")
                                           : QStringLiteral("color: #e6e7ea;"));
        text->setAttribute(Qt::WA_TransparentForMouseEvents);
        inner->addWidget(glyph);
        inner->addWidget(text);
        button->setMinimumWidth(glyph->width() + text->sizeHint().width() + 46);
        connect(button, &QPushButton::clicked, this, [this, i] {
            if (i < m_actions.size() && m_actions[i].run) {
                // The action can replace the buttons: run a copy.
                const auto run = m_actions[i].run;
                run();
            }
        });
        button->installEventFilter(this);
        layout->addWidget(button);
        m_action_buttons.push_back(button);
        if (action.primary || focus == nullptr) {
            focus = button;
        }
    }
    if (focus != nullptr && isVisible()) {
        focus->setFocus(Qt::OtherFocusReason);
    }
}

void UpdateDialog::ShowPrompt() {
    m_busy = false;
    m_cancelled = false;
    m_eyebrow->setText(m_test ? tr("SYSTEM UPDATE  ·  PREVIEW") : tr("SYSTEM UPDATE"));
    m_title->setText(tr("%1 is available").arg(QString::fromStdString(m_release.title)));

    QString subtitle = tr("You have %1").arg(CurrentVersionText());
    if (m_release.published != 0) {
        subtitle += QStringLiteral("   ·   ") +
                    tr("Released %1").arg(QLocale().toString(
                        QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m_release.published))
                            .date(),
                        QStringLiteral("MMM d, yyyy")));
    }
    if (m_has_asset && m_asset.size != 0) {
        subtitle += QStringLiteral("   ·   ") + ReadableSize(m_asset.size);
    }
    m_subtitle->setText(subtitle);

    std::string text{m_release.body};
    if (auto pos = text.find("# Packages"); pos != std::string::npos) {
        text = text.substr(0, pos);
    }
    m_notes->setMarkdown(QString::fromStdString(text));
    m_notes->setVisible(true);
    m_status->setVisible(false);
    m_progress->setVisible(false);

    std::vector<Action> actions;
    actions.push_back({GameHome::HintRole::Options, tr("Skip This Version"),
                       [this] { done(SkipVersion); }});
    actions.push_back({GameHome::HintRole::Back, tr("Later"), [this] { reject(); }});
    if (m_has_asset || m_test) {
        actions.push_back({GameHome::HintRole::Confirm, tr("Update"), [this] { Update(); }, true});
    } else {
        actions.push_back({GameHome::HintRole::Confirm, tr("View on GitHub"),
                           [this] {
                               QDesktopServices::openUrl(
                                   QUrl{QString::fromStdString(m_release.html_url)});
                               accept();
                           },
                           true});
    }
    SetActions(std::move(actions));
}

void UpdateDialog::ShowChangelog(QWidget* parent, GameHome::ButtonStyle buttons) {
    UpdateDialog dialog(Common::Net::Release{}, parent, buttons);
    dialog.ShowNotes();
    dialog.exec();
}

void UpdateDialog::ShowNotes() {
    setWindowTitle(tr("What's New"));
    m_eyebrow->setText(tr("WHAT'S NEW"));
    m_title->setText(tr("TuttiFruchii v%1").arg(ChangelogVersion));
    m_subtitle->setText(tr("Here's what's new and what got fixed in this version."));
    QFile file(QStringLiteral(":/text/changelog.md"));
    if (file.open(QIODevice::ReadOnly)) {
        m_notes->setMarkdown(QString::fromUtf8(file.readAll()));
    }
    m_notes->setMaximumHeight(440);
    m_notes->setMinimumHeight(300);
    m_notes->setVisible(true);
    m_status->setVisible(false);
    m_progress->setVisible(false);
    SetActions({{GameHome::HintRole::Confirm, tr("Got It"), [this] { accept(); }, true}});
}

void UpdateDialog::ShowProgress(const QString& title, const QString& text) {
    m_busy = true;
    m_cancelled = false;
    m_eyebrow->setText(m_test ? tr("SYSTEM UPDATE  ·  PREVIEW") : tr("SYSTEM UPDATE"));
    m_title->setText(title);
    m_subtitle->setText(QString::fromStdString(m_release.title));
    m_notes->setVisible(false);
    m_status->setText(text);
    m_status->setVisible(true);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    SetActions({{GameHome::HintRole::Back, tr("Cancel"), [this] { m_cancelled = true; }}});
}

void UpdateDialog::SetProgress(int percent) {
    m_progress->setValue(std::clamp(percent, 0, 100));
}

void UpdateDialog::ShowDone(const QString& title, const QString& text,
                            std::vector<Action> actions) {
    m_busy = false;
    m_title->setText(title);
    m_status->setText(text);
    m_status->setVisible(true);
    m_progress->setValue(100);
    m_progress->setVisible(true);
    m_notes->setVisible(false);
    SetActions(std::move(actions));
}

void UpdateDialog::ShowError(const QString& title, const QString& text) {
    m_busy = false;
    m_eyebrow->setText(tr("SYSTEM UPDATE"));
    m_title->setText(title);
    m_status->setText(text);
    m_status->setVisible(true);
    m_progress->setVisible(false);
    m_notes->setVisible(false);
    SetActions({{GameHome::HintRole::Back, tr("Back"), [this] { ShowPrompt(); }},
                {GameHome::HintRole::Confirm, tr("Close"), [this] { reject(); }, true}});
}

bool UpdateDialog::eventFilter(QObject* object, QEvent* event) {
    // The focused button gets the keys first: arrows, B and X act on the whole prompt.
    if (event->type() == QEvent::KeyPress &&
        std::ranges::find(m_action_buttons, object) != m_action_buttons.end()) {
        auto* key_event = static_cast<QKeyEvent*>(event);
        switch (key_event->key()) {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Escape:
        case Qt::Key_Backspace:
        case Qt::Key_Menu:
        case Qt::Key_X:
        case Qt::Key_Plus:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            keyPressEvent(key_event);
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(object, event);
}

void UpdateDialog::keyPressEvent(QKeyEvent* event) {
    const int key = event->key();
    const auto find_action = [this](GameHome::HintRole role) -> const Action* {
        for (const Action& action : m_actions) {
            if (action.role == role) {
                return &action;
            }
        }
        return nullptr;
    };
    const auto run = [](const Action* action) {
        if (action != nullptr && action->run) {
            const auto copy = action->run;
            copy();
        }
    };

    if (key == Qt::Key_Left || key == Qt::Key_Right) {
        const auto it = std::find(m_action_buttons.begin(), m_action_buttons.end(),
                                  focusWidget());
        int index = it == m_action_buttons.end() ? 0
                                                 : static_cast<int>(it - m_action_buttons.begin());
        index += key == Qt::Key_Left ? -1 : 1;
        index = std::clamp(index, 0, static_cast<int>(m_action_buttons.size()) - 1);
        if (!m_action_buttons.empty()) {
            m_action_buttons[static_cast<size_t>(index)]->setFocus(Qt::OtherFocusReason);
        }
    } else if (key == Qt::Key_Up || key == Qt::Key_Down) {
        QScrollBar* bar = m_notes->verticalScrollBar();
        bar->setValue(bar->value() + (key == Qt::Key_Up ? -1 : 1) * bar->singleStep() * 3);
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) {
        if (auto* button = qobject_cast<QPushButton*>(focusWidget());
            button != nullptr && std::ranges::find(m_action_buttons, button) !=
                                     m_action_buttons.end()) {
            button->click();
        } else {
            run(find_action(GameHome::HintRole::Confirm));
        }
    } else if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
        if (const Action* back = find_action(GameHome::HintRole::Back)) {
            run(back);
        } else if (!m_busy) {
            reject();
        }
    } else if (key == Qt::Key_Menu || key == Qt::Key_X || key == Qt::Key_Plus) {
        run(find_action(GameHome::HintRole::Options));
    } else {
        QDialog::keyPressEvent(event);
        return;
    }
    event->accept();
}

void UpdateDialog::FakeUpdate() {
    ShowProgress(tr("Downloading the update"),
                 tr("Preview: nothing is downloaded. %1").arg(QStringLiteral("158.5 MB")));
    auto* timer = new QTimer(this);
    timer->setInterval(30);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        if (m_cancelled) {
            timer->deleteLater();
            ShowPrompt();
            return;
        }
        const int value = m_progress->value() + 1;
        SetProgress(value);
        m_status->setText(tr("Preview: nothing is downloaded.   %1%").arg(value));
        if (value < 100) {
            return;
        }
        timer->deleteLater();
        ShowDone(tr("Update ready"),
                 tr("TuttiFruchii will restart to finish the update. Your settings, saves and "
                    "games are not changed.\n\nThis was a preview: nothing was installed."),
                 {{GameHome::HintRole::Back, tr("Later"), [this] { reject(); }},
                  {GameHome::HintRole::Confirm, tr("Restart Now"), [this] { accept(); }, true}});
    });
    timer->start();
}

void UpdateDialog::Restart(QWidget* window, const QString& target) {
    auto environment = QProcessEnvironment::systemEnvironment();
    const auto app_dir = environment.value(QStringLiteral("APPDIR"));
    if (!app_dir.isEmpty()) {
        for (const auto& key : environment.keys()) {
            const auto value = environment.value(key);
            if (!value.contains(app_dir)) {
                continue;
            }
            const auto stripped = UpdateInstaller::StripPathListEntries(value.toStdString(),
                                                                        app_dir.toStdString());
            if (stripped.empty()) {
                environment.remove(key);
            } else {
                environment.insert(key, QString::fromStdString(stripped));
            }
        }
    }
    for (const auto& key : {"APPDIR", "APPIMAGE", "ARGV0", "OWD"}) {
        environment.remove(QString::fromLatin1(key));
    }

    // Wait for this process to exit, so the new one reads the settings this one writes on close.
    QProcess process;
    process.setProgram(QStringLiteral("/bin/sh"));
    process.setArguments(
        {QStringLiteral("-c"),
         QStringLiteral("i=0; while kill -0 \"$1\" 2>/dev/null && [ \"$i\" -lt 1500 ]; do "
                        "sleep 0.2; i=$((i+1)); done; kill -0 \"$1\" 2>/dev/null && exit 0; "
                        "exec \"$2\""),
         QStringLiteral("sh"), QString::number(QCoreApplication::applicationPid()), target});
    process.setProcessEnvironment(environment);
    if (!process.startDetached()) {
        LOG_ERROR(Frontend, "Could not start the restart helper for {}", target.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to restart"),
                                     tr("Please start %1 again yourself.").arg(target));
        return;
    }
    if (window) {
        QMetaObject::invokeMethod(window, &QWidget::close, Qt::QueuedConnection);
    }
}

#ifdef _WIN32
bool UpdateDialog::LaunchInstaller(QWidget* window, const QString& staging) {
    // The helper runs from a copy, because the installed one is replaced along with everything else.
    const QString helper =
        QCoreApplication::applicationDirPath() + QLatin1Char('/') + QLatin1String(HelperName);
    const QString copy = QDir::temp().filePath(
        QStringLiteral("TuttiFruchii-updater-%1.exe").arg(QCoreApplication::applicationPid()));
    QFile::remove(copy);
    if (!QFile::copy(helper, copy)) {
        LOG_ERROR(Frontend, "Could not copy the installer helper {}", helper.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to install the update"),
                                     tr("Could not start the update installer."));
        return false;
    }

    const auto install_dir = QCoreApplication::applicationDirPath();
    const auto native = [](const QString& path) { return QDir::toNativeSeparators(path); };
    if (!QProcess::startDetached(
            copy, {QString::number(QCoreApplication::applicationPid()), native(staging),
                   native(install_dir), native(QCoreApplication::applicationFilePath())})) {
        LOG_ERROR(Frontend, "Could not start the installer helper {}", copy.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to install the update"),
                                     tr("Could not start the update installer."));
        return false;
    }
    if (window) {
        QMetaObject::invokeMethod(window, &QWidget::close, Qt::QueuedConnection);
    }
    return true;
}

void UpdateDialog::RemoveOldHelper() {
    const QString dir = QCoreApplication::applicationDirPath() + QLatin1Char('/');
    if (QFile::exists(dir + QLatin1String(HelperName))) {
        QFile::remove(dir + QLatin1String(OldHelperName));
    }
}
#endif

void UpdateDialog::Update() {
    if (m_test) {
        FakeUpdate();
        return;
    }
#ifdef _WIN32
    if (m_asset.filename.ends_with(".zip")) {
        const std::filesystem::path exe{QCoreApplication::applicationFilePath().toStdWString()};
        if (UpdateInstaller::CanReplace(exe) &&
            QFile::exists(QCoreApplication::applicationDirPath() + QLatin1Char('/') +
                          QLatin1String(HelperName))) {
            InstallOnRestart(exe);
            return;
        }
    }
#endif
#ifdef __linux__
    if (Common::Net::CurrentLinuxPackage() == Common::Net::LinuxPackage::AppImage &&
        m_asset.filename.ends_with(".AppImage")) {
        const std::filesystem::path target{qEnvironmentVariable("APPIMAGE").toStdString()};
        if (UpdateInstaller::CanReplace(target)) {
            InstallInPlace(target);
            return;
        }
    }
#endif

    // Not installable in place (a build folder, a read-only location): save the file instead.
    auto default_dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (default_dir.isEmpty()) {
        default_dir = QDir::homePath();
    }
    const auto filename = QtCommon::Frontend::GetSaveFileName(
        tr("New Version Location"),
        default_dir % QStringLiteral("/") % QString::fromStdString(m_asset.filename),
        tr("All Files (*.*)"));
    if (filename.isEmpty()) {
        return;
    }
    if (!DownloadTo(filename)) {
        if (m_cancelled) {
            ShowPrompt();
        }
        return;
    }
    ShowDone(tr("Download complete"),
             tr("%1 is saved as %2.").arg(QString::fromStdString(m_release.title), filename),
             {{GameHome::HintRole::Back, tr("Close"), [this] { accept(); }},
              {GameHome::HintRole::Confirm, tr("Open"),
               [this, filename] {
                   QDesktopServices::openUrl(QUrl::fromLocalFile(filename));
                   accept();
               },
               true}});
}

void UpdateDialog::InstallInPlace(const std::filesystem::path& target) {
    const UpdateInstaller::Lock lock{UpdateInstaller::LockPath(target)};
    if (!lock.Held()) {
        ShowError(tr("Could not update"),
                  tr("Another TuttiFruchii window is already updating this installation."));
        return;
    }

    const auto staged = UpdateInstaller::StagedPath(target);
    if (!DownloadTo(QString::fromStdString(staged.string()))) {
        if (m_cancelled) {
            ShowPrompt();
        }
        return;
    }

    std::error_code ec;
    if (!UpdateInstaller::Swap(staged, target, ec)) {
        LOG_ERROR(Frontend, "Could not replace {}: {}", target.string(), ec.message());
        std::filesystem::remove(staged, ec);
        ShowError(tr("Could not update"),
                  tr("Could not replace %1. Your installation was not changed.")
                      .arg(QString::fromStdString(target.string())));
        return;
    }

    LOG_INFO(Frontend, "Installed {} over {}", m_asset.filename, target.string());
    const QString target_path = QString::fromStdString(target.string());
    ShowDone(tr("Update installed"),
             tr("Restart TuttiFruchii to use the new version. The previous version is kept as "
                "%1, and your settings, saves and games are not changed.")
                 .arg(QString::fromStdString(UpdateInstaller::PreviousPath(target).string())),
             {{GameHome::HintRole::Back, tr("Later"), [this] { accept(); }},
              {GameHome::HintRole::Confirm, tr("Restart Now"),
               [this, target_path] {
                   Restart(parentWidget(), target_path);
                   accept();
               },
               true}});
}

#ifdef _WIN32
void UpdateDialog::InstallOnRestart(const std::filesystem::path& exe) {
    const auto install_dir = exe.parent_path();
    const auto staging = install_dir / ".update-staging";
    const auto archive = install_dir / ".update-download.zip";
    const auto cleanup = [staging, archive] {
        std::error_code ec;
        std::filesystem::remove_all(staging, ec);
        std::filesystem::remove(archive, ec);
    };

    cleanup();
    std::error_code ec;
    std::filesystem::create_directories(staging, ec);
    if (ec) {
        ShowError(tr("Could not update"), tr("Could not create a folder for the update."));
        return;
    }
    if (!DownloadTo(QString::fromStdWString(archive.wstring()))) {
        cleanup();
        if (m_cancelled) {
            ShowPrompt();
        }
        return;
    }

    ShowProgress(tr("Preparing the update"), tr("Unpacking..."));
    const auto unpacked = QtCommon::Compress::extractDir(
        QString::fromStdWString(archive.wstring()), QString::fromStdWString(staging.wstring()),
        [this](std::size_t total, std::size_t done) {
            QGuiApplication::processEvents();
            if (total != 0) {
                SetProgress(static_cast<int>((done * 100) / total));
            }
            return !m_cancelled;
        });
    if (m_cancelled) {
        cleanup();
        ShowPrompt();
        return;
    }
    if (unpacked.isEmpty() || !std::filesystem::exists(staging / exe.filename(), ec)) {
        cleanup();
        ShowError(tr("Could not update"), tr("The downloaded update could not be unpacked."));
        return;
    }
    std::filesystem::remove(archive, ec);

    const QString staging_path = QString::fromStdWString(staging.wstring());
    ShowDone(tr("Update ready"),
             tr("TuttiFruchii will close, install the update and start again. The previous "
                "version is kept, and your settings, saves and games are not changed."),
             {{GameHome::HintRole::Back, tr("Later"),
               [this, cleanup] {
                   cleanup();
                   reject();
               }},
              {GameHome::HintRole::Confirm, tr("Restart Now"),
               [this, cleanup, staging_path] {
                   if (LaunchInstaller(parentWidget(), staging_path)) {
                       accept();
                   } else {
                       cleanup();
                       ShowPrompt();
                   }
               },
               true}});
}
#endif

// TODO: migrate to a net.cpp wrapper
bool UpdateDialog::DownloadTo(const QString& filename) {
    QSaveFile file(filename);
    if (!file.open(QIODevice::Truncate | QIODevice::WriteOnly)) {
        LOG_WARNING(Frontend, "Could not open file {}", filename.toStdString());
        ShowError(tr("Could not update"), tr("Could not open %1 for writing.").arg(filename));
        return false;
    }

    constexpr std::size_t timeout_seconds = 15;
    std::unique_ptr<httplib::Client> client = std::make_unique<httplib::Client>(m_asset.url);
    client->set_connection_timeout(timeout_seconds);
    client->set_read_timeout(timeout_seconds);
    client->set_write_timeout(timeout_seconds);
    client->set_follow_location(true);
#ifdef LICHII_BUNDLED_OPENSSL
    client->load_ca_cert_store(kCert, sizeof(kCert));
#endif

    const QString size_text = ReadableSize(m_asset.size);
    ShowProgress(tr("Downloading the update"), size_text);
    QGuiApplication::processEvents();

    const auto progress_callback = [&](size_t processed_size, size_t total_size) {
        QGuiApplication::processEvents();
        if (total_size != 0) {
            const int percent = static_cast<int>((processed_size * 100) / total_size);
            SetProgress(percent);
            m_status->setText(size_text.isEmpty()
                                  ? QStringLiteral("%1%").arg(percent)
                                  : QStringLiteral("%1   ·   %2%").arg(size_text).arg(percent));
        }
        return !m_cancelled;
    };

    QCryptographicHash hash(QCryptographicHash::Sha256);
    u64 received = 0;
    bool write_failed = false;
    const auto content_receiver = [&](const char* t_data, size_t data_length) -> bool {
        hash.addData(QByteArrayView(t_data, static_cast<qsizetype>(data_length)));
        received += data_length;
        if (file.write(t_data, static_cast<qint64>(data_length)) == -1) {
            LOG_WARNING(Frontend, "Could not write {} bytes to file {}", data_length,
                        filename.toStdString());
            write_failed = true;
            return false;
        }
        return true;
    };

    // GitHub's file server sometimes answers 502-504 or drops the connection for a moment: try
    // up to three times, a few seconds apart, starting the file over each time. The size and
    // SHA-256 checks below still decide.
    constexpr int attempts = 3;
    httplib::Result result;
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        if (attempt > 1) {
            file.seek(0);
            file.resize(0);
            hash.reset();
            received = 0;
            SetProgress(0);
            m_status->setText(tr("Trying again (%1 of %2)...").arg(attempt).arg(attempts));
            QEventLoop wait;
            QTimer::singleShot(2000 * (attempt - 1), &wait, &QEventLoop::quit);
            wait.exec();
            if (m_cancelled) {
                break;
            }
        }
        result = client->Get(m_asset.path, content_receiver, progress_callback);
        const bool canceled = !result && result.error() == httplib::Error::Canceled;
        const bool transient = (!result && !canceled && !write_failed) ||
                               (result && (result->status >= 500 || result->status == 429));
        if (!transient || attempt == attempts) {
            break;
        }
        LOG_WARNING(Frontend, "GET to {}{} failed ({}), trying again", m_asset.url, m_asset.path,
                    result ? std::to_string(result->status) : httplib::to_string(result.error()));
    }

    if (m_cancelled) {
        file.cancelWriting();
        return false;
    }
    if (write_failed) {
        file.cancelWriting();
        ShowError(tr("Could not update"), tr("Could not write to %1.").arg(filename));
        return false;
    }
    if (!result) {
        LOG_ERROR(Frontend, "GET to {}{} returned null", m_asset.url, m_asset.path);
        file.cancelWriting();
        ShowError(tr("Could not update"),
                  tr("The download did not finish. Check your connection and try again."));
        return false;
    }

    const auto& response = result.value();
    if (response.status >= 400) {
        LOG_ERROR(Frontend, "GET to {}{} returned error status code: {}", m_asset.url, m_asset.path,
                  response.status);
        file.cancelWriting();
        ShowError(tr("Could not update"),
                  tr("GitHub answered the download with error %1. Try again later.")
                      .arg(response.status));
        return false;
    }

    const bool wrong_size = m_asset.size != 0 && received != m_asset.size;
    const bool wrong_hash =
        !m_asset.sha256.empty() &&
        hash.result().toHex() != QByteArray::fromStdString(m_asset.sha256);
    if (wrong_size || wrong_hash) {
        LOG_ERROR(Frontend, "Downloaded {} does not match the published size or SHA-256",
                  m_asset.filename);
        file.cancelWriting();
        ShowError(tr("Download failed verification"),
                  tr("The downloaded file does not match the checksum GitHub published for it, "
                     "so it was discarded. Try again later."));
        return false;
    }

    if (!file.commit()) {
        LOG_WARNING(Frontend, "Could not commit to file {}", filename.toStdString());
        ShowError(tr("Could not update"), tr("Could not save %1.").arg(filename));
        return false;
    }
    return true;
}
