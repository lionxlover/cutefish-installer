// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "core/util.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <signal.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cutefish::installer {

// -- files -----------------------------------------------------------------

bool isDirectory(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool fileExists(const std::string& path)
{
    struct stat st;
    return lstat(path.c_str(), &st) == 0;
}

bool makeDirectories(const std::string& path)
{
    if (path.empty() || isDirectory(path))
        return !path.empty();
    const size_t slash = path.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        if (!makeDirectories(path.substr(0, std::max(slash, size_t(1)))))
            return false;
    }
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

bool removeFile(const std::string& path)
{
    return path.empty() ? false : (unlink(path.c_str()) == 0 || rmdir(path.c_str()) == 0 || errno == ENOENT);
}

bool readFile(const std::string& path, std::string& into)
{
    into.clear();
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    char buffer[65536];
    for (;;) {
        const ssize_t got = read(fd, buffer, sizeof(buffer));
        if (got < 0) {
            close(fd);
            return false;
        }
        if (got == 0)
            break;
        into.append(buffer, size_t(got));
    }
    close(fd);
    return true;
}

bool writeFile(const std::string& path, const std::string& data)
{
    const std::string temp = path + ".new";
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return false;
    size_t written = 0;
    while (written < data.size()) {
        const ssize_t put = write(fd, data.data() + written, data.size() - written);
        if (put <= 0) {
            close(fd);
            unlink(temp.c_str());
            return false;
        }
        written += size_t(put);
    }
    if (close(fd) != 0) {
        unlink(temp.c_str());
        return false;
    }
    if (rename(temp.c_str(), path.c_str()) != 0) {
        unlink(temp.c_str());
        return false;
    }
    return true;
}

std::vector<std::string> listDirectory(const std::string& path)
{
    std::vector<std::string> names;
    DIR* dir = opendir(path.c_str());
    if (!dir)
        return names;
    while (dirent* entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        names.push_back(entry->d_name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end());
    return names;
}

std::string humanReadableSize(uint64_t bytes)
{
    static const char* const units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = double(bytes);
    int unit = 0;
    while (size >= 1000.0 && unit < 4) {
        size /= 1000.0;
        ++unit;
    }
    char buffer[64];
    if (unit == 0)
        std::snprintf(buffer, sizeof(buffer), "%u B", unsigned(bytes));
    else
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", size, units[unit]);
    return buffer;
}

// -- the machine -----------------------------------------------------------

bool isEfiSystem()
{
    struct stat st;
    return stat("/sys/firmware/efi", &st) == 0;
}

uint64_t systemMemory()
{
    std::string proc;
    if (!readFile("/proc/meminfo", proc))
        return 0;
    const size_t at = proc.find("MemTotal:");
    if (at == std::string::npos)
        return 0;
    const uint64_t kb = strtoull(proc.c_str() + at + 9, nullptr, 10);
    return kb * 1024ull;
}

bool isLiveSession()
{
    std::string command;
    if (!readFile("/proc/cmdline", command))
        return false;
    const auto has = [&](const char* word) { return command.find(word) != std::string::npos; };
    if (has("boot=live") || has("boot=casper") || has("root=/run/live"))
        return true;
    // The image itself, mounted through a squashfs, is a live session too.
    std::string mounts;
    if (readFile("/proc/self/mounts", mounts))
        return mounts.find(" squashfs ") != std::string::npos;
    return false;
}

bool runningAsRoot()
{
    return geteuid() == 0;
}

// -- running the work -------------------------------------------------------

std::string Command::describe() const
{
    std::string text = program;
    for (const std::string& argument : arguments) {
        text += ' ';
        bool plain = !argument.empty();
        for (const char c : argument)
            plain = plain && (isalnum(unsigned(c)) || strchr("/._-+:=@%,", c));
        if (plain) {
            text += argument;
        } else {
            text += '\'';
            for (const char c : argument) {
                if (c == '\'')
                    text += "'\\''";
                else
                    text += c;
            }
            text += '\'';
        }
    }
    return text;
}

CommandResult runCommand(const std::string& program, const std::vector<std::string>& arguments,
                         const std::function<void(const std::string&)>& onOutput,
                         const std::function<bool()>& cancelled)
{
    return runCommandWithInput(program, arguments, {}, onOutput, cancelled);
}

CommandResult runCommand(const Command& command,
                         const std::function<void(const std::string&)>& onOutput,
                         const std::function<bool()>& cancelled)
{
    return runCommandWithInput(command.program, command.arguments, {}, onOutput, cancelled);
}

CommandResult runCommandWithInput(const std::string& program, const std::vector<std::string>& arguments,
                                  std::string input,
                                  const std::function<void(const std::string&)>& onOutput,
                                  const std::function<bool()>& cancelled)
{
    CommandResult result;

    int outPipe[2];
    int inPipe[2];
    if (pipe(outPipe) != 0 || pipe(inPipe) != 0) {
        result.exitCode = -1;
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        result.exitCode = -1;
        return result;
    }

    if (pid == 0) {
        // The child: its input from one pipe, its output and errors to the
        // other, then the command in this one's place.
        close(outPipe[0]);
        close(inPipe[1]);
        dup2(inPipe[0], 0);
        dup2(outPipe[1], 1);
        dup2(outPipe[1], 2);
        close(outPipe[1]);
        close(inPipe[0]);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 2);
        argv.push_back(const_cast<char*>(program.c_str()));
        for (const std::string& argument : arguments)
            argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        execvp(program.c_str(), argv.data());
        _exit(127);
    }

    close(outPipe[1]);
    close(inPipe[0]);

    // The input goes in whole before the output is read; what these commands
    // read, they read to the end.
    size_t written = 0;
    while (written < input.size()) {
        const ssize_t put = write(inPipe[1], input.data() + written, input.size() - written);
        if (put <= 0)
            break;
        written += size_t(put);
    }
    close(inPipe[1]);

    std::string output;
    std::string piece;
    char buffer[16384];
    for (;;) {
        if (cancelled && cancelled()) {
            kill(pid, SIGTERM);
            break;
        }
        const ssize_t got = read(outPipe[0], buffer, sizeof(buffer));
        if (got <= 0)
            break;
        piece.assign(buffer, size_t(got));
        output += piece;
        if (onOutput)
            onOutput(piece);
    }
    close(outPipe[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        result.exitCode = -1;
    else if (WIFEXITED(status))
        result.exitCode = WEXITSTATUS(status);
    else
        result.exitCode = 128 + WTERMSIG(status);
    result.output = std::move(output);
    return result;
}

bool writeFileMode(const std::string& path, const std::string& data, mode_t mode)
{
    if (!writeFile(path, data))
        return false;
    return chmod(path.c_str(), mode) == 0;
}

bool writeKeyValueFile(const std::string& path, const std::vector<std::pair<std::string, std::string>>& entries)
{
    std::string text;
    std::vector<std::pair<std::string, std::string>> missing = entries;
    std::string whole;
    if (readFile(path, whole)) {
        size_t at = 0;
        while (at < whole.size()) {
            size_t end = whole.find('\n', at);
            const std::string line = whole.substr(at, end == std::string::npos ? std::string::npos : end - at);
            at = end == std::string::npos ? whole.size() : end + 1;
            const size_t equals = line.find('=');
            if (equals != std::string::npos) {
                const std::string key = line.substr(0, equals);
                const auto known = std::find_if(missing.begin(), missing.end(),
                                               [&](const auto& pair) { return pair.first == key; });
                if (known != missing.end()) {
                    text += key + '=' + known->second + '\n';
                    missing.erase(known);
                    continue;
                }
            }
            text += line + '\n';
        }
    }
    for (const auto& pair : missing)
        text += pair.first + '=' + pair.second + '\n';
    return writeFile(path, text);
}

std::string suggestUsername(const std::string& fullName)
{
    std::string name;
    bool hyphen = false;
    for (const char c : fullName) {
        const unsigned char letter = static_cast<unsigned char>(c);
        if (isalnum(letter) && letter < 128) {
            name += char(tolower(letter));
            hyphen = false;
        } else if (!name.empty() && !hyphen) {
            name += '-';
            hyphen = true;
        }
    }
    while (!name.empty() && (name.back() == '-'))
        name.pop_back();
    return name;
}

std::string suggestHostname(const std::string& text)
{
    std::string name;
    bool hyphen = false;
    for (const char c : text) {
        const unsigned char letter = static_cast<unsigned char>(c);
        if (isalnum(letter) && letter < 128) {
            name += char(tolower(letter));
            hyphen = false;
        } else if (!name.empty() && !hyphen) {
            name += '-';
            hyphen = true;
        }
    }
    while (!name.empty() && (name.back() == '-'))
        name.pop_back();
    return name;
}


// The rules themselves, written once for the plan and the pages alike.
bool validUsernameName(const std::string& name)
{
    if (name.empty() || name.size() > 32)
        return false;
    if (!isalpha(unsigned(name[0])) && name[0] != '_')
        return false;
    for (const char c : name) {
        if (!isalnum(unsigned(c)) && c != '-' && c != '_')
            return false;
    }
    return true;
}

bool validHostName(const std::string& name)
{
    if (name.empty() || name.size() > 63)
        return false;
    if (name.front() == '-' || name.back() == '-')
        return false;
    for (const char c : name) {
        if (!isalnum(unsigned(c)) && c != '-')
            return false;
    }
    return true;
}

std::string systemTimeZone()
{
    char buffer[512];
    const ssize_t length = readlink("/etc/localtime", buffer, sizeof(buffer) - 1);
    if (length <= 0)
        return "UTC";
    buffer[length] = '\0';
    const std::string path(buffer);
    static const char* const prefix = "/usr/share/zoneinfo/";
    if (path.compare(0, strlen(prefix), prefix) == 0)
        return path.substr(strlen(prefix));
    return "UTC";
}

} // namespace cutefish::installer
