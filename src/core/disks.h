// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cutefish::installer {

class Json;

// One partition of a disk, as lsblk reports it.
struct Partition
{
    std::string kname; // "/dev/sda2"
    std::string fstype; // "ext4", "vfat", "swap" …
    std::string label;
    std::string uuid;
    std::string partUuid;
    std::vector<std::string> mountpoints;
    std::string majMin;  // "8:2" — the kernel's own number of the device
    uint64_t sizeBytes = 0;
    int number = 0;     // 2 for "/dev/sda2", 12 for "/dev/nvme0n1p12"

    bool mounted() const;
    // A partition the installer can put a system on, or an ESP, or swap.
    bool isEfiCandidate() const { return fstype == "vfat" || fstype == "fat32" || fstype == "exfat"; }
    bool isSwap() const { return fstype == "swap"; }
};

// One disk the system might be installed to.
struct DiskDevice
{
    std::string kname; // "/dev/sda"
    std::string model; // "Samsung SSD 980 1TB"
    uint64_t sizeBytes = 0;
    bool removable = false;
    std::vector<Partition> partitions;

    // "Samsung SSD 980 · 1.0 TB" — what a page's row shows of a disk.
    std::string description() const;
    // The partitions the disk holds, with their own share of its space.
    uint64_t usedBytes() const;
};

// The machine's disks, read with lsblk and kept apart from the machine the
// session itself runs from. Built so the tests can feed the scan from a file
// instead of a machine, and so a machine without lsblk still shows its disks.
class DiskScanner
{
public:
    // Every disk the system can be installed to, the removable included,
    // the medium the live session runs from never. The pointer takes the
    // reason the disks could not be read, if they could not.
    static std::vector<DiskDevice> scan(std::string* error = nullptr);

    // The device whose filesystem carries `path`, as "/dev/sda2"; empty when
    // it is not one the kernel names by the tree.
    static std::string deviceOfPath(const std::string& path);

    // Feeds the scan from `json` instead of a real lsblk; for the tests and
    // the scripted install. Returns false when the JSON is not lsblk's own.
    static bool parseLsblkJson(const std::string& json, std::vector<DiskDevice>& disks);

private:
    static std::vector<DiskDevice> fromSysfs();
    static void parseBlockDevice(const cutefish::installer::Json& node, std::vector<DiskDevice>& disks);
    static bool isVirtualName(const std::string& kname);
    static bool carriesLiveSession(const DiskDevice& disk);
    static int partitionNumber(const std::string& diskKname, const std::string& partitionKname);
};

} // namespace cutefish::installer
