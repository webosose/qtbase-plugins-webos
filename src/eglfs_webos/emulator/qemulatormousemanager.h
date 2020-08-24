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

#ifndef QEMULATORMOUSEMANAGER_H
#define QEMULATORMOUSEMANAGER_H

#include <QObject>
#include <QHash>
#include <QSocketNotifier>
#include <QPoint>
#include <qpa/qwindowsysteminterface.h>
#include <QTouchDevice>

#include "qemulatormousehandler.h"

class QDeviceDiscovery;

class QEmulatorMouseManager : public QObject
{
public:
    QEmulatorMouseManager(const QString &key, const QString &specification, QObject *parent = 0);
    ~QEmulatorMouseManager();

    void registerTouchDevice();
    void unregisterTouchDevice();
    QWindowSystemInterface::TouchPoint translateTouchPoint(QPoint pos, Qt::MouseButton button, QEvent::Type type, int index);
    void handleMouseEvent(int x, int y, bool abs, Qt::MouseButtons buttons,
                          Qt::MouseButton button, QEvent::Type type);
    void handleWheelEvent(QPoint delta);

    void addMouse(const QString &deviceNode = QString());
    void removeMouse(const QString &deviceNode);

public slots:
    void handleKeycodeSlot(quint16 keycode, bool pressed, bool autorepeat);
private:
    void clampPosition();

    QString m_spec;
    QHash<QString,QEmulatorMouseHandler*> m_mice;
    QDeviceDiscovery *m_deviceDiscovery;
    int m_x;
    int m_y;
    int m_xoffset;
    int m_yoffset;

    QTouchDevice *m_touchDevice;
    int m_touchState;
    bool m_isTouch;
    bool m_isMultiTouch;

    void enableVboxHostMousePointer();
};

#endif // QEMULATORMOUSEMANAGER_H
