#include "cameraview.h"
#include <QDateTime>
#include <QPainter>
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
        emit logMessage(sn_, QString("Recording started: %1").arg(path));
    });
    connect(recorder_, &VideoRecorder::recordingStopped, this, [this](const QString& path){
        isRecording_ = false;
        emit recordingStopped(path);
        emit logMessage(sn_, QString("Recording stopped: %1").arg(path));
    });
    connect(recorder_, &VideoRecorder::snapshotSaved, this, [this](const QString& path){
        emit snapshotSaved(path);
        emit logMessage(sn_, QString("Snapshot saved: %1").arg(path));
    });
}

CameraView::~CameraView()
{
    disconnect();

    if (recThread_) {
        recThread_->quit();
        recThread_->wait(3000);
    }

    if (view_) {
        view_->deleteLater();
    }
    if (recorder_) {
        recorder_->deleteLater();
    }
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
    if (!viewer_) return;

    stopPullTimer();

    if (viewer_) {
        viewer_->stop();
        viewer_->wait(3000);
        viewer_->deleteLater();
        viewer_ = nullptr;
    }

    connected_ = false;
    view_->setImage(QImage());

    emit disconnected();
    emit logMessage(sn_, "Disconnected");
}

void CameraView::startRecording()
{
    if (!connected_ || !recorder_) return;

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
