// rtspviewerqt.cpp  (FULL REPLACEABLE FILE)
// Windows/Qt + GStreamer RTSP client (TCP) low-latency, avoid green & avoid Qt queue buildup.
//
// Fixes:
// 1) Green/odd colors: request BGRx and render as QImage::Format_RGB32 (alpha forced to 255).
// 2) "卡的要死": coalesce frames inside worker thread to prevent GUI queued-signal backlog.
// 3) Print negotiated caps to verify real output format/stride.

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
    wait(1000);
}

void RtspViewerQt::setUrl(const QString& url)
{
    url_ = url;
}

void RtspViewerQt::stop()
{
    stopFlag_.store(true, std::memory_order_release);
}

void RtspViewerQt::run()
{
    if (url_.isEmpty()) {
        emit logLine("[GST] url is empty");
        return;
    }

    stopFlag_.store(false, std::memory_order_release);

RECONNECT:
    if (stopFlag_.load(std::memory_order_acquire)) return;

    // 建议：TCP latency 先 60~120ms；太小更容易“抖/花”，太大延迟显著上升
    const int latency = std::max(0, latencyMs_);

    const QString pipeStr = QString(
                                "rtspsrc name=src location=%1 protocols=tcp latency=%2 timeout=5000000 drop-on-latency=false "
                                // 解码前：不要 leaky，避免丢压缩包导致宏块
                                "src. ! queue max-size-buffers=0 max-size-time=0 max-size-bytes=0 "
                                "! decodebin "
                                "! videoconvert "
                                // 解码后：可以 leaky，只丢“已解码帧”，不会把图像打花
                                "! queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 "
                                "! video/x-raw,format=BGRx "
                                "! appsink name=sink drop=true max-buffers=1 sync=false"
                                ).arg(url_).arg(latency);


    emit logLine(QString("[GST] pipeline: %1").arg(pipeStr));
    emit logLine("[GST] started (tcp)");

    GError* err = nullptr;
    GstElement* pipeline = gst_parse_launch(pipeStr.toUtf8().constData(), &err);
    if (!pipeline) {
        emit logLine(QString("[GST] parse_launch failed: %1").arg(err ? qstr(err->message) : "unknown"));
        if (err) g_error_free(err);
        QThread::msleep(300);
        goto RECONNECT;
    }
    if (err) g_error_free(err);

    GstElement* sinkElem = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if (!sinkElem) {
        emit logLine("[GST] appsink not found");
        gst_object_unref(pipeline);
        QThread::msleep(300);
        goto RECONNECT;
    }

    GstAppSink* appsink = GST_APP_SINK(sinkElem);
    gst_app_sink_set_emit_signals(appsink, FALSE);
    gst_app_sink_set_drop(appsink, TRUE);
    gst_app_sink_set_max_buffers(appsink, 1);

    // 强制 caps
    {
        GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "BGRx",
                                            NULL);
        gst_app_sink_set_caps(appsink, caps);
        gst_caps_unref(caps);
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    bool needReconnect = false;
    int no_sample_cnt = 0;

    // 为了防止 Qt queued signal 积压：线程内只保留“最新帧”
    QSharedPointer<QImage> latest;
    QElapsedTimer emitTimer;
    emitTimer.start();

    // === GUI 显示限频（防止 Qt queued signal 堆积导致卡顿）===
    const int targetHz = 22;                 // 你之前明确提到只需要 22Hz
    const int minEmitIntervalMs = 1000 / targetHz;

    // 降低 bus pump 频率，减少循环开销
    int busPumpTick = 0;

    while (!stopFlag_.load(std::memory_order_acquire)) {

        if ((busPumpTick++ & 7) == 0) { // 每 8 次循环 pump 一次
            pump_bus(pipeline, [&](const QString& s){ emit logLine(s); }, &needReconnect);
            if (needReconnect) break;
        }

        // 10ms 超时：低延迟拉取
        GstSample* sample = gst_app_sink_try_pull_sample(appsink, 10 * GST_MSECOND);

        if (!sample) {
            if (++no_sample_cnt > 800) { // ~8s 兜底
                emit logLine("[GST] no samples too long, reconnect...");
                break;
            }
            // 即便没新 sample，也允许把 latest 按限频发出去（避免 GUI “饿死”）
        } else {
            no_sample_cnt = 0;

            GstCaps* caps = gst_sample_get_caps(sample);
            if (!caps) {
                gst_sample_unref(sample);
                continue;
            }

            GstVideoInfo vinfo;
            if (!gst_video_info_from_caps(&vinfo, caps)) {
                emit logLine(QString("[GST] gst_video_info_from_caps failed, caps=%1").arg(capsToString(caps)));
                gst_sample_unref(sample);
                continue;
            }

            const int w = (int)GST_VIDEO_INFO_WIDTH(&vinfo);
            const int h = (int)GST_VIDEO_INFO_HEIGHT(&vinfo);
            const int srcStride = (int)GST_VIDEO_INFO_PLANE_STRIDE(&vinfo, 0);

            // 打印一次 negotiated caps（排查“到底输出啥格式”）
            static bool printedCaps = false;
            if (!printedCaps) {
                printedCaps = true;
                emit logLine(QString("[GST] negotiated caps: %1").arg(capsToString(caps)));
                emit logLine(QString("[GST] w=%1 h=%2 stride=%3").arg(w).arg(h).arg(srcStride));
            }

            GstBuffer* buffer = gst_sample_get_buffer(sample);
            GstMapInfo map;
            if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {

                // BGRx -> QImage::Format_RGB32 (内存布局匹配：little-endian 下为 B,G,R,x)
                QSharedPointer<QImage> img = QSharedPointer<QImage>::create(w, h, QImage::Format_RGB32);
                const int dstStride = img->bytesPerLine();

                const uchar* src = reinterpret_cast<const uchar*>(map.data);
                uchar* dst0 = img->bits();

                // 只拷贝 w*4，避免 stride 对齐差异带来的“行尾垃圾”
                const int rowBytes = w * 4;
                for (int y = 0; y < h; ++y) {
                    memcpy(dst0 + y * dstStride, src + y * srcStride, rowBytes);
                }

                gst_buffer_unmap(buffer, &map);

                // 更新“最新帧”
                latest = img;
            } else {
                emit logLine("[GST] buffer_map failed");
            }

            gst_sample_unref(sample);
        }

        // 触发重连？
        if ((busPumpTick & 15) == 0) { // 再补一次
            pump_bus(pipeline, [&](const QString& s){ emit logLine(s); }, &needReconnect);
            if (needReconnect) break;
        }

        // 限频发给 GUI（避免 queued signal 堆积导致卡顿）
        if (latest) {
            if (minEmitIntervalMs <= 0 || emitTimer.elapsed() >= minEmitIntervalMs) {
                emitTimer.restart();
                emit frameReady(latest);
            }
        }
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(sinkElem);
    gst_object_unref(pipeline);
    emit logLine("[GST] stopped");

    if (!stopFlag_.load(std::memory_order_acquire)) {
        QThread::msleep(300);
        goto RECONNECT;
    }
}
