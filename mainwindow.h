#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <rtspviewerqt.h>
#include <QtNetwork/QNetworkInterface>
#include <QtNetwork/QHostAddress>
#include <QCloseEvent>
#include <QProgressDialog>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QTimer>
#include <udpserver.h>
#include <QInputDialog>
#include <QProgressDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QPushButton>
#include <QHash>            // ★ 新增：path 状态表用到
#include <QPainter>
#include <opencv2/opencv.hpp>
#include <TitleBar.h>
class RtspViewerQt;
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

struct MediaMtxRuntimeCfg {
    QString logLevel = "info";
    bool rtsp = true;
    bool rtmp = false;
    bool hls  = false;
    bool webrtc = false;

    // v1.15.3：用 rtspTransports 代替 protocols
    QStringList rtspTransports = {"udp"};

    // 监听与 RTP/RTCP（支持 "IP:PORT" 或 ":PORT"）
    QString rtspAddress = ":10000";
    QString rtpAddress  = ":10002";
    QString rtcpAddress = ":10003";

    // 路径
    QString pathName = "mystream";
    bool sourceOnDemand = false;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    RtspViewerQt* viewer_ = nullptr;

    // ---- MediaMTX 管理 ----
    QProcess* mtxProc_ = nullptr;
    void startMediaMTX();
    void stopMediaMTX();
    // 当前配置（便于遇错重试）
    QString curBindIp_ = "192.168.194.77";
    int     curRtspPort_ = 10000;
    int     curRtpBase_  = 10002;
    QString curPath_     = "mystream";
    QStringList probeWiredIPv4s();  // 枚举并返回所有有线 IPv4

    bool stopMediaMTXBlocking(int gracefulMs = 3000, int killMs = 2000);

    UdpDeviceManager* mgr_ = nullptr;
    void upsertCameraSN(const QString& sn);
    QHash<QString, QString> sn2ip_;

    // ---- 改相机 IP 相关状态 ----
    QString        pendingIpSn_;       // 正在修改的 SN
    QString        pendingIpNew_;      // 目标 IP
    bool           ipChangeWaiting_ = false;
    QProgressDialog* ipWaitDlg_ = nullptr;
    QTimer*        ipChangeTimer_ = nullptr;
    QTimer*        devAliveTimer_ = nullptr;
    void finishIpChange(bool ok, const QString& msg);

    // ---- 新增：与相机状态 / UI 相关 ----
    QString curSelectedSn_;       // 当前 table 中选中的设备 SN（没选中则为空）
    bool    previewActive_ = false; // 是否已经收到至少一帧图像

    void updateCameraButtons();        // 统一管理“打开/关闭相机”按钮 enable 状态
    void onTableSelectionChanged();    // tableWidget 选中行变化
    void doStopViewer();               // 统一关闭预览 + 更新按钮
    void changeCameraIpForSn(const QString& sn); // 按 SN 修改 IP 的核心逻辑

    // ---- ★ 新增：MediaMTX 推流状态表（path 维度，一般 path == SN） ----
    struct PathState {
        bool   hasPublisher = false;   // 当前是否检测到 publisher
        qint64 lastPubMs    = 0;       // 最近一次 "is publishing" 的时间
    };
    QHash<QString, PathState> pathStates_;  // key = RTSP path，一般就是 SN

    void onMediaMtxLogLine(const QString& line);  // 解析 MediaMTX 一行日志，更新 pathStates_
    void updateDeviceInfoPanel(const DeviceInfo* dev, bool online);
    void clearDeviceInfoPanel();
    QHash<QString, qint64> camOnlineSinceMs_;   // key: SN, value: 本次上线开始时间(ms)
    QIcon iconOnline_;
    QIcon iconOffline_;
    bool isRecording_ = false;

    void titleForm();

private slots:
    void onFrame(const QImage& img);

    void updateSystemIP();
    void onSnUpdatedForIpChange(const QString& sn);  // 监听 SN 更新，判断是否已经用新 IP 上线
    void onIpChangeTimeout();                        // 等待超时
    void updateTableDevice(const QString& sn);
    void onCheckDeviceAlive();   // 周期检查设备在线/离线
    void on_action_openCamera_triggered();

    void on_action_closeCamera_triggered();

    void on_action_grap_triggered();

    void on_action_startRecord_triggered();

    void on_action_stopRecord_triggered();

    void on_action_triggered();

protected:
    void closeEvent(QCloseEvent* event) override;
};

#endif // MAINWINDOW_H
