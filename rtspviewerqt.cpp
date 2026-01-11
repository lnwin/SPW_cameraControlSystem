// rtspviewerqt.cpp  (FULL REPLACEABLE FILE)
// Windows/Qt + GStreamer RTSP client (UDP) + sample-interval jitter stats.
//
// Adds sample interval statistics per 2s window:
//  gap_avg, p50/p90/p99, min/max, >80ms/>120ms counts, stall_max, jitter_rms.
//
// Also keeps: pre-decode queue + triple QImage pool + short pull timeout.

#include "rtspviewerqt.h"

#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <functional>
#include <mutex>
#include <array>
#include <vector>
#include <cmath>

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

static bool hasFactory(const char* name)
{
    GstElementFactory* f = gst_element_factory_find(name);
    if (f) { gst_object_unref(f); return true; }
    return false;
}

// bus：打印日志；遇到 ERROR/EOS 触发重连
static void pump_bus(GstElement* pipeline,
                     const std::function<void(const QString&)>& logFn,
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

// -------------------------- Pipeline Builders (UDP) --------------------------
static constexpr int  kUdpRcvBufBytes = 16 * 1024 * 1024; // 16MB
static constexpr bool kDropOnLatency  = false;

// queue before decoder: absorb short RTP burst; must NOT be leaky
static const char* kPreDecodeQueue =
    "queue "
    "max-size-buffers=0 max-size-bytes=0 max-size-time=0 "
    "min-threshold-time=0 "
    "leaky=no";

static QString build_hw_d3d11_pipeline_udp(const QString& url, int latencyMs)
{
    return QString(
               "rtspsrc location=%1 protocols=udp latency=%2 buffer-mode=auto "
               "udp-buffer-size=%3 do-retransmission=false drop-on-latency=%4 timeout=5000000 "
               "! rtph264depay "
               "! h264parse "
               "! %5 "
               "! d3d11h264dec "
               "! d3d11convert "
               "! video/x-raw(memory:D3D11Memory),format=BGRA "
               "! d3d11download "
               "! video/x-raw,format=BGRA "
               "! queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 "
               "! appsink name=sink drop=true max-buffers=1 sync=false"
               ).arg(url)
        .arg(latencyMs)
        .arg(kUdpRcvBufBytes)
        .arg(kDropOnLatency ? "true" : "false")
        .arg(kPreDecodeQueue);
}

static QString build_sw_pipeline_udp(const QString& url, int latencyMs)
{
    return QString(
               "rtspsrc location=%1 protocols=udp latency=%2 buffer-mode=auto "
               "udp-buffer-size=%3 do-retransmission=false drop-on-latency=%4 timeout=5000000 "
               "! rtph264depay "
               "! h264parse "
               "! %5 "
               "! avdec_h264 "
               "! videoconvert "
               "! video/x-raw,format=BGRA "
               "! queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 "
               "! appsink name=sink drop=true max-buffers=1 sync=false"
               ).arg(url)
        .arg(latencyMs)
        .arg(kUdpRcvBufBytes)
        .arg(kDropOnLatency ? "true" : "false")
        .arg(kPreDecodeQueue);
}

static QString build_fallback_decodebin_udp(const QString& url, int latencyMs)
{
    return QString(
               "rtspsrc name=src location=%1 protocols=udp latency=%2 buffer-mode=auto "
               "udp-buffer-size=%3 do-retransmission=false drop-on-latency=%4 timeout=5000000 "
               "src. ! queue max-size-buffers=0 max-size-time=0 max-size-bytes=0 "
               "! decodebin "
               "! videoconvert "
               "! video/x-raw,format=BGRA "
               "! queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 "
               "! appsink name=sink drop=true max-buffers=1 sync=false"
               ).arg(url)
        .arg(latencyMs)
        .arg(kUdpRcvBufBytes)
        .arg(kDropOnLatency ? "true" : "false");
}

static bool wait_playing_or_fail(GstElement* pipeline, int timeout_ms, QString& errOut)
{
    GstState state = GST_STATE_NULL, pending = GST_STATE_NULL;
    GstStateChangeReturn r = gst_element_get_state(pipeline, &state, &pending, timeout_ms * GST_MSECOND);
    if (r == GST_STATE_CHANGE_FAILURE) {
        errOut = "[GST] state change failure";
        return false;
    }
    return true;
}

// --------- jitter helpers ----------
static inline double clampd(double v, double lo, double hi){
    return std::max(lo, std::min(hi, v));
}

static double percentile_ms(std::vector<double>& v, double p01)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double p = clampd(p01, 0.0, 1.0);
    const double idx = p * (double)(v.size() - 1);
    const size_t i0 = (size_t)std::floor(idx);
    const size_t i1 = (size_t)std::ceil(idx);
    if (i0 == i1) return v[i0];
    const double t = idx - (double)i0;
    return v[i0] * (1.0 - t) + v[i1] * t;
}

// ---------------------------------------------------------------------------

RtspViewerQt::RtspViewerQt(QObject* parent)
    : QThread(parent)
{
    gst_init_once();
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
    requestInterruption();
}

QSharedPointer<QImage> RtspViewerQt::takeLatestFrameIfNew()
{
    const uint64_t cur = latestSeq_.load(std::memory_order_acquire);
    const uint64_t last = takenSeq_.load(std::memory_order_acquire);
    if (cur == 0 || cur == last) return {};

    std::lock_guard<std::mutex> lk(latestMtx_);
    const uint64_t cur2 = latestSeq_.load(std::memory_order_acquire);
    const uint64_t last2 = takenSeq_.load(std::memory_order_acquire);
    if (cur2 == 0 || cur2 == last2) return {};
    takenSeq_.store(cur2, std::memory_order_release);
    return latest_;
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

    int latency = latencyMs_;
    if (latency <= 0) latency = 350;
    latency = std::clamp(latency, 300, 600);

    // Short pull timeout to reduce "stall then jump".
    const GstClockTime pullTimeout = 20 * GST_MSECOND;

    const bool haveD3D11 =
        hasFactory("rtph264depay") &&
        hasFactory("h264parse") &&
        hasFactory("d3d11h264dec") &&
        hasFactory("d3d11convert") &&
        hasFactory("d3d11download");

    const bool haveSW =
        hasFactory("rtph264depay") &&
        hasFactory("h264parse") &&
        hasFactory("avdec_h264") &&
        hasFactory("videoconvert");

    QString pipeStr;
    QString decoderTag;

    if (haveD3D11) {
        pipeStr = build_hw_d3d11_pipeline_udp(url_, latency);
        decoderTag = "d3d11(h264)->BGRA(download)";
    } else if (haveSW) {
        pipeStr = build_sw_pipeline_udp(url_, latency);
        decoderTag = "avdec_h264";
    } else {
        pipeStr = build_fallback_decodebin_udp(url_, latency);
        decoderTag = "decodebin(fallback)";
    }

    emit logLine(QString("[GST] pipeline: %1").arg(pipeStr));
    emit logLine(QString("[GST] started (udp) | decoder=%1 | latency=%2ms | drop-on-latency=%3 | udpbuf=%4MB")
                     .arg(decoderTag)
                     .arg(latency)
                     .arg(kDropOnLatency ? "true" : "false")
                     .arg(kUdpRcvBufBytes / (1024 * 1024)));

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

    // Force caps to BGRA
    {
        GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "BGRA",
                                            NULL);
        gst_app_sink_set_caps(appsink, caps);
        gst_caps_unref(caps);
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    QString stateErr;
    wait_playing_or_fail(pipeline, 2000, stateErr);

    bool needReconnect = false;
    int  no_sample_cnt = 0;
    bool printedCaps = false;
    int  busPumpTick = 0;

    // triple-buffer reuse (avoid per-frame new)
    std::array<QSharedPointer<QImage>, 3> pool;
    int poolIdx = 0;
    int poolW = 0, poolH = 0;

    auto ensurePool = [&](int w, int h) {
        if (w == poolW && h == poolH && pool[0] && pool[1] && pool[2]) return;
        poolW = w; poolH = h;
        for (int i = 0; i < 3; ++i) {
            pool[i] = QSharedPointer<QImage>::create(poolW, poolH, QImage::Format_ARGB32);
        }
        poolIdx = 0;
        emit logLine(QString("[GST] QImage pool recreated: %1x%2").arg(poolW).arg(poolH));
    };

    // PERF + jitter window (2s)
    QElapsedTimer tPerf;
    tPerf.start();
    qint64 frames = 0;
    qint64 copyNsAcc = 0;

    // sample timing
    QElapsedTimer tWall;
    tWall.start();
    qint64 lastSampleWallMs = -1;
    qint64 lastAnyWallMs = tWall.elapsed(); // used for stall_max
    qint64 stallMaxMs = 0;

    std::vector<double> gapsMs; gapsMs.reserve(256);
    double gapSum = 0.0;
    double jitterSqSum = 0.0;
    int gapGt80 = 0;
    int gapGt120 = 0;
    double gapMin = 1e9;
    double gapMax = 0.0;

    const double nominalGap = 1000.0 / 22.0; // you can change if your true fps differs

    while (!stopFlag_.load(std::memory_order_acquire) && !isInterruptionRequested()) {

        const qint64 nowAny = tWall.elapsed();
        const qint64 stall = nowAny - lastAnyWallMs;
        if (stall > stallMaxMs) stallMaxMs = stall;

        if ((busPumpTick++ & 7) == 0) {
            pump_bus(pipeline, [&](const QString& s){ emit logLine(s); }, &needReconnect);
            if (needReconnect) break;
        }

        GstSample* sample = gst_app_sink_try_pull_sample(appsink, pullTimeout);

        lastAnyWallMs = tWall.elapsed();

        if (!sample) {
            if (++no_sample_cnt > 200) { // 600 * 20ms = 12s
                emit logLine("[GST] no samples too long, reconnect...");
                break;
            }
            continue;
        }

        // sample arrived
        const qint64 nowMs = tWall.elapsed();
        if (lastSampleWallMs >= 0) {
            const double gap = (double)(nowMs - lastSampleWallMs);
            gapsMs.push_back(gap);
            gapSum += gap;
            gapMin = std::min(gapMin, gap);
            gapMax = std::max(gapMax, gap);
            if (gap > 80.0)  ++gapGt80;
            if (gap > 120.0) ++gapGt120;
            const double j = gap - nominalGap;
            jitterSqSum += j * j;
        }
        lastSampleWallMs = nowMs;

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

        ensurePool(w, h);

        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {

            QElapsedTimer tCopy; tCopy.start();

            QSharedPointer<QImage> img = pool[poolIdx];
            poolIdx = (poolIdx + 1) % (int)pool.size();

            const int dstStride = img->bytesPerLine();
            const uchar* src = reinterpret_cast<const uchar*>(map.data);
            uchar* dst0 = img->bits();

            const int rowBytes = w * 4;
            for (int y = 0; y < h; ++y) {
                memcpy(dst0 + y * dstStride, src + y * srcStride, rowBytes);
            }

            copyNsAcc += tCopy.nsecsElapsed();

            gst_buffer_unmap(buffer, &map);

            {
                std::lock_guard<std::mutex> lk(latestMtx_);
                latest_ = img;
                latestSeq_.fetch_add(1, std::memory_order_release);
            }

            ++frames;

        } else {
            emit logLine("[GST] buffer_map failed");
        }

        gst_sample_unref(sample);

        if ((busPumpTick & 15) == 0) {
            pump_bus(pipeline, [&](const QString& s){ emit logLine(s); }, &needReconnect);
            if (needReconnect) break;
        }

        if (tPerf.elapsed() > 2000) {
            const double sec = std::max(0.001, tPerf.elapsed() / 1000.0);
            const double fps = frames / sec;
            const double copyMs = (copyNsAcc / 1e6) / std::max<qint64>(1, frames);

            // jitter stats
            double gapAvg = 0.0;
            double p50 = 0.0, p90 = 0.0, p99 = 0.0;
            double jitterRms = 0.0;
            const int nGap = (int)gapsMs.size();
            if (nGap > 0) {
                gapAvg = gapSum / (double)nGap;
                std::vector<double> tmp = gapsMs; // copy for percentile sort
                p50 = percentile_ms(tmp, 0.50);
                p90 = percentile_ms(tmp, 0.90);
                p99 = percentile_ms(tmp, 0.99);
                jitterRms = std::sqrt(jitterSqSum / (double)nGap);
            } else {
                gapMin = 0.0;
                gapMax = 0.0;
            }

            emit logLine(QString("[PERF] fps=%1 copy=%2ms decoder=%3 transport=udp latency=%4ms | "
                                 "gap_avg=%5ms p50=%6 p90=%7 p99=%8 min=%9 max=%10 gt80=%11 gt120=%12 "
                                 "stall_max=%13ms jitter_rms=%14ms")
                             .arg(fps, 0, 'f', 1)
                             .arg(copyMs, 0, 'f', 3)
                             .arg(decoderTag)
                             .arg(latency)
                             .arg(gapAvg, 0, 'f', 1)
                             .arg(p50, 0, 'f', 1)
                             .arg(p90, 0, 'f', 1)
                             .arg(p99, 0, 'f', 1)
                             .arg(gapMin, 0, 'f', 1)
                             .arg(gapMax, 0, 'f', 1)
                             .arg(gapGt80)
                             .arg(gapGt120)
                             .arg(stallMaxMs)
                             .arg(jitterRms, 0, 'f', 1));

            // reset window
            tPerf.restart();
            frames = 0;
            copyNsAcc = 0;

            gapsMs.clear();
            gapSum = 0.0;
            jitterSqSum = 0.0;
            gapGt80 = 0;
            gapGt120 = 0;
            gapMin = 1e9;
            gapMax = 0.0;
            stallMaxMs = 0;
        }
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    QThread::msleep(30);

    gst_object_unref(sinkElem);
    gst_object_unref(pipeline);
    emit logLine("[GST] stopped");

    if (!stopFlag_.load(std::memory_order_acquire) && !isInterruptionRequested()) {
        QThread::msleep(200);
        goto RECONNECT;
    }
}
