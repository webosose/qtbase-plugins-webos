# Copyright (c) 2020 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

TARGET = weboseglfs

PLUGIN_TYPE = platforms
PLUGIN_CLASS_NAME = WebOSEglFSIntegrationPlugin
!equals(TARGET, $$QT_DEFAULT_QPA_PLUGIN): PLUGIN_EXTENDS = -

load(qt_plugin)

QT += eglfsdeviceintegration-private input_support-private devicediscovery_support-private fb_support-private

CONFIG += egl

# Avoid X11 header collision, use generic EGL native types
DEFINES += QT_EGL_NO_X11

SOURCES += $$PWD/weboseglfsmain.cpp \
           $$PWD/weboseglfsintegration.cpp \
           $$PWD/webosdevicediscovery_udev_sorted.cpp

HEADERS += $$PWD/weboseglfsintegration.h \
           $$PWD/webosdevicediscovery_udev_sorted_p.h

OTHER_FILES += $$PWD/eglfs_webos.json

emulator {
    DEFINES += EMULATOR
    include($$PWD/emulator/emulator.pri)
}
