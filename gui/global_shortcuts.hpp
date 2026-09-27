// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Global ChatMix hotkeys via KGlobalAccel (works for Qt/KDE apps on Plasma Wayland too).
// The actions are registered without default keys; they are assigned once in
// System Settings -> Keyboard -> Shortcuts -> priomix. While a hardware wheel is active
// the daemon rejects the calls, so nothing special is needed here.

#include <QObject>

namespace priomix::gui {

class DbusClient;

class GlobalShortcuts : public QObject {
    Q_OBJECT

public:
    explicit GlobalShortcuts(DbusClient* client, QObject* parent = nullptr);

    static constexpr int kStep = 10;
};

}  // namespace priomix::gui
