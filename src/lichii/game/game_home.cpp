// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <numbers>

#include <SDL3/SDL.h>

#include <QAction>
#include <QConicalGradient>
#include <QEasingCurve>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include "common/logging.h"
#include "common/settings.h"
#include "frontend_common/play_time_manager.h"
#include "lichii/game/common.h"
#include "lichii/game/game_home.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/util/game.h"

namespace {

const QColor Accent{0x41, 0x69, 0xe1};
/// Room to each side of a cover; the selection ring reaches RingOverflow past it.
constexpr int CardSidePad = 4;
constexpr int RingOverflow = 18;
bool IsGame(const QModelIndex& index) {
    return index.data(GameListItem::TypeRole).toInt() == static_cast<int>(GameListItemType::Game);
}

/// The status bar shows its values in capitals ("NO AA", "FSR 1 SOFT"); the chips read better in
/// title case ("No AA", "FSR 1 Soft"), keeping acronyms as they are.
QString ChipCase(const QString& text) {
    if (text != text.toUpper()) {
        return text;
    }
    static const QStringList acronyms{
        QStringLiteral("AA"),   QStringLiteral("FX"),  QStringLiteral("FG"),
        QStringLiteral("FSR"),  QStringLiteral("SMAA"), QStringLiteral("FXAA"),
        QStringLiteral("TAA"),  QStringLiteral("MSAA"), QStringLiteral("DLSS"),
        QStringLiteral("FPS"),  QStringLiteral("GPU"),  QStringLiteral("CPU"),
        QStringLiteral("HDR"),  QStringLiteral("SGSR"), QStringLiteral("CAS"),
        QStringLiteral("XESS"), QStringLiteral("FSR1"), QStringLiteral("FSR2"),
        QStringLiteral("FSR3"), QStringLiteral("FSR4"), QStringLiteral("CRT"),
    };
    QStringList words = text.split(QLatin1Char{' '});
    for (QString& word : words) {
        if (word.isEmpty() || acronyms.contains(word) || word.front().isDigit()) {
            continue;
        }
        if (word == QStringLiteral("OPENGL")) {
            word = QStringLiteral("OpenGL");
            continue;
        }
        word = word.front() + word.mid(1).toLower();
    }
    return words.join(QLatin1Char{' '});
}

void Repolish(QWidget* widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// The home screen keeps its own dark look whatever the theme, like the console's.
const QString HomeStyle = QStringLiteral(R"(
GameHome { background-color: #101114; border: 0; }
QWidget#HomePillBar, QWidget#HomeHeader, QWidget#HomeChipBar, QWidget#HomeHintBar {
    background: transparent;
}
QPushButton#HomePill {
    background-color: #1a1b1f; color: #e6e7ea; border: 2px solid #2a2c31;
    border-radius: 10px; font-size: 15px; padding: 0 22px; min-height: 40px; max-height: 40px;
}
QPushButton#HomePill:hover { background-color: #23252a; }
QPushButton#HomePill[homeFocus="true"] {
    border: 2px solid #4169e1; background-color: #1d2640; color: #ffffff;
}
QLabel#HomeStatus { color: #7f9fff; font-size: 14px; font-weight: 600; background: transparent; }
QLabel#HomeTitle { color: #f2f3f5; font-size: 23px; background: transparent; }
QLabel#HomeChipCaption { color: #8b8d94; font-size: 12px; background: transparent; }
QPushButton#HomeHint {
    background: transparent; border: 0; color: #e6e7ea; font-size: 16px; padding: 4px 6px;
}
QPushButton#HomeHint:hover { color: #ffffff; }
QLabel#HomeHintText { color: #e6e7ea; font-size: 16px; background: transparent; }
)");

/// A controller button as it is printed on the controller player 1 uses: Switch letters in a
/// ring, Xbox letters on colored buttons, PlayStation symbols, or a keyboard key. The buttons are
/// matched by position (SDL maps them that way): the right one confirms, the bottom one goes back
/// and the top one opens options.
class HomeGlyph : public QWidget {
public:
    HomeGlyph(GameHome::ButtonStyle style_, GameHome::HintRole role_, QWidget* parent)
        : QWidget{parent}, style{style_}, role{role_} {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        if (style == GameHome::ButtonStyle::Keyboard) {
            QFont key_font = font();
            key_font.setPixelSize(12);
            key_font.setBold(true);
            const int text_w = QFontMetrics(key_font).horizontalAdvance(KeyText());
            setFixedSize(std::max(26, text_w + 14), 24);
        } else {
            setFixedSize(role == GameHome::HintRole::Adjust ? 34 : 26, 26);
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor light(0xe6, 0xe7, 0xea);
        const QRectF box = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);

        if (style == GameHome::ButtonStyle::Keyboard) {
            // A light key cap that reads on dark and on the accent-colored button alike.
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 38));
            painter.drawRoundedRect(box, 5, 5);
            painter.setPen(light);
            QFont key_font = font();
            key_font.setPixelSize(12);
            key_font.setBold(true);
            painter.setFont(key_font);
            painter.drawText(rect(), Qt::AlignCenter, KeyText());
            return;
        }
        if (role == GameHome::HintRole::Adjust) {
            // Left and right on the d-pad or stick.
            painter.setPen(Qt::NoPen);
            painter.setBrush(light);
            const qreal cy = height() / 2.0;
            painter.drawPolygon(QPolygonF{{2, cy}, {13, cy - 7}, {13, cy + 7}});
            painter.drawPolygon(QPolygonF{{width() - 2.0, cy}, {width() - 13.0, cy - 7},
                                          {width() - 13.0, cy + 7}});
            return;
        }

        QFont letter_font = font();
        letter_font.setPixelSize(13);
        letter_font.setBold(true);
        painter.setFont(letter_font);

        switch (style) {
        case GameHome::ButtonStyle::Switch:
            painter.setPen(QPen(light, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(box);
            painter.drawText(rect(), Qt::AlignCenter, Letter());
            break;
        case GameHome::ButtonStyle::Xbox: {
            // Xbox prints A green (bottom), B red (right), Y yellow (top), X blue (left).
            QColor fill;
            switch (role) {
            case GameHome::HintRole::Confirm:
                fill = QColor(0xe0, 0x52, 0x52);
                break;
            case GameHome::HintRole::Back:
                fill = QColor(0x5c, 0xb8, 0x5c);
                break;
            default:
                fill = QColor(0xf0, 0xc3, 0x3c);
                break;
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawEllipse(box);
            painter.setPen(role == GameHome::HintRole::Options ? QColor(0x1a, 0x1a, 0x1a)
                                                               : QColor(Qt::white));
            painter.drawText(rect(), Qt::AlignCenter, Letter());
            break;
        }
        case GameHome::ButtonStyle::PlayStation: {
            // The symbol on a faint light disc: no dark outline around it.
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 34));
            painter.drawEllipse(box);
            const QPointF c = QRectF(rect()).center();
            const qreal r = width() * 0.22;
            switch (role) {
            case GameHome::HintRole::Confirm: // circle, right
                painter.setPen(QPen(QColor(0xff, 0x6b, 0x7a), 2));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(c, r, r);
                break;
            case GameHome::HintRole::Back: // cross, bottom
                painter.setPen(QPen(QColor(0x7a, 0xa7, 0xff), 2, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(c + QPointF(-r, -r), c + QPointF(r, r));
                painter.drawLine(c + QPointF(-r, r), c + QPointF(r, -r));
                break;
            default: // triangle, top
                painter.setPen(QPen(QColor(0x3d, 0xdc, 0x97), 2, Qt::SolidLine, Qt::RoundCap,
                                    Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPolygon(QPolygonF{c + QPointF(0, -r * 1.1),
                                              c + QPointF(r * 1.05, r * 0.75),
                                              c + QPointF(-r * 1.05, r * 0.75)});
                break;
            }
            break;
        }
        default:
            break;
        }
    }

private:
    QString Letter() const {
        // By position: Switch A/B/X, and the same places on Xbox are B/A/Y.
        const bool xbox = style == GameHome::ButtonStyle::Xbox;
        switch (role) {
        case GameHome::HintRole::Confirm:
            return xbox ? QStringLiteral("B") : QStringLiteral("A");
        case GameHome::HintRole::Back:
            return xbox ? QStringLiteral("A") : QStringLiteral("B");
        default:
            return xbox ? QStringLiteral("Y") : QStringLiteral("X");
        }
    }

    QString KeyText() const {
        switch (role) {
        case GameHome::HintRole::Confirm:
            return QStringLiteral("Enter");
        case GameHome::HintRole::Back:
            return QStringLiteral("Esc");
        case GameHome::HintRole::Options:
            return QStringLiteral("X");
        default:
            return QStringLiteral("\u2190 \u2192");
        }
    }

    GameHome::ButtonStyle style;
    GameHome::HintRole role;
};

/// The selection ring: a rainbow that slowly flows around the tile (phase 0..1 is one turn), with
/// a soft glow, while the cards have the focus; a thin accent outline otherwise.
void DrawSelectionRing(QPainter* painter, const QRectF& tile, qreal radius, bool focused,
                       qreal phase) {
    constexpr qreal Offset = 8;
    const QRectF ring = tile.adjusted(-Offset, -Offset, Offset, Offset);
    const qreal ring_radius = radius + Offset;
    painter->save();
    painter->setBrush(Qt::NoBrush);
    if (!focused) {
        QColor dim = Accent;
        dim.setAlpha(110);
        painter->setPen(QPen(dim, 2));
        painter->drawRoundedRect(ring, ring_radius, ring_radius);
        painter->restore();
        return;
    }
    QConicalGradient gradient(ring.center(), -phase * 360.0);
    constexpr int Stops = 12;
    for (int i = 0; i <= Stops; ++i) {
        const qreal t = static_cast<qreal>(i) / Stops;
        gradient.setColorAt(t, QColor::fromHsvF(static_cast<float>(i == Stops ? 0.0 : t), 0.7f,
                                                1.0f));
    }
    painter->setOpacity(0.3);
    painter->setPen(QPen(QBrush(gradient), 16));
    painter->drawRoundedRect(ring, ring_radius, ring_radius);
    painter->setOpacity(1.0);
    painter->setPen(QPen(QBrush(gradient), 6));
    painter->drawRoundedRect(ring, ring_radius, ring_radius);
    painter->restore();
}

} // namespace

/// A short, soft tick played as the selection moves from game to game, synthesized once and
/// played through SDL (already linked for controllers). The device is opened on the first tick
/// and closed when the home view hides, before a game's own audio starts.
class HomeSound {
public:
    ~HomeSound() {
        Close();
    }

    void Play() {
        if (!UISettings::values.home_sounds.GetValue() || Settings::values.audio_muted) {
            return;
        }
        const float volume =
            std::clamp(static_cast<float>(Settings::values.volume.GetValue()) / 100.0f, 0.0f,
                       2.0f);
        if (volume <= 0.0f || !Open()) {
            return;
        }
        std::vector<float> samples = tick;
        for (float& sample : samples) {
            sample *= volume;
        }
        // A new tick replaces one still playing, so fast scrolling never queues up.
        SDL_ClearAudioStream(stream);
        SDL_PutAudioStreamData(stream, samples.data(),
                               static_cast<int>(samples.size() * sizeof(float)));
    }

    void Close() {
        if (stream != nullptr) {
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
        if (audio_initialized) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audio_initialized = false;
        }
    }

private:
    bool Open() {
        if (stream != nullptr) {
            return true;
        }
        if (failed) {
            return false;
        }
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            LOG_WARNING(Frontend, "Home view sound: no audio ({})", SDL_GetError());
            failed = true;
            return false;
        }
        audio_initialized = true;
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, SampleRate};
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr,
                                           nullptr);
        if (stream == nullptr) {
            LOG_WARNING(Frontend, "Home view sound: no playback device ({})", SDL_GetError());
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audio_initialized = false;
            failed = true;
            return false;
        }
        SDL_ResumeAudioStreamDevice(stream);
        if (tick.empty()) {
            BuildTick();
        }
        return true;
    }

    /// ~40 ms: a quick attack and a fast decay on a bright tone with a softer octave below, like
    /// a console menu's tick.
    void BuildTick() {
        constexpr int Length = SampleRate * 40 / 1000;
        constexpr double Tau = 2.0 * std::numbers::pi;
        tick.resize(Length);
        for (int i = 0; i < Length; ++i) {
            const double t = static_cast<double>(i) / SampleRate;
            const double envelope = std::min(1.0, t / 0.0008) * std::exp(-t / 0.0085);
            const double tone = 0.55 * std::sin(Tau * 1850.0 * t) +
                                0.25 * std::sin(Tau * 3700.0 * t) +
                                0.20 * std::sin(Tau * 925.0 * t);
            tick[static_cast<size_t>(i)] = static_cast<float>(0.2 * envelope * tone);
        }
    }

    static constexpr int SampleRate = 48000;
    SDL_AudioStream* stream = nullptr;
    bool audio_initialized = false;
    bool failed = false;
    std::vector<float> tick;
};

/// Paints one game as a large square cover with rounded corners, and the selection ring around the
/// selected one.
class HomeCard : public QStyledItemDelegate {
public:
    explicit HomeCard(GameHome* home_) : QStyledItemDelegate{home_}, home{home_} {}

    /// The cover's side, and the room around it: little to the sides (the selection ring spills
    /// into the gap between games, so they can sit close), enough above and below for the ring.
    void SetGeometry(int side_, int side_pad_, int ring_) {
        side = side_;
        side_pad = side_pad_;
        ring = ring_;
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return {side + 2 * side_pad, side + 2 * ring};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);

        const QRect card = option.rect.adjusted(side_pad, ring, -side_pad, -ring);
        const qreal radius = std::max(8.0, card.width() * 0.045);
        QPainterPath path;
        path.addRoundedRect(QRectF(card), radius, radius);

        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QPixmap cover = Cover(index, card.width(), dpr);
        if (!cover.isNull()) {
            painter->setClipPath(path);
            painter->drawPixmap(card, cover);
            painter->setClipping(false);
        } else {
            // No artwork: the name on a plain tile.
            painter->fillPath(path, QColor(0x22, 0x23, 0x28));
            painter->setPen(QColor(0xc4, 0xc5, 0xca));
            QFont font = painter->font();
            font.setPixelSize(std::max(14, card.width() / 14));
            painter->setFont(font);
            painter->drawText(card.adjusted(16, 16, -16, -16),
                              Qt::AlignCenter | Qt::TextWordWrap,
                              index.data(GameListItemPath::TitleRole).toString());
        }

        if (index == home->currentIndex() && !home->PlusSelected()) {
            if (home->Reordering()) {
                // Picked up: a solid white ring.
                const QRectF ring = QRectF(card).adjusted(-8, -8, 8, 8);
                painter->setBrush(Qt::NoBrush);
                painter->setPen(QPen(QColor(255, 255, 255, 70), 16));
                painter->drawRoundedRect(ring, radius + 8, radius + 8);
                painter->setPen(QPen(QColor(255, 255, 255), 6));
                painter->drawRoundedRect(ring, radius + 8, radius + 8);
            } else {
                DrawSelectionRing(painter, QRectF(card), radius, home->CardsFocused(),
                                  home->Pulse());
            }
        }
        painter->restore();
    }

private:
    /// The game's cover at the card's size: the artwork the user set or downloaded, else the
    /// game's own icon scaled up. Cached by program and by the icon the model holds, so a new
    /// download (which replaces the icon) shows at once.
    static QPixmap Cover(const QModelIndex& index, int side, qreal dpr) {
        const u64 program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
        const QPixmap icon = index.data(Qt::DecorationRole).value<QPixmap>();
        const int pixels = static_cast<int>(side * dpr);
        const QString key = QStringLiteral("home:%1:%2:%3")
                                .arg(program_id, 16, 16, QLatin1Char{'0'})
                                .arg(icon.cacheKey())
                                .arg(pixels);
        QPixmap cached;
        if (QPixmapCache::find(key, &cached)) {
            return cached;
        }

        QImage image;
        if (program_id != 0) {
            const std::vector<u8> artwork = QtCommon::Game::ReadLibraryArtwork(program_id);
            if (!artwork.empty()) {
                image.loadFromData(artwork.data(), static_cast<int>(artwork.size()));
            }
        }
        if (image.isNull() && !icon.isNull() &&
            index.data(GameListItemPath::HasIconRole).toBool()) {
            image = icon.toImage();
        }
        if (image.isNull()) {
            return {};
        }
        image = image.scaled(pixels, pixels, Qt::KeepAspectRatioByExpanding,
                             Qt::SmoothTransformation);
        image = image.copy((image.width() - pixels) / 2, (image.height() - pixels) / 2, pixels,
                           pixels);
        QPixmap pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(dpr);
        QPixmapCache::insert(key, pixmap);
        return pixmap;
    }

    GameHome* home;
    int side = 280;
    int side_pad = 4;
    int ring = 12;
};

GameHome::GameHome(QWidget* parent) : QListView{parent} {
    setObjectName(QStringLiteral("GameHome"));
    setStyleSheet(HomeStyle);

    // Covers are up to 440 px a side, 1-2 MiB each at high DPI: room for a library's worth.
    QPixmapCache::setCacheLimit(std::max(QPixmapCache::cacheLimit(), 96 * 1024));

    card_delegate = new HomeCard(this);
    setItemDelegate(card_delegate);

    setViewMode(QListView::ListMode);
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setMovement(QListView::Static);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    pill_bar = new QWidget(this);
    pill_bar->setObjectName(QStringLiteral("HomePillBar"));
    auto* pill_layout = new QHBoxLayout(pill_bar);
    pill_layout->setContentsMargins(0, 0, 0, 0);
    pill_layout->setSpacing(12);
    pill_layout->addStretch(1);

    header = new QWidget(this);
    header->setObjectName(QStringLiteral("HomeHeader"));
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(4);
    header_layout->addStretch(1);
    status_label = new QLabel(header);
    status_label->setObjectName(QStringLiteral("HomeStatus"));
    title_label = new QLabel(header);
    title_label->setObjectName(QStringLiteral("HomeTitle"));
    title_label->setTextFormat(Qt::RichText);
    header_layout->addWidget(status_label);
    header_layout->addWidget(title_label);

    chip_bar = new QWidget(this);
    chip_bar->setObjectName(QStringLiteral("HomeChipBar"));
    auto* chip_layout = new QGridLayout(chip_bar);
    chip_layout->setContentsMargins(0, 0, 0, 0);
    chip_layout->setHorizontalSpacing(14);
    chip_layout->setVerticalSpacing(6);

    hint_bar = new QWidget(this);
    hint_bar->setObjectName(QStringLiteral("HomeHintBar"));
    auto* hint_layout = new QHBoxLayout(hint_bar);
    hint_layout->setContentsMargins(0, 0, 0, 0);
    hint_layout->setSpacing(22);

    scroll_animation = new QVariantAnimation(this);
    scroll_animation->setDuration(220);
    scroll_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(scroll_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { horizontalScrollBar()->setValue(value.toInt()); });

    // One slow, even turn of the rainbow around the selected tile every six seconds.
    pulse_animation = new QVariantAnimation(this);
    pulse_animation->setStartValue(0.0);
    pulse_animation->setEndValue(1.0);
    pulse_animation->setDuration(6000);
    pulse_animation->setLoopCount(-1);
    pulse_animation->setEasingCurve(QEasingCurve::Linear);
    connect(pulse_animation, &QVariantAnimation::valueChanged, this, [this] {
        if (row != Row::Cards) {
            return;
        }
        if (plus_selected) {
            viewport()->update(PlusRect().adjusted(-RingOverflow, 0, RingOverflow, 0));
        } else if (const QModelIndex current = currentIndex(); current.isValid()) {
            viewport()->update(visualRect(current).adjusted(-RingOverflow, 0, RingOverflow, 0));
        }
    });

    sound = std::make_unique<HomeSound>();

    // Chips with a level (volume) ease toward a new one: they fill up and drain smoothly.
    level_timer = new QTimer(this);
    level_timer->setInterval(16);
    connect(level_timer, &QTimer::timeout, this, [this] {
        bool moving = false;
        for (size_t i = 0; i < chips.size() && i < chip_levels.size(); ++i) {
            if (!chips[i].level) {
                continue;
            }
            const double target = std::clamp(chips[i].level(), 0.0, 1.0);
            double& shown = chip_levels[i];
            if (std::abs(target - shown) < 0.002) {
                if (shown != target) {
                    shown = target;
                    StyleChip(i);
                }
                continue;
            }
            shown += (target - shown) * 0.22;
            moving = true;
            StyleChip(i);
        }
        if (!moving) {
            level_timer->stop();
        }
    });

    // Settings change elsewhere too (Configure, the hotkeys): keep the chips current.
    refresh_timer = new QTimer(this);
    refresh_timer->setInterval(1000);
    connect(refresh_timer, &QTimer::timeout, this, &GameHome::RefreshChips);

    UpdateHints();
}

GameHome::~GameHome() = default;

void GameHome::SetModel(GameListModel* game_model) {
    QListView::setModel(game_model);
    // New or re-sorted rows: hide the ones that are not games, keep the search applied.
    const auto settle = [this, game_model] {
        QTimer::singleShot(0, this, [this, game_model] { ApplyFilter(filter_text, game_model); });
    };
    connect(game_model, &QAbstractItemModel::rowsInserted, this, settle);
    connect(game_model, &QAbstractItemModel::modelReset, this, settle);
    connect(game_model, &QAbstractItemModel::layoutChanged, this, settle);
    connect(game_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& top_left) {
                if (top_left.row() == currentIndex().row()) {
                    UpdateHeader();
                }
            });
    UpdateIconSize();
}

void GameHome::ApplyFilter(const QString& edit_filter_text, GameListModel* model) {
    filter_text = edit_filter_text;
    const int row_count = model->rowCount();
    for (int i = 0; i < row_count; ++i) {
        QStandardItem* item = model->item(i, 0);
        const bool show = item != nullptr && IsGame(item->index()) &&
                          Lichii::FilterMatches(edit_filter_text, item);
        setRowHidden(i, !show);
    }
    EnsureCurrent();
    UpdateHeader();
}

void GameHome::UpdateIconSize() {
    LayoutChrome();
}

void GameHome::SetMenuBar(QMenuBar* menu_bar_) {
    menu_bar = menu_bar_;
    HideWhileShown(menu_bar_);
    if (isVisible()) {
        RebuildPills();
    }
}

void GameHome::SetChips(std::vector<HomeChip> chips_) {
    chips = std::move(chips_);
    for (QPushButton* button : chip_buttons) {
        button->deleteLater();
    }
    for (QLabel* caption : chip_captions) {
        caption->deleteLater();
    }
    chip_buttons.clear();
    chip_captions.clear();

    auto* layout = static_cast<QGridLayout*>(chip_bar->layout());
    for (size_t i = 0; i < chips.size(); ++i) {
        auto* caption = new QLabel(chips[i].caption, chip_bar);
        caption->setObjectName(QStringLiteral("HomeChipCaption"));
        auto* button = new QPushButton(chip_bar);
        button->setObjectName(QStringLiteral("HomeChip"));
        button->setFocusPolicy(Qt::NoFocus);
        const int column = static_cast<int>(i);
        connect(button, &QPushButton::clicked, this, [this, column] {
            focused_chip = column;
            SetRow(Row::Chips);
            ActivateChip(column);
        });
        layout->addWidget(caption, 0, column);
        layout->addWidget(button, 1, column);
        chip_captions.push_back(caption);
        chip_buttons.push_back(button);
    }
    layout->setColumnStretch(static_cast<int>(chips.size()), 1);
    focused_chip = std::clamp(focused_chip, 0, std::max(0, static_cast<int>(chips.size()) - 1));
    chip_levels.assign(chips.size(), 0.0);
    for (size_t i = 0; i < chips.size(); ++i) {
        if (chips[i].level) {
            chip_levels[i] = chips[i].level();
        }
    }
    LayoutChrome();
    RefreshChips();
}

void GameHome::RefreshChips() {
    for (size_t i = 0; i < chips.size() && i < chip_buttons.size(); ++i) {
        QPushButton* button = chip_buttons[i];
        const QString text = chips[i].text ? ChipCase(chips[i].text()) : QString{};
        if (button->text() != text) {
            button->setText(text);
        }
        const bool inactive = chips[i].active && !chips[i].active();
        if (button->property("inactive").toBool() != inactive ||
            button->styleSheet().isEmpty()) {
            button->setProperty("inactive", inactive);
            StyleChip(i);
        }
        if (chips[i].level && i < chip_levels.size() &&
            std::abs(chips[i].level() - chip_levels[i]) > 0.001) {
            level_timer->start();
        }
    }
}

void GameHome::StyleChip(size_t index) {
    if (index >= chip_buttons.size() || index >= chips.size()) {
        return;
    }
    QPushButton* button = chip_buttons[index];
    const QColor color = chips[index].color;
    const bool inactive = button->property("inactive").toBool();
    const bool focus = button->property("homeFocus").toBool();
    const bool adjust = button->property("adjusting").toBool();
    const bool has_level = static_cast<bool>(chips[index].level) && index < chip_levels.size();
    const auto rgba = [&color](double alpha) {
        return QStringLiteral("rgba(%1, %2, %3, %4)")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(alpha);
    };

    // The status bar's colors: a tint of the category color, grey when the setting is off. The
    // focused chip gets a border in its own color, white while it is being adjusted.
    QString background = inactive ? QStringLiteral("#17181c") : rgba(focus ? 0.28 : 0.14);
    const QString text = inactive ? QStringLiteral("#5c5e65") : color.name();
    QString border = QStringLiteral("transparent");
    if (adjust) {
        border = QStringLiteral("#ffffff");
    } else if (focus) {
        border = inactive ? QStringLiteral("#8b8d94") : color.name();
    }
    if (inactive && focus) {
        background = QStringLiteral("#202227");
    }
    if (has_level) {
        // Filled from the left up to the level, a fainter tint after it.
        const double level = std::clamp(chip_levels[index], 0.0, 1.0);
        const QString fill = inactive ? QStringLiteral("rgba(92, 94, 101, 0.35)")
                                      : rgba(focus || adjust ? 0.55 : 0.42);
        const QString rest = inactive ? QStringLiteral("#17181c") : rgba(focus ? 0.12 : 0.07);
        background = QStringLiteral("qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %1, "
                                    "stop:%2 %1, stop:%3 %4, stop:1 %4)")
                         .arg(fill)
                         .arg(level, 0, 'f', 4)
                         .arg(std::min(1.0, level + 0.002), 0, 'f', 4)
                         .arg(rest);
    }
    button->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2; border: 2px solid %3;"
                       " border-radius: 8px; font-size: 15px; min-height: 44px;"
                       " max-height: 44px; padding: 0 10px; }"
                       "QPushButton:hover { background: %4; }")
            .arg(background, text, border,
                 has_level ? background
                           : (inactive ? QStringLiteral("#202227") : rgba(0.24))));
}

QWidget* GameHome::CreateGlyph(ButtonStyle style, HintRole role, QWidget* parent) {
    return new HomeGlyph(style, role, parent);
}

void GameHome::SetButtonStyle(ButtonStyle style) {
    if (button_style == style) {
        return;
    }
    button_style = style;
    UpdateHints();
}

void GameHome::HideWhileShown(QWidget* bar) {
    if (bar == nullptr) {
        return;
    }
    bar->installEventFilter(this);
    HiddenBar entry{bar, bar->isVisible()};
    if (isVisible()) {
        bar->hide();
    }
    hidden_bars.push_back(entry);
}

bool GameHome::event(QEvent* event) {
    // Esc backs out of reordering, adjusting and the menu or chip rows here; the main window's
    // own Esc shortcut must not take it first.
    if (event->type() == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Escape || key->key() == Qt::Key_Backspace) &&
            (reordering || adjusting || row != Row::Cards)) {
            event->accept();
            return true;
        }
    }
    return QListView::event(event);
}

bool GameHome::eventFilter(QObject* object, QEvent* event) {
    // Something showed a hidden bar while the home screen is up (leaving fullscreen does):
    // remember that it should come back afterwards, and hide it again.
    if (event->type() == QEvent::Show && isVisible()) {
        for (HiddenBar& entry : hidden_bars) {
            if (entry.bar == object) {
                entry.was_visible = true;
                QTimer::singleShot(0, entry.bar, [this, bar = entry.bar] {
                    if (bar && isVisible()) {
                        bar->hide();
                    }
                });
            }
        }
    }
    return QListView::eventFilter(object, event);
}

void GameHome::RebuildPills() {
    for (QPushButton* pill : pills) {
        pill->deleteLater();
    }
    pills.clear();
    pill_menus.clear();
    if (!menu_bar) {
        return;
    }
    auto* layout = static_cast<QHBoxLayout*>(pill_bar->layout());
    for (QAction* action : menu_bar->actions()) {
        QMenu* menu = action->menu();
        if (menu == nullptr || !action->isVisible()) {
            continue;
        }
        QString text = action->text();
        text.remove(QLatin1Char{'&'});
        auto* pill = new QPushButton(text, pill_bar);
        pill->setObjectName(QStringLiteral("HomePill"));
        pill->setFocusPolicy(Qt::NoFocus);
        pill->setMinimumWidth(118);
        const int index = static_cast<int>(pills.size());
        connect(pill, &QPushButton::clicked, this, [this, index] { OpenMenu(index); });
        layout->insertWidget(index, pill);
        pills.push_back(pill);
        pill_menus.emplace_back(menu);
    }
    focused_pill = std::clamp(focused_pill, 0, std::max(0, static_cast<int>(pills.size()) - 1));
    UpdateFocusStyles();
}

bool GameHome::CardsFocused() const {
    return row == Row::Cards && hasFocus();
}

qreal GameHome::Pulse() const {
    return pulse_animation->currentValue().toReal();
}

void GameHome::LayoutChrome() {
    const int w = width();
    const int h = height();

    // A pill is 40 px of text area and a 2 px border each side: the bar leaves room for both.
    constexpr int pill_top = 16;
    constexpr int pill_h = 50;
    constexpr int header_h = 66;
    constexpr int caption_h = 18;
    constexpr int chip_h = 50;
    constexpr int cards_to_chips = 26;
    constexpr int chips_to_hints = 18;
    constexpr int hint_h = 44;
    constexpr int bottom_pad = 14;
    ring_pad = 18; // The rainbow ring and its glow, around the cover.

    const int top_fixed = pill_top + pill_h;
    const int chips_block = caption_h + 6 + chip_h;
    const int bottom_fixed = cards_to_chips + chips_block + chips_to_hints + hint_h + bottom_pad;
    // The list view also keeps its item spacing (the gap) above and below the cards, so the row
    // is the card, the ring's room and a gap on each side: the cards stay square.
    const int available = h - top_fixed - header_h - bottom_fixed - 2 * ring_pad;
    // gap = max(18, card / 17), so card + 2 * gap fits when card is at most this:
    int fitting = available * 17 / 19;
    if (fitting < 18 * 17) {
        fitting = available - 2 * 18;
    }
    card_side = std::clamp(fitting, 120, 620);
    // The ring reaches 16 px past a cover: the gap plus both sides' padding leaves it room.
    gap = std::max(18, card_side / 17);
    const int view_h = card_side + 2 * ring_pad + 2 * gap;
    const int extra = std::max(0, h - top_fixed - header_h - bottom_fixed - view_h);
    const int spacer_top = extra * 55 / 100;

    const int top = top_fixed + spacer_top + header_h;
    const int bottom = std::max(0, h - top - view_h);
    const int side_pad = gap + CardSidePad;

    card_delegate->SetGeometry(card_side, CardSidePad, ring_pad);
    if (spacing() != gap) {
        setSpacing(gap);
    }
    if (viewportMargins() != QMargins(0, top, 0, bottom)) {
        setViewportMargins(0, top, 0, bottom);
    }
    scheduleDelayedItemsLayout();

    const int inner_w = std::max(0, w - 2 * side_pad);
    pill_bar->setGeometry(side_pad, pill_top, inner_w, pill_h);
    // The header sits right above the covers and the chips right below them, over the gap the
    // view keeps around the row.
    header->setGeometry(side_pad, top + gap - header_h, inner_w, header_h - 4);
    chip_bar->setGeometry(side_pad, top + view_h - gap + cards_to_chips, inner_w, chips_block);
    // Up to 136 px a chip; narrower windows share the width out.
    if (!chip_buttons.empty()) {
        constexpr int chip_spacing = 14;
        const int count = static_cast<int>(chip_buttons.size());
        const int chip_w =
            std::clamp((inner_w - (count - 1) * chip_spacing) / count, 64, 136);
        for (QPushButton* button : chip_buttons) {
            button->setFixedWidth(chip_w);
        }
        for (QLabel* caption : chip_captions) {
            caption->setFixedWidth(chip_w);
        }
    }
    hint_bar->setGeometry(side_pad, h - bottom_pad - hint_h, inner_w, hint_h);
}

void GameHome::resizeEvent(QResizeEvent* event) {
    QListView::resizeEvent(event);
    LayoutChrome();
}

void GameHome::showEvent(QShowEvent* event) {
    QListView::showEvent(event);
    if (menu_bar) {
        // The pills replace the menu bar here. Its actions keep their shortcuts while it is
        // hidden only if the window holds them too.
        if (!shortcuts_moved) {
            QList<QAction*> with_shortcuts;
            const std::function<void(const QList<QAction*>&)> collect =
                [&](const QList<QAction*>& actions) {
                    for (QAction* action : actions) {
                        if (QMenu* menu = action->menu()) {
                            collect(menu->actions());
                        } else if (!action->shortcut().isEmpty()) {
                            with_shortcuts.append(action);
                        }
                    }
                };
            collect(menu_bar->actions());
            window()->addActions(with_shortcuts);
            shortcuts_moved = true;
        }
        RebuildPills();
    }
    for (HiddenBar& entry : hidden_bars) {
        if (entry.bar) {
            entry.was_visible = entry.bar->isVisible();
            entry.bar->hide();
        }
    }
    LayoutChrome();
    RefreshChips();
    EnsureCurrent();
    UpdateHeader();
    pulse_animation->start();
    refresh_timer->start();
    QTimer::singleShot(0, this, [this] {
        if (isVisible() && isEnabled()) {
            setFocus(Qt::OtherFocusReason);
        }
    });
}

void GameHome::hideEvent(QHideEvent* event) {
    QListView::hideEvent(event);
    for (const HiddenBar& entry : hidden_bars) {
        if (entry.bar && entry.was_visible) {
            entry.bar->show();
        }
    }
    pulse_animation->stop();
    refresh_timer->stop();
    adjusting = false;
    FinishReorder(true);
    // Free the audio device before a game opens its own.
    sound->Close();
}

void GameHome::SetRow(Row row_) {
    if (row != row_) {
        row = row_;
        adjusting = false;
    }
    UpdateFocusStyles();
    UpdateHints();
    viewport()->update();
}

void GameHome::UpdateFocusStyles() {
    for (size_t i = 0; i < pills.size(); ++i) {
        const bool focus = row == Row::Menus && static_cast<int>(i) == focused_pill;
        if (pills[i]->property("homeFocus").toBool() != focus) {
            pills[i]->setProperty("homeFocus", focus);
            Repolish(pills[i]);
        }
    }
    for (size_t i = 0; i < chip_buttons.size(); ++i) {
        const bool focus = row == Row::Chips && static_cast<int>(i) == focused_chip;
        const bool adjust = focus && adjusting;
        if (chip_buttons[i]->property("homeFocus").toBool() != focus ||
            chip_buttons[i]->property("adjusting").toBool() != adjust) {
            chip_buttons[i]->setProperty("homeFocus", focus);
            chip_buttons[i]->setProperty("adjusting", adjust);
            StyleChip(i);
        }
    }
}

void GameHome::UpdateHeader() {
    if (reordering && currentIndex().isValid()) {
        status_label->setText(tr("Reorder Game Position: move it, then press Done"));
        title_label->setText(
            QStringLiteral("<span style=\"color:#ffffff;\">&#8644;</span>&nbsp;&nbsp;%1")
                .arg(currentIndex().data(GameListItemPath::TitleRole).toString().toHtmlEscaped()));
        return;
    }
    if (plus_selected) {
        status_label->setText(tr("Add a folder with your games"));
        title_label->setText(
            QStringLiteral("<span style=\"color:#4169e1;\">&#9679;</span>&nbsp;&nbsp;%1")
                .arg(tr("Add Games")));
        return;
    }
    const QModelIndex current = currentIndex();
    if (!current.isValid() || isRowHidden(current.row())) {
        status_label->clear();
        title_label->setText(tr("No games"));
        return;
    }
    const QString title = current.data(GameListItemPath::TitleRole).toString();
    const QString file_type = current.data(GameListItemPath::FileTypeRole).toString();
    const u64 play_time =
        model()
            ->index(current.row(), GameListModel::COLUMN_PLAY_TIME)
            .data(GameListItemPlayTime::PlayTimeRole)
            .toULongLong();
    const QString played =
        play_time > 0
            ? tr("Played %1")
                  .arg(QString::fromStdString(
                      PlayTime::PlayTimeManager::GetReadablePlayTime(play_time)))
            : tr("Not played yet");
    status_label->setText(file_type.isEmpty() ? played
                                              : QStringLiteral("%1    %2").arg(file_type, played));
    title_label->setText(
        QStringLiteral("<span style=\"color:#4169e1;\">&#9679;</span>&nbsp;&nbsp;%1")
            .arg(title.toHtmlEscaped()));
}

void GameHome::UpdateHints() {
    struct Hint {
        HintRole role;
        QString text;
        std::function<void()> action;
    };
    std::vector<Hint> hints;
    switch (row) {
    case Row::Cards:
        if (reordering) {
            hints = {{HintRole::Adjust, tr("Move"), nullptr},
                     {HintRole::Back, tr("Cancel"), [this] { FinishReorder(false); }},
                     {HintRole::Confirm, tr("Done"), [this] { FinishReorder(true); }}};
        } else if (plus_selected) {
            hints = {{HintRole::Confirm, tr("Add Games"), [this] { ActivateCurrent(); }}};
        } else {
            hints = {{HintRole::Options, tr("Options"), [this] { OpenOptions(); }},
                     {HintRole::Confirm, tr("Play"), [this] { ActivateCurrent(); }}};
        }
        break;
    case Row::Chips:
        if (adjusting) {
            hints = {{HintRole::Adjust, tr("Adjust"), nullptr},
                     {HintRole::Confirm, tr("Done"), [this] { ActivateChip(focused_chip); }}};
        } else {
            hints = {{HintRole::Back, tr("Back"), [this] { SetRow(Row::Cards); }},
                     {HintRole::Confirm, tr("Change"), [this] { ActivateChip(focused_chip); }}};
        }
        break;
    case Row::Menus:
        hints = {{HintRole::Back, tr("Back"), [this] { SetRow(Row::Cards); }},
                 {HintRole::Confirm, tr("Open"), [this] { OpenMenu(focused_pill); }}};
        break;
    }

    auto* layout = static_cast<QHBoxLayout*>(hint_bar->layout());
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    layout->addStretch(1);
    for (const Hint& hint : hints) {
        auto* button = new QPushButton(hint_bar);
        button->setObjectName(QStringLiteral("HomeHint"));
        button->setFocusPolicy(Qt::NoFocus);
        button->setFlat(true);
        auto* inner = new QHBoxLayout(button);
        inner->setContentsMargins(6, 0, 6, 0);
        inner->setSpacing(8);
        auto* glyph = new HomeGlyph(button_style, hint.role, button);
        auto* text = new QLabel(hint.text, button);
        text->setObjectName(QStringLiteral("HomeHintText"));
        text->setAttribute(Qt::WA_TransparentForMouseEvents);
        inner->addWidget(glyph);
        inner->addWidget(text);
        button->setMinimumWidth(glyph->width() + text->sizeHint().width() + 32);
        button->setMinimumHeight(36);
        if (hint.action) {
            connect(button, &QPushButton::clicked, this, hint.action);
        } else {
            button->setEnabled(false);
        }
        layout->addWidget(button);
    }
}

void GameHome::EnsureCurrent() {
    if (model() == nullptr) {
        return;
    }
    const QModelIndex current = currentIndex();
    if (current.isValid() && !isRowHidden(current.row())) {
        return;
    }
    for (int i = 0; i < model()->rowCount(); ++i) {
        if (!isRowHidden(i)) {
            setCurrentIndex(model()->index(i, 0));
            return;
        }
    }
}

std::vector<int> GameHome::VisibleRows() const {
    std::vector<int> visible;
    if (model() == nullptr) {
        return visible;
    }
    for (int i = 0; i < model()->rowCount(); ++i) {
        if (!isRowHidden(i)) {
            visible.push_back(i);
        }
    }
    return visible;
}

void GameHome::MoveSelection(int delta) {
    // Positions 0..size-1 are the games, size is the "+" tile after them.
    const std::vector<int> visible = VisibleRows();
    const int size = static_cast<int>(visible.size());
    int position = size;
    if (!plus_selected) {
        const auto it = std::find(visible.begin(), visible.end(), currentIndex().row());
        position = it == visible.end() ? 0 : static_cast<int>(it - visible.begin());
    }
    const int next = std::clamp(position + delta, 0, size);
    if (next == position) {
        return;
    }
    if (next == size) {
        SelectPlus(true);
    } else {
        SelectPlus(false);
        setCurrentIndex(model()->index(visible[static_cast<size_t>(next)], 0));
    }
    sound->Play();
}

void GameHome::SelectPlus(bool selected) {
    if (plus_selected == selected) {
        return;
    }
    plus_selected = selected;
    if (plus_selected) {
        ScrollToRect(PlusRect());
    } else if (currentIndex().isValid()) {
        ScrollToRect(visualRect(currentIndex()));
    }
    UpdateHeader();
    UpdateHints();
    viewport()->update();
}

QRect GameHome::PlusRect() const {
    const std::vector<int> visible = VisibleRows();
    const int tile_w = card_side + 2 * CardSidePad;
    const int tile_h = card_side + 2 * ring_pad;
    if (visible.empty()) {
        return {spacing() - horizontalScrollBar()->value(), spacing(), tile_w, tile_h};
    }
    // Exactly a game card's box, one gap after the last game.
    const QRect last = visualRect(model()->index(visible.back(), 0));
    return {last.right() + 1 + spacing(), last.top(), last.width(), last.height()};
}

void GameHome::updateGeometries() {
    QListView::updateGeometries();
    // Room for the "+" tile after the last game.
    QScrollBar* bar = horizontalScrollBar();
    const QRect plus = PlusRect().translated(bar->value(), 0);
    const int needed = std::max(0, plus.right() + 1 + spacing() - viewport()->width());
    if (needed > bar->maximum()) {
        bar->setRange(0, needed);
    }
}

void GameHome::paintEvent(QPaintEvent* event) {
    QListView::paintEvent(event);

    // The "+" tile, as the game list's "Add New Game Directory" entry.
    const QRect rect = PlusRect();
    if (!event->rect().intersects(rect.adjusted(-RingOverflow, 0, RingOverflow, 0))) {
        return;
    }
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF tile =
        QRectF(rect).adjusted(CardSidePad, ring_pad, -CardSidePad, -ring_pad);
    const qreal radius = std::max(8.0, tile.width() * 0.045);
    painter.setPen(QPen(QColor(0x34, 0x36, 0x3c), 2));
    painter.setBrush(QColor(0x18, 0x19, 0x1d));
    painter.drawRoundedRect(tile, radius, radius);

    const QPointF c = tile.center() - QPointF(0, tile.height() * 0.06);
    const qreal arm = tile.width() * 0.12;
    painter.setPen(QPen(QColor(0xc4, 0xc5, 0xca), std::max(4.0, tile.width() * 0.025),
                        Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(c - QPointF(arm, 0), c + QPointF(arm, 0));
    painter.drawLine(c - QPointF(0, arm), c + QPointF(0, arm));

    QFont font = painter.font();
    font.setPixelSize(std::max(13, static_cast<int>(tile.width() / 16)));
    painter.setFont(font);
    painter.setPen(QColor(0x9b, 0x9d, 0xa4));
    painter.drawText(QRectF(tile.left(), c.y() + arm * 1.6, tile.width(), tile.height() * 0.2),
                     Qt::AlignHCenter | Qt::AlignTop, tr("Add Games"));

    if (plus_selected) {
        DrawSelectionRing(&painter, tile, radius, CardsFocused(), Pulse());
    }
}

void GameHome::BeginReorder() {
    if (plus_selected || !currentIndex().isValid()) {
        return;
    }
    SetRow(Row::Cards);
    reordering = true;
    reorder_original.clear();
    for (const int row : VisibleRows()) {
        reorder_original.push_back(
            model()->index(row, 0).data(GameListItemPath::ProgramIdRole).toULongLong());
    }
    setFocus(Qt::OtherFocusReason);
    UpdateHeader();
    UpdateHints();
    viewport()->update();
}

void GameHome::FinishReorder(bool keep) {
    if (!reordering) {
        return;
    }
    reordering = false;
    if (!keep) {
        // Put every game back where it was.
        auto* game_model = qobject_cast<QStandardItemModel*>(model());
        const u64 selected =
            currentIndex().data(GameListItemPath::ProgramIdRole).toULongLong();
        int target = 0;
        for (const u64 program_id : reorder_original) {
            for (int row = target; game_model != nullptr && row < game_model->rowCount(); ++row) {
                const QModelIndex index = game_model->index(row, 0);
                if (!IsGame(index) ||
                    index.data(GameListItemPath::ProgramIdRole).toULongLong() != program_id) {
                    continue;
                }
                if (row != target) {
                    const QList<QStandardItem*> items = game_model->takeRow(row);
                    game_model->insertRow(target, items);
                }
                ++target;
                break;
            }
        }
        SelectProgram(selected);
    }
    UpdateHeader();
    UpdateHints();
    viewport()->update();
    emit ReorderFinished(keep);
}

void GameHome::SelectProgram(u64 program_id) {
    if (model() == nullptr) {
        return;
    }
    for (int i = 0; i < model()->rowCount(); ++i) {
        const QModelIndex index = model()->index(i, 0);
        if (index.data(GameListItemPath::ProgramIdRole).toULongLong() == program_id &&
            IsGame(index)) {
            SelectPlus(false);
            setCurrentIndex(index);
            return;
        }
    }
}

void GameHome::MoveCurrentGame(int delta) {
    auto* game_model = qobject_cast<QStandardItemModel*>(model());
    const QModelIndex current = currentIndex();
    if (game_model == nullptr || !current.isValid()) {
        return;
    }
    const std::vector<int> visible = VisibleRows();
    const auto it = std::find(visible.begin(), visible.end(), current.row());
    if (it == visible.end()) {
        return;
    }
    const int position = static_cast<int>(it - visible.begin());
    const int next = std::clamp(position + delta, 0, static_cast<int>(visible.size()) - 1);
    if (next == position) {
        return;
    }
    // Taking the row out shifts the ones after it; inserting at the target's old row puts the
    // game after the target when moving right and before it when moving left.
    const int from = current.row();
    const int to = visible[static_cast<size_t>(next)];
    const QList<QStandardItem*> items = game_model->takeRow(from);
    game_model->insertRow(to, items);
    setCurrentIndex(game_model->index(to, 0));
    ScrollToRect(visualRect(currentIndex()));
    sound->Play();
}

void GameHome::StepSelection(int delta) {
    SetRow(Row::Cards);
    if (reordering) {
        MoveCurrentGame(delta);
        return;
    }
    MoveSelection(delta);
}

void GameHome::ActivateCurrent() {
    if (plus_selected) {
        emit AddGamesRequested();
        return;
    }
    const QModelIndex current = currentIndex();
    if (current.isValid() && !isRowHidden(current.row())) {
        emit activated(current);
    }
}

void GameHome::OpenOptions() {
    if (plus_selected) {
        return;
    }
    const QModelIndex current = currentIndex();
    if (current.isValid() && !isRowHidden(current.row())) {
        emit customContextMenuRequested(visualRect(current).center());
    }
}

void GameHome::OpenMenu(int index) {
    if (index < 0 || index >= static_cast<int>(pills.size()) || !pill_menus[index]) {
        return;
    }
    focused_pill = index;
    SetRow(Row::Menus);
    QPushButton* pill = pills[static_cast<size_t>(index)];
    pill_menus[static_cast<size_t>(index)]->exec(pill->mapToGlobal(QPoint(0, pill->height() + 6)));
    // Actions in the menu can switch the view away, or start a game.
    if (isVisible()) {
        setFocus(Qt::OtherFocusReason);
    }
}

void GameHome::ActivateChip(int index) {
    if (index < 0 || index >= static_cast<int>(chips.size())) {
        return;
    }
    const HomeChip& chip = chips[static_cast<size_t>(index)];
    if (chip.adjust && !chip.activate) {
        adjusting = !adjusting;
        UpdateFocusStyles();
        UpdateHints();
        return;
    }
    if (chip.activate) {
        chip.activate();
    }
    RefreshChips();
}

void GameHome::scrollTo(const QModelIndex& index, ScrollHint) {
    if (index.isValid()) {
        ScrollToRect(visualRect(index));
    }
}

void GameHome::ScrollToRect(const QRect& rect) {
    QScrollBar* bar = horizontalScrollBar();
    const int look_ahead = card_side / 2;
    const int value = scroll_animation->state() == QAbstractAnimation::Running
                          ? scroll_animation->endValue().toInt()
                          : bar->value();
    const int shown = value - bar->value();
    int target = value;
    if (rect.right() - shown + look_ahead > viewport()->width()) {
        target = value + rect.right() - shown + look_ahead - viewport()->width();
    } else if (rect.left() - shown - look_ahead < 0) {
        target = value + rect.left() - shown - look_ahead;
    }
    target = std::clamp(target, 0, bar->maximum());
    if (target == value) {
        return;
    }
    scroll_animation->stop();
    scroll_animation->setStartValue(bar->value());
    scroll_animation->setEndValue(target);
    scroll_animation->start();
}

void GameHome::currentChanged(const QModelIndex& current, const QModelIndex& previous) {
    QListView::currentChanged(current, previous);
    if (current.isValid()) {
        scrollTo(current);
    }
    UpdateHeader();
    viewport()->update();
}

void GameHome::mousePressEvent(QMouseEvent* event) {
    FinishReorder(true);
    SetRow(Row::Cards);
    if (PlusRect().contains(event->position().toPoint())) {
        SelectPlus(true);
        if (event->button() == Qt::LeftButton) {
            emit AddGamesRequested();
        }
        event->accept();
        return;
    }
    if (indexAt(event->position().toPoint()).isValid()) {
        SelectPlus(false);
    }
    QListView::mousePressEvent(event);
}

void GameHome::keyPressEvent(QKeyEvent* event) {
    const int key = event->key();
    const bool confirm = key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space;
    const bool back = key == Qt::Key_Escape || key == Qt::Key_Backspace;

    if (reordering) {
        if (key == Qt::Key_Left) {
            MoveCurrentGame(-1);
        } else if (key == Qt::Key_Right) {
            MoveCurrentGame(1);
        } else if (key == Qt::Key_PageUp) {
            MoveCurrentGame(-5);
        } else if (key == Qt::Key_PageDown) {
            MoveCurrentGame(5);
        } else if (confirm) {
            FinishReorder(true);
        } else if (back) {
            FinishReorder(false);
        }
        event->accept();
        return;
    }

    switch (row) {
    case Row::Cards:
        if (key == Qt::Key_Left) {
            MoveSelection(-1);
        } else if (key == Qt::Key_Right) {
            MoveSelection(1);
        } else if (key == Qt::Key_PageUp) {
            MoveSelection(-5);
        } else if (key == Qt::Key_PageDown) {
            MoveSelection(5);
        } else if (key == Qt::Key_Home) {
            MoveSelection(-(1 << 20));
        } else if (key == Qt::Key_End) {
            MoveSelection(1 << 20);
        } else if (key == Qt::Key_Up) {
            if (!pills.empty()) {
                SetRow(Row::Menus);
                sound->Play();
            }
        } else if (key == Qt::Key_Down) {
            if (!chip_buttons.empty()) {
                SetRow(Row::Chips);
                sound->Play();
            }
        } else if (confirm) {
            ActivateCurrent();
        } else if (key == Qt::Key_Menu || key == Qt::Key_X || key == Qt::Key_Plus) {
            // The controller's X and + arrive as the menu key; on a keyboard X and + work too.
            OpenOptions();
        } else if (back) {
            // Nothing to go back to from the cards.
        } else {
            QListView::keyPressEvent(event);
            return;
        }
        break;
    case Row::Chips: {
        const int count = static_cast<int>(chip_buttons.size());
        if (adjusting) {
            const HomeChip& chip = chips[static_cast<size_t>(focused_chip)];
            if (key == Qt::Key_Left || key == Qt::Key_Down) {
                chip.adjust(-1);
                RefreshChips();
            } else if (key == Qt::Key_Right || key == Qt::Key_Up) {
                chip.adjust(1);
                RefreshChips();
            } else if (confirm || back) {
                adjusting = false;
                UpdateFocusStyles();
                UpdateHints();
            }
        } else if (key == Qt::Key_Left || key == Qt::Key_Right) {
            const int next = std::clamp(focused_chip + (key == Qt::Key_Left ? -1 : 1), 0,
                                        count - 1);
            if (next != focused_chip) {
                focused_chip = next;
                UpdateFocusStyles();
                sound->Play();
            }
        } else if (key == Qt::Key_Up || back) {
            SetRow(Row::Cards);
            sound->Play();
        } else if (confirm) {
            ActivateChip(focused_chip);
        }
        break;
    }
    case Row::Menus: {
        const int count = static_cast<int>(pills.size());
        if (key == Qt::Key_Left || key == Qt::Key_Right) {
            const int next = std::clamp(focused_pill + (key == Qt::Key_Left ? -1 : 1), 0,
                                        count - 1);
            if (next != focused_pill) {
                focused_pill = next;
                UpdateFocusStyles();
                sound->Play();
            }
        } else if (key == Qt::Key_Down || back) {
            SetRow(Row::Cards);
            sound->Play();
        } else if (confirm) {
            OpenMenu(focused_pill);
        }
        break;
    }
    }
    event->accept();
}
