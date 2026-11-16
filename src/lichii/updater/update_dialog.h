// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <vector>

#include <QDialog>

#include "common/net/net.h"
#include "lichii/game/game_home.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;
class QWidget;

/**
 * The update prompt, after the Switch 2's system update screen: the window dims and a card shows
 * the new version, its notes and what to do, with the buttons of the controller in use. Updating
 * downloads, verifies and installs in the card itself (progress, then "restart"), from
 * codyrainy-bit/TuttiFruchii's releases only (UpdateChecker).
 */
class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    // exec() returns QDialog::Accepted when an update was installed or started,
    // QDialog::Rejected to be reminded later, SkipVersion to skip this release.
    static constexpr int SkipVersion = 2;

    /// The version dist/changelog.md describes. Raise it with the changelog so the first launch
    /// of the new version shows it once.
    static constexpr u32 ChangelogVersion = 9;

    /// The changelog (What's New) in the same card, from dist/changelog.md.
    static void ShowChangelog(QWidget* parent, GameHome::ButtonStyle buttons);

    /// test: a preview with a made-up release; "Update" only pretends to download.
    explicit UpdateDialog(const Common::Net::Release& release, QWidget* parent = nullptr,
                          GameHome::ButtonStyle buttons = GameHome::ButtonStyle::Keyboard,
                          bool test = false);
    ~UpdateDialog() override;

    // Start `target` again once this process has exited, then close `window`.
    static void Restart(QWidget* window, const QString& target);

#ifdef _WIN32
    // Start the installer helper on the files under `staging`, then close `window`.
    static bool LaunchInstaller(QWidget* window, const QString& staging);
    // Removes the installer helper older versions shipped under another name.
    static void RemoveOldHelper();
#endif

    /// A release to preview the prompt with (TUTTIFRUCHII_TEST_UPDATE=1).
    static Common::Net::Release TestRelease();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    struct Action {
        GameHome::HintRole role;
        QString text;
        std::function<void()> run;
        bool primary = false;
    };

    void ShowPrompt();
    void ShowNotes();
    void ShowProgress(const QString& title, const QString& text);
    void ShowDone(const QString& title, const QString& text, std::vector<Action> actions);
    void ShowError(const QString& title, const QString& text);
    void SetActions(std::vector<Action> actions);
    void SetProgress(int percent);

    void Update();
    void FakeUpdate();
    // Download the asset to `filename`, verified against the published size and SHA-256.
    bool DownloadTo(const QString& filename);
    // Replace the running AppImage with the new one, keeping the old one for a rollback.
    void InstallInPlace(const std::filesystem::path& target);
#ifdef _WIN32
    // Download and unpack the new version beside the running one; a helper installs it on exit.
    void InstallOnRestart(const std::filesystem::path& exe);
#endif

    Common::Net::Release m_release;
    Common::Net::Asset m_asset;
    bool m_has_asset = false;
    bool m_test = false;
    bool m_busy = false;
    bool m_cancelled = false;
    GameHome::ButtonStyle m_buttons;

    QWidget* m_card = nullptr;
    QLabel* m_eyebrow = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QTextBrowser* m_notes = nullptr;
    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;
    QWidget* m_action_bar = nullptr;
    std::vector<QPushButton*> m_action_buttons;
    std::vector<Action> m_actions;
};
