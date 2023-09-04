/* @@@LICENSE
 *
 * Copyright (c) 2023 LG Electronics, Inc.
 *
 * Confidential computer software. Valid license from LG required for
 * possession, use or copying. Consistent with FAR 12.211 and 12.212,
 * Commercial Computer Software, Computer Software Documentation, and
 * Technical Data for Commercial Items are licensed to the U.S. Government
 * under vendor's standard commercial license.
 *
 * LICENSE@@@ */

#ifndef QSTARFISHSNAPSHOTOPERATOR_H
#define QSTARFISHSNAPSHOTOPERATOR_H

#include <QObject>

QT_BEGIN_NAMESPACE

class EglFSStarfishScreen;
class QStarfishSnapshotRenderer;
class QStarfishSnapshotAwaiter;

class QStarfishSnapshotOperator : public QObject
{
    Q_OBJECT

public:
    enum SnapshotMode {
        SnapshotMode_Making,
        SnapshotMode_Resume,
        SnapshotMode_Cold,
        SnapshotMode_Max,
    };
    Q_FLAGS(SnapshotMode);

    enum SnapshotProgressive {
        SnapshotProgressive_Waiting,
        SnapshotProgressive_Done,
        SnapshotProgressive_Max,
    };
    Q_FLAGS(SnapshotProgressive);

    struct SnapshotProfiling {
        qint64 set_elapsed_ms;
        qint64 render_elapsed_ms;
        qint64 wait_elapsed_ms;
        qint64 clear_elapsed_ms;
    };

public:
    QStarfishSnapshotOperator(EglFSStarfishScreen *screen);
    ~QStarfishSnapshotOperator();

    SnapshotMode snapshotMode() const { return m_snapshotMode; }
    SnapshotProgressive snapshotProgressive() const { return m_snapshotProgressive; }
    SnapshotProfiling snapshotProfiling() const { return m_profiling; }

    void execute();
    void waitForDone();
    //bool isOwner(const QString& windowName);
    bool isDone() const;

public slots:
    void done(qint64 elapsed_ms = -1);

private:
    QStarfishSnapshotRenderer *snapshotRenderer();

private:
    EglFSStarfishScreen *m_screen;
    SnapshotMode m_snapshotMode;
    SnapshotProgressive m_snapshotProgressive;
    SnapshotProfiling m_profiling;
    QStarfishSnapshotRenderer *m_renderer;
    QStarfishSnapshotAwaiter *m_awaiter;
};

QT_END_NAMESPACE
#endif // QSTARFISHSNAPSHOTOPERATOR_H
