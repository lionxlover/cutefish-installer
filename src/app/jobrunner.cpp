// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "app/jobrunner.h"

#include <chrono>
#include <string>
#include <vector>

namespace installer {

using Json = cutefish::installer::Json;
using CoreJson = cutefish::installer::Json;

JobRunner::JobRunner() = default;

JobRunner::~JobRunner() = default;

void JobRunner::start(const std::string& program, const std::vector<std::string>& arguments, bool privileged)
{
    // A job already running is stopped first; the engine leaves the
    // machine as it found it on the way out.
    if (m_process && m_process->isRunning()) {
        m_process->terminate();
        m_process->waitForFinished(std::chrono::seconds(5));
    }
    m_process = std::make_unique<cutefish::base::Process>();
    m_pending.clear();
    m_summary = Json();
    m_errorText.clear();

    m_process->onStandardOutput = [this](std::string_view data) {
        // The events come as whole lines; a chunk may hold several or a
        // part of one.
        m_pending.append(data);
        size_t at = 0;
        while (true) {
            const size_t end = m_pending.find('\n', at);
            if (end == std::string::npos)
                break;
            const std::string line = m_pending.substr(at, end - at);
            at = end + 1;
            Json event;
            if (!line.empty() && Json::parse(line, event) && event.isObject()) {
                const std::string name = event.get("event").toString();
                if (name == "finished")
                    m_summary = event;
                else if (name == "error")
                    m_summary = event;
                if (onEvent) {
                    Event whole;
                    whole.name = name;
                    whole.data = event;
                    onEvent(whole);
                }
            }
        }
        m_pending.erase(0, at);
    };

    // The CLI keeps the human talk on the error stream; the reason a job
    // failed comes from it.
    m_process->onStandardError = [this](std::string_view data) { m_errorText.append(data); };

    m_process->onFinished = [this](int exitCode, bool crashed) { finished(exitCode, crashed); };

    std::vector<std::string> commandLine;
    if (privileged) {
        commandLine.push_back("pkexec");
        commandLine.push_back(program);
    } else {
        commandLine.push_back(program);
    }
    for (const std::string& argument : arguments)
        commandLine.push_back(argument);
    if (!m_process->start(commandLine.front(),
                          std::vector<std::string>(commandLine.begin() + 1, commandLine.end()))) {
        if (onDone)
            onDone(Outcome::Failed, Json(),
                   privileged ? "The administrator's helper, pkexec, could not be started."
                              : "cutefish-installerctl could not be found on this system.");
    }
}

void JobRunner::cancel()
{
    // SIGTERM: the engine stops between its commands and reports itself
    // stopped, leaving the machine as it found it.
    if (m_process && m_process->isRunning())
        m_process->terminate();
}

void JobRunner::finished(int exitCode, bool crashed)
{
    if (!onDone)
        return;

    // The first line of what the CLI said on its error stream, without the
    // progress chatter it leaves there.
    std::string error = m_errorText;
    while (!error.empty() && (error.back() == '\n' || error.back() == '\r' || error.back() == ' '))
        error.pop_back();
    if (crashed || exitCode == 1) {
        // An error event carries its message; the error stream may say more.
        std::string message = m_summary.get("message").toString();
        if (message.empty()) {
            const size_t line = error.find('\n');
            message = line == std::string::npos ? error : error.substr(0, line);
        }
        onDone(Outcome::Failed, m_summary, message);
        return;
    }
    if (exitCode == 2) {
        onDone(Outcome::Cancelled, Json(), std::string());
        return;
    }
    onDone(Outcome::Finished, m_summary, error);
}

} // namespace installer
