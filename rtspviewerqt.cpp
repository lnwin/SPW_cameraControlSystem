// rtspviewerqt.cpp  (FULL REPLACEABLE FILE)
// Windows/Qt + GStreamer RTSP client (TCP) low-latency, good quality, stable reconnect.
//
// Key fixes:
// - HW decode path (D3D11) MUST include d3d11download before appsink, otherwise not-linked.
// - Avoid decodebin dynamic pad linking issues by using explicit rtph264depay ! h264parse ! decoder.
// - Drop only AFTER decode (appsink drop + post-decode leaky) to keep quality.
// - Coalesce frames in worker thread + emit at 22Hz to prevent Qt backlog.
// - Safer stop to avoid "QThread destroyed while still running".

#include "rtspviewerqt.h"

#include <QThread>
#include <QSharedPointer>
#include <QImage>
#include <QElapsedTimer>

#include <algorithm>
#include <mutex>
#include <functional>

extern "C" {
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
}

static void gst_init_once()
{
    static std::once_flag f;
    std::call_once(f, []{
        gst_init(nullptr, nullptr);
    });
}

static inline QString qstr(const char* s){ return QString::fromUtf8(s ? s : ""); }

static QString capsToString(GstCaps* caps)
{
    if (!caps) return {};
    gchar* s = gst_caps_to_string(caps);
    QString out = qstr(s);
    if (s) g_free(s);
    return out;
}

// Check if an element factory exists (plugin installed)
static bool hasFactory(const char* name)
{
    GstElementFactory* f = gst_element_factory_find(name);
    if (f) {
        gst_object_unref(f);
        return true;
    }
    return false;
}

// bus：打印日志；遇到 ERROR/EOS 触发重连
static void pump_bus(GstElement* pipeline,
                     std::function<void(const QString&)> logFn,
                     bool* needReconnect)
{
    GstBus* bus = gst_element_get_bus(pipeline);
    while (gst_bus_have_pending(bus)) {
        GstMessage* msg = gst_bus_pop(bus);
        if (!msg) break;

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            logFn(QString("[GST][ERR] %1 | %2")
                      .arg(qstr(err ? err->message : ""))
                      .arg(qstr(dbg)));
            if (err) g_error_free(err);
            if (dbg) g_free(dbg);
            if (needReconnect) *needReconnect = true;
        } break;
        case GST_MESSAGE_EOS: {
            logFn("[GST][EOS] end of stream");
            if (needReconnect) *needReconnect = true;
        } break;
        case GST_MESSAGE_WARNING: {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_warning(msg, &err, &dbg);
            logFn(QString("[GST][WRN] %1 | %2")
                      .arg(qstr(err ? err->message : ""))
                      .arg(qstr(dbg)));
            if (err) g_error_free(err);
            if (dbg) g_free(dbg);
        } break;
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
                GstState oldS, newS, pend;
                gst_message_parse_state_changed(msg, &oldS, &newS, &pend);
                logFn(QString("[GST] state: %1 -> %2")
                          .arg(gst_element_state_get_name(oldS))
                          .arg(gst_element_state_get_name(newS)));
            }
        } break;
        default:
            break;
        }

        gst_message_unref(msg);
    }
    gst_object_unref(bus);
}

RtspViewerQt::RtspViewerQt(QObject* parent)
    : QThread(parent)
{
    gst_init_once();
    qRegisterMetaType<QSharedPointer<QImage>>("QSharedPointer<QImage>");
}

RtspViewerQt::~RtspViewerQt()
{
    stop();
    wait(1500);
}

void RtspViewerQt::setUrl(const QString& url)
{
    url_ = url;
}

void RtspViewerQt::stop()
{
    stopFlag_.store(true, std::memory_order_release);
    requestInterruption(); // ensure Qt thread interruption flag
}

static QString build_hw_d3d11_pipeline(const QString& url, int latency)
{
    return QString(
               "rtspsrc location=%1 protocols=udp latency=%2 buffer-mode=none timeout=5000000 drop-on-latency=true "
               "! rtph264depay ! h264parse "
               "! d3d11h264dec "
               "! d3d11convert "
               "! d3d11download "
               "! videoconvert "
               "! video/x-raw,format=BGRx "
               "! appsink name=sink drop=true max-buffers=1 sync=false"
               ).arg(url).arg(latency);
}


static QString build_sw_pipeline_explicit(const QString& url, int latency)
{
    return QString(
               "rtspsrc location=%1 protocols=udp latency=%2 buffer-mode=none timeout=5000000 drop-on-latency=true "
               "! rtph264depay ! h264parse "
               "! avdec_h264 "
               "! videoconvert "
               "! queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 "
               "! video/x-raw,format=BGRx "
               "! appsink name=sink drop=true max-buffers=1 sync=false"
               ).arg(url).arg(latency);
}


static bool wait_playing_or_fail(GstElement* pipeline, int timeout_ms, QString& errOut)
{
    // Wait for state change, return false if failure is reported
    GstState state = GST_STATE_NULL, pending = GST_STATE_NULL;
    GstStateChangeReturn r = gst_element_get_state(pipeline, &state, &pending, timeout_ms * GST_MSECOND);
    if (r == GST_STATE_CHANGE_FAILURE) {
        errOut = "[GST] state change failure";
        return false;
    }
    // It's ok if async; we will still receive bus errors if not-linked
    return true;
}

void RtspViewerQt::run()
{
    if (url_.isEmpty()) {
        emit logLine("[GST] url is empty");
        return;
    }

    stopFlag_.store(false, std::memory_order_release);

RECONNECT:
    if (stopFlag_.load(std::memory_order_acquire) || isInterruptionRequested()) return;

    const int latency = std::max(0, latencyMs_);
    const int targetHz = 22;
    const int minEmitIntervalMs = (targetHz > 0 ? (1000 / targetHz) : 0);

    const bool haveD3D11 =
        hasFactory("rtph264depay") &&
        hasFactory("h264parse") &&
        hasFactory("d3d11h264dec") &&
        hasFactory("d3d11convert") &&
        hasFactory("d3d11download") &&
        hasFactory("videoconvert");

    const bool haveSW =
        hasFactory("rtph264depay") &&
        hasFactory("h264parse") &&
        hasFactory("avdec_h264") &&
        hasFactory("videoconvert");

    QString pipeStr;
    QString decoderTag;

    // Try HW first, fallback to SW
    if (haveD3D11) {
        pipeStr = build_hw_d3d11_pipeline(url_, latency);
        decoderTag = "d3d11(h264)+download";
    } else if (haveSW) {
        pipeStr = build_sw_pipeline_explicit(url_, latency);
        decoderTag = "avdec_h264";
    } else {
        // last resort: your old decodebin chain (may be unstable on some installs)
        pipeStr = QString(
                      "rtspsrc name=src location=%1 protocols=tcp latency=%2 timeout=5000000 drop-on-latency=false "
                      "src. ! queue max-size-buffers=0 max-size-time=0 max-size-bytes=0 "
                      "! decodebin "
                      "! videoconvert "
                      "! queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 "
                      "! video/x-raw,format=BGRx "
                      "! appsink name=sink drop=true max-buffers=1 sync=false"
                      ).arg(url_).arg(latency);
        decoderTag = "decodebin(fallback)";
    }

    emit logLine(QString("[GST] pipeline: %1").arg(pipeStr));
    emit logLine(QString("[GST] started (tcp) | decoder=%1 | latency=%2ms").arg(decoderTag).arg(latency));

    GError* err = nullptr;
    GstElement* pipeline = gst_parse_launch(pipeStr.toUtf8().constData(), &err);
    if (!pipeline) {
        emit logLine(QString("[GST] parse_launch failed: %1").arg(err ? qstr(err->message) : "unknown"));
        if (err) g_error_free(err);
        QThread::msleep(200);
        goto RECONNECT;
    }
    if (err) g_error_free(err);

    GstElement* sinkElem = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if (!sinkElem) {
        emit logLine("[GST] appsink not found");
        gst_object_unref(pipeline);
        QThread::msleep(200);
        goto RECONNECT;
    }

    GstAppSink* appsink = GST_APP_SINK(sinkElem);
    gst_app_sink_set_emit_signals(appsink, FALSE);
    gst_app_sink_set_drop(appsink, TRUE);
    gst_app_sink_set_max_buffers(appsink, 1);

    // Force caps to BGRx
    {
        GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "BGRx",
                                            NULL);
        gst_app_sink_set_caps(appsink, caps);
        gst_caps_unref(caps);
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    QString stateErr;
    wait_playing_or_fail(pipeline, 800, stateErr); // don't block too long

    bool needReconnect = false;
    int no_sample_cnt = 0;

    QSharedPointer<QImage> latest;
    QElapsedTimer emitTimer;
    emitTimer.start();

    // PERF
    QElapsedTimer tPerf;
    tPerf.start();
    qint64 nFrames = 0;
    qint64 pullNsAcc = 0;
    qint64 copyNsAcc = 0;

    int busPumpTick = 0;
    static bool printedCaps = false;
    printedCaps = false;

    while (!stopFlag_.load(std::memory_order_acquire) && !isInterruptionRequested()) {

        if ((busPumpTick++ & 7) == 0) {
            pump_bus(pipeline, [&](const QString& s){ emit logLine(s); }, &needReconnect);
            if (needReconnect) break;
        }

        QElapsedTimer tPull; tPull.start();
        GstSample* sample = gst_app_sink_try_pull_sample(appsink, 10 * GST_MSECOND);
        pullNsAcc += tPull.nsecsElapsed();

        if (!sample) {
            if (++no_sample_cnt > 800) { // ~8s
                emit logLine("[GST] no samples too long, reconnect...");
                break;
            }
        } else {
            no_sample_cnt = 0;

            GstCaps* caps = gst_sample_get_caps(sample);
            if (!caps) { gst_sample_unref(sample); continue; }

            GstVideoInfo vinfo;
            if (!gst_video_info_from_caps(&vinfo, caps)) {
                emit logLine(QString("[GST] gst_video_info_from_caps failed, caps=%1").arg(capsToString(caps)));
                gst_sample_unref(sample);
                continue;
            }

            const int w = (int)GST_VIDEO_INFO_WIDTH(&vinfo);
            const int h = (int)GST_VIDEO_INFO_HEIGHT(&vinfo);
            const int srcStride = (int)GST_VIDEO_INFO_PLANE_STRIDE(&vinfo, 0);

            if (!printedCaps) {
                printedCaps = true;
                emit logLine(QString("[GST] negotiated caps: %1").arg(capsToString(caps)));
                emit logLine(QString("[GST] w=%1 h=%2 stride=%3").arg(w).arg(h).arg(srcStride));
            }

            GstBuffer* buffer = gst_sample_get_buffer(sample);
            GstMapInfo map;
            if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {

                QElapsedTimer tCopy; tCopy.start();

                QSharedPointer<QImage> img = QSharedPointer<QImage>::create(w, h, QImage::Format_RGB32);
                const int dstStride = img->bytesPerLine();

                const uchar* src = reinterpret_cast<const uchar*>(map.data);
                uchar* dst0 = img->bits();

                const int rowBytes = w * 4;
                for (int y = 0; y < h; ++y) {
                    memcpy(dst0 + y * dstStride, src + y * srcStride, rowBytes);
                }

                copyNsAcc += tCopy.nsecsElapsed();

                gst_buffer_unmap(buffer, &map);

                latest = img;
                ++nFrames;

            } else {
                emit logLine("[GST] buffer_map failed");
            }

            gst_sample_unref(sample);
        }

        if ((busPumpTick & 15) == 0) {
            pump_bus(pipeline, [&](const QString& s){ emit logLine(s); }, &needReconnect);
            if (needReconnect) break;
        }

        if (latest) {
            if (minEmitIntervalMs <= 0 || emitTimer.elapsed() >= minEmitIntervalMs) {
                emitTimer.restart();
                emit frameReady(latest);
            }
        }

        if (nFrames > 0 && (nFrames % 120 == 0 || tPerf.elapsed() > 2000)) {
            const double sec = std::max(0.001, tPerf.elapsed() / 1000.0);
            const double fps = nFrames / sec;
            const double pullMs = (pullNsAcc / 1e6) / std::max<qint64>(1, nFrames);
            const double copyMs = (copyNsAcc / 1e6) / std::max<qint64>(1, nFrames);
            emit logLine(QString("[PERF] fps=%1 pull=%2ms copy=%3ms decoder=%4")
                             .arg(fps, 0, 'f', 1)
                             .arg(pullMs, 0, 'f', 3)
                             .arg(copyMs, 0, 'f', 3)
                             .arg(decoderTag));
            tPerf.restart();
            nFrames = 0;
            pullNsAcc = 0;
            copyNsAcc = 0;
        }
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(sinkElem);
    gst_object_unref(pipeline);
    emit logLine("[GST] stopped");

    if (!stopFlag_.load(std::memory_order_acquire) && !isInterruptionRequested()) {
        QThread::msleep(200);
        goto RECONNECT;
    }
}
