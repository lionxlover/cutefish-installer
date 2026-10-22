// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "core/disks.h"
#include "core/installplan.h"
#include "core/json.h"
#include "core/partitionplan.h"
#include "core/util.h"

namespace cutefish::installer {

// The installer's own work: everything the pages chose, done to the machine
// in the order an installation goes — the disk laid out, the file systems
// made and mounted, the system's files copied onto them, and the new system
// configured to boot and to welcome its user. Every part reports as it runs:
// one JSON object a line on stdout, the same events whatever starts it, the
// window or a shell. A dry run plans and reports the whole work and does
// none of it, so a plan can be checked before anything is touched.
class InstallEngine
{
public:
    // How a run ends. The numbers are the CLI's own exit codes.
    enum class Outcome {
        Finished = 0,
        Failed = 1,
        Cancelled = 2,
    };

    // What the engine tells as it works. `event` takes one JSON object at a
    // time; `cancelled` is asked between the commands.
    struct Hooks
    {
        std::function<void(const Json&)> event;
        std::function<bool()> cancelled;
    };

    explicit InstallEngine(Hooks hooks);

    // Runs the whole installation. `dryRun` reports every command the plan
    // would run without running any of them, and skips the work that needs
    // the machine's own state — mounts, file writes — reporting it instead.
    Outcome run(const InstallPlan& plan, bool dryRun);

    // Why a run failed, and where it stood.
    const std::string& error() const { return m_error; }
    const std::string& failedStep() const { return m_failedStep; }
    // Every command the run made, in the order it ran them; the dry run's
    // own record of what it would have done.
    const std::vector<std::string>& commandLog() const { return m_commandLog; }

    // The steps of the installation, in the order the run walks them.
    static std::vector<std::string> stepOrder();

    // /etc/fstab for a layout: an entry a partition, by the UUID the kernel
    // knows them by, falling back to the device's own name when the scan has
    // no UUID of it. Exposed for the tests.
    static std::string generateFstab(const PartitionPlanner::Layout& layout, bool efi,
                                     const std::vector<DiskDevice>& disks);
    // The same for the manual mode's own chosen partitions.
    static std::string generateManualFstab(const std::vector<PlannedPartition>& planned,
                                           const std::vector<DiskDevice>& disks);

private:
    // The steps' own share of the whole percent, kept so the bar moves the
    // way the work does: the copy takes the most of it.
    struct Step
    {
        const char* id;
        double from;
        double to;
    };
    static const std::vector<Step>& steps();
    static std::string stepLabel(const std::string& id);

    // -- reporting ---------------------------------------------------------
    void emit(const Json& event);
    void emitStep(const std::string& id);
    void emitProgress(double percent, const std::string& message = {});
    void emitLog(const std::string& message);
    bool checkCancelled();
    void beginStep(const std::string& id);
    void stepAt(double fraction, const std::string& message = {});
    bool fail(const std::string& message);

    // -- the work -----------------------------------------------------------
    bool doPrepare(const InstallPlan& plan, bool dryRun);
    bool doPartition(const InstallPlan& plan, bool dryRun);
    bool doFormat(const InstallPlan& plan, bool dryRun);
    bool doMount(const InstallPlan& plan, bool dryRun);
    bool doCopy(const InstallPlan& plan, bool dryRun);
    bool doFstab(const InstallPlan& plan, bool dryRun);
    bool doConfigure(const InstallPlan& plan, bool dryRun);
    bool doBootloader(const InstallPlan& plan, bool dryRun);
    bool doFirstBoot(const InstallPlan& plan, bool dryRun);
    bool doCleanup(const InstallPlan& plan, bool dryRun);

    // One command of the plan, run for real or only told of in a dry run.
    bool run(const Command& command, const char* what = nullptr);
    bool run(const std::string& program, const std::vector<std::string>& arguments,
             const char* what = nullptr);
    // The same, but a command that may say no without stopping the work.
    bool runBestEffort(const Command& command, const char* what = nullptr);
    bool runBestEffort(const std::string& program, const std::vector<std::string>& arguments,
                       const char* what = nullptr);
    bool runInput(const std::string& program, const std::vector<std::string>& arguments,
                  const std::string& input, const char* what = nullptr);
    // A file written into the target, or only told of in a dry run.
    bool writeTargetFile(const std::string& path, const std::string& data, const char* what);
    bool writeTargetFileMode(const std::string& path, const std::string& data, mode_t mode,
                             const char* what);

    const PartitionPlanner::Layout& layout() const { return m_layout; }
    bool findDisk(const std::string& kname, DiskDevice& disk) const;
    bool findUuid(const std::string& kname, std::string& uuid) const;

    Hooks m_hooks;
    Step m_step{"", 0, 0};
    double m_lastPercent = -1;
    std::string m_error;
    std::string m_failedStep;
    std::vector<std::string> m_commandLog;
    std::vector<std::string> m_mountedPoints;
    bool m_swapOn = false;

    std::vector<DiskDevice> m_disks;
    bool m_efi = false;
    PartitionPlanner::Layout m_layout;
    std::string m_target = "/mnt/target"; // where the new system is built
    std::string m_sourceMount;            // a squashfs mounted for the copy
    bool m_dryRun = true;
    std::chrono::steady_clock::time_point m_started;
};

} // namespace cutefish::installer
