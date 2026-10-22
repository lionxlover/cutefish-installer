// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "core/installengine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace cutefish::installer {

// The steps, with the share of the whole run each one holds. The copy is
// what an installation mostly is, so it takes most of the bar.
const std::vector<InstallEngine::Step>& InstallEngine::steps()
{
    static const std::vector<Step> table = {
        {"prepare", 0, 2},    {"partition", 2, 10},   {"format", 10, 16},
        {"mount", 16, 19},    {"copy", 19, 64},       {"fstab", 64, 67},
        {"configure", 67, 84}, {"bootloader", 84, 93}, {"firstboot", 93, 95},
        {"cleanup", 95, 100},
    };
    return table;
}

std::vector<std::string> InstallEngine::stepOrder()
{
    std::vector<std::string> order;
    for (const Step& step : steps())
        order.push_back(step.id);
    return order;
}

std::string InstallEngine::stepLabel(const std::string& id)
{
    if (id == "prepare")
        return "Checking the machine and the plan";
    if (id == "partition")
        return "Laying out the disk";
    if (id == "format")
        return "Making the file systems";
    if (id == "mount")
        return "Mounting the new system";
    if (id == "copy")
        return "Copying the system's files";
    if (id == "fstab")
        return "Writing the mounts table";
    if (id == "configure")
        return "Configuring the new system";
    if (id == "bootloader")
        return "Installing the bootloader";
    if (id == "firstboot")
        return "Setting the first boot up";
    if (id == "cleanup")
        return "Finishing up";
    return id;
}

InstallEngine::InstallEngine(Hooks hooks)
    : m_hooks(std::move(hooks))
{
}

// -- reporting ---------------------------------------------------------------

void InstallEngine::emit(const Json& event)
{
    if (m_hooks.event)
        m_hooks.event(event);
}

void InstallEngine::emitStep(const std::string& id)
{
    Json event = Json::object();
    event.set("event", "step");
    event.set("id", id);
    event.set("label", stepLabel(id));
    emit(event);
}

void InstallEngine::emitProgress(double percent, const std::string& message)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    // The bar only ever moves forward: a step that reports nothing after one
    // that reported much does not pull it back.
    if (percent < m_lastPercent)
        percent = m_lastPercent;
    if (int(percent * 10) == int(m_lastPercent * 10) && m_lastPercent >= 0)
        return;
    m_lastPercent = percent;
    Json event = Json::object();
    event.set("event", "progress");
    event.set("percent", percent);
    if (!message.empty())
        event.set("message", message);
    emit(event);
}

void InstallEngine::emitLog(const std::string& message)
{
    Json event = Json::object();
    event.set("event", "log");
    event.set("message", message);
    emit(event);
}

bool InstallEngine::checkCancelled()
{
    return m_hooks.cancelled && m_hooks.cancelled();
}

void InstallEngine::beginStep(const std::string& id)
{
    for (const Step& step : steps()) {
        if (id == step.id) {
            m_step = step;
            break;
        }
    }
    emitStep(id);
    stepAt(0.0);
}

void InstallEngine::stepAt(double fraction, const std::string& message)
{
    emitProgress(m_step.from + (m_step.to - m_step.from) * fraction, message);
}

bool InstallEngine::fail(const std::string& message)
{
    m_error = message;
    return false;
}

// -- the run -------------------------------------------------------------------

InstallEngine::Outcome InstallEngine::run(const InstallPlan& plan, bool dryRun)
{
    m_dryRun = dryRun;
    m_error.clear();
    m_failedStep.clear();
    m_commandLog.clear();
    m_lastPercent = -1;
    m_started = std::chrono::steady_clock::now();

    Json start = Json::object();
    start.set("event", "start");
    start.set("device", plan.device());
    start.set("mode", plan.mode() == InstallPlan::Mode::Erase ? "erase" : "manual");
    start.set("dry_run", dryRun);
    emit(start);

    struct Part
    {
        const char* id;
        bool (InstallEngine::* work)(const InstallPlan&, bool);
    };
    static const Part parts[] = {
        {"prepare", &InstallEngine::doPrepare},       {"partition", &InstallEngine::doPartition},
        {"format", &InstallEngine::doFormat},         {"mount", &InstallEngine::doMount},
        {"copy", &InstallEngine::doCopy},             {"fstab", &InstallEngine::doFstab},
        {"configure", &InstallEngine::doConfigure},   {"bootloader", &InstallEngine::doBootloader},
        {"firstboot", &InstallEngine::doFirstBoot},   {"cleanup", &InstallEngine::doCleanup},
    };

    for (const Part& part : parts) {
        if (checkCancelled()) {
            m_failedStep = part.id;
            Json event = Json::object();
            event.set("event", "cancelled");
            event.set("step", part.id);
            emit(event);
            return Outcome::Cancelled;
        }
        beginStep(part.id);
        if (!(this->*part.work)(plan, dryRun)) {
            m_failedStep = part.id;
            // A stop asked for mid-work is not a failure: it is a stop.
            if (m_error == "cancelled") {
                Json event = Json::object();
                event.set("event", "cancelled");
                event.set("step", part.id);
                emit(event);
                return Outcome::Cancelled;
            }
            Json event = Json::object();
            event.set("event", "error");
            event.set("step", part.id);
            event.set("message", m_error);
            emit(event);
            return Outcome::Failed;
        }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_started);
    Json event = Json::object();
    event.set("event", "finished");
    event.set("elapsed_ms", elapsed.count());
    event.set("device", plan.device());
    event.set("dry_run", dryRun);
    emit(event);
    return Outcome::Finished;
}

// -- the work, step by step ------------------------------------------------------

bool InstallEngine::doPrepare(const InstallPlan& plan, bool dryRun)
{
    std::string problem;
    if (!plan.validate(&problem))
        return fail(problem);
    if (!dryRun && !runningAsRoot())
        return fail("the installation needs the administrator: run it as root or through pkexec");

    m_efi = isEfiSystem();
    emitLog(m_efi ? "the machine boots the UEFI way" : "the machine boots the BIOS way");

    // The disks, read for what the run needs of them: the chosen disk's own
    // size, and the UUIDs the mounts table is written with.
    m_disks = DiskScanner::scan(&problem);
    if (m_disks.empty() && problem.empty())
        problem = "no disk to install to was found";
    if (!problem.empty())
        return fail("cannot read the machine's disks: " + problem);

    DiskDevice disk;
    if (!findDisk(plan.device(), disk))
        return fail("the chosen disk " + plan.device() + " is not among the machine's");
    emitLog("installing to " + disk.description());

    // Where the system's files come from, checked before anything is touched.
    std::string kind, argument;
    InstallPlan::sourceKind(plan.source(), &kind, &argument);
    if (kind == "live") {
        if (!isLiveSession())
            emitLog("note: this session does not look like a live one; the running system itself is copied");
    } else if (kind == "squashfs") {
        if (!fileExists(argument))
            return fail("the image " + argument + " is not there to install from");
    } else if (!isDirectory(argument)) {
        return fail("the folder " + argument + " is not there to install from");
    }

    // The layout, worked out once and followed by every step after.
    if (plan.mode() == InstallPlan::Mode::Erase) {
        m_layout = PartitionPlanner::eraseLayout(plan, m_efi, systemMemory(), disk.sizeBytes);
        emitLog("root on " + m_layout.rootPartition + ", "
                + (m_layout.firmwareIsEsp ? "EFI system partition on " + m_layout.firmwarePartition : "BIOS boot partition on " + m_layout.firmwarePartition)
                + (m_layout.swapPartition.empty() ? ", no swap" : ", swap on " + m_layout.swapPartition));
    } else {
        m_layout = PartitionPlanner::Layout();
        for (const PlannedPartition& planned : plan.partitions()) {
            if (planned.mountPoint.empty())
                continue;
            if (planned.mountPoint == "/")
                m_layout.rootPartition = planned.kname;
            else if (planned.mountPoint == "/boot/efi") {
                m_layout.firmwarePartition = planned.kname;
                m_layout.firmwareIsEsp = true;
            } else if (planned.mountPoint == "swap")
                m_layout.swapPartition = planned.kname;
        }
        emitLog("root on " + m_layout.rootPartition);
    }
    stepAt(1.0);
    return true;
}

bool InstallEngine::doPartition(const InstallPlan& plan, bool dryRun)
{
    if (plan.mode() != InstallPlan::Mode::Erase) {
        emitLog("the partition table is left as it is");
        stepAt(1.0);
        return true;
    }
    std::vector<Command> commands = PartitionPlanner::partitionCommands(plan, m_layout, m_efi);
    const double each = commands.empty() ? 1.0 : 1.0 / double(commands.size());
    for (size_t i = 0; i < commands.size(); ++i) {
        if (!run(commands[i], "laying out the disk"))
            return false;
        stepAt(double(i + 1) * each);
    }
    // The table was read again by the kernel; the disks are read again here,
    // so the file systems made next are known by their own UUIDs.
    if (!dryRun) {
        std::string problem;
        m_disks = DiskScanner::scan(&problem);
        if (!problem.empty())
            emitLog("note: the disks could not be read again (" + problem + ")");
    }
    return true;
}

bool InstallEngine::doFormat(const InstallPlan& plan, bool dryRun)
{
    (void)dryRun; // the commands themselves know what a dry run does
    std::vector<Command> commands;
    if (plan.mode() == InstallPlan::Mode::Erase)
        commands = PartitionPlanner::formatCommands(plan, m_layout, m_efi);
    else
        commands = PartitionPlanner::manualFormatCommands(plan);
    const double each = commands.empty() ? 1.0 : 1.0 / double(commands.size());
    for (size_t i = 0; i < commands.size(); ++i) {
        if (!run(commands[i], "making the file systems"))
            return false;
        stepAt(double(i + 1) * each);
    }
    return true;
}

bool InstallEngine::doMount(const InstallPlan& plan, bool dryRun)
{
    (void)dryRun; // the commands themselves know what a dry run does
    if (plan.mode() == InstallPlan::Mode::Erase) {
        if (!run({"mkdir", {"-p", m_target}}, "the place the new system is built in"))
            return false;
        if (!run({"mount", {m_layout.rootPartition, m_target}}, "mounting the root"))
            return false;
        m_mountedPoints.push_back(m_target);
        if (!m_layout.swapPartition.empty()) {
            runBestEffort({"swapon", {m_layout.swapPartition}}, "turning swap on");
            m_swapOn = true;
        }
        if (m_efi && m_layout.firmwareIsEsp) {
            if (!run({"mkdir", {"-p", m_target + "/boot/efi"}}, "the EFI partition's place"))
                return false;
            if (!run({"mount", {m_layout.firmwarePartition, m_target + "/boot/efi"}},
                     "mounting the EFI partition"))
                return false;
            m_mountedPoints.push_back(m_target + "/boot/efi");
        }
    } else {
        for (const PlannedPartition& planned : plan.partitions()) {
            if (planned.mountPoint.empty() || planned.kname.empty())
                continue;
            if (planned.mountPoint == "swap") {
                runBestEffort({"swapon", {planned.kname}}, "turning swap on");
                m_swapOn = true;
                continue;
            }
            if (!run({"mkdir", {"-p", m_target + planned.mountPoint}}, "the mount's place"))
                return false;
            const std::string doing = "mounting " + planned.mountPoint;
            if (!run({"mount", {planned.kname, m_target + planned.mountPoint}}, doing.c_str()))
                return false;
            m_mountedPoints.push_back(m_target + planned.mountPoint);
        }
    }
    stepAt(1.0);
    return true;
}

// The share of the copy, drawn out of rsync's own progress line: the last
// percent it printed, kept across the pieces the pipe reads.
static int lastPercent(std::string& rolling)
{
    const size_t at = rolling.rfind('%');
    if (at == std::string::npos)
        return -1;
    size_t digits = at;
    size_t end = at;
    while (digits > 0) {
        const char c = rolling[digits - 1];
        if ((c >= '0' && c <= '9') || c == ',')
            --digits;
        else
            break;
    }
    if (digits == end)
        return -1;
    std::string number;
    for (size_t i = digits; i < end; ++i) {
        if (rolling[i] != ',')
            number += rolling[i];
    }
    return atoi(number.c_str());
}

bool InstallEngine::doCopy(const InstallPlan& plan, bool dryRun)
{
    std::string kind, argument;
    InstallPlan::sourceKind(plan.source(), &kind, &argument);
    std::string source;
    if (kind == "live") {
        source = "/";
        emitLog("copying the live system itself onto the new disk");
    } else if (kind == "squashfs") {
        source = "/mnt/source";
        m_sourceMount = source;
        if (!run({"mkdir", {"-p", source}}, "the image's place"))
            return false;
        if (!run({"mount", {"-t", "squashfs", "-o", "loop", argument, source}}, "mounting the image"))
            return false;
        emitLog("copying the image " + argument + " onto the new disk");
    } else {
        source = argument;
        emitLog("copying " + argument + " onto the new disk");
    }

    // What the copy leaves where it was: the kernel's own filesystems, the
    // places a desktop mounts its disks in, and the build's target itself.
    std::vector<std::string> arguments{"-aHAXx", "--numeric-ids", "--info=progress2"};
    static const char* const skip[] = {"/proc/*", "/sys/*", "/dev/*", "/run/*", "/tmp/*",
                                       "/mnt/*", "/media/*", "/lost+found"};
    for (const char* exclude : skip) {
        arguments.push_back("--exclude");
        arguments.push_back(exclude);
    }
    arguments.push_back(source + "/");
    arguments.push_back(m_target + "/");

    if (dryRun) {
        const std::string described = Command{"rsync", arguments}.describe();
        emitLog("$ " + described);
        m_commandLog.push_back(described);
        stepAt(1.0);
        return true;
    }

    std::string rolling;
    int lastSeen = -1;
    const CommandResult result = runCommandWithInput(
        "rsync", arguments, {},
        [&](const std::string& piece) {
            rolling += piece;
            if (rolling.size() > 512)
                rolling.erase(0, rolling.size() - 512);
            const int percent = lastPercent(rolling);
            if (percent >= 0 && percent != lastSeen) {
                lastSeen = percent;
                stepAt(std::min(1.0, percent / 100.0));
            }
        },
        m_hooks.cancelled ? std::function<bool()>([this] { return checkCancelled(); }) : nullptr);

    if (checkCancelled()) {
        emitLog("the copy was stopped");
        return fail("cancelled");
    }
    if (!result.ok()) {
        std::string tail = result.output;
        if (tail.size() > 300)
            tail = "…" + tail.substr(tail.size() - 300);
        return fail("copying the system's files failed: " + tail);
    }
    emitLog("the copy is done");
    return true;
}

// -- the mounts table -----------------------------------------------------------

static std::string fstabEntry(const std::string& kname, const std::string& mountPoint,
                              const std::string& fileSystem, const std::string& options, int pass,
                              const std::vector<DiskDevice>& disks)
{
    std::string uuid;
    std::string partUuid;
    for (const DiskDevice& disk : disks) {
        for (const Partition& partition : disk.partitions) {
            if (partition.kname == kname) {
                uuid = partition.uuid;
                partUuid = partition.partUuid;
            }
        }
    }
    std::string what;
    if (!uuid.empty())
        what = "UUID=" + uuid;
    else if (!partUuid.empty())
        what = "PARTUUID=" + partUuid;
    else
        what = kname;
    return what + "  " + mountPoint + "  " + fileSystem + "  " + options + "  0  "
        + std::to_string(pass) + "\n";
}

std::string InstallEngine::generateFstab(const PartitionPlanner::Layout& layout, bool efi,
                                         const std::vector<DiskDevice>& disks)
{
    std::string text = "# /etc/fstab: the mounts the system starts with, written by the Cutefish installer\n";
    if (!layout.rootPartition.empty())
        text += fstabEntry(layout.rootPartition, "/", "ext4", "rw,noatime", 1, disks);
    if (efi && layout.firmwareIsEsp && !layout.firmwarePartition.empty())
        text += fstabEntry(layout.firmwarePartition, "/boot/efi", "vfat", "rw,umask=0077", 2, disks);
    if (!layout.swapPartition.empty())
        text += fstabEntry(layout.swapPartition, "none", "swap", "sw", 0, disks);
    return text;
}

std::string InstallEngine::generateManualFstab(const std::vector<PlannedPartition>& planned,
                                               const std::vector<DiskDevice>& disks)
{
    std::string text = "# /etc/fstab: the mounts the system starts with, written by the Cutefish installer\n";
    for (const PlannedPartition& partition : planned) {
        if (partition.mountPoint.empty() || partition.kname.empty())
            continue;
        if (partition.mountPoint == "swap")
            text += fstabEntry(partition.kname, "none", "swap", "sw", 0, disks);
        else if (partition.mountPoint == "/")
            text += fstabEntry(partition.kname, "/", partition.fileSystem, "rw,noatime", 1, disks);
        else if (partition.mountPoint == "/boot/efi")
            text += fstabEntry(partition.kname, "/boot/efi", "vfat", "rw,umask=0077", 2, disks);
        else
            text += fstabEntry(partition.kname, partition.mountPoint, partition.fileSystem, "rw", 2, disks);
    }
    return text;
}

bool InstallEngine::doFstab(const InstallPlan& plan, bool dryRun)
{
    (void)dryRun; // the write itself knows what a dry run does
    std::string text;
    if (plan.mode() == InstallPlan::Mode::Erase)
        text = generateFstab(m_layout, m_efi, m_disks);
    else
        text = generateManualFstab(plan.partitions(), m_disks);
    emitLog("the mounts table:\n" + text);
    if (!writeTargetFile("/etc/fstab", text, "the mounts table"))
        return false;
    stepAt(1.0);
    return true;
}

// -- the configuration -----------------------------------------------------------

bool InstallEngine::doConfigure(const InstallPlan& plan, bool dryRun)
{
    (void)dryRun; // the writes and commands themselves know what a dry run does
    // The kernel's own filesystems, lent to the new system while its own are
    // set up, in the order a chroot needs them.
    if (!run({"mount", {"--bind", "/dev", m_target + "/dev"}}, "lending /dev"))
        return false;
    if (!run({"mount", {"--bind", "/run", m_target + "/run"}}, "lending /run"))
        return false;
    if (!run({"mount", {"-t", "proc", "proc", m_target + "/proc"}}, "lending /proc"))
        return false;
    if (!run({"mount", {"--bind", "/sys", m_target + "/sys"}}, "lending /sys"))
        return false;
    if (m_efi && m_layout.firmwareIsEsp)
        runBestEffort({"mount", {"--bind", "/sys/firmware/efi/efivars",
                                 m_target + "/sys/firmware/efi/efivars"}},
                      "lending the EFI variables");

    stepAt(0.1);

    // The language.
    std::string locale = plan.language() + ".UTF-8";
    if (!writeTargetFile("/etc/locale.conf", "LANG=" + locale + "\n", "the language"))
        return false;
    {
        // The locale itself, enabled in the table the locale generator reads.
        const std::string path = m_target + "/etc/locale.gen";
        std::string text;
        std::string line;
        if (!dryRun && readFile(path, text)) {
            const std::string wanted = locale + " UTF-8";
            const std::string commented = "#" + locale + " UTF-8";
            std::string rebuilt;
            size_t at = 0;
            while (at < text.size()) {
                size_t end = text.find('\n', at);
                line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
                at = end == std::string::npos ? text.size() : end + 1;
                if (line == commented)
                    line = wanted;
                rebuilt += line + '\n';
            }
            writeFile(path, rebuilt);
        } else {
            emitLog("$ enable " + locale + " in /etc/locale.gen");
        }
        if (!dryRun && (fileExists(m_target + "/usr/bin/locale-gen")
                        || fileExists(m_target + "/usr/sbin/locale-gen")))
            run({"chroot", {m_target, "locale-gen"}}, "generating the locale");
        else if (dryRun)
            emitLog("$ chroot " + m_target + " locale-gen");
    }
    stepAt(0.25);

    // The keyboard: what the console takes, and what the session takes.
    if (!writeTargetFile("/etc/vconsole.conf", "KEYMAP=" + plan.keyboard() + "\n", "the console keyboard"))
        return false;
    const std::string xkb =
        "# the keyboard layout the installer chose, kept by the Cutefish desktop\n"
        "Section \"InputClass\"\n"
        "    Identifier \"system-keyboard\"\n"
        "    MatchIsKeyboard \"on\"\n"
        "    Option \"XkbLayout\" \"" + plan.keyboard() + "\"\n"
        "EndSection\n";
    if (!writeTargetFile("/etc/X11/xorg.conf.d/00-keyboard.conf", xkb, "the session keyboard"))
        return false;
    stepAt(0.4);

    // The time zone.
    if (!run({"ln", {"-sf", "/usr/share/zoneinfo/" + plan.timeZone(), m_target + "/etc/localtime"}},
             "the time zone"))
        return false;
    if (!writeTargetFile("/etc/timezone", plan.timeZone() + "\n", "the time zone's name"))
        return false;
    stepAt(0.5);

    // The machine's own name.
    if (!writeTargetFile("/etc/hostname", plan.hostName() + "\n", "the hostname"))
        return false;
    {
        std::string hosts = "127.0.0.1\tlocalhost\n::1\t\tlocalhost\n127.0.1.1\t"
            + plan.hostName() + "\n";
        std::string existing;
        if (!dryRun && readFile(m_target + "/etc/hosts", existing)) {
            std::string rebuilt;
            size_t at = 0;
            while (at < existing.size()) {
                size_t end = existing.find('\n', at);
                std::string line = existing.substr(at, end == std::string::npos
                                                           ? std::string::npos : end - at);
                at = end == std::string::npos ? existing.size() : end + 1;
                if (line.compare(0, 9, "127.0.1.1") == 0)
                    continue;
                rebuilt += line + '\n';
            }
            rebuilt += "127.0.1.1\t" + plan.hostName() + "\n";
            hosts = rebuilt;
        }
        if (!writeTargetFile("/etc/hosts", hosts, "the hosts table"))
            return false;
    }
    stepAt(0.6);

    // The user, their password, and the right to administer the system.
    if (!run({"chroot", {m_target, "useradd", "-m",
                         "-G", "wheel,audio,video,network,storage,power", "-s", "/bin/bash",
                         plan.userName()}},
             "the user's account")) {
        emitLog("the account is made without the extra groups, as the base image lacks some of them");
        if (!run({"chroot", {m_target, "useradd", "-m", "-s", "/bin/bash", plan.userName()}},
                 "the user's account"))
            return false;
    }
    stepAt(0.75);
    if (!runInput("chroot", {m_target, "chpasswd"},
                  plan.userName() + ":" + plan.password() + "\n", "the user's password"))
        return false;
    {
        // The administrators' group, given its right, whatever the base
        // image's own table keeps commented.
        const std::string path = m_target + "/etc/sudoers";
        std::string text;
        if (!dryRun && readFile(path, text)) {
            const std::string commented = "# %wheel ALL=(ALL:ALL) ALL";
            const std::string enabled = "%wheel ALL=(ALL:ALL) ALL";
            size_t at = text.find(commented);
            if (at != std::string::npos) {
                text.replace(at, commented.size(), enabled);
                if (!writeTargetFileMode("/etc/sudoers", text, 0440, "the administrators' right"))
                    return false;
            }
        } else {
            emitLog("$ enable %wheel in /etc/sudoers");
        }
    }
    stepAt(0.85);

    // The privacy switches, kept where the desktop's own settings read them.
    {
        const std::string privacy = "[Privacy]\nlocation="
            + std::string(plan.privacy().location ? "true" : "false") + "\ndiagnostics="
            + std::string(plan.privacy().diagnostics ? "true" : "false") + "\ntailored="
            + std::string(plan.privacy().tailored ? "true" : "false") + "\n";
        if (!writeTargetFile("/etc/cutefish/privacy.conf", privacy, "the privacy defaults"))
            return false;
    }
    stepAt(0.9);

    // The boot's own image of the kernel, when the base image has a tool
    // that makes one; a live image carries its own, made this way.
    if (!dryRun && fileExists(m_target + "/usr/bin/mkinitcpio"))
        runBestEffort({"chroot", {m_target, "mkinitcpio", "-P"}}, "the kernel's boot image");
    else if (!dryRun && fileExists(m_target + "/usr/sbin/update-initramfs"))
        runBestEffort({"chroot", {m_target, "update-initramfs", "-u", "-k", "all"}},
                      "the kernel's boot image");
    else if (dryRun)
        emitLog("$ chroot " + m_target + " mkinitcpio -P");
    stepAt(1.0);
    return true;
}

// -- the bootloader -----------------------------------------------------------------

bool InstallEngine::doBootloader(const InstallPlan& plan, bool dryRun)
{
    (void)dryRun; // the commands themselves know what a dry run does
    if (m_efi) {
        if (!run({"chroot", {m_target, "grub-install", "--target=x86_64-efi",
                             "--efi-directory=/boot/efi", "--bootloader-id=Cutefish", "--recheck"}},
                 "the bootloader"))
            return false;
    } else {
        if (!run({"chroot", {m_target, "grub-install", "--target=i386-pc", "--recheck",
                             plan.device()}},
                 "the bootloader"))
            return false;
    }
    stepAt(0.7);
    if (!run({"chroot", {m_target, "grub-mkconfig", "-o", "/boot/grub/grub.cfg"}},
             "the boot menu"))
        return false;
    stepAt(1.0);
    return true;
}

bool InstallEngine::doFirstBoot(const InstallPlan& plan, bool dryRun)
{
    (void)plan;
    if (dryRun) {
        emitLog("$ chroot " + m_target + " systemctl preset-all");
        emitLog("$ write /etc/cutefish/firstboot.conf");
        stepAt(1.0);
        return true;
    }
    if (fileExists(m_target + "/usr/bin/systemctl"))
        runBestEffort({"chroot", {m_target, "systemctl", "preset-all"}},
                      "the services the system starts with");
    // The marker the desktop reads: the welcome that walks a new user in is
    // for this user, on their first sign-in.
    if (!writeTargetFile("/etc/cutefish/firstboot.conf", "welcome=true\n", "the first boot's marker"))
        return false;
    stepAt(1.0);
    return true;
}

bool InstallEngine::doCleanup(const InstallPlan& plan, bool dryRun)
{
    (void)plan;
    if (dryRun) {
        emitLog("nothing was mounted; the dry run leaves the machine exactly as it was");
        stepAt(1.0);
        return true;
    }
    // What was mounted comes down in the reverse of the order it went up,
    // the whole tree at once where the bind mounts sit under it.
    if (!m_sourceMount.empty())
        runBestEffort({"umount", {m_sourceMount}}, "the image's mount");
    runBestEffort({"umount", {"-R", m_target}}, "the new system's mounts");
    if (m_swapOn)
        runBestEffort({"swapoff", {"-a"}}, "swap");
    runBestEffort({"sync", {}}, "the disks' own buffers");
    emitLog("the new system is ready: unmount the disk, or just restart");
    stepAt(1.0);
    return true;
}

// -- the run's own helpers -----------------------------------------------------------

bool InstallEngine::run(const Command& command, const char* what)
{
    return run(command.program, command.arguments, what);
}

bool InstallEngine::run(const std::string& program, const std::vector<std::string>& arguments,
                        const char* what)
{
    const Command command{program, arguments};
    emitLog("$ " + command.describe());
    m_commandLog.push_back(command.describe());
    if (m_dryRun)
        return true;
    const CommandResult result = runCommand(command, nullptr, m_hooks.cancelled);
    if (checkCancelled()) {
        m_error = "cancelled";
        return false;
    }
    if (!result.ok()) {
        std::string tail = result.output;
        if (tail.size() > 300)
            tail = "…" + tail.substr(tail.size() - 300);
        const std::string doing = what ? what : "running " + program;
        m_error = doing + " failed (" + std::to_string(result.exitCode) + ")"
            + (tail.empty() ? "" : ": " + tail);
        return false;
    }
    return true;
}

bool InstallEngine::runBestEffort(const Command& command, const char* what)
{
    return runBestEffort(command.program, command.arguments, what);
}

bool InstallEngine::runBestEffort(const std::string& program, const std::vector<std::string>& arguments,
                                  const char* what)
{
    const Command command{program, arguments};
    emitLog("$ " + command.describe());
    m_commandLog.push_back(command.describe());
    if (m_dryRun)
        return true;
    const CommandResult result = runCommand(command);
    if (!result.ok()) {
        std::string tail = result.output;
        if (tail.size() > 200)
            tail = "…" + tail.substr(tail.size() - 200);
        emitLog(std::string(what ? what : program) + " said no, and the work goes on: "
                + (tail.empty() ? "exit " + std::to_string(result.exitCode) : tail));
    }
    return true;
}

bool InstallEngine::runInput(const std::string& program, const std::vector<std::string>& arguments,
                             const std::string& input, const char* what)
{
    const Command command{program, arguments};
    // The input is the user's password: it is never named in the log.
    emitLog("$ " + command.describe() + " < input");
    m_commandLog.push_back(command.describe() + " < input");
    if (m_dryRun)
        return true;
    const CommandResult result = runCommandWithInput(program, arguments, input, nullptr,
                                                     m_hooks.cancelled);
    if (!result.ok()) {
        const std::string doing = what ? what : "running " + program;
        m_error = doing + " failed (" + std::to_string(result.exitCode) + ")";
        return false;
    }
    return true;
}

bool InstallEngine::writeTargetFile(const std::string& path, const std::string& data, const char* what)
{
    if (m_dryRun) {
        emitLog(std::string("$ write ") + m_target + path + " (" + std::to_string(data.size())
                + " bytes)");
        m_commandLog.push_back("write " + m_target + path);
        return true;
    }
    const std::string whole = m_target + path;
    const size_t slash = whole.rfind('/');
    if (slash != std::string::npos && !makeDirectories(whole.substr(0, slash)))
        return fail(std::string("cannot make the folder of ") + (what ? what : path));
    if (!writeFile(whole, data))
        return fail(std::string("cannot write ") + (what ? what : path));
    return true;
}

bool InstallEngine::writeTargetFileMode(const std::string& path, const std::string& data, mode_t mode,
                                        const char* what)
{
    if (m_dryRun)
        return writeTargetFile(path, data, what);
    if (!writeTargetFile(path, data, what))
        return false;
    if (!writeFileMode(m_target + path, data, mode))
        return fail(std::string("cannot set the rights of ") + (what ? what : path));
    return true;
}

bool InstallEngine::findDisk(const std::string& kname, DiskDevice& disk) const
{
    for (const DiskDevice& candidate : m_disks) {
        if (candidate.kname == kname) {
            disk = candidate;
            return true;
        }
    }
    return false;
}

bool InstallEngine::findUuid(const std::string& kname, std::string& uuid) const
{
    for (const DiskDevice& disk : m_disks) {
        for (const Partition& partition : disk.partitions) {
            if (partition.kname == kname && !partition.uuid.empty()) {
                uuid = partition.uuid;
                return true;
            }
        }
    }
    return false;
}

} // namespace cutefish::installer
