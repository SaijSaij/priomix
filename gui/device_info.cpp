// SPDX-License-Identifier: GPL-3.0-only
#include "device_info.hpp"

#include <QDBusMetaType>

namespace priomix::gui {

QDBusArgument& operator<<(QDBusArgument& argument, const DeviceInfo& device) {
    argument.beginStructure();
    argument << device.key << device.displayName << device.available << device.ignored;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, DeviceInfo& device) {
    argument.beginStructure();
    argument >> device.key >> device.displayName >> device.available >> device.ignored;
    argument.endStructure();
    return argument;
}

void RegisterDbusTypes() {
    qDBusRegisterMetaType<DeviceInfo>();
    qDBusRegisterMetaType<DeviceInfoList>();
}

}  // namespace priomix::gui
