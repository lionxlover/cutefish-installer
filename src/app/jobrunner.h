// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/process.h"
#include "core/json.h"

namespace cutefish::ui {
class Window;
} // namespace cutefish::ui

namespace installer {

// The core's Json, spelled out: the window is an installer:: of its own,
// not a cutefish::installer, so the two namespaces are named apart.
using CoreJson = cutefish::installer::Json;

// Runs cutefish-installerctl for the window: the installation itself, with
// the progress it prints as one JSON object a line. The window never
// blocks; the engine works away and reports in here, and Cancel tells it to
// stop by the same signal a terminal would.
class JobRunner
{
public:
    // How a job ended. The exit codes are the CLI's own: 0 well, 1 failed,
    // 2 stopped.
    enum class Outcome {
        Finished,
        Failed,
        Cancelled,
    };

    // One event the CLI printed. `data` holds the event's own fields, with
    // its own name under "event".
    struct Event
    {
        std::string name;
        CoreJson data;
    };

    JobRunner();
    ~JobRunner();

    // Starts the CLI with `arguments` behind the command, reading the
    // events it prints on stdout. `privileged` runs it through pkexec, for
    // the machines where the window itself does not hold the
    // administrator's own rights; the program's full path must be given
    // then, as pkexec does not look on the PATH the way a shell does.
    void start(const std::string& program, const std::vector<std::string>& arguments, bool privileged);
    void cancel();
    bool isRunning() const { return m_process && m_process->isRunning(); }

    std::function<void(const Event&)> onEvent;

    // The job is over: how, with the last whole event of the run, and the
    // reason it failed when it did.
    std::function<void(Outcome outcome, const CoreJson& summary, const std::string& errorText)> onDone;

private:
    void finished(int exitCode, bool crashed);

    std::unique_ptr<cutefish::base::Process> m_process;
    std::string m_pending; // the tail of a line not yet whole
    CoreJson m_summary;   // the last "finished" or "error" event
    std::string m_errorText;
};

} // namespace installer
