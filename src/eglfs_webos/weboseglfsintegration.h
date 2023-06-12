// Copyright (c) 2020-2022 LG Electronics, Inc.
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

#ifndef WEBOS_EGLFS_INTEGRATION_H
#define WEBOS_EGLFS_INTEGRATION_H

#include <QJsonDocument>

#include <qpa/qplatformfontdatabase.h>
#include <qpa/qplatformservices.h>
#include <QtFbSupport/private/qfbvthandler_p.h>
#include <private/qeglfsintegration_p.h>
#include <QTimer>

#if QT_CONFIG(evdev)
#include <QtInputSupport/private/qevdevmousemanager_p.h>
#include <QtInputSupport/private/qevdevkeyboardmanager_p.h>
#include <QtInputSupport/private/qevdevtouchmanager_p.h>
#include <QtInputSupport/private/qoutputmapping_p.h>
#endif


#if defined(EMULATOR)
#include "qlinuxmouse.h"
#include "qemulatorkeyboardmanager.h"
#include "qemulatormousemanager.h"
#endif

#if QT_CONFIG(evdev)
class WebOSOutputMapping : public QOutputMapping
{
public:
    QString screenNameForDeviceNode(const QString &deviceNode) override;
    QWindow *windowForDeviceNode(const QString &deviceNode) override;
    bool load() override;

    void addDevice(const QString &deviceNode, QWindow *window);
    void removeDevice(const QString &deviceNode);

private:
    QHash<QString, QWindow *> m_mapping;
};
#endif

class WebOSEglFSIntegration : public QEglFSIntegration
{
    Q_OBJECT
public:
    WebOSEglFSIntegration();
#if QT_CONFIG(evdev)
    void createInputHandlers() override;

    QString initializeDevices(QStringList devices);
    void prepareOutputMapping(const QStringList &devices);
    void prepareFixedOutputMapping(const QStringList &devices, QString deviceType);
    void arrangeTouchDevices();
    void removeTouchDevice(const QString &deviceNode);
    void arrangeKbdDevices();
    void removeKbdDevice(const QString &deviceNode);
#if not defined(EMULATOR)
    QPlatformWindow *createPlatformWindow(QWindow *window) const override;

public slots:
    void updateWindowMapping();
    void handleWindowCreated(QWindow *window);
    void handleScreenChange(QScreen *screen);

signals:
    void platformWindowCreated(QWindow *) const;
#endif
#endif

private:
    QVector<QWindow *> m_windows;
    QTimer m_initTimer;

#if QT_CONFIG(evdev)
    QEvdevTouchManager *m_touchMgr = nullptr;
    QDeviceDiscovery *m_touchDiscovery = nullptr;
    QDeviceDiscovery *m_kbdDiscovery = nullptr;
    QEvdevMouseManager *m_mouseMgr = nullptr;
    QHash<QString, QString> m_currentMapping;

    WebOSOutputMapping m_mappingHelper;
#endif

    QJsonDocument m_configJson;
    bool m_disableKbdOutputMapping = false;
    bool m_useFixedAssociationForTouch = false;
    bool m_useFixedAssociationForKeyboard = false;
    QMap<QString, QVariantMap> m_outputSettings;
#if defined(EMULATOR)
    QEmulatorKeyboardManager* m_emulatorKeyboardManager;
    QEmulatorMouseManager* m_emulatorMouseManager;
#endif
};

#endif
