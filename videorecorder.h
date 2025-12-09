#pragma once

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QDateTime>
#include <myStruct.h>   // 里面定义了 myRecordOptions

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
            fps(22),               // 你的 22fps
            bitrateKbps(8000),     // 默认 8Mbps
            enableAudio(false)
        {}
    };

    explicit VideoRecorder(QObject* parent = nullptr);
    ~VideoRecorder() override;

public slots:

    void receiveRecordOptions(myRecordOptions myOptions);
    void receiveFrame2Save(const QImage& img);
    void receiveFrame2Record(const QImage& img);

signals:
    void recordingStarted(const QString& filePath);
    void recordingStopped(const QString& filePath);
    void snapshotSaved(const QString& filePath);
    void sendMSG2ui(const QString&);
private:
    // 生成视频完整路径：
    //   <videoRootDir_>/<YYYY-MM-DD>/<YYYY-MM-DD_hh-mm-ss>.<ext>
    QString makeVideoFilePathLocked(const VideoOptions& opt) const;

    // 生成截图完整路径：
    //   <snapshotRootDir_>/<YYYY-MM-DD>/<YYYY-MM-DD_hh-mm-ss_zzz>.<ext>
    QString makeSnapshotFilePathLocked(ImageFormat fmt) const;

    // ImageFormat -> "PNG"/"JPG"/"BMP"
    static const char* imageFormatToQtString(ImageFormat fmt);

    // VideoContainer -> "mp4"/"avi"
    static QString containerToExtension(VideoContainer c);

private:
    mutable QMutex mutex_;

    // 路径配置
    QString videoRootDir_;    // 视频保存根目录
    QString snapshotRootDir_; // 截图保存根目录

    ImageFormat myCaptureType = ImageFormat::PNG;    // 来自 myRecordOptions.capturTpye
    VideoContainer myRecordType  = VideoContainer::MP4;    // 来自 myRecordOptions.recordTpye

    // 录制状态
    bool recording_ = false;
    QString currentRecordingPath_;
    VideoOptions currentOptions_;

    // 最近一帧缓存（供截图用）
    QImage lastFrame_;
    qint64 lastPtsMs_ = 0;

    // 挂起的截图请求
    bool pendingSnapshot_ = false;
    ImageFormat pendingSnapshotFmt_ = ImageFormat::PNG;

    // TODO: 下面会加 FFmpeg 成员和帧队列
    // e.g. AVFormatContext* ofmt_ = nullptr;
    //      AVCodecContext*  vcc_  = nullptr;
    //      SwsContext*      sws_  = nullptr;
    //      int              frameIndex_ = 0;
};
