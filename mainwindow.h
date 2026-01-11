// mainwindow.h  (FULL REPLACEABLE)

#pragma once

#include <QMainWindow>
#include <QThread>
#include <QTimer>
#include <QProgressDialog>
#include <QSharedPointer>
#include <QHash>
#include <QDateTime>
#include <QIcon>

#include "udpserver.h"        // UdpDeviceManager / DeviceInfo
#include "rtspviewerqt.h"     // RtspViewerQt
#include "colortuneworker.h"  // ColorTuneWorker (你已有)
#include "zoompanimageview.h" // ZoomPanImageView (你已有)
#include "systemsetting.h"
#include "videorecorder.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

signals:
    void sendFrameToColorTune(QSharedPointer<QImage> img);

    void sendFrame2Capture(const QImage& img);
    void sendFrame2Record(const QImage& img);

    void startRecord();
    void stopRecord();

private slots:
    void onCheckDeviceAlive();
    void onTableSelectionChanged();

    void on_action_openCamera_triggered();
    void on_action_closeCamera_triggered();
    void on_action_grap_triggered();
    void on_action_startRecord_triggered();
    void on_action_stopRecord_triggered();
    void on_action_triggered();

    void onSnUpdatedForIpChange(const QString& sn);
    void onIpChangeTimeout();

    void onColorTunedFrame(QSharedPointer<QImage> img);

private:
    // ===== 你原来已有的功能函数（这里只列出本文会用到的）=====
    void titleForm();
    void startColorTuneThread();
    void stopColorTuneThread();

    void startPreviewPullTimer();
    void stopPreviewPullTimer();

    void updateSystemIP();
    void upsertCameraSN(const QString& sn);
    void updateTableDevice(const QString& sn);

    void updateCameraButtons();
    void updateDeviceInfoPanel(const DeviceInfo* dev, bool online);
    void clearDeviceInfoPanel();

    void changeCameraIpForSn(const QString& sn);
    void finishIpChange(bool ok, const QString& msg);

    void doStopViewer();
    void shutdownAllThreads();

    // ===== 新增：视频健康状态机（关键）=====
    bool openCameraForSelected(bool showMsgBox);

    bool isControlOnline(const QString& sn, DeviceInfo* outDev = nullptr) const;

private:
    Ui::MainWindow* ui = nullptr;

    // view
    ZoomPanImageView* view_ = nullptr;

    // UDP device manager
    UdpDeviceManager* mgr_ = nullptr;

    // viewer
    RtspViewerQt* viewer_ = nullptr;

    // timers
    QTimer* devAliveTimer_ = nullptr;
    QTimer* previewPullTimer_ = nullptr;
    QTimer* ipChangeTimer_ = nullptr;

    // color tune worker
    QThread* colorThread_ = nullptr;
    ColorTuneWorker* colorWorker_ = nullptr;

    // recorder
    systemsetting* mysystemsetting = nullptr;
    VideoRecorder* myVideoRecorder = nullptr;
    QThread* recThread_ = nullptr;

    // record indicator
    QLabel* recIndicator_ = nullptr;
    QTimer*  recBlinkTimer_ = nullptr;
    bool     isRecording_ = false;
    bool     iscapturing_ = false;

    // selection
    QString curSelectedSn_;
    bool previewActive_ = false;

    // ip change
    bool ipChangeWaiting_ = false;
    QString pendingIpSn_;
    QString pendingIpNew_;
    QProgressDialog* ipWaitDlg_ = nullptr;

    // online tracking
    QHash<QString, qint64> camOnlineSinceMs_;
    QHash<QString, int> offlineStrikes_;

    // ===== 新增：视频健康 tracking =====
    qint64 lastFrameMs_ = 0;                    // 最近一帧（UI真正显示出来）的时间
    QHash<QString, int> streamStrikes_;         // 连续“视频无帧”计数（按 SN）
    QHash<QString, qint64> streamDownSinceMs_;  // 视频断流开始时间（按 SN）

    // icons
    QIcon iconOnline_;
    QIcon iconOffline_;


    // network
    QString curBindIp_;
    // 断网弹窗门禁：同一 SN 的一次断网只弹一次；恢复在线后清零
    QHash<QString, bool> offlinePopupShown_;

    // === 你原有其它字段（ColorTune 参数、pathStates_、meanStride_ 等）请保留并放回这里 ===
};
