#include "videorecorder.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QMutexLocker>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>     // av_gettime_relative
#include <libswscale/swscale.h>
}
// ===================== Letterbox 1080p (KeepAspect + Black Padding) =====================
// Output: RGB888 1920x1080
static inline QImage letterboxTo1080pRGB888(const QImage& in, bool fast)
{
    constexpr int OUT_W = 1920;
    constexpr int OUT_H = 1080;

    if (in.isNull()) return {};

    // 1) Ensure RGB888
    QImage src = in;
    if (src.format() != QImage::Format_RGB888) {
        src = src.convertToFormat(QImage::Format_RGB888);
        if (src.isNull()) return {};
    }

    // 2) Compute keep-aspect fit size
    const double sx = (double)OUT_W / (double)src.width();
    const double sy = (double)OUT_H / (double)src.height();
    const double s  = std::min(sx, sy);

    int w = std::max(1, (int)std::lround(src.width()  * s));
    int h = std::max(1, (int)std::lround(src.height() * s));

    // Safety clamp
    if (w > OUT_W) w = OUT_W;
    if (h > OUT_H) h = OUT_H;

    // 3) Scale (still RGB888)
    const auto tr = fast ? Qt::FastTransformation : Qt::SmoothTransformation;
    QImage scaled = src.scaled(w, h, Qt::IgnoreAspectRatio, tr);
    if (scaled.isNull()) return {};
    if (scaled.format() != QImage::Format_RGB888)
        scaled = scaled.convertToFormat(QImage::Format_RGB888);

    // 4) Create black canvas
    QImage out(OUT_W, OUT_H, QImage::Format_RGB888);
    out.fill(Qt::black);

    // 5) Center blit (row copy)
    const int offX = (OUT_W - w) / 2;
    const int offY = (OUT_H - h) / 2;

    const int srcStride = scaled.bytesPerLine();
    const int dstStride = out.bytesPerLine();
    const int rowBytes  = w * 3;

    const uchar* ps = scaled.constBits();
    uchar* pd = out.bits() + offY * dstStride + offX * 3;

    for (int y = 0; y < h; ++y) {
        memcpy(pd + y * dstStride, ps + y * srcStride, rowBytes);
    }

    return out;
}

// ========== 构造 / 析构 ==========

VideoRecorder::VideoRecorder(QObject* parent)
    : QObject(parent)
{
}

VideoRecorder::~VideoRecorder()
{
    QMutexLocker lk(&mutex_);
    if (encoderOpened_) {
        closeEncoderLocked();
    }
}

// ========== 路径配置 ==========

void VideoRecorder::receiveRecordOptions(myRecordOptions myOptions)
{
    QMutexLocker lk(&mutex_);

    videoRootDir_    = myOptions.recordPath;
    snapshotRootDir_ = myOptions.capturePath;

    myCaptureType = static_cast<ImageFormat>(myOptions.capturType);
    myRecordType  = static_cast<VideoContainer>(myOptions.recordType);

    currentOptions_.container = myRecordType;
    // fps/bitrate 仍用 currentOptions_ 默认（你需要的话可在 myRecordOptions 里补字段再同步）

    qDebug() << "[VideoRecorder] receiveRecordOptions:"
             << "videoRootDir =" << videoRootDir_
             << "snapshotRootDir =" << snapshotRootDir_
             << "captureType =" << int(myCaptureType)
             << "recordType =" << int(myRecordType)
             << "fps =" << currentOptions_.fps
             << "bitrateKbps =" << currentOptions_.bitrateKbps;
}

// ========== 单帧保存 ==========

void VideoRecorder::receiveFrame2Save(const QImage& img)
{
    QMutexLocker lk(&mutex_);

    if (img.isNull()) {
        qWarning() << "[VideoRecorder] receiveFrame2Save: empty image, skip.";
        return;
    }

    if (snapshotRootDir_.isEmpty()) {
        emit sendMSG2ui(QStringLiteral("[VideoRecorder] 单帧保存失败：截图根目录未设置"));
        return;
    }

    ImageFormat fmt = myCaptureType;
    QString path = makeSnapshotFilePathLocked(fmt);
    if (path.isEmpty()) {
        emit sendMSG2ui(QStringLiteral("[VideoRecorder] 单帧保存失败：生成文件路径失败"));
        return;
    }

    // ★关键：保存用 1080p letterbox（不变形、不裁剪、有黑边）
    QImage out = letterboxTo1080pRGB888(img, /*fast=*/false);
    if (out.isNull()) {
        emit sendMSG2ui(QStringLiteral("[VideoRecorder] 单帧保存失败：letterbox 转换失败"));
        return;
    }

    const char* fmtStr = imageFormatToQtString(fmt);
    if (!out.save(path, fmtStr)) {
        emit sendMSG2ui(QStringLiteral("[VideoRecorder] 单帧保存失败：%1").arg(path));
        return;
    }

    emit snapshotSaved(path);
    emit sendMSG2ui(QStringLiteral("[VideoRecorder] saved snapshot to %1").arg(path));
}

// ========== 录制帧输入 ==========

void VideoRecorder::receiveFrame2Record(const QImage& img)
{
    QMutexLocker lk(&mutex_);

    if (!recording_) return;
    if (img.isNull()) return;

    // 缓存最近一帧（如果你后面要做“录制中截图”也有用）
    lastFrame_ = img;

    if (!encoderOpened_) {
        frameIndex_ = 0;
        lastPtsMs_ = 0;
        recStartUs_ = 0;

        if (!openEncoderLockedForImage(img)) {
            recording_ = false;
            encoderOpened_ = false;
            emit sendMSG2ui(QStringLiteral("[VideoRecorder] 视频录制初始化失败"));
            return;
        }

        encoderOpened_ = true;
        emit recordingStarted(currentRecordingPath_);
    }

    if (!encodeImageLocked(img)) {
        emit sendMSG2ui(QStringLiteral("[VideoRecorder] 视频编码失败"));
    }
}

// ========== 路径生成 ==========

QString VideoRecorder::makeVideoFilePathLocked(const VideoOptions& opt) const
{
    if (videoRootDir_.isEmpty())
        return QString();

    const QDate today = QDate::currentDate();
    const QString dateStr = today.toString("yyyy-MM-dd");

    QDir root(videoRootDir_);
    if (!root.exists()) {
        if (!root.mkpath(".")) {
            qWarning() << "[VideoRecorder] mkpath video root failed:" << videoRootDir_;
            return QString();
        }
    }

    QDir dateDir(root.filePath(dateStr));
    if (!dateDir.exists()) {
        if (!dateDir.mkpath(".")) {
            qWarning() << "[VideoRecorder] mkpath date dir failed:" << dateDir.absolutePath();
            return QString();
        }
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString prefix = now.toString("yyyy-MM-dd_hh-mm-ss");
    const QString ext = containerToExtension(opt.container);

    return dateDir.filePath(prefix + "." + ext);
}

QString VideoRecorder::makeSnapshotFilePathLocked(ImageFormat fmt) const
{
    if (snapshotRootDir_.isEmpty())
        return QString();

    const QDate today = QDate::currentDate();
    const QString dateStr = today.toString("yyyy-MM-dd");

    QDir root(snapshotRootDir_);
    if (!root.exists()) {
        if (!root.mkpath(".")) {
            qWarning() << "[VideoRecorder] mkpath snapshot root failed:" << snapshotRootDir_;
            return QString();
        }
    }

    QDir dateDir(root.filePath(dateStr));
    if (!dateDir.exists()) {
        if (!dateDir.mkpath(".")) {
            qWarning() << "[VideoRecorder] mkpath snapshot date dir failed:" << dateDir.absolutePath();
            return QString();
        }
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString prefix = now.toString("yyyy-MM-dd_hh-mm-ss_zzz");

    QString ext = "png";
    if (fmt == ImageFormat::JPG) ext = "jpg";
    else if (fmt == ImageFormat::BMP) ext = "bmp";

    return dateDir.filePath(prefix + "." + ext);
}

const char* VideoRecorder::imageFormatToQtString(ImageFormat fmt)
{
    switch (fmt) {
    case ImageFormat::PNG: return "PNG";
    case ImageFormat::JPG: return "JPG";
    case ImageFormat::BMP: return "BMP";
    }
    return "PNG";
}

QString VideoRecorder::containerToExtension(VideoContainer c)
{
    switch (c) {
    case VideoContainer::MP4: return QStringLiteral("mp4");
    case VideoContainer::AVI: return QStringLiteral("avi");
    }
    return QStringLiteral("mp4");
}

// ========== start/stop ==========

void VideoRecorder::startRecording()
{
    QMutexLocker lk(&mutex_);

    if (videoRootDir_.isEmpty()) {
        emit sendMSG2ui(QStringLiteral("[VideoRecorder] 视频根目录未设置"));
        return;
    }
    if (recording_) {
        emit sendMSG2ui(QStringLiteral("[VideoRecorder] 当前已在录制中"));
        return;
    }

    currentOptions_.container = myRecordType;

    // 强烈建议：为避免 AVI+H264 在 PC 上黑屏/时长异常，默认强制 MP4
    // 如果你必须支持 AVI，请在 openEncoderLockedForImage() 里用 MPEG4 代替 H264
    if (currentOptions_.container == VideoContainer::AVI) {
        emit sendMSG2ui(QStringLiteral("[VideoRecorder] AVI 容器兼容性较差，已自动切换为 MP4"));
        currentOptions_.container = VideoContainer::MP4;
    }

    recording_ = true;
    encoderOpened_ = false;
    currentRecordingPath_.clear();

    emit sendMSG2ui(QStringLiteral("[VideoRecorder] startRecording"));
}

void VideoRecorder::stopRecording()
{
    QMutexLocker lk(&mutex_);

    if (!recording_) return;

    // flush
    if (encoderOpened_ && codecCtx_) {
        avcodec_send_frame(codecCtx_, nullptr);

        while (true) {
            int ret = avcodec_receive_packet(codecCtx_, pkt_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            // packet duration（毫秒 time_base）
            pkt_->duration = qMax<int64_t>(1, (int64_t)(1000.0 / encFps_));

            av_packet_rescale_ts(pkt_, codecCtx_->time_base, videoStream_->time_base);
            pkt_->stream_index = videoStream_->index;
            av_interleaved_write_frame(fmtCtx_, pkt_);
            av_packet_unref(pkt_);
        }
    }

    QString finishedPath = currentRecordingPath_;
    closeEncoderLocked();

    recording_ = false;
    encoderOpened_ = false;
    currentRecordingPath_.clear();

    if (!finishedPath.isEmpty()) {
        emit recordingStopped(finishedPath);
        emit sendMSG2ui(QStringLiteral("[VideoRecorder] 录像已保存到：%1").arg(finishedPath));
    }
}

// ========== 核心：打开编码器 ==========

bool VideoRecorder::openEncoderLockedForImage(const QImage &img)
{
    static bool ffInited = false;
    if (!ffInited) {
        av_log_set_level(AV_LOG_ERROR);
        avformat_network_init();
        ffInited = true;
    }

    encWidth_  = 1920;
    encHeight_ = 1080;

    encFps_    = (currentOptions_.fps > 0) ? currentOptions_.fps : 22.0;

    currentRecordingPath_ = makeVideoFilePathLocked(currentOptions_);
    if (currentRecordingPath_.isEmpty()) {
        qWarning() << "[VideoRecorder] makeVideoFilePathLocked failed.";
        return false;
    }

    int ret = avformat_alloc_output_context2(
        &fmtCtx_, nullptr, nullptr,
        currentRecordingPath_.toUtf8().constData()
        );
    if (!fmtCtx_) {
        qWarning() << "[VideoRecorder] avformat_alloc_output_context2 failed, ret =" << ret;
        return false;
    }

    // codec：MP4 走 H264
    AVCodecID codecId = AV_CODEC_ID_H264;

    // 如果你未来要真的支持 AVI，把 startRecording 的强制 MP4 去掉，然后这里用 MPEG4：
    // if (currentOptions_.container == VideoContainer::AVI) codecId = AV_CODEC_ID_MPEG4;

    const AVCodec *codec = avcodec_find_encoder(codecId);
    if (!codec) {
        qWarning() << "[VideoRecorder] cannot find encoder.";
        return false;
    }

    videoStream_ = avformat_new_stream(fmtCtx_, codec);
    if (!videoStream_) {
        qWarning() << "[VideoRecorder] avformat_new_stream failed.";
        return false;
    }
    videoStream_->id = fmtCtx_->nb_streams - 1;

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        qWarning() << "[VideoRecorder] avcodec_alloc_context3 failed.";
        return false;
    }

    codecCtx_->codec_id = codecId;
    codecCtx_->width    = encWidth_;
    codecCtx_->height   = encHeight_;
    codecCtx_->pix_fmt  = AV_PIX_FMT_YUV420P;
    codecCtx_->bit_rate = (int64_t)currentOptions_.bitrateKbps * 1000LL;

    // 关键修复：用毫秒 time_base，后续 pts 用真实时间（避免时长漂）
    codecCtx_->time_base = AVRational{1, 1000}; // 1 tick = 1ms
    codecCtx_->framerate = AVRational{(int)encFps_, 1};

    codecCtx_->gop_size = 25;
    codecCtx_->max_b_frames = 0;

    if (codecCtx_->codec_id == AV_CODEC_ID_H264) {
        av_opt_set(codecCtx_->priv_data, "preset", "veryfast", 0);
        av_opt_set(codecCtx_->priv_data, "tune",   "zerolatency", 0);
    }

    if (fmtCtx_->oformat->flags & AVFMT_GLOBALHEADER) {
        codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0) {
        qWarning() << "[VideoRecorder] avcodec_open2 failed, ret =" << ret;
        return false;
    }

    ret = avcodec_parameters_from_context(videoStream_->codecpar, codecCtx_);
    if (ret < 0) {
        qWarning() << "[VideoRecorder] avcodec_parameters_from_context failed, ret =" << ret;
        return false;
    }

    // stream 时间基/帧率提示（播放器推时长更稳）
    videoStream_->time_base = codecCtx_->time_base;
    videoStream_->avg_frame_rate = AVRational{(int)encFps_, 1};
    videoStream_->r_frame_rate   = AVRational{(int)encFps_, 1};

    // frame / packet
    frame_ = av_frame_alloc();
    pkt_   = av_packet_alloc();
    if (!frame_ || !pkt_) {
        qWarning() << "[VideoRecorder] alloc frame/packet failed.";
        return false;
    }

    frame_->format = codecCtx_->pix_fmt;
    frame_->width  = codecCtx_->width;
    frame_->height = codecCtx_->height;

    ret = av_frame_get_buffer(frame_, 32);
    if (ret < 0) {
        qWarning() << "[VideoRecorder] av_frame_get_buffer failed, ret =" << ret;
        return false;
    }

    // sws：固定按首帧尺寸创建（后续输入若变尺寸，在 encode 里强制缩放）
    swsCtx_ = sws_getContext(encWidth_, encHeight_, AV_PIX_FMT_RGB24,
                             encWidth_, encHeight_, AV_PIX_FMT_YUV420P,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsCtx_) {
        qWarning() << "[VideoRecorder] sws_getContext failed.";
        return false;
    }

    if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmtCtx_->pb, currentRecordingPath_.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            qWarning() << "[VideoRecorder] avio_open failed, ret =" << ret;
            return false;
        }
    }

    // MP4 faststart（可选，增强兼容性）
    AVDictionary* muxOpts = nullptr;
    if (currentOptions_.container == VideoContainer::MP4) {
        av_dict_set(&muxOpts, "movflags", "+faststart", 0);
    }

    ret = avformat_write_header(fmtCtx_, &muxOpts);
    av_dict_free(&muxOpts);

    if (ret < 0) {
        qWarning() << "[VideoRecorder] avformat_write_header failed, ret =" << ret;
        return false;
    }

    // 初始化真实时间基准
    recStartUs_ = (qint64)av_gettime_relative();
    lastPtsMs_ = 0;

    qDebug().noquote() << "[VideoRecorder] start writing to " << currentRecordingPath_
                       << "enc=" << encWidth_ << "x" << encHeight_
                       << "fps(meta)=" << encFps_;
    return true;
}

// ========== 核心：编码一帧 ==========

bool VideoRecorder::encodeImageLocked(const QImage &img)
{
    if (!fmtCtx_ || !codecCtx_ || !frame_ || !swsCtx_ || !videoStream_)
        return false;

    // 1) 确保 RGB888
    QImage rgb = img;
    if (rgb.format() != QImage::Format_RGB888) {
        rgb = rgb.convertToFormat(QImage::Format_RGB888);
    }

    // ★关键：录制用 1080p letterbox（不变形、不裁剪、有黑边）
    QImage rgb1080 = letterboxTo1080pRGB888(rgb, /*fast=*/true);
    if (rgb1080.isNull() || rgb1080.width() != encWidth_ || rgb1080.height() != encHeight_) {
        qWarning() << "[VideoRecorder] letterboxTo1080p failed.";
        return false;
    }


    // 关键修复2：复用 AVFrame 前必须可写（避免偶发黑帧）
    int ret = av_frame_make_writable(frame_);
    if (ret < 0) {
        qWarning() << "[VideoRecorder] av_frame_make_writable failed, ret =" << ret;
        return false;
    }

    const uint8_t *srcData[1] = { rgb1080.bits() };
    int srcStride[1]          = { rgb1080.bytesPerLine() };


    // 2) RGB -> YUV420P
    ret = sws_scale(swsCtx_,
                    srcData, srcStride,
                    0, encHeight_,
                    frame_->data, frame_->linesize);
    if (ret <= 0) {
        qWarning() << "[VideoRecorder] sws_scale failed, ret =" << ret;
        return false;
    }

    // 关键修复3：真实时间 PTS（毫秒），解决“10秒显示1分钟”
    if (recStartUs_ <= 0) recStartUs_ = (qint64)av_gettime_relative();
    qint64 nowUs = (qint64)av_gettime_relative();
    qint64 ms = (nowUs - recStartUs_) / 1000;   // 毫秒

    // 单调递增（避免相等/倒退导致播放器时长计算异常）
    if (ms <= lastPtsMs_) ms = lastPtsMs_ + 1;
    lastPtsMs_ = ms;

    frame_->pts = (int64_t)ms;

    // 4) 编码
    ret = avcodec_send_frame(codecCtx_, frame_);
    if (ret < 0) {
        qWarning() << "[VideoRecorder] avcodec_send_frame failed, ret =" << ret;
        return false;
    }

    while (true) {
        ret = avcodec_receive_packet(codecCtx_, pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            qWarning() << "[VideoRecorder] avcodec_receive_packet failed, ret =" << ret;
            return false;
        }

        // 给 packet 补 duration（毫秒 time_base）
        pkt_->duration = qMax<int64_t>(1, (int64_t)(1000.0 / encFps_));

        av_packet_rescale_ts(pkt_, codecCtx_->time_base, videoStream_->time_base);
        pkt_->stream_index = videoStream_->index;

        ret = av_interleaved_write_frame(fmtCtx_, pkt_);
        av_packet_unref(pkt_);
        if (ret < 0) {
            qWarning() << "[VideoRecorder] av_interleaved_write_frame failed, ret =" << ret;
            return false;
        }
    }

    frameIndex_++;
    return true;
}

// ========== 关闭编码器 ==========

void VideoRecorder::closeEncoderLocked()
{
    if (fmtCtx_) {
        av_write_trailer(fmtCtx_);
    }

    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }

    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }

    if (pkt_) {
        av_packet_free(&pkt_);
        pkt_ = nullptr;
    }

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }

    if (fmtCtx_) {
        if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE) && fmtCtx_->pb) {
            avio_closep(&fmtCtx_->pb);
        }
        avformat_free_context(fmtCtx_);
        fmtCtx_ = nullptr;
    }

    videoStream_ = nullptr;

    recStartUs_ = 0;
    lastPtsMs_ = 0;

    qDebug() << "[VideoRecorder] encoder closed.";
}
