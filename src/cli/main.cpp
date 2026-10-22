// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

// cutefish-installerctl: the tool behind the window. The machine's disks are
// read here, the installation is done here, and the window starts it and
// reads the events it prints — so the same code installs from the desktop,
// from a shell, or from a script with a plan's JSON.

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/disks.h"
#include "core/installengine.h"
#include "core/installplan.h"
#include "core/json.h"
#include "core/tables.h"
#include "core/util.h"

using namespace cutefish::installer;

static const char* const kProgram = "cutefish-installerctl";

static std::atomic<bool> g_cancelled{false};

static void handleSignal(int)
{
    g_cancelled = true;
}

// The exit codes, the same for every command: 0 the work is done, 1 it
// failed, 2 it was stopped, 3 it was not started right.
enum ExitCode {
    ExitOk = 0,
    ExitFailed = 1,
    ExitCancelled = 2,
    ExitUsage = 3,
};

static void usage(FILE* out)
{
    std::fprintf(out,
        "Usage: %s COMMAND [options]\n"
        "\n"
        "The installer of Cutefish OS.\n"
        "\n"
        "Commands:\n"
        "  install    Follow a plan's JSON and install the system.\n"
        "  validate   Check a plan's JSON and tell what the run would do.\n"
        "  disks      The disks the system can be installed to.\n"
        "  locales    The languages the installer offers.\n"
        "  layouts    The keyboard layouts the installer offers.\n"
        "  zones      The time zones the installer offers.\n"
        "  reboot     Restart the machine into the new system.\n"
        "  poweroff   Turn the machine off.\n"
        "\n"
        "Options:\n"
        "  --config PATH        The plan's JSON file (install, validate)\n"
        "  --dry-run            Plan and report the whole install, touch nothing\n"
        "  --progress-events    Progress as one JSON object a line on stdout\n"
        "  --json               disks, locales, layouts and zones as JSON\n",
        kProgram);
}

// Reads an option that carries a value; a plain block, not a do-while, so
// the continue belongs to the loop around it.
#define READ_VALUE(name, into)                                                                     \
    if (argument == (name) || argument.rfind(std::string(name) + "=", 0) == 0) {                   \
        if (argument == (name) && i + 1 >= arguments.size()) {                                     \
            std::fprintf(stderr, "%s: %s needs a value\n", kProgram, name);                        \
            return ExitUsage;                                                                      \
        }                                                                                          \
        (into) = argument == (name) ? arguments[++i]                                               \
                                    : argument.substr(std::string(name).size() + 1);              \
        continue;                                                                                  \
    }

// -- the tables -----------------------------------------------------------------

static int commandList(const std::string& command, bool json)
{
    Json list = Json::array();
    std::string text;
    if (command == "locales") {
        for (const Language& language : languages()) {
            text += std::string(language.code) + "  " + language.name + " ("
                + language.territory + ")\n";
            Json entry = Json::object();
            entry.set("code", language.code);
            entry.set("name", language.name);
            entry.set("territory", language.territory);
            entry.set("native", language.native);
            list.append(entry);
        }
    } else if (command == "layouts") {
        for (const KeyboardLayout& layout : keyboardLayouts()) {
            text += std::string(layout.code) + "  " + layout.name + " (" + layout.hint + ")\n";
            Json entry = Json::object();
            entry.set("code", layout.code);
            entry.set("name", layout.name);
            entry.set("hint", layout.hint);
            list.append(entry);
        }
    } else {
        for (const Zone& zone : timeZones()) {
            const std::string path = zonePath(zone);
            text += path + "\n";
            Json entry = Json::object();
            entry.set("zone", path);
            list.append(entry);
        }
    }
    if (json)
        std::printf("%s\n", list.dump().c_str());
    else
        std::fputs(text.c_str(), stdout);
    return ExitOk;
}

// -- the disks ------------------------------------------------------------------

static int commandDisks(bool json)
{
    std::string error;
    const std::vector<DiskDevice> disks = DiskScanner::scan(&error);
    if (!error.empty()) {
        std::fprintf(stderr, "%s: %s\n", kProgram, error.c_str());
        return ExitFailed;
    }
    Json list = Json::array();
    std::string text;
    for (const DiskDevice& disk : disks) {
        std::string partitions;
        for (const Partition& partition : disk.partitions) {
            if (!partitions.empty())
                partitions += ", ";
            partitions += partition.kname;
            if (!partition.fstype.empty())
                partitions += " " + partition.fstype;
            if (!partition.mountpoints.empty())
                partitions += " at " + partition.mountpoints.front();
        }
        text += disk.kname + "  " + disk.description() + (partitions.empty() ? "  (empty)" : "  " + partitions)
            + "\n";
        Json entry = Json::object();
        entry.set("device", disk.kname);
        entry.set("model", disk.model);
        entry.set("size", disk.sizeBytes);
        entry.set("removable", disk.removable);
        Json parts = Json::array();
        for (const Partition& partition : disk.partitions) {
            Json part = Json::object();
            part.set("device", partition.kname);
            part.set("size", partition.sizeBytes);
            part.set("fstype", partition.fstype);
            part.set("label", partition.label);
            part.set("uuid", partition.uuid);
            part.set("partuuid", partition.partUuid);
            Json mounts = Json::array();
            for (const std::string& mount : partition.mountpoints)
                mounts.append(mount);
            part.set("mountpoints", mounts);
            parts.append(part);
        }
        entry.set("partitions", parts);
        list.append(entry);
    }
    if (json)
        std::printf("%s\n", list.dump().c_str());
    else
        std::fputs(text.c_str(), stdout);
    return ExitOk;
}

// -- the plan ---------------------------------------------------------------------

static bool loadPlan(const std::string& path, InstallPlan& plan, std::string* error)
{
    std::string text;
    if (!readFile(path, text)) {
        if (error)
            *error = "cannot read " + path;
        return false;
    }
    Json json;
    if (!Json::parse(text, json)) {
        if (error)
            *error = path + " is not JSON";
        return false;
    }
    std::string problem;
    if (!plan.fromJson(json, &problem) || !plan.validate(&problem)) {
        if (error)
            *error = problem;
        return false;
    }
    return true;
}

static int commandValidate(const std::string& configPath)
{
    InstallPlan plan;
    std::string error;
    if (!loadPlan(configPath, plan, &error)) {
        std::fprintf(stderr, "%s: %s\n", kProgram, error.c_str());
        return ExitFailed;
    }
    std::printf("%s\n", plan.toJson().dump(true).c_str());
    return ExitOk;
}

// -- the installation ----------------------------------------------------------------

static int commandInstall(const std::string& configPath, bool dryRun, bool events)
{
    InstallPlan plan;
    std::string error;
    if (!loadPlan(configPath, plan, &error)) {
        std::fprintf(stderr, "%s: %s\n", kProgram, error.c_str());
        return ExitUsage;
    }
    if (!dryRun && !runningAsRoot()) {
        std::fprintf(stderr, "%s: the installation needs the administrator (run as root, or with pkexec)\n",
                     kProgram);
        return ExitUsage;
    }

    InstallEngine::Hooks hooks;
    hooks.cancelled = [] { return g_cancelled.load(); };
    hooks.event = [events](const Json& event) {
        if (events) {
            std::printf("%s\n", event.dump().c_str());
            std::fflush(stdout);
            return;
        }
        // The same events, spelled for a person watching a terminal.
        const std::string name = event.get("event").toString();
        if (name == "step") {
            std::printf("\n== %s ==\n", event.get("label").toString().c_str());
        } else if (name == "progress") {
            const std::string message = event.get("message").toString();
            std::printf("\r  [%3.0f%%] %s", event.get("percent").toNumber(),
                        message.c_str());
            std::fflush(stdout);
        } else if (name == "log") {
            std::printf("\n%s", event.get("message").toString().c_str());
            std::fflush(stdout);
        } else if (name == "finished") {
            std::printf("\n  done in %.1f minutes\n",
                        event.get("elapsed_ms").toUnsigned() / 60000.0);
        } else if (name == "error") {
            std::fprintf(stderr, "\n%s: %s\n", kProgram, event.get("message").toString().c_str());
        } else if (name == "cancelled") {
            std::printf("\n  stopped\n");
        }
    };

    InstallEngine engine(std::move(hooks));
    const InstallEngine::Outcome outcome = engine.run(plan, dryRun);
    switch (outcome) {
    case InstallEngine::Outcome::Finished:
        return ExitOk;
    case InstallEngine::Outcome::Cancelled:
        return ExitCancelled;
    case InstallEngine::Outcome::Failed:
        if (!events)
            std::fprintf(stderr, "%s: the install failed at the %s step\n", kProgram,
                         engine.failedStep().c_str());
        return ExitFailed;
    }
    return ExitFailed;
}

// -- the machine's own buttons -----------------------------------------------------

static int commandPower(const std::string& action)
{
    if (!runningAsRoot()) {
        std::fprintf(stderr, "%s: %s needs the administrator\n", kProgram, action.c_str());
        return ExitUsage;
    }
    // systemd's own way first; a machine without it takes the kernel's.
    if (runCommand("systemctl", {action}).ok())
        return ExitOk;
    const int code = runCommand(action, {}).exitCode;
    return code == 0 ? ExitOk : ExitFailed;
}

// -- the command line ----------------------------------------------------------------

int main(int argc, char** argv)
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::vector<std::string> words(argv + 1, argv + argc);
    if (words.empty()) {
        usage(stderr);
        return ExitUsage;
    }

    std::string command = words.front();
    // The macro reads the options from a vector named `arguments` — the one
    // this loop walks, without the command itself at its head.
    std::vector<std::string> arguments(words.begin() + 1, words.end());

    if (command == "--help" || command == "-h") {
        usage(stdout);
        return ExitOk;
    }
    if (command == "--version") {
        std::printf("%s 1.0\n", kProgram);
        return ExitOk;
    }

    std::string configPath;
    bool dryRun = false;
    bool events = false;
    bool json = false;

    for (size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        READ_VALUE("--config", configPath)
        if (argument == "--dry-run") {
            dryRun = true;
            continue;
        }
        if (argument == "--progress-events") {
            events = true;
            continue;
        }
        if (argument == "--json") {
            json = true;
            continue;
        }
        std::fprintf(stderr, "%s: %s is not an option this command takes\n", kProgram, argument.c_str());
        return ExitUsage;
    }

    if (command == "disks")
        return commandDisks(json);
    if (command == "locales" || command == "layouts" || command == "zones")
        return commandList(command, json);
    if (command == "validate") {
        if (configPath.empty()) {
            std::fprintf(stderr, "%s: validate needs --config PATH\n", kProgram);
            return ExitUsage;
        }
        return commandValidate(configPath);
    }
    if (command == "install") {
        if (configPath.empty()) {
            std::fprintf(stderr, "%s: install needs --config PATH\n", kProgram);
            return ExitUsage;
        }
        return commandInstall(configPath, dryRun, events);
    }
    if (command == "reboot")
        return commandPower("reboot");
    if (command == "poweroff")
        return commandPower("poweroff");

    std::fprintf(stderr, "%s: %s is not a command\n", kProgram, command.c_str());
    usage(stderr);
    return ExitUsage;
}
