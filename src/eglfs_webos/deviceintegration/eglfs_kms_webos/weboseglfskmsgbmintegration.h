// Copyright (c) 2020-2023 LG Electronics, Inc.
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
#include <qpa/qplatformscreen_p.h>

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

    void screenInit() override;
    QSurfaceFormat surfaceFormatFor(const QSurfaceFormat &inputFormat) const override;
#ifdef CURSOR_OPENGL
    void waitForVSync(QPlatformSurface *surface) const override;
#endif

    QFunctionPointer platformFunction(const QByteArray &function) const override;
    void *nativeResourceForIntegration(const QByteArray &name) override;
#ifdef IM_ENABLE
    void *nativeResourceForScreen(const QByteArray &resource, QScreen *screen) override;
#endif

    QEglFSWindow *createWindow(QWindow *window) const override;
    QKmsDevice *createDevice() override;

#ifdef PLANE_COMPOSITION
    static void setOverlayBufferObject(const QScreen *screen, void *bo, QRectF rect, uint32_t zpos);
#endif
    bool isProtected() const { return m_protected; }
private:
    QJsonObject m_configJson;
    bool m_protected = false;
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#ifdef PROTECTED_CONTENT
    uint32_t secureMode = 0;
    uint32_t fbTranslationModeId = 0;
#endif
#endif
};

// Hold additional planes
struct WebOSKmsOutput {
    // z-pos, plane
    QMap<uint32_t, QKmsPlane> m_assignedPlanes;
};
#endif

class WebOSEglFSKmsGbmDevice : public QEglFSKmsGbmDevice
{
public:
    WebOSEglFSKmsGbmDevice(QKmsScreenConfig *screenConfig, const QString &path)
        : QEglFSKmsGbmDevice(screenConfig, path)
    {
    }

    QPlatformScreen *createScreen(const QKmsOutput &output) override;

#ifdef PLANE_COMPOSITION
    void addPlaneProperties();
    void assignPlanes(const QKmsOutput &output);

    WebOSKmsOutput &getOutput(const QKmsOutput &output) { return m_webosOutputs[output.connector_id]; }
    WebOSKmsPlane &getPlane(const QKmsPlane &plane) { return m_webosPlanes[plane.id]; }

private:
    // plane_id, WebOSKmsPlane - for additional property
    QMap<uint32_t, WebOSKmsPlane> m_webosPlanes;
    // connector_id, WebOSKmsOutput
    QMap<uint32_t, WebOSKmsOutput> m_webosOutputs;
#endif
};

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) && defined(PLANE_COMPOSITION)
class WebOSEglFSKmsGbmScreen : public QEglFSKmsGbmScreen, public QNativeInterface::Private::QWebOSScreen
#else
class WebOSEglFSKmsGbmScreen : public QEglFSKmsGbmScreen
#endif
{
public:
    WebOSEglFSKmsGbmScreen(QEglFSKmsDevice *device, const QKmsOutput &output, bool headless);

    QDpi logicalDpi() const override;
    qreal getDevicePixelRatio();
    qreal getDevicePixelRatio() const;
    QRect applicationWindowGeometry() const;

    void updateFlipStatus() override;
    void flip() override;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)) || (defined(HAS_PAGEFLIPPED))
    void pageFlipped(unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec) override;
#endif
#ifdef IM_ENABLE
    QPlatformCursor *cursor() const override {
        return m_cursor.get();
    }
#endif

#ifndef PLANE_COMPOSITION
#ifdef SECURE_RENDERING
    uint32_t gbmFlags() override;
#endif
#else
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    uint32_t gbmFlags() override;
#endif

    void setOverlayBufferObject(void *bo, QRectF rect, uint32_t zpos);

    struct BufferObject {
        BufferObject() {}
        BufferObject(gbm_bo *b, QRectF r, bool u) : gbo(b), rect(r), updated(u) {}
        gbm_bo *gbo = nullptr;
        uint32_t fb = 0;
        QRectF rect;
        bool updated = false;
    };

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    int addLayer(void *gbm_bo, const QRectF &geometry) override;
    void setLayerBuffer(int id, void *gbm_bo) override;
    void setLayerGeometry(int id, const QRectF &geometry) override;
    void setLayerAlpha(int id, qreal alpha) override {}
    bool removeLayer(int id) override;
    void addFlipListener(void (*callback)()) override { m_flipCb = callback; }

    void clearBufferObject(uint32_t zpos);
#endif

private:
    uint32_t framebufferForOverlayBufferObject(gbm_bo *bo);

    QMutex m_bufferObjectMutex;

    QVector<struct BufferObject> m_bufferObjects;
    QVector<struct BufferObject> m_nextBufferObjects;
    QVector<struct BufferObject> m_currentBufferObjects;

    void (*m_flipCb)() = nullptr;
    QVector<bool> m_layerAdded;
#endif //PLANE_COMPOSITION
private:
    qreal m_dpr;
#ifdef IM_ENABLE
    QScopedPointer<QPlatformCursor> m_cursor;
#endif
};

#endif
