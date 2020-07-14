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

#include <QScreen>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <QtGui/private/qhighdpiscaling_p.h>

#include "webosdevicediscovery_udev_sorted_p.h"
#include "weboseglfsintegration.h"

QString WebOSOutputMapping::screenNameForDeviceNode(const QString &deviceNode)
{
    QWindow *window =  m_mapping.value(deviceNode);

    if (!window || !window->screen())
        return QString();

    QString screenName = window->screen()->name();

    qDebug() << "screenNameForDeviceNode" << deviceNode << screenName;

    return screenName;
}

QWindow *WebOSOutputMapping::windowForDeviceNode(const QString &deviceNode)
{
    QWindow *window =  m_mapping.value(deviceNode);

    qDebug() << "windowForDeviceNode" << deviceNode << window;

    return window;
}

bool WebOSOutputMapping::load()
{
    // Always return true as the mapping is done
    // whenever there is an update on the device discovery
    return true;
}

void WebOSOutputMapping::addDevice(const QString &deviceNode, QWindow *window)
{
    m_mapping[deviceNode] = window;
}

void WebOSOutputMapping::removeDevice(const QString &deviceNode)
{
    m_mapping.remove(deviceNode);
}

WebOSEglFSIntegration::WebOSEglFSIntegration()
    : QEglFSIntegration()
{
    static QByteArray json = qgetenv("QT_QPA_EGLFS_CONFIG");

    if (!json.isEmpty()) {
        QFile file(QString::fromUtf8(json));
        if (file.open(QFile::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                m_configJson = doc;
                qInfo() << "Using config file" << json;
            } else {
                qWarning() << "Invalid config file" << json << "- no top-level JSON object";
            }
            file.close();
        } else {
            qWarning() << "Could not open config file" << json << "for reading";
        }
    } else {
        qWarning("No config file given");
    }
}

QString WebOSEglFSIntegration::initializeDevices(QStringList devices)
{
    for (int i = 0; i < devices.size(); i++) {
        QString screenName = m_mappingHelper.screenNameForDeviceNode(devices[i]);
        m_currentMapping[devices[i]] = screenName;
    }

    return devices.join(QLatin1Char(':'));
}

void WebOSEglFSIntegration::createInputHandlers()
{
    QOutputMapping::set(&m_mappingHelper);

    if (!m_configJson.isEmpty()) {
        for (int i = 0; i < m_configJson.array().size(); i++) {
            const QJsonObject object = m_configJson.array().at(i).toObject();
            const QJsonArray outputs = object.value(QLatin1String("outputs")).toArray();
            for (int j = 0; j < outputs.size(); j++) {
                const QJsonObject output = outputs.at(j).toObject();
                if (!m_useFixedAssociationForTouch) {
                    QString touchDevice = output.value(QLatin1String("touchDevice")).toString();
                    if (!touchDevice.isEmpty())
                        m_useFixedAssociationForTouch = true;
                }
                if (!m_useFixedAssociationForKeyboard) {
                    QString keyboardDevice = output.value(QLatin1String("keyboardDevice")).toString();
                    if (!keyboardDevice.isEmpty())
                        m_useFixedAssociationForKeyboard = true;
                }

                const QVariantMap outputSettings = outputs.at(j).toObject().toVariantMap();
                if (outputSettings.contains(QStringLiteral("name"))) {
                    const QString name = outputSettings.value(QStringLiteral("name")).toString();
                    if (m_outputSettings.contains(name))
                        qWarning() << "Output" << name << "is duplicated";
                    m_outputSettings.insert(name, outputSettings);
                }
            }
        }
    }

    qDebug() << "useFixedAssociationForTouch:" << m_useFixedAssociationForTouch
        << "useFixedAssociationForKeyboard:" << m_useFixedAssociationForKeyboard;

    m_touchDiscovery = WebOSDeviceDiscoveryUDevSorted::create(QDeviceDiscovery::Device_Touchpad | QDeviceDiscovery::Device_Touchscreen, this);

    QStringList scannedTouchDevices = m_touchDiscovery->scanConnectedDevices();
    if (m_useFixedAssociationForTouch)
        prepareFixedOutputMapping(scannedTouchDevices, QLatin1String("touchDevice"));
    else
        prepareOutputMapping(scannedTouchDevices);

    QString touchDevs = initializeDevices(scannedTouchDevices);

    // HACK: to disable device discovery in QEvdevTouchManager when no device is connected.
    if (touchDevs.isEmpty()) {
        touchDevs = "/dev/null";
        m_dummyTouchDevice = true;
    }

    QString env = QString::fromLocal8Bit(qgetenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS"));
    qDebug() << "createInputHandlers, touchDevs" << touchDevs << env;
    if (!env.isEmpty()) {
        env.append(":" + touchDevs);
        qWarning() << "Updating QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS to" << env;
        qputenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS", env.toUtf8());
    }

    m_touchMgr = new QEvdevTouchManager(QLatin1String("EvdevTouch"), touchDevs, this);

    if (m_touchDiscovery && m_touchMgr) {
        connect(m_touchDiscovery, &QDeviceDiscovery::deviceDetected,
                this, &WebOSEglFSIntegration::arrangeTouchDevices);
        connect(m_touchDiscovery, &QDeviceDiscovery::deviceRemoved,
                this, &WebOSEglFSIntegration::removeTouchDevice);
    }

    m_kbdDiscovery = WebOSDeviceDiscoveryUDevSorted::create(QDeviceDiscovery::Device_Keyboard, this);

    QStringList scannedKbdDevices = m_kbdDiscovery->scanConnectedDevices();
    if (m_useFixedAssociationForKeyboard)
        prepareFixedOutputMapping(scannedKbdDevices, QLatin1String("keyboardDevice"));
    else
        prepareOutputMapping(scannedKbdDevices);

    QString kbdDevs = initializeDevices(scannedKbdDevices);

    // HACK: to disable device discovery in QEvdevKeyboardManager when no device is connected.
    if (kbdDevs.isEmpty()) {
        kbdDevs = "/dev/null";
        m_dummyKbdDevice = true;
    }

    env = QString::fromLocal8Bit(qgetenv("QT_QPA_EVDEV_KEYBOARD_PARAMETERS"));
    qDebug() << "createInputHandlers: kbdDevs" << kbdDevs << env;
    if (!env.isEmpty()) {
        env.append(":" + kbdDevs);
        qWarning() << "Updating QT_QPA_EVDEV_KEYBOARD_PARAMETERS to" << env;
        qputenv("QT_QPA_EVDEV_KEYBOARD_PARAMETERS", env.toUtf8());
    }

    m_kbdMgr = new QEvdevKeyboardManager(QLatin1String("EvdevKeyboard"), kbdDevs, this);

    if (m_kbdDiscovery && m_kbdMgr) {
        connect(m_kbdDiscovery, &QDeviceDiscovery::deviceDetected,
                this, &WebOSEglFSIntegration::arrangeKbdDevices);
        connect(m_kbdDiscovery, &QDeviceDiscovery::deviceRemoved,
                this, &WebOSEglFSIntegration::removeKbdDevice);
    }

    m_mouseMgr = new QEvdevMouseManager(QLatin1String("EvdevMouse"), QString(), this);

    connect(this, &WebOSEglFSIntegration::platformWindowCreated, this, &WebOSEglFSIntegration::handleWindowCreated);

    m_initTimer.setSingleShot(true);
    connect(&m_initTimer, &QTimer::timeout, this, &WebOSEglFSIntegration::updateWindowMapping);
}

QPlatformWindow *WebOSEglFSIntegration::createPlatformWindow(QWindow *window) const
{
    if (window->screen())
        emit platformWindowCreated(window);
    else
        connect(window, &QWindow::screenChanged, this, &WebOSEglFSIntegration::handleScreenChange);

    return QEglFSIntegration::createPlatformWindow(window);
}

void WebOSEglFSIntegration::handleScreenChange(QScreen *screen)
{
    QWindow *window = qobject_cast<QWindow *>(sender());
    if (window->screen())
        emit platformWindowCreated(window);
}

void WebOSEglFSIntegration::handleWindowCreated(QWindow *window)
{
    qInfo() << "Adding window" << window << "to" << window->screen()->name();
    m_windows.push_back(window);
    // To update window mapping as batch
    m_initTimer.start(200);
}

void WebOSEglFSIntegration::updateWindowMapping()
{
    qDebug() << "updateWindowMapping";
    arrangeTouchDevices();
    arrangeKbdDevices();
}

void WebOSEglFSIntegration::prepareOutputMapping(const QStringList &devices)
{
    for (int i = 0; i < devices.size() && i < m_windows.size(); i++) {
        QWindow *window = m_windows[i];
        QScreen *screen = window->screen();

        if (!screen)
            continue;

        qDebug() << "prepareOutputMapping" << devices[i] << screen->name();

        m_mappingHelper.addDevice(devices[i], window);
    }
}

void WebOSEglFSIntegration::prepareFixedOutputMapping(const QStringList &devices, QString deviceType)
{
    foreach (QWindow *window, m_windows) {
        QScreen *screen = window->screen();
        if (!screen)
            continue;

        if (!m_outputSettings.contains(screen->name()))
            continue;

        QString devName = m_outputSettings.value(screen->name()).value(deviceType).toString();

        foreach (QString device, devices) {
            if (devName != device)
                continue;

            qDebug() << "prepareFixedOutputMapping" << device << screen->name();
            m_mappingHelper.addDevice(device, window);
        }
    }
}

void WebOSEglFSIntegration::arrangeTouchDevices()
{
    if (!m_touchMgr)
        return;

    const QStringList devices = m_touchDiscovery->scanConnectedDevices();

    if (m_dummyTouchDevice && !devices.empty()) {
        m_dummyTouchDevice = false;
        m_touchMgr->removeDevice("/dev/null");
    }

    if (m_useFixedAssociationForTouch)
        prepareFixedOutputMapping(devices, QLatin1String("touchDevice"));
    else
        prepareOutputMapping(devices);

    for (int i = 0; i < devices.size(); i++) {
        QString screenName = m_mappingHelper.screenNameForDeviceNode(devices[i]);

        if (!m_currentMapping.contains(devices[i])) {
            m_currentMapping[devices[i]] = screenName;
            m_touchMgr->addDevice(devices[i]);
            continue;
        }

        if (m_currentMapping.value(devices[i]) == screenName)
            continue;

        // Associated screen changed
        qDebug() << "add and remove touch device" << devices[i];
        m_currentMapping[devices[i]] = screenName;
        m_touchMgr->removeDevice(devices[i]);
        m_touchMgr->addDevice(devices[i]);
    }
}

void WebOSEglFSIntegration::removeTouchDevice(const QString &deviceNode)
{
    if (!m_touchMgr)
        return;

    m_currentMapping.remove(deviceNode);
    m_mappingHelper.removeDevice(deviceNode);
    m_touchMgr->removeDevice(deviceNode);

    arrangeTouchDevices();
}

void WebOSEglFSIntegration::arrangeKbdDevices()
{
    if (!m_kbdMgr)
        return;

    const QStringList devices = m_kbdDiscovery->scanConnectedDevices();

    if (m_dummyKbdDevice && !devices.empty()) {
        m_dummyKbdDevice = false;
        m_kbdMgr->removeKeyboard("/dev/null");
    }

    if (m_useFixedAssociationForKeyboard)
        prepareFixedOutputMapping(devices, QLatin1String("keyboardDevice"));
    else
        prepareOutputMapping(devices);

    for (int i = 0; i < devices.size(); i++) {
        QString screenName = m_mappingHelper.screenNameForDeviceNode(devices[i]);

        if (!m_currentMapping.contains(devices[i])) {
            m_currentMapping[devices[i]] = screenName;
            m_kbdMgr->addKeyboard(devices[i]);
            continue;
        }

        if (m_currentMapping.value(devices[i]) == screenName)
            continue;

        // Associated screen changed
        qDebug() << "add and remove keyboard" << devices[i];
        m_currentMapping[devices[i]] = screenName;
        m_kbdMgr->removeKeyboard(devices[i]);
        m_kbdMgr->addKeyboard(devices[i]);
    }
}

void WebOSEglFSIntegration::removeKbdDevice(const QString &deviceNode)
{
    if (!m_kbdMgr)
        return;

    m_currentMapping.remove(deviceNode);
    m_mappingHelper.removeDevice(deviceNode);
    m_kbdMgr->removeKeyboard(deviceNode);

    arrangeKbdDevices();
}
