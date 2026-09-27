// SPDX-License-Identifier: GPL-3.0-only
#include "dbus_service.hpp"

#include "daemon.hpp"
#include "logging.hpp"

namespace priomix {

namespace {

std::unique_ptr<sdbus::IConnection> CreateConnection(sd_event* event) {
    auto connection =
        sdbus::createSessionBusConnection(sdbus::ServiceName{DbusService::kServiceName});
    connection->attachSdEventLoop(event);
    return connection;
}

void ThrowIfFailed(const Status& status) {
    if (!status.Ok())
        throw sdbus::Error(sdbus::Error::Name{status.error}, status.message);
}

}  // namespace

DbusService::DbusService(Daemon& daemon, sd_event* event)
    : DbusConnectionHolder{CreateConnection(event)},
      AdaptorInterfaces(*connection, sdbus::ObjectPath{kObjectPath}),
      daemon_(daemon) {
    registerAdaptor();
    PRIOMIX_LOG_INFO("D-Bus service %s registered", kServiceName);
}

DbusService::~DbusService() {
    unregisterAdaptor();
    connection->detachSdEventLoop();
}

std::vector<DbusService::DeviceStruct> DbusService::ListSinks() {
    std::vector<DeviceStruct> out;
    for (const auto& d : daemon_.ListSinks()) {
        out.emplace_back(d.key, d.display_name, d.available, d.ignored);
    }
    return out;
}

std::vector<DbusService::DeviceStruct> DbusService::ListSources() {
    std::vector<DeviceStruct> out;
    for (const auto& d : daemon_.ListSources()) {
        out.emplace_back(d.key, d.display_name, d.available, d.ignored);
    }
    return out;
}

void DbusService::SetSinkIgnored(const std::string& key, const bool& ignored) {
    ThrowIfFailed(daemon_.SetSinkIgnored(key, ignored));
}

void DbusService::SetSourceIgnored(const std::string& key, const bool& ignored) {
    ThrowIfFailed(daemon_.SetSourceIgnored(key, ignored));
}

std::vector<std::string> DbusService::GetGameSinkPriority() {
    return daemon_.GameSinkPriority();
}

void DbusService::SetGameSinkPriority(const std::vector<std::string>& keys) {
    ThrowIfFailed(daemon_.SetGameSinkPriority(keys));
}

std::vector<std::string> DbusService::GetChatSinkPriority() {
    return daemon_.ChatSinkPriority();
}

void DbusService::SetChatSinkPriority(const std::vector<std::string>& keys) {
    ThrowIfFailed(daemon_.SetChatSinkPriority(keys));
}

std::vector<std::string> DbusService::GetSourcePriority() {
    return daemon_.SourcePriority();
}

void DbusService::SetSourcePriority(const std::vector<std::string>& keys) {
    ThrowIfFailed(daemon_.SetSourcePriority(keys));
}

bool DbusService::GetSyncPriorityLists() {
    return daemon_.SyncPriorityLists();
}

void DbusService::SetSyncPriorityLists(const bool& enabled) {
    ThrowIfFailed(daemon_.SetSyncPriorityLists(enabled));
}

std::string DbusService::GetActiveGameDevice() {
    return daemon_.ActiveGameDevice();
}

std::string DbusService::GetActiveChatDevice() {
    return daemon_.ActiveChatDevice();
}

std::string DbusService::GetActiveSource() {
    return daemon_.ActiveSource();
}

std::tuple<uint32_t, uint32_t> DbusService::GetPollIntervals() {
    return daemon_.PollIntervals();
}

void DbusService::SetPollIntervals(const uint32_t& ac_ms, const uint32_t& battery_ms) {
    ThrowIfFailed(daemon_.SetPollIntervals(ac_ms, battery_ms));
}

void DbusService::TriggerReevaluation() {
    daemon_.TriggerReevaluation();
}

uint32_t DbusService::GetGameVolume() {
    return daemon_.GameVolume();
}

void DbusService::SetGameVolume(const uint32_t& volume) {
    ThrowIfFailed(daemon_.SetGameVolume(volume));
}

uint32_t DbusService::GetChatVolume() {
    return daemon_.ChatVolume();
}

void DbusService::SetChatVolume(const uint32_t& volume) {
    ThrowIfFailed(daemon_.SetChatVolume(volume));
}

uint32_t DbusService::GetChatMixBalance() {
    return daemon_.ChatMixBalance();
}

void DbusService::SetChatMixBalance(const uint32_t& balance) {
    ThrowIfFailed(daemon_.SetChatMixBalance(balance));
}

void DbusService::AdjustChatMixBalance(const int32_t& delta) {
    ThrowIfFailed(daemon_.AdjustChatMixBalance(delta));
}

bool DbusService::GetHardwareChatMixActive() {
    return daemon_.HardwareChatMixActive();
}

}  // namespace priomix
