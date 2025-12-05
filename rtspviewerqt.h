#ifndef RTSP_VIEWER_QT_H
#define RTSP_VIEWER_QT_H

#include <QThread>
#include <QImage>
#include <QString>
#include <atomic>

// 依赖：FFmpeg & OpenCV（仅用于色彩转换和 letterbox 可选）
// 你项目里已在 pro/cmake 里配置好了这些库的包含和链接

class RtspViewerQt : public QThread
{
    Q_OBJECT
public:
    explicit RtspViewerQt(QObject* parent = nullptr);
    ~RtspViewerQt() override;

    // 设置 RTSP 地址（必须在 start() 前调用；或 stop() 完成后再改）
    void setUrl(const QString& url);

    // 线程安全停止
    void stop();

signals:
    // 送 UI 的帧（固定 1224x1024，RGB888）
    void frameReady(const QImage& img);
    void frameDecoded(const QImage& img, qint64 ptsMs);
    // 关键日志
    void logLine(const QString& line);

protected:
    void run() override;

private:
    QString url_;
    std::atomic<bool> stopFlag_{false};
    // 固定输出分辨率：1080p


};

#endif // RTSP_VIEWER_QT_H
