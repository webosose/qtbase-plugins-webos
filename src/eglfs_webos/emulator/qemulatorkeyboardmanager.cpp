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

#include <QStringList>
#include <QDebug>

#include <private/qguiapplication_p.h>
#include <private/qinputdevicemanager_p_p.h>

#include "qemulatorkeyboardmanager.h"

QEmulatorKeyboardManager::QEmulatorKeyboardManager(const QString &key, const QString &specification, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(key);


    QString spec = QString::fromLocal8Bit(qgetenv("QT_QPA_EVDEV_KEYBOARD_PARAMETERS"));

    if (spec.isEmpty())
        spec = specification;

    QStringList args = spec.split(QLatin1Char(':'));
    QStringList devices;

    foreach (const QString &arg, args) {
        if (arg.startsWith(QLatin1String("/dev/"))) {
            // if device is specified try to use it
            devices.append(arg);
            args.removeAll(arg);
        }
    }

    // build new specification without /dev/ elements
    m_spec = args.join(QLatin1Char(':'));

    // add all keyboards for devices specified in the argument list
    foreach (const QString &device, devices)
        addKeyboard(device);

    if (devices.isEmpty()) {
        qWarning() << "emulatorkeyboard: Using device discovery";
        m_deviceDiscovery = QDeviceDiscovery::create(QDeviceDiscovery::Device_Keyboard, this);
        if (m_deviceDiscovery) {
            // scan and add already connected keyboards
            const QStringList devices = m_deviceDiscovery->scanConnectedDevices();
            for (const QString &device : devices)
                addKeyboard(device);

            connect(m_deviceDiscovery, &QDeviceDiscovery::deviceDetected,
                    this, &QEmulatorKeyboardManager::addKeyboard);
            connect(m_deviceDiscovery, &QDeviceDiscovery::deviceRemoved,
                    this, &QEmulatorKeyboardManager::removeKeyboard);
        }
    }
}

QEmulatorKeyboardManager::~QEmulatorKeyboardManager()
{
    qDeleteAll(m_keyboards);
    m_keyboards.clear();
}

void QEmulatorKeyboardManager::addKeyboard(const QString &deviceNode)
{
    qWarning() << "Adding keyboard at" << deviceNode;
    QEmulatorKeyboardHandler *keyboard;
    keyboard = QEmulatorKeyboardHandler::create(deviceNode, m_spec, m_defaultKeymapFile);
    if (keyboard) {
        connect(keyboard, &QEmulatorKeyboardHandler::processKeycodeSignal, this, &QEmulatorKeyboardManager::processKeycodeSlot);
        m_keyboards.insert(deviceNode, keyboard);
        QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())->setDeviceCount(
            QInputDeviceManager::DeviceTypeKeyboard, m_keyboards.count());
    } else {
        qWarning("Failed to open keyboard device %s", qPrintable(deviceNode));
    }
}

void QEmulatorKeyboardManager::removeKeyboard(const QString &deviceNode)
{
    if (m_keyboards.contains(deviceNode)) {
        qWarning() << "Removing keyboard at" << deviceNode;
        QEmulatorKeyboardHandler *keyboard = m_keyboards.value(deviceNode);
        m_keyboards.remove(deviceNode);
        QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())->setDeviceCount(
            QInputDeviceManager::DeviceTypeKeyboard, m_keyboards.count());
        delete keyboard;
    }
}

void QEmulatorKeyboardManager::loadKeymap(const QString &file)
{
    m_defaultKeymapFile = file;

    if (file.isEmpty()) {
        // Restore the default, which is either the built-in keymap or
        // the one given in the plugin spec.
        QString keymapFromSpec;
        const auto specs = m_spec.splitRef(QLatin1Char(':'));
        for (const QStringRef &arg : specs) {
            if (arg.startsWith(QLatin1String("keymap=")))
                keymapFromSpec = arg.mid(7).toString();
        }
        foreach (QEmulatorKeyboardHandler *handler, m_keyboards) {
            if (keymapFromSpec.isEmpty())
                handler->unloadKeymap();
            else
                handler->loadKeymap(keymapFromSpec);
        }
    } else {
        foreach (QEmulatorKeyboardHandler *handler, m_keyboards)
            handler->loadKeymap(file);
    }
}

void QEmulatorKeyboardManager::switchLang()
{
    foreach (QEmulatorKeyboardHandler *handler, m_keyboards)
        handler->switchLang();
}

void QEmulatorKeyboardManager::processKeycodeSlot(quint16 keycode, bool pressed, bool autorepeat) {
    emit handleKeycodeSignal(keycode, pressed, autorepeat);
}
