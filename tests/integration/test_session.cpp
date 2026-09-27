// SPDX-License-Identifier: GPL-3.0-only
#include "test_session.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace priomix::test {

namespace {

using namespace std::chrono_literals;

bool WaitForPath(const std::string& path, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(path)) return true;
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

std::string Tail(const std::string& path, size_t max_lines) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    std::ostringstream out;
    size_t start = lines.size() > max_lines ? lines.size() - max_lines : 0;
    for (size_t i = start; i < lines.size(); ++i) out << "    " << lines[i] << '\n';
    return out.str();
}

}  // namespace

TestSession::~TestSession() {
    Stop();
}

std::string TestSession::FindProgram(const std::string& name) {
    const char* path = std::getenv("PATH");
    std::istringstream dirs(path != nullptr ? path : "/usr/bin");
    for (std::string dir; std::getline(dirs, dir, ':');) {
        std::string candidate = dir + "/" + name;
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return {};
}

bool TestSession::Start(std::string& error) {
    char templ[] = "/tmp/priomix-XXXXXX";
    if (::mkdtemp(templ) == nullptr) {
        error = std::string("mkdtemp failed: ") + std::strerror(errno);
        return false;
    }
    dir_ = templ;
    for (const char* sub : {"/run", "/config", "/state"}) {
        std::filesystem::create_directories(dir_ + sub);
    }
    ::chmod((dir_ + "/run").c_str(), 0700);

    // Switch this process (and thereby every child) into the private session.
    const std::string bus = "unix:path=" + dir_ + "/run/bus";
    ::setenv("XDG_RUNTIME_DIR", (dir_ + "/run").c_str(), 1);
    ::setenv("PIPEWIRE_RUNTIME_DIR", (dir_ + "/run").c_str(), 1);
    ::setenv("XDG_CONFIG_HOME", (dir_ + "/config").c_str(), 1);
    ::setenv("XDG_STATE_HOME", (dir_ + "/state").c_str(), 1);
    ::setenv("DBUS_SESSION_BUS_ADDRESS", bus.c_str(), 1);
    ::unsetenv("PIPEWIRE_REMOTE");
    ::unsetenv("PIPEWIRE_CORE");
    ::setenv("PRIOMIX_DEBUG", "1", 1);

    for (const char* program : {"dbus-daemon", "pipewire", "wireplumber"}) {
        if (FindProgram(program).empty()) {
            error = std::string(program) + " not found";
            return false;
        }
    }

    if (!Spawn("dbus-daemon",
               {"dbus-daemon", "--session", "--nofork", "--address=" + bus}, error)) {
        return false;
    }
    if (!WaitForPath(dir_ + "/run/bus", 5s)) {
        error = "private D-Bus did not come up";
        return false;
    }
    if (!Spawn("pipewire", {"pipewire"}, error)) return false;
    if (!WaitForPath(dir_ + "/run/pipewire-0", 5s)) {
        error = "private PipeWire did not come up";
        return false;
    }
    return Spawn("wireplumber", {"wireplumber", "--profile", "policy"}, error);
}

bool TestSession::StartDaemon(std::string& error) {
    return Spawn("priomix-daemon", {PRIOMIX_DAEMON_PATH}, error);
}

void TestSession::StopDaemon() {
    for (auto& process : processes_) {
        if (process.name == "priomix-daemon") Terminate(process);
    }
}

void TestSession::Stop() {
    for (auto it = processes_.rbegin(); it != processes_.rend(); ++it) Terminate(*it);
    processes_.clear();
    if (!dir_.empty() && std::getenv("PRIOMIX_KEEP_TEST_DIR") == nullptr) {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    dir_.clear();
}

bool TestSession::Spawn(const std::string& name, const std::vector<std::string>& argv,
                        std::string& error) {
    Process process;
    process.name = name;
    process.log = dir_ + "/" + name + ".log";

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, process.log.c_str(),
                                     O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

    std::vector<char*> args;
    for (const auto& arg : argv) args.push_back(const_cast<char*>(arg.c_str()));
    args.push_back(nullptr);

    int r =
        ::posix_spawnp(&process.pid, args[0], &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (r != 0) {
        error = "cannot start " + name + ": " + std::strerror(r);
        return false;
    }
    processes_.push_back(process);
    return true;
}

void TestSession::Terminate(Process& process) {
    if (process.pid <= 0) return;
    ::kill(process.pid, SIGTERM);
    for (int i = 0; i < 100; ++i) {  // up to 2 s for a clean shutdown
        if (::waitpid(process.pid, nullptr, WNOHANG) == process.pid) {
            process.pid = -1;
            return;
        }
        std::this_thread::sleep_for(20ms);
    }
    ::kill(process.pid, SIGKILL);
    ::waitpid(process.pid, nullptr, 0);
    process.pid = -1;
}

std::string TestSession::Logs() const {
    std::ostringstream out;
    for (const auto& process : processes_) {
        out << "--- " << process.name << " (" << process.log << ")\n"
            << Tail(process.log, 40);
    }
    return out.str();
}

}  // namespace priomix::test
