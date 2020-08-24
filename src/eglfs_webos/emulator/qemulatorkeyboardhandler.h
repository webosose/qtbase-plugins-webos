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

#ifndef QEMULATORKEYBOARDHANDLER_H
#define QEMULATORKEYBOARDHANDLER_H

#include <QObject>
#include <QTimer>
#include <QDataStream>

class QSocketNotifier;

namespace QEmulatorKeyboardMap {
    const quint32 FileMagic = 0x514d4150; // 'QMAP'

    struct Mapping {
        quint16 keycode;
        quint16 unicode;
        quint32 qtcode;
        quint8 modifiers;
        quint8 flags;
        quint16 special;

    };

    enum Flags {
        IsDead     = 0x01,
        IsLetter   = 0x02,
        IsModifier = 0x04,
        IsSystem   = 0x08
    };

    enum System {
        SystemConsoleFirst    = 0x0100,
        SystemConsoleMask     = 0x007f,
        SystemConsoleLast     = 0x017f,
        SystemConsolePrevious = 0x0180,
        SystemConsoleNext     = 0x0181,
        SystemReboot          = 0x0200,
        SystemZap             = 0x0300
    };

    struct Composing {
        quint16 first;
        quint16 second;
        quint16 result;
    };

    enum Modifiers {
        ModPlain   = 0x00,
        ModShift   = 0x01,
        ModAltGr   = 0x02,
        ModControl = 0x04,
        ModAlt     = 0x08,
        ModShiftL  = 0x10,
        ModShiftR  = 0x20,
        ModCtrlL   = 0x40,
        ModCtrlR   = 0x80
        // ModCapsShift = 0x100, // not supported!
    };
}

inline QDataStream &operator>>(QDataStream &ds, QEmulatorKeyboardMap::Mapping &m)
{
    return ds >> m.keycode >> m.unicode >> m.qtcode >> m.modifiers >> m.flags >> m.special;
}

inline QDataStream &operator<<(QDataStream &ds, const QEmulatorKeyboardMap::Mapping &m)
{
    return ds << m.keycode << m.unicode << m.qtcode << m.modifiers << m.flags << m.special;
}

inline QDataStream &operator>>(QDataStream &ds, QEmulatorKeyboardMap::Composing &c)
{
    return ds >> c.first >> c.second >> c.result;
}

inline QDataStream &operator<<(QDataStream &ds, const QEmulatorKeyboardMap::Composing &c)
{
    return ds << c.first << c.second << c.result;
}

class QEmulatorFdContainer
{
    int m_fd;
    Q_DISABLE_COPY(QEmulatorFdContainer);
public:
    explicit QEmulatorFdContainer(int fd = -1) Q_DECL_NOTHROW : m_fd(fd) {}
    ~QEmulatorFdContainer() { reset(); }

    int get() const Q_DECL_NOTHROW { return m_fd; }

    int release() Q_DECL_NOTHROW { int result = m_fd; m_fd = -1; return result; }
    void reset() Q_DECL_NOTHROW;
};

class QEmulatorKeyboardHandler : public QObject
{
    Q_OBJECT
public:
    QEmulatorKeyboardHandler(const QString &device, QEmulatorFdContainer &fd, bool disableZap, bool enableCompose, const QString &keymapFile, QObject *parent = nullptr);
    ~QEmulatorKeyboardHandler();

    enum KeycodeAction {
        None               = 0,

        CapsLockOff        = 0x01000000,
        CapsLockOn         = 0x01000001,
        NumLockOff         = 0x02000000,
        NumLockOn          = 0x02000001,
        ScrollLockOff      = 0x03000000,
        ScrollLockOn       = 0x03000001,

        Reboot             = 0x04000000,

        PreviousConsole    = 0x05000000,
        NextConsole        = 0x05000001,
        SwitchConsoleFirst = 0x06000000,
        SwitchConsoleLast  = 0x0600007f,
        SwitchConsoleMask  = 0x0000007f
    };

    static QEmulatorKeyboardHandler *create(const QString &device,
                                         const QString &specification,
                                         const QString &defaultKeymapFile = QString());

    static Qt::KeyboardModifiers toQtModifiers(quint8 mod)
    {
        Qt::KeyboardModifiers qtmod = Qt::NoModifier;

        if (mod & (QEmulatorKeyboardMap::ModShift | QEmulatorKeyboardMap::ModShiftL | QEmulatorKeyboardMap::ModShiftR))
            qtmod |= Qt::ShiftModifier;
        if (mod & (QEmulatorKeyboardMap::ModControl | QEmulatorKeyboardMap::ModCtrlL | QEmulatorKeyboardMap::ModCtrlR))
            qtmod |= Qt::ControlModifier;
        if (mod & QEmulatorKeyboardMap::ModAlt)
            qtmod |= Qt::AltModifier;

        return qtmod;
    }

    bool loadKeymap(const QString &file);
    void unloadKeymap();

    void readKeycode();
    KeycodeAction processKeycode(quint16 keycode, bool pressed, bool autorepeat);

    void switchLang();

signals:
    void processKeycodeSignal(quint16 keycode, bool pressed, bool autorepeat);
private:
    void processKeyEvent(int nativecode, int unicode, int qtcode,
                         Qt::KeyboardModifiers modifiers, bool isPress, bool autoRepeat);
    void switchLed(int, bool);

    QString m_device;
    QEmulatorFdContainer m_fd;
    QSocketNotifier *m_notify;

    // keymap handling
    quint8 m_modifiers;
    quint8 m_locks[3];
    int m_composing;
    quint16 m_dead_unicode;
    quint8 m_langLock;

    bool m_no_zap;
    bool m_do_compose;

    const QEmulatorKeyboardMap::Mapping *m_keymap;
    int m_keymap_size;
    const QEmulatorKeyboardMap::Composing *m_keycompose;
    int m_keycompose_size;

    static const QEmulatorKeyboardMap::Mapping s_keymap_default[];
    static const QEmulatorKeyboardMap::Composing s_keycompose_default[];
};

#endif // QEMULATORKEYBOARDHANDLER_H
