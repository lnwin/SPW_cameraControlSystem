/**
 * qt_worker.cpp  --  SdkQtWorker + SdkQtApp implementation
 */

#include "qt_worker.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

/* ============================================================ SdkQtApp === */

namespace SdkQtApp {

static std::mutex               g_mtx;          /* protects everything below  */
static int                      g_refcount = 0; /* live contexts             */
static bool                     g_owns     = false;
static std::thread              g_app_thread;
static std::condition_variable  g_ready_cv;
static bool                     g_ready    = false;

void acquire()
{
    std::unique_lock<std::mutex> lk(g_mtx);
    ++g_refcount;
    if (g_refcount > 1) return;                 /* already running           */

    if (QCoreApplication::instance()) {         /* host owns the app         */
        g_owns = false;
        return;
    }

    g_owns  = true;
    g_ready = false;
    g_app_thread = std::thread([](){
        static int    argc = 1;
        static const char* name = "spwcam_sdk";
        static char*  argv[] = { const_cast<char*>(name), nullptr };

        QCoreApplication app(argc, argv);
        app.setApplicationName("spwcam_sdk");
        {
            std::lock_guard<std::mutex> lk2(g_mtx);
            g_ready = true;
        }
        g_ready_cv.notify_all();
        app.exec();     /* blocks until QCoreApplication::quit() */
    });

    g_ready_cv.wait(lk, []{ return g_ready; });
}

void release()
{
    std::unique_lock<std::mutex> lk(g_mtx);
    if (g_refcount == 0) return;
    if (--g_refcount > 0) return;               /* other contexts still alive */
    if (!g_owns) return;
    g_owns = false;

    if (QCoreApplication::instance())
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  "quit", Qt::QueuedConnection);
    if (g_app_thread.joinable())
        g_app_thread.join();
}

bool runOnQtThread(std::function<void()> fn)
{
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) return false;

    if (QThread::currentThread() == app->thread()) {
        fn();   /* already on Qt thread (e.g. inside a callback) — call directly */
        return true;
    }

    QMetaObject::invokeMethod(app, [&fn](){ fn(); }, Qt::BlockingQueuedConnection);
    return true;
}

}  /* namespace SdkQtApp */

/* ========================================================= SdkQtWorker === */

SdkQtWorker::SdkQtWorker(SdkContext* ctx, QObject* parent)
    : QObject(parent)
    , ctx_(ctx)
{
    /* --- VideoRecorder lives on its own thread for FFmpeg encoding --- */
    recThread_ = new QThread(this);
    recThread_->setObjectName("spwcam_rec");
    recorder_  = new VideoRecorder();
    recorder_->moveToThread(recThread_);
    recThread_->start();

    /* --- RTSP viewer (is itself a QThread subclass) --- */
    rtsp_ = new RtspViewerQt(this);

    /* --- UDP device manager --- */
    udp_ = new UdpDeviceManager(this);

    /* --- frame pump timer: polls the RTSP viewer for the latest frame.
           16 ms + PreciseTimer so that a 25 fps (40 ms) source is never
           under-sampled by Windows' ~15.6 ms default timer granularity;
           the pump returns immediately when no new frame is available.  --- */
    framePump_ = new QTimer(this);
    framePump_->setTimerType(Qt::PreciseTimer);
    framePump_->setInterval(16);

    /* --- watchdog: stream-lost / device-lost detection --- */
    watchdog_ = new QTimer(this);
    watchdog_->setInterval(1000);

    /* --- set_ip timeout --- */
    ipTimer_ = new QTimer(this);
    ipTimer_->setSingleShot(true);

    connectSignals();
    watchdog_->start();
}

SdkQtWorker::~SdkQtWorker()
{
    watchdog_->stop();
    ipTimer_->stop();

    /* 1. stop frames first so nothing is fed to the recorder any more */
    doStreamClose();

    /* 2. finalise the recording file synchronously (writes MP4 moov atom) */
    doRecordStopBlocking();

    /* 3. stop recorder thread, then destroy recorder on this (Qt) thread —
          its thread no longer runs, so a direct delete is safe.          */
    if (recThread_) {
        recThread_->quit();
        recThread_->wait(5000);
    }
    delete recorder_;
    recorder_ = nullptr;

    doDiscoveryStop();
}

void SdkQtWorker::connectSignals()
{
    /* UDP signals */
    connect(udp_, &UdpDeviceManager::snDiscoveredOrUpdated,
            this, &SdkQtWorker::onDeviceDiscovered);
    connect(udp_, &UdpDeviceManager::datagramReceived,
            this, &SdkQtWorker::onDatagram);
    connect(udp_, &UdpDeviceManager::setIpAckReceived,
            this, &SdkQtWorker::onSetIpAck);
    connect(udp_, &UdpDeviceManager::logLine,
            this, &SdkQtWorker::onLogLine);

    /* RTSP log */
    connect(rtsp_, &RtspViewerQt::logLine,
            this, &SdkQtWorker::onLogLine);

    /* Recorder signals (cross-thread) */
    connect(recorder_, &VideoRecorder::recordingStarted,
            this, &SdkQtWorker::onRecordingStarted, Qt::QueuedConnection);
    connect(recorder_, &VideoRecorder::recordingStopped,
            this, &SdkQtWorker::onRecordingStopped, Qt::QueuedConnection);
    connect(recorder_, &VideoRecorder::recordingFailed,
            this, &SdkQtWorker::onRecordingFailed, Qt::QueuedConnection);
    connect(recorder_, &VideoRecorder::snapshotSaved,
            this, &SdkQtWorker::onSnapshotSaved, Qt::QueuedConnection);
    connect(recorder_, &VideoRecorder::sendMSG2ui,
            this, &SdkQtWorker::onLogLine, Qt::QueuedConnection);

    connect(framePump_, &QTimer::timeout, this, &SdkQtWorker::pumpFrame);
    connect(watchdog_,  &QTimer::timeout, this, &SdkQtWorker::watchdogTick);
    connect(ipTimer_,   &QTimer::timeout, this, &SdkQtWorker::onIpChangeTimeout);
}

/* -------------- frame pump ------------------------------------------------ */

void SdkQtWorker::pumpFrame()
{
    if (!rtsp_ || !streamOpen_) return;

    QSharedPointer<QImage> img = rtsp_->takeLatestFrameIfNew();
    if (!img || img->isNull()) return;

    lastFrame_   = img;
    lastFrameMs_ = QDateTime::currentMSecsSinceEpoch();

    deliverFrame(*img);

    /* feed recorder while a recording is requested; the recorder opens its
       encoder lazily on the first frame and then emits recordingStarted.  */
    if (recordRequested_ && recorder_) {
        QMetaObject::invokeMethod(recorder_, "receiveFrame2Record",
                                  Qt::QueuedConnection,
                                  Q_ARG(QSharedPointer<QImage>, img));
    }
}

void SdkQtWorker::deliverFrame(const QImage& src)
{
    /* Frames from RtspViewerQt are already ARGB32 (== BGRA in memory);
       convertToFormat() is a cheap shallow copy in that case.            */
    const QImage bgra = (src.format() == QImage::Format_ARGB32)
                        ? src
                        : src.convertToFormat(QImage::Format_ARGB32);

    const int w  = bgra.width();
    const int h  = bgra.height();
    const int st = bgra.bytesPerLine();
    const int64_t ts = sdk_now_ms();
    const uint64_t seq = ++frameSeq_;

    /* Update grab slot (pull mode) */
    {
        std::lock_guard<std::mutex> lk(ctx_->grab.lock);
        ctx_->grab.data.resize(static_cast<size_t>(st) * h);
        std::memcpy(ctx_->grab.data.data(), bgra.constBits(),
                    ctx_->grab.data.size());
        ctx_->grab.width  = static_cast<uint32_t>(w);
        ctx_->grab.height = static_cast<uint32_t>(h);
        ctx_->grab.stride = static_cast<uint32_t>(st);
        ctx_->grab.ts_ms  = ts;
        ctx_->grab.seq    = seq;
        ctx_->grab.fresh  = true;
    }

    /* Stream status: any state other than RUNNING -> RUNNING + CONNECTED event */
    const int prev = ctx_->stream_status.exchange(SPWCAM_STREAM_RUNNING);
    if (prev != SPWCAM_STREAM_RUNNING) {
        everConnected_ = true;
        sdk_log(ctx_, SPWCAM_LOG_INFO, "[SDK] stream connected");
        sdk_event(ctx_, SPWCAM_EVENT_STREAM_CONNECTED, nullptr);
    }

    /* Push-mode callback (lock released before the call) */
    spwcam_frame_callback_t cb  = nullptr;
    void*                   ud  = nullptr;
    {
        std::lock_guard<std::mutex> lk(ctx_->cbs.lock);
        cb = ctx_->cbs.frame_cb;
        ud = ctx_->cbs.frame_ud;
    }
    if (cb) {
        spwcam_frame_t f{};
        f.struct_size   = static_cast<uint32_t>(sizeof(f));
        f.data          = const_cast<uint8_t*>(bgra.constBits());
        f.width         = static_cast<uint32_t>(w);
        f.height        = static_cast<uint32_t>(h);
        f.stride        = static_cast<uint32_t>(st);
        f.pixel_format  = SPWCAM_PIXEL_BGRA32;
        f.timestamp_ms  = ts;
        f.sequence      = seq;
        cb(&f, ud);
    }
}

void SdkQtWorker::markStreamLost()
{
    int expected = SPWCAM_STREAM_RUNNING;
    if (ctx_->stream_status.compare_exchange_strong(expected, SPWCAM_STREAM_ERROR)) {
        sdk_log(ctx_, SPWCAM_LOG_WARN, "[SDK] stream lost (no frames for 5 s), reconnecting");
        sdk_event(ctx_, SPWCAM_EVENT_STREAM_LOST, nullptr);
    }
}

/* -------------- watchdog -------------------------------------------------- */

void SdkQtWorker::watchdogTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    /* stream lost detection */
    if (streamOpen_ &&
        ctx_->stream_status.load() == SPWCAM_STREAM_RUNNING &&
        lastFrameMs_ > 0 && now - lastFrameMs_ > kStreamLostMs)
    {
        markStreamLost();
    }

    /* device lost detection */
    if (discoveryOn_ && udp_) {
        const QList<QString> sns = udp_->allSns();
        QSet<QString> stillOnline;
        for (const QString& sn : sns) {
            DeviceInfo d;
            if (!udp_->getDevice(sn, d)) continue;
            if (now - d.lastSeenMs <= kDeviceLostMs) stillOnline.insert(sn);
        }
        for (const QString& sn : onlineSns_) {
            if (!stillOnline.contains(sn)) {
                sdk_log(ctx_, SPWCAM_LOG_WARN, "[SDK] device lost: " + sn.toStdString());
                sdk_event(ctx_, SPWCAM_EVENT_DEVICE_LOST, sn.toUtf8().constData());
            }
        }
        onlineSns_ = stillOnline;
    }
}

/* -------------- discovery ------------------------------------------------- */

bool SdkQtWorker::doDiscoveryStart(quint16 dp, quint16 hb, quint16 cmd)
{
    udp_->setDefaultCmdPort(cmd ? cmd : ctx_->init_cmd_port);
    discoveryOn_ = udp_->start(dp ? dp : ctx_->init_discover_port,
                               hb ? hb : ctx_->init_heartbeat_port);
    return discoveryOn_;
}

void SdkQtWorker::doDiscoveryStop()
{
    if (udp_) udp_->stop();
    discoveryOn_ = false;
    onlineSns_.clear();
}

int SdkQtWorker::queryDeviceCount()
{
    return udp_->allSns().size();
}

QStringList SdkQtWorker::queryDeviceSnList()
{
    return QStringList(udp_->allSns());
}

bool SdkQtWorker::queryDeviceInfo(const QString& sn,
                                   QString& outIp,
                                   quint16& outRtspPort,
                                   QString& outRtspPath,
                                   qint64&  outLastSeenMs)
{
    DeviceInfo d;
    if (!udp_->getDevice(sn, d)) return false;
    outIp        = d.ip.toString();
    outRtspPort  = d.rtspPort;
    outRtspPath  = d.rtspPath;
    outLastSeenMs= d.lastSeenMs;
    return true;
}

/* -------------- stream ---------------------------------------------------- */

void SdkQtWorker::doStreamOpen(const QString& url, int latencyMs)
{
    if (streamOpen_) {
        framePump_->stop();
        rtsp_->stop();
        rtsp_->wait(3000);
    }
    rtsp_->setUrl(url);
    rtsp_->setLatencyMs(latencyMs > 0 ? latencyMs : 0);
    lastFrame_.reset();
    lastFrameMs_   = 0;
    everConnected_ = false;
    ctx_->stream_status.store(SPWCAM_STREAM_CONNECTING);
    rtsp_->start();          /* QThread::start() */
    streamOpen_ = true;
    framePump_->start();
}

void SdkQtWorker::doStreamClose()
{
    if (framePump_) framePump_->stop();
    if (streamOpen_ && rtsp_) {
        rtsp_->stop();
        rtsp_->wait(3000);
        streamOpen_ = false;
    }
    lastFrame_.reset();
    lastFrameMs_ = 0;
    ctx_->stream_status.store(SPWCAM_STREAM_STOPPED);
}

/* -------------- device control -------------------------------------------- */

void SdkQtWorker::doSetLed(const QString& sn, bool enable)
{
    udp_->sendSetLed(sn, enable);
}

void SdkQtWorker::doSetTrigger(const QString& sn, const QString& mode)
{
    udp_->sendSetTrigger(sn, mode);
}

void SdkQtWorker::doSetCameraParams(const QString& sn, int exposureUs, double gainDb)
{
    udp_->sendSetCameraParams(sn, exposureUs, gainDb);
}

void SdkQtWorker::doSetIp(const QString& sn, const QString& ip, int mask)
{
    ipPendingSn_    = sn;
    ipPendingNewIp_ = ip;
    ipAckAccepted_  = false;
    ipTimer_->start(kIpNoAckMs);
    udp_->sendSetIp(sn, ip, mask);
}

void SdkQtWorker::onSetIpAck(const QString& sn, const QString& status)
{
    if (ipPendingSn_.isEmpty() || sn != ipPendingSn_) return;

    if (status == QLatin1String("accepted") || status == QLatin1String("applying")) {
        ipAckAccepted_ = true;
        ipTimer_->start(kIpReconnectMs);   /* device reboots its network */
    } else if (status == QLatin1String("success")) {
        ipTimer_->stop();
        const QByteArray d = "ok:" + ipPendingNewIp_.toUtf8();
        ipPendingSn_.clear();
        sdk_event(ctx_, SPWCAM_EVENT_IP_CHANGED, d.constData());
    } else if (status == QLatin1String("failed")) {
        ipTimer_->stop();
        ipPendingSn_.clear();
        sdk_event(ctx_, SPWCAM_EVENT_IP_CHANGED, "failed:device_rejected");
    }
}

void SdkQtWorker::onIpChangeTimeout()
{
    if (ipPendingSn_.isEmpty()) return;
    const char* reason = ipAckAccepted_ ? "failed:reconnect_timeout" : "failed:no_ack";
    ipPendingSn_.clear();
    sdk_log(ctx_, SPWCAM_LOG_ERROR, std::string("[SDK] set_ip ") + reason);
    sdk_event(ctx_, SPWCAM_EVENT_IP_CHANGED, reason);
}

/* -------------- recording ------------------------------------------------- */

void SdkQtWorker::doRecordSetOptions(const myRecordOptions& opts,
                                      int fps, int bitrateKbps)
{
    QMetaObject::invokeMethod(recorder_, "receiveRecordOptions",
                              Qt::QueuedConnection,
                              Q_ARG(myRecordOptions, opts));
    /* The upstream encoder currently runs at fixed 25 fps / 8000 kbps /
       30-minute segments; other values are accepted but not applied.    */
    if (fps != 25 || bitrateKbps != 8000)
        sdk_log(ctx_, SPWCAM_LOG_WARN,
                "[SDK] record options: fps/bitrate_kbps are fixed at 25/8000 in this version; requested values ignored");
}

void SdkQtWorker::doRecordStart()
{
    recordRequested_ = true;
    QMetaObject::invokeMethod(recorder_, "startRecording", Qt::QueuedConnection);
}

void SdkQtWorker::doRecordStop()
{
    recordRequested_ = false;
    QMetaObject::invokeMethod(recorder_, "stopRecording", Qt::QueuedConnection);
}

void SdkQtWorker::doRecordStopBlocking()
{
    recordRequested_ = false;
    if (!recorder_ || !recThread_ || !recThread_->isRunning()) return;
    QMetaObject::invokeMethod(recorder_, "stopRecording", Qt::BlockingQueuedConnection);
}

bool SdkQtWorker::doSnapshot()
{
    if (!streamOpen_ || !lastFrame_ || lastFrame_->isNull()) return false;
    QMetaObject::invokeMethod(recorder_, "receiveFrame2Save",
                              Qt::QueuedConnection,
                              Q_ARG(QSharedPointer<QImage>, lastFrame_));
    return true;
}

/* -------------- slot callbacks -------------------------------------------- */

void SdkQtWorker::onDeviceDiscovered(const QString& sn)
{
    const bool isNew = !onlineSns_.contains(sn);
    if (isNew) onlineSns_.insert(sn);

    /* pending IP change: device shows up again with the new IP -> success */
    if (!ipPendingSn_.isEmpty() && sn == ipPendingSn_) {
        DeviceInfo d;
        if (udp_->getDevice(sn, d) && d.ip.toString() == ipPendingNewIp_) {
            ipTimer_->stop();
            ipPendingSn_.clear();
            const QByteArray data = "ok:" + ipPendingNewIp_.toUtf8();
            sdk_log(ctx_, SPWCAM_LOG_INFO, "[SDK] set_ip success: " + ipPendingNewIp_.toStdString());
            sdk_event(ctx_, SPWCAM_EVENT_IP_CHANGED, data.constData());
        }
    }

    if (isNew) {
        sdk_log(ctx_, SPWCAM_LOG_INFO, "[SDK] device discovered: " + sn.toStdString());
        sdk_event(ctx_, SPWCAM_EVENT_DEVICE_DISCOVERED, sn.toUtf8().constData());
    }
}

void SdkQtWorker::onDatagram(const QString& sn, const QHostAddress&, quint16,
                             const QByteArray& payload)
{
    if (!payload.trimmed().startsWith('{')) return;
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return;
    const QJsonObject json = doc.object();
    if (json.value("cmd").toString() != QLatin1String("TRIGGER_STATUS")) return;

    const bool fallback = json.value("fallback").toBool();
    const QString cur   = json.value("current_mode").toString();
    const QByteArray data = fallback ? QByteArray("fallback") : cur.toUtf8();

    sdk_log(ctx_, fallback ? SPWCAM_LOG_WARN : SPWCAM_LOG_INFO,
            "[SDK] trigger status sn=" + sn.toStdString() + " " + data.toStdString());
    sdk_event(ctx_, SPWCAM_EVENT_TRIGGER_STATUS, data.constData());
}

void SdkQtWorker::onLogLine(const QString& line)
{
    sdk_log(ctx_, SPWCAM_LOG_DEBUG, line.toStdString());
}

void SdkQtWorker::onRecordingStarted(const QString& path)
{
    bool isSegment = false;
    {
        std::lock_guard<std::mutex> lk(ctx_->rec.lock);
        isSegment = ctx_->rec.segment_index > 0;   /* set by onRecordingStopped on split */
        ctx_->rec.recording        = true;
        ctx_->rec.current_file     = path.toStdString();
        ctx_->rec.segment_start_ms = sdk_now_ms();
        if (!isSegment) ctx_->rec.session_start_ms = ctx_->rec.segment_start_ms;
    }
    const QByteArray p = path.toUtf8();
    if (isSegment) {
        sdk_log(ctx_, SPWCAM_LOG_INFO, "[SDK] recording new segment: " + path.toStdString());
        sdk_event(ctx_, SPWCAM_EVENT_RECORD_SEGMENT, p.constData());
    } else {
        sdk_log(ctx_, SPWCAM_LOG_INFO, "[SDK] recording started: " + path.toStdString());
        sdk_event(ctx_, SPWCAM_EVENT_RECORD_STARTED, p.constData());
    }
}

void SdkQtWorker::onRecordingStopped(const QString& path)
{
    /* Emitted both on user stop and on automatic 30-minute segment split.
       If a recording is still requested this is a split: count the segment
       and do NOT report RECORD_STOPPED (RECORD_SEGMENT follows).           */
    const bool split = recordRequested_;
    {
        std::lock_guard<std::mutex> lk(ctx_->rec.lock);
        if (split) {
            ctx_->rec.segment_index++;
        } else {
            ctx_->rec.recording = false;
            ctx_->rec.current_file.clear();
        }
    }
    if (!split) {
        sdk_log(ctx_, SPWCAM_LOG_INFO, "[SDK] recording stopped: " + path.toStdString());
        sdk_event(ctx_, SPWCAM_EVENT_RECORD_STOPPED, path.toUtf8().constData());
    }
}

void SdkQtWorker::onRecordingFailed(const QString& reason)
{
    recordRequested_ = false;
    {
        std::lock_guard<std::mutex> lk(ctx_->rec.lock);
        ctx_->rec.recording = false;
        ctx_->rec.current_file.clear();
    }
    sdk_log(ctx_, SPWCAM_LOG_ERROR, "[SDK] recording failed: " + reason.toStdString());
    sdk_event(ctx_, SPWCAM_EVENT_RECORD_FAILED, reason.toUtf8().constData());
}

void SdkQtWorker::onSnapshotSaved(const QString& path)
{
    sdk_log(ctx_, SPWCAM_LOG_INFO, "[SDK] snapshot saved: " + path.toStdString());
    sdk_event(ctx_, SPWCAM_EVENT_SNAPSHOT_SAVED, path.toUtf8().constData());
}

/* AUTOMOC ON — CMake generates moc_qt_worker.cpp automatically from qt_worker.h */
