#pragma once
#include <QThread>
#include <QString>
#include <atomic>

// 头文件不要包含任何 GStreamer 头，避免宏冲突。
// 用不透明指针保存 GStreamer 对象。
class GstRtspServerQt : public QThread {
    Q_OBJECT
public:
    explicit GstRtspServerQt(QObject* parent=nullptr);
    ~GstRtspServerQt() override;

    // 端口示例 "10000"；路径示例 "/mystream"
    void configure(const QString& ip, const QString& servicePort, const QString& mountPath);

    void stopAsync();

protected:
    void run() override;

private:
    QString service_ = "10000";
    QString path_    = "/mystream";
    std::atomic<bool> running_{false};
    QString ip_ = "0.0.0.0";
    // Opaque pointers to GStreamer objects (defined/used only in .cpp)
    void* main_loop_ = nullptr;    // GMainLoop*
    void* server_    = nullptr;    // GstRTSPServer*
    void* mounts_    = nullptr;    // GstRTSPMountPoints*
    void* factory_   = nullptr;    // GstRTSPMediaFactory*
};
