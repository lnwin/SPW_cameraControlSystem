/**
 * qt_worker.h  --  Qt-thread worker that owns all QObject components
 *
 * One SdkQtWorker instance lives on the Qt event-loop thread.
 * All calls from the C API go through SdkQtApp::runOnQtThread() so they are
 * serialised onto that thread.
 */
#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QString>
#include <QSharedPointer>
#include <QImage>
#include <QStringList>
#include <QHostAddress>
#include <QSet>
#include <QElapsedTimer>

#include "udpserver.h"
#include "rtspviewerqt.h"
#include "videorecorder.h"
#include "myStruct.h"
#include "spwcam_internal.h"

/* ======================================================== SdkQtWorker === */

class SdkQtWorker : public QObject
{
    Q_OBJECT

public:
    explicit SdkQtWorker(SdkContext* ctx, QObject* parent = nullptr);
    ~SdkQtWorker() override;

    /* ---------- called from Qt thread only (via runOnQtThread) ---------- */
    bool doDiscoveryStart(quint16 discPort, quint16 hbPort, quint16 cmdPort);
    void doDiscoveryStop();
    void doStreamOpen(const QString& url, int latencyMs);
    void doStreamClose();
    void doSetLed(const QString& sn, bool enable);
    void doSetTrigger(const QString& sn, const QString& mode);
    void doSetCameraParams(const QString& sn, int exposureUs, double gainDb);
    void doSetIp(const QString& sn, const QString& ip, int mask);
    void doRecordSetOptions(const myRecordOptions& opts, int fps, int bitrateKbps);
    void doRecordStart();
    void doRecordStop();          /* asynchronous request                      */
    void doRecordStopBlocking();  /* waits until the recorder has closed file  */
    bool doSnapshot();            /* false = no frame cached yet               */

    /* ---------- read-only queries ---------------------------------------- */
    int         queryDeviceCount();
    QStringList queryDeviceSnList();
    bool        queryDeviceInfo(const QString& sn,
                                QString& outIp,
                                quint16& outRtspPort,
                                QString& outRtspPath,
                                qint64&  outLastSeenMs);

private slots:
    void pumpFrame();
    void watchdogTick();
    void onDeviceDiscovered(const QString& sn);
    void onDatagram(const QString& sn, const QHostAddress& ip, quint16 port, const QByteArray& payload);
    void onSetIpAck(const QString& sn, const QString& status);
    void onIpChangeTimeout();
    void onLogLine(const QString& line);
    void onRecordingStarted(const QString& path);
    void onRecordingStopped(const QString& path);
    void onRecordingFailed(const QString& reason);
    void onSnapshotSaved(const QString& path);

private:
    void connectSignals();
    void deliverFrame(const QImage& img);
    void markStreamLost();

    SdkContext*        ctx_       = nullptr;

    UdpDeviceManager*  udp_       = nullptr;
    RtspViewerQt*      rtsp_      = nullptr;
    VideoRecorder*     recorder_  = nullptr;

    QThread*           recThread_ = nullptr;   /* recorder lives on this thread */

    QTimer*            framePump_ = nullptr;   /* 16 ms precise poll (source is 25 fps) */
    QTimer*            watchdog_  = nullptr;   /* 1 s: stream-lost / device-lost  */
    QTimer*            ipTimer_   = nullptr;   /* single-shot: set_ip timeout     */

    uint64_t           frameSeq_  = 0;
    bool               streamOpen_= false;
    bool               discoveryOn_ = false;

    /* recording: the user has requested recording; frames are fed to the
       recorder while this is true, regardless of whether the encoder has
       been opened yet (the encoder opens lazily on the first frame).       */
    bool               recordRequested_ = false;

    /* most recent frame handed to the pump; used by snapshot so that it never
       competes with the frame pump for the RTSP "new frame" flag.           */
    QSharedPointer<QImage> lastFrame_;
    qint64             lastFrameMs_ = 0;       /* monotonic ms of last frame      */
    bool               everConnected_ = false;

    /* device presence tracking for SPWCAM_EVENT_DEVICE_LOST */
    QSet<QString>      onlineSns_;

    /* pending IP change (spwcam_set_ip) */
    QString            ipPendingSn_;
    QString            ipPendingNewIp_;
    bool               ipAckAccepted_ = false;

    static constexpr qint64 kStreamLostMs   = 5000;
    static constexpr qint64 kDeviceLostMs   = 10000;
    static constexpr int    kIpNoAckMs      = 15000;
    static constexpr int    kIpReconnectMs  = 20000;
};

/* ======================================================= Qt app helper === */

namespace SdkQtApp {

/**
 * Acquire a reference to the process-wide QCoreApplication.
 * If the host already owns a QCoreApplication, nothing is created.
 * Otherwise the first caller creates one on a background std::thread and
 * blocks until its event loop is running.  Reference-counted: the
 * application is only torn down when the last context calls release().
 */
void acquire();
void release();

/** Post a functor to the Qt event loop and block until it completes.
 *  Returns false if no QCoreApplication exists (functor not run). */
bool runOnQtThread(std::function<void()> fn);

}  /* namespace SdkQtApp */
