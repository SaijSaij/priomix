// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Pure D-Bus client for priomix-daemon. Mirrors the daemon state and re-emits changes as
// Qt signals. All calls are asynchronous so a busy daemon (e.g. a slow HID read) never
// blocks the UI. The GUI knows nothing about PipeWire or the config files.

#include <QDBusPendingCall>
#include <QObject>
#include <QStringList>

#include "device_info.hpp"

class PriomixInterface;
class QDBusServiceWatcher;

namespace priomix::gui {

class DbusClient : public QObject {
    Q_OBJECT

public:
    explicit DbusClient(QObject* parent = nullptr);

    bool isConnected() const { return connected_; }

    const DeviceInfoList& sinks() const { return sinks_; }
    const DeviceInfoList& sources() const { return sources_; }
    const QStringList& gamePriority() const { return game_priority_; }
    const QStringList& chatPriority() const { return chat_priority_; }
    const QStringList& sourcePriority() const { return source_priority_; }
    bool syncPriorityLists() const { return sync_; }
    const QString& activeGameDevice() const { return active_game_; }
    const QString& activeChatDevice() const { return active_chat_; }
    const QString& activeSource() const { return active_source_; }
    uint gameVolume() const { return game_volume_; }
    uint chatVolume() const { return chat_volume_; }
    uint chatMixBalance() const { return balance_; }
    bool hardwareChatMixActive() const { return hardware_chatmix_; }

    // Display name for a key, falling back to the raw key.
    QString displayName(const QString& key) const;

    void setGamePriority(const QStringList& keys);
    void setChatPriority(const QStringList& keys);
    void setSourcePriority(const QStringList& keys);
    void setSyncPriorityLists(bool enabled);
    void setSinkIgnored(const QString& key, bool ignored);
    void setSourceIgnored(const QString& key, bool ignored);
    void setGameVolume(uint volume);
    void setChatVolume(uint volume);
    void setChatMixBalance(uint balance);
    void adjustChatMixBalance(int delta);
    void triggerReevaluation();

    // Blocking helpers for the settings dialog (small, explicit user action).
    bool pollIntervals(uint& ac_ms, uint& battery_ms);
    void setPollIntervals(uint ac_ms, uint battery_ms);

signals:
    void connectedChanged(bool connected);
    void devicesChanged();     // sinks/sources incl. availability and ignore state
    void prioritiesChanged();  // any of the three lists or the sync flag
    void activeDevicesChanged();
    void volumesChanged();
    void chatMixChanged();  // balance or hardware wheel state
    void errorOccurred(const QString& message);

private:
    void refreshAll();
    void fetchSinks();
    void fetchSources();
    void fetchGamePriority();
    void fetchChatPriority();
    void fetchSourcePriority();
    void setConnected(bool connected);
    // Reports D-Bus errors of fire-and-forget calls.
    void watch(const QDBusPendingCall& call, bool refreshOnError = false);

    PriomixInterface* iface_ = nullptr;
    QDBusServiceWatcher* watcher_ = nullptr;
    bool connected_ = false;

    DeviceInfoList sinks_;
    DeviceInfoList sources_;
    QStringList game_priority_;
    QStringList chat_priority_;
    QStringList source_priority_;
    bool sync_ = true;
    QString active_game_;
    QString active_chat_;
    QString active_source_;
    uint game_volume_ = 100;
    uint chat_volume_ = 100;
    uint balance_ = 50;
    bool hardware_chatmix_ = false;
};

}  // namespace priomix::gui
