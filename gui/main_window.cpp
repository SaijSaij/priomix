// SPDX-License-Identifier: GPL-3.0-only
#include "main_window.hpp"

#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QVBoxLayout>

#include "dbus_client.hpp"
#include "settings_dialog.hpp"

namespace priomix::gui {

namespace {

constexpr int kKeyRole = Qt::UserRole;

QStringList KeysOf(const QListWidget* list) {
    QStringList keys;
    for (int i = 0; i < list->count(); ++i)
        keys << list->item(i)->data(kKeyRole).toString();
    return keys;
}

const DeviceInfo* FindDevice(const DeviceInfoList& devices, const QString& key) {
    for (const auto& device : devices) {
        if (device.key == key) return &device;
    }
    return nullptr;
}

QIcon ThemeIcon(const char* name, const char* fallback) {
    return QIcon::fromTheme(QString::fromLatin1(name),
                            QIcon::fromTheme(QString::fromLatin1(fallback)));
}

}  // namespace

MainWindow::MainWindow(DbusClient* client, QWidget* parent)
    : QMainWindow(parent), client_(client) {
    setWindowTitle(tr("priomix"));
    setWindowIcon(ThemeIcon("audio-card", "audio-volume-high"));

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    banner_ = new QLabel(tr("priomix-daemon is not running. Start it with "
                            "<tt>systemctl --user start priomix-daemon</tt>."),
                         central);
    banner_->setWordWrap(true);
    banner_->setStyleSheet(QStringLiteral(
        "QLabel { background: palette(highlight); color: palette(highlighted-text);"
        " padding: 8px; border-radius: 4px; }"));
    root->addWidget(banner_);

    content_ = new QWidget(central);
    auto* content = new QVBoxLayout(content_);
    content->setContentsMargins(0, 0, 0, 0);

    sync_checkbox_ = new QCheckBox(tr("Keep game and chat priority in sync"), content_);
    connect(sync_checkbox_, &QCheckBox::toggled, this, [this](bool checked) {
        if (!updating_) client_->setSyncPriorityLists(checked);
    });
    content->addWidget(sync_checkbox_);

    auto* grid = new QGridLayout();
    grid->addWidget(createPriorityColumn(tr("Game output"), ListKind::Game, &game_list_),
                    0, 0);
    grid->addWidget(createPriorityColumn(tr("Chat output"), ListKind::Chat, &chat_list_),
                    0, 1);
    grid->addWidget(
        createPriorityColumn(tr("Microphones"), ListKind::Source, &source_list_), 0, 2);
    grid->addWidget(createIgnoredSection(tr("Ignored outputs"), true,
                                         &ignored_sinks_toggle_, &ignored_sinks_list_),
                    1, 0, 1, 2);
    grid->addWidget(
        createIgnoredSection(tr("Ignored microphones"), false, &ignored_sources_toggle_,
                             &ignored_sources_list_),
        1, 2);
    grid->setRowStretch(0, 1);
    content->addLayout(grid, 1);

    content->addWidget(createChatMixSection());

    auto* buttons = new QHBoxLayout();
    auto* recheck = new QPushButton(ThemeIcon("view-refresh", "reload"),
                                    tr("Re-check now"), content_);
    recheck->setToolTip(
        tr("Query the headsets and re-evaluate all priorities immediately"));
    connect(recheck, &QPushButton::clicked, client_, &DbusClient::triggerReevaluation);
    auto* settings = new QPushButton(ThemeIcon("configure", "preferences-system"),
                                     tr("Settings…"), content_);
    connect(settings, &QPushButton::clicked, this, &MainWindow::openSettings);
    buttons->addWidget(recheck);
    buttons->addStretch();
    buttons->addWidget(settings);
    content->addLayout(buttons);

    root->addWidget(content_, 1);
    setCentralWidget(central);
    resize(900, 600);

    createTrayIcon();

    connect(client_, &DbusClient::connectedChanged, this,
            &MainWindow::updateConnectionState);
    connect(client_, &DbusClient::devicesChanged, this, &MainWindow::rebuildLists);
    connect(client_, &DbusClient::prioritiesChanged, this, &MainWindow::rebuildLists);
    connect(client_, &DbusClient::activeDevicesChanged, this, [this] {
        rebuildLists();
        updateTrayTooltip();
    });
    connect(client_, &DbusClient::volumesChanged, this, &MainWindow::updateVolumes);
    connect(client_, &DbusClient::chatMixChanged, this, &MainWindow::updateChatMix);
    connect(client_, &DbusClient::errorOccurred, this, [this](const QString& message) {
        if (tray_ != nullptr) {
            tray_->showMessage(tr("priomix"), message, QSystemTrayIcon::Warning, 4000);
        }
    });

    updateConnectionState();
    rebuildLists();
    updateVolumes();
    updateChatMix();
}

QWidget* MainWindow::createPriorityColumn(const QString& title, ListKind kind,
                                          QListWidget** list) {
    auto* box = new QGroupBox(title, content_);
    auto* layout = new QVBoxLayout(box);
    auto* widget = new QListWidget(box);
    widget->setDragDropMode(QAbstractItemView::InternalMove);
    widget->setDefaultDropAction(Qt::MoveAction);
    widget->setSelectionMode(QAbstractItemView::SingleSelection);
    widget->setContextMenuPolicy(Qt::CustomContextMenu);
    widget->setTextElideMode(Qt::ElideRight);
    widget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    widget->setToolTip(tr("Drag to reorder: the first available device wins"));
    connect(widget->model(), &QAbstractItemModel::rowsMoved, this,
            [this, kind] { onRowsMoved(kind); });
    bool sinks = kind != ListKind::Source;
    connect(widget, &QListWidget::customContextMenuRequested, this,
            [this, widget, sinks](const QPoint& pos) {
                showContextMenu(widget, pos, sinks, false);
            });
    layout->addWidget(widget);
    *list = widget;
    return box;
}

QWidget* MainWindow::createIgnoredSection(const QString& title, bool sinks,
                                          QToolButton** toggle, QListWidget** list) {
    auto* section = new QWidget(content_);
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* button = new QToolButton(section);
    button->setText(title);
    button->setCheckable(true);
    button->setChecked(false);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setArrowType(Qt::RightArrow);
    button->setAutoRaise(true);

    auto* widget = new QListWidget(section);
    widget->setVisible(false);
    widget->setTextElideMode(Qt::ElideRight);
    widget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    widget->setMaximumHeight(110);
    widget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(widget, &QListWidget::customContextMenuRequested, this,
            [this, widget, sinks](const QPoint& pos) {
                showContextMenu(widget, pos, sinks, true);
            });
    connect(button, &QToolButton::toggled, this, [button, widget](bool expanded) {
        button->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        widget->setVisible(expanded);
    });

    layout->addWidget(button);
    layout->addWidget(widget);
    *toggle = button;
    *list = widget;
    return section;
}

QWidget* MainWindow::createChatMixSection() {
    auto* box = new QGroupBox(tr("ChatMix"), content_);
    auto* grid = new QGridLayout(box);

    auto makeVolume = [this, box](QSlider** slider, QLabel** label) {
        *slider = new QSlider(Qt::Horizontal, box);
        (*slider)->setRange(0, 100);
        (*slider)->setPageStep(5);
        *label = new QLabel(box);
        (*label)->setMinimumWidth(
            QFontMetrics((*label)->font()).horizontalAdvance(QStringLiteral("100 %")));
    };
    makeVolume(&game_volume_, &game_volume_label_);
    makeVolume(&chat_volume_, &chat_volume_label_);
    connect(game_volume_, &QSlider::valueChanged, this, [this](int value) {
        game_volume_label_->setText(tr("%1 %").arg(value));
        if (!updating_) client_->setGameVolume(static_cast<uint>(value));
    });
    connect(chat_volume_, &QSlider::valueChanged, this, [this](int value) {
        chat_volume_label_->setText(tr("%1 %").arg(value));
        if (!updating_) client_->setChatVolume(static_cast<uint>(value));
    });

    grid->addWidget(new QLabel(tr("Game volume"), box), 0, 0);
    grid->addWidget(game_volume_, 0, 1, 1, 3);
    grid->addWidget(game_volume_label_, 0, 4);
    grid->addWidget(new QLabel(tr("Chat volume"), box), 1, 0);
    grid->addWidget(chat_volume_, 1, 1, 1, 3);
    grid->addWidget(chat_volume_label_, 1, 4);

    balance_ = new QSlider(Qt::Horizontal, box);
    balance_->setRange(0, 100);
    balance_->setPageStep(10);
    balance_->setTickPosition(QSlider::TicksBelow);
    balance_->setTickInterval(50);
    balance_->setToolTip(tr("Left: more chat, right: more game"));
    connect(balance_, &QSlider::valueChanged, this, [this](int value) {
        if (!updating_) client_->setChatMixBalance(static_cast<uint>(value));
    });
    center_button_ = new QToolButton(box);
    center_button_->setText(tr("Center"));
    connect(center_button_, &QToolButton::clicked, this,
            [this] { balance_->setValue(50); });

    grid->addWidget(new QLabel(tr("Balance"), box), 2, 0);
    grid->addWidget(new QLabel(tr("Chat"), box), 2, 1);
    grid->addWidget(balance_, 2, 2);
    grid->addWidget(new QLabel(tr("Game"), box), 2, 3);
    grid->addWidget(center_button_, 2, 4);

    balance_hint_ = new QLabel(tr("Controlled by the headset's ChatMix wheel"), box);
    balance_hint_->setEnabled(false);
    grid->addWidget(balance_hint_, 3, 2);
    grid->setColumnStretch(2, 1);
    return box;
}

void MainWindow::createTrayIcon() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    tray_ = new QSystemTrayIcon(windowIcon(), this);
    auto* menu = new QMenu(this);
    menu->addAction(tr("Show / hide window"), this, &MainWindow::toggleVisible);
    menu->addAction(ThemeIcon("view-refresh", "reload"), tr("Re-check now"), client_,
                    &DbusClient::triggerReevaluation);
    menu->addSeparator();
    menu->addAction(ThemeIcon("application-exit", "window-close"), tr("Quit"), this,
                    [this] {
                        quitting_ = true;
                        qApp->quit();
                    });
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger) toggleVisible();
            });
    tray_->show();
    updateTrayTooltip();
}

void MainWindow::toggleVisible() {
    if (isVisible() && !isMinimized()) {
        hide();
    } else {
        showNormal();
        raise();
        activateWindow();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // With a tray icon, closing the window only hides it (hotkeys keep working).
    if (tray_ != nullptr && tray_->isVisible() && !quitting_) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::fillPriorityList(QListWidget* list, const QStringList& order, bool sinks,
                                  const QString& active) {
    const DeviceInfoList& devices = sinks ? client_->sinks() : client_->sources();
    int current = list->currentRow();
    list->clear();
    for (const QString& key : order) {
        const DeviceInfo* device = FindDevice(devices, key);
        if (device != nullptr && device->ignored) continue;

        QString name = device != nullptr && !device->displayName.isEmpty()
                           ? device->displayName
                           : (key.isEmpty() ? tr("Unknown device") : key);
        auto* item = new QListWidgetItem(name, list);
        item->setData(kKeyRole, key);
        QString tooltip = name + QLatin1Char('\n') + key;
        bool available = device != nullptr && device->available;
        if (key == active && !active.isEmpty()) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setIcon(ThemeIcon("audio-volume-high", "media-playback-start"));
            tooltip += QLatin1Char('\n') + tr("Active");
        } else if (!available) {
            item->setForeground(
                list->palette().brush(QPalette::Disabled, QPalette::Text));
            QFont font = item->font();
            font.setItalic(true);
            item->setFont(font);
            tooltip += QLatin1Char('\n') + tr("Not available");
        }
        item->setToolTip(tooltip);
    }
    if (current >= 0 && current < list->count()) list->setCurrentRow(current);
}

void MainWindow::fillIgnoredList(QListWidget* list, QToolButton* toggle,
                                 const QString& title, bool sinks) {
    const DeviceInfoList& devices = sinks ? client_->sinks() : client_->sources();
    list->clear();
    for (const auto& device : devices) {
        if (!device.ignored) continue;
        auto* item = new QListWidgetItem(
            device.displayName.isEmpty() ? device.key : device.displayName, list);
        item->setData(kKeyRole, device.key);
        item->setToolTip(device.key);
    }
    toggle->setText(QStringLiteral("%1 (%2)").arg(title).arg(list->count()));
}

void MainWindow::rebuildLists() {
    updating_ = true;
    sync_checkbox_->setChecked(client_->syncPriorityLists());
    fillPriorityList(game_list_, client_->gamePriority(), true,
                     client_->activeGameDevice());
    fillPriorityList(
        chat_list_,
        client_->syncPriorityLists() ? client_->gamePriority() : client_->chatPriority(),
        true, client_->activeChatDevice());
    fillPriorityList(source_list_, client_->sourcePriority(), false,
                     client_->activeSource());
    // While synced the chat list mirrors the game list and is read only.
    chat_list_->setEnabled(!client_->syncPriorityLists());
    fillIgnoredList(ignored_sinks_list_, ignored_sinks_toggle_, tr("Ignored outputs"),
                    true);
    fillIgnoredList(ignored_sources_list_, ignored_sources_toggle_,
                    tr("Ignored microphones"), false);
    updating_ = false;
}

void MainWindow::onRowsMoved(ListKind kind) {
    if (updating_) return;
    // Ignored devices are hidden but stay in the daemon's list; keep them at the end.
    auto withIgnored = [](QStringList visible, const QStringList& full,
                          const DeviceInfoList& devices) {
        for (const QString& key : full) {
            const DeviceInfo* device = FindDevice(devices, key);
            if (device != nullptr && device->ignored) visible << key;
        }
        return visible;
    };
    switch (kind) {
        case ListKind::Game:
            client_->setGamePriority(withIgnored(
                KeysOf(game_list_), client_->gamePriority(), client_->sinks()));
            if (client_->syncPriorityLists())
                rebuildLists();  // mirror into the chat list
            break;
        case ListKind::Chat:
            client_->setChatPriority(withIgnored(
                KeysOf(chat_list_), client_->chatPriority(), client_->sinks()));
            break;
        case ListKind::Source:
            client_->setSourcePriority(withIgnored(
                KeysOf(source_list_), client_->sourcePriority(), client_->sources()));
            break;
    }
}

void MainWindow::showContextMenu(QListWidget* list, const QPoint& pos, bool sinks,
                                 bool ignored) {
    QListWidgetItem* item = list->itemAt(pos);
    if (item == nullptr) return;
    const QString key = item->data(kKeyRole).toString();
    QMenu menu(this);
    QAction* action =
        ignored ? menu.addAction(tr("Stop ignoring"))
                : menu.addAction(ThemeIcon("view-hidden", "list-remove"), tr("Ignore"));
    if (!ignored && sinks) {
        action->setToolTip(tr("Applies to game and chat output"));
    }
    if (menu.exec(list->viewport()->mapToGlobal(pos)) != action) return;
    if (sinks) {
        client_->setSinkIgnored(key, !ignored);
    } else {
        client_->setSourceIgnored(key, !ignored);
    }
}

void MainWindow::updateVolumes() {
    updating_ = true;
    game_volume_->setValue(static_cast<int>(std::min(client_->gameVolume(), 100u)));
    chat_volume_->setValue(static_cast<int>(std::min(client_->chatVolume(), 100u)));
    game_volume_label_->setText(tr("%1 %").arg(client_->gameVolume()));
    chat_volume_label_->setText(tr("%1 %").arg(client_->chatVolume()));
    updating_ = false;
}

void MainWindow::updateChatMix() {
    updating_ = true;
    // Don't fight the user while they drag.
    if (!balance_->isSliderDown())
        balance_->setValue(static_cast<int>(client_->chatMixBalance()));
    bool hardware = client_->hardwareChatMixActive();
    balance_->setEnabled(!hardware);
    center_button_->setEnabled(!hardware);
    balance_hint_->setVisible(hardware);
    updating_ = false;
}

void MainWindow::updateTrayTooltip() {
    if (tray_ == nullptr) return;
    if (!client_->isConnected()) {
        tray_->setToolTip(tr("priomix: daemon not running"));
        return;
    }
    tray_->setToolTip(tr("priomix\nGame: %1\nChat: %2\nMicrophone: %3")
                          .arg(client_->displayName(client_->activeGameDevice()),
                               client_->displayName(client_->activeChatDevice()),
                               client_->displayName(client_->activeSource())));
}

void MainWindow::updateConnectionState() {
    bool connected = client_->isConnected();
    banner_->setVisible(!connected);
    content_->setEnabled(connected);
    updateTrayTooltip();
}

void MainWindow::openSettings() {
    uint ac_ms = 0;
    uint battery_ms = 0;
    if (!client_->pollIntervals(ac_ms, battery_ms)) return;
    SettingsDialog dialog(ac_ms, battery_ms, this);
    if (dialog.exec() == QDialog::Accepted) {
        client_->setPollIntervals(dialog.acIntervalMs(), dialog.batteryIntervalMs());
    }
}

}  // namespace priomix::gui
