// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "core/partitionplan.h"

#include <algorithm>
#include <cstdlib>

#include "core/util.h"

namespace cutefish::installer {

uint64_t PartitionPlanner::suggestedSwapBytes(uint64_t memoryBytes)
{
    if (memoryBytes == 0)
        memoryBytes = 4ull * 1024 * 1024 * 1024;
    uint64_t swap = memoryBytes / 3;
    swap = std::min<uint64_t>(swap, 8ull * 1024 * 1024 * 1024);
    swap = std::max<uint64_t>(swap, 2ull * 1024 * 1024 * 1024);
    return swap;
}

std::string PartitionPlanner::shortName(const std::string& kname)
{
    return kname.compare(0, 5, "/dev/") == 0 ? kname.substr(5) : kname;
}

std::string PartitionPlanner::partitionPath(const std::string& diskKname, int number)
{
    // The kernel writes an extra p between the disk and the number whenever
    // the disk's own name ends in a digit, as nvme0n1's does.
    std::string path = diskKname;
    if (!path.empty() && isdigit(unsigned(path.back())))
        path += 'p';
    path += std::to_string(number);
    return path;
}

PartitionPlanner::Layout PartitionPlanner::eraseLayout(const InstallPlan& plan, bool efi,
                                                       uint64_t memoryBytes, uint64_t diskBytes)
{
    static constexpr uint64_t kEspBytes = 512ull * 1024 * 1024;
    static constexpr uint64_t kBiosBootBytes = 2ull * 1024 * 1024;

    Layout layout;
    int next = 1;

    // What the machine keeps its loaders on, first on the disk: a 512 MB
    // EFI system partition on a UEFI machine, 2 MB of BIOS boot partition
    // where GRUB keeps itself on a GPT disk of a BIOS one.
    layout.firmwareIsEsp = efi;
    layout.firmwarePartition = partitionPath(plan.device(), next++);
    layout.firmwareBytes = efi ? kEspBytes : kBiosBootBytes;

    // The swap asked for — "auto" sizes it to the machine.
    if (plan.swap() == "auto") {
        layout.swapPartition = partitionPath(plan.device(), next++);
        layout.swapBytes = suggestedSwapBytes(memoryBytes);
    } else if (plan.swap() != "none") {
        layout.swapPartition = partitionPath(plan.device(), next++);
        layout.swapBytes = strtoull(plan.swap().c_str(), nullptr, 10) * 1024ull * 1024;
    }

    // The root, the rest of the disk. When the disk's own size is not known
    // yet, the root is told nothing and takes what there is.
    layout.rootPartition = partitionPath(plan.device(), next);
    if (diskBytes > layout.firmwareBytes + layout.swapBytes)
        layout.rootBytes = diskBytes - layout.firmwareBytes - layout.swapBytes;
    return layout;
}

std::vector<Command> PartitionPlanner::partitionCommands(const InstallPlan& plan, const Layout& layout,
                                                         bool efi)
{
    std::vector<Command> commands;
    const std::string device = plan.device();

    // Everything the disk had, wiped: the file systems' own signatures first,
    // then the partition table itself.
    commands.push_back({"wipefs", {"-a", device}});
    commands.push_back({"sgdisk", {"--zap-all", device}});

    // The partitions, in the order the layout numbered them, each with the
    // type code the system expects of it: ef00 the EFI system partition,
    // ef02 the BIOS boot partition, 8200 swap, 8300 the linux file systems.
    int number = 1;
    const auto make = [&](const std::string&, uint64_t bytes, const char* type, const char* name) {
        const std::string size = "+" + std::to_string(bytes / (1024ull * 1024)) + "M";
        commands.push_back({"sgdisk",
                            {"-n" + std::to_string(number) + ":0:" + size,
                             "-t" + std::to_string(number) + ":" + type,
                             "-c" + std::to_string(number) + ":" + name, device}});
        ++number;
    };

    if (efi) {
        if (layout.firmwarePartition.empty())
            return commands;
        make(layout.firmwarePartition, layout.firmwareBytes, "ef00", "EFI");
    } else {
        make(layout.firmwarePartition, layout.firmwareBytes, "ef02", "BIOS-BOOT");
    }
    if (!layout.swapPartition.empty())
        make(layout.swapPartition, layout.swapBytes, "8200", "swap");
    if (!layout.rootPartition.empty()) {
        // The last partition takes the rest of the disk: its end is the
        // default, not a size.
        commands.push_back({"sgdisk",
                            {"-n" + std::to_string(number) + ":0:0",
                             "-t" + std::to_string(number) + ":8300",
                             "-c" + std::to_string(number) + ":cutefish", device}});
        ++number;
    }

    // The kernel is told to read the table again before anything is made on
    // the partitions it names.
    commands.push_back({"partprobe", {device}});
    commands.push_back({"udevadm", {"settle", "--timeout=5", "--exit-if-exists=" + layout.rootPartition}});
    return commands;
}

std::vector<Command> PartitionPlanner::formatCommands(const InstallPlan& /*plan*/, const Layout& layout,
                                                      bool efi)
{
    std::vector<Command> commands;
    if (efi && !layout.firmwarePartition.empty())
        commands.push_back({"mkfs.vfat", {"-F", "32", "-n", "EFI", layout.firmwarePartition}});
    if (!layout.swapPartition.empty())
        commands.push_back({"mkswap", {"-L", "swap", layout.swapPartition}});
    if (!layout.rootPartition.empty())
        commands.push_back({"mkfs.ext4", {"-F", "-L", "cutefish", layout.rootPartition}});
    return commands;
}

std::vector<Command> PartitionPlanner::manualFormatCommands(const InstallPlan& plan)
{
    std::vector<Command> commands;
    for (const PlannedPartition& partition : plan.partitions()) {
        if (!partition.format || partition.kname.empty())
            continue;
        if (partition.fileSystem == "ext4")
            commands.push_back({"mkfs.ext4", {"-F", partition.kname}});
        else if (partition.fileSystem == "btrfs")
            commands.push_back({"mkfs.btrfs", {"-f", partition.kname}});
        else if (partition.fileSystem == "xfs")
            commands.push_back({"mkfs.xfs", {"-f", partition.kname}});
        else if (partition.fileSystem == "vfat")
            commands.push_back({"mkfs.vfat", {"-F", "32", partition.kname}});
        else if (partition.fileSystem == "swap")
            commands.push_back({"mkswap", {partition.kname}});
    }
    return commands;
}

} // namespace cutefish::installer
