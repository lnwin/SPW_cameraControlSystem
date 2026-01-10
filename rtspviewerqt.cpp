#include "rtspviewerqt.h"
#include <QCoreApplication>
#include <QDir>
#include <QMutex>
#include <QMutexLocker>

static void gst_init_once()
{
    static std::once_flag f;
    std::call_once(f, []{
        gst_init(nullptr, nullptr);
    });
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

static inline QString qstr(const char* s){ return QString::fromUtf8(s ? s : ""); }

// 把 GST 的错误打印出来（便于你定位“插件没找到/解码器缺失”）
static void log_bus_messages(GstElement* pipeline, std::function<void(const QString&)> logFn)
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
            logFn(QString("[GST][ERR] %1 | %2").arg(qstr(err?err->message:"")).arg(qstr(dbg)));
            if (err) g_error_free(err);
            if (dbg) g_free(dbg);
        } break;
        case GST_MESSAGE_WARNING: {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_warning(msg, &err, &dbg);
            logFn(QString("[GST][WRN] %1 | %2").arg(qstr(err?err->message:"")).arg(qstr(dbg)));
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

void RtspViewerQt::run()
{
    if (url_.isEmpty()){
        emit logLine("[GST] url is empty");
        return;
    }

    stopFlag_.store(false, std::memory_order_release);

// 运行时插件路径（非常关键：否则 decodebin 可能找不到解码器）
// 建议你最终改成相对路径（打包目录下的 plugins）
// 这里先不强行设置，避免覆盖你 Qt Creator 里配置的环境。
// qputenv("GST_PLUGIN_PATH", "...");

RECONNECT:
    if (stopFlag_.load(std::memory_order_acquire)) return;

    const QString proto = useTcp_ ? "tcp" : "udp";
    const QString pipeStr = QString(
                                "rtspsrc location=%1 protocols=%2 latency=%3 ! "
                                "decodebin ! "
                                "videoconvert ! "
                                "queue leaky=downstream max-size-buffers=1 ! "
                                "video/x-raw,format=RGB ! "
                                "appsink name=sink drop=true max-buffers=1 sync=false"
                                ).arg(url_).arg(proto).arg(latencyMs_);

    emit logLine(QString("[GST] pipeline: %1").arg(pipeStr));

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

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    emit logLine(QString("[GST] started (%1)").arg(proto));

    int no_sample_cnt = 0;

    while (!stopFlag_.load(std::memory_order_acquire)) {

        // 拉取样本（50ms 超时），同时处理 bus 信息
        GstSample* sample = gst_app_sink_try_pull_sample(appsink, 50 * GST_MSECOND);

        log_bus_messages(pipeline, [&](const QString& s){ emit logLine(s); });

        if (!sample) {
            // 长时间没 sample 可能是断流/卡死，重连
            if (++no_sample_cnt > 200) { // ~10s
                emit logLine("[GST] no samples too long, reconnect...");
                break;
            }
            continue;
        }
        no_sample_cnt = 0;

        GstCaps* caps = gst_sample_get_caps(sample);
        if (!caps) { gst_sample_unref(sample); continue; }

        GstVideoInfo vinfo;
        if (!gst_video_info_from_caps(&vinfo, caps)) {
            gst_sample_unref(sample);
            continue;
        }
        const int w = (int)GST_VIDEO_INFO_WIDTH(&vinfo);
        const int h = (int)GST_VIDEO_INFO_HEIGHT(&vinfo);
        const int srcStride = (int)GST_VIDEO_INFO_PLANE_STRIDE(&vinfo, 0); // RGB stride

        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {

            // 生成 QImage 并按行拷贝（兼容 stride != w*3 的情况，避免花屏）
            auto img = QSharedPointer<QImage>::create(w, h, QImage::Format_RGB888);
            const int dstStride = img->bytesPerLine();
            const uchar* src = reinterpret_cast<const uchar*>(map.data);

            const int copyBytes = std::min(dstStride, srcStride);
            for (int y = 0; y < h; ++y) {
                memcpy(img->scanLine(y), src + y * srcStride, copyBytes);
            }

            gst_buffer_unmap(buffer, &map);

            emit frameReady(img);
        } else {
            emit logLine("[GST] buffer_map failed");
        }

        gst_sample_unref(sample);
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
