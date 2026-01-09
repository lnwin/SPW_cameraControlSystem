#include "rtspviewerqt.h"

#include <QDebug>
#include <QCoreApplication>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

static inline QString err2str_q(int e){ char b[256]; av_strerror(e,b,sizeof(b)); return QString::fromLocal8Bit(b); }

// 兼容：老版本没有 av_gettime_relative，用 av_gettime 代替
#ifndef av_gettime_relative
#define av_gettime_relative av_gettime
#endif
static inline int64_t now_us(){ return av_gettime_relative(); }

RtspViewerQt::RtspViewerQt(QObject* parent)
    : QThread(parent)
{
    av_log_set_level(AV_LOG_ERROR);
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
    if (url_.isEmpty()){
        emit logLine("[RTSP] url is empty");
        return;
    }

    stopFlag_.store(false, std::memory_order_release);

RECONNECT:
    if (stopFlag_.load(std::memory_order_acquire)) return;

    // -------- 1) 打开输入（百兆 UDP：允许小抖动缓冲） --------
    AVFormatContext* fmt = nullptr;
    AVDictionary* inOpts = nullptr;

    av_dict_set(&inOpts, "rtsp_transport", "udp", 0);
    av_dict_set(&inOpts, "max_delay", "400000", 0);          // 400ms：更能吸收百兆抖动
    av_dict_set(&inOpts, "reorder_queue_size", "64", 0);     // 关键：给一点重排序空间（UDP 很有用）

    av_dict_set(&inOpts, "buffer_size", "8388608", 0);       // 8MB
    av_dict_set(&inOpts, "recv_buffer_size", "8388608", 0);  // 8MB
    av_dict_set(&inOpts, "fifo_size", "16000000", 0);        // 16MB
    av_dict_set(&inOpts, "fflags", "+discardcorrupt", 0);
    av_dict_set(&inOpts, "flags", "low_delay", 0);
    av_dict_set(&inOpts, "pkt_size", "1200", 0);

    av_dict_set(&inOpts, "overrun_nonfatal", "1", 0);

    av_dict_set(&inOpts, "use_wallclock_as_timestamps", "1", 0);

    int r = avformat_open_input(&fmt, url_.toUtf8().constData(), nullptr, &inOpts);
    av_dict_free(&inOpts);
    if (r < 0){
        emit logLine(QString("[RTSP] open_input: %1").arg(err2str_q(r)));
        QThread::msleep(300);
        goto RECONNECT;
    }

    r = avformat_find_stream_info(fmt, nullptr);
    if (r < 0){
        emit logLine(QString("[RTSP] find_stream_info: %1").arg(err2str_q(r)));
        avformat_close_input(&fmt);
        QThread::msleep(300);
        goto RECONNECT;
    }

    int vindex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vindex < 0){
        emit logLine("[RTSP] no video stream");
        avformat_close_input(&fmt);
        QThread::msleep(300);
        goto RECONNECT;
    }

    AVStream* vs = fmt->streams[vindex];
    const AVCodec* dec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!dec){
        emit logLine("[RTSP] no decoder");
        avformat_close_input(&fmt);
        QThread::msleep(300);
        goto RECONNECT;
    }

    AVCodecContext* cc = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(cc, vs->codecpar);

    cc->flags |= AV_CODEC_FLAG_LOW_DELAY;
#ifdef AV_CODEC_FLAG2_FAST
    cc->flags2 |= AV_CODEC_FLAG2_FAST;
#endif

    // ===================== 解码线程：不要锁死 1 =====================
    // 经验：用 (CPU核心数 - 1)，至少 2 线程；避免把 UI/其它线程饿死
    const int tc = std::max(2, QThread::idealThreadCount() - 1);
    cc->thread_count = tc;

    cc->pkt_timebase = vs->time_base;

    AVDictionary* decOpts = nullptr;
    av_dict_set(&decOpts, "flags", "low_delay", 0);
    av_dict_set(&decOpts, "thread_type", "slice", 0);
    const QByteArray tcStr = QByteArray::number(tc);
    av_dict_set(&decOpts, "threads", tcStr.constData(), 0);


    r = avcodec_open2(cc, dec, &decOpts);
    av_dict_free(&decOpts);

    emit logLine(QString("[RTSP] decoder threads=%1").arg(tc));


    if (r < 0){
        emit logLine(QString("[RTSP] avcodec_open2: %1").arg(err2str_q(r)));
        avcodec_free_context(&cc);
        avformat_close_input(&fmt);
        QThread::msleep(300);
        goto RECONNECT;
    }

    // -------- 2) SWS / 帧缓冲（三缓冲，避免每帧 new + copy） --------
    SwsContext* sws = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt  = av_packet_alloc();

    int outW_fixed = 0; // 0=跟随源；你也可以固定 1224
    int outH_fixed = 0;

    auto recreateSwsIfNeeded = [&](int srcW, int srcH, AVPixelFormat srcFmt)->bool{
        int outW = (outW_fixed > 0) ? outW_fixed : srcW;
        int outH = (outH_fixed > 0) ? outH_fixed : srcH;

        static int cachedSrcW=-1, cachedSrcH=-1, cachedOutW=-1, cachedOutH=-1;
        static AVPixelFormat cachedSrcFmt=AV_PIX_FMT_NONE;

        if (!sws || srcW!=cachedSrcW || srcH!=cachedSrcH || srcFmt!=cachedSrcFmt ||
            outW!=cachedOutW || outH!=cachedOutH)
        {
            if (sws) sws_freeContext(sws);
            sws = sws_getContext(srcW, srcH, srcFmt,
                                 outW, outH, AV_PIX_FMT_RGB24,
                                 SWS_POINT, nullptr, nullptr, nullptr);
            cachedSrcW=srcW; cachedSrcH=srcH; cachedSrcFmt=srcFmt;
            cachedOutW=outW; cachedOutH=outH;

            // 重新分配三缓冲
        }
        return sws != nullptr;
    };

    auto is_keyframe = [&](const AVFrame* f)->bool {
#ifdef AV_FRAME_FLAG_KEY
        return (f->flags & AV_FRAME_FLAG_KEY) != 0;
#else
        return f->pict_type == AV_PICTURE_TYPE_I || f->pict_type == AV_PICTURE_TYPE_SI;
#endif
    };

    auto pkt_ts_ms = [&](const AVPacket* p)->int64_t{
        int64_t pts = (p->pts != AV_NOPTS_VALUE) ? p->pts : p->dts;
        if (pts == AV_NOPTS_VALUE) return INT64_MIN;
        return av_rescale_q(pts, vs->time_base, AVRational{1,1000});
    };

    // ★ 更温和的追赶：短抖动不立刻跳帧
    const int64_t LAG_BUDGET_MS = 250;
    int lag_over_cnt = 0;
    bool catching_up = false;

    // 三缓冲：QSharedPointer 管生命周期，避免 copy
    QSharedPointer<QImage> buf[3];
    int bufW = -1, bufH = -1;
    int bufIdx = 0;

    auto ensureBuffers = [&](int w, int h){
        if (w == bufW && h == bufH && buf[0] && buf[1] && buf[2]) return;
        bufW = w; bufH = h;
        for (int i = 0; i < 3; ++i){
            buf[i] = QSharedPointer<QImage>::create(bufW, bufH, QImage::Format_RGB888);
        }
        bufIdx = 0;
        emit logLine(QString("[RTSP] buffers recreated: %1x%2").arg(bufW).arg(bufH));
    };

    emit logLine(QString("[RTSP] started: %1").arg(url_));

    int read_err_cnt = 0;
    const int READ_ERR_RECONNECT = 50; // 连续错误次数阈值，防止长时间卡死

    // -------- 3) 主循环 --------
    while (!stopFlag_.load(std::memory_order_acquire)) {
        r = av_read_frame(fmt, pkt);
        if (r == AVERROR_EOF) break;
        if (r < 0) {
            // 网络/服务器抖动：计数后重连
            if (++read_err_cnt > READ_ERR_RECONNECT) {
                emit logLine("[RTSP] read_frame error too many, reconnect...");
                break;
            }
            QThread::msleep(2);
            continue;
        }
        read_err_cnt = 0;

        if (pkt->stream_index != vindex){
            av_packet_unref(pkt);
            continue;
        }

        // lag 估计
        int64_t pkt_ms = pkt_ts_ms(pkt);
        if (pkt_ms != INT64_MIN){
            int64_t now_ms = now_us() / 1000;
            int64_t lag = now_ms - pkt_ms;

            if (lag > LAG_BUDGET_MS) lag_over_cnt++;
            else lag_over_cnt = 0;

            // 连续超阈值才进入 catching_up，避免短抖动就跳
            if (!catching_up && lag_over_cnt >= 10){
                catching_up = true;
                cc->skip_frame = AVDISCARD_NONREF;
                emit logLine(QString("[RTSP] catching up (lag=%1ms)").arg(lag));
            }
        }

        bool pkt_is_key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        if (catching_up && pkt_is_key){
            catching_up = false;
            lag_over_cnt = 0;
            cc->skip_frame = AVDISCARD_DEFAULT;
            emit logLine("[RTSP] catch-up done (keyframe)");
        }

        if (avcodec_send_packet(cc, pkt) == 0){
            while (!stopFlag_.load(std::memory_order_acquire)) {
                r = avcodec_receive_frame(cc, frame);
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                if (r < 0) break;

                if (catching_up && is_keyframe(frame)) {
                    catching_up = false;
                    lag_over_cnt = 0;
                    cc->skip_frame = AVDISCARD_DEFAULT;
                }

                if (!recreateSwsIfNeeded(cc->width, cc->height, cc->pix_fmt)){
                    emit logLine("[RTSP] sws_getContext failed");
                    goto L_EXIT;
                }

                int outW = (outW_fixed > 0) ? outW_fixed : cc->width;
                int outH = (outH_fixed > 0) ? outH_fixed : cc->height;

                ensureBuffers(outW, outH);

                // 取一个 buffer 写入
                QSharedPointer<QImage> img = buf[bufIdx];
                bufIdx = (bufIdx + 1) % 3;

                uint8_t* dstData[4]  = { img->bits(), nullptr, nullptr, nullptr };
                int      dstLines[4] = { (int)img->bytesPerLine(), 0, 0, 0 };

                sws_scale(sws, frame->data, frame->linesize, 0, cc->height, dstData, dstLines);

                // ★ 无 copy：直接把 shared_ptr 发给 UI
                emit frameReady(img);
            }
        }

        av_packet_unref(pkt);
    }

L_EXIT:
    av_packet_free(&pkt);
    av_frame_free(&frame);
    if (sws) sws_freeContext(sws);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);

    emit logLine("[RTSP] stopped");

    if (!stopFlag_.load(std::memory_order_acquire)) {
        QThread::msleep(300);
        goto RECONNECT;
    }
}
