/* @@@LICENSE
 *
 * Copyright (c) 2019-2023 LG Electronics, Inc.
 *
 * Confidential computer software. Valid license from LG required for
 * possession, use or copying. Consistent with FAR 12.211 and 12.212,
 * Commercial Computer Software, Computer Software Documentation, and
 * Technical Data for Commercial Items are licensed to the U.S. Government
 * under vendor's standard commercial license.
 *
 * LICENSE@@@ */

#include "qstarfishsnapshotoperator.h"

#include <QString>
#include <QPainter>
#include <QtGui/qwindow.h>
#include <QOpenGLContext>
#include <QOpenGLPaintDevice>
#include <private/qopenglcontext_p.h>
#include <private/qguiapplication_p.h>
#include <QImage>
#include <QThread>
#include <QScreen>
#include <QElapsedTimer>
#include <QDebug>
#include <QFile>
#include <snapshot-boot/snapshot-boot.h>
#ifdef __cplusplus
extern "C" {
#include <dile/dile_boardinfo.h>
}
#endif

#include <private/qeglfswindow_p.h>
#include <QtEglSupport/private/qeglplatformcontext_p.h>

#include "eglfsstarfishintegration.h"
#include "eglfsstarfishwindow.h"
QT_BEGIN_NAMESPACE

//NOTE: This file is from qt5-qpa-starfish. Check the difference from it.

//TODO: Change the path?
#define SNAPSHOT_IMAGE_PATH ("/usr/share/qt5-qpa-starfish/resources/images")
#define LSM_RESPAWNED_FILE "/tmp/lsm-respawned"

static QStarfishSnapshotOperator::SnapshotMode toSnapshotMode(snapshot_boot_mode_constant mode)
{
    switch (mode) {
    case SNAPSHOT_MODE_MAKING:
        return QStarfishSnapshotOperator::SnapshotMode_Making;
    case SNAPSHOT_MODE_RESUME:
        return QStarfishSnapshotOperator::SnapshotMode_Resume;
    case SNAPSHOT_MODE_COLD:
        return QStarfishSnapshotOperator::SnapshotMode_Cold;
    case MAX_SNAPSHOT_MODE:
        return QStarfishSnapshotOperator::SnapshotMode_Max;
    }
    return QStarfishSnapshotOperator::SnapshotMode_Max;
}

static BOARDINFO_DISPLAY_TYPE_T getDisplayType()
{
    static BOARDINFO_DISPLAY_TYPE_T displayType = BOARDINFO_DISPLAY_MAX;

    if (displayType != BOARDINFO_DISPLAY_MAX)
        return displayType;

    if (DILE_OK != DILE_BOARDINFO_Initialze()) {
        qWarning() << "failure in DILE_BOARDINFO_Initialze(): displayType=" << displayType;
        return displayType;
    }
    if (DILE_OK != DILE_BOARDINFO_GetDisplayType(&displayType)) {
        qWarning() << "failure in DILE_BOARDINFO_GetDisplayType(): displayType=" << displayType;
        return displayType;
    }
    return displayType;
}

static QString getSnapshotImageFilePath(QRect geometry)
{
    static BOARDINFO_DISPLAY_TYPE_T displayType = getDisplayType();
    static QString snapshotImageFile = "";

    if (QRect(0, 0, 2560, 1080) == geometry) {
        snapshotImageFile = QString("%1/%2/").arg(SNAPSHOT_IMAGE_PATH).arg("wuhd");
    } else if (QRect(0, 0, 3840, 2160) == geometry) {
        snapshotImageFile = QString("%1/%2/").arg(SNAPSHOT_IMAGE_PATH).arg("uhd");
    } else if (QRect(0, 0, 1920, 1080) == geometry) {
        snapshotImageFile = QString("%1/%2/").arg(SNAPSHOT_IMAGE_PATH).arg("fhd");
    } else if (QRect(0, 0, 5120, 2160) == geometry ||
               QRect(0, 0, 1024, 768) == geometry ||
	       QRect(0, 0, 1280, 720) == geometry) {
        snapshotImageFile = QString("%1/%2/").arg(SNAPSHOT_IMAGE_PATH).arg("hd");
    } else if (QRect(0, 0, 1366, 768) == geometry) {
        snapshotImageFile = QString("%1/%2/").arg(SNAPSHOT_IMAGE_PATH).arg("hd");
        snapshotImageFile += "1366x768_";
    } else {
        qWarning() << "current egl surface geometry is " << geometry
                   << ", we can't find out right bootlogo image.";
        snapshotImageFile = QString("%1/%2/").arg(SNAPSHOT_IMAGE_PATH).arg("hd");
    }

    snapshotImageFile += "SecondBootLogo";

    if (displayType == BOARDINFO_OLED_DISPLAY)
        snapshotImageFile += "ForOLED";
    snapshotImageFile += ".png";

    qInfo() << "determine snapshot image file path: path=" << snapshotImageFile
            << ", geometry=" << geometry
            << ", displayType=" << displayType;

    return snapshotImageFile;
}

bool isMakingSnapshot(const QStarfishSnapshotOperator::SnapshotMode snapshotMode)
{
    return snapshotMode == QStarfishSnapshotOperator::SnapshotMode_Making;
}

bool isResumeSnapshot(const QStarfishSnapshotOperator::SnapshotMode snapshotMode)
{
    return snapshotMode == QStarfishSnapshotOperator::SnapshotMode_Resume;
}

// This will works as dummy provider for eglSurface with platform context
class QStarfishSnapshotWindow : public QEglFSWindow
{
public:
    QStarfishSnapshotWindow(EglFSStarfishScreen *targetScreen)
        : QEglFSWindow(targetScreen->window()->window())
        , m_context(nullptr)
        , m_paintDevice(nullptr)
        , m_screen(targetScreen)
    {
    }

    ~QStarfishSnapshotWindow()
    {
        if (m_paintDevice)
            delete m_paintDevice;
        if (m_context)
            delete m_context;
    }

    bool makeCurrent() {
        if (openGLContext()) {
            // NOTE: Bit tricky - Before makeCurrent with the platform window,
            // make the QOpenGLContext being current with the original window.
            // QOpenGLPaintDevice will have current QOpenGLContext on construction
            // And will compare it with current context on painting with QPainter
            m_context->makeCurrent(m_screen->window()->window());
            return openGLContext()->makeCurrent(this);
        }
        return false;
    }
    void doneCurrent() { if (openGLContext()) openGLContext()->doneCurrent(); }
    void swapBuffers() { if (openGLContext()) openGLContext()->swapBuffers(this); }

    // Return platform context to makeCurrent with platform window
    // It's to avoid additional 'window create' which is not supported eglfs
    QPlatformOpenGLContext *openGLContext()
    {
        if (!m_context) {
            m_context = new QOpenGLContext;
            m_context->setFormat(m_screen->window()->format());
            m_context->setScreen(m_screen->screen());
            if (!m_context->create())
                qCritical() << "failure in snapshot QOpenGLContext creation";
        }

        return m_context->handle();
    }

    QOpenGLPaintDevice *paintDevice()
    {
        if (!m_paintDevice) {
            m_paintDevice = new QOpenGLPaintDevice(m_screen->geometry().size());
        }
        return m_paintDevice;
    }

    EGLSurface surface() const override { return m_screen->primarySurface(); }

private:
    QOpenGLContext *m_context;
    QOpenGLPaintDevice *m_paintDevice;
    EglFSStarfishScreen *m_screen;
};

class QStarfishSnapshotRenderer
{
public:
    QStarfishSnapshotRenderer(EglFSStarfishScreen *screen)
        : m_screen(screen)
        , m_snapshotMode(QStarfishSnapshotOperator::SnapshotMode_Max)
    {
    }

    ~QStarfishSnapshotRenderer()
    {
        delete m_snapshotWindow;
    }

    qint64 setSnapshotImage(const QString &path)
    {
        QElapsedTimer timer;
        timer.start();

        if (!m_snapshotImage.load(path))
            qWarning() << "failure in loading snapshot image, path=" << path;

        return timer.elapsed();
    }

    qint64 render(const QStarfishSnapshotOperator::SnapshotMode &snapshotMode)
    {
        m_snapshotMode = snapshotMode;

        qInfo() << "[second_boot_logo] QStarfishSnapshotRenderer::render" << snapshotWindow() << m_snapshotMode;

        QElapsedTimer timer;
        timer.start();

        snapshotWindow()->makeCurrent();

        m_painter.begin(snapshotWindow()->paintDevice());
        m_painter.drawImage(m_screen->geometry(),
                            m_snapshotImage,
                            QRectF(QPointF(0, 0), m_snapshotImage.size()));
        m_painter.end();
        snapshotWindow()->swapBuffers();

        snapshotWindow()->doneCurrent();

        return timer.elapsed();
    }

    qint64 clear(const QStarfishSnapshotOperator::SnapshotMode &snapshotMode)
    {
        qInfo() << "[second_boot_logo] QStarfishSnapshotRenderer::clear" << snapshotWindow() << m_snapshotMode;

        if (snapshotMode != m_snapshotMode &&
            snapshotMode != QStarfishSnapshotOperator::SnapshotMode_Resume)
            qWarning() << "before clearing, snapshotMode is chanaged: "
                       << m_snapshotMode << " -> " << snapshotMode;

        QElapsedTimer timer;
        timer.start();

        snapshotWindow()->makeCurrent();

        m_painter.begin(snapshotWindow()->paintDevice());
        m_painter.setCompositionMode(QPainter::CompositionMode_Clear);
        m_painter.eraseRect(m_screen->geometry());
        m_painter.end();
        snapshotWindow()->swapBuffers();

        snapshotWindow()->doneCurrent();

        return timer.elapsed();
    }

    QStarfishSnapshotWindow* snapshotWindow()
    {
        if (!m_snapshotWindow) {
            m_snapshotWindow = new QStarfishSnapshotWindow(m_screen);
        }
        return m_snapshotWindow;
    }

private:
    EglFSStarfishScreen *m_screen = nullptr;
    QStarfishSnapshotOperator::SnapshotMode m_snapshotMode;
    QImage m_snapshotImage;
    QPainter m_painter;
    QStarfishSnapshotWindow *m_snapshotWindow = nullptr;
};

class QStarfishSnapshotAwaiter : public QThread
{
    Q_OBJECT

public:
    void wait(const QStarfishSnapshotOperator::SnapshotMode &snapshotMode) {
        qDebug() << "wait for snapshot_boot making (" << snapshotMode
                 << "), my name is \"surface-manager\"...";

        if (!isMakingSnapshot(snapshotMode)) {
            emit fired(-1);
        } else {
            start();
        }
    }

signals:
    void fired(qint64 elapsed_ms);

protected:
    void run() override {
        QElapsedTimer timer;
        timer.start();

#if 0
        qDebug() << "...sleep  5s..";
        QThread::msleep(5000);  // for test
#else
        qInfo() << "...invoking snapshot_boot_ready()...";
        snapshot_boot_ready("surface-manager");
#endif

        emit fired(timer.elapsed());
    };
};

QStarfishSnapshotOperator::QStarfishSnapshotOperator(EglFSStarfishScreen *screen)
    : m_screen(screen)
    , m_snapshotMode(toSnapshotMode(snapshot_boot_mode()))
    , m_snapshotProgressive(SnapshotProgressive_Max)
    , m_renderer(nullptr)
    , m_awaiter(new QStarfishSnapshotAwaiter)
{
    qInfo() << "[snapshot_boot]" << "QStarfishSnapshotOperator" << "mode" << m_snapshotMode << snapshot_boot_mode();
    connect(m_awaiter, SIGNAL(fired(qint64)), this, SLOT(done(qint64)));
}

QStarfishSnapshotOperator::~QStarfishSnapshotOperator()
{
    delete m_awaiter;
    if (m_renderer)
        delete m_renderer;
}

void QStarfishSnapshotOperator::execute()
{
    memset(&m_profiling, 0x00, sizeof(struct SnapshotProfiling));


    qInfo() << "[snapshot_boot]" << "QStarfishSnapshotOperator::execute" << m_screen->primary();
    /* NOTE
     currently, only primary screen supports snapshot operation.
     */
    if (!m_screen->primary()) {
        done();
        return;
    }

    if (QFile::exists(LSM_RESPAWNED_FILE)) {
        done();
        return;
    }

    if (isMakingSnapshot(m_snapshotMode)) {
        qInfo() << "try to render" << snapshotRenderer();
        m_profiling.set_elapsed_ms = snapshotRenderer()->setSnapshotImage(getSnapshotImageFilePath(m_screen->geometry()));
        m_profiling.render_elapsed_ms = snapshotRenderer()->render(m_snapshotMode);
    }

    waitForDone();
}

void QStarfishSnapshotOperator::done(qint64 elapsed_ms)
{
    qDebug() << "...complete of snapshot_boot making, my name is \"surface-manager\"";

    m_profiling.wait_elapsed_ms = elapsed_ms;
    if (isMakingSnapshot(m_snapshotMode) || isResumeSnapshot(m_snapshotMode)) {
        m_profiling.clear_elapsed_ms = snapshotRenderer()->clear(m_snapshotMode);
    }
    m_snapshotProgressive = SnapshotProgressive_Done;

    qDebug() << "snapshot profiling: set=" << m_profiling.set_elapsed_ms
             << "ms, render=" << m_profiling.render_elapsed_ms
             << "ms, wait=" << m_profiling.wait_elapsed_ms
             << "ms, clear=" << m_profiling.clear_elapsed_ms
             << "ms";

    m_screen->snapshotDone();
}

void QStarfishSnapshotOperator::waitForDone()
{
    m_snapshotProgressive = SnapshotProgressive_Waiting;
    m_awaiter->wait(m_snapshotMode);
}

bool QStarfishSnapshotOperator::isDone() const
{
    return m_snapshotProgressive == SnapshotProgressive_Done;
}

QStarfishSnapshotRenderer *QStarfishSnapshotOperator::snapshotRenderer()
{
    if (!m_renderer) {
        m_renderer = new QStarfishSnapshotRenderer(m_screen);
    }
    return m_renderer;
}

QT_END_NAMESPACE

#include "qstarfishsnapshotoperator.moc"
