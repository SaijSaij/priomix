// SPDX-License-Identifier: GPL-3.0-only
#include "dbus_client.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>

#include "priomix_interface.h"

namespace priomix::gui {

namespace {

const QString kService = QStringLiteral("io.github.saijsaij.Priomix");
const QString kPath = QStringLiteral("/io/github/saijsaij/Priomix");

// Runs `on_value` with the reply value once an asynchronous call finished successfully.
template <typename T, typename F>
void OnReply(QObject* context, const QDBusPendingReply<T>& reply, F on_value) {
    auto* watcher = new QDBusPendingCallWatcher(reply, context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context,
                     [on_value](QDBusPendingCallWatcher* w) {
                         QDBusPendingReply<T> r = *w;
                         if (!r.isError()) on_value(r.value());
                         w->deleteLater();
                     });
}

}  // namespace

DbusClient::DbusClient(QObject* parent) : QObject(parent) {
    RegisterDbusTypes();
    auto bus = QDBusConnection::sessionBus();
    iface_ = new PriomixInterface(kService, kPath, bus, this);

    watcher_ = new QDBusServiceWatcher(kService, bus,
                                       QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher_, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString&, const QString&, const QString& new_owner) {
                if (new_owner.isEmpty()) {
                    setConnected(false);
                } else {
                    refreshAll();
                }
            });

    connect(iface_, &PriomixInterface::SinksChanged, this, &DbusClient::fetchSinks);
    connect(iface_, &PriomixInterface::SourcesChanged, this, &DbusClient::fetchSources);
    connect(iface_, &PriomixInterface::GameSinkPriorityChanged, this,
            &DbusClient::fetchGamePriority);
    connect(iface_, &PriomixInterface::ChatSinkPriorityChanged, this,
            &DbusClient::fetchChatPriority);
    connect(iface_, &PriomixInterface::SourcePriorityChanged, this,
            &DbusClient::fetchSourcePriority);
    connect(iface_, &PriomixInterface::SyncPriorityListsChanged, this,
            [this](bool enabled) {
                sync_ = enabled;
                emit prioritiesChanged();
            });
    connect(iface_, &PriomixInterface::ActiveGameDeviceChanged, this,
            [this](const QString& k) {
                active_game_ = k;
                emit activeDevicesChanged();
            });
    connect(iface_, &PriomixInterface::ActiveChatDeviceChanged, this,
            [this](const QString& k) {
                active_chat_ = k;
                emit activeDevicesChanged();
            });
    connect(iface_, &PriomixInterface::ActiveSourceChanged, this,
            [this](const QString& k) {
                active_source_ = k;
                emit activeDevicesChanged();
            });
    connect(iface_, &PriomixInterface::GameVolumeChanged, this, [this](uint v) {
        game_volume_ = v;
        emit volumesChanged();
    });
    connect(iface_, &PriomixInterface::ChatVolumeChanged, this, [this](uint v) {
        chat_volume_ = v;
        emit volumesChanged();
    });
    connect(iface_, &PriomixInterface::ChatMixBalanceChanged, this, [this](uint b) {
        balance_ = b;
        emit chatMixChanged();
    });
    connect(iface_, &PriomixInterface::HardwareChatMixActiveChanged, this,
            [this](bool a) {
                hardware_chatmix_ = a;
                emit chatMixChanged();
            });

    if (bus.interface() != nullptr && bus.interface()->isServiceRegistered(kService)) {
        refreshAll();
    }
}

QString DbusClient::displayName(const QString& key) const {
    for (const auto* list : {&sinks_, &sources_}) {
        for (const auto& device : *list) {
            if (device.key == key && !device.displayName.isEmpty())
                return device.displayName;
        }
    }
    return key.isEmpty() ? tr("None") : key;
}

void DbusClient::setConnected(bool connected) {
    if (connected == connected_) return;
    connected_ = connected;
    emit connectedChanged(connected);
}

void DbusClient::refreshAll() {
    // The first answer tells us the daemon is really there.
    OnReply(this, iface_->GetSyncPriorityLists(), [this](bool enabled) {
        sync_ = enabled;
        setConnected(true);
        emit prioritiesChanged();
    });
    fetchSinks();
    fetchSources();
    fetchGamePriority();
    fetchChatPriority();
    fetchSourcePriority();
    OnReply(this, iface_->GetActiveGameDevice(), [this](const QString& k) {
        active_game_ = k;
        emit activeDevicesChanged();
    });
    OnReply(this, iface_->GetActiveChatDevice(), [this](const QString& k) {
        active_chat_ = k;
        emit activeDevicesChanged();
    });
    OnReply(this, iface_->GetActiveSource(), [this](const QString& k) {
        active_source_ = k;
        emit activeDevicesChanged();
    });
    OnReply(this, iface_->GetGameVolume(), [this](uint v) {
        game_volume_ = v;
        emit volumesChanged();
    });
    OnReply(this, iface_->GetChatVolume(), [this](uint v) {
        chat_volume_ = v;
        emit volumesChanged();
    });
    OnReply(this, iface_->GetChatMixBalance(), [this](uint b) {
        balance_ = b;
        emit chatMixChanged();
    });
    OnReply(this, iface_->GetHardwareChatMixActive(), [this](bool a) {
        hardware_chatmix_ = a;
        emit chatMixChanged();
    });
}

void DbusClient::fetchSinks() {
    OnReply(this, iface_->ListSinks(), [this](const DeviceInfoList& list) {
        sinks_ = list;
        emit devicesChanged();
        emit activeDevicesChanged();  // display names may have changed
    });
}

void DbusClient::fetchSources() {
    OnReply(this, iface_->ListSources(), [this](const DeviceInfoList& list) {
        sources_ = list;
        emit devicesChanged();
        emit activeDevicesChanged();
    });
}

void DbusClient::fetchGamePriority() {
    OnReply(this, iface_->GetGameSinkPriority(), [this](const QStringList& keys) {
        game_priority_ = keys;
        emit prioritiesChanged();
    });
}

void DbusClient::fetchChatPriority() {
    OnReply(this, iface_->GetChatSinkPriority(), [this](const QStringList& keys) {
        chat_priority_ = keys;
        emit prioritiesChanged();
    });
}

void DbusClient::fetchSourcePriority() {
    OnReply(this, iface_->GetSourcePriority(), [this](const QStringList& keys) {
        source_priority_ = keys;
        emit prioritiesChanged();
    });
}

void DbusClient::watch(const QDBusPendingCall& call, bool refreshOnError) {
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, refreshOnError](QDBusPendingCallWatcher* w) {
                if (w->isError()) {
                    emit errorOccurred(w->error().message());
                    // Resynchronize optimistic UI state with the daemon.
                    if (refreshOnError) refreshAll();
                }
                w->deleteLater();
            });
}

void DbusClient::setGamePriority(const QStringList& keys) {
    game_priority_ = keys;
    watch(iface_->SetGameSinkPriority(keys), true);
}

void DbusClient::setChatPriority(const QStringList& keys) {
    chat_priority_ = keys;
    watch(iface_->SetChatSinkPriority(keys), true);
}

void DbusClient::setSourcePriority(const QStringList& keys) {
    source_priority_ = keys;
    watch(iface_->SetSourcePriority(keys), true);
}

void DbusClient::setSyncPriorityLists(bool enabled) {
    watch(iface_->SetSyncPriorityLists(enabled), true);
}

void DbusClient::setSinkIgnored(const QString& key, bool ignored) {
    watch(iface_->SetSinkIgnored(key, ignored));
}

void DbusClient::setSourceIgnored(const QString& key, bool ignored) {
    watch(iface_->SetSourceIgnored(key, ignored));
}

void DbusClient::setGameVolume(uint volume) {
    game_volume_ = volume;
    watch(iface_->SetGameVolume(volume));
}

void DbusClient::setChatVolume(uint volume) {
    chat_volume_ = volume;
    watch(iface_->SetChatVolume(volume));
}

void DbusClient::setChatMixBalance(uint balance) {
    watch(iface_->SetChatMixBalance(balance), true);
}

void DbusClient::adjustChatMixBalance(int delta) {
    // Rejected by the daemon while a hardware wheel is active; that is expected for
    // hotkeys, so no error is shown.
    if (hardware_chatmix_) return;
    watch(iface_->AdjustChatMixBalance(delta));
}

void DbusClient::triggerReevaluation() {
    watch(iface_->TriggerReevaluation());
}

bool DbusClient::pollIntervals(uint& ac_ms, uint& battery_ms) {
    QDBusPendingReply<uint, uint> reply = iface_->GetPollIntervals();
    reply.waitForFinished();
    if (reply.isError()) {
        emit errorOccurred(reply.error().message());
        return false;
    }
    ac_ms = reply.argumentAt<0>();
    battery_ms = reply.argumentAt<1>();
    return true;
}

void DbusClient::setPollIntervals(uint ac_ms, uint battery_ms) {
    watch(iface_->SetPollIntervals(ac_ms, battery_ms));
}

}  // namespace priomix::gui
