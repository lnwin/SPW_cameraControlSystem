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
    enum class VideoContainer {
        MP4,
        AVI
    };

    enum class ImageFormat {
        PNG,
        JPG,
        BMP
    };

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
    // ==== 1) 路径配置 ====

    // 由外部类通过信号设置视频/截图根目录
    // 例如:
    //   myRecordOptions opt;
    //   opt.recordPath  = "D:/TurbidCam/video";
    //   opt.capturePath = "D:/TurbidCam/snapshot";
    //   emit recordOptionsChanged(opt);
    void receiveRecordOptions(myRecordOptions myOptions);

    // ==== 2) 录制控制 ====

    // 根据当前时间自动生成路径:
    //   <videoRootDir>/<YYYY-MM-DD>/<YYYY-MM-DD_hh-mm-ss>.(mp4/avi)
    void startRecording();

    // 停止录制：后面会加 flush 编码器和写尾
    void stopRecording();

    bool isRecording() const;

    // ==== 3) 截图 ====

    // 请求一次截图：
    //   - 有 lastFrame_ 就立即保存
    //   - 否则挂起，等下一帧 onFrame 时保存
    // 真正落地路径：
    //   <snapshotRootDir>/<YYYY-MM-DD>/<YYYY-MM-DD_hh-mm-ss_zzz>.(png/jpg/bmp)
    void requestSnapshot();

    // ==== 4) 帧输入 ====

    // 供 RtspViewerQt 的 frameDecoded 信号连接：
    //   connect(viewer, &RtspViewerQt::frameDecoded,
    //           recorder, &VideoRecorder::onFrame);
    void myonFrame(const QImage& frame);

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

    int myCaptureType = 0;    // 来自 myRecordOptions.capturTpye
    int myRecordType  = 0;    // 来自 myRecordOptions.recordTpye

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
