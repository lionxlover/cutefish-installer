// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "core/installplan.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "core/tables.h"
#include "core/util.h"

namespace cutefish::installer {

// A member's text, or `fallback` when the member is not a string.
static std::string stringOr(const Json& json, const char* key, const char* fallback)
{
    const Json& value = json.get(key);
    return value.isString() ? value.toString() : std::string(fallback);
}

// -- the source of the files ------------------------------------------------

std::string InstallPlan::liveSource()
{
    return "live";
}

std::string InstallPlan::squashfsSource(const std::string& path)
{
    return "squashfs:" + path;
}

std::string InstallPlan::directorySource(const std::string& path)
{
    return "dir:" + path;
}

bool InstallPlan::sourceKind(const std::string& source, std::string* kind, std::string* argument)
{
    if (source == "live") {
        if (kind)
            *kind = "live";
        if (argument)
            argument->clear();
        return true;
    }
    static const char* const prefixes[] = {"squashfs:", "dir:"};
    for (const char* prefix : prefixes) {
        const size_t length = strlen(prefix);
        if (source.compare(0, length, prefix) == 0 && source.size() > length) {
            if (kind)
                *kind = source.substr(0, length - 1);
            if (argument)
                *argument = source.substr(length);
            return true;
        }
    }
    return false;
}

// -- JSON ---------------------------------------------------------------------

Json InstallPlan::toJson() const
{
    Json json = Json::object();
    json.set("version", 1);
    json.set("mode", m_mode == Mode::Erase ? "erase" : "manual");
    json.set("device", m_device);
    json.set("swap", m_swap);
    Json partitions = Json::array();
    for (const PlannedPartition& partition : m_partitions) {
        Json entry = Json::object();
        entry.set("path", partition.kname);
        entry.set("mount", partition.mountPoint);
        entry.set("fs", partition.fileSystem);
        entry.set("format", partition.format);
        partitions.append(entry);
    }
    json.set("partitions", partitions);
    json.set("language", m_language);
    json.set("keyboard", m_keyboard);
    json.set("timezone", m_timeZone);
    json.set("fullName", m_fullName);
    json.set("username", m_userName);
    json.set("password", m_password);
    json.set("hostname", m_hostName);
    Json privacy = Json::object();
    privacy.set("location", m_privacy.location);
    privacy.set("diagnostics", m_privacy.diagnostics);
    privacy.set("tailored", m_privacy.tailored);
    json.set("privacy", privacy);
    json.set("source", m_source);
    return json;
}

bool InstallPlan::fromJson(const Json& json, std::string* error)
{
    const auto fail = [&](const std::string& reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (!json.isObject())
        return fail("the plan is not a JSON object");
    if (!json.contains("device") || json.get("device").toString().empty())
        return fail("no disk is chosen");
    if (json.contains("mode") && json.get("mode").toString() == "manual")
        m_mode = Mode::Manual;
    else
        m_mode = Mode::Erase;
    m_device = json.get("device").toString();
    m_swap = json.contains("swap") ? json.get("swap").toString() : "auto";
    m_language = stringOr(json, "language", "en_US");
    m_keyboard = stringOr(json, "keyboard", "us");
    m_timeZone = stringOr(json, "timezone", "UTC");
    m_fullName = json.get("fullName").toString();
    m_userName = json.get("username").toString();
    m_password = json.get("password").toString();
    m_hostName = stringOr(json, "hostname", "cutefish-pc");
    m_source = stringOr(json, "source", "live");
    m_partitions.clear();
    if (json.contains("partitions") && json.get("partitions").isArray()) {
        const Json& partitions = json.get("partitions");
        for (size_t i = 0; i < partitions.size(); ++i) {
            const Json& entry = partitions.at(i);
            PlannedPartition partition;
            partition.kname = entry.get("path").toString();
            partition.mountPoint = entry.get("mount").toString();
            partition.fileSystem = stringOr(entry, "fs", "ext4");
            partition.format = entry.get("format").toBool();
            m_partitions.push_back(std::move(partition));
        }
    }
    const Json& privacy = json.get("privacy");
    if (privacy.isObject()) {
        m_privacy.location = privacy.get("location").toBool();
        m_privacy.diagnostics = privacy.get("diagnostics").toBool();
        m_privacy.tailored = privacy.get("tailored").toBool();
    }
    return true;
}

// -- the rules ----------------------------------------------------------------

bool InstallPlan::validate(std::string* error) const
{
    const auto fail = [&](const std::string& reason) {
        if (error)
            *error = reason;
        return false;
    };

    if (m_device.empty())
        return fail("no disk is chosen");
    if (m_device.compare(0, 5, "/dev/") != 0)
        return fail("the disk is not a device of /dev");
    if (!knownLanguage(m_language))
        return fail("the language is not one the installer knows");
    if (!knownKeyboard(m_keyboard))
        return fail("the keyboard layout is not one the installer knows");
    if (!knownZone(m_timeZone))
        return fail("the time zone is not one the installer knows");
    if (!validUsernameName(m_userName))
        return fail("the username is not one the system takes");
    if (m_password.empty())
        return fail("the account needs a password");
    if (!validHostName(m_hostName))
        return fail("the hostname is not one the network takes");
    if (!sourceKind(m_source, nullptr, nullptr))
        return fail("the source of the system's files is not one the installer knows");

    if (m_mode == Mode::Manual) {
        // A manual layout boots only with a root, and a UEFI machine needs
        // the partition it keeps its loaders on.
        bool hasRoot = false;
        bool hasEsp = false;
        for (const PlannedPartition& partition : m_partitions) {
            if (partition.kname.empty() || partition.kname.compare(0, 5, "/dev/") != 0)
                return fail("a partition of the layout is not a device of /dev");
            if (partition.mountPoint.empty())
                continue;
            if (partition.mountPoint == "/")
                hasRoot = true;
            else if (partition.mountPoint == "/boot/efi")
                hasEsp = true;
            else if (partition.mountPoint == "swap")
                ; // swap is the layout's own to take or leave
            else
                return fail("the mount point " + partition.mountPoint + " is not one the installer sets up");
            if (partition.fileSystem != "ext4" && partition.fileSystem != "btrfs"
                && partition.fileSystem != "xfs" && partition.fileSystem != "vfat"
                && partition.fileSystem != "swap")
                return fail("the file system " + partition.fileSystem + " is not one the installer makes");
            if (partition.mountPoint == "/boot/efi" && partition.fileSystem != "vfat")
                return fail("the EFI partition needs a FAT file system");
            if (partition.mountPoint == "swap" && partition.fileSystem != "swap")
                return fail("the swap partition needs the swap file system");
        }
        if (!hasRoot)
            return fail("the layout needs a partition mounted at /");
        if (firmwareMode() && !hasEsp)
            return fail("this machine boots the UEFI way: the layout needs a partition at /boot/efi");
    } else {
        // The erase mode's swap choice: "auto", "none", or a size in
        // megabytes, whole and sane.
        if (m_swap != "auto" && m_swap != "none") {
            if (m_swap.empty() || m_swap.find_first_not_of("0123456789") != std::string::npos)
                return fail("the swap size is not one the installer takes");
            const unsigned long size = strtoul(m_swap.c_str(), nullptr, 10);
            if (size < 256 || size > 65536)
                return fail("the swap size is not one the installer takes");
        }
    }
    return true;
}

} // namespace cutefish::installer
