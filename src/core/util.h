// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace cutefish::installer {

// POSIX-only helpers shared by the engine, the CLI and the tests. The core
// carries no third-party dependency: it reads the machine it runs on with
// the kernel's own interfaces, and does its work through the standard
// command-line tools an installation image always has.

// -- files -----------------------------------------------------------------

bool isDirectory(const std::string& path);
bool fileExists(const std::string& path);
// mkdir -p, one component at a time; true when it exists afterwards.
bool makeDirectories(const std::string& path);
// Removes a file, a link or an empty directory; ignores what is not there.
bool removeFile(const std::string& path);
// Reads a whole file into a string; empty and false when it cannot.
bool readFile(const std::string& path, std::string& into);
bool writeFile(const std::string& path, const std::string& data);
// A directory listing, without "." and ".."; sorted by the kernel.
std::vector<std::string> listDirectory(const std::string& path);
// "12.3 MB", "1.2 GB" …, with one figure below GB.
std::string humanReadableSize(uint64_t bytes);

// -- the machine -----------------------------------------------------------

// True when the system was started the UEFI way.
bool isEfiSystem();
// The installed memory in bytes, from /proc/meminfo; 0 when it cannot read.
uint64_t systemMemory();
// True when this is a live session (boot=live or boot=casper on the kernel
// command line, or a squashfs mounted at the root of the running image).
bool isLiveSession();
bool runningAsRoot();

// -- running the work -------------------------------------------------------

// One command of the engine's plan: the program and its own arguments, kept
// apart so nothing a name might carry is ever read by a shell.
struct Command
{
    std::string program;
    std::vector<std::string> arguments;

    std::string describe() const;
};

// What a command did.
struct CommandResult
{
    int exitCode = -1;
    std::string output; // stdout and stderr together
    bool crashed() const { return exitCode < 0; }
    bool ok() const { return exitCode == 0; }
};

// Runs a command and waits for it. `onOutput` sees its output as it arrives,
// in pieces; without it, the output is still collected.
CommandResult runCommand(const std::string& program, const std::vector<std::string>& arguments,
                         const std::function<void(const std::string&)>& onOutput = nullptr,
                         const std::function<bool()>& cancelled = nullptr);
CommandResult runCommand(const Command& command,
                         const std::function<void(const std::string&)>& onOutput = nullptr,
                         const std::function<bool()>& cancelled = nullptr);

// The same, with text written to the command's input before its output is
// read — the way chpasswd takes an account and its password.
CommandResult runCommandWithInput(const std::string& program, const std::vector<std::string>& arguments,
                                  std::string input,
                                  const std::function<void(const std::string&)>& onOutput = nullptr,
                                  const std::function<bool()>& cancelled = nullptr);

// Writes a text the way the installer's helper tools take their settings:
// `KEY=value` lines, with the values of `entries` kept when they are there,
// replaced when they are, added at the end when they are not.
bool writeKeyValueFile(const std::string& path, const std::vector<std::pair<std::string, std::string>>& entries);

// writeFile with the mode the file must carry, as sudoers does.
bool writeFileMode(const std::string& path, const std::string& data, mode_t mode);

// The words of a full name folded into a username: lower case, the letters
// and digits kept, the rest turned into single hyphens, none at the ends.
std::string suggestUsername(const std::string& fullName);

// A hostname the system accepts: lower case letters, digits and hyphens,
// none of the latter at the ends.
std::string suggestHostname(const std::string& text);

// The rules themselves, shared by the plan's checking and the pages' live
// hints: a username a system takes, a hostname a network takes.
bool validUsernameName(const std::string& name);
bool validHostName(const std::string& name);

// The machine's own zone, as the zoneinfo tree spells it, from where
// /etc/localtime points; "UTC" when it says nothing.
std::string systemTimeZone();

} // namespace cutefish::installer
