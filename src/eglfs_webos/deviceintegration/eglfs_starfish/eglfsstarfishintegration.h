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

#ifndef EGLFSSTARFISHINTEGRATION_H
#define EGLFSSTARFISHINTEGRATION_H

#include <QMap>
#include <QJsonObject>
#include <private/qeglfskmsgbmintegration_p.h>
#include <private/qeglfskmsgbmdevice_p.h>
#include <private/qeglfskmsgbmscreen_p.h>
#include <private/qeglfskmsdevice_p.h>
#include <qpa/qplatformscreen_p.h>

#include <StarfishServiceIntegration/qstarfishpowerdbridge.h>

class EglFSStarfishScreen;
class EglFSStarfishWindow;

// TODO: remove dup by using WebOSKmsScreenConfig
class EglFSStarfishScreenConfig : public QKmsScreenConfig
{
public:
    EglFSStarfishScreenConfig(QJsonObject);
    void loadConfig() override;

    QVariantMap connector() const { return m_connector; }
private:
    QJsonObject m_configJson;
    QVariantMap m_connector;
};

class EglFSStarfishIntegration : public QEglFSKmsGbmIntegration
{
public:
    EglFSStarfishIntegration();
    QKmsScreenConfig *createScreenConfig() override;

    void screenInit() override;
#ifdef CURSOR_OPENGL
    void waitForVSync(QPlatformSurface *surface) const override;
#endif

    QFunctionPointer platformFunction(const QByteArray &function) const override;
    void *nativeResourceForIntegration(const QByteArray &name) override;
    void *nativeResourceForScreen(const QByteArray &resource, QScreen *screen) override;

    QEglFSWindow *createWindow(QWindow *window) const override;
    QKmsDevice *createDevice() override;

    void updateScreenVisibleDirectly(EglFSStarfishScreen *screen, bool visible, const QString& policy);
    void onPowerStateChanged(const QStarfishPowerDBridge::State& state);

    static void onSnapshotBootDone();
private:
    class EglFSStarfishIntegrationPrivate* d_ptr;
    Q_DECLARE_PRIVATE(EglFSStarfishIntegration);

    QJsonObject m_configJson;
    QList<EglFSStarfishScreen*> m_screens;
};

class EglFSStarfishDevice : public QEglFSKmsGbmDevice
{
public:
    EglFSStarfishDevice(QKmsScreenConfig *screenConfig, const QString &path)
        : QEglFSKmsGbmDevice(screenConfig, path)
    {
    }

    QPlatformScreen *createScreen(const QKmsOutput &output) override;

    void createStarfishScreens();
    QPlatformScreen *createStarfishScreenForConnector(drmModeResPtr resources,
                                                      drmModeConnectorPtr connector,
                                                      ScreenInfo *vinfo,
                                                      const QString& connectorName,
                                                      size_t crtc,
                                                      int selected_mode,
                                                      QList<drmModeModeInfo> modes);

    bool getSizeForPlane(const QString& connectorNameForPlane, QSize &size);

    QVector<uint64_t> getGbmModifiersFromPlane(const QKmsOutput &output);
    drmModePropertyBlobPtr planePropertyBlob(drmModePlanePtr plane, const QByteArray &name);
};

class EglFSStarfishScreen : public QEglFSKmsGbmScreen
{
public:
    EglFSStarfishScreen(QEglFSKmsDevice *device, const QKmsOutput &output, bool headless, QVector<uint64_t> modfiers);

    gbm_surface *createSurface(EGLConfig eglConfig) override;

    QDpi logicalDpi() const override;
    qreal getDevicePixelRatio();
    qreal getDevicePixelRatio() const;
    QRect applicationWindowGeometry() const;

    void updateFlipStatus() override;
    void flip() override;

#ifdef IM_ENABLE
    QPlatformCursor *cursor() const override {
        return m_cursor.get();
    }
#endif

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)) || (defined(HAS_PAGEFLIPPED))
    void pageFlipped(unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec) override;
#endif
    FrameBuffer *framebufferForBufferObject(gbm_bo *bo);

    QRect rawGeometry() const override
    {
        const QKmsOutput &op(m_output);
        return QRect(x(), y(), op.size.width(), op.size.height());
    }

    void setVisiblePolicyValue(const QString& policy, bool visible);
    bool visibleByPolicy(const QString& policy = "");
    void appendPlatformWindow(EglFSStarfishWindow *window);
    void removePlatformWindow(EglFSStarfishWindow *window);

    void setVisible(bool visible);

    void setX(int value) { m_position.setX(value); }
    void setY(int value) { m_position.setY(value); }
    int x() const { return m_position.x(); }
    int y() const { return m_position.y(); }

    bool primary() const;

private:
    qreal m_dpr;
#ifdef IM_ENABLE
    QScopedPointer<QPlatformCursor> m_cursor;
#endif
    QPoint m_position;
    bool m_visible = false;
    QVector<uint64_t> m_modifiers;
    QMap<QString,bool> m_visiblePolicies;
    QList<EglFSStarfishWindow*> m_windows;
};

#endif // EGLFSSTARFISHINTEGRATION_H
