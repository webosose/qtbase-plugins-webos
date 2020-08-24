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

#ifndef QLINUX_MOUSE_H
#define QLINUX_MOUSE_H

#include <QObject>

class QSocketNotifier;

class QLinuxMouseHandlerData;

class QLinuxMouseHandler : public QObject
{
    Q_OBJECT
public:
    QLinuxMouseHandler(const QString &specification);
    ~QLinuxMouseHandler();

private slots:
    void readMouseData();

private:
    void sendMouseEvent(int x, int y, Qt::MouseButtons buttons, int MTag);
    QSocketNotifier *          m_notify;
    int                        m_fd;
    int                        m_x, m_y;
    int m_prevx, m_prevy;
    int m_xoffset, m_yoffset;
    int m_smoothx, m_smoothy;
    Qt::MouseButtons           m_buttons;
    bool m_compression;
    bool m_smooth;
    int m_jitterLimitSquared;
    float m_scalex, m_scaley;
};

#endif /* QLINUX_MOUSE_H */
