// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "core/disks.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sstream>
#include <sys/sysmacros.h>

#include "core/json.h"
#include "core/util.h"

namespace cutefish::installer {

bool Partition::mounted() const
{
    for (const std::string& mount : mountpoints) {
        if (!mount.empty())
            return true;
    }
    return false;
}

std::string DiskDevice::description() const
{
    std::string text = model;
    if (text.empty())
        text = "Disk";
    text += " \xc2\xb7 ";
    text += humanReadableSize(sizeBytes);
    return text;
}

uint64_t DiskDevice::usedBytes() const
{
    uint64_t used = 0;
    for (const Partition& partition : partitions)
        used += partition.sizeBytes;
    return used;
}

// -- the scanner ------------------------------------------------------------

std::vector<DiskDevice> DiskScanner::scan(std::string* error)
{
    if (error)
        error->clear();

    // The tests and the scripted installs feed the scan from a file.
    const char* canned = std::getenv("CUTEFISH_INSTALLER_FAKE_LSBLK");
    if (canned && *canned) {
        std::string json;
        std::vector<DiskDevice> disks;
        if (!readFile(canned, json) || !parseLsblkJson(json, disks)) {
            if (error)
                *error = "cannot read the disks from CUTEFISH_INSTALLER_FAKE_LSBLK";
            return {};
        }
        return disks;
    }

    std::vector<std::string> output;
    // util-linux names the column MOUNTPOINTS since 2.33; the older ones know
    // MOUNTPOINT alone, and refuse a column they do not know.
    const char* columns[] = {
        "KNAME,SIZE,TYPE,FSTYPE,LABEL,UUID,PARTUUID,MOUNTPOINTS,MOUNTPOINT,MODEL,VENDOR,RM,MAJ:MIN",
        "KNAME,SIZE,TYPE,FSTYPE,LABEL,UUID,PARTUUID,MOUNTPOINT,MODEL,VENDOR,RM,MAJ:MIN",
    };
    for (const char* columnList : columns) {
        const CommandResult lsblk = runCommand("lsblk",
                                                {"--json", "--bytes", "--paths", "--output", columnList});
        if (lsblk.ok()) {
            std::vector<DiskDevice> disks;
            if (!parseLsblkJson(lsblk.output, disks)) {
                if (error)
                    *error = "cannot make sense of the disks lsblk reports";
                return {};
            }
            return disks;
        }
    }

    std::vector<DiskDevice> disks = fromSysfs();
    if (disks.empty() && error)
        *error = "cannot read the machine's disks";
    return disks;
}

std::string DiskScanner::deviceOfPath(const std::string& path)
{
    // The device the filesystem at `path` lives on, as the mounts table
    // spells it: the mount whose own device number is the path's.
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return {};
    std::string mounts;
    if (!readFile("/proc/self/mounts", mounts))
        return {};
    size_t at = 0;
    while (at < mounts.size()) {
        size_t end = mounts.find('\n', at);
        const std::string line = mounts.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? mounts.size() : end + 1;
        std::string device, mountPoint;
        std::istringstream stream(line);
        stream >> device >> mountPoint;
        struct stat mounted;
        if (!device.empty() && device.compare(0, 5, "/dev/") == 0
            && stat(mountPoint.c_str(), &mounted) == 0 && mounted.st_dev == st.st_dev)
            return device;
    }
    return {};
}

std::vector<DiskDevice> DiskScanner::fromSysfs()
{
    std::vector<DiskDevice> disks;
    for (const std::string& name : listDirectory("/sys/block")) {
        if (isVirtualName(name))
            continue;
        DiskDevice disk;
        disk.kname = "/dev/" + name;
        std::string text;
        if (readFile("/sys/block/" + name + "/size", text))
            disk.sizeBytes = strtoull(text.c_str(), nullptr, 10) * 512ull;
        if (readFile("/sys/block/" + name + "/device/model", text)) {
            while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
                text.pop_back();
            disk.model = text;
        }
        for (const std::string& entry : listDirectory("/sys/block/" + name)) {
            if (entry.compare(0, name.size(), name) != 0 || entry.size() <= name.size())
                continue;
            Partition partition;
            partition.kname = "/dev/" + entry;
            partition.number = partitionNumber(disk.kname, partition.kname);
            if (readFile("/sys/block/" + name + "/" + entry + "/size", text))
                partition.sizeBytes = strtoull(text.c_str(), nullptr, 10) * 512ull;
            disk.partitions.push_back(std::move(partition));
        }
        if (disk.sizeBytes > 0)
            disks.push_back(std::move(disk));
    }
    return disks;
}

bool DiskScanner::isVirtualName(const std::string& kname)
{
    // The kernel's own devices, never an install target: loop devices, the
    // compressed ram disks, device-mapper, floppies and optical media.
    static const char* const prefixes[] = {"loop", "ram", "zram", "dm-", "sr", "md", "fd"};
    for (const char* prefix : prefixes) {
        if (kname.compare(0, strlen(prefix), prefix) == 0)
            return true;
    }
    return false;
}

bool DiskScanner::carriesLiveSession(const DiskDevice& disk)
{
    // The medium a live session runs from — where the image keeps its squashfs
    // — is never offered as the target. Its partitions sit under the paths
    // the live systems of every family mount them at.
    static const char* const prefixes[] = {"/run/archiso", "/run/live", "/run/casper", "/run/initramfs",
                                           "/run/medium", "/run/mount", "/rofs"};
    for (const Partition& partition : disk.partitions) {
        for (const std::string& mount : partition.mountpoints) {
            for (const char* prefix : prefixes) {
                if (mount.compare(0, strlen(prefix), prefix) == 0)
                    return true;
            }
        }
    }
    return false;
}

int DiskScanner::partitionNumber(const std::string& diskKname, const std::string& partitionKname)
{
    if (partitionKname.size() <= diskKname.size())
        return 0;
    std::string rest = partitionKname.substr(diskKname.size());
    if (!rest.empty() && rest[0] == 'p')
        rest.erase(0, 1);
    if (rest.empty() || rest.find_first_not_of("0123456789") != std::string::npos)
        return 0;
    return atoi(rest.c_str());
}

// One device or one partition of the JSON lsblk prints, read into a model.
void DiskScanner::parseBlockDevice(const Json& node, std::vector<DiskDevice>& disks)
{
    using cutefish::installer::Json;
    const std::string type = node.get("type").toString();
    if (type != "disk")
        return;

    DiskDevice disk;
    disk.kname = node.get("kname").toString();
    // The names of the tree without the /dev/ in front: "sda", "nvme0n1".
    if (disk.kname.compare(0, 5, "/dev/") != 0 || isVirtualName(disk.kname.substr(5)))
        return;

    disk.sizeBytes = node.get("size").toUnsigned();
    disk.model = node.get("model").toString();
    const std::string vendor = node.get("vendor").toString();
    if (!disk.model.empty() && !vendor.empty())
        disk.model = vendor + " " + disk.model;
    disk.removable = node.get("rm").isBool() ? node.get("rm").toBool() : node.get("rm").toNumber() != 0;

    // "8:16" — the numbers the kernel knows the device by.
    const std::string majMin = node.get("maj:min").toString();

    // The number the running system itself lives on, as "8:2" — the disk
    // that carries it stays out of the list, whatever partition holds it.
    std::string systemDevice;
    struct stat mounted;
    if (stat("/", &mounted) == 0)
        systemDevice = std::to_string(major(mounted.st_dev)) + ':' + std::to_string(minor(mounted.st_dev));
    const bool isSystemDisk = !systemDevice.empty()
        && (majMin == systemDevice
            || std::any_of(disk.partitions.begin(), disk.partitions.end(), [&](const Partition& partition) {
                   return partition.majMin == systemDevice;
               }));

    if (node.contains("children")) {
        const Json& children = node.get("children");
        for (size_t i = 0; i < children.size(); ++i) {
            const Json& child = children.at(i);
            Partition partition;
            partition.kname = child.get("kname").toString();
            partition.fstype = child.get("fstype").toString();
            partition.label = child.get("label").toString();
            partition.uuid = child.get("uuid").toString();
            partition.partUuid = child.get("partuuid").toString();
            partition.sizeBytes = child.get("size").toUnsigned();
            partition.majMin = child.get("maj:min").toString();
            partition.number = partitionNumber(disk.kname, partition.kname);
            if (child.contains("mountpoints") && child.get("mountpoints").isArray()) {
                const Json& mounts = child.get("mountpoints");
                for (size_t j = 0; j < mounts.size(); ++j) {
                    const std::string mount = mounts.at(j).toString();
                    if (!mount.empty())
                        partition.mountpoints.push_back(mount);
                }
            }
            const std::string oneMount = child.get("mountpoint").toString();
            if (!oneMount.empty() && std::find(partition.mountpoints.begin(), partition.mountpoints.end(),
                                                oneMount) == partition.mountpoints.end())
                partition.mountpoints.push_back(oneMount);
            if (!partition.kname.empty())
                disk.partitions.push_back(std::move(partition));
        }
    }

    // The disk the running system itself is on, and the one the live session
    // keeps its image on, stay out of the list.
    if (isSystemDisk || carriesLiveSession(disk))
        return;
    disks.push_back(std::move(disk));
}

bool DiskScanner::parseLsblkJson(const std::string& json, std::vector<DiskDevice>& disks)
{
    disks.clear();
    cutefish::installer::Json root;
    if (!cutefish::installer::Json::parse(json, root) || !root.isObject())
        return false;
    const cutefish::installer::Json& devices = root.get("blockdevices");
    if (!devices.isArray())
        return false;
    for (size_t i = 0; i < devices.size(); ++i)
        parseBlockDevice(devices.at(i), disks);
    return true;
}

} // namespace cutefish::installer
