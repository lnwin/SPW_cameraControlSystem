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

    // -------- 1) 打开输入（尽量低缓冲） --------
    AVFormatContext* fmt = nullptr;
    AVDictionary* inOpts = nullptr;
    av_dict_set(&inOpts, "rtsp_transport", "udp", 0);
    av_dict_set(&inOpts, "fflags", "+nobuffer+discardcorrupt", 0);
    av_dict_set(&inOpts, "flags", "low_delay", 0);
    av_dict_set(&inOpts, "max_delay", "0", 0);
    av_dict_set(&inOpts, "probesize", "32", 0);
    av_dict_set(&inOpts, "analyzeduration", "0", 0);
    av_dict_set(&inOpts, "fpsprobesize", "0", 0);
    av_dict_set(&inOpts, "use_wallclock_as_timestamps", "1", 0);

    int r = avformat_open_input(&fmt, url_.toUtf8().constData(), nullptr, &inOpts);
    av_dict_free(&inOpts);
    if (r < 0){
        emit logLine(QString("[RTSP] open_input: %1").arg(err2str_q(r)));
        return;
    }

    fmt->flags |= AVFMT_FLAG_NOBUFFER;

    r = avformat_find_stream_info(fmt, nullptr);
    if (r < 0){
        emit logLine(QString("[RTSP] find_stream_info: %1").arg(err2str_q(r)));
        avformat_close_input(&fmt);
        return;
    }

    int vindex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vindex < 0){
        emit logLine("[RTSP] no video stream");
        avformat_close_input(&fmt);
        return;
    }

    AVStream* vs = fmt->streams[vindex];
    const AVCodec* dec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!dec){
        emit logLine("[RTSP] no decoder");
        avformat_close_input(&fmt);
        return;
    }

    AVCodecContext* cc = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(cc, vs->codecpar);
    cc->flags  |= AV_CODEC_FLAG_LOW_DELAY;
#ifdef AV_CODEC_FLAG2_FAST
    cc->flags2 |= AV_CODEC_FLAG2_FAST;
#endif
    cc->thread_count = 1;                 // 避免帧并行引入缓冲
    cc->pkt_timebase = vs->time_base;

    AVDictionary* decOpts = nullptr;
    av_dict_set(&decOpts, "flags", "low_delay", 0);
    av_dict_set(&decOpts, "threads", "1", 0);
    r = avcodec_open2(cc, dec, &decOpts);
    av_dict_free(&decOpts);
    if (r < 0){
        emit logLine(QString("[RTSP] avcodec_open2: %1").arg(err2str_q(r)));
        avcodec_free_context(&cc);
        avformat_close_input(&fmt);
        return;
    }

    // -------- 2) SWS / 帧缓冲 --------
    SwsContext* sws = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt  = av_packet_alloc();

    const int outW_fixed = 1224;
    const int outH_fixed = 1024;

    auto recreateSwsIfNeeded = [&](int srcW, int srcH, AVPixelFormat srcFmt)->bool{
        static int cachedSrcW=-1, cachedSrcH=-1; static AVPixelFormat cachedSrcFmt=AV_PIX_FMT_NONE;
        if (!sws || srcW!=cachedSrcW || srcH!=cachedSrcH || srcFmt!=cachedSrcFmt){
            if (sws) sws_freeContext(sws);
            sws = sws_getContext(srcW, srcH, srcFmt,
                                 outW_fixed, outH_fixed, AV_PIX_FMT_RGB24, // 直接变成 RGB24 便于构造 QImage
                                 SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
            cachedSrcW=srcW; cachedSrcH=srcH; cachedSrcFmt=srcFmt;
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

    const int64_t LAG_BUDGET_MS = 80;
    bool catching_up = false;

    auto pkt_ts_ms = [&](const AVPacket* p)->int64_t{
        int64_t pts = (p->pts != AV_NOPTS_VALUE) ? p->pts : p->dts;
        if (pts == AV_NOPTS_VALUE) return INT64_MIN;
        return av_rescale_q(pts, vs->time_base, AVRational{1,1000});
    };

    emit logLine(QString("[RTSP] started: %1").arg(url_));

    // -------- 3) 主循环 --------
    while (!stopFlag_.load(std::memory_order_acquire)) {
        r = av_read_frame(fmt, pkt);
        if (r == AVERROR_EOF) break;
        if (r < 0) continue;

        if (pkt->stream_index != vindex){ av_packet_unref(pkt); continue; }

        // 落后追赶控制
        int64_t pkt_ms = pkt_ts_ms(pkt);
        if (pkt_ms != INT64_MIN){
            int64_t now_ms = now_us() / 1000;
            int64_t lag = now_ms - pkt_ms;
            if (lag > LAG_BUDGET_MS && !catching_up){
                catching_up = true;
                cc->skip_frame = AVDISCARD_NONREF;
            }
        }
        bool pkt_is_key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        if (catching_up && pkt_is_key){
            catching_up = false;
            cc->skip_frame = AVDISCARD_DEFAULT;
        }

        if (avcodec_send_packet(cc, pkt) == 0){
            while (!stopFlag_.load(std::memory_order_acquire)) {
                r = avcodec_receive_frame(cc, frame);
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                if (r < 0) break;

                if (catching_up && is_keyframe(frame)) {
                    catching_up = false;
                    cc->skip_frame = AVDISCARD_DEFAULT;
                }

                if (!recreateSwsIfNeeded(cc->width, cc->height, cc->pix_fmt)){
                    emit logLine("[RTSP] sws_getContext failed");
                    goto L_EXIT;
                }

                // 转为 RGB24 并发 QImage（1224x1024）
                QImage rgb(outW_fixed, outH_fixed, QImage::Format_RGB888);
                uint8_t* dstData[4] = { rgb.bits(), nullptr, nullptr, nullptr };
                int      dstLines[4] = { (int)rgb.bytesPerLine(), 0, 0, 0 };

                sws_scale(sws, frame->data, frame->linesize, 0, cc->height, dstData, dstLines);

                emit frameReady(rgb.copy()); // 拷贝一份，避免复用缓冲导致撕裂
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
}
