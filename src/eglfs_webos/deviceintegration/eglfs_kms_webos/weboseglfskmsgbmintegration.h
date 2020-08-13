// Copyright (c) 2020 LG Electronics, Inc.
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

#ifndef WEBOSEGLFSKMSGBMINTEGRATION_H
#define WEBOSEGLFSKMSGBMINTEGRATION_H

#include <QMap>
#include <QJsonObject>
#include <private/qeglfskmsgbmintegration_p.h>
#include <private/qeglfskmsgbmdevice_p.h>
#include <private/qeglfskmsgbmscreen_p.h>
#include <private/qeglfskmsdevice_p.h>

class WebOSKmsScreenConfig : public QKmsScreenConfig
{
public:
    WebOSKmsScreenConfig(QJsonObject);
    void loadConfig() override;

private:
    QJsonObject m_configJson;
};

class WebOSEglFSKmsGbmIntegration : public QEglFSKmsGbmIntegration
{
public:
    WebOSEglFSKmsGbmIntegration();
    QKmsScreenConfig *createScreenConfig() override;

#ifdef PLANE_COMPOSITION
    void screenInit() override;

    static void setOverlayBufferObject(const QScreen *screen, void *bo, QRectF rect, uint32_t zpos);
    QFunctionPointer platformFunction(const QByteArray &function) const override;
    void *nativeResourceForIntegration(const QByteArray &name) override;

    QKmsDevice *createDevice() override;
#endif

private:
    QJsonObject m_configJson;
};

#ifdef PLANE_COMPOSITION
enum PlaneOrder {
    VideoPlane = 0,
    FullscreenPlane = 1,
    MainPlane = 2,
    Plane_End = 3
};

// Hold additional properties
struct WebOSKmsPlane {
    uint32_t blendPropertyId = 0;
};

// Hold additional planes
struct WebOSKmsOutput {
    // z-pos, plane
    QMap<uint32_t, QKmsPlane> m_assignedPlanes;
};

class WebOSEglFSKmsGbmDevice : public QEglFSKmsGbmDevice
{
public:
    WebOSEglFSKmsGbmDevice(QKmsScreenConfig *screenConfig, const QString &path)
        : QEglFSKmsGbmDevice(screenConfig, path)
    {
    }

    QPlatformScreen *createScreen(const QKmsOutput &output) override;

    void addPlaneProperties();
    void assignPlanes(const QKmsOutput &output);

    WebOSKmsOutput &getOutput(const QKmsOutput &output) { return m_webosOutputs[output.connector_id]; }
    WebOSKmsPlane &getPlane(const QKmsPlane &plane) { return m_webosPlanes[plane.id]; }

private:
    // plane_id, WebOSKmsPlane - for additional property
    QMap<uint32_t, WebOSKmsPlane> m_webosPlanes;
    // connector_id, WebOSKmsOutput
    QMap<uint32_t, WebOSKmsOutput> m_webosOutputs;
};

class WebOSEglFSKmsGbmScreen : public QEglFSKmsGbmScreen
{
public:
    WebOSEglFSKmsGbmScreen(QEglFSKmsDevice *device, const QKmsOutput &output, bool headless);

    void flip() override;
    void updateFlipStatus() override;

    void setOverlayBufferObject(void *bo, QRectF rect, uint32_t zpos);

    struct BufferObject {
        BufferObject() {}
        BufferObject(gbm_bo *b, QRectF r, bool u) : gbo(b), rect(r), updated(u) {}
        gbm_bo *gbo = nullptr;
        uint32_t fb = 0;
        QRectF rect;
        bool updated = false;
    };

private:
    uint32_t framebufferForOverlayBufferObject(gbm_bo *bo);

    QMutex m_bufferObjectMutex;

    QVector<struct BufferObject> m_bufferObjects;
    QVector<struct BufferObject> m_nextBufferObjects;
    QVector<struct BufferObject> m_currentBufferObjects;
};
#endif

#endif
