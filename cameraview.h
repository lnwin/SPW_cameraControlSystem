#pragma once

#include <QObject>
#include <QWidget>
#include <QThread>
#include <QTimer>
#include <QImage>
#include <QSharedPointer>
#include <QDateTime>

#include "rtspviewerqt.h"
#include "ZoomPanImageView.h"
#include "videorecorder.h"

// 封装单个相机的所有组件：viewer + view + recorder + 状态
class CameraView : public QObject
{
    Q_OBJECT
public:
    explicit CameraView(const QString& sn, QWidget* parentWidget, QObject* parent = nullptr);
    ~CameraView() override;

    // 基本信息
    QString sn() const { return sn_; }
    ZoomPanImageView* view() const { return view_; }
    VideoRecorder* recorder() const { return recorder_; }

    // 连接控制
    void connectToCamera(const QString& rtspUrl);
    void disconnect();
    bool isConnected() const { return connected_; }

    // 帧率统计
    double currentFps() const { return currentFps_; }
    int lastFps() const { return lastFps_; }  // 获取最近一次统计的 FPS 值
    qint64 lastFrameMs() const { return lastFrameMs_; }  // 获取最后收到帧的时间戳

    // 录像控制
    void startRecording();
    void stopRecording();
    bool isRecording() const { return isRecording_; }

    // 截图
    void captureSnapshot();

    // 设置录像选项（录像和截图保存路径等）
    void setRecordOptions(const myRecordOptions& options);

    // overlay 配置
    void setOverlayEnabled(bool enabled) { overlayEnabled_ = enabled; }
    void setOverlayText(const QString& text) { overlayTopText_ = text; }

signals:
    void connected();
    void disconnected();
    void recordingStarted(const QString& filePath);
    void recordingStopped(const QString& filePath);
    void recordingDurationUpdate(int segmentSeconds, int totalSeconds);
    void snapshotSaved(const QString& filePath);
    void logMessage(const QString& sn, const QString& msg);
    void fpsUpdated(int fps);

private slots:
    void onPullFrame();

private:
    void startPullTimer();
    void stopPullTimer();

    QString sn_;
    QWidget* parentWidget_;

    // 组件
    ZoomPanImageView* view_ = nullptr;
    RtspViewerQt* viewer_ = nullptr;
    VideoRecorder* recorder_ = nullptr;
    QThread* recThread_ = nullptr;

    // 拉帧定时器
    QTimer* pullTimer_ = nullptr;

    // 状态
    bool connected_ = false;
    bool isRecording_ = false;
    bool pendingSnapshot_ = false;

    // overlay 配置
    bool overlayEnabled_ = true;
    QString overlayTopText_;

    // overlay 缓冲区（3槽避免竞争）
    QImage overlayDispBuf_[3];
    int overlayDispIdx_ = 0;

    // 帧统计
    qint64 lastFrameMs_ = 0;
    int fpsFrameCount_ = 0;
    qint64 fpsWindowStart_ = 0;
    int lastFps_ = 0;
    double currentFps_ = 0.0;

    // 拉帧控制
    int noFrameStreak_ = 0;
    qint64 lastNewFrameMs_ = 0;
};
