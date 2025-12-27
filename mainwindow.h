#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QtNetwork/QNetworkInterface>
#include <QtNetwork/QHostAddress>
#include <QCloseEvent>
#include <QProgressDialog>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QTimer>
#include <QInputDialog>
#include <QDateTime>
#include <QPushButton>
#include <QHash>
#include <QPainter>
#include <QLabel>
#include <QThread>

#include <opencv2/opencv.hpp>

#include <vector>
#include <cstdint>
#include <cmath>

#include "rtspviewerqt.h"
#include "udpserver.h"
#include "TitleBar.h"
#include "systemsetting.h"
#include "videorecorder.h"

// 你工程里应该已有：UdpDeviceManager / DeviceInfo 定义（通常在 udpserver.h 或别处）
// 若不在 udpserver.h，请把对应头文件 include 过来。

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE
// ====== Color tune (FAST cached LUT) ======
struct LabABFixed {
    float ga = 1.0f;
    float gb = 1.0f;
    float da = 0.0f;
    float db = 0.0f;
    float abShiftClamp = 18.0f;

    float chromaGain  = 1.0f;
    float chromaGamma = 1.0f;
    float chromaMax   = 130.0f;

    bool  keepL = true;
};
class RtspViewerQt;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();



protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onFrame(const QImage& img);

    void updateSystemIP();
    void onSnUpdatedForIpChange(const QString& sn);
    void onIpChangeTimeout();
    void updateTableDevice(const QString& sn);
    void onCheckDeviceAlive();

    void on_action_openCamera_triggered();
    void on_action_closeCamera_triggered();
    void on_action_grap_triggered();
    void on_action_startRecord_triggered();
    void on_action_stopRecord_triggered();
    void on_action_triggered();

    void getMSG(const QString& sn);
    void configCameraForSn(const QString& sn);

signals:
    void sendFrame2Record(const QImage& img);
    void sendFrame2Capture(const QImage& img);
    void startRecord();
    void stopRecord();

private:
    Ui::MainWindow *ui = nullptr;

    // ===== RTSP viewer =====
    RtspViewerQt* viewer_ = nullptr;

    // ===== setting/record =====
    systemsetting* mysystemsetting = nullptr;
    VideoRecorder* myVideoRecorder = nullptr;
    QThread *recThread_ = nullptr;

    QLabel* recIndicator_   = nullptr;
    QTimer* recBlinkTimer_  = nullptr;

    // ---- MediaMTX 管理 ----
    QProcess* mtxProc_ = nullptr;
    void startMediaMTX();
    void stopMediaMTX();
    bool stopMediaMTXBlocking(int gracefulMs = 3000, int killMs = 2000);

    // 当前配置
    QString curBindIp_ = "192.168.194.77";
    int     curRtspPort_ = 10000;
    int     curRtpBase_  = 10002;
    QString curPath_     = "mystream";

    QStringList probeWiredIPv4s();

    // ---- device manager ----
    UdpDeviceManager* mgr_ = nullptr;
    void upsertCameraSN(const QString& sn);

    // ---- 改相机 IP 相关 ----
    QString          pendingIpSn_;
    QString          pendingIpNew_;
    bool             ipChangeWaiting_ = false;
    QProgressDialog* ipWaitDlg_ = nullptr;
    QTimer*          ipChangeTimer_ = nullptr;
    QTimer*          devAliveTimer_ = nullptr;

    void finishIpChange(bool ok, const QString& msg);

    // ---- UI 状态 ----
    QString curSelectedSn_;
    bool    previewActive_ = false;

    void updateCameraButtons();
    void onTableSelectionChanged();
    void doStopViewer();
    void changeCameraIpForSn(const QString& sn);

    // ---- MediaMTX 推流状态 ----
    struct PathState {
        bool   hasPublisher = false;
        qint64 lastPubMs    = 0;
    };
    QHash<QString, PathState> pathStates_;

    void onMediaMtxLogLine(const QString& line);

    // ---- info panel ----
    void updateDeviceInfoPanel(const DeviceInfo* dev, bool online);
    void clearDeviceInfoPanel();
    QHash<QString, qint64> camOnlineSinceMs_;

    // ---- icons ----
    QIcon iconOnline_;
    QIcon iconOffline_;

    // ---- record/capture ----
    bool  isRecording_ = false;
    bool  iscapturing_ = false;

    // ---- title ----
    void titleForm();

    // ====== Color tune members (MUST be members, not globals) ======
    bool enableColorTune_ = true;
    LabABFixed tuneParams_;
    int   meanStride_ = 4;
    float corrRebuildThr_ = 0.5f;

    std::vector<uint16_t> abLut_;
    bool  lutValid_ = false;
    float lastCorrA_ = 1e9f;
    float lastCorrB_ = 1e9f;

    QImage applyColorTuneFast(const QImage& in);
};

#endif // MAINWINDOW_H
