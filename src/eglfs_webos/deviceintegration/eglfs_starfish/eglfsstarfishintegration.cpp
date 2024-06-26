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

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QScreen>
#include <QWindow>
#include <QtCore/QLoggingCategory>
#include <QRegularExpression>

#include <QtDeviceDiscoverySupport/private/qdevicediscovery_p.h>
#include <QtEglFSDeviceIntegration/private/qeglfshooks_p.h>
#include <qpa/qplatformwindow.h>

#include <private/qguiapplication_p.h>

#include "eglfsstarfishintegration.h"
#include "eglfsstarfishwindow.h"

#ifdef MULTIINPUT_SUPPORT
#include "qstarfishinputmanager.h"
#endif
#ifdef IM_ENABLE
#include "qstarfishimcursor.h"
#include <im/im_openapi_input_type.h>
#endif

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

static QMutex s_frameBufferMutex;

Q_LOGGING_CATEGORY(qLcStarfishDebug, "qt.qpa.eglfs.starfish")

enum OutputConfiguration {
    OutputConfigOff,
    OutputConfigPreferred,
    OutputConfigCurrent,
    OutputConfigSkip,
    OutputConfigMode,
    OutputConfigModeline
};

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)) || (defined(HAS_PAGEFLIPPED))
static void(*page_flip_notifier)(void* key, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec) = nullptr;
#else
static void(*page_flip_notifier)(void* key) = nullptr;
#endif

static void setScreenVisibleDirectly(QScreen *screen, bool visible, QString reason)
{
    if (!screen) {
        qWarning() << "[QPA:EGL:INTERFACE] null screen";
        return;
    }

    EglFSStarfishIntegration* integration = static_cast<EglFSStarfishIntegration*>(qt_egl_device_integration());
    EglFSStarfishScreen* platformScreen = static_cast<EglFSStarfishScreen*>(screen->handle());
    if (integration && platformScreen) {
        integration->updateScreenVisibleDirectly(platformScreen, visible, reason);
    } else {
        qWarning() << "[QPA:EGL:INTERFACE] null egl_integration or egl_screen";
    }
}

static void setScreenPositionDirectly(QScreen *screen, QPoint position)
{
    if (!screen) {
        qWarning() << "[QPA:EGL:INTERFACE] null screen";
        return;
    }

    EglFSStarfishScreen* platformScreen = static_cast<EglFSStarfishScreen*>(screen->handle());
    if (platformScreen) {
        platformScreen->setX(position.x());
        platformScreen->setY(position.y());
    } else {
        qWarning() << "[QPA:EGL:INTERFACE] null egl_screen";
    }
}

static void setScreenRegionDirectly(QScreen *screen, QRect region)
{
    qWarning() << "setScreenPositionDirectly: NOT IMPLEMENTED";
#if 0
    if (!screen) {
        qWarning() << "[QPA:EGL:INTERFACE] null screen";
        return;
    }

    QStarfishEglScreen* egl_screen = static_cast<QStarfishEglScreen*>(screen->handle());
    if (egl_screen) {
        egl_screen->setRegion(region);
    } else {
        qWarning() << "[QPA:EGL:INTERFACE] null egl_screen";
    }
#endif
}

struct OrderedScreen
{
    OrderedScreen() : screen(nullptr) { }
    OrderedScreen(QPlatformScreen *screen, const QKmsDevice::ScreenInfo &vinfo)
        : screen(screen), vinfo(vinfo) { }
    QPlatformScreen *screen;
    QKmsDevice::ScreenInfo vinfo;
};

// in qkmsdevice.cpp
QDebug operator<<(QDebug dbg, const OrderedScreen &s);

static bool orderedScreenLessThan(const OrderedScreen &a, const OrderedScreen &b)
{
    return a.vinfo.virtualIndex < b.vinfo.virtualIndex;
}

static const char * const connector_type_names[] = { // must match DRM_MODE_CONNECTOR_*
    "None",
    "VGA",
    "DVI",
    "DVI",
    "DVI",
    "Composite",
    "TV",
    "LVDS",
    "CTV",
    "DIN",
    "DP",
    "HDMI",
    "HDMI",
    "TV",
    "eDP",
    "Virtual",
    "DSI"
};

// TODO: UNUSED
static bool parseModeline(const QByteArray &text, drmModeModeInfoPtr mode)
{
    char hsync[16];
    char vsync[16];
    float fclock;

    mode->type = DRM_MODE_TYPE_USERDEF;
    mode->hskew = 0;
    mode->vscan = 0;
    mode->vrefresh = 0;
    mode->flags = 0;

    QTextStream stream(text, QIODevice::ReadOnly);
    stream >> fclock >> mode->hdisplay >> mode->hsync_start >> mode->hsync_end
           >> mode->htotal >> mode->vdisplay >> mode->vsync_start >> mode->vsync_end
           >> mode->vtotal >> hsync >> vsync;
    if (stream.status() != QTextStream::Ok) {
        qWarning() << "Failed to parse the modeline";
        return false;
    }

    mode->clock = fclock * 1000;

    if (strcmp(hsync, "+hsync") == 0)
        mode->flags |= DRM_MODE_FLAG_PHSYNC;
    else if (strcmp(hsync, "-hsync") == 0)
        mode->flags |= DRM_MODE_FLAG_NHSYNC;
    else
        return false;

    if (strcmp(vsync, "+vsync") == 0)
        mode->flags |= DRM_MODE_FLAG_PVSYNC;
    else if (strcmp(vsync, "-vsync") == 0)
        mode->flags |= DRM_MODE_FLAG_NVSYNC;
    else
        return false;

    return true;
}

static inline void assignPlane(QKmsOutput *output, QKmsPlane *plane)
{
    if (output->eglfs_plane)
        output->eglfs_plane->activeCrtcId = 0;

    plane->activeCrtcId = output->crtc_id;
    output->eglfs_plane = plane;
}

static QByteArray nameForConnector(const drmModeConnectorPtr connector)
{
    QByteArray connectorName("UNKNOWN");

    if (connector->connector_type < ARRAY_LENGTH(connector_type_names))
        connectorName = connector_type_names[connector->connector_type];

    connectorName += QByteArray::number(connector->connector_type_id);

    return connectorName;
}

EglFSStarfishScreenConfig::EglFSStarfishScreenConfig(QJsonObject config)
    : QKmsScreenConfig()
    , m_configJson(config)
{
}

void EglFSStarfishScreenConfig::loadConfig()
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
    qCDebug(qLcStarfishDebug) << "loadConfig: m_outputSettings:"
                              << m_outputSettings;
}

class EglFSStarfishIntegrationPrivate : public QObject {
    class EglFSStarfishIntegration* q_ptr;
    Q_DECLARE_PUBLIC(EglFSStarfishIntegration);

    public:
        EglFSStarfishIntegrationPrivate(EglFSStarfishIntegration* parent) : q_ptr(parent) {
            QObject::connect(QStarfishPowerDBridge::instance(),
                     &QStarfishPowerDBridge::powerStateChanged,
                     this,
                     &EglFSStarfishIntegrationPrivate::onPowerStateChanged);
        }

    private slots:
        void onPowerStateChanged(const QStarfishPowerDBridge::State& state) {
            Q_Q(EglFSStarfishIntegration);
            if (q) {
                emit q->onPowerStateChanged(state);
            }
        }
};

EglFSStarfishIntegration::EglFSStarfishIntegration()
    : QEglFSKmsGbmIntegration()
    , d_ptr(new EglFSStarfishIntegrationPrivate(this))
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

QKmsScreenConfig *EglFSStarfishIntegration::createScreenConfig()
{
    QKmsScreenConfig *screenConfig = new EglFSStarfishScreenConfig(m_configJson);
    screenConfig->loadConfig();

    return screenConfig;
}

void EglFSStarfishIntegration::screenInit()
{
    EglFSStarfishDevice *device = static_cast<EglFSStarfishDevice*>(m_device);
    if (!device)
        qFatal("Expect EglFSStarfishDevice");

    device->createStarfishScreens();

    QList<QScreen*> screens = QGuiApplication::screens();
    for (QList<QScreen*>::iterator i = screens.begin(); i != screens.end(); ++i) {
        EglFSStarfishScreen *screen = static_cast<EglFSStarfishScreen*>((*i)->handle());
        m_screens.append(screen);
    }
}

QFunctionPointer EglFSStarfishIntegration::platformFunction(const QByteArray &function) const
{
    return nullptr;
}

void *EglFSStarfishIntegration::nativeResourceForIntegration(const QByteArray &name)
{
    if (name == QByteArrayLiteral("gbm_device") && m_device)
        return (void *) static_cast<QEglFSKmsGbmDevice *>(m_device)->gbmDevice();

#if !defined(EMULATOR)
    if (name == QByteArrayLiteral("dri_address_of_page_flip_notifier") && m_device)
        // return pointer to function "page_flip_notifier"
        return (void*)&page_flip_notifier;
#endif

    QByteArray lowerCaseResource = name.toLower();

    void *input_interface = QStarfishInputManager::instance()->nativeResourceForIntegration(lowerCaseResource);
    if (input_interface)
        return input_interface;

    if (lowerCaseResource == "") {
        ;
    } else if (lowerCaseResource == "setscreenvisibledirectly") {
        return (void*)setScreenVisibleDirectly;
    } else if (lowerCaseResource == "setscreenpositiondirectly") {
        return (void*)setScreenPositionDirectly;
    } else if (lowerCaseResource == "setscreenregiondirectly") {
        return (void*)setScreenRegionDirectly;
    }

    return QEglFSKmsIntegration::nativeResourceForIntegration(name);
}

QEglFSWindow *EglFSStarfishIntegration::createWindow(QWindow *window) const
{
    return new EglFSStarfishWindow(window, this);
}

QKmsDevice *EglFSStarfishIntegration::createDevice()
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

    return new EglFSStarfishDevice(screenConfig(), path);
}

void *EglFSStarfishIntegration::nativeResourceForScreen(const QByteArray &resource, QScreen *screen)
{
    QByteArray lowerCaseResource = resource.toLower();

    void *input_interface = QStarfishInputManager::instance()->nativeResourceForScreen(lowerCaseResource, screen);
    if (input_interface)
        return input_interface;

    return QEglFSKmsGbmIntegration::nativeResourceForScreen(resource, screen);
}

#ifdef CURSOR_OPENGL
#include <qstarfishimcursor.h>

// Use 'extern' instead of 'Q_DECL_IMPORT' here, because 'Q_DECL_IMPORT' has not external linkage in the Qt plugin.
//extern bool starfish_im_cursor_cursorNeedUpdate;
void EglFSStarfishIntegration::waitForVSync(QPlatformSurface *surface) const
{
    //starfish_im_cursor_cursorNeedUpdate = false;
    if (auto *window = static_cast<QPlatformWindow *>(surface)) {
        if (auto screen = window->screen()) {
            if (auto cursor = static_cast<QStarfishIMCursor *>(screen->cursor())) {
                cursor->paint();
            }
        }
    }

    QEglFSKmsIntegration::waitForVSync(surface);
}
#endif // CURSOR_OPENGL

static inline uint32_t *
formats_ptr(struct drm_format_modifier_blob *blob)
{
    return (uint32_t *)(((char *)blob) + blob->formats_offset);
}

static inline struct drm_format_modifier *
modifiers_ptr(struct drm_format_modifier_blob *blob)
{
    return (struct drm_format_modifier *)
        (((char *)blob) + blob->modifiers_offset);
}

QVector<uint64_t> EglFSStarfishDevice::getGbmModifiersFromPlane(const QKmsOutput &output)
{
    QVector<uint64_t> modifiers;

    drmModePlaneResPtr planeResources = drmModeGetPlaneResources(m_dri_fd);
    if (!planeResources)
        return modifiers;
    const unsigned int countPlanes = planeResources->count_planes;
    qCDebug(qLcStarfishDebug, "Found %u planes", countPlanes);

    drmModePlanePtr plane = nullptr;
    bool found = false;

    for (unsigned int planeIdx = 0; planeIdx < countPlanes; ++planeIdx) {
        plane = drmModeGetPlane(m_dri_fd, planeResources->planes[planeIdx]);
        if (!plane) {
            qCDebug(qLcStarfishDebug, "Failed to query plane %u, ignoring", planeIdx);
            continue;
        }
        if (plane->plane_id != output.eglfs_plane->id) {
            drmModeFreePlane(plane);
            continue;
        } else {
            found = true;
            break;
        }
    }

    if (!found) {
        qCDebug(qLcStarfishDebug, "No matching plane found having id %d", output.eglfs_plane->id);
        return modifiers;
    }

    drmModePropertyBlobRes *blob = planePropertyBlob(plane, QByteArrayLiteral("IN_FORMATS"));
    if (!blob) {
        drmModeFreePlane(plane);
        return modifiers;
    }

    struct drm_format_modifier_blob *fmt_mod_blob = static_cast<struct drm_format_modifier_blob *>(blob->data);
    uint32_t *blob_formats = formats_ptr(fmt_mod_blob);
    struct drm_format_modifier *blob_modifiers = modifiers_ptr(fmt_mod_blob);

    for (int i = 0; i < fmt_mod_blob->count_formats; i++) {
        uint32_t _f = blob_formats[i];

        if (output.drm_format != _f)
            continue;

        for (int j = 0; j < fmt_mod_blob->count_modifiers; j++) {
            struct drm_format_modifier *mod = &blob_modifiers[j];

            if ((i < mod->offset) || (i > mod->offset + 63))
                continue;
            if (!(mod->formats & (1UL << (i - mod->offset))))
                continue;

            modifiers.append(mod->modifier);

            qCDebug(qLcStarfishDebug, "Found modifier(0x%llx) for format(%c%c%c%c)\n",
                    mod->modifier,
                    (_f>>0)&0xff, (_f>>8)&0xff, (_f>>16)&0xff, (_f>>24)&0xff);
        }
    }

    drmModeFreePropertyBlob(blob);
    drmModeFreePlane(plane);

    return modifiers;
}

QPlatformScreen * EglFSStarfishDevice::createScreen(const QKmsOutput &output)
{
    QVector<uint64_t> modifiers = getGbmModifiersFromPlane(output);
    EglFSStarfishScreen *screen = new EglFSStarfishScreen(this, output, false, modifiers);

#ifndef IM_ENABLE
    createGlobalCursor(screen);
#endif

    return screen;
}

// from luna-surfacemanager
static bool parseGeometryString(const QString& string, QRect &geometry, int &rotation, double &ratio)
{
    // Syntax: WIDTH[x]HEIGHT[+/-]X[+/-]Y[r]ROTATION[s]RATIO
    QRegularExpression re("([0-9]+)x([0-9]+)([+-][0-9]+)([+-][0-9]+)r([0-9]+)s([0-9]+.?[0-9]*)");
    QRegularExpressionMatch match = re.match(string);

    if (match.hasMatch()) {
        QString w = match.captured(1);
        QString h = match.captured(2);
        QString x = match.captured(3);
        QString y = match.captured(4);
        QString r = match.captured(5);
        QString s = match.captured(6);
        geometry.setRect(x.toInt(), y.toInt(), w.toInt(), h.toInt());
        rotation = r.toInt();
        ratio = s.toDouble();
        qCDebug(qLcStarfishDebug) << "Geometry string" << string << "->" << w << h << x << y << r << s;
        return true;
    } else {
        qWarning() << "Invalid geometry string" << string;
    }

    return false;
}

bool EglFSStarfishDevice::getSizeForPlane(const QString& connectorNameForPlane, QSize &size)
{
    auto userConfig = m_screenConfig->outputSettings();
    QVariantMap userConnectorConfig = userConfig.value(connectorNameForPlane);

    const QByteArray geometryString = userConnectorConfig.value(QStringLiteral("geometry"), QStringLiteral(""))
        .toByteArray();
    if (geometryString.isEmpty()) {
        qCDebug(qLcStarfishDebug) << "No \"geometry\" is available for" << connectorNameForPlane;
        return false;
    }

    QRect geometry;
    int rotation;
    double ratio;

    if (parseGeometryString(geometryString, geometry, rotation, ratio)) {
        size = geometry.size();
        qCDebug(qLcStarfishDebug) << "framebuffer size is" << size << "for" << connectorNameForPlane;
        return true;
    }
    return false;
}

drmModePropertyBlobPtr EglFSStarfishDevice::planePropertyBlob(drmModePlanePtr plane, const QByteArray &name)
{
    drmModePropertyPtr prop;
    drmModePropertyBlobPtr blob = nullptr;

    drmModeObjectPropertiesPtr objProps = drmModeObjectGetProperties(m_dri_fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);

    if (!objProps) {
        qCDebug(qLcStarfishDebug, "Failed to query plane %d object properties, ignoring", plane->plane_id);
        return blob;
    }

    for (int i = 0; i < objProps->count_props && !blob; i++) {
        prop = drmModeGetProperty(m_dri_fd, objProps->props[i]);
        if (!prop)
            continue;
        if ((prop->flags & DRM_MODE_PROP_BLOB) && (strcmp(prop->name, name.constData()) == 0)) {
            uint64_t prop_values = objProps->prop_values[i];
            uint32_t u_prop_values;
            if (prop_values > UINT_MAX)
                continue;

            u_prop_values = (uint32_t) prop_values;
            blob = drmModeGetPropertyBlob(m_dri_fd, u_prop_values);
        }
        drmModeFreeProperty(prop);
    }

    drmModeFreeObjectProperties(objProps);

    return blob;
}

void EglFSStarfishDevice::createStarfishScreens()
{
    drmSetClientCap(m_dri_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

#if QT_CONFIG(drm_atomic)
    // check atomic support
    m_has_atomic_support = !drmSetClientCap(m_dri_fd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (m_has_atomic_support) {
        qCDebug(qLcStarfishDebug, "Atomic reported as supported");
        if (qEnvironmentVariableIntValue("QT_QPA_EGLFS_KMS_ATOMIC")) {
            qCDebug(qLcStarfishDebug, "Atomic enabled");
        } else {
            qCDebug(qLcStarfishDebug, "Atomic disabled");
            m_has_atomic_support = false;
        }
    }
#endif

    drmModeResPtr resources = drmModeGetResources(m_dri_fd);
    if (!resources) {
        qErrnoWarning(errno, "drmModeGetResources failed");
        return;
    }

    discoverPlanes();

    QList<OrderedScreen> screens;

    drmModeConnectorPtr connector = nullptr;
    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(m_dri_fd, resources->connectors[i]);
        if (connector)
            break;
    }
    if (!connector) {
        qErrnoWarning(errno, "no connector found");
        return;
    }

    // root@LGwebOSTV:~# cat /tmp/xdg/eglfs_config.json
    // [{
    //   "device": "/dev/dri/card0",
    //   "hwcursor": false,
    //   "outputs":[
    //     {"name":"fb0","geometry":"1920x1080+0+0r0s1", "primary": true},
    //     {"name":"fb1","geometry":"512x2160+0+0r0s1"}
    //   ]
    //  }
    // ]
    const QByteArray connectorName = nameForConnector(connector);

    const int crtcIdx = crtcForConnector(resources, connector);
    if (crtcIdx < 0) {
        qWarning() << "No usable crtc/encoder pair for connector" << connectorName;
        return;
    }
    const uint32_t crtc = (unsigned int) crtcIdx;
    const uint32_t crtc_id = resources->crtcs[crtc];

    // Get the current mode on the current crtc
    drmModeModeInfo crtc_mode;
    memset(&crtc_mode, 0, sizeof crtc_mode);
    if (drmModeEncoderPtr encoder = drmModeGetEncoder(m_dri_fd, connector->encoder_id)) {
        drmModeCrtcPtr crtc = drmModeGetCrtc(m_dri_fd, encoder->crtc_id);
        drmModeFreeEncoder(encoder);

        if (!crtc)
            return;

        if (crtc->mode_valid)
            crtc_mode = crtc->mode;

        drmModeFreeCrtc(crtc);
    }

    QList<drmModeModeInfo> modes;
    modes.reserve(connector->count_modes);
    qCDebug(qLcStarfishDebug) << connectorName << "mode count:" << connector->count_modes
                         << "crtc index:" << crtc << "crtc id:" << crtc_id;
    for (int i = 0; i < connector->count_modes; i++) {
        const drmModeModeInfo &mode = connector->modes[i];
        qCDebug(qLcStarfishDebug) << "mode" << i << mode.hdisplay << "x" << mode.vdisplay
                                  << '@' << mode.vrefresh << "hz";
        modes << connector->modes[i];
    }

    int preferred = -1;
    int current = -1;
    int best = -1;

    int configurationRefresh = 0;

    auto userConfig = m_screenConfig->outputSettings();
    QVariantMap userConnectorConfig = userConfig.value(QString::fromUtf8(connectorName));

    for (int i = modes.size() - 1; i >= 0; i--) {
        const drmModeModeInfo &m = modes.at(i);

        if (!memcmp(&crtc_mode, &m, sizeof m))
            current = i;

        if (m.type & DRM_MODE_TYPE_PREFERRED)
            preferred = i;

        best = i;
    }

    if (current < 0 && crtc_mode.clock != 0) {
        modes << crtc_mode;
        current = modes.size() - 1;
    }

    int selected_mode = -1;

    if (preferred >= 0)
        selected_mode = preferred;
    else if (current >= 0)
        selected_mode = current;
    else if (best >= 0)
        selected_mode = best;

    if (selected_mode < 0) {
        qWarning() << "No modes available for output" << connectorName;
        return;
    } else {
        int width = modes[selected_mode].hdisplay;
        int height = modes[selected_mode].vdisplay;
        uint32_t refresh = modes[selected_mode].vrefresh;
        qCDebug(qLcStarfishDebug) << "Selected mode" << selected_mode << ":" << width << "x" << height
                                  << '@' << refresh << "hz for output" << connectorName;
    }

    QString fbdevs = QString(qgetenv("QT_QPA_EGLFS_FB"));
    if (fbdevs.isEmpty())
        fbdevs = QByteArrayLiteral("/dev/fb0:/dev/fb1");

    QStringList screenNames = fbdevs.split(':');

    QString connectorNameForPrimary(screenNames.at(0).split('/').last()); // fb0
    ScreenInfo primaryInfo;
    QPlatformScreen *primaryScreen = createStarfishScreenForConnector(resources, connector, &primaryInfo,
                                                                      connectorNameForPrimary,
                                                                      crtc, selected_mode,
                                                                      modes);
    if (primaryScreen)
        screens.append(OrderedScreen(primaryScreen, primaryInfo));

    QString connectorNameForSecondary(screenNames.at(1).split('/').last()); // fb1
    ScreenInfo secondaryInfo;
    if (userConfig.contains(connectorNameForSecondary)) {
        QPlatformScreen *secondaryScreen = createStarfishScreenForConnector(resources, connector, &secondaryInfo,
                                                                            connectorNameForSecondary,
                                                                            crtc, selected_mode,
                                                                            modes);
        if (secondaryScreen)
            screens.append(OrderedScreen(secondaryScreen, secondaryInfo));
    }

    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);

    QPoint virtualPos(0, 0);
    for (const OrderedScreen &orderedScreen : screens) {
        QPlatformScreen *s = orderedScreen.screen;
        qCDebug(qLcStarfishDebug) << "Adding QPlatformScreen" << s << "(" << s->name() << ")"
                                  << "to QPA with geometry" << s->geometry()
                                  << "and isPrimary=" << orderedScreen.vinfo.isPrimary;
        registerScreen(s, orderedScreen.vinfo.isPrimary, virtualPos, QList<QPlatformScreen *>() << s);
    }
}

QPlatformScreen *EglFSStarfishDevice::createStarfishScreenForConnector(drmModeResPtr resources,
                                                                       drmModeConnectorPtr connector,
                                                                       ScreenInfo *vinfo,
                                                                       const QString& connectorName,
                                                                       size_t crtc,
                                                                       int selected_mode,
                                                                       QList<drmModeModeInfo> modes)
{
    Q_ASSERT(vinfo);

    int configurationRefresh = 0;
    auto userConfig = m_screenConfig->outputSettings();
    QVariantMap userConnectorConfig = userConfig.value(connectorName);

    *vinfo = ScreenInfo();
    vinfo->virtualIndex = INT_MAX;
    if (userConnectorConfig.value(QStringLiteral("primary")).toBool())
        vinfo->isPrimary = true;

    // TODO: get format from config if necessary
    // const QByteArray formatStr = userConnectorConfig.value(QStringLiteral("format"), QString())
    //         .toByteArray().toLower();
    uint32_t drmFormat = DRM_FORMAT_XRGB8888;
    bool drmFormatExplicit = false;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    qCDebug(qLcStarfishDebug) << "Format is" << Qt::hex << drmFormat << Qt::dec << "requested_by_user =" << drmFormatExplicit
                         << "for output" << connectorName;
#else
    qCDebug(qLcStarfishDebug) << "Format is" << hex << drmFormat << dec << "requested_by_user =" << drmFormatExplicit
                         << "for output" << connectorName;
#endif

    if (crtc > UINT_MAX)
        return nullptr;
    const uint32_t crtc_id = resources->crtcs[crtc];
    // TODO: implement cloneSource if necessary
    QString cloneSource;

    QSize framebufferSize;
    if (!getSizeForPlane(connectorName, framebufferSize)) {
        if (!vinfo->isPrimary)
            qFatal("No geometry config found for %s", connectorName.toUtf8().constData());
    }

    // FHD is default for primary plane
    if (framebufferSize.isEmpty()) {
        framebufferSize = QSize(1920, 1080);
        qWarning() << "Use default framebuffer size for primary plane" << framebufferSize;
    }

    QSizeF physSize;

    QKmsOutput output;
    output.name = connectorName;
    output.connector_id = connector->connector_id;
    output.crtc_index = crtc;
    output.crtc_id = crtc_id;
    // TODO: implement physical_size if necessary
    output.physical_size = physSize;
    // TODO: implement preferred mode if necessary
    output.preferred_mode = selected_mode;
    output.mode = selected_mode;
    output.mode_set = false;
    output.saved_crtc = drmModeGetCrtc(m_dri_fd, crtc_id);
    output.modes = modes;
    output.subpixel = connector->subpixel;
    output.dpms_prop = connectorProperty(connector, QByteArrayLiteral("DPMS"));
    output.edid_blob = connectorPropertyBlob(connector, QByteArrayLiteral("EDID"));
    output.wants_forced_plane = false;
    output.forced_plane_id = 0;
    output.forced_plane_set = false;
    output.drm_format = drmFormat;
    output.drm_format_requested_by_user = drmFormatExplicit;
    output.clone_source = cloneSource;
    output.size = framebufferSize;

#if QT_CONFIG(drm_atomic)
    if (drmModeCreatePropertyBlob(m_dri_fd, &modes[selected_mode], sizeof(drmModeModeInfo),
                                  &output.mode_blob_id) != 0) {
        qCDebug(qLcStarfishDebug) << "Failed to create mode blob for mode" << selected_mode;
    }

    parseConnectorProperties(output.connector_id, &output);
    parseCrtcProperties(output.crtc_id, &output);
#endif

    QString planeListStr;
    QKmsPlane::Type planeType = vinfo->isPrimary ? QKmsPlane::PrimaryPlane : QKmsPlane::OverlayPlane;

    if (output.crtc_index > 31) {
        qWarning("left shifting by more than 31 bits has undefined behavior");
        return nullptr;
    }
    uint32_t bits_crtc = 1U << output.crtc_index;

    for (QKmsPlane &plane : m_planes) {
        int int_possibleCrtcs = plane.possibleCrtcs;
        uint32_t uint_possibleCrtcs = int_possibleCrtcs < 0 ? 0 : (uint32_t) int_possibleCrtcs;
        if (uint_possibleCrtcs & bits_crtc) {
            output.available_planes.append(plane);
            planeListStr.append(QString::number(plane.id));
            planeListStr.append(u' ');

            // Choose the plane that is not already assigned to
            // another screen's associated crtc.
            if (!output.eglfs_plane && plane.type == planeType && !plane.activeCrtcId) {
                output.wants_forced_plane = true;
                output.forced_plane_id = plane.id;
                assignPlane(&output, &plane);
            }
        }
    }
    qCDebug(qLcStarfishDebug, "Output %s can use %d planes: %s",
            connectorName.toUtf8().constData(), int(output.available_planes.count()), qPrintable(planeListStr));

    if (output.eglfs_plane) {
        qCDebug(qLcStarfishDebug, "Chose plane %u for output %s (crtc id %u) (may not be applicable)",
                output.eglfs_plane->id, connectorName.toUtf8().constData(), output.crtc_id);
    }

    m_crtc_allocator |= bits_crtc;

    vinfo->output = output;

    return createScreen(output);
}

EglFSStarfishScreen::EglFSStarfishScreen(QEglFSKmsDevice *device, const QKmsOutput &output, bool headless, QVector<uint64_t> modifiers)
    : QEglFSKmsGbmScreen(device, output, headless)
#ifdef IM_ENABLE
    , m_cursor(new QStarfishIMCursor(device->fd(), output.crtc_id, this))
#endif
    , m_dpr(-1.0)
    , m_modifiers(modifiers)
{
}

static inline uint32_t drmFormatToGbmFormat(uint32_t drmFormat)
{
    Q_ASSERT(DRM_FORMAT_XRGB8888 == GBM_FORMAT_XRGB8888);
    return drmFormat;
}

static inline uint32_t gbmFormatToDrmFormat(uint32_t gbmFormat)
{
    Q_ASSERT(DRM_FORMAT_XRGB8888 == GBM_FORMAT_XRGB8888);
    return gbmFormat;
}

gbm_surface *EglFSStarfishScreen::createSurface(EGLConfig eglConfig)
{
    qInfo() << "#### EglFSStarfishScreen::createSurface";
    // Copied from QEglFSKmsGbmScreen::createSurface
    if (!m_gbm_surface) {
        qCDebug(qLcStarfishDebug) << "Creating gbm_surface for screen" << name()
                                  << "with modifiers" << m_modifiers;

        const auto gbmDevice = static_cast<QEglFSKmsGbmDevice *>(device())->gbmDevice();
        // If there was no format override given in the config file,
        // query the native (here, gbm) format from the EGL config.
        const bool queryFromEgl = !m_output.drm_format_requested_by_user;

        int int_rawGeometryWidth = rawGeometry().width();
        int int_rawGeometryHeight = rawGeometry().height();
        int int_modifiersSize = m_modifiers.size();
        uint32_t uint_rawGeometryWidth = int_rawGeometryWidth < 0 ? 0 : (uint32_t) int_rawGeometryWidth;
        uint32_t uint_rawGeometryHeight = int_rawGeometryHeight < 0 ? 0 : (uint32_t) int_rawGeometryHeight;
        uint32_t uint_modifiersSize = int_modifiersSize < 0 ? 0 : (uint32_t) int_modifiersSize;

        if (queryFromEgl) {
            EGLint native_format = -1;
            EGLBoolean success = eglGetConfigAttrib(display(), eglConfig, EGL_NATIVE_VISUAL_ID, &native_format);
            qCDebug(qLcStarfishDebug) << "Got native format" << hex << native_format << dec
                                      << "from eglGetConfigAttrib() with return code" << bool(success);

            if (success) {
                uint32_t uint_nativeFormat = native_format < 0 ? 0 : (uint32_t) native_format;
                if (m_modifiers.size()) {
                    m_gbm_surface = gbm_surface_create_with_modifiers(gbmDevice,
                                                                      uint_rawGeometryWidth,
                                                                      uint_rawGeometryHeight,
                                                                      uint_nativeFormat,
                                                                      m_modifiers.data(), uint_modifiersSize);
                } else {
                    m_gbm_surface = gbm_surface_create(gbmDevice,
                                                       uint_rawGeometryWidth,
                                                       uint_rawGeometryHeight,
                                                       uint_nativeFormat,
                                                       GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
                }
                if (m_gbm_surface)
                    m_output.drm_format = gbmFormatToDrmFormat(uint_nativeFormat);
            }
        }

        // Fallback for older drivers, and when "format" is explicitly specified
        // in the output config. (not guaranteed that the requested format works
        // of course, but do what we are told to)
        if (!m_gbm_surface) {
            uint32_t gbmFormat = drmFormatToGbmFormat(m_output.drm_format);
            if (queryFromEgl)
                qCDebug(qLcStarfishDebug, "Could not create surface with EGL_NATIVE_VISUAL_ID, falling back to format %x", gbmFormat);

            if (m_modifiers.size()) {
                m_gbm_surface = gbm_surface_create_with_modifiers(gbmDevice,
                                                                  uint_rawGeometryWidth,
                                                                  uint_rawGeometryHeight,
                                                                  gbmFormat,
                                                                  m_modifiers.data(), uint_modifiersSize);
            } else {
                m_gbm_surface = gbm_surface_create(gbmDevice,
                                                   uint_rawGeometryWidth,
                                                   uint_rawGeometryHeight,
                                                   gbmFormat,
                                                   GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
            }
        }
    }
    return m_gbm_surface; // not owned, gets destroyed in QEglFSKmsGbmIntegration::destroyNativeWindow() via QEglFSKmsGbmWindow::invalidateSurface()
}

qreal EglFSStarfishScreen::getDevicePixelRatio()
{
    if (!qFuzzyCompare(m_dpr, -1.0))
        return m_dpr;

    QByteArray env = qgetenv("WEBOS_DEVICE_PIXEL_RATIO");
    if (!env.isEmpty()) {
        // Override devicePixelRatio if WEBOS_DEVICE_PIXEL_RATIO is set
        // Valid values are:
        // 1) WEBOS_DEVICE_PIXEL_RATIO=auto
        // 2) WEBOS_DEVICE_PIXEL_RATIO=<ratio>
        if (env.startsWith("auto") && geometry().isValid()) {
            QRect ssg = geometry();
            QRect awg = applicationWindowGeometry();
            if (awg.width() <= 0 && awg.height() <= 0)
                m_dpr = QPlatformScreen::devicePixelRatio();
            else if (awg.width() <= 0)
                m_dpr = (qreal)ssg.height()/awg.height();
            else if (awg.height() <= 0)
                m_dpr = (qreal)ssg.width()/awg.width();
            else {
                m_dpr = qMin((qreal)ssg.width()/awg.width(),
                             (qreal)ssg.height()/awg.height());
            }
            qInfo() << "set auto devicePixelRatio as dpr=" << m_dpr
                    << "screen=" << ssg << ", window=" << awg;
            return m_dpr;
        }

        double ratio = env.toDouble();
        if (ratio > 0) {
            m_dpr = (qreal) ratio;
            qInfo() << "set WEBOS_DEVICE_PIXEL_RATIO devicePixelRatio as dpr=" << m_dpr;
            return m_dpr;
        }
    }

    m_dpr = QPlatformScreen::devicePixelRatio();
    qInfo() << "set default devicePixelRatio as dpr=" << m_dpr;
    return m_dpr;
}

qreal EglFSStarfishScreen::getDevicePixelRatio() const
{
    return const_cast<EglFSStarfishScreen*>(this)->getDevicePixelRatio();
}

QDpi EglFSStarfishScreen::logicalDpi() const
{
    qreal dpr = getDevicePixelRatio();
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    QDpi baseDpi = logicalBaseDpi();
#else
    QDpi baseDpi = QEglFSKmsGbmScreen::logicalDpi();
#endif

    return QDpi(baseDpi.first * dpr, baseDpi.second * dpr);
}

QRect EglFSStarfishScreen::applicationWindowGeometry() const
{
    if (!qEnvironmentVariableIsEmpty("WEBOS_COMPOSITOR_GEOMETRY")) {
        // Syntax: WIDTH[x]HEIGHT[+/-]X[+/-]Y[r]ROTATION[s]RATIO
        QRegularExpression re("([0-9]+)x([0-9]+)([+-][0-9]+)([+-][0-9]+)r([0-9]+)s([0-9]+.?[0-9]*)");
        QRegularExpressionMatch match = re.match(QString(qgetenv("WEBOS_COMPOSITOR_GEOMETRY")));
        if (match.hasMatch())
            return QRect(0, 0, match.captured(1).toInt(), match.captured(2).toInt());
    }

    qCritical() << "failure in getting application window geometry=" << QRect();
    return QRect();
}

void EglFSStarfishScreen::updateFlipStatus()
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0)) && (!defined(HAS_PAGEFLIPPED))
    if (page_flip_notifier)
            (*page_flip_notifier)(this);
#endif
    QEglFSKmsGbmScreen::updateFlipStatus();
}

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)) || (defined(HAS_PAGEFLIPPED))
void EglFSStarfishScreen::pageFlipped(unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec)
{
    if (page_flip_notifier)
        (*page_flip_notifier)(this, sequence, tv_sec, tv_usec);
}
#endif

void EglFSStarfishScreen::flip()
{
    if (!m_visible) {
        updateFlipStatus();
        return;
    }

    // For headless screen just return silently. It is not necessarily an error
    // to end up here, so show no warnings.
    if (m_headless)
        return;

    if (m_cloneSource) {
        qWarning("Screen %s clones another screen. swapBuffers() not allowed.", qPrintable(name()));
        return;
    }

    if (!m_gbm_surface) {
        qWarning("Cannot sync before platform init!");
        return;
    }

    m_gbm_bo_next = gbm_surface_lock_front_buffer(m_gbm_surface);
    if (!m_gbm_bo_next) {
        qWarning("Could not lock GBM surface front buffer!");
        return;
    }

    FrameBuffer *fb = framebufferForBufferObject(m_gbm_bo_next);
    ensureModeSet(fb->fb);

    QKmsOutput &op(output());
    // TODO: UNUSED
    const int fd = device()->fd();
    m_flipPending = true;

    if (device()->hasAtomicSupport()) {
#if QT_CONFIG(drm_atomic)
        drmModeAtomicReq *request = device()->threadLocalAtomicRequest();
        if (request) {
            int int_geometryX = geometry().x();
            int int_geometryY = geometry().y();
            int int_geometryWidth = geometry().width();
            int int_geometryHeight = geometry().height();
            uint32_t uint_geometryX = int_geometryX < 0 ? 0 : (uint32_t) int_geometryX;
            uint32_t uint_geometryY = int_geometryY < 0 ? 0 : (uint32_t) int_geometryY;
            uint32_t uint_geometryWidth = int_geometryWidth < 0 ? 0 : (uint32_t) int_geometryWidth;
            uint32_t uint_geometryHeight = int_geometryHeight < 0 ? 0 : (uint32_t) int_geometryHeight;

            const uint32_t crtc_x = uint_geometryX;
            const uint32_t crtc_y = uint_geometryY;

            const uint32_t w = uint_geometryWidth;
            const uint32_t h = uint_geometryHeight;

            uint32_t crtc_w = uint_geometryWidth;
            uint32_t crtc_h = uint_geometryHeight;

            bool isPrimaryPlane = op.eglfs_plane && op.eglfs_plane->type == QKmsPlane::PrimaryPlane;

            if (isPrimaryPlane) {
                // gbm surface for primary plane always has 1920x1080 and it should be mapped to
                // CRTC 3840x2160 for 4K
                crtc_w = op.modes[op.mode].hdisplay;
                crtc_h = op.modes[op.mode].vdisplay;
            }

            qCDebug(qLcStarfishDebug, "%s (plane %u): %ux%u -> %ux%u+%u+%u",
                    name().toUtf8().constData(), op.forced_plane_id,
                    w, h, crtc_w, crtc_h, crtc_x, crtc_y);
            if(op.eglfs_plane)
            {
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->framebufferPropertyId, fb->fb);
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->crtcPropertyId, op.crtc_id);
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->srcwidthPropertyId, w << 16);
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->srcXPropertyId, 0);
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->srcYPropertyId, 0);
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->srcheightPropertyId, h << 16);
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->crtcXPropertyId, crtc_x);
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->crtcYPropertyId, crtc_y);
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->crtcwidthPropertyId, crtc_w);
                drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->crtcheightPropertyId, crtc_h);
            }
        }
#endif // QT_CONFIG(drm_atomic)
    } else {
        qFatal("DRM atomic support is mandatory. Set QT_QPA_EGLFS_KMS_ATOMIC=1");
    }
#if QT_CONFIG(drm_atomic)
    device()->threadLocalAtomicCommit(this);
#endif
}

void EglFSStarfishScreen::setVisible(bool visible)
{
    if (m_visible == visible)
        return;

    QKmsOutput &op(output());
    qCDebug(qLcStarfishDebug, "setVisible plane %u to %s (was %s)", op.forced_plane_id,
            qPrintable(visible ? "true" : "false"),
            qPrintable(m_visible ? "true" : "false"));

    m_visible = visible;

    if (!device()->hasAtomicSupport())
        qFatal("DRM atomic support is mandatory. Set QT_QPA_EGLFS_KMS_ATOMIC=1");

    if (!m_visible) {
#if QT_CONFIG(drm_atomic)
        qCDebug(qLcStarfishDebug, "setVisible: Turn off for invisible plane %u", output().forced_plane_id);

        if (m_gbm_bo_next) {
            qCDebug(qLcStarfishDebug, "setVisible: Release bo not to wait in waitForFlip()");
            gbm_surface_release_buffer(m_gbm_surface, m_gbm_bo_next);
            m_gbm_bo_next = nullptr;
        }

        // TODO: device()->threadLocalAtomicRequest() if possible
        drmModeAtomicReq *request = drmModeAtomicAlloc();
        if (!request) {
            qWarning("setVisible: Fail to drmModeAtomicAlloc");
            return;
        }

        drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->framebufferPropertyId, 0);
        drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->crtcPropertyId, 0);
        drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->srcwidthPropertyId, 0);
        drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->srcXPropertyId, 0);
        drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->srcYPropertyId, 0);
        drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->srcheightPropertyId, 0);
        drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->crtcXPropertyId, 0);
        drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->crtcYPropertyId, 0);
        drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->crtcwidthPropertyId, 0);
        drmModeAtomicAddProperty(request, op.eglfs_plane->id, op.eglfs_plane->crtcheightPropertyId, 0);

        uint32_t flags = 0;
        int ret = drmModeAtomicCommit(device()->fd(), request, flags, nullptr);

        if (ret)
            qWarning("setVisible: Failed to commit atomic request (code=%d)", ret);

        drmModeAtomicFree(request);
#endif // QT_CONFIG(drm_atomic)
    }

    foreach(EglFSStarfishWindow *w, m_windows) {
        QEvent ev(QEvent::Type(QEvent::User + (visible ? 1 : 2)));
        QGuiApplication::sendEvent(w->window(), &ev);
    }
}

void EglFSStarfishIntegration::updateScreenVisibleDirectly(EglFSStarfishScreen *screen, bool visible, const QString& policy)
{
    if (!screen) {
        qWarning() << "[QPA:EGLI] failed to update screen visibility (null current screen)";
        return;
    }

    qInfo() << "[QPA:EGLI] update_visible:" << screen->name() << "," << policy << "," << visible;

    // update policy values for current screen
    screen->setVisiblePolicyValue(policy, visible);

    if (visible) {
        // apply exclusive policy (only one fb should be visible at a time)
        if (policy == "application") {
            foreach (EglFSStarfishScreen *s, m_screens) {
                if (s == screen) continue;
                qInfo() << "[QPA:EGLI] exclusive_policy:make_off:" << s->name();
                s->setVisiblePolicyValue("application", false);
            }
        }
    }

    // check exception cases
    int application_visible_count = 0;
    foreach (EglFSStarfishScreen *s, m_screens) {
        if (s->visibleByPolicy("application")) {
            if (application_visible_count == INT_MAX) {
                qWarning() << "Cannot increase application_visible_count greater than " << INT_MAX;
                continue;
            }
            application_visible_count++;
        }
    }
    // case1: ensure exclusive policy if something goes wrong
    if (application_visible_count > 1) {
        foreach (EglFSStarfishScreen *s, m_screens) {
            qWarning() << "[QPA:EGLI] exclusive_policy:force_mode:" << s->name() << ":" << s->primary();
            s->setVisiblePolicyValue("application", s->primary());
        }
    // case2: ensure primary fb should be true, when all fbs are false
    } else if (application_visible_count == 0) {
        foreach (EglFSStarfishScreen *s, m_screens) {
            if (s->primary()) {
                qInfo() << "[QPA:EGLI] default_policy:" << s->name() << ":" << true;
                s->setVisiblePolicyValue("application", true);
                break;
            }
        }
    }

    // ensure turn off first
    foreach (EglFSStarfishScreen *s, m_screens) {
        if (s->visibleByPolicy()) continue;
        qInfo() << "[QPA:EGLI] (set_off)" << s->name() << ":" << false;
        s->setVisible(false);
    }

    // now turn on if there's only one
    foreach (EglFSStarfishScreen *s, m_screens) {
        if (s->visibleByPolicy() == false) continue;
        qInfo() << "[QPA:EGLI] (set_on)" << s->name() << ":" << true;
        s->setVisible(true);
    }
}

void EglFSStarfishIntegration::onPowerStateChanged(const QStarfishPowerDBridge::State& state) {
    bool visible = (state == QStarfishPowerDBridge::AlwaysReady ? false : true);
    qInfo() << "[QPA:EGLI] ) onPowerStateChanged:" << state << "," << visible;
    foreach (EglFSStarfishScreen *s, m_screens) {
        updateScreenVisibleDirectly(s, visible, QString("power.state"));
    }
}

void EglFSStarfishScreen::setVisiblePolicyValue(const QString& policy, bool visible)
{
    m_visiblePolicies.insert(policy, visible);
    foreach (auto p, m_visiblePolicies.keys()) {
        qDebug() << "[QPA:EGLS] visible_policy_list = " << name() << ":" << p << ":" << m_visiblePolicies.value(p);
    }
}

bool EglFSStarfishScreen::visibleByPolicy(const QString& policy)
{
    // AND operation (It's false if one of policy is false)
    foreach (auto p, m_visiblePolicies.keys()) {
        if (policy.isEmpty()) {
            if (m_visiblePolicies.value(p) == false) return false;
        } else {
            if (policy == p && m_visiblePolicies.value(p) == false) return false;
        }
    }
    return true;
}

void EglFSStarfishScreen::appendPlatformWindow(EglFSStarfishWindow *window)
{
    m_windows.append(window);

    QEvent ev(QEvent::Type(QEvent::User + (m_visible ? 1 : 2)));
    QGuiApplication::sendEvent(window->window(), &ev);
}

void EglFSStarfishScreen::removePlatformWindow(EglFSStarfishWindow *window)
{
    m_windows.removeAll(window);
}

bool EglFSStarfishScreen::primary() const
{
    // TODO: set once from ctor if it's good
    QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.length() < 1)
        return false;

    // assume first one is primary
    return screens.first()->handle() == this;
}
