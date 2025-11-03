// ======= 顶部保持你的防宏，顺序不变 =======
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

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include "gstrtspserverqt.h"
#include <QDebug>

GstRtspServerQt::GstRtspServerQt(QObject* parent) : QThread(parent) {}
GstRtspServerQt::~GstRtspServerQt(){ stopAsync(); wait(); }

// 新签名：可指定 IP
void GstRtspServerQt::configure(const QString& ip, const QString& servicePort, const QString& mountPath){
    ip_ = ip;
    service_ = servicePort;
    path_ = mountPath.startsWith("/") ? mountPath : ("/" + mountPath);
}

void GstRtspServerQt::stopAsync(){
    running_.store(false, std::memory_order_release);
}

void GstRtspServerQt::run(){
    running_.store(true, std::memory_order_release);

    static bool inited = false;
    if (!inited) { gst_init(nullptr, nullptr); inited = true; }

    // —— 关键探针：在你的进程里能否创建 rtpbin？
    GstElement* probe = gst_element_factory_make("rtpbin", nullptr);
    if (!probe) {
        qWarning() << "[RTSP] FATAL: cannot create 'rtpbin'. Check PATH/GST_PLUGIN_PATH.";
        return; // 直接返回，避免后续 5XX
    }
    gst_object_unref(probe);
    qInfo() << "[RTSP] 'rtpbin' OK in-process";

    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    GstRTSPServer* server = gst_rtsp_server_new();

    g_object_set(server,
                 "service", service_.toUtf8().constData(),
                 "address", ip_.toUtf8().constData(),
                 nullptr);

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server);

    // —— NVENC 低时延 H.264 ——
    // 要点：NV12 输入、关闭 B 帧、超低时延、每个 IDR 重发 SPS/PPS
    const char* launch_str =
        "( videotestsrc is-live=true pattern=smpte75 ! "
        "  video/x-raw,framerate=21/1 ! "
        "  videoconvert ! video/x-raw,format=NV12 ! "
        "  nvh264enc tune=ultra-low-latency zerolatency=true preset=p2 "
        "            bitrate=2000 bframes=0 cabac=true "
        "            aud=true repeat-sequence-header=true ! "
        "  h264parse config-interval=1 ! "
        "  rtph264pay name=pay0 pt=96 )";

    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, launch_str);
    gst_rtsp_media_factory_set_latency(factory, 0);
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    gst_rtsp_mount_points_add_factory(mounts, path_.toUtf8().constData(), factory);
    g_object_unref(mounts);
    // 有客户端 TCP 控制连接时会触发
    g_signal_connect(server, "client-connected", G_CALLBACK(+[](
                                                                 GstRTSPServer*, GstRTSPClient* client, gpointer){
                         g_print("[RTSP] client-connected\n");
                     }), nullptr);

    // 每次要为一个客户端创建媒体管道时触发（关键）
    g_signal_connect(factory, "media-configure", G_CALLBACK(+[](
                                                                 GstRTSPMediaFactory*, GstRTSPMedia* media, gpointer){
                         g_print("[RTSP] media-configure: building pipeline\n");
                         // 拿到顶层 pipeline，方便进一步调试
                         GstElement* element = gst_rtsp_media_get_element(media);
                         if (element) {
                             gchar* name = gst_element_get_name(element);
                             g_print("[RTSP] media pipeline element: %s\n", name ? name : "(null)");
                             g_free(name);
                         }
                     }), nullptr);

    if (gst_rtsp_server_attach(server, nullptr) == 0) {
        qWarning() << "[RTSP] attach failed (port busy or main loop problem)";
        g_object_unref(server);
        g_main_loop_unref(loop);
        return;
    }

    qInfo() << "[RTSP] listening at rtsp://" << ip_ << ":" << service_ << path_;
    g_main_loop_run(loop);

    g_object_unref(server);
    g_main_loop_unref(loop);
}

