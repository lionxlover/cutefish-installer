// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "app/widgets.h"

#include "core/util.h"
#include "ui/color_id.h"
#include "ui/color_mixer.h"
#include "ui/color_provider.h"
#include "ui/icon.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/theme.h"
#include "ui/view.h"
#include "ui/window.h"

using namespace cutefish::ui;
using cutefish::installer::DiskDevice;
using cutefish::installer::Partition;

namespace installer {

// The settings app's own tokens, on its 4 px grid: the page margin, a
// card's padding, and the heights of its rows.
static constexpr double kCardPadding = 16;
static constexpr double kCardSpacing = 12;
static constexpr double kRowHeight = 44;
static constexpr double kTwoLineRowHeight = 56;

// The sidebar's own: its items, their tiles, and the air around them.
static constexpr double kSidebarMargin = 12;
static constexpr double kSidebarItemHeight = 36;
static constexpr double kSidebarIconTile = 24;
static constexpr double kSidebarItemPadding = (kSidebarItemHeight - kSidebarIconTile) / 2;

Card::Card(bool padded)
{
    if (padded) {
        setPadding({kCardPadding, 12, kCardPadding, 12});
        setSpacing(kCardSpacing);
    } else {
        setSpacing(0);
    }
    setLayoutHints({.fillWidth = true});
}

void Card::paint(Painter& painter)
{
    const Theme& theme = Theme::current();
    painter.drawRoundedRect(localRect(), {theme.bigRadius, 0, themeCorners()}, theme.secondBackground);
}

PageStack::PageStack()
{
    setLayoutHints({.fillWidth = true, .fillHeight = true});
    setClipsChildren(true);
}

void PageStack::replace(std::unique_ptr<View> page)
{
    if (m_page)
        takeChild(m_page);
    m_page = page.get();
    addChild(std::move(page));
    if (geometry().width > 0 && geometry().height > 0)
        m_page->setGeometry({0, 0, geometry().width, geometry().height});
    requestLayout();
}

void PageStack::layout()
{
    if (m_page)
        m_page->setGeometry({0, 0, geometry().width, geometry().height});
}

void PageStack::paint(Painter& painter)
{
    // Opaque, as the settings app's pages are: only the sidebar keeps the
    // window's own backdrop showing through.
    painter.fillRect(localRect(), Theme::current().background);
}

SectionLabel::SectionLabel(std::string text, bool wrap)
{
    setMargins({kCardPadding, 0, kCardPadding, 0});
    setLayoutHints({.fillWidth = true});
    m_label = addChild<Label>(std::move(text));
    m_label->setWrap(wrap);
    m_label->setColor(ColorId::DisabledText);
    m_label->setLayoutHints({.fillWidth = true});
}

Section::Section(std::string title)
{
    // The label sits tight above what it names, 8 px of it, so the group
    // reads as one thing amid the page's wider rhythm.
    setSpacing(8);
    if (!title.empty())
        addChild<SectionLabel>(std::move(title));
}

// The rows of a card share their neighbours: the separator between two rows
// and the corners a press lights up depend on the rows around them.
detail::RowNeighbours detail::rowNeighbours(const View* self)
{
    detail::RowNeighbours around;
    const View* parent = self->parent();
    if (!parent)
        return around;
    bool seen = false;
    for (const auto& child : parent->children()) {
        auto* row = dynamic_cast<AbstractRow*>(child.get());
        if (!row || !child->isVisible())
            continue;
        if (child.get() == self) {
            seen = true;
            continue;
        }
        if (!seen) {
            around.first = false;
        } else {
            around.last = false;
            around.next = row;
            break;
        }
    }
    return around;
}

IconTextRow::IconTextRow(std::string icon, double size)
    : m_icon(std::move(icon))
    , m_iconSize(size)
{
    m_row = addChild<RowLayout>();
    m_row->setSpacing(12);
    m_row->setPadding({kCardPadding, 0, kCardPadding, 0});

    if (!m_icon.empty()) {
        View* iconHolder = m_row->addChild<ThemeIcon>(m_icon, m_iconSize);
        iconHolder->setLayoutHints({.verticalAlignment = Alignment::Center});
    }
    ColumnLayout* text = m_row->addChild<ColumnLayout>();
    text->setSpacing(2);
    text->setLayoutHints({.fillWidth = true});
    m_titleLabel = text->addChild<Label>();
    m_subtitleLabel = text->addChild<Label>();
    m_titleLabel->setElide(Label::Elide::Right);
    m_subtitleLabel->setElide(Label::Elide::Right);
    m_subtitleLabel->setFontSize(Theme::current().smallFontSize());
    m_subtitleLabel->setColor(ColorId::DisabledText);

    LayoutHints hints{.fillWidth = true};
    hints.preferredHeight = kRowHeight;
    setLayoutHints(hints);
    setTracksHover(true);
}

void IconTextRow::setTitle(const std::string& title)
{
    m_title = title;
    updateLabels();
}

void IconTextRow::setSubtitle(const std::string& subtitle)
{
    m_subtitle = subtitle;
    updateLabels();
}

void IconTextRow::updateLabels()
{
    m_titleLabel->setText(m_title);
    m_subtitleLabel->setText(m_subtitle);
    m_subtitleLabel->setVisible(!m_subtitle.empty());
    // A row with a second line is the settings app's taller row.
    LayoutHints hints = layoutHints();
    const double height = m_subtitle.empty() ? kRowHeight : kTwoLineRowHeight;
    if (hints.preferredHeight != height) {
        hints.preferredHeight = height;
        setLayoutHints(hints);
    }
    requestLayout();
}

void IconTextRow::setCheckable(bool checkable)
{
    if (m_checkable == checkable)
        return;
    m_checkable = checkable;
    if (checkable) {
        m_check = m_row->addChild<Icon>("check", 16);
        m_check->setLayoutHints({.verticalAlignment = Alignment::Center});
    } else {
        takeChild(m_check);
        m_check = nullptr;
    }
    updateLabels();
    setChecked(false);
}

void IconTextRow::setChecked(bool checked)
{
    m_checked = checked;
    if (m_check)
        m_check->setVisible(m_checked);
}

SizeF IconTextRow::sizeHint() const
{
    return {0, m_subtitle.empty() ? kRowHeight : kTwoLineRowHeight};
}

detail::RowNeighbours IconTextRow::neighbours() const
{
    return detail::rowNeighbours(this);
}

void IconTextRow::pointerEnter()
{
    m_hovered = true;
}

void IconTextRow::pointerLeave()
{
    m_hovered = false;
    m_pressed = false;
    requestPaint();
}

void IconTextRow::pointerPress(const PointerEvent&)
{
    m_pressed = true;
    requestPaint();
    if (detail::RowNeighbours around = neighbours(); around.next && around.next->rowPressed())
        requestPaint();
}

void IconTextRow::pointerRelease(const PointerEvent& event)
{
    const bool wasPressed = m_pressed;
    m_pressed = false;
    requestPaint();
    if (wasPressed && localRect().contains(event.position) && onClicked)
        onClicked();
}

void IconTextRow::paint(Painter& painter)
{
    const Theme& theme = Theme::current();
    const detail::RowNeighbours around = neighbours();
    const double width = geometry().width;
    const double height = geometry().height;

    // One shape only, as the settings app's rows: overlapping translucent
    // pieces would darken where they meet, so the corners that meet a
    // neighbour are pushed outside the row.
    if (m_pressed) {
        const double radius = theme.bigRadius;
        const double top = around.first ? 0 : -radius;
        const double bottom = around.last ? height : height + radius;
        painter.save();
        painter.clip({0, 0, width, height});
        painter.drawRoundedRect({0, top, width, bottom - top}, {radius, 0, themeCorners()},
                                color(ColorId::Text).withAlpha(theme.darkMode ? 0.05 : 0.15));
        painter.restore();
    }

    if (m_separatorVisible && !around.last && !m_pressed && !(around.next && around.next->rowPressed())) {
        const double pixel = 1 / painter.scale();
        painter.drawRoundedRect({kCardPadding, height - pixel, width - 2 * kCardPadding, pixel},
                                {}, color(ColorId::Separator));
    }
}

FormRow::FormRow(std::string text, std::string subtitle, double minHeight)
    : m_minHeight(subtitle.empty() ? minHeight : kTwoLineRowHeight)
{
    m_row = addChild<RowLayout>();
    m_row->setSpacing(12);
    m_row->setPadding({kCardPadding, 0, kCardPadding, 0});

    ColumnLayout* textColumn = m_row->addChild<ColumnLayout>();
    textColumn->setSpacing(2);
    textColumn->setLayoutHints({.fillWidth = true});
    m_label = textColumn->addChild<Label>(std::move(text));
    m_label->setLayoutHints({.verticalAlignment = Alignment::Center});
    if (!subtitle.empty()) {
        m_subtitleLabel = textColumn->addChild<Label>(std::move(subtitle));
        m_subtitleLabel->setElide(Label::Elide::Right);
        m_subtitleLabel->setFontSize(Theme::current().smallFontSize());
        m_subtitleLabel->setColor(ColorId::DisabledText);
    }

    LayoutHints hints{.fillWidth = true};
    hints.preferredHeight = m_minHeight;
    setLayoutHints(hints);
}

void FormRow::layout()
{
    m_row->setGeometry(localRect());
}

detail::RowNeighbours FormRow::neighbours() const
{
    return detail::rowNeighbours(this);
}

void FormRow::paint(Painter& painter)
{
    const detail::RowNeighbours around = neighbours();
    if (m_separatorVisible && !around.last) {
        const double width = geometry().width;
        const double pixel = 1 / painter.scale();
        painter.drawRoundedRect({kCardPadding, geometry().height - pixel, width - 2 * kCardPadding, pixel},
                                {}, color(ColorId::Separator));
    }
}

// One sidebar entry: its coloured tile and its name, with the press and the
// current backgrounds the settings app's entries carry, and the small check
// of a step already walked on the tile's own corner.
class SidebarItem : public View
{
public:
    SidebarItem(const Sidebar::Entry& entry)
        : m_entry(entry)
    {
        m_glyph = addChild<Icon>(m_entry.icon, 16);
        m_glyph->setColor(Color::rgb(0xFFFFFF));
        m_title = addChild<Label>(m_entry.title);
        m_title->setFontSize(8 * 4 / 3);
        m_title->setVerticallyCentered(true);
        m_done = addChild<Icon>("check", 10);
        m_done->setColor(Color::rgb(0xFFFFFF));
        m_done->setVisible(false);
    }

    void setCurrent(bool current)
    {
        if (current == m_current)
            return;
        m_current = current;
        requestPaint();
    }

    void setDone(bool done)
    {
        if (done == m_doneState)
            return;
        m_doneState = done;
        m_done->setVisible(done);
        requestPaint();
    }

    bool acceptsPointer() const override { return true; }

    std::function<void()> onClicked;

protected:
    void layout() override
    {
        const Theme& theme = Theme::current();
        const double height = geometry().height;
        m_tile = {kSidebarItemPadding, (height - kSidebarIconTile) / 2, kSidebarIconTile, kSidebarIconTile};
        m_glyph->setGeometry({m_tile.x + (m_tile.width - 16) / 2, m_tile.y + (m_tile.height - 16) / 2, 16, 16});
        const double x = kSidebarItemPadding + kSidebarIconTile + 8;
        m_title->setGeometry({x, 0, std::max(0.0, geometry().width - x), height});
        const double badge = 12;
        m_done->setGeometry({m_tile.x + m_tile.width - badge + 2, m_tile.y - badge / 2 + 2, badge, badge});
        const Window* window = this->window();
        const bool active = !window || window->isActive();
        m_title->setColor(active ? theme.text : theme.disabledText);
    }

    void paint(Painter& painter) override
    {
        const Theme& theme = Theme::current();
        // The tile: the entry's own colour, lit from above, white glyph on it.
        painter.fillRoundedRect(m_tile, 6, LinearGradient::vertical(m_tile, m_entry.tint.lighter(1.15), m_entry.tint));
        // The step done: the check in its own circle, on the tile's corner.
        if (m_doneState) {
            RectF badge = {m_tile.x + m_tile.width - 12, m_tile.y - 4, 14, 14};
            painter.fillRoundedRect(badge, 7, accent::green);
        }
        // The current entry carries the list's highlight under its own fill.
        if (m_current)
            painter.drawRoundedRect(localRect(), {theme.mediumRadius, 0, themeCorners()},
                                     theme.text.withAlpha(0.05));
        if (m_pressed || m_current)
            painter.drawSmoothRectangle(localRect(), theme.mediumRadius,
                                        m_pressed ? color(ColorId::ItemPressed)
                                                  : color(ColorId::Text).withAlpha(theme.darkMode ? 0.1 : 0.05));
    }

    void pointerPress(const PointerEvent& event) override
    {
        if (event.button != MouseButton::Left)
            return;
        m_pressed = true;
        requestPaint();
    }

    void pointerRelease(const PointerEvent& event) override
    {
        if (!m_pressed)
            return;
        m_pressed = false;
        requestPaint();
        if (localRect().contains(event.position) && onClicked)
            onClicked();
    }

private:
    Sidebar::Entry m_entry;
    Icon* m_glyph = nullptr;
    Label* m_title = nullptr;
    Icon* m_done = nullptr;
    RectF m_tile;
    bool m_current = false;
    bool m_doneState = false;
    bool m_pressed = false;
};

Sidebar::Sidebar()
{
    m_title = addChild<Label>(std::string());
    m_title->setFontSize(12 * 4 / 3);
    m_title->setVerticallyCentered(true);
    setLayoutHints({.fillHeight = true});
}

void Sidebar::setTitle(std::string title)
{
    m_title->setText(std::move(title));
}

void Sidebar::setEntries(std::vector<Entry> entries)
{
    m_entries = std::move(entries);
    for (View* item : m_items)
        takeChild(item);
    m_items.clear();
    for (const Entry& entry : m_entries) {
        SidebarItem* item = addChild<SidebarItem>(entry);
        item->onClicked = [this, item] {
            if (onActivated) {
                for (size_t i = 0; i < m_items.size(); ++i) {
                    if (m_items[i] == item) {
                        onActivated(int(i));
                        break;
                    }
                }
            }
        };
        m_items.push_back(item);
    }
    setCurrentIndex(m_current);
    requestLayout();
}

void Sidebar::setCurrentIndex(int index)
{
    m_current = index;
    for (size_t i = 0; i < m_items.size(); ++i)
        static_cast<SidebarItem*>(m_items[i])->setCurrent(int(i) == index);
}

void Sidebar::setDoneUpTo(int index)
{
    m_doneUpTo = index;
    for (size_t i = 0; i < m_items.size(); ++i)
        static_cast<SidebarItem*>(m_items[i])->setDone(int(i) <= index);
}

void Sidebar::layout()
{
    const double width = geometry().width;
    // The name of the tool, over its steps.
    const double titleLeft = kSidebarMargin + kSidebarItemPadding;
    m_title->setColor(Theme::current().text);
    m_title->setGeometry({titleLeft, 8, std::max(0.0, width - 2 * titleLeft), 32});
    double y = 48;
    const double itemWidth = width - 2 * kSidebarMargin;
    for (View* item : m_items) {
        item->setGeometry({kSidebarMargin, y, itemWidth, kSidebarItemHeight});
        y += kSidebarItemHeight + 2;
    }
}

void Sidebar::paint(Painter& painter)
{
    // The settings app's own sidebar plate: the background, darkened a step
    // and half see-through, over the window's frosted backdrop.
    const Theme& theme = Theme::current();
    painter.fillRect(localRect(),
                     (theme.darkMode ? color(ColorId::Background).lighter(1.5)
                                     : color(ColorId::Background).darker(1.05))
                         .withAlpha(0.7));
    (void)m_doneUpTo;
}

// -- the disk's own picture ------------------------------------------------------

DiskBar::DiskBar()
{
    setLayoutHints({.fillWidth = true});
}

// The stretches the bar shows: the partitions the disk holds now, or the
// layout the erase mode would lay on it, each with its own share of the
// whole width. Names ride in their own Label children, as the toolkit's own
// rows carry theirs.
struct DiskBar::Stretch
{
    double from = 0;
    double width = 0;
    Color fill;
    std::string label;
};

std::vector<DiskBar::Stretch> DiskBar::stretches() const
{
    std::vector<Stretch> made;
    if (!m_disk || m_disk->sizeBytes == 0)
        return made;

    const double inside = 8;
    const double usable = geometry().width - 2 * inside;
    const auto share = [&](uint64_t bytes) {
        return std::max(6.0, usable * double(bytes) / double(m_disk->sizeBytes));
    };

    if (m_planShown) {
        // What the erase mode would lay: the firmware's own corner, the
        // swap, and the root the rest — the bar agrees with the plan the
        // engine works from.
        const uint64_t firmware = 512ull * 1024 * 1024;
        const uint64_t memory = cutefish::installer::systemMemory();
        const uint64_t swap = memory == 0 ? (2ull << 30)
                                          : std::min<uint64_t>(std::max<uint64_t>(memory / 3, 2ull << 30),
                                                               8ull << 30);
        double x = inside;
        const double firmwareWidth = share(firmware);
        made.push_back({x, firmwareWidth, accent::blue, "EFI"});
        x += firmwareWidth;
        const double swapWidth = share(swap);
        made.push_back({x, swapWidth, accent::purple, "swap"});
        x += swapWidth;
        made.push_back({x, std::max(6.0, geometry().width - inside - x), accent::blue, "Cutefish OS"});
        return made;
    }

    double x = inside;
    for (const Partition& partition : m_disk->partitions) {
        const double width = share(partition.sizeBytes);
        Color fill = accent::grey;
        if (partition.isEfiCandidate())
            fill = accent::blue;
        else if (partition.isSwap())
            fill = accent::purple;
        else if (!partition.fstype.empty())
            fill = accent::blue.lighter(0.8);
        std::string label = partition.label.empty() ? partition.kname : partition.label;
        made.push_back({x, width, fill, std::move(label)});
        x += width;
    }
    if (x < geometry().width - inside)
        made.push_back({x, geometry().width - inside - x, Color(), "free space"});
    return made;
}

void DiskBar::updateLabels()
{
    for (Label* label : m_labels)
        takeChild(label);
    m_labels.clear();

    for (const Stretch& stretch : stretches()) {
        if (stretch.label.empty() || stretch.width < 64)
            continue;
        Label* label = addChild<Label>(stretch.label);
        label->setFontSize(11 * 4 / 3 * 0.75);
        label->setElide(Label::Elide::Right);
        // The filled stretches carry their names in white; the free space,
        // in the theme's own secondary colour.
        if (stretch.fill.a > 0)
            label->setColor(Color::rgb(0xFFFFFF));
        else
            label->setColor(ColorId::DisabledText);
        m_labels.push_back(label);
    }
    requestLayout();
}

void DiskBar::setDisk(const DiskDevice* disk)
{
    m_disk = disk;
    updateLabels();
}

void DiskBar::setPlanShown(bool shown)
{
    m_planShown = shown;
    updateLabels();
}

void DiskBar::layout()
{
    const std::vector<Stretch> parts = stretches();
    const double height = geometry().height;
    size_t index = 0;
    for (const Stretch& stretch : parts) {
        if (index >= m_labels.size())
            break;
        if (stretch.label.empty() || stretch.width < 64)
            continue;
        Label* label = m_labels[index++];
        label->setGeometry({stretch.from + 8, 0, std::max(0.0, stretch.width - 16), height});
        label->setVerticallyCentered(true);
    }
}

void DiskBar::paint(Painter& painter)
{
    const Theme& theme = Theme::current();
    const RectF rect = localRect();
    const double radius = theme.mediumRadius;

    // The bar's own plate, and the stretches over it.
    painter.drawRoundedRect(rect, {radius, 0, themeCorners()}, theme.secondBackground);
    painter.save();
    painter.clip(rect);
    for (const Stretch& stretch : stretches()) {
        if (stretch.fill.a == 0)
            continue;
        painter.save();
        painter.clip({stretch.from, 0, stretch.width, rect.height});
        painter.fillRect({stretch.from, 0, stretch.width, rect.height}, stretch.fill);
        painter.restore();
    }
    painter.restore();
}

// -- the badge and the log ----------------------------------------------------------

CheckBadge::CheckBadge(double side)
    : m_side(side)
{
    setLayoutHints({.alignment = Alignment::Center});
    m_check = addChild<Icon>("check", side * 0.5);
    m_check->setColor(Color::rgb(0xFFFFFF));
    m_check->setLayoutHints({.alignment = Alignment::Center});
}

void CheckBadge::layout()
{
    m_check->setGeometry({(m_side - m_side * 0.5) / 2, (m_side - m_side * 0.5) / 2,
                          m_side * 0.5, m_side * 0.5});
}

void CheckBadge::paint(Painter& painter)
{
    painter.fillRoundedRect(localRect(), m_side / 2, accent::blue);
}

LogView::LogView()
{
    setLayoutHints({.fillWidth = true, .fillHeight = true});
    setClipsChildren(true);

    m_scroll = addChild<ScrollView>();
    m_scroll->setLayoutHints({.fillWidth = true, .fillHeight = true});
    m_column = m_scroll->setContent<ColumnLayout>();
    m_column->setSpacing(0);
    m_column->setPadding({kCardPadding, 8, kCardPadding, 8});
    m_column->setLayoutHints({.fillWidth = true});
}

void LogView::addLine(const std::string& line)
{
    Label* label = m_column->addChild<Label>(line);
    label->setFontSize(Theme::current().smallFontSize());
    label->setFontFamily("monospace");
    label->setColor(ColorId::DisabledText);
    label->setWrapAnywhere(true);
    label->setLayoutHints({.fillWidth = true});
    m_lines.push_back(label);
    trim();
    requestLayout();
}

void LogView::clear()
{
    for (Label* label : m_lines)
        m_column->takeChild(label);
    m_lines.clear();
    requestLayout();
}

void LogView::trim()
{
    // The record stays long enough to read, short enough to stay quick.
    while (m_lines.size() > 240) {
        m_column->takeChild(m_lines.front());
        m_lines.erase(m_lines.begin());
    }
}

void LogView::layout()
{
    m_scroll->setGeometry(localRect());
    // The work's own end stays in view, as a terminal keeps its newest line.
    const double contentHeight = m_scroll->contentSize().height;
    if (contentHeight > 0)
        m_scroll->setScrollPosition({0, contentHeight});
}

void LogView::paint(Painter& painter)
{
    const Theme& theme = Theme::current();
    painter.drawRoundedRect(localRect(), {theme.bigRadius, 0, themeCorners()}, theme.secondBackground);
}

} // namespace installer
