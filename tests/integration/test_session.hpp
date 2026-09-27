// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Isolated audio session for integration tests: a private D-Bus session bus, PipeWire
// and WirePlumber (profile "policy": linking policy and default nodes, but no ALSA,
// Bluetooth or camera monitors, so real hardware is never touched), plus priomix-daemon.
// Everything lives in a fresh /tmp/priomix-XXXXXX directory (short on purpose: unix
// socket paths are limited to 108 bytes) and is torn down afterwards. The calling
// process is switched to this session too, so libpipewire and sdbus-c++ connect there.

#include <string>
#include <sys/types.h>
#include <vector>

namespace priomix::test {

class TestSession {
public:
    TestSession() = default;
    ~TestSession();

    TestSession(const TestSession&) = delete;
    TestSession& operator=(const TestSession&) = delete;

    // Starts D-Bus, PipeWire and WirePlumber. On failure `error` explains why.
    bool Start(std::string& error);
    bool StartDaemon(std::string& error);
    void StopDaemon();
    void Stop();

    // Tail of all process logs, for failure diagnostics.
    std::string Logs() const;

    const std::string& Directory() const { return dir_; }

    // Empty if the program is not found in PATH.
    static std::string FindProgram(const std::string& name);

private:
    struct Process {
        std::string name;
        std::string log;
        pid_t pid = -1;
    };

    bool Spawn(const std::string& name, const std::vector<std::string>& argv,
               std::string& error);
    static void Terminate(Process& process);

    std::string dir_;
    std::vector<Process> processes_;  // in start order
};

}  // namespace priomix::test
