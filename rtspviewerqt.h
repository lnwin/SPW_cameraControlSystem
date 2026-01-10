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

    // 可选：应急切 TCP（默认 UDP）
    void setUseTcp(bool on) { useTcp_ = on; }
    void setLatencyMs(int ms) { latencyMs_ = ms; }

    // UDP 端口范围：跨路由/防火墙必备（默认 50000-51000）
    void setPortRange(const QString& r) { portRange_ = r; }

signals:
    void frameReady(QSharedPointer<QImage> img);
    void logLine(const QString& s);

protected:
    void run() override;

private:
    QString url_;
    std::atomic<bool> stopFlag_{false};

    bool useTcp_ = false;        // 默认 UDP
    int  latencyMs_ =200;       // 默认 200ms
    QString portRange_ = "50000-51000";
    friend GstFlowReturn on_new_sample(GstAppSink* sink, gpointer user_data);

};
