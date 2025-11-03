#pragma once
#include <QThread>
#include <QImage>
#include <QString>
#include <atomic>

// 只前向声明 GStreamer 类型，避免在 .h 里包含其头文件
// ✅ 两种等价写法任选其一：我这里用 typedef（最兼容）

typedef struct _GMainLoop           GMainLoop;
typedef struct _GstRTSPServer       GstRTSPServer;
typedef struct _GstRTSPMountPoints  GstRTSPMountPoints;
typedef struct _GstRTSPMediaFactory GstRTSPMediaFactory;
typedef struct _GstRTSPMedia        GstRTSPMedia;
typedef struct _GstAppSink          GstAppSink;
typedef struct _GstBus              GstBus;
typedef struct _GstMessage          GstMessage;

class GstRtspRecordServerQt : public QThread {
    Q_OBJECT
public:
    explicit GstRtspRecordServerQt(QObject* parent=nullptr);
    ~GstRtspRecordServerQt() override;

    void configure(const QString& ip, const QString& service, const QString& path);
    void stopAsync();

signals:
    void frameReady(const QImage& img);
    void publisherConnected();
    void publisherDisconnected();

protected:
    void run() override;

private:
    // GStreamer 对象（不在 .h 里包含其头）
    GMainLoop*           main_loop_ = nullptr;
    GstRTSPServer*       server_    = nullptr;
    GstRTSPMountPoints*  mounts_    = nullptr;
    GstRTSPMediaFactory* factory_   = nullptr;
    GstAppSink*          appsink_   = nullptr;

    QString ip_{"0.0.0.0"};
    QString service_{"10000"};
    QString path_{"/uplink"};
    std::atomic<bool> running_{false};

    // 全部用 void* 避免在 .h 引入 gst 头
    static int  on_bus(void* bus, void* msg, void* user_data);
    static void on_media_configure(void* factory, void* media, void* user_data);
    static int  on_new_sample(void* sink, void* user_data);
};
