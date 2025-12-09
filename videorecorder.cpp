#include "videorecorder.h"

#include <QDate>
#include <QTime>
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
#include <libswscale/swscale.h>
}

// ========== 构造 / 析构 ==========

VideoRecorder::VideoRecorder(QObject* parent)
    : QObject(parent)
{
}

VideoRecorder::~VideoRecorder()
{

}

// ========== 路径配置 ==========

void VideoRecorder::receiveRecordOptions(myRecordOptions myOptions)
{
    QMutexLocker lk(&mutex_);

    videoRootDir_    = myOptions.recordPath;
    snapshotRootDir_ = myOptions.capturePath;

    myCaptureType    = static_cast<ImageFormat>(myOptions.capturType);
    myRecordType     = static_cast<VideoContainer>(myOptions.recordType);

    // 同步到 currentOptions_
    currentOptions_.container = myRecordType;
    // currentOptions_.fps 这里不动，继续用默认 22
    // 如果以后 myRecordOptions 里也有 fps，再在这里改就行

    qDebug() << "[VideoRecorder] receiveRecordOptions:"
             << "videoRootDir =" << videoRootDir_
             << "snapshotRootDir =" << snapshotRootDir_
             << "captureType =" << int(myCaptureType)
             << "recordType =" << int(myRecordType)
             << "fps =" << currentOptions_.fps;
}


void VideoRecorder::receiveFrame2Save(const QImage& img)
{
    QMutexLocker lk(&mutex_);

    if (img.isNull()) {
        qWarning() << "[VideoRecorder] receiveFrame2Save: empty image, skip.";
        return;
    }

    if (snapshotRootDir_.isEmpty()) {
        qWarning() << "[VideoRecorder] receiveFrame2Save: snapshotRootDir_ is empty.";
#ifdef QT_DEBUG
        emit errorOccurred(tr("单帧保存失败：未设置截图根目录"));
#endif
        return;
    }

    // 使用当前配置的截图格式
    ImageFormat fmt = myCaptureType;

    // 生成完整文件路径（内部已经按日期创建子目录）
    QString path = makeSnapshotFilePathLocked(fmt);
    if (path.isEmpty()) {
        qWarning() << "[VideoRecorder] receiveFrame2Save: makeSnapshotFilePathLocked failed.";
        emit sendMSG2ui(tr("单帧保存失败：生成文件路径失败"));
        return;
    }

    const char* fmtStr = imageFormatToQtString(fmt);
    if (!img.save(path, fmtStr)) {
        qWarning() << "[VideoRecorder] receiveFrame2Save: save image failed:" << path;
        emit sendMSG2ui(tr("单帧保存失败：%1").arg(path));
        return;
    }


    QString msg = QStringLiteral("[VideoRecorder] saved snapshot to %1").arg(path);

    emit sendMSG2ui(msg);
}
void VideoRecorder::receiveFrame2Record(const QImage& img)
{
    QMutexLocker lk(&mutex_);

    if (!recording_) {
        return;  // 没在录制，直接丢掉
    }

    if (img.isNull()) {
        qWarning() << "[VideoRecorder] receiveFrame2Record: empty frame.";
        return;
    }

    // 第一次收到帧时，根据图像尺寸初始化 FFmpeg 编码器
    if (!encoderOpened_) {
        if (!openEncoderLockedForImage(img)) {
            recording_ = false;
            encoderOpened_ = false;
            emit sendMSG2ui(QStringLiteral("[VideoRecorder] 视频录制初始化失败"));
            return;
        }
        encoderOpened_ = true;
        emit recordingStarted(currentRecordingPath_);
    }

    // 编码当前帧
    if (!encodeImageLocked(img)) {
        emit sendMSG2ui(QStringLiteral("[VideoRecorder] 视频编码失败"));
    }
}

// ========== 内部辅助函数：路径生成 ==========

QString VideoRecorder::makeVideoFilePathLocked(const VideoOptions& opt) const
{
    if (videoRootDir_.isEmpty())
        return QString();

    // 日期到天的子目录：YYYY-MM-DD
    const QDate today      = QDate::currentDate();
    const QString dateStr  = today.toString("yyyy-MM-dd");

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

    // 文件名：YYYY-MM-DD_hh-mm-ss.ext
    const QDateTime now   = QDateTime::currentDateTime();
    const QString prefix  = now.toString("yyyy-MM-dd_hh-mm-ss");
    const QString ext     = containerToExtension(opt.container);

    const QString fileName = prefix + "." + ext;
    return dateDir.filePath(fileName);
}

QString VideoRecorder::makeSnapshotFilePathLocked(ImageFormat fmt) const
{
    if (snapshotRootDir_.isEmpty())
        return QString();

    // 日期到天的子目录：YYYY-MM-DD
    const QDate today      = QDate::currentDate();
    const QString dateStr  = today.toString("yyyy-MM-dd");

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

    // 文件名：YYYY-MM-DD_hh-mm-ss_zzz.ext
    const QDateTime now   = QDateTime::currentDateTime();
    const QString prefix  = now.toString("yyyy-MM-dd_hh-mm-ss_zzz");

    const char* fmtStr = imageFormatToQtString(fmt);
    Q_UNUSED(fmtStr);

    QString ext = "png";
    if (fmt == ImageFormat::JPG)      ext = "jpg";
    else if (fmt == ImageFormat::BMP) ext = "bmp";

    const QString fileName = prefix + "." + ext;
    return dateDir.filePath(fileName);
}

// ========== 静态工具函数 ==========

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

    // 确保 container 跟 myRecordType 一致
    currentOptions_.container = myRecordType;
    // currentOptions_.fps 仍然是 22，除非你以后改

    recording_ = true;
    currentRecordingPath_.clear();
    // ⚠️ 真正打开 FFmpeg 的逻辑建议放到 receiveFrame2Record 首帧里做，
    //    因为那时候才能拿到图像宽高

    qDebug() << "[VideoRecorder] startRecording, fps =" << currentOptions_.fps
             << "container =" << int(currentOptions_.container);
}
void VideoRecorder::stopRecording()
{
    QMutexLocker lk(&mutex_);

    if (!recording_) {
        return;
    }

    // flush 编码器，把缓冲里的数据写出来
    if (encoderOpened_ && codecCtx_) {
        avcodec_send_frame(codecCtx_, nullptr); // 发送空帧表示结束
        while (true) {
            int ret = avcodec_receive_packet(codecCtx_, pkt_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0)
                break;

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

    qDebug() << "[VideoRecorder] stopRecording.";

    if (!finishedPath.isEmpty()) {
        emit recordingStopped(finishedPath);
        emit sendMSG2ui(QStringLiteral("[VideoRecorder] 录像已保存到：%1").arg(finishedPath));
    }
}
bool VideoRecorder::openEncoderLockedForImage(const QImage &img)
{
    // 1) FFmpeg 全局初始化一次即可
    static bool ffInited = false;
    if (!ffInited) {
        av_log_set_level(AV_LOG_ERROR);
        avformat_network_init();
        ffInited = true;
    }

    encWidth_  = img.width();
    encHeight_ = img.height();
    encFps_    = (currentOptions_.fps > 0) ? currentOptions_.fps : 22.0;

    // 2) 生成输出文件路径
    currentRecordingPath_ = makeVideoFilePathLocked(currentOptions_);
    if (currentRecordingPath_.isEmpty()) {
        qWarning() << "[VideoRecorder] openEncoderLockedForImage: makeVideoFilePathLocked failed.";
        return false;
    }

    // 3) 创建输出上下文（根据后缀自动推容器）
    int ret = avformat_alloc_output_context2(
        &fmtCtx_, nullptr, nullptr,
        currentRecordingPath_.toUtf8().constData()
        );
    if (!fmtCtx_) {
        qWarning() << "[VideoRecorder] avformat_alloc_output_context2 failed, ret =" << ret;
        return false;
    }

    AVCodecID codecId = AV_CODEC_ID_H264;
    const AVCodec *codec = avcodec_find_encoder(codecId);

    if (!codec) {
        qWarning() << "[VideoRecorder] cannot find H264 encoder.";
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
    codecCtx_->time_base = AVRational{1, encFps_ > 0 ? (int)encFps_ : 22};
    codecCtx_->framerate = AVRational{(int)encFps_, 1};
    codecCtx_->gop_size  = 25;
    codecCtx_->max_b_frames = 0;
    codecCtx_->pix_fmt   = AV_PIX_FMT_YUV420P;
    codecCtx_->bit_rate  = currentOptions_.bitrateKbps * 1000LL;

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
    videoStream_->time_base = codecCtx_->time_base;

    // 4) 分配 frame / packet
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

    // 5) RGB -> YUV420P 的 sws context
    swsCtx_ = sws_getContext(encWidth_, encHeight_, AV_PIX_FMT_RGB24,
                             encWidth_, encHeight_, AV_PIX_FMT_YUV420P,
                             SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!swsCtx_) {
        qWarning() << "[VideoRecorder] sws_getContext failed.";
        return false;
    }

    // 6) 打开输出文件
    if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmtCtx_->pb, currentRecordingPath_.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            qWarning() << "[VideoRecorder] avio_open failed, ret =" << ret;
            return false;
        }
    }

    // 7) 写文件头
    ret = avformat_write_header(fmtCtx_, nullptr);
    if (ret < 0) {
        qWarning() << "[VideoRecorder] avformat_write_header failed, ret =" << ret;
        return false;
    }

    qDebug().noquote() << "[VideoRecorder] start writing to " << currentRecordingPath_;
    return true;
}
bool VideoRecorder::encodeImageLocked(const QImage &img)
{
    if (!fmtCtx_ || !codecCtx_ || !frame_ || !swsCtx_)
        return false;

    // 1) 确保是 RGB24
    QImage rgb = img;
    if (img.format() != QImage::Format_RGB888) {
        rgb = img.convertToFormat(QImage::Format_RGB888);
    }

    const uint8_t *srcData[1] = { rgb.bits() };
    int srcStride[1]          = { rgb.bytesPerLine() };

    // 2) RGB24 -> YUV420P
    int ret = sws_scale(swsCtx_,
                        srcData, srcStride,
                        0, encHeight_,
                        frame_->data, frame_->linesize);
    if (ret <= 0) {
        qWarning() << "[VideoRecorder] sws_scale failed, ret =" << ret;
        return false;
    }

    // 3) PTS
    frame_->pts = frameIndex_++;

    // 4) 编码一帧
    ret = avcodec_send_frame(codecCtx_, frame_);
    if (ret < 0) {
        qWarning() << "[VideoRecorder] avcodec_send_frame failed, ret =" << ret;
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(codecCtx_, pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            qWarning() << "[VideoRecorder] avcodec_receive_packet failed, ret =" << ret;
            return false;
        }

        av_packet_rescale_ts(pkt_, codecCtx_->time_base, videoStream_->time_base);
        pkt_->stream_index = videoStream_->index;

        ret = av_interleaved_write_frame(fmtCtx_, pkt_);
        av_packet_unref(pkt_);
        if (ret < 0) {
            qWarning() << "[VideoRecorder] av_interleaved_write_frame failed, ret =" << ret;
            return false;
        }
    }

    return true;
}
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
    qDebug() << "[VideoRecorder] encoder closed.";
}
