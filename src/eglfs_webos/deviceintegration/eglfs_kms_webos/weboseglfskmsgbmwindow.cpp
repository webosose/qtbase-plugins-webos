// Copyright (c) 2022-2023 LG Electronics, Inc.
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

#include <qpa/qwindowsysteminterface.h>
#include <private/qwindow_p.h>

#ifdef IM_ENABLE
#include "qstarfishinputmanager.h"
#endif

#include "weboseglfskmsgbmwindow.h"

WebOSEglFSKmsGbmWindow::WebOSEglFSKmsGbmWindow(QWindow* window, const QEglFSKmsGbmIntegration *integration)
    : QEglFSKmsGbmWindow(window, integration)
{
}

void WebOSEglFSKmsGbmWindow::requestActivateWindow()
{
    QEglFSKmsGbmWindow::requestActivateWindow();

#ifdef IM_ENABLE
    //This sequence makes sure that Top Window get focus
    //so that it can receive key event.
    qInfo() << "requestActivateWindow QStarfishInputManager::startInputService";
    QStarfishInputManager::instance()->startInputService();
#endif
}
