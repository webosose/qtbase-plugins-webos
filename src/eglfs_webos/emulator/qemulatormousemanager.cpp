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

#include <QStringList>
#include <QGuiApplication>
#include <QScreen>
#include <QtDeviceDiscoverySupport/private/qdevicediscovery_p.h>
#include <private/qguiapplication_p.h>
#include <private/qinputdevicemanager_p_p.h>
#include <private/qhighdpiscaling_p.h>

#include <sys/ioctl.h>
#include <fcntl.h>

#include "qemulatormousemanager.h"

QEmulatorMouseManager::QEmulatorMouseManager(const QString &key, const QString &specification, QObject *parent)
    : QObject(parent), m_x(0), m_y(0), m_xoffset(0), m_yoffset(0)
    , m_touchState(0), m_isTouch(true), m_isMultiTouch(false)
{
    enableVboxHostMousePointer();
    registerTouchDevice();
    Q_UNUSED(key);

    QString spec = QString::fromLocal8Bit(qgetenv("QT_QPA_EVDEV_MOUSE_PARAMETERS"));

    if (spec.isEmpty())
        spec = specification;

    QStringList args = spec.split(QLatin1Char(':'));
    QStringList devices;

    foreach (const QString &arg, args) {
        if (arg.startsWith(QLatin1String("/dev/"))) {
            // if device is specified try to use it
            devices.append(arg);
            args.removeAll(arg);
        } else if (arg.startsWith(QLatin1String("xoffset="))) {
            m_xoffset = arg.mid(8).toInt();
        } else if (arg.startsWith(QLatin1String("yoffset="))) {
            m_yoffset = arg.mid(8).toInt();
        }
    }

    // build new specification without /dev/ elements
    m_spec = args.join(QLatin1Char(':'));

    // add all mice for devices specified in the argument list
    foreach (const QString &device, devices)
        addMouse(device);

    if (devices.isEmpty()) {
        qWarning() << "emulatormouse: Using device discovery";
        m_deviceDiscovery = QDeviceDiscovery::create(QDeviceDiscovery::Device_Mouse | QDeviceDiscovery::Device_Touchpad, this);
        if (m_deviceDiscovery) {
            // scan and add already connected keyboards
            const QStringList devices = m_deviceDiscovery->scanConnectedDevices();
            for (const QString &device : devices)
                addMouse(device);

            connect(m_deviceDiscovery, &QDeviceDiscovery::deviceDetected,
                    this, &QEmulatorMouseManager::addMouse);
            connect(m_deviceDiscovery, &QDeviceDiscovery::deviceRemoved,
                    this, &QEmulatorMouseManager::removeMouse);
        }
    }

    QInputDeviceManager *manager = QGuiApplicationPrivate::inputDeviceManager();
    connect(manager, &QInputDeviceManager::cursorPositionChangeRequested, [this](const QPoint &pos) {
        m_x = pos.x();
        m_y = pos.y();
        clampPosition();
    });
}

QEmulatorMouseManager::~QEmulatorMouseManager()
{
    qDeleteAll(m_mice);
    m_mice.clear();
    unregisterTouchDevice();
}

void QEmulatorMouseManager::clampPosition()
{
    // clamp to screen geometry
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    QRect g = QHighDpi::toNativePixels(primaryScreen->virtualGeometry(), primaryScreen);
    if (m_x + m_xoffset < g.left())
        m_x = g.left() - m_xoffset;
    else if (m_x + m_xoffset > g.right())
        m_x = g.right() - m_xoffset;

    if (m_y + m_yoffset < g.top())
        m_y = g.top() - m_yoffset;
    else if (m_y + m_yoffset > g.bottom())
        m_y = g.bottom() - m_yoffset;
}

void QEmulatorMouseManager::registerTouchDevice()
{
// for Multi-profile case...
//    if (m_touchDevice)
//        return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_touchDevice = new QPointingDevice;
    m_touchDevice->setType(QInputDevice::DeviceType::TouchScreen);
    m_touchDevice->setCapabilities(QInputDevice::Capability::Position | QInputDevice::Capability::Area);
    m_touchDevice->setCapabilities(m_touchDevice->capabilities() | QInputDevice::Capability::Pressure);

    QWindowSystemInterface::registerInputDevice(m_touchDevice);
#else
    m_touchDevice = new QTouchDevice;
    m_touchDevice->setName(QLatin1String("EmulatorTouch"));
    m_touchDevice->setType(QTouchDevice::TouchScreen);
    m_touchDevice->setCapabilities(QTouchDevice::Position | QTouchDevice::Area);
    m_touchDevice->setCapabilities(m_touchDevice->capabilities() | QTouchDevice::Pressure);

    QWindowSystemInterface::registerTouchDevice(m_touchDevice);
#endif
}

void QEmulatorMouseManager::unregisterTouchDevice()
{
    if (!m_touchDevice)
        return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Quoted from https://codereview.qt-project.org/c/qt/qtbase/+/263526:
    // When a device is unplugged, the platform plugin should destroy the
    // corresponding QInputDevice instance. There is no unregisterInputDevice()
    // function, because it's enough for the destructor to call
    // QInputDevicePrivate::unregisterDevice(); while other parts of Qt can
    // connect to the QObject::destroyed() signal to be notified when a device is
    // unplugged or otherwise destroyed.
    delete m_touchDevice;
#else
    if (QWindowSystemInterface::isTouchDeviceRegistered(m_touchDevice)) {
        QWindowSystemInterface::unregisterTouchDevice(m_touchDevice);
        delete m_touchDevice;
    }
#endif
    m_touchDevice = nullptr;
}

QWindowSystemInterface::TouchPoint QEmulatorMouseManager::translateTouchPoint(QPoint pos, Qt::MouseButton button, QEvent::Type type, int index) {
    QWindowSystemInterface::TouchPoint touchPoint;
    QPoint pt(pos);
    QRect rc = QGuiApplication::primaryScreen()->virtualGeometry();

    touchPoint.id = index;
    touchPoint.normalPosition = QPointF((qreal)pt.x() / (qreal)rc.width(), (qreal)pt.y() / (qreal)rc.height());
    touchPoint.area = QRectF(pt, QSize(1,1));
    touchPoint.pressure = 1;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (button == Qt::LeftButton) {
        if (type == QEvent::MouseButtonPress) {
            if (index == 0) m_touchState = 1;
            touchPoint.state = QEventPoint::State(Qt::TouchPointPressed);
        } else if (type == QEvent::MouseButtonRelease) {
            if (index == 0) {
                m_touchState = 0;
                m_isMultiTouch = 0;
            }
            touchPoint.state = QEventPoint::State(Qt::TouchPointReleased);
        }
    } else if (m_touchState == 1) {
        touchPoint.state = QEventPoint::State(Qt::TouchPointMoved);
    } else {
        touchPoint.state = QEventPoint::State(Qt::TouchPointReleased);
        touchPoint.pressure = 0;
    }
#else
    if (button == Qt::LeftButton) {
        if (type == QEvent::MouseButtonPress) {
            if (index == 0) m_touchState = 1;
            touchPoint.state = Qt::TouchPointPressed;
        } else if (type == QEvent::MouseButtonRelease) {
            if (index == 0) {
                m_touchState = 0;
                m_isMultiTouch = 0;
            }
            touchPoint.state = Qt::TouchPointReleased;
        }
    } else if (m_touchState == 1) {
        touchPoint.state = Qt::TouchPointMoved;
    } else {
        touchPoint.state = Qt::TouchPointReleased;
        touchPoint.pressure = 0;
    }
#endif

    return touchPoint;
}

void QEmulatorMouseManager::handleMouseEvent(int x, int y, bool abs, Qt::MouseButtons buttons,
                                          Qt::MouseButton button, QEvent::Type type)
{
    if ((type == QEvent::KeyPress) || (type == QEvent::KeyRelease)) {
        if ((x == 0x38) && (!m_isMultiTouch) && (m_touchState == 0) && (type == QEvent::KeyRelease)) {  // LAlt
            m_isTouch = !m_isTouch;
        } else if ((x == 0x1d) && (m_isTouch) && (m_touchState == 0)) {     // LCtrl
            m_isMultiTouch = (type == QEvent::KeyPress);
        }
        return;
    }
    // update current absolute coordinates
    if (!abs) {
        m_x += x;
        m_y += y;
    } else {
        m_x = x;
        m_y = y;
    }

    clampPosition();

    QPoint pos(m_x + m_xoffset, m_y + m_yoffset);
    if (!m_isTouch) {
        QWindowSystemInterface::handleMouseEvent(0, pos, pos, buttons, button, type, QGuiApplicationPrivate::inputDeviceManager()->keyboardModifiers());
    } else {
        //QPoint pos(x+m_xoffset, y+m_yoffset);
        //convert to touch event
        QList<QWindowSystemInterface::TouchPoint> pointList;

        QWindowSystemInterface::TouchPoint point = translateTouchPoint(pos, button, type, 0);
        if (point.pressure != 0) {
            pointList.append(point);
        }

        if (m_isMultiTouch) {
            QRect screenRect = (QGuiApplication::screenAt(pos))->availableGeometry();
            int centerX, centerY;
            centerX = screenRect.width()/2 + screenRect.left();
            centerY = screenRect.height()/2 + screenRect.top();
            QPoint pos2(centerX - (pos.x() - centerX), centerY - (pos.y() - centerY));
            QWindowSystemInterface::TouchPoint secondPoint = translateTouchPoint(pos2, button, type, 1);
            if (secondPoint.pressure != 0) {
                pointList.append(secondPoint);
            }
        }
        if (pointList.count() > 0)
            QWindowSystemInterface::handleTouchEvent(0, m_touchDevice, pointList);
    }
}

void QEmulatorMouseManager::handleWheelEvent(QPoint delta)
{
    QPoint pos(m_x + m_xoffset, m_y + m_yoffset);
    QWindowSystemInterface::handleWheelEvent(0, pos, pos, QPoint(), delta, QGuiApplicationPrivate::inputDeviceManager()->keyboardModifiers());
}

void QEmulatorMouseManager::addMouse(const QString &deviceNode)
{
    qWarning() << "Adding mouse at" << deviceNode;
    QEmulatorMouseHandler *handler = QEmulatorMouseHandler::create(deviceNode, m_spec);
    if (handler) {
        connect(handler, &QEmulatorMouseHandler::handleMouseEvent,
                this, &QEmulatorMouseManager::handleMouseEvent);
        connect(handler, &QEmulatorMouseHandler::handleWheelEvent,
                this, &QEmulatorMouseManager::handleWheelEvent);
        m_mice.insert(deviceNode, handler);
        QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())->setDeviceCount(
            QInputDeviceManager::DeviceTypeTouch, m_mice.count());
    } else {
        qWarning("emulatormouse: Failed to open mouse device %s", qPrintable(deviceNode));
    }
}

void QEmulatorMouseManager::removeMouse(const QString &deviceNode)
{
    if (m_mice.contains(deviceNode)) {
        qWarning() << "Removing mouse at" << deviceNode;
        QEmulatorMouseHandler *handler = m_mice.value(deviceNode);
        m_mice.remove(deviceNode);
        QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())->setDeviceCount(
            QInputDeviceManager::DeviceTypeTouch, m_mice.count());
        delete handler;
    }
}

void QEmulatorMouseManager::handleKeycodeSlot(quint16 keycode, bool pressed, bool autorepeat)
{
    if ((keycode == 0x38) && (!m_isMultiTouch) && (m_touchState == 0) && (!pressed)) {  // LAlt
        m_isTouch = !m_isTouch;
    } else if ((keycode == 0x1d) && (m_isTouch) && (m_touchState == 0)) {     // LCtrl
        m_isMultiTouch = pressed;
    }
}

#define VBOXGUEST_IOCTL_FLAG     0
#define VBOXGUEST_IOCTL_CODE_(Function, Size)  _IOC(_IOC_READ|_IOC_WRITE, 'V', (Function), (Size))
#define VBOXGUEST_IOCTL_CODE(Function, Size)   VBOXGUEST_IOCTL_CODE_((Function) | VBOXGUEST_IOCTL_FLAG, Size)
#define VBOXGUEST_IOCTL_VMMREQUEST(Size)       VBOXGUEST_IOCTL_CODE(2, (Size))

/** generic VMMDev request header */
typedef struct
{
    /** size of the structure in bytes (including body). Filled by caller */
    uint32_t size;
    /** version of the structure. Filled by caller */
    uint32_t version;
    /** type of the request */
    /*VMMDevRequestType*/ uint32_t requestType;
    /** return code. Filled by VMMDev */
    int32_t  rc;
    /** reserved fields */
    uint32_t reserved1;
    uint32_t reserved2;
} VMMdev_request_header;

/** mouse status request structure */
typedef struct
{
    /** header */
    VMMdev_request_header header;
    /** mouse feature mask */
    uint32_t mouseFeatures;
    /** mouse x position */
    int32_t pointerXPos;
    /** mouse y position */
    int32_t pointerYPos;
} VMMdev_req_mouse_status;

/**
 * mouse pointer shape/visibility change request
 */
typedef struct VMMdev_req_mouse_pointer
{
    /** Header. */
    VMMdev_request_header header;
    /** VBOX_MOUSE_POINTER_* bit flags. */
    uint32_t fFlags;
    /** x coordinate of hot spot. */
    uint32_t xHot;
    /** y coordinate of hot spot. */
    uint32_t yHot;
    /** Width of the pointer in pixels. */
    uint32_t width;
    /** Height of the pointer in scanlines. */
    uint32_t height;
    /** Pointer data. */
    char pointerData[4];
} VMMdev_req_mouse_pointer;

// for using the VBOX host cursor
void QEmulatorMouseManager::enableVboxHostMousePointer()
{
    // Open the VirtualBox kernel module driver
    int vbox_fd = open("/dev/vboxguest", O_RDWR, 0);

    if (vbox_fd < 0)
    {
        qWarning("ERROR: vboxguest module open failed: %d", errno);
        goto error;
    }

    VMMdev_req_mouse_status Req;
    Req.header.size        = (uint32_t)sizeof(VMMdev_req_mouse_status);
    Req.header.version     = 0x10001;   // VMMDEV_REQUEST_HEADER_VERSION;
    Req.header.requestType = 2;         // VMMDevReq_SetMouseStatus;
    Req.header.rc          = -1;        // VERR_GENERAL_FAILURE;
    Req.header.reserved1   = 0;
    Req.header.reserved2   = 0;

    // set MouseGuestNeedsHostCursor (bit 2)
    Req.mouseFeatures = (1 << 2);
    Req.pointerXPos = 0;
    Req.pointerYPos = 0;

    // perform VMM request
    if (ioctl(vbox_fd, VBOXGUEST_IOCTL_VMMREQUEST(Req.header.size),
              (void *)&Req.header) < 0)
    {
        qWarning("ERROR: vboxguest rms ioctl failed: %d", errno);
        goto error;
    }
    else if (Req.header.rc < 0)
    {
        qWarning("ERROR: vboxguest SetMouseStatus failed: %d", Req.header.rc);
        goto error;
    }

    VMMdev_req_mouse_pointer mpReq;
    mpReq.header.size        = (uint32_t)sizeof(VMMdev_req_mouse_pointer);
    mpReq.header.version     = 0x10001; // VMMDEV_REQUEST_HEADER_VERSION;
    mpReq.header.requestType = 3;       // VMMDevReq_SetPointerShape;
    mpReq.header.rc          = -1;      // VERR_GENERAL_FAILURE;
    mpReq.header.reserved1   = 0;
    mpReq.header.reserved2   = 0;

    // set fields for SetPointerShape (most importantly VISIBLE)
    mpReq.fFlags = 1;           // VBOX_MOUSE_POINTER_VISIBLE;
    mpReq.xHot = 0;
    mpReq.yHot = 0;
    mpReq.width = 0;
    mpReq.height = 0;
    mpReq.pointerData[0] = 0;
    mpReq.pointerData[1] = 0;
    mpReq.pointerData[2] = 0;
    mpReq.pointerData[3] = 0;

    // perform VMM request
    if (ioctl(vbox_fd, VBOXGUEST_IOCTL_VMMREQUEST(mpReq.header.size),
              (void *)&mpReq.header) < 0)
    {
        qWarning("ERROR: vboxguest mpr ioctl failed: %d", errno);
        goto error;
    }
    else if (mpReq.header.rc < 0)
    {
        qWarning("ERROR: vboxguest SetPointerShape failed: %d", mpReq.header.rc);
        goto error;
    }

    return;
error:
    if (vbox_fd >= 0)
    {
        close(vbox_fd);
    }

    return;
}
