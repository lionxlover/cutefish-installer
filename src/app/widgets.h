// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/disks.h"
#include "ui/cell_layout.h"
#include "ui/layout.h"
#include "ui/scroll_view.h"
#include "ui/view.h"

namespace cutefish::ui {
class Icon;
class Label;
class Painter;
} // namespace cutefish::ui

namespace installer {

// The base of a row of a card: what paints the separator between rows and
// the press highlight needs to know its neighbours, whatever the row holds.
class AbstractRow
{
public:
    virtual ~AbstractRow() = default;
    virtual bool rowPressed() const = 0;
};

namespace detail {

struct RowNeighbours
{
    bool first = true;
    bool last = true;
    AbstractRow* next = nullptr;
};

detail::RowNeighbours rowNeighbours(const cutefish::ui::View* self);

} // namespace detail

// A card: a column that paints the rounded panel the settings app paints
// behind its groups — the theme's second background, the big radius.
class Card : public cutefish::ui::ColumnLayout
{
public:
    explicit Card(bool padded = true);

protected:
    void paint(cutefish::ui::Painter& painter) override;
};

// One page at a time: the window swaps the wizard's steps in and out.
class PageStack : public cutefish::ui::View
{
public:
    PageStack();

    void replace(std::unique_ptr<cutefish::ui::View> page);
    cutefish::ui::View* page() const { return m_page; }

protected:
    void layout() override;
    void paint(cutefish::ui::Painter& painter) override;

private:
    cutefish::ui::View* m_page = nullptr;
};

// The settings app's section label: secondary text, inset as a card's own
// text is, so every edge on the page lines up.
class SectionLabel : public cutefish::ui::Column
{
public:
    explicit SectionLabel(std::string text = {}, bool wrap = false);

    cutefish::ui::Label* label() const { return m_label; }

private:
    cutefish::ui::Label* m_label = nullptr;
};

// A titled group on a page: its label above its content, set tight.
class Section : public cutefish::ui::ColumnLayout
{
public:
    explicit Section(std::string title = {});
};

// A row with a theme icon beside a two-line column. The rows of a card sit
// against one another, parted by separators, and light up under the press
// the way the settings app's rows do. Checkable, it carries the list's own
// choice instead.
class IconTextRow : public cutefish::ui::View, public AbstractRow
{
public:
    // `icon` is a freedesktop name, such as "drive-harddisk"; an empty one
    // leaves the space out. `size` is the icon's side.
    IconTextRow(std::string icon, double size = 24);

    void setTitle(const std::string& title);
    void setSubtitle(const std::string& subtitle);
    const std::string& title() const { return m_title; }

    cutefish::ui::Label* titleLabel() const { return m_titleLabel; }
    cutefish::ui::Label* subtitleLabel() const { return m_subtitleLabel; }
    void setCheckable(bool checkable);
    bool isChecked() const { return m_checked; }
    void setChecked(bool checked);

    void setSeparatorVisible(bool visible) { m_separatorVisible = visible; }
    bool rowPressed() const override { return m_pressed; }

    cutefish::ui::SizeF sizeHint() const override;

    std::function<void()> onClicked;

protected:
    bool acceptsPointer() const override { return true; }
    void pointerEnter() override;
    void pointerLeave() override;
    void pointerPress(const cutefish::ui::PointerEvent& event) override;
    void pointerRelease(const cutefish::ui::PointerEvent& event) override;
    void paint(cutefish::ui::Painter& painter) override;

private:
    detail::RowNeighbours neighbours() const;

    void updateLabels();

    cutefish::ui::RowLayout* m_row = nullptr;
    cutefish::ui::Label* m_titleLabel = nullptr;
    cutefish::ui::Label* m_subtitleLabel = nullptr;
    cutefish::ui::View* m_check = nullptr;
    std::string m_icon;
    std::string m_title;
    std::string m_subtitle;
    double m_iconSize;
    bool m_checkable = false;
    bool m_checked = false;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_separatorVisible = true;
};

// A settings-style row: a label with a control at its end, and the row's
// separator below. The row itself is not clickable — its control is the
// interaction. `subtitle` adds the smaller, fainter line the privacy rows
// carry under their own names.
class FormRow : public cutefish::ui::View, public AbstractRow
{
public:
    explicit FormRow(std::string text, std::string subtitle = {}, double minHeight = 44);

    cutefish::ui::Label* label() const { return m_label; }

    // Puts the control at the row's end and hands it back, centred.
    template<typename T, typename... Args>
    T* setTrailing(Args&&... args)
    {
        auto view = std::make_unique<T>(std::forward<Args>(args)...);
        T* trailing = view.get();
        cutefish::ui::LayoutHints hints = trailing->layoutHints();
        hints.verticalAlignment = cutefish::ui::Alignment::Center;
        trailing->setLayoutHints(hints);
        m_row->addChild(std::move(view));
        return trailing;
    }

    void setSeparatorVisible(bool visible) { m_separatorVisible = visible; }
    bool rowPressed() const override { return false; }

    cutefish::ui::SizeF sizeHint() const override { return {0, m_minHeight}; }

protected:
    void layout() override;
    void paint(cutefish::ui::Painter& painter) override;

private:
    detail::RowNeighbours neighbours() const;

    cutefish::ui::RowLayout* m_row = nullptr;
    cutefish::ui::Label* m_label = nullptr;
    cutefish::ui::Label* m_subtitleLabel = nullptr;
    double m_minHeight;
    bool m_separatorVisible = true;
};

// The sidebar: the settings app's own navigation, one entry a step of the
// wizard. An entry's icon is a white glyph on a rounded tile of its own
// colour; the entry the window stands on carries the list's highlight, and
// the steps already walked carry the small check of a thing done.
class Sidebar : public cutefish::ui::View
{
public:
    struct Entry
    {
        std::string title;
        std::string icon;
        cutefish::ui::Color tint;
    };

    Sidebar();

    void setTitle(std::string title);
    void setEntries(std::vector<Entry> entries);
    void setCurrentIndex(int index);
    int currentIndex() const { return m_current; }
    // The steps already walked, shown with the small check; -1 for none.
    void setDoneUpTo(int index);

    std::function<void(int index)> onActivated;

    cutefish::ui::SizeF sizeHint() const override { return {230, 0}; }

protected:
    void layout() override;
    void paint(cutefish::ui::Painter& painter) override;

private:
    cutefish::ui::Label* m_title = nullptr;
    std::vector<Entry> m_entries;
    std::vector<cutefish::ui::View*> m_items;
    int m_current = -1;
    int m_doneUpTo = -1;
};

// The disk's own picture: the partitions it holds as coloured stretches of
// its whole width, sized to their own share of its space, the rest the free
// space that is left. The installer's own page shows what a disk holds
// before anything is done to it, and what the erase mode would lay instead.
class DiskBar : public cutefish::ui::View
{
public:
    DiskBar();

    void setDisk(const cutefish::installer::DiskDevice* disk);
    // The plan the bar shows over the disk: the stretches the installer
    // would lay on it, with the root taking the rest.
    void setPlanShown(bool shown);

    cutefish::ui::SizeF sizeHint() const override { return {0, 52}; }

protected:
    void layout() override;
    void paint(cutefish::ui::Painter& painter) override;

private:
    struct Stretch;
    std::vector<Stretch> stretches() const;
    void updateLabels();

    const cutefish::installer::DiskDevice* m_disk = nullptr;
    bool m_planShown = false;
    std::vector<cutefish::ui::Label*> m_labels;
};

// The big check of a thing done: the accent-filled circle with the white
// glyph, the size the last page holds it at.
class CheckBadge : public cutefish::ui::View
{
public:
    explicit CheckBadge(double side = 80);

    cutefish::ui::SizeF sizeHint() const override { return {m_side, m_side}; }

protected:
    void layout() override;
    void paint(cutefish::ui::Painter& painter) override;

private:
    double m_side;
    cutefish::ui::Icon* m_check = nullptr;
};

// The install's own record: a card that holds a line a row, newest at its
// end and kept to a sane number, showing the end of itself as the work goes
// on.
class LogView : public cutefish::ui::View
{
public:
    LogView();

    void addLine(const std::string& line);
    void clear();

    cutefish::ui::SizeF sizeHint() const override { return {0, 0}; }

protected:
    void layout() override;
    void paint(cutefish::ui::Painter& painter) override;

private:
    void trim();

    cutefish::ui::ScrollView* m_scroll = nullptr;
    cutefish::ui::ColumnLayout* m_column = nullptr;
    std::vector<cutefish::ui::Label*> m_lines;
};

} // namespace installer
