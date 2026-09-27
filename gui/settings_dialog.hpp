// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Headset poll intervals (on AC power / on battery), in seconds.

#include <QDialog>

class QSpinBox;

namespace priomix::gui {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(uint ac_ms, uint battery_ms, QWidget* parent = nullptr);

    uint acIntervalMs() const;
    uint batteryIntervalMs() const;

private:
    QSpinBox* ac_ = nullptr;
    QSpinBox* battery_ = nullptr;
};

}  // namespace priomix::gui
