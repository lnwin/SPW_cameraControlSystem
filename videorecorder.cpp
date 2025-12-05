// videorecorder.cpp

#include "videorecorder.h"

#include <QDate>
#include <QTime>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

VideoRecorder::VideoRecorder(QObject* parent)
    : QObject(parent)
{
}

VideoRecorder::~VideoRecorder()
{
    // 确保退出前停止录制
    stopRecording();
}

// ========== 路径配置 ==========

void VideoRecorder::setVideoRootDir(const QString& dir)
{
    QMutexLocker lk(&mutex_);
    videoRootDir_ = dir.trimmed();
}

void VideoRecorder::setSnapshotRootDir(const QString& dir)
{
    QMutexLocker lk(&mutex_);
    snapshotRootDir_ = dir.trimmed();
}

// ========== 录制控制 ==========

void VideoRecorder::startRecording(const VideoOptions& opt)
{
    QMutexLocker lk(&mutex_);

    if (videoRootDir_.isEmpty()) {
        emit errorOccurred(QStringLiteral("视频根目录未设置"));
        return;
    }
    if (recording_) {
        // 已经在录制，先停掉再开新文件，或者直接报错
        emit errorOccurred(QStringLiteral("当前已在录制中"));
        return;
    }

    QString filePath = makeVideoFilePathLocked(opt);
    if (filePath.isEmpty()) {
        emit errorOccurred(QStringLiteral("生成视频文件路径失败"));
        return;
    }

    // 这里后面要加：初始化 FFmpeg 输出、打开文件、写 header 等
    // TODO: init FFmpeg encoder here
    qDebug() << "[VideoRecorder] startRecording ->" << filePath;

    // 先只把路径/状态记住，方便你调试路径逻辑
    currentRecordingPath_ = filePath;
    currentOptions_ = opt;
    recording_ = true;

    emit recordingStarted(filePath);
}

void VideoRecorder::stopRecording()
{
    QMutexLocker lk(&mutex_);

    if (!recording_) return;

    // TODO: flush 编码器 + 写 trailer + 关闭文件
    qDebug() << "[VideoRecorder] stopRecording ->" << currentRecordingPath_;

    recording_ = false;
    const QString path = currentRecordingPath_;
    currentRecordingPath_.clear();

    emit recordingStopped(path);
}

bool VideoRecorder::isRecording() const
{
    QMutexLocker lk(&mutex_);
    return recording_;
}

// ========== 截图 ==========

void VideoRecorder::requestSnapshot(ImageFormat fmt)
{
    QMutexLocker lk(&mutex_);

    // 如果已经有最近一帧，直接保存
    if (!lastFrame_.isNull()) {
        QString path = makeSnapshotFilePathLocked(fmt);
        if (path.isEmpty()) {
            emit errorOccurred(QStringLiteral("生成截图文件路径失败"));
            return;
        }

        const char* fmtStr = imageFormatToQtString(fmt);
        bool ok = lastFrame_.save(path, fmtStr);
        if (!ok) {
            emit errorOccurred(QStringLiteral("保存截图失败: ") + path);
            return;
        }

        emit snapshotSaved(path);
        return;
    }

    // 没有 lastFrame_，就挂起请求，等下一帧 onFrame 来时再保存
    pendingSnapshot_ = true;
    pendingSnapshotFmt_ = fmt;
}

// ========== 帧输入 ==========

void VideoRecorder::onFrame(const QImage& frame, qint64 ptsMs)
{
    QMutexLocker lk(&mutex_);

    // 更新最近一帧缓存
    lastFrame_ = frame;
    lastPtsMs_ = ptsMs;

    // 1) 先处理挂起的截图
    if (pendingSnapshot_) {
        pendingSnapshot_ = false;

        if (!lastFrame_.isNull()) {
            QString path = makeSnapshotFilePathLocked(pendingSnapshotFmt_);
            if (!path.isEmpty()) {
                const char* fmtStr = imageFormatToQtString(pendingSnapshotFmt_);
                bool ok = lastFrame_.save(path, fmtStr);
                if (!ok) {
                    emit errorOccurred(QStringLiteral("保存截图失败: ") + path);
                } else {
                    emit snapshotSaved(path);
                }
            } else {
                emit errorOccurred(QStringLiteral("生成截图文件路径失败"));
            }
        }
    }

    // 2) 如果正在录制，这里后面会把 frame 放入队列交给 FFmpeg 编码
    if (recording_) {
        // TODO:
        //   - 将 frame/ptsMs 入队
        //   - 在 VideoRecorder 所在线程的工作循环里 pop 帧 -> sws_scale -> avcodec_send_frame
        // 暂时先打印日志占位：
        // qDebug() << "[VideoRecorder] onFrame for recording, ptsMs =" << ptsMs;
    }
}

// ========== 内部辅助函数：路径生成 ==========

QString VideoRecorder::makeVideoFilePathLocked(const VideoOptions& opt) const
{
    if (videoRootDir_.isEmpty())
        return QString();

    // 日期到天的子目录：YYYY-MM-DD
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

    // 文件名：YYYY-MM-DD_hh-mm-ss.ext
    const QDateTime now = QDateTime::currentDateTime();
    const QString prefix = now.toString("yyyy-MM-dd_hh-mm-ss");
    const QString ext = containerToExtension(opt.container);

    const QString fileName = prefix + "." + ext;
    return dateDir.filePath(fileName);
}

QString VideoRecorder::makeSnapshotFilePathLocked(ImageFormat fmt) const
{
    if (snapshotRootDir_.isEmpty())
        return QString();

    // 日期到天的子目录：YYYY-MM-DD
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

    // 文件名：YYYY-MM-DD_hh-mm-ss_zzz.ext
    const QDateTime now = QDateTime::currentDateTime();
    const QString prefix = now.toString("yyyy-MM-dd_hh-mm-ss_zzz");

    const char* fmtStr = imageFormatToQtString(fmt);
    QString ext = "png";
    if (fmt == ImageFormat::JPG) ext = "jpg";
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
