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

#ifndef QEMULATORKEYBOARDMANAGER_H
#define QEMULATORKEYBOARDMANAGER_H

#include <QObject>
#include <QHash>
#include <QSocketNotifier>

#include <QtDeviceDiscoverySupport/private/qdevicediscovery_p.h>

#include "qemulatorkeyboardhandler.h"

class QEmulatorKeyboardManager : public QObject
{
    Q_OBJECT
public:
    QEmulatorKeyboardManager(const QString &key, const QString &specification, QObject *parent = 0);
    ~QEmulatorKeyboardManager();

    void loadKeymap(const QString &file);
    void switchLang();

    void addKeyboard(const QString &deviceNode = QString());
    void removeKeyboard(const QString &deviceNode);
signals:
    void handleKeycodeSignal(quint16 keycode, bool pressed, bool autorepeat);
private:
    QString m_spec;
    QHash<QString,QEmulatorKeyboardHandler*> m_keyboards;
    QDeviceDiscovery *m_deviceDiscovery;
    QString m_defaultKeymapFile;
private slots:
    void processKeycodeSlot(quint16 keycode, bool pressed, bool autorepeat);
};

#endif // QEMULATORKEYBOARDMANAGER_H
