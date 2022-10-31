# Copyright (c) 2023 LG Electronics, Inc.
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

TARGET = eglfs-starfish-integration

PLUGIN_TYPE = egldeviceintegrations
PLUGIN_CLASS_NAME = EglFSStarfishIntegrationPlugin

load(qt_plugin)

QT += eglfs_kms_gbm_support-private eglfsdeviceintegration-private eglfs_kms_support-private kms_support-private devicediscovery_support-private

# CONFIG += inputmanager to use libim through qpa-starfish-input
inputmanager {
    QT += starfishserviceintegration starfishinput
}

equals(QT_MAJOR_VERSION, 5) {
    QT += edid_support-private
} else {
    QT += devicediscovery_support-private
    QMAKE_INCDIR_DRM = $$[QT_SYSROOT]/usr/include/libdrm
}

QMAKE_USE += gbm drm

CONFIG += egl

# Avoid X11 header collision, use generic EGL native types
DEFINES += QT_EGL_NO_X11

SOURCES += $$PWD/eglfsstarfishmain.cpp \
           $$PWD/eglfsstarfishintegration.cpp \
           $$PWD/eglfsstarfishwindow.cpp

HEADERS += $$PWD/eglfsstarfishintegration.h \
           $$PWD/eglfsstarfishwindow.h

inputmanager {
    # TODO: These should be hidden from outside of libim
    DEFINES += IM_ENABLE \
               DEBUG_KEY_EVENT
    INCLUDEPATH  += $$WEBOS_STAGING_INCDIR/im

    !no_multi_input {
        # Multiple input support
        DEFINES += MULTIINPUT_SUPPORT
    }
} else {
    # NOTE: Recently this case wasn't tested.
}

CONFIG += link_pkgconfig
PKGCONFIG = glib-2.0 luna-service2

OTHER_FILES += $$PWD/eglfs_starfish.json

emulator {
    DEFINES += EMULATOR
}
