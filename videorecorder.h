#pragma once

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QDateTime>
#include <myStruct.h>   // 里面定义了 myRecordOptions / ImageFormat / VideoContainer

// FFmpeg 前向声明，避免在头文件里包含一堆 C 头
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwsContext;
struct AVFrame;
struct AVPacket;

class VideoRecorder : public QObject
{
    Q_OBJECT
public:
    struct VideoOptions {
        VideoContainer container;
        int fps;
        int bitrateKbps;
        bool enableAudio;

        VideoOptions()
            : container(VideoContainer::MP4),
            fps(22),
            bitrateKbps(8000),
            enableAudio(false)
        {}
    };

    explicit VideoRecorder(QObject* parent = nullptr);
    ~VideoRecorder() override;

public slots:
    void receiveRecordOptions(myRecordOptions myOptions);
    void receiveFrame2Save(const QImage& img);
    void receiveFrame2Record(const QImage& img);

    void startRecording();   // ✅ 无参数
    void stopRecording();    // ✅ 无参数

signals:
    void recordingStarted(const QString& filePath);
    void recordingStopped(const QString& filePath);
    void snapshotSaved(const QString& filePath);
    void sendMSG2ui(const QString&);

private:
    QString makeVideoFilePathLocked(const VideoOptions& opt) const;
    QString makeSnapshotFilePathLocked(ImageFormat fmt) const;

    static const char* imageFormatToQtString(ImageFormat fmt);
    static QString containerToExtension(VideoContainer c);

private:
    mutable QMutex mutex_;

    // 路径配置
    QString videoRootDir_    = "D:/SP_camera_record";
    QString snapshotRootDir_ = "D:/SP_camera_capture";

    ImageFormat    myCaptureType = ImageFormat::PNG;
    VideoContainer myRecordType  = VideoContainer::MP4;

    // 录制状态
    bool recording_ = false;
    QString currentRecordingPath_;
    VideoOptions currentOptions_;

    // 最近一帧缓存（供截图用）
    QImage lastFrame_;

    // 用于“真实时间PTS”的基准与单调控制（解决时长漂移）
    qint64 recStartUs_ = 0;     // 录制开始的时间（微秒）
    qint64 lastPtsMs_  = 0;     // 上一次写入的 pts（毫秒），保证单调递增

    // 挂起的截图请求
    bool pendingSnapshot_ = false;
    ImageFormat pendingSnapshotFmt_ = ImageFormat::PNG;

    // ========== FFmpeg 相关 ==========
    bool encoderOpened_ = false;

    AVFormatContext *fmtCtx_      = nullptr;
    AVCodecContext  *codecCtx_    = nullptr;
    AVStream        *videoStream_ = nullptr;
    SwsContext      *swsCtx_      = nullptr;
    AVFrame         *frame_       = nullptr;
    AVPacket        *pkt_         = nullptr;

    int     encWidth_  = 0;
    int     encHeight_ = 0;
    double  encFps_    = 22.0;
    qint64  frameIndex_ = 0;

    bool openEncoderLockedForImage(const QImage &img);
    bool encodeImageLocked(const QImage &img);
    void closeEncoderLocked();
};
