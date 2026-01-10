#pragma once

#include <QThread>
#include <QSharedPointer>
#include <QImage>
#include <QString>
#include <atomic>

// GStreamer
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

class RtspViewerQt : public QThread
{
    Q_OBJECT
public:
    explicit RtspViewerQt(QObject* parent = nullptr);
    ~RtspViewerQt() override;

    void setUrl(const QString& url);
    void stop();

    // 可选：让你能切 UDP/TCP（默认 udp）
    void setUseTcp(bool on) { useTcp_ = on; }
    void setLatencyMs(int ms) { latencyMs_ = ms; }

signals:
    void frameReady(QSharedPointer<QImage> img);
    void logLine(const QString& s);

protected:
    void run() override;

private:
    QString url_;
    std::atomic<bool> stopFlag_{false};

    bool useTcp_ = false;   // 默认 UDP
    int latencyMs_ = 200;   // 默认 200ms
};
