// Copyright (c) 2020-2021 LG Electronics, Inc.
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

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QScreen>
#include <QWindow>

#include <QtDeviceDiscoverySupport/private/qdevicediscovery_p.h>
#include <qpa/qplatformwindow.h>

#include "weboseglfskmsgbmintegration.h"

#ifdef PLANE_COMPOSITION
#include <gbm.h>
#include <gbm_priv.h>
#endif

static QMutex s_frameBufferMutex;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
static void(*page_flip_notifier)(void* key, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec) = nullptr;
#else
static void(*page_flip_notifier)(void* key) = nullptr;
#endif

WebOSKmsScreenConfig::WebOSKmsScreenConfig(QJsonObject config)
    : QKmsScreenConfig()
    , m_configJson(config)
{
}

void WebOSKmsScreenConfig::loadConfig()
{
    if (m_configJson.isEmpty()) {
        qWarning() << "No config set";
        return;
    }

    m_hwCursor = m_configJson.value(QLatin1String("hwcursor")).toBool(m_hwCursor);
    m_devicePath = m_configJson.value(QLatin1String("device")).toString();

    const QJsonArray outputs = m_configJson.value(QLatin1String("outputs")).toArray();
    m_outputSettings.clear();
    for (int i = 0; i < outputs.size(); i++) {
        const QVariantMap outputSettings = outputs.at(i).toObject().toVariantMap();
        if (outputSettings.contains(QStringLiteral("name"))) {
            const QString name = outputSettings.value(QStringLiteral("name")).toString();
            if (m_outputSettings.contains(name))
                qWarning() << "Output" << name << "is duplicated";
            m_outputSettings.insert(name, outputSettings);
        }
    }
}

WebOSEglFSKmsGbmIntegration::WebOSEglFSKmsGbmIntegration()
    : QEglFSKmsGbmIntegration()
{
    static QByteArray json = qgetenv("QT_QPA_EGLFS_CONFIG");

    if (!json.isEmpty()) {
        QFile file(QString::fromUtf8(json));
        if (file.open(QFile::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                m_configJson = doc.array().at(0).toObject();
                qInfo() << "Using config file" << json;
            } else {
                qWarning() << "Invalid config file" << json << "- no top-level JSON object";
            }
            file.close();
        } else {
            qWarning() << "Could not open config file" << json << "for reading";
        }
    } else {
        qWarning("No config file given");
    }
}

QKmsScreenConfig *WebOSEglFSKmsGbmIntegration::createScreenConfig()
{
    QKmsScreenConfig *screenConfig = new WebOSKmsScreenConfig(m_configJson);
    screenConfig->loadConfig();

    return screenConfig;
}

void WebOSEglFSKmsGbmIntegration::screenInit()
{
    QEglFSKmsGbmIntegration::screenInit();

#ifdef PLANE_COMPOSITION
    static_cast<WebOSEglFSKmsGbmDevice *>(m_device)->addPlaneProperties();
#endif
}

QFunctionPointer WebOSEglFSKmsGbmIntegration::platformFunction(const QByteArray &function) const
{
#ifdef PLANE_COMPOSITION
    if (function == "setOverlayBufferObject")
        return QFunctionPointer(setOverlayBufferObject);
#endif

    return nullptr;
}

void *WebOSEglFSKmsGbmIntegration::nativeResourceForIntegration(const QByteArray &name)
{
    if (name == QByteArrayLiteral("gbm_device") && m_device)
        return (void *) static_cast<QEglFSKmsGbmDevice *>(m_device)->gbmDevice();

    if (name == QByteArrayLiteral("dri_address_of_page_flip_notifier") && m_device)
        // return pointer to function "page_flip_notifier"
        return (void*)&page_flip_notifier;

    return QEglFSKmsIntegration::nativeResourceForIntegration(name);
}

QKmsDevice *WebOSEglFSKmsGbmIntegration::createDevice()
{
    QString path = screenConfig()->devicePath();
    if (!path.isEmpty()) {
        qDebug() << "GBM: Using DRM device" << path << "specified in config file";
    } else {
        QDeviceDiscovery *d = QDeviceDiscovery::create(QDeviceDiscovery::Device_VideoMask);
        const QStringList devices = d->scanConnectedDevices();
        qDebug() << "Found the following video devices:" << devices;
        d->deleteLater();

        if (Q_UNLIKELY(devices.isEmpty()))
            qFatal("Could not find DRM device!");

        path = devices.first();
        qDebug() << "Using" << path;
    }

    return new WebOSEglFSKmsGbmDevice(screenConfig(), path);
}

#ifdef PLANE_COMPOSITION
void WebOSEglFSKmsGbmIntegration::setOverlayBufferObject(const QScreen *screen, void *bo, QRectF rect, uint32_t zpos)
{
    if (!screen || !screen->handle())
        return;

    auto *gbmScreen = static_cast<WebOSEglFSKmsGbmScreen *>(screen->handle());
    gbmScreen->setOverlayBufferObject(bo, rect, zpos);
}
#endif

QPlatformScreen * WebOSEglFSKmsGbmDevice::createScreen(const QKmsOutput &output)
{
    QEglFSKmsGbmScreen *screen = new WebOSEglFSKmsGbmScreen(this, output, false);

#ifdef PLANE_COMPOSITION
    assignPlanes(screen->output());
#endif
    createGlobalCursor(screen);

    return screen;
}

#ifdef PLANE_COMPOSITION
void WebOSEglFSKmsGbmDevice::addPlaneProperties()
{
    for (QKmsPlane &plane : m_planes) {
        drmModeObjectPropertiesPtr objProps = drmModeObjectGetProperties(m_dri_fd, plane.id, DRM_MODE_OBJECT_PLANE);
        if (!objProps) {
            qDebug("Failed to query plane %d object properties, ignoring", plane.id);
            continue;
        }

        WebOSKmsPlane &webosPlane = m_webosPlanes[plane.id];
        enumerateProperties(objProps, [&webosPlane, &plane](drmModePropertyPtr prop, quint64 value) {
            if (!strcasecmp(prop->name, "blend_op")) {
                webosPlane.blendPropertyId = prop->prop_id;
            }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#ifdef PROTECTED_CONTENT
            else if (!strcasecmp(prop->name, "fb_translation_mode")) {
                webosPlane.fbTranslationModeId = prop->prop_id;
                for (int i = 0; i < prop->count_enums; i++) {
                    if (strcmp(prop->enums[i].name, "sec"))
                        continue;
                    webosPlane.secureMode = prop->enums[i].value;
                }
            }
#endif
#endif
        });

        drmModeFreeObjectProperties(objProps);
    }
}

static inline void assignPlane(QKmsOutput *output, QKmsPlane *plane)
{
     plane->activeCrtcId = output->crtc_id;
     output->eglfs_plane = plane;
     qDebug() << "assign main plane" << plane->id << "to" << output->name;
}

void WebOSEglFSKmsGbmDevice::assignPlanes(const QKmsOutput &output)
{
    auto userConfig = m_screenConfig->outputSettings();
    QVariantMap userConnectorConfig = userConfig.value(output.name);

    qInfo() << "Try assignPlanes" << userConnectorConfig.value(QStringLiteral("useMultiPlanes")).toBool() << userConnectorConfig;

    if (!userConnectorConfig.value(QStringLiteral("useMultiPlanes")).toBool())
        return;

    // Unset main plane which is assigned from QPA
    if (output.eglfs_plane) {
        QKmsOutput *op = const_cast<QKmsOutput *>(&output);
        op->eglfs_plane->activeCrtcId = 0;
        op->eglfs_plane = 0;
    }

    for (QKmsPlane &plane : m_planes) {
        if (!(plane.possibleCrtcs & (1 << output.crtc_index)))
            continue;

        if (plane.activeCrtcId)
            continue;

        if (!output.eglfs_plane && plane.type == QKmsPlane::OverlayPlane) {
            assignPlane(const_cast<QKmsOutput *>(&output), &plane);
            continue;
        }

        if (plane.type == QKmsPlane::OverlayPlane)
            continue;

        // 1:1 map from KmsOuput to WebOSKmsOutput
        WebOSKmsOutput &webosOutput = m_webosOutputs[output.connector_id];

        // Fully assigned except MainPlane
        if (webosOutput.m_assignedPlanes.size() == Plane_End - 1)
            continue;

        for (int p = 0; p < Plane_End; p++) {
            if (p == MainPlane)
                continue;
            if (webosOutput.m_assignedPlanes.contains(p))
                continue;

            qInfo() << "assign plane" << plane.id << "for zpos" << p << output.name;

            plane.activeCrtcId = output.crtc_id;
            webosOutput.m_assignedPlanes[p] = plane;
            break;
        }
    }
}
#endif

WebOSEglFSKmsGbmScreen::WebOSEglFSKmsGbmScreen(QEglFSKmsDevice *device, const QKmsOutput &output, bool headless)
    : QEglFSKmsGbmScreen(device, output, headless)
{
#ifdef PLANE_COMPOSITION
    m_bufferObjects.resize(Plane_End);
    m_nextBufferObjects.resize(Plane_End);
    m_currentBufferObjects.resize(Plane_End);
    m_layerAdded.resize(Plane_End);
#endif
}

void WebOSEglFSKmsGbmScreen::updateFlipStatus()
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    if (page_flip_notifier)
            (*page_flip_notifier)(this);
#endif
    QEglFSKmsGbmScreen::updateFlipStatus();

#ifdef PLANE_COMPOSITION
    for (int p = 0; p < Plane_End; p++) {
        // The main plane will be handled in QEglFSKmsGbmScreen
        if (p == MainPlane)
            continue;
        struct BufferObject current = m_currentBufferObjects[p];

        if (current.gbo && m_nextBufferObjects[p].updated) {
            gbm_device *device = gbm_bo_get_device(current.gbo);
            drmModeRmFB(gbm_device_get_fd(device), current.fb);
            qDebug() << "destroy current bo" << current.gbo;
            gbm_bo_destroy(current.gbo);
        }

        m_currentBufferObjects[p] = m_nextBufferObjects[p];
        m_nextBufferObjects[p].updated = false;
    }

    if (m_flipCb)
        m_flipCb();
#endif
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void WebOSEglFSKmsGbmScreen::pageFlipped(unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec)
{
    if (page_flip_notifier)
        (*page_flip_notifier)(this, sequence, tv_sec, tv_usec);
}
#endif

void WebOSEglFSKmsGbmScreen::flip()
{
#ifdef PLANE_COMPOSITION
    QKmsOutput &op(output());
    WebOSEglFSKmsGbmDevice *wd = static_cast<WebOSEglFSKmsGbmDevice *>(device());
    WebOSKmsOutput &webosOutput = wd->getOutput(op);

    if (device()->hasAtomicSupport()) {
#if QT_CONFIG(drm_atomic)
        drmModeAtomicReq *request = device()->threadLocalAtomicRequest();

        for (int p = 0; p < Plane_End; p++) {
            // The main plane will be flipped in QEglFSKmsGbmScreen::flip
            // Do some additional command for webos
            if (p == MainPlane) {
                WebOSKmsPlane &wPlane = wd->getPlane(*op.eglfs_plane);
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->zposPropertyId, p);
                //Additional Properties
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, wPlane.blendPropertyId, 2);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#ifdef PROTECTED_CONTENT
                static int secured = qEnvironmentVariableIntValue("QT_EGL_PROTECTED_RENDERING");
                if (secured)
                    drmModeAtomicAddProperty(request, op.eglfs_plane->id, wPlane.fbTranslationModeId, wPlane.secureMode);
#endif
#endif
                continue;
            }

            if (!webosOutput.m_assignedPlanes.contains(p))
                continue;

            QKmsPlane &plane = webosOutput.m_assignedPlanes[p];
            WebOSKmsPlane &wPlane = wd->getPlane(plane);

            //In rendering thread - which access to m_bufferObjects
            QMutexLocker lock(&m_bufferObjectMutex);
            if (!m_bufferObjects[p].updated) {
                lock.unlock();
                continue;
            }

            BufferObject bo = m_bufferObjects[p];
            m_bufferObjects[p].updated = false;
            lock.unlock();

            m_nextBufferObjects[p] = bo;

            if (!bo.gbo) {
                //clear overlay plane
                qDebug() << op.name << "clear overlay" << "plane" << plane.id << "zpos" << p;
                drmModeAtomicAddProperty(request, plane.id, plane.framebufferPropertyId, 0);
                drmModeAtomicAddProperty(request, plane.id, plane.crtcPropertyId, 0);
                continue;
            }

            qDebug() << "render buffer object plane" << p << "bo" << bo.gbo << bo.rect;

            // Can be null to mean clear overlay plane
            bo.fb = framebufferForOverlayBufferObject(bo.gbo);
            // Set fb to clear it on updateFlipStatus
            m_nextBufferObjects[p].fb = bo.fb;

            uint32_t sw = gbm_bo_get_width(bo.gbo);
            uint32_t sh = gbm_bo_get_height(bo.gbo);

            // Crop out the dest region to avoid from overflowing the screen
            bo.rect = bo.rect.intersected(QRectF(QPointF(0,0), geometry().size()));

            qDebug() << "overlay" << plane.id << "plane" << p << "fb" << bo.fb << "source" << sw << sh << "dest" << bo.rect << name() << this;

            drmModeAtomicAddProperty(request, plane.id, plane.framebufferPropertyId, bo.fb);
            drmModeAtomicAddProperty(request, plane.id, plane.crtcPropertyId, op.crtc_id);
            drmModeAtomicAddProperty(request, plane.id, plane.srcXPropertyId, 0);
            drmModeAtomicAddProperty(request, plane.id, plane.srcYPropertyId, 0);
            drmModeAtomicAddProperty(request, plane.id, plane.srcwidthPropertyId, sw << 16);
            drmModeAtomicAddProperty(request, plane.id, plane.srcheightPropertyId, sh << 16);
            drmModeAtomicAddProperty(request, plane.id, plane.crtcXPropertyId, bo.rect.x());
            drmModeAtomicAddProperty(request, plane.id, plane.crtcYPropertyId, bo.rect.y());
            drmModeAtomicAddProperty(request, plane.id, plane.crtcwidthPropertyId, bo.rect.width());
            //HACK: limit to minimum size of destination height to avoid failure of drm atomic commit
            drmModeAtomicAddProperty(request, plane.id, plane.crtcheightPropertyId, qMax(bo.rect.height(), 270.0));
            drmModeAtomicAddProperty(request, plane.id, plane.zposPropertyId, p);
            //Additional Properties
            drmModeAtomicAddProperty(request, plane.id, wPlane.blendPropertyId, 2);

#ifdef PROTECTED_CONTENT
            int secured = 0;
            gbm_perform(GBM_PERFORM_GET_SECURE_BUFFER_STATUS, bo.gbo, &secured);
            if (secured) {
                qDebug() << "overlay bo" << bo.gbo << "secured";
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                drmModeAtomicAddProperty(request, plane.id, wPlane.fbTranslationModeId, wPlane.secureMode);
#else
                drmModeAtomicAddProperty(request, plane.id, plane.fbTranslationModeId, plane.secureMode);
#endif
            }
#endif
        }
#endif
    }
#endif

    QEglFSKmsGbmScreen::flip();
}

#ifdef PLANE_COMPOSITION
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
uint32_t WebOSEglFSKmsGbmScreen::gbmFlags()
{
    uint32_t flags = QEglFSKmsGbmScreen::gbmFlags();
#ifdef PROTECTED_CONTENT
    if (qEnvironmentVariableIntValue("QT_EGL_PROTECTED_RENDERING"))
        flags |= GBM_BO_USAGE_PROTECTED_QTI;
#endif
    return flags;
}
#endif

uint32_t WebOSEglFSKmsGbmScreen::framebufferForOverlayBufferObject(gbm_bo *bo)
{
    struct gbm_import_fd_data import_fd_data;
    generic_buf_layout_t buf_layout;
    uint32_t alignedWidth = 0;
    uint32_t alignedHeight = 0;
    uint32_t stride[4] = { 0 };
    uint32_t offset[4] = { 0 };
    uint32_t ubwc_status = 0;

    gbm_perform(GBM_PERFORM_GET_BO_ALIGNED_WIDTH, bo, &alignedWidth);
    gbm_perform(GBM_PERFORM_GET_BO_ALIGNED_HEIGHT, bo, &alignedHeight);
    gbm_perform(GBM_PERFORM_GET_UBWC_STATUS, bo, &ubwc_status);

    import_fd_data.fd = gbm_bo_get_fd(bo);
    import_fd_data.format = gbm_bo_get_format(bo);

    int ret = gbm_perform(GBM_PERFORM_GET_PLANE_INFO, bo, &buf_layout);
    if (ret == GBM_ERROR_NONE) {
        for(int j=0; j< buf_layout.num_planes; j++) {
            offset[j] = buf_layout.planes[j].offset;
            stride[j] = buf_layout.planes[j].v_increment;
        }
    }

    qDebug() << bo << gbm_bo_get_fd(bo) << alignedWidth << alignedHeight << "format" << import_fd_data.format << "NV12" << GBM_FORMAT_NV12 << ubwc_status;

    // The gem_handle should not be closed by another screen
    // Cover mirroring case when same gem handle is used across the screens
    QMutexLocker lock(&s_frameBufferMutex);

    uint32_t gem_handle = 0;
    ret = drmPrimeFDToHandle(device()->fd(), import_fd_data.fd, &gem_handle);
    if (ret) {
        qWarning() << "Failed to drmPrimeFDToHandle" << device()->fd() << import_fd_data.fd;
        return 0;
    }

    struct drm_mode_fb_cmd2 cmd2 {};
    cmd2.width = alignedWidth;
    cmd2.height = alignedHeight;
    cmd2.pixel_format = import_fd_data.format;
    cmd2.flags = DRM_MODE_FB_MODIFIERS;

    for (int i = 0; i < buf_layout.num_planes; i++) {
        cmd2.handles[i] = gem_handle;
        cmd2.pitches[i] = buf_layout.planes[0].v_increment;
        cmd2.offsets[i] = 0;
        cmd2.modifier[i] = ubwc_status == 0 ? 0 : DRM_FORMAT_MOD_QCOM_COMPRESSED;
    }

    // In ubwc case, the offsets[0] is non-zero.
    if (import_fd_data.format == GBM_FORMAT_NV12) {
        cmd2.pitches[0] = buf_layout.planes[0].v_increment;
        cmd2.pitches[1] = cmd2.pitches[0];
        cmd2.offsets[0] = 0;
        cmd2.offsets[1] = cmd2.pitches[0] * cmd2.height;
    }

    if ((ret = drmIoctl(device()->fd(), DRM_IOCTL_MODE_ADDFB2, &cmd2))) {
        qWarning() << "Failed to DRM_IOCTL_MODE_ADDFB2" << bo << gem_handle;
        return 0;
    }

    uint32_t fb = cmd2.fb_id;

    struct drm_gem_close gem_close = {};
    gem_close.handle = gem_handle;
    if (drmIoctl(device()->fd(), DRM_IOCTL_GEM_CLOSE, &gem_close)) {
        qWarning() << "Failed to DRM_IOCTL_GEM_CLOSE" << bo << gem_handle;
        return 0;
    }

    return fb;
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
void WebOSEglFSKmsGbmScreen::setOverlayBufferObject(void *bo, QRectF rect, uint32_t zpos)
{
    QKmsOutput &op(output());
    WebOSEglFSKmsGbmDevice *wd = static_cast<WebOSEglFSKmsGbmDevice *>(device());
    WebOSKmsOutput &webosOutput = wd->getOutput(op);

    if (!webosOutput.m_assignedPlanes.contains(zpos)) {
        qWarning() << "WebOSEglFSKmsGbmScreen::setOverlayBufferObject - no plane for" << zpos << "bo" << bo;
        gbm_bo_destroy((gbm_bo *)bo);
        return;
    }

    qDebug() << "QEglFSKmsGbmScreen::setOverlayPlaneFramebuffer:" << bo << name() << rect << zpos;

    // In GUI thread
    QMutexLocker lock(&m_bufferObjectMutex);

    if (m_bufferObjects[zpos].updated) {
        gbm_bo *old_bo = m_bufferObjects[zpos].gbo;
        qDebug() << "destroy old bo" << old_bo;
        gbm_bo_destroy(old_bo);
    }

    m_bufferObjects[zpos] = BufferObject((gbm_bo *)bo, rect, true);
}
#else
void WebOSEglFSKmsGbmScreen::setOverlayBufferObject(void *bo, QRectF rect, uint32_t zpos)
{
    qDebug() << "WebOSEglFSKmsGbmScreen::setOverlayPlaneFramebuffer:" << bo << name() << rect << zpos;

    // Invalid destination rect
    if (bo != nullptr && rect.isEmpty())
        return;

    clearBufferObject(zpos);

    // In GUI thread
    QMutexLocker lock(&m_bufferObjectMutex);
    m_bufferObjects[zpos] = BufferObject((gbm_bo *)bo, rect, true);
}

int WebOSEglFSKmsGbmScreen::addLayer(void *gbm_bo, const QRectF &geometry)
{
    QKmsOutput &op(output());
    WebOSEglFSKmsGbmDevice *wd = static_cast<WebOSEglFSKmsGbmDevice *>(device());
    WebOSKmsOutput &webosOutput = wd->getOutput(op);

    for (int p = 0; p < Plane_End; p++) {
        if (!webosOutput.m_assignedPlanes.contains(p))
            continue;

        if (m_layerAdded[p])
            continue;

        qInfo() << "addLayer plane" << p << "bo" << gbm_bo << "dest" << geometry << name() << this;

        m_layerAdded[p] = true;
        setOverlayBufferObject(gbm_bo, geometry, p);
        return p;
    }

    return -1;
}

void WebOSEglFSKmsGbmScreen::setLayerBuffer(int zpos, void *bo)
{
    if (!m_layerAdded[zpos]) {
        qWarning() << "The layer" << zpos << "is not added yet.";
        return;
    }

    qDebug() << "WebOSEglFSKmsGbmScreen::setLayerBuffer plane" << zpos << "bo" << bo << name() << this;

    // Use previous geometry rect
    setOverlayBufferObject((gbm_bo *)bo,  m_bufferObjects[zpos].rect, zpos);
}

void WebOSEglFSKmsGbmScreen::setLayerGeometry(int zpos, const QRectF &geometry)
{
    if (!m_layerAdded[zpos])
        qWarning() << "The layer" << zpos << "is not added yet.";

    qDebug() << "WebOSEglFSKmsGbmScreen::setLayerGeometry" << geometry << "plane" << zpos << name() << this;

    // In GUI thread
    QMutexLocker lock(&m_bufferObjectMutex);

    m_bufferObjects[zpos].rect = geometry;
}

bool WebOSEglFSKmsGbmScreen::removeLayer(int zpos)
{
    if (!m_layerAdded[zpos]) {
        qWarning() << "The layer" << zpos << "is not added yet.";
        return false;
    }

    m_layerAdded[zpos] = false;

    qInfo() << "removeLayer plane" << zpos << name() << this;

    // Use previous geometry rect
    setOverlayBufferObject(nullptr, QRectF(), zpos);
    return true;
}

void WebOSEglFSKmsGbmScreen::clearBufferObject(uint32_t zpos)
{
    // In GUI thread
    QMutexLocker lock(&m_bufferObjectMutex);

    if (m_bufferObjects[zpos].updated) {
        gbm_bo *old_bo = m_bufferObjects[zpos].gbo;
        m_bufferObjects[zpos].gbo = nullptr;
        qDebug() << "destroy old bo" << old_bo;
        gbm_bo_destroy(old_bo);
    }
}
#endif

#endif
