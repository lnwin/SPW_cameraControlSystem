#include "cameraview.h"
#include <QDateTime>
#include <QPainter>
#include <QDebug>
#include <cstring>

static int calcNextPullIntervalMs(int streak)
{
    if (streak <= 2)  return 0;
    if (streak <= 8)  return 2;
    if (streak <= 20) return 8;
    if (streak <= 60) return 16;
    return 33;
}

static void applyOverlayInto(QImage& dst, const QImage& src, const QString& topText)
{
    if (dst.size() != src.size() || dst.format() != src.format())
        dst = QImage(src.size(), src.format());
    std::memcpy(dst.bits(), src.constBits(),
                static_cast<size_t>(src.sizeInBytes()));

    // 强制深拷贝，避免 QPainter 在 GPU 创建共享纹理缓存（修复长时间运行GPU显存累积）
    dst.detach();

    QPainter p(&dst);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QFont font("Arial", 20, QFont::Bold);
    p.setFont(font);
    const QFontMetrics fm(font);
    const int pad = 6;
    const QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                       + (topText.isEmpty() ? "" : "  " + topText);
    const int w = fm.horizontalAdvance(line) + pad * 2;
    const int h = fm.height() + pad * 2;
    const QRect bg((dst.width() - w) / 2, 6, w, h);
    p.fillRect(bg, Qt::black);
    p.setPen(Qt::white);
    p.drawText(bg, Qt::AlignCenter, line);
}

CameraView::CameraView(const QString& sn, QWidget* parentWidget, QObject* parent)
    : QObject(parent)
    , sn_(sn)
    , parentWidget_(parentWidget)
{
    // 创建视图
    view_ = new ZoomPanImageView(parentWidget);
    view_->setZoomRange(1.0, 3.0);
    view_->setDisplayCrop(290, 290);
    view_->hide(); // 初始隐藏，布局时显示

    // 创建录像器和线程
    recorder_ = new VideoRecorder;
    recThread_ = new QThread(this);
    recorder_->moveToThread(recThread_);
    recThread_->start();

    // 设置录像器的 SN（用于按 SN 分文件夹保存录像和截图）
    QMetaObject::invokeMethod(recorder_, "setSn", Qt::QueuedConnection, Q_ARG(QString, sn));

    // 创建拉帧定时器
    pullTimer_ = new QTimer(this);
    pullTimer_->setTimerType(Qt::PreciseTimer);
    pullTimer_->setSingleShot(true);
    connect(pullTimer_, &QTimer::timeout, this, &CameraView::onPullFrame);

    // 连接录像信号
    connect(recorder_, &VideoRecorder::recordingStarted, this, [this](const QString& path){
        isRecording_ = true;
        emit recordingStarted(path);
        // 不要在这里发送 logMessage，CameraManager 会转发 recordingStarted 信号
    });
    connect(recorder_, &VideoRecorder::recordingStopped, this, [this](const QString& path){
        isRecording_ = false;
        emit recordingStopped(path);
        // 不要在这里发送 logMessage，CameraManager 会转发 recordingStopped 信号
    });
    connect(recorder_, &VideoRecorder::recordingDurationUpdate, this, &CameraView::recordingDurationUpdate);
    connect(recorder_, &VideoRecorder::snapshotSaved, this, [this](const QString& path){
        emit snapshotSaved(path);
        // 不要在这里发送 logMessage，CameraManager 会转发 snapshotSaved 信号
    });
}

CameraView::~CameraView()
{
    qDebug() << "[CameraView] Destructor called for SN:" << sn_;

    // *** 关键修复：先断开所有信号连接，避免在析构时触发信号导致崩溃 ***
    QObject::disconnect(this, nullptr, nullptr, nullptr);
    qDebug() << "[CameraView] All signals disconnected, SN:" << sn_;

    // 1. 先停止录像(如果正在录像)
    if (isRecording_) {
        qDebug() << "[CameraView] Stopping recording before cleanup, SN:" << sn_;
        stopRecording();
        QThread::msleep(100);  // 给一点时间让停止操作完成
    }

    // 2. 断开RTSP连接(停止拉流) - 现在不会触发信号了
    if (viewer_) {
        qDebug() << "[CameraView] Calling disconnect (no signals will be emitted), SN:" << sn_;

        stopPullTimer();

        viewer_->stop();
        if (!viewer_->wait(2000)) {
            qWarning() << "[CameraView] Viewer thread timeout, terminating, SN:" << sn_;
            viewer_->terminate();
            viewer_->wait(500);
        }

        viewer_->deleteLater();
        viewer_ = nullptr;

        connected_ = false;
        if (view_) {
            view_->setImage(QImage());
        }
    }

    // 3. 停止并等待录像线程结束
    if (recThread_ && recThread_->isRunning()) {
        qDebug() << "[CameraView] Quitting recorder thread, SN:" << sn_;
        recThread_->quit();

        if (!recThread_->wait(5000)) {  // 等待5秒
            qWarning() << "[CameraView] Recorder thread did not finish in time, terminating, SN:" << sn_;
            recThread_->terminate();  // 强制终止
            recThread_->wait(1000);   // 再等1秒
        }
        qDebug() << "[CameraView] Recorder thread stopped, SN:" << sn_;
    }

    // 4. 删除录像器(现在线程已经停止,可以安全删除)
    if (recorder_) {
        delete recorder_;
        recorder_ = nullptr;
    }

    // 5. 删除录像线程
    if (recThread_) {
        delete recThread_;
        recThread_ = nullptr;
    }

    // 6. 删除视图
    if (view_) {
        delete view_;
        view_ = nullptr;
    }

    qDebug() << "[CameraView] Destructor finished, SN:" << sn_;
}

void CameraView::connectToCamera(const QString& rtspUrl)
{
    if (connected_) {
        disconnect();
    }

    viewer_ = new RtspViewerQt(this);
    viewer_->setLatencyMs(300);
    viewer_->setUrl(rtspUrl);

    connect(viewer_, &RtspViewerQt::logLine, this, [this](const QString& msg){
        emit logMessage(sn_, msg);
    });

    viewer_->start();
    connected_ = true;
    startPullTimer();

    emit this->connected();
    emit logMessage(sn_, QString("Connecting to %1").arg(rtspUrl));
}

void CameraView::disconnect()
{
    qDebug() << "[CameraView] disconnect() called for SN:" << sn_ << "viewer_:" << viewer_;

    if (!viewer_) {
        qDebug() << "[CameraView] No viewer to disconnect, SN:" << sn_;
        return;
    }

    qDebug() << "[CameraView] Stopping pull timer, SN:" << sn_;
    stopPullTimer();

    qDebug() << "[CameraView] Calling viewer_->stop(), SN:" << sn_;
    viewer_->stop();

    qDebug() << "[CameraView] Waiting for viewer thread, SN:" << sn_;
    if (!viewer_->wait(2000)) {  // 只等待 2 秒
        qWarning() << "[CameraView] Viewer thread timeout, terminating, SN:" << sn_;
        viewer_->terminate();
        viewer_->wait(500);  // 再等 0.5 秒
    }

    qDebug() << "[CameraView] Deleting viewer (deleteLater), SN:" << sn_;
    viewer_->deleteLater();
    viewer_ = nullptr;

    connected_ = false;
    view_->setImage(QImage());

    qDebug() << "[CameraView] Emitting disconnected signal, SN:" << sn_;
    emit disconnected();
    emit logMessage(sn_, "Disconnected");

    qDebug() << "[CameraView] disconnect() finished, SN:" << sn_;
}

void CameraView::startRecording()
{
    if (!connected_ || !recorder_) return;

    // 立即设置 isRecording_ 为 true，这样下一帧就会被发送给录像器
    isRecording_ = true;

    QMetaObject::invokeMethod(recorder_, &VideoRecorder::startRecording, Qt::QueuedConnection);
}

void CameraView::stopRecording()
{
    if (!recorder_) return;

    QMetaObject::invokeMethod(recorder_, &VideoRecorder::stopRecording, Qt::QueuedConnection);
}

void CameraView::captureSnapshot()
{
    pendingSnapshot_ = true;
}

void CameraView::setRecordOptions(const myRecordOptions& options)
{
    if (!recorder_) {
        qWarning() << "[CameraView] Cannot set record options: no recorder";
        return;
    }

    // 通过信号槽将录像选项传递给 VideoRecorder（在另一个线程中）
    QMetaObject::invokeMethod(recorder_, "receiveRecordOptions",
                              Qt::QueuedConnection,
                              Q_ARG(myRecordOptions, options));

    qDebug() << "[CameraView] Record options set for SN:" << sn_
             << "recordPath:" << options.recordPath
             << "capturePath:" << options.capturePath;
}

void CameraView::startPullTimer()
{
    noFrameStreak_ = 0;
    lastNewFrameMs_ = 0;
    pullTimer_->start(0);
}

void CameraView::stopPullTimer()
{
    pullTimer_->stop();
    noFrameStreak_ = 0;
}

void CameraView::onPullFrame()
{
    if (!viewer_ || !connected_) {
        stopPullTimer();
        return;
    }

    QSharedPointer<QImage> img = viewer_->takeLatestFrameIfNew();
    if (img && !img->isNull()) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        lastNewFrameMs_ = now;
        lastFrameMs_ = now;
        noFrameStreak_ = 0;

        // 帧率统计
        if (fpsWindowStart_ == 0) fpsWindowStart_ = now;
        fpsFrameCount_++;
        if (now - fpsWindowStart_ >= 1000) {
            lastFps_ = fpsFrameCount_;
            currentFps_ = static_cast<double>(fpsFrameCount_);
            fpsFrameCount_ = 0;
            fpsWindowStart_ = now;
            emit fpsUpdated(lastFps_);
        }

        // 显示
        if (view_ && overlayEnabled_) {
            QImage& disp = overlayDispBuf_[overlayDispIdx_];
            overlayDispIdx_ = (overlayDispIdx_ + 1) % 3;
            applyOverlayInto(disp, *img, overlayTopText_);
            view_->setImage(disp);
        } else if (view_) {
            view_->setImage(*img);
        }

        // 录像
        if (isRecording_ && recorder_) {
            QSharedPointer<QImage> recImg;
            if (overlayEnabled_) {
                recImg = QSharedPointer<QImage>::create();
                applyOverlayInto(*recImg, *img, overlayTopText_);
            } else {
                recImg = img;
            }
            QMetaObject::invokeMethod(recorder_, "receiveFrame2Record", Qt::QueuedConnection,
                                      Q_ARG(QSharedPointer<QImage>, recImg));
        }

        // 截图
        if (pendingSnapshot_ && recorder_) {
            QSharedPointer<QImage> snapImg;
            if (overlayEnabled_) {
                snapImg = QSharedPointer<QImage>::create();
                applyOverlayInto(*snapImg, *img, overlayTopText_);
            } else {
                snapImg = img;
            }
            QMetaObject::invokeMethod(recorder_, "receiveFrame2Save", Qt::QueuedConnection,
                                      Q_ARG(QSharedPointer<QImage>, snapImg));
            pendingSnapshot_ = false;
        }
    } else {
        noFrameStreak_++;
    }

    const int nextMs = (lastNewFrameMs_ == 0)
                     ? qMin(calcNextPullIntervalMs(noFrameStreak_), 8)
                     : qMax(calcNextPullIntervalMs(noFrameStreak_), 16);
    pullTimer_->start(nextMs);
}
