// Copyright (c) 2023 LG Electronics, Inc.
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

#include <QtCore/QLoggingCategory>
#include <qpa/qwindowsysteminterface.h>
#include <private/qwindow_p.h>

#ifdef IM_ENABLE
#include "qstarfishinputmanager.h"
#endif

#include "eglfsstarfishwindow.h"
#include "eglfsstarfishintegration.h"

Q_DECLARE_LOGGING_CATEGORY(qLcStarfishDebug)

EglFSStarfishWindow::EglFSStarfishWindow(QWindow* window, const QEglFSKmsGbmIntegration *integration)
    : QEglFSKmsGbmWindow(window, integration)
{
    m_screen = static_cast<EglFSStarfishScreen*>(screen());
    if (m_screen)
        m_screen->appendPlatformWindow(this);
}

EglFSStarfishWindow::~EglFSStarfishWindow()
{
    m_screen->removePlatformWindow(this);
}

void EglFSStarfishWindow::setVisible(bool visible)
{
    qCDebug(qLcStarfishDebug) << "EglFSStarfishWindow::setVisible" << visible;
    QEglFSKmsGbmWindow::setVisible(visible);

    EglFSStarfishScreen *screen = static_cast<EglFSStarfishScreen*>(this->screen());
    if (screen)
        screen->setVisible(visible);
}

// Overrides eglfs. Need to consider upstream change
void EglFSStarfishWindow::setGeometry(const QRect &r)
{
    qCDebug(qLcStarfishDebug) << "EglFSStarfishWindow::setGeometry" << r;
#ifdef IM_ENABLE
    QRect rect = r;
    QPlatformWindow::setGeometry(rect);

    QWindowSystemInterface::handleGeometryChange(window(), rect);

    const QRect lastReportedGeometry = qt_window_private(window())->geometry;
    if (rect != lastReportedGeometry)
        QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(0, 0), rect.size()));

    EglFSStarfishScreen *screen = static_cast<EglFSStarfishScreen*>(this->screen());
    if (screen)
        screen->setX(rect.x());
#else
    QEglFSKmsGbmWindow::setGeometry(r);
#endif
}

void EglFSStarfishWindow::requestActivateWindow()
{
    QEglFSKmsGbmWindow::requestActivateWindow();
}

