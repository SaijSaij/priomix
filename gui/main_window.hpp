// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Three priority lists side by side (game output / chat output / microphones), collapsed
// "ignored" sections below them, the ChatMix controls and a system tray icon.

#include <QMainWindow>

class QCheckBox;
class QLabel;
class QListWidget;
class QSlider;
class QSystemTrayIcon;
class QToolButton;
class QWidget;

namespace priomix::gui {

class DbusClient;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(DbusClient* client, QWidget* parent = nullptr);

    void toggleVisible();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    enum class ListKind { Game, Chat, Source };

    QWidget* createPriorityColumn(const QString& title, ListKind kind,
                                  QListWidget** list);
    QWidget* createIgnoredSection(const QString& title, bool sinks, QToolButton** toggle,
                                  QListWidget** list);
    QWidget* createChatMixSection();
    void createTrayIcon();

    void rebuildLists();
    void fillPriorityList(QListWidget* list, const QStringList& order, bool sinks,
                          const QString& active);
    void fillIgnoredList(QListWidget* list, QToolButton* toggle, const QString& title,
                         bool sinks);
    void onRowsMoved(ListKind kind);
    void showContextMenu(QListWidget* list, const QPoint& pos, bool sinks, bool ignored);
    void updateChatMix();
    void updateVolumes();
    void updateTrayTooltip();
    void updateConnectionState();
    void openSettings();

    DbusClient* client_;
    QSystemTrayIcon* tray_ = nullptr;
    QLabel* banner_ = nullptr;
    QWidget* content_ = nullptr;
    QCheckBox* sync_checkbox_ = nullptr;
    QListWidget* game_list_ = nullptr;
    QListWidget* chat_list_ = nullptr;
    QListWidget* source_list_ = nullptr;
    QToolButton* ignored_sinks_toggle_ = nullptr;
    QListWidget* ignored_sinks_list_ = nullptr;
    QToolButton* ignored_sources_toggle_ = nullptr;
    QListWidget* ignored_sources_list_ = nullptr;
    QSlider* game_volume_ = nullptr;
    QLabel* game_volume_label_ = nullptr;
    QSlider* chat_volume_ = nullptr;
    QLabel* chat_volume_label_ = nullptr;
    QSlider* balance_ = nullptr;
    QLabel* balance_hint_ = nullptr;
    QToolButton* center_button_ = nullptr;
    bool updating_ = false;  // suppresses feedback while applying daemon state
    bool quitting_ = false;
};

}  // namespace priomix::gui
