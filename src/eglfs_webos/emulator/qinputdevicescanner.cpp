// Copyright (c) 2015-2020 LG Electronics, Inc.
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

#include <QFile>
#include <QDebug>

#include <linux/input.h>

#include "qinputdevicescanner.h"

#define MAX_INPUT_DEVICES   20

#define BITS_PER_LONG           (sizeof(long) * 8)
#define NBITS(x)                ((((x)-1)/BITS_PER_LONG)+1)
#define OFFSET(x)               ((x)%BITS_PER_LONG)
#define LONG(x)                 ((x)/BITS_PER_LONG)
#define IS_BIT_SET(bit, array)  ((array[LONG(bit)] >> OFFSET(bit)) & 1)

QInputDeviceScanner::QInputDeviceScanner()
{
    setObjectName(QLatin1String("Input Device Scanner"));
}

QInputDeviceScanner::~QInputDeviceScanner()
{
    m_listOfKeyboard.clear();
    m_listOfMouse.clear();
    m_listOfMotion.clear();
}

void QInputDeviceScanner::scan()
{
    for (int index = 0; index < MAX_INPUT_DEVICES; index++) {
        QString devInput = QString("/dev/input");
        QString devEvent = devInput + "/event" + QString::number(index);
        QFile fileEvent;
        fileEvent.setFileName(devEvent);

        if (fileEvent.open(QIODevice::ReadOnly)) {
            char name[256] = "Unknown";
            char phys[256] = "";

            if (::ioctl(fileEvent.handle(), EVIOCGNAME(sizeof(name)), name) < 0)
                qWarning("Cannot get the name of device");

            if (::ioctl(fileEvent.handle(), EVIOCGPHYS(sizeof(phys)), phys) < 0)
                qWarning("Cannot get the physical location");

            QString deviceName = QString(name);
            QString devicePhys = QString(phys);

            if (deviceName.contains("LGE RCU")) {
                qDebug() << QString("Found RCU: ") << devEvent;
                m_listOfRcu.append(devEvent);
            } else if (deviceName.contains("M-RCU - Builtin")) {
                qDebug() << QString("Found Motion: ") << devEvent;
                m_listOfMotion.append(devEvent);
            } else if (devicePhys.startsWith("usb-dev")
                       || devicePhys.startsWith("usb-ehci")
                       || devicePhys.startsWith("usb-ohci")
                       || deviceName.contains("keyboard")
                       || deviceName.contains("Mouse", Qt::CaseInsensitive)
                       || deviceName.contains("Tablet")) {
                unsigned long evbit[NBITS(EV_MAX + 1)];

                if (::ioctl(fileEvent.handle(), EVIOCGBIT(0, sizeof(evbit)), evbit) >= 0) {
                    if (IS_BIT_SET(EV_REL, evbit)
                        || IS_BIT_SET(EV_ABS, evbit)) {
                        qDebug() << QString("Found Mouse: ") << devEvent;
                        m_listOfMouse.append(devEvent);
                    } else if (IS_BIT_SET(EV_KEY, evbit)
                               && !IS_BIT_SET(EV_REL, evbit)
                               && !IS_BIT_SET(EV_ABS, evbit)) {
                        qDebug() << QString("Found Keyboard: ") << devEvent;
                        m_listOfKeyboard.append(devEvent);
                    }
                }
            }
            fileEvent.close();
        }
    }
}

int QInputDeviceScanner::getNumOfMouses()
{
    return m_listOfMouse.count();
}

int QInputDeviceScanner::getNumOfKeyboards()
{
    return m_listOfKeyboard.count();
}

int QInputDeviceScanner::getNumOfMotions()
{
    return m_listOfMotion.count();
}

int QInputDeviceScanner::getNumOfRcu()
{
    return m_listOfRcu.count();
}

const QString & QInputDeviceScanner::getMouseName(int idx)
{
    return m_listOfMouse.at(idx);
}

const QString & QInputDeviceScanner::getKeyboardName(int idx)
{
    return m_listOfKeyboard.at(idx);
}

const QString & QInputDeviceScanner::getMotionName(int idx)
{
    return m_listOfMotion.at(idx);
}

const QString & QInputDeviceScanner::getRcuName(int idx)
{
    return m_listOfRcu.at(idx);
}
