// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/util.h"

namespace cutefish::installer {

// One partition the manual mode takes as it finds it: a mount point it is
// given, and whether the installer makes a new filesystem on it.
struct PlannedPartition
{
    std::string kname;    // "/dev/sda2"
    std::string mountPoint; // "/", "/boot/efi", "swap", "" leaves it aside
    std::string fileSystem; // "ext4", "btrfs", "xfs", "vfat", "swap"
    bool format = false;
};

// The privacy switches the pages ask after and the installed system keeps.
struct PrivacySettings
{
    bool location = false;
    bool diagnostics = false;
    bool tailored = false;
};

// Everything an installation needs, in the shape both the window and the
// command line read and write: the JSON file they hand the engine. The plan
// is checked before anything touches a disk, and the summary page is built
// from the same model, so what is shown is always what is done.
class InstallPlan
{
public:
    // "erase" wipes the chosen disk and lays out the whole system on it;
    // "manual" leaves the partition table alone and mounts what it is told.
    enum class Mode {
        Erase,
        Manual,
    };

    // Where the files of the installed system come from: the running live
    // session itself, or a squashfs of an image mounted for the install, or
    // a plain directory (an unpacked image, an OEM tree).
    static std::string liveSource();
    static std::string squashfsSource(const std::string& path);
    static std::string directorySource(const std::string& path);
    static bool sourceKind(const std::string& source, std::string* kind, std::string* argument);

    InstallPlan() = default;

    // The plan as JSON, and the plan from JSON; false and a reason when the
    // JSON is not a plan the installer can follow.
    Json toJson() const;
    bool fromJson(const Json& json, std::string* error = nullptr);

    // Every rule the engine leans on, checked up front: the names a system
    // takes, the tables' own codes, and a layout that can actually boot.
    // `error` names the first rule the plan breaks, with no words wasted.
    bool validate(std::string* error = nullptr) const;

    // -- the disk -----------------------------------------------------------
    Mode mode() const { return m_mode; }
    void setMode(Mode mode) { m_mode = mode; }
    const std::string& device() const { return m_device; }
    void setDevice(const std::string& device) { m_device = device; }

    // The swap choice of the erase mode: "auto" asks for a partition the
    // machine's own size suggests, "none" asks for none, a number in
    // megabytes asks for exactly that.
    const std::string& swap() const { return m_swap; }
    void setSwap(const std::string& swap) { m_swap = swap; }

    const std::vector<PlannedPartition>& partitions() const { return m_partitions; }
    void setPartitions(std::vector<PlannedPartition> partitions) { m_partitions = std::move(partitions); }

    // -- the machine's own settings ------------------------------------------
    const std::string& language() const { return m_language; }
    void setLanguage(const std::string& language) { m_language = language; }
    const std::string& keyboard() const { return m_keyboard; }
    void setKeyboard(const std::string& keyboard) { m_keyboard = keyboard; }
    const std::string& timeZone() const { return m_timeZone; }
    void setTimeZone(const std::string& timeZone) { m_timeZone = timeZone; }

    // -- the user -------------------------------------------------------------
    const std::string& fullName() const { return m_fullName; }
    void setFullName(const std::string& name) { m_fullName = name; }
    const std::string& userName() const { return m_userName; }
    void setUserName(const std::string& name) { m_userName = name; }
    const std::string& password() const { return m_password; }
    void setPassword(const std::string& password) { m_password = password; }
    const std::string& hostName() const { return m_hostName; }
    void setHostName(const std::string& name) { m_hostName = name; }

    // -- the rest ---------------------------------------------------------------
    const PrivacySettings& privacy() const { return m_privacy; }
    void setPrivacy(const PrivacySettings& privacy) { m_privacy = privacy; }

    const std::string& source() const { return m_source; }
    void setSource(const std::string& source) { m_source = source; }

    // The firmware mode the rules follow — the machine's own unless told
    // otherwise, as the tests do for both modes.
    bool firmwareMode() const { return m_firmwareOverride ? *m_firmwareOverride : isEfiSystem(); }
    void setFirmwareOverride(std::optional<bool> efi) { m_firmwareOverride = efi; }

private:
    Mode m_mode = Mode::Erase;
    std::string m_device;
    std::string m_swap = "auto";
    std::vector<PlannedPartition> m_partitions;

    std::string m_language = "en_US";
    std::string m_keyboard = "us";
    std::string m_timeZone = "UTC";

    std::string m_fullName;
    std::string m_userName;
    std::string m_password;
    std::string m_hostName = "cutefish-pc";

    PrivacySettings m_privacy;
    std::string m_source;
    std::optional<bool> m_firmwareOverride;
};

} // namespace cutefish::installer
