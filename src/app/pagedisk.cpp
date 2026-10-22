// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

// The disk step: which disk, and how. The disks are read from the machine
// itself — the medium the live session runs from never among them — and the
// page shows what the chosen one holds, as a bar of its own partitions or
// as the layout the erase mode would lay on it. The manual mode takes the
// partitions as they are, and asks only where each one mounts.

#include "app/installerwindow.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/i18n.h"
#include "core/disks.h"
#include "core/tables.h"
#include "core/util.h"
#include "ui/check_box.h"
#include "ui/combo_box.h"
#include "ui/label.h"
#include "ui/layout.h"

using namespace cutefish::ui;
using namespace cutefish::installer;
using cutefish::i18n::tr;

namespace installer {

void InstallerWindow::scanDisks()
{
    std::string error;
    m_disks = DiskScanner::scan(&error);
    if (!m_disks.empty() && (m_diskIndex < 0 || size_t(m_diskIndex) >= m_disks.size())) {
        // The first disk is the natural choice until the user picks another.
        m_diskIndex = 0;
        // A disk too small for a system is not chosen by itself.
        for (size_t i = 0; i < m_disks.size(); ++i) {
            if (m_disks[i].sizeBytes >= 16ull * 1024 * 1024 * 1024) {
                m_diskIndex = int(i);
                break;
            }
        }
    }
    if (m_diskIndex >= int(m_disks.size()))
        m_diskIndex = -1;
}

void InstallerWindow::pickDisk(int row)
{
    m_diskIndex = row;
    m_manualPartitions.clear();
    buildDiskPage();
    refreshFooter();
}

void InstallerWindow::pickInstallMode(bool manual)
{
    m_manualMode = manual;
    m_manualPartitions.clear();
    buildDiskPage();
    refreshFooter();
}

// The manual layout's own roles, as the rows offer them.
static const char* const kRoles[] = {"Not used", "/ \xe2\x80\x94 the system",
                                     "/boot/efi \xe2\x80\x94 the loaders", "swap"};

void InstallerWindow::rebuildManualRows()
{
    m_manualRows.clear();
    if (!m_manualMode || m_diskIndex < 0 || m_diskIndex >= int(m_disks.size()))
        return;

    for (const cutefish::installer::Partition& partition : m_disks[size_t(m_diskIndex)].partitions) {
        ManualRow row;
        row.kname = partition.kname;
        row.fstype = partition.fstype;
        m_manualRows.push_back(std::move(row));
    }
}

void InstallerWindow::manualRowChanged()
{
    // The rows' own state, gathered back into the plan the review shows and
    // the engine follows.
    m_manualPartitions.clear();
    for (size_t i = 0; i < m_manualRows.size(); ++i) {
        const ManualRow& row = m_manualRows[i];
        if (!row.role)
            continue;
        const int choice = row.role->currentIndex();
        if (choice <= 0)
            continue;
        cutefish::installer::PlannedPartition planned;
        planned.kname = row.kname;
        planned.mountPoint = choice == 1 ? "/" : (choice == 2 ? "/boot/efi" : "swap");
        planned.fileSystem = choice == 2 ? "vfat"
            : (choice == 3 ? "swap"
                           : (row.fstype == "ext4" || row.fstype == "btrfs" || row.fstype == "xfs"
                                  ? row.fstype : "ext4"));
        planned.format = row.format ? row.format->isChecked() : false;
        m_manualPartitions.push_back(std::move(planned));
    }
}

bool InstallerWindow::diskChoiceReady(std::string* why)
{
    if (m_diskIndex < 0 || m_diskIndex >= int(m_disks.size())) {
        if (why)
            *why = tr("Choose a disk for Cutefish OS.");
        return false;
    }
    const DiskDevice& disk = m_disks[size_t(m_diskIndex)];
    if (disk.sizeBytes < 16ull * 1024 * 1024 * 1024) {
        if (why)
            *why = tr("That disk is too small for Cutefish OS, which needs at least 16 GB.");
        return false;
    }
    if (m_manualMode) {
        bool hasRoot = false;
        bool hasEsp = false;
        for (const auto& planned : m_manualPartitions) {
            if (planned.mountPoint == "/")
                hasRoot = true;
            if (planned.mountPoint == "/boot/efi")
                hasEsp = true;
        }
        if (!hasRoot) {
            if (why)
                *why = tr("Choose which partition the system itself mounts at /.");
            return false;
        }
        if (cutefish::installer::isEfiSystem() && !hasEsp) {
            if (why)
                *why = tr("This machine boots the UEFI way: choose a partition at /boot/efi as well.");
            return false;
        }
    }
    return true;
}

void InstallerWindow::buildDiskPage()
{
    PageFrame frame = makePage(tr("Disk"),
                               tr("Choose the disk Cutefish OS is installed on. The installer never offers "
                                  "the disk the live session itself runs from."));

    if (m_disks.empty()) {
        Card* card = frame.content->addChild<Card>();
        Label* none = card->addChild<Label>(tr("No disk this machine can install to was found. Plug one in "
                                               "and choose this step again."));
        none->setWrap(true);
        none->setLayoutHints({.fillWidth = true});
        none->setColor(ColorId::DisabledText);
        return;
    }

    // -- the disk ------------------------------------------------------------
    Section* diskSection = frame.content->addChild<Section>(tr("The disk"));
    Card* diskCard = diskSection->addChild<Card>(false);
    std::vector<IconTextRow*> rows;
    for (size_t i = 0; i < m_disks.size(); ++i) {
        const DiskDevice& disk = m_disks[i];
        IconTextRow* row = diskCard->addChild<IconTextRow>("drive-harddisk", 24);
        row->setTitle(disk.model.empty() ? disk.kname : disk.model);
        std::string subtitle = humanReadableSize(disk.sizeBytes);
        if (disk.removable)
            subtitle += std::string(" \xc2\xb7 ") + tr("removable");
        if (!disk.partitions.empty())
            subtitle += " \xc2\xb7 " + std::to_string(disk.partitions.size()) + " "
                + tr("partitions");
        row->setSubtitle(subtitle);
        row->setCheckable(true);
        row->setChecked(int(i) == m_diskIndex);
        row->onClicked = [this, rows, row, index = int(i)] {
            (void)rows;
            pickDisk(index);
        };
        rows.push_back(row);
    }

    // -- what the chosen disk holds, and what the install would do with it ---
    if (m_diskIndex >= 0) {
        Section* barSection = frame.content->addChild<Section>(tr("The chosen disk"));
        DiskBar* bar = barSection->addChild<DiskBar>();
        bar->setDisk(&m_disks[size_t(m_diskIndex)]);
        bar->setPlanShown(!m_manualMode);
    }

    // -- how -------------------------------------------------------------------
    Section* howSection = frame.content->addChild<Section>(tr("How"));
    Card* howCard = howSection->addChild<Card>(false);
    IconTextRow* erase = howCard->addChild<IconTextRow>("drive-harddisk-root", 22);
    erase->setTitle(tr("Erase the whole disk"));
    erase->setSubtitle(tr("The disk is laid out anew: an EFI corner, swap, and the system on the rest. "
                          "Everything on it is lost."));
    erase->setCheckable(true);
    erase->setChecked(!m_manualMode);
    erase->onClicked = [this] { pickInstallMode(false); };

    IconTextRow* manual = howCard->addChild<IconTextRow>("drive-partition", 22);
    manual->setTitle(tr("Use the partitions as they are"));
    manual->setSubtitle(tr("The partition table is left alone; choose where each partition mounts."));
    manual->setCheckable(true);
    manual->setChecked(m_manualMode);
    manual->onClicked = [this] { pickInstallMode(true); };

    // -- the manual layout's own rows ------------------------------------------
    if (m_manualMode && m_diskIndex >= 0) {
        Section* mountSection = frame.content->addChild<Section>(tr("Mount points"));
        Card* mountCard = mountSection->addChild<Card>(false);
        rebuildManualRows();
        const DiskDevice& disk = m_disks[size_t(m_diskIndex)];
        if (m_manualRows.empty()) {
            Label* none = mountCard->addChild<Label>(tr("This disk holds no partitions to mount."));
            none->setColor(ColorId::DisabledText);
            LayoutHints noneHints{.fillWidth = true};
            noneHints.margins = {16, 12, 16, 12};
            none->setLayoutHints(noneHints);
        }
        for (ManualRow& row : m_manualRows) {
            const cutefish::installer::Partition& partition =
                *std::find_if(disk.partitions.begin(), disk.partitions.end(),
                              [&](const auto& part) { return part.kname == row.kname; });
            FormRow* formRow = mountCard->addChild<FormRow>(
                partition.kname + "  \xc2\xb7  "
                    + (partition.fstype.empty() ? tr("no file system") : partition.fstype)
                    + "  \xc2\xb7  " + humanReadableSize(partition.sizeBytes));
            row.role = formRow->setTrailing<ComboBox>(std::vector<std::string>{
                tr("Not used"), "/ \xe2\x80\x94 the system", "/boot/efi \xe2\x80\x94 the loaders", "swap"});
            // The plan's held choice, back where it was made.
            int current = 0;
            bool formatting = false;
            for (const auto& planned : m_manualPartitions) {
                if (planned.kname == row.kname) {
                    if (planned.mountPoint == "/")
                        current = 1;
                    else if (planned.mountPoint == "/boot/efi")
                        current = 2;
                    else if (planned.mountPoint == "swap")
                        current = 3;
                    formatting = planned.format;
                }
            }
            row.role->setCurrentIndex(current);
            row.format = formRow->setTrailing<CheckBox>(tr("Format"));
            row.format->setChecked(formatting);
            row.format->setVisible(true);

            const auto changed = [this] {
                manualRowChanged();
                refreshFooter();
            };
            row.role->onActivated = [changed](int) { changed(); };
            row.format->onToggled = [changed](bool) { changed(); };
        }
    }
}

} // namespace installer
