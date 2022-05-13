// Copyright (c) 2020 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QRegularExpression>
#include <QDebug>

#include <linux/input.h>

#include "webosdevicediscovery_udev_sorted_p.h"

WebOSDeviceDiscoveryUDevSorted::WebOSDeviceDiscoveryUDevSorted(QDeviceTypes types, struct udev *udev, QObject *parent) :
    QDeviceDiscoveryUDev(types, udev, parent)
{
}

class USBKey
{
public:
    USBKey(QString value) : m_value(value) {}
    operator QString() const { return m_value; }

    // How to match input device (by USB topological order)
    //
    //                  [1]
    //                 /
    //               [1.1] - [1.2] - [1.3] - [1.4]
    //                /
    //             [1.1.1] - [1.1.2] - [1.1.3] - ...
    //
    // Sort criteria
    // 1. Smaller number has higher priority than bigger one
    // 2. Leaves(having longer length) have lower priority than its parent
    //
    // 1.1 > 1.2 > 1.3 > 1.4    (Rule 1)
    // 1.2.1 > 1.2.2 > 1.2.3    (Rule 1)
    // 1.1.1 > 1.2              (Rule 2)
    // 1.1.1.1 > 1.2            (Rule 2)
    friend bool operator<(const USBKey& e1, const USBKey& e2)
    {
        int i = 0;
        QStringList l1 = ((QString)e1).split(QLatin1String("."));
        QStringList l2 = ((QString)e2).split(QLatin1String("."));

        int n1, n2;
        while (i < l1.length() && i < l2.length()) {
            n1 = l1[i].toInt();
            n2 = l2[i].toInt();
            if (n1 < n2)
                return true;
            else if (n1 > n2)
                return false;
            else
                i++;
        }
        return i == l1.length();
    }

private:
    QString m_value;
};

QStringList WebOSDeviceDiscoveryUDevSorted::scanConnectedDevices()
{
    QVector<QString> nodes;
    QVector<QString> syspaths;
    QVector<QString> pendingNodes;

    udev_enumerate *ue = udev_enumerate_new(m_udev);
    udev_enumerate_add_match_subsystem(ue, "input");

    if (m_types & Device_Touchpad)
        udev_enumerate_add_match_property(ue, "ID_INPUT_TOUCHPAD", "1");
    if (m_types & Device_Touchscreen)
        udev_enumerate_add_match_property(ue, "ID_INPUT_TOUCHSCREEN", "1");
    if (m_types & Device_Keyboard) {
        udev_enumerate_add_match_property(ue, "ID_INPUT_KEYBOARD", "1");
        udev_enumerate_add_match_property(ue, "ID_INPUT_KEY", "1");
    }
    QStringList devices;

    if (udev_enumerate_scan_devices(ue) != 0) {
        qWarning("Failed to scan devices");
        return devices;
    }

    udev_list_entry *entry;
    udev_list_entry_foreach (entry, udev_enumerate_get_list_entry(ue)) {
        const char *syspath = udev_list_entry_get_name(entry);
        udev_device *udevice = udev_device_new_from_syspath(m_udev, syspath);
        QString candidate = QString::fromUtf8(udev_device_get_devnode(udevice));

        if (candidate.startsWith(QLatin1String(QT_EVDEV_DEVICE))) {
            bool match = false;
            if (m_types & Device_Touchpad) {
                const char* property = udev_device_get_property_value(udevice, "ID_INPUT_TOUCHPAD");
                if (property && strcmp(property, "1") == 0)
                    match = true;
            }
            if (m_types & Device_Touchscreen) {
                const char* property = udev_device_get_property_value(udevice, "ID_INPUT_TOUCHSCREEN");
                if (property && strcmp(property, "1") == 0)
                    match = true;
            }
            if (m_types & Device_Keyboard) {
                const char* property = udev_device_get_property_value(udevice, "ID_INPUT_KEYBOARD");
                if (property && strcmp(property, "1") == 0)
                    match = true;

                property = udev_device_get_property_value(udevice, "ID_INPUT_KEY");
                if (property && strcmp(property, "1") == 0)
                    match = true;
            }
            if (match) {
                qDebug() << "matched:" << QLatin1String(syspath) << candidate;
                nodes.push_back(candidate);
                syspaths.push_back(QLatin1String(syspath));
            }
        }
        udev_device_unref(udevice);
    }
    udev_enumerate_unref(ue);

    QMap<USBKey, int> orders;
    QRegularExpression re(QLatin1String("/1-([0-9\\.]+):1.0"));

    for (int i = 0; i < syspaths.length(); i++) {
        QRegularExpressionMatch match = re.match(syspaths[i]);
        QString key = match.captured(1);
        if (key.isEmpty()) {
            qWarning() << "Failed to get order from" << syspaths[i] << ". Append them at the end instead";
            pendingNodes.push_back(nodes[i]);
            continue;
        }
        orders.insert(USBKey(key), i);
    }

    QMap<USBKey, int>::const_iterator i = orders.constBegin();
    while (i != orders.constEnd()) {
        devices << nodes[i.value()];
        i++;
    }

    QVector<QString>::const_iterator pi = pendingNodes.constBegin();
    while (pi != pendingNodes.constEnd()) {
        devices << *pi;
        pi++;
    }

    qDebug() << "Found matching devices" << devices;

    return devices;
}

QDeviceDiscovery *WebOSDeviceDiscoveryUDevSorted::create(QDeviceTypes types, QObject *parent)
{
    QDeviceDiscovery *ret = 0;
    struct udev *udev = udev_new();

    if (udev) {
        // For touch devices only case, sort devices by port number in ascending order
        if (types == (Device_Touchpad | Device_Touchscreen))
            ret = new WebOSDeviceDiscoveryUDevSorted(types, udev, parent);
        // For keyboard devices only case, sort devices by port number in ascending order
        else if (types == Device_Keyboard)
            ret = new WebOSDeviceDiscoveryUDevSorted(types, udev, parent);
        else
            ret = new QDeviceDiscoveryUDev(types, udev, parent);
    } else {
        qWarning("Failed to get udev library context");
    }

    return ret;
}
