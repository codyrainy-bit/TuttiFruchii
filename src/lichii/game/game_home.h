// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <QColor>
#include "common/common_types.h"
#include <QListView>
#include <QPointer>
#include <QString>

class GameListModel;
class HomeCard;
class HomeSound;
class QLabel;
class QMenu;
class QMenuBar;
class QPushButton;
class QTimer;
class QVariantAnimation;
class QWidget;

/// One quick-setting chip under the cards (renderer, docked mode, volume, ...).
struct HomeChip {
    QString caption;
    /// The chip's category color, the same as the status bar's.
    QColor color{0xc4, 0xc5, 0xca};
    /// The value shown on the chip.
    std::function<QString()> text;
    /// False greys the chip out, for a setting that is off (for example post effects).
    std::function<bool()> active;
    /// A, or a click. Unset for a chip that only adjusts.
    std::function<void()> activate;
    /// Left/right while the chip is being adjusted (volume). A enters and leaves adjusting.
    std::function<void(int)> adjust;
    /// 0..1: the chip fills to this level from the left (volume), and fills or drains smoothly
    /// as it changes. Unset for a chip without a level.
    std::function<double()> level;
};

/**
 * A console-style home screen for the game list, after the Switch 2's: one row of large cover
 * cards with the selected game's name above it, the main menus as pills at the top and quick
 * settings below. Everything is reachable with a controller (ControllerNavigation sends the keys):
 * left/right picks a game, up/down moves between the menus, the cards and the settings, A starts
 * the game or changes a setting, X or + opens the game's options, B goes back and L/R jump.
 */
class GameHome : public QListView {
    Q_OBJECT

public:
    /// Which buttons the hints show: the controller player 1 uses, or the keyboard.
    enum class ButtonStyle { Keyboard, Switch, Xbox, PlayStation };

    /// What a hint's button does; the glyph comes from the style.
    enum class HintRole { Confirm, Back, Options, Adjust };

    explicit GameHome(QWidget* parent = nullptr);
    ~GameHome() override;

    void SetModel(GameListModel* model);
    void ApplyFilter(const QString& edit_filter_text, GameListModel* model);
    void UpdateIconSize();

    /// The main window's menu bar: its menus become the pills at the top, and it is hidden while
    /// the home screen shows.
    void SetMenuBar(QMenuBar* menu_bar);
    /// Bars hidden while the home screen shows (the status bar: the chips replace it).
    void HideWhileShown(QWidget* bar);
    void SetChips(std::vector<HomeChip> chips);
    void SetButtonStyle(ButtonStyle style);
    [[nodiscard]] ButtonStyle CurrentButtonStyle() const {
        return button_style;
    }
    /// A button glyph as the home view's hints draw it, for other controller-driven screens.
    static QWidget* CreateGlyph(ButtonStyle style, HintRole role, QWidget* parent);
    void RefreshChips();

    /// Moves the selection by delta games (the mouse wheel uses it).
    void StepSelection(int delta);

    void scrollTo(const QModelIndex& index, ScrollHint hint = EnsureVisible) override;

    /// Which row has the controller focus; the card delegate draws the selection ring only on
    /// the cards row.
    [[nodiscard]] bool CardsFocused() const;
    /// 0..1, how far the selection ring's rainbow has turned.
    [[nodiscard]] qreal Pulse() const;
    /// The "+" tile after the last game is selected instead of a game.
    [[nodiscard]] bool PlusSelected() const {
        return plus_selected;
    }

    /// Reorder Game Position: the selected game lifts and moves with left/right until A puts it
    /// down (ReorderFinished(true)) or B puts everything back (ReorderFinished(false)).
    void BeginReorder();
    [[nodiscard]] bool Reordering() const {
        return reordering;
    }
    /// Selects the game with this program ID.
    void SelectProgram(u64 program_id);

signals:
    /// The "+" tile was chosen: add a game directory.
    void AddGamesRequested();
    void ReorderFinished(bool keep);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void currentChanged(const QModelIndex& current, const QModelIndex& previous) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void updateGeometries() override;
    bool eventFilter(QObject* object, QEvent* event) override;
    bool event(QEvent* event) override;

private:
    enum class Row { Menus, Cards, Chips };

    void LayoutChrome();
    void SetRow(Row row);
    void UpdateFocusStyles();
    void UpdateHeader();
    void UpdateHints();
    void EnsureCurrent();
    void MoveSelection(int delta);
    void ActivateCurrent();
    void OpenOptions();
    void OpenMenu(int index);
    void ActivateChip(int index);
    void RebuildPills();
    void StyleChip(size_t index);
    void SelectPlus(bool selected);
    void MoveCurrentGame(int delta);
    void FinishReorder(bool keep);
    void ScrollToRect(const QRect& rect);
    [[nodiscard]] std::vector<int> VisibleRows() const;
    /// Where the "+" tile is, in viewport coordinates.
    [[nodiscard]] QRect PlusRect() const;

    struct HiddenBar {
        QPointer<QWidget> bar;
        bool was_visible = false;
    };
    std::vector<HiddenBar> hidden_bars;
    ButtonStyle button_style = ButtonStyle::Keyboard;

    HomeCard* card_delegate = nullptr;

    QWidget* pill_bar = nullptr;
    QWidget* header = nullptr;
    QLabel* status_label = nullptr;
    QLabel* title_label = nullptr;
    QWidget* chip_bar = nullptr;
    QWidget* hint_bar = nullptr;

    QPointer<QMenuBar> menu_bar;
    bool shortcuts_moved = false;
    std::vector<QPushButton*> pills;
    std::vector<QPointer<QMenu>> pill_menus;

    std::vector<HomeChip> chips;
    std::vector<QPushButton*> chip_buttons;
    std::vector<QLabel*> chip_captions;

    QString filter_text;
    Row row = Row::Cards;
    int focused_pill = 0;
    int focused_chip = 0;
    bool adjusting = false;
    bool plus_selected = false;
    bool reordering = false;
    /// The games' order when reordering started, for B to put back.
    std::vector<u64> reorder_original;
    std::unique_ptr<HomeSound> sound;

    int card_side = 280;
    int ring_pad = 12;
    int gap = 24;

    QVariantAnimation* scroll_animation = nullptr;
    QVariantAnimation* pulse_animation = nullptr;
    QTimer* refresh_timer = nullptr;
    /// The level each chip shows, easing toward chips[i].level().
    std::vector<double> chip_levels;
    QTimer* level_timer = nullptr;
};
