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

#ifndef QEMULATORMOUSEHANDLER_H
#define QEMULATORMOUSEHANDLER_H

#include <QObject>
#include <QString>
#include <QPoint>
#include <QEvent>

class QSocketNotifier;

class QEmulatorMouseHandler : public QObject
{
    Q_OBJECT
public:
    static QEmulatorMouseHandler *create(const QString &device, const QString &specification);
    ~QEmulatorMouseHandler();

    void readMouseData();

signals:
    void handleMouseEvent(int x, int y, bool abs, Qt::MouseButtons buttons,
                          Qt::MouseButton button, QEvent::Type type);
    void handleWheelEvent(QPoint delta);

private:
    QEmulatorMouseHandler(const QString &device, int fd, bool abs, bool compression, int jitterLimit);

    void sendMouseEvent();
    bool getHardwareMaximum();

    QString m_device;
    int m_fd;
    QSocketNotifier *m_notify;
    int m_x, m_y;
    int m_prevx, m_prevy;
    bool m_abs;
    bool m_compression;
    Qt::MouseButtons m_buttons;
    Qt::MouseButton m_button;
    QEvent::Type m_eventType;
    int m_jitterLimitSquared;
    bool m_prevInvalid;
    int m_hardwareWidth;
    int m_hardwareHeight;
    qreal m_hardwareScalerY;
    qreal m_hardwareScalerX;
};

#endif // QEMULATORMOUSEHANDLER_H
