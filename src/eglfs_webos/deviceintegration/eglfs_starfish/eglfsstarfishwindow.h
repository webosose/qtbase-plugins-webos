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

#ifndef EGLFSSTARFISHWINDOW_H
#define EGLFSSTARFISHWINDOW_H

#include <private/qeglfskmsgbmwindow_p.h>

class QEglFSKmsGbmIntegration;
class EglFSStarfishScreen;

class EglFSStarfishWindow : public QEglFSKmsGbmWindow
{
public:
    EglFSStarfishWindow(QWindow *, const QEglFSKmsGbmIntegration *);
    ~EglFSStarfishWindow() override;

    void setVisible(bool visible) override;
    void setGeometry(const QRect &rect) override;
    void requestActivateWindow() override;

private:
    EglFSStarfishScreen *m_screen = nullptr;
};

#endif // EGLFSSTARFISHWINDOW_H
