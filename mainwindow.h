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
#include <dhcpminiserver.h>
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
    QString writeMediaMtxYaml(const QString& dir, const MediaMtxRuntimeCfg& c);
    void relaunchMediaMTX(const QString& bindIp);

    // 当前配置（便于遇错重试）
    QString curBindIp_ = "192.168.194.77";
    int     curRtspPort_ = 10000;
    int     curRtpBase_  = 10002;
    QString curPath_     = "mystream";
    QStringList probeWiredIPv4s();  // 枚举并返回所有有线 IPv4

    bool stopMediaMTXBlocking(int gracefulMs = 3000, int killMs = 2000);

    DhcpMiniServer* dhcp_ = nullptr;
    // 设置并应用（对外一个入口）
    void setCurBindIp(const QString& ip);
    void applyDhcpFromCurBindIp();  // 根据 curBindIp_ 动态设置 DHCP 参数

private slots:
    void onFrame(const QImage& img);
    void on_openCamera_clicked();
    void on_closeCamera_clicked();
    void on_changeCameraIP_clicked();
    void on_updateCameraIP_clicked();
    void on_changeSystemIP_clicked();
    void on_updateSystemIP_clicked();
protected:
    void closeEvent(QCloseEvent* event) override;
};

#endif // MAINWINDOW_H
