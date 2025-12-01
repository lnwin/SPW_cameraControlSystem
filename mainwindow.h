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
    QTimer* devAliveTimer_ = nullptr;
    void finishIpChange(bool ok, const QString& msg);

private slots:
    void onFrame(const QImage& img);
    void on_openCamera_clicked();
    void on_closeCamera_clicked();
    void on_changeCameraIP_clicked();

    void updateSystemIP();
    void on_changeSystemIP_clicked();
    void onSnUpdatedForIpChange(const QString& sn);  // 监听 SN 更新，判断是否已经用新 IP 上线
    void onIpChangeTimeout();                        // 等待超时
    void updateTableDevice(const QString& sn);
    void onCheckDeviceAlive();   // 周期检查设备在线/离线
protected:
    void closeEvent(QCloseEvent* event) override;
};

#endif // MAINWINDOW_H
