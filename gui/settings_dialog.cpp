// SPDX-License-Identifier: GPL-3.0-only
#include "settings_dialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace priomix::gui {

namespace {

QSpinBox* MakeSecondsBox(uint ms, QWidget* parent) {
    auto* box = new QSpinBox(parent);
    box->setRange(1, 3600);  // the daemon accepts 500 ms .. 1 h
    box->setSuffix(QObject::tr(" s"));
    box->setValue(static_cast<int>((ms + 500) / 1000));
    return box;
}

}  // namespace

SettingsDialog::SettingsDialog(uint ac_ms, uint battery_ms, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("priomix settings"));
    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(tr("How often supported wireless headsets are checked for "
                                "being switched on:"),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout();
    ac_ = MakeSecondsBox(ac_ms, this);
    battery_ = MakeSecondsBox(battery_ms, this);
    form->addRow(tr("On AC power:"), ac_);
    form->addRow(tr("On battery:"), battery_);
    layout->addLayout(form);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

uint SettingsDialog::acIntervalMs() const {
    return static_cast<uint>(ac_->value()) * 1000;
}

uint SettingsDialog::batteryIntervalMs() const {
    return static_cast<uint>(battery_->value()) * 1000;
}

}  // namespace priomix::gui
