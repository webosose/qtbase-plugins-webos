// Copyright (c) 2015-2021 LG Electronics, Inc.
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

#include <QKeyEvent>
#include <QSocketNotifier>
#include <QStringList>
#include <QGuiApplication>
#include <QDebug>
#include <QFile>

#include <qpa/qwindowsysteminterface.h>
#include <qplatformdefs.h>
#include <private/qcore_unix_p.h>

#include <errno.h>
#include <linux/input.h>

#include "InputControl.h"
#include "NyxInputControl.h"

#include "qlinuxmouse.h"

extern "C" {
    InputControl* m_tpInput = NULL;
    InputControl* getTouchpanel() { return m_tpInput; }
}

QLinuxMouseHandler::QLinuxMouseHandler(const QString &specifi)
    : m_buttons(0), m_yoffset(0), m_xoffset(0), m_prevy(0), m_prevx(0), m_y(0), m_x(0), m_notify(0)
{
    qDebug() << "QLinuxMouseHandler" << specifi;

    setObjectName(QLatin1String("LinuxInputSubsystem Mouse Handler for Emulator"));

    QString device = QLatin1String("/dev/input/event0");
    m_compression = true;
    m_smooth = false;
    int jitterLimit = 0;
    struct input_absinfo abs;
    QFile fileEvent;
    int maxX, maxY;

    QStringList qslargs = specifi.split(QLatin1Char(':'));
    QString temparg;
    for (int i=0; i<qslargs.size(); i++) {
        temparg = qslargs.at(i);
        if (temparg.startsWith(QLatin1String("/dev/")))
            device = temparg;
        else if (temparg.startsWith("yoffset="))
            m_yoffset = temparg.mid(8).toInt();
        else if (temparg.startsWith("xoffset="))
            m_xoffset = temparg.mid(8).toInt();
        else if (temparg.startsWith("dejitter="))
            jitterLimit = temparg.mid(9).toInt();
        else if (temparg == "nocompress")
            m_compression = false;
    }
    m_jitterLimitSquared = jitterLimit*jitterLimit;

    m_fd = QT_OPEN(device.toLocal8Bit().constData(), O_RDONLY | O_NDELAY, 0);
    if (m_fd >= 0) {
        QSize screenSize = QGuiApplication::primaryScreen()->geometry().size();
        m_scalex = m_scaley = 1.0f;
        maxY = screenSize.height();
        maxX = screenSize.width();
//#ifdef HAS_NYX
        InputControl* inputcontrol = new NyxInputControl(NYX_DEVICE_TOUCHPANEL, "Main");
        if(inputcontrol)
        {
            fileEvent.setFileName(device);
            if(fileEvent.open(QIODevice::ReadOnly)) {
                if(::ioctl(fileEvent.handle(), EVIOCGABS(0), &abs) >= 0)
                    maxX = abs.maximum;
                if(::ioctl(fileEvent.handle(), EVIOCGABS(1), &abs) >= 0)
                    maxY = abs.maximum;

                m_scalex = (float)screenSize.width() / (float)maxX;
                m_scaley = (float)screenSize.height() /(float) maxY;

                fileEvent.close();
            }
        }
//#endif
        m_notify = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_notify, SIGNAL(activated(int)), this, SLOT(readMouseData()));
    } else {
        qWarning("Cannot open mouse input device '%s': %s", qPrintable(device), strerror(errno));
        return;
    }
}

QLinuxMouseHandler::~QLinuxMouseHandler()
{
    if (m_fd >= 0)
        QT_CLOSE(m_fd);
}

void QLinuxMouseHandler::sendMouseEvent(int x, int y, Qt::MouseButtons buttons, int MTag)
{
    QPoint pos(x+m_xoffset, y+m_yoffset);

    //convert to touch event
    QList<QWindowSystemInterface::TouchPoint> pointList;

    QWindowSystemInterface::TouchPoint touchPoint;
    QPoint pt(pos);
    QRect rc = QGuiApplication::primaryScreen()->virtualGeometry();

    touchPoint.id = 0;
    touchPoint.normalPosition = QPointF((qreal)pt.x() / (qreal)rc.width(), (qreal)pt.y() / (qreal)rc.height());
    touchPoint.area = QRectF(pt, QSize(1,1));
    touchPoint.pressure = 1;

    // determine event type and update state of current touch point
    QEvent::Type type = QEvent::None;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    switch (buttons) {
    case Qt::LeftButton:
        if (!MTag) {
            touchPoint.state = QEventPoint::State(Qt::TouchPointMoved);
            type = QEvent::TouchUpdate;
        } else {
            touchPoint.state = QEventPoint::State(Qt::TouchPointPressed);
            type = QEvent::TouchBegin;
        }
        break;

    case Qt::NoButton:
        touchPoint.state = QEventPoint::State(Qt::TouchPointReleased);
        type = QEvent::TouchEnd;
        break;

    default:
        touchPoint.state = QEventPoint::State(Qt::TouchPointMoved);
        type = QEvent::TouchUpdate;
        break;
    }

    pointList.append(touchPoint);

    Qt::MouseButton button{Qt::MouseButton::LeftButton};
    QWindowSystemInterface::handleMouseEvent(0, pos, pos, buttons, button, type);
#else
    switch (buttons) {
    case Qt::LeftButton:
        if (!MTag) {
            touchPoint.state = Qt::TouchPointMoved;
            type = QEvent::TouchUpdate;
        } else {
            touchPoint.state = Qt::TouchPointPressed;
            type = QEvent::TouchBegin;
        }
        break;

    case Qt::NoButton:
        touchPoint.state = Qt::TouchPointReleased;
        type = QEvent::TouchEnd;
        break;

    default:
        touchPoint.state = Qt::TouchPointMoved;
        type = QEvent::TouchUpdate;
        break;
    }

    pointList.append(touchPoint);

    QWindowSystemInterface::handleMouseEvent(0, pos, pos, buttons);
#endif
    m_prevx = x;
    m_prevy = y;
}

void QLinuxMouseHandler::readMouseData()
{
    int iEventCompressCount = 0;
    bool bPendingMouseEvent = false;
    bool bPosChanged = false;
    int num = 0;
    struct ::input_event ie_buffer[32];

    forever {
        num = QT_READ(m_fd, reinterpret_cast<char *>(ie_buffer) + num, sizeof(ie_buffer) - num);

        if (num % sizeof(ie_buffer[0]) == 0) {
            break;
        } else if (num < 0 && (errno != EINTR && errno != EAGAIN)) {
            qWarning("Could not read from mouse input device: %s", strerror(errno));
            m_notify->setEnabled(false);
            return;
        } else if (num == 0) {
            qWarning("Got EOF from the mouse input device.");
            m_notify->setEnabled(false);
            return;
        }
    }

    num /= sizeof(ie_buffer[0]);
    QSize screenSize = QGuiApplication::primaryScreen()->geometry().size();

    for (int i = 0; i < num; ++i) {
        struct ::input_event *ie_data = &ie_buffer[i];
        bool unknown = false;
        if (ie_data->type == EV_ABS) {
            if (ie_data->code == ABS_X && m_x != ie_data->value) {
                m_x = (int)(ie_data->value * m_scalex);
                bPosChanged = true;
            } else if (ie_data->code == ABS_Y && m_y != ie_data->value) {
                m_y = (int)(ie_data->value * m_scaley);
                bPosChanged = true;
            } else if (ie_data->code == ABS_PRESSURE) {
                //ignore for now...
            } else if (ie_data->code == ABS_TOOL_WIDTH) {
                //ignore for now...
            } else if (ie_data->code == ABS_HAT0X) {
                //ignore for now...
            } else if (ie_data->code == ABS_HAT0Y) {
                //ignore for now...
            } else {
                unknown = true;
            }
        } else if (ie_data->type == EV_REL) {
            if (ie_data->code == REL_X) {
                m_x += ie_data->value;
                bPosChanged = true;
            } else if (ie_data->code == REL_Y) {
                m_y += ie_data->value;
                bPosChanged = true;
            }
            else if (ie_data->code == REL_WHEEL) {
                int delta = 120 * ie_data->value;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                QPoint pixelDelta{m_x, m_y + delta};
                QPoint angleData;
                QWindowSystemInterface::handleWheelEvent(0,
                                                         QPoint(m_x, m_y),
                                                         QPoint(m_x, m_y),
                                                         pixelDelta, angleData);
#else
                QWindowSystemInterface::handleWheelEvent(0,
                                                         QPoint(m_x, m_y),
                                                         QPoint(m_x, m_y),
                                                         delta, Qt::Vertical);
#endif
            }
        } else if (ie_data->type == EV_KEY && ie_data->code == BTN_TOUCH) {
            m_buttons = ie_data->value ? Qt::LeftButton : Qt::NoButton;

            sendMouseEvent(m_x, m_y, m_buttons, 1);
            bPendingMouseEvent = false;
        } else if (ie_data->type == EV_KEY && ie_data->code >= BTN_LEFT && ie_data->code <= BTN_MIDDLE) {
            Qt::MouseButton button = Qt::NoButton;
            switch (ie_data->code) {
            case BTN_LEFT: button = Qt::LeftButton; break;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            case BTN_MIDDLE: button = Qt::MiddleButton; break;
#else
            case BTN_MIDDLE: button = Qt::MidButton; break;
#endif
            case BTN_RIGHT: button = Qt::RightButton; break;
            }
            if (ie_data->value)
                m_buttons |= button;
            else
                m_buttons &= ~button;
            sendMouseEvent(m_x, m_y, m_buttons, 1);
            bPendingMouseEvent = false;
        } else if (ie_data->type == EV_SYN && ie_data->code == SYN_REPORT) {
            if (bPosChanged) {
                // Saturation of position
                m_x = qBound(0, m_x, screenSize.width());
                m_y = qBound(0, m_y, screenSize.height());

                bPosChanged = false;
                if (m_compression) {
                    bPendingMouseEvent = true;
                    iEventCompressCount++;
                } else {
                    sendMouseEvent(m_x, m_y, m_buttons, 0);
                }
            }
        } else if (ie_data->type == EV_MSC && ie_data->code == MSC_SCAN) {
            // kernel encountered an unmapped key - just ignore it
            continue;
        } else {
            unknown = true;
        }
#ifdef QLINUXINPUT_EXTRA_DEBUG
        if (unknown) {
            qWarning("unknown mouse event type=%x, code=%x, value=%x",
                     ie_data->type, ie_data->code, ie_data->value);
        }
#endif
    }
    if (m_compression && bPendingMouseEvent) {
        int distanceSquared = (m_x - m_prevx) * (m_x - m_prevx) + (m_y - m_prevy) * (m_y - m_prevy);
        if (distanceSquared > m_jitterLimitSquared)
            sendMouseEvent(m_x, m_y, m_buttons, 0);
    }
}
