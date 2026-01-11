// rtspviewerqt.h  (FULL REPLACEABLE FILE)
#pragma once

#include <QThread>
#include <QSharedPointer>
#include <QImage>
#include <QString>
#include <atomic>
#include <mutex>

class RtspViewerQt : public QThread
{
    Q_OBJECT
public:
    explicit RtspViewerQt(QObject* parent = nullptr);
    ~RtspViewerQt() override;

    void setUrl(const QString& url);

    // latency hint (ms). If <=0, viewer will choose a sane default.
    void setLatencyMs(int ms) { latencyMs_ = ms; }

    // Non-blocking stop (thread exits by itself)
    void stop();

    // UI thread calls this periodically (e.g., 60Hz).
    // Returns latest frame ONLY if a new one arrived since last take.
    QSharedPointer<QImage> takeLatestFrameIfNew();

signals:
    void logLine(const QString& s);

protected:
    void run() override;

private:
    QString url_;
    std::atomic<bool> stopFlag_{false};
    int latencyMs_ = 0;

    // latest frame handoff
    std::mutex latestMtx_;
    QSharedPointer<QImage> latest_;
    std::atomic<uint64_t> latestSeq_{0};
    std::atomic<uint64_t> takenSeq_{0};
};
