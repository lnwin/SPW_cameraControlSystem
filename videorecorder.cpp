#include "videorecorder.h"

#include <QDate>
#include <QTime>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QMutexLocker>

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
    // 跟其他函数一样加锁，保证线程安全
    QMutexLocker lk(&mutex_);

    // 路径配置
    videoRootDir_    = myOptions.recordPath;
    snapshotRootDir_ = myOptions.capturePath;

    // 预留：后续可以根据 myRecordType/myCaptureType 决定编码参数 / 图片格式
    myCaptureType    = myOptions.capturType;
    myRecordType     = myOptions.recordType;

    qDebug() << "[VideoRecorder] receiveRecordOptions:"
             << "videoRootDir =" << videoRootDir_
             << "snapshotRootDir =" << snapshotRootDir_
             << "captureType =" << myCaptureType
             << "recordType =" << myRecordType;
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

    qDebug() << "[VideoRecorder] receiveFrame2Save: saved snapshot to" << path;
    emit snapshotSaved(path);
}
void VideoRecorder::receiveFrame2Record(const QImage& img)
{

};
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
