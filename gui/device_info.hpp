// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// D-Bus struct (ssbb) of ListSinks()/ListSources(), shared with the proxy class that
// qdbusxml2cpp generates from common/io.github.saijsaij.Priomix.xml.

#include <QDBusArgument>
#include <QList>
#include <QMetaType>
#include <QString>

namespace priomix::gui {

struct DeviceInfo {
    QString key;
    QString displayName;
    bool available = false;
    bool ignored = false;
};

QDBusArgument& operator<<(QDBusArgument& argument, const DeviceInfo& device);
const QDBusArgument& operator>>(const QDBusArgument& argument, DeviceInfo& device);

void RegisterDbusTypes();

}  // namespace priomix::gui

// Name used by the org.qtproject.QtDBus.QtTypeName annotations in the XML.
using DeviceInfoList = QList<priomix::gui::DeviceInfo>;

Q_DECLARE_METATYPE(priomix::gui::DeviceInfo)
Q_DECLARE_METATYPE(DeviceInfoList)
