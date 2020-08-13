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

TARGET = webos-eglfs-kms-integration

PLUGIN_TYPE = egldeviceintegrations
PLUGIN_CLASS_NAME = WebOSEglFSKmsGbmIntegrationPlugin

load(qt_plugin)

QT += eglfs_kms_gbm_support-private eglfsdeviceintegration-private eglfs_kms_support-private kms_support-private edid_support-private

QMAKE_USE += gbm drm

CONFIG += egl

# Avoid X11 header collision, use generic EGL native types
DEFINES += QT_EGL_NO_X11

plane_composition {
    DEFINES += PLANE_COMPOSITION
}

SOURCES += $$PWD/weboseglfskmsgbmmain.cpp \
           $$PWD/weboseglfskmsgbmintegration.cpp

HEADERS += $$PWD/weboseglfskmsgbmintegration.h

OTHER_FILES += $$PWD/eglfs_kms_webos.json
