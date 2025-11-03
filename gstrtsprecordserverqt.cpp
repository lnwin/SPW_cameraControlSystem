// gstrtsp_record_server_qt.cpp

// ===== Windows 宏防护（在任何 GStreamer 头之前）=====
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifdef interface
#undef interface
#endif
#endif

#include "gstrtsprecordserverqt.h"
#include <gst/rtsp/gstrtspmessage.h>

#include <QDebug>
#include <opencv2/opencv.hpp>
// 头上放一个计时工具（没有 lp::now_us 就用 QElapsedTimer）
#include <QElapsedTimer>
static QElapsedTimer s_tmr; static bool s_tmr_inited=false;
static uint64_t s_cnt=0;
// 关键：避免 Qt 的关键字宏污染 GLib 头（signals/slots 在 GLib 结构体里是字段名）
#include <QtCore/qobjectdefs.h>
#ifdef signals
#undef signals
#endif
#ifdef slots
#undef slots
#endif
#ifdef interface
#undef interface
#endif

// ===== 只在 .cpp 引入 GStreamer 头 =====
extern "C" {
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/video/video.h>
}
// 需要这个头（放在你的 extern "C" 块里）：#include <gst/rtsp/gstrtspmessage.h>

// 纯 C 回调：解析 request -> 打印 method + uri
static void on_req_log_cb(GstRTSPClient*, GstRTSPContext* ctx, gpointer user_data) {
    const char* tag = static_cast<const char*>(user_data);
    const char* uri = (ctx && ctx->uri && ctx->uri->abspath) ? ctx->uri->abspath : "/";

    char method_txt[32] = "?";
    if (ctx && ctx->request) {
        GstRTSPMethod   method;
        const gchar*    req_uri = nullptr;
        GstRTSPVersion  ver;
        if (gst_rtsp_message_parse_request(ctx->request, &method, &req_uri, &ver) == GST_RTSP_OK) {
            if (const gchar* s = gst_rtsp_method_as_text(method)) {
                size_t n = qMin<size_t>(sizeof(method_txt)-1, strlen(s));
                memcpy(method_txt, s, n); method_txt[n] = '\0';
            }
            if (req_uri && *req_uri) uri = req_uri; // 指向内部缓冲，别 free
        }
    }

    qInfo().noquote() << QString("[HOOK] %1 method=%2 uri=%3").arg(tag).arg(method_txt).arg(uri);
}




static inline void qlogE(const char* s){ qWarning().noquote() << s; }
static inline void qlogI(const char* s){ qInfo().noquote()    << s; }

// ---------------- bus 日志（.h 中性签名：int(void*,void*,void*)） ----------------
int GstRtspRecordServerQt::on_bus(void* /*bus*/, void* msg_v, void* /*user_data*/){
    GstMessage* msg = static_cast<GstMessage*>(msg_v);
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError* err=nullptr; gchar* dbg=nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        qWarning().noquote() << "[GST-ERROR]" << (err?err->message:"unknown") << (dbg?dbg:"");
        if (err) g_error_free(err); if (dbg) g_free(dbg);
        break; }
    case GST_MESSAGE_WARNING: {
        GError* err=nullptr; gchar* dbg=nullptr;
        gst_message_parse_warning(msg, &err, &dbg);
        qWarning().noquote() << "[GST-WARN ]" << (err?err->message:"unknown") << (dbg?dbg:"");
        if (err) g_error_free(err); if (dbg) g_free(dbg);
        break; }
    default: break;
    }
    return 1; // TRUE
}

const gchar* launch =
    "( rtpjitterbuffer mode=slave latency=0 drop-on-late=true do-lost=true "
    "  ! rtph264depay name=depay0 "
    "  ! h264parse config-interval=-1 disable-passthrough=true "
    "  ! avdec_h264 "
    "  ! videoconvert "
    "  ! video/x-raw,format=RGB "
    "  ! appsink name=preview emit-signals=true sync=false max-buffers=2 drop=true )";

int GstRtspRecordServerQt::on_new_sample(void* sink_v, void* user_data){
    auto* self = static_cast<GstRtspRecordServerQt*>(user_data);
    auto* sink = GST_APP_SINK(sink_v);

    if (!s_tmr_inited){ s_tmr_inited=true; s_tmr.start(); }

    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) return 0;

    GstCaps* caps = gst_sample_get_caps(sample);
    GstBuffer* buf = gst_sample_get_buffer(sample);
    if (!caps || !buf){ gst_sample_unref(sample); return 0; }

    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, caps)) { gst_sample_unref(sample); return 0; }

    GstVideoFrame vf;
    if (!gst_video_frame_map(&vf, &info, buf, GST_MAP_READ)) { gst_sample_unref(sample); return 0; }

    const int w = GST_VIDEO_FRAME_WIDTH(&vf);
    const int h = GST_VIDEO_FRAME_HEIGHT(&vf);
    uchar* data = (uchar*)GST_VIDEO_FRAME_PLANE_DATA(&vf, 0);
    const int stride = (int)GST_VIDEO_FRAME_PLANE_STRIDE(&vf, 0);

    // 我们的 launch 保证输出 RGB888
    QImage img((const uchar*)data, w, h, stride, QImage::Format_RGB888);
    QImage imgCopy = img.copy(); // 深拷贝以避免 unmap 后悬挂

    gst_video_frame_unmap(&vf);
    gst_sample_unref(sample);

    Q_EMIT self->frameReady(imgCopy);

    // FPS 日志（便于确认 appsink 在产帧）
    if ((++s_cnt % 30) == 0) {
        const double fps = s_tmr.elapsed() > 0 ? (s_cnt * 1000.0 / s_tmr.elapsed()) : 0.0;
        qInfo().noquote() << "[RTSP] preview frames =" << s_cnt << " ~" << fps << "FPS";
    }
    return 0;
}
// ---------------- media-configure（.h 中性签名：void(void*,void*,void*)） ----------------
void GstRtspRecordServerQt::on_media_configure(void* /*factory_v*/, void* media_v, void* user_data){
    auto* self  = static_cast<GstRtspRecordServerQt*>(user_data);
    auto* media = static_cast<GstRTSPMedia*>(media_v);
    g_object_ref(media);

    GstElement* pipe = gst_rtsp_media_get_element(media);
    if (!pipe){ qlogE("[RTSP] no pipeline"); g_object_unref(media); return; }

    // bus watch
    GstBus* bus = gst_element_get_bus(pipe);
    gst_bus_add_watch(bus, (GstBusFunc)GstRtspRecordServerQt::on_bus, self);
    gst_object_unref(bus);

    // 找 appsink
    GstElement* sink_e = gst_bin_get_by_name(GST_BIN(pipe), "preview");
    if (!sink_e){
        qlogE("[RTSP] appsink 'preview' not found");
        gst_object_unref(pipe);
        g_object_unref(media);
        return;
    }

    self->appsink_ = GST_APP_SINK(sink_e);
    g_object_set(self->appsink_, "emit-signals", TRUE,
                 "sync", FALSE,
                 "max-buffers", 2,
                 "drop", TRUE, nullptr);
    g_signal_connect(self->appsink_, "new-sample",
                     G_CALLBACK(GstRtspRecordServerQt::on_new_sample), self);

    g_signal_connect(media, "unprepared", G_CALLBACK(+[](GstRTSPMedia* m, gpointer ud){
                         auto* s = static_cast<GstRtspRecordServerQt*>(ud);
                         s->appsink_ = nullptr;
                         Q_EMIT s->publisherDisconnected();
                         Q_UNUSED(m);
                     }), self);

    Q_EMIT self->publisherConnected();

    qlogI("[RTSP] media-configure: appsink ready");
    gst_object_unref(pipe);
    g_object_unref(media);
}

// ================= 类实现 =================
GstRtspRecordServerQt::GstRtspRecordServerQt(QObject* parent) : QThread(parent){
    static bool inited=false;
    if (!inited){ gst_init(nullptr,nullptr); inited=true; }
}
GstRtspRecordServerQt::~GstRtspRecordServerQt(){
    stopAsync();
    wait();
}

void GstRtspRecordServerQt::configure(const QString& ip, const QString& service, const QString& path){
    ip_ = ip;
    service_ = service;
    path_ = path.startsWith('/') ? path : ("/"+path);
}

void GstRtspRecordServerQt::stopAsync(){
    running_.store(false, std::memory_order_release);
    if (main_loop_) g_main_loop_quit(main_loop_);
}

void GstRtspRecordServerQt::run(){
    running_.store(true, std::memory_order_release);

    main_loop_ = g_main_loop_new(nullptr, FALSE);
    server_    = gst_rtsp_server_new();
    g_object_set(server_, "address", ip_.toUtf8().constData(),
                 "service", service_.toUtf8().constData(), nullptr);

    mounts_ = gst_rtsp_server_get_mount_points(server_);

    // RECORD：接收 H264/RTP(UDP) → depay → parse → 解码 → BGR → appsink
    const gchar* launch =
        "( rtpjitterbuffer mode=slave latency=0 drop-on-late=true do-lost=true "
        "  ! rtph264depay name=depay0 "
        "  ! h264parse config-interval=-1 disable-passthrough=true "
        "  ! avdec_h264 "
        "  ! videoconvert "
        "  ! video/x-raw,format=RGB "
        "  ! appsink name=preview emit-signals=true sync=false max-buffers=2 drop=true )";


    factory_ = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory_, launch);
    gst_rtsp_media_factory_set_shared(factory_, TRUE);
    gst_rtsp_media_factory_set_latency(factory_, 0);

    // ★ 关键：RECORD + 仅 UDP
    gst_rtsp_media_factory_set_transport_mode(factory_, GST_RTSP_TRANSPORT_MODE_RECORD);
    gst_rtsp_media_factory_set_protocols(factory_, GST_RTSP_LOWER_TRANS_UDP);

    g_signal_connect(factory_, "media-configure",
                     G_CALLBACK(GstRtspRecordServerQt::on_media_configure), this);

    gst_rtsp_mount_points_add_factory(mounts_, path_.toUtf8().constData(), factory_);
    g_object_unref(mounts_); mounts_ = nullptr;



    g_signal_connect(server_, "client-connected",
                     G_CALLBACK(+[](GstRTSPServer*, GstRTSPClient* client, gpointer){
                         qInfo() << "[HOOK] client-connected" << client;
                         g_signal_connect(client, "options-request",  G_CALLBACK(on_req_log_cb), (gpointer)"OPTIONS");
                         g_signal_connect(client, "describe-request", G_CALLBACK(on_req_log_cb), (gpointer)"DESCRIBE");
                         g_signal_connect(client, "announce-request", G_CALLBACK(on_req_log_cb), (gpointer)"ANNOUNCE");
                         g_signal_connect(client, "setup-request",    G_CALLBACK(on_req_log_cb), (gpointer)"SETUP");
                         g_signal_connect(client, "record-request",   G_CALLBACK(on_req_log_cb), (gpointer)"RECORD");
                         g_signal_connect(client, "teardown-request", G_CALLBACK(on_req_log_cb), (gpointer)"TEARDOWN");
                     }), nullptr);


    if (gst_rtsp_server_attach(server_, nullptr) == 0){
        qlogE("[RTSP] attach failed (port busy?)");
        g_object_unref(server_); server_ = nullptr;
        g_main_loop_unref(main_loop_); main_loop_ = nullptr;
        return;
    }

    // 启动时打印挂载与监听信息，便于核对 URL
    qInfo().noquote() << "[RTSP-RECORD] mount path =" << path_;
    qInfo().noquote() << "[RTSP-RECORD] listening at rtsp://"
                      << ip_ << ":" << service_ << path_;

    g_main_loop_run(main_loop_);

    // 清理
    appsink_ = nullptr;
    if (server_)    { g_object_unref(server_);    server_ = nullptr; }
    if (main_loop_) { g_main_loop_unref(main_loop_); main_loop_ = nullptr; }
}
