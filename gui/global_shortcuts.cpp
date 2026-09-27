// SPDX-License-Identifier: GPL-3.0-only
#include "global_shortcuts.hpp"

#include <KGlobalAccel>
#include <QAction>

#include "dbus_client.hpp"

namespace priomix::gui {

GlobalShortcuts::GlobalShortcuts(DbusClient* client, QObject* parent) : QObject(parent) {
    struct Definition {
        const char* id;
        QString text;
        std::function<void()> trigger;
    };
    const Definition definitions[] = {
        {"chatmix-more-game", tr("ChatMix: more game"),
         [client] { client->adjustChatMixBalance(kStep); }},
        {"chatmix-more-chat", tr("ChatMix: more chat"),
         [client] { client->adjustChatMixBalance(-kStep); }},
        {"chatmix-center", tr("ChatMix: center"),
         [client] {
             if (!client->hardwareChatMixActive()) client->setChatMixBalance(50);
         }},
    };

    for (const auto& definition : definitions) {
        auto* action = new QAction(definition.text, this);
        // The object name is the stable shortcut ID stored by kglobalaccel.
        action->setObjectName(QString::fromLatin1(definition.id));
        connect(action, &QAction::triggered, this, definition.trigger);
        KGlobalAccel::setGlobalShortcut(action, QList<QKeySequence>());
    }
}

}  // namespace priomix::gui
