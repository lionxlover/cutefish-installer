// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/installplan.h"
#include "core/util.h"

namespace cutefish::installer {

// The layout the installer lays on the disk, already worked out: which
// partition is which, what file systems they take, and the exact commands
// that make them. The engine runs what the planner made; the tests check
// what the planner planned, without a disk anywhere near.
class PartitionPlanner
{
public:
    // What the layout gives every part of the system.
    struct Layout
    {
        std::string rootPartition;      // "/dev/sda2"
        // Where the machine keeps its loaders: the EFI system partition on a
        // UEFI machine, the BIOS boot partition GRUB keeps itself in on a
        // BIOS one — mounted only in the UEFI case, formatted only there.
        std::string firmwarePartition;
        bool firmwareIsEsp = false;
        std::string swapPartition;      // "" without swap
        uint64_t rootBytes = 0;
        uint64_t firmwareBytes = 0;
        uint64_t swapBytes = 0;
    };

    // The swap size the machine's own memory suggests: a third of it, under
    // 8 GB but never below 2 GB. `memoryBytes` 0 falls back to 4 GB.
    static uint64_t suggestedSwapBytes(uint64_t memoryBytes);

    // The erase mode's layout on `plan.device()`: a partition table made new
    // from the first sector, with the parts the machine needs — a 512 MB ESP
    // where UEFI looks for its loaders, a 2 MB BIOS boot partition where GRUB
    // keeps itself on a GPT disk, the swap asked for, and the root the rest.
    // `diskBytes` keeps the disk's own size; 0 lets the layout ask the kernel.
    static Layout eraseLayout(const InstallPlan& plan, bool efi, uint64_t memoryBytes,
                              uint64_t diskBytes = 0);

    // The commands that make the erase mode's layout: the table wiped and
    // written first, the file systems made after.
    static std::vector<Command> partitionCommands(const InstallPlan& plan, const Layout& layout, bool efi);
    static std::vector<Command> formatCommands(const InstallPlan& plan, const Layout& layout, bool efi);

    // The manual mode: the table is left alone, and only the partitions the
    // plan says to format are made new file systems.
    static std::vector<Command> manualFormatCommands(const InstallPlan& plan);

    // "sda2" from "/dev/sda2" — the kernel's own name, for sgdisk's numbering.
    static std::string shortName(const std::string& kname);
    // The nth partition of a disk: "/dev/sda" + "1" → "/dev/sda1";
    // "/dev/nvme0n1" + "12" → "/dev/nvme0n1p12".
    static std::string partitionPath(const std::string& diskKname, int number);
};

} // namespace cutefish::installer
