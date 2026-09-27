// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Session bus service io.github.saijsaij.Priomix. The adaptor base class is generated
// from common/io.github.saijsaij.Priomix.xml by sdbus-c++-xml2cpp at build time. This is
// the only place where D-Bus errors are turned into sdbus::Error exceptions; the daemon
// core itself reports failures through Status values.

#include <sdbus-c++/sdbus-c++.h>
#include <systemd/sd-event.h>

#include <memory>

#include "priomix_adaptor.h"

namespace priomix {

class Daemon;

// Base-from-member: the connection must exist before the adaptor base is constructed.
struct DbusConnectionHolder {
    std::unique_ptr<sdbus::IConnection> connection;
};

class DbusService final
    : private DbusConnectionHolder,
      public sdbus::AdaptorInterfaces<io::github::saijsaij::Priomix_adaptor> {
public:
    static constexpr const char* kServiceName = "io.github.saijsaij.Priomix";
    static constexpr const char* kObjectPath = "/io/github/saijsaij/Priomix";

    // Throws sdbus::Error if the name is already owned (second instance).
    DbusService(Daemon& daemon, sd_event* event);
    ~DbusService();

private:
    using DeviceStruct = sdbus::Struct<std::string, std::string, bool, bool>;

    std::vector<DeviceStruct> ListSinks() override;
    std::vector<DeviceStruct> ListSources() override;
    void SetSinkIgnored(const std::string& key, const bool& ignored) override;
    void SetSourceIgnored(const std::string& key, const bool& ignored) override;
    std::vector<std::string> GetGameSinkPriority() override;
    void SetGameSinkPriority(const std::vector<std::string>& keys) override;
    std::vector<std::string> GetChatSinkPriority() override;
    void SetChatSinkPriority(const std::vector<std::string>& keys) override;
    std::vector<std::string> GetSourcePriority() override;
    void SetSourcePriority(const std::vector<std::string>& keys) override;
    bool GetSyncPriorityLists() override;
    void SetSyncPriorityLists(const bool& enabled) override;
    std::string GetActiveGameDevice() override;
    std::string GetActiveChatDevice() override;
    std::string GetActiveSource() override;
    std::tuple<uint32_t, uint32_t> GetPollIntervals() override;
    void SetPollIntervals(const uint32_t& ac_ms, const uint32_t& battery_ms) override;
    void TriggerReevaluation() override;
    uint32_t GetGameVolume() override;
    void SetGameVolume(const uint32_t& volume) override;
    uint32_t GetChatVolume() override;
    void SetChatVolume(const uint32_t& volume) override;
    uint32_t GetChatMixBalance() override;
    void SetChatMixBalance(const uint32_t& balance) override;
    void AdjustChatMixBalance(const int32_t& delta) override;
    bool GetHardwareChatMixActive() override;

    Daemon& daemon_;
};

}  // namespace priomix
