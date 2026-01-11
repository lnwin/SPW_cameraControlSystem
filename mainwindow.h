#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QtNetwork/QNetworkInterface>
#include <QtNetwork/QHostAddress>
#include <QCloseEvent>
#include <QProgressDialog>
#include <QMessageBox>
#include <QTimer>
#include <QInputDialog>
#include <QDateTime>
#include <QPushButton>
#include <QHash>
#include <QPainter>
#include <QLabel>
#include <QThread>
#include <QSharedPointer>
#include <QImage>
#include <QSet>

#include "rtspviewerqt.h"
#include "udpserver.h"        // UdpDeviceManager / DeviceInfo
#include "TitleBar.h"
#include "systemsetting.h"
#include "videorecorder.h"
#include "colortuneworker.h"  // LabABFixed + ColorTuneWorker
#include "ZoomPanImageView.h" // ZoomPanImageView

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

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
    // NOTE: 新结构下不再依赖 viewer_->frameReady 推送；
    // 这个槽保留（兼容/调试），但默认不会被连接。
    void onFrame(QSharedPointer<QImage> img);

    void onColorTunedFrame(QSharedPointer<QImage> img);

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

    void onTableSelectionChanged();

signals:
    void sendFrame2Record(const QImage& img);
    void sendFrame2Capture(const QImage& img);
    void startRecord();
    void stopRecord();

    void sendFrameToColorTune(QSharedPointer<QImage> img); // UI -> worker

private:
    Ui::MainWindow *ui = nullptr;
    ZoomPanImageView* view_ = nullptr;

    // ===== ColorTune thread =====
    QThread*         colorThread_ = nullptr;
    ColorTuneWorker* colorWorker_ = nullptr;
    void startColorTuneThread();
    void stopColorTuneThread();

    // ===== RTSP viewer =====
    RtspViewerQt* viewer_ = nullptr;

    // ===== New: UI pull timer (decouple acquisition from presentation) =====
    QTimer* previewPullTimer_ = nullptr;
    void startPreviewPullTimer();
    void stopPreviewPullTimer();

    // ===== setting/record =====
    systemsetting* mysystemsetting = nullptr;
    VideoRecorder* myVideoRecorder = nullptr;
    QThread* recThread_ = nullptr;

    QLabel* recIndicator_ = nullptr;
    QTimer* recBlinkTimer_ = nullptr;

    // ---- MediaMTX ----
    QProcess* mtxProc_ = nullptr;
    void startMediaMTX();
    void stopMediaMTX();
    bool stopMediaMTXBlocking(int gracefulMs = 3000, int killMs = 2000);

    // ---- host rtsp ----
    QString curBindIp_ = "192.168.194.77";
    int     curRtspPort_ = 10000;
    QString curPath_ = "mystream";
    QStringList probeWiredIPv4s();

    // ---- device manager ----
    UdpDeviceManager* mgr_ = nullptr;
    void upsertCameraSN(const QString& sn);

    // ---- IP change ----
    QString pendingIpSn_;
    QString pendingIpNew_;
    bool    ipChangeWaiting_ = false;
    QProgressDialog* ipWaitDlg_ = nullptr;
    QTimer* ipChangeTimer_ = nullptr;
    QTimer* devAliveTimer_ = nullptr;
    void finishIpChange(bool ok, const QString& msg);
    void changeCameraIpForSn(const QString& sn);

    // ---- UI selection ----
    QString curSelectedSn_;
    bool    previewActive_ = false;
    void updateCameraButtons();
    void doStopViewer();

    // ---- MediaMTX pushing state ----
    struct PathState {
        bool   hasPublisher = false;
        qint64 lastPubMs = 0;
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
    bool isRecording_ = false;
    bool iscapturing_ = false;

    // ---- title ----
    void titleForm();

    // ===== Color tune params =====
    bool      enableColorTune_ = true;
    LabABFixed tuneParams_;
    int       meanStride_ = 4;
    float     corrRebuildThr_ = 0.5f;

    // ---- offline/stream alive ----
    QHash<QString, int> offlineStrikes_;
    qint64 lastFrameMs_ = 0;

    // ---- misc (your existing) ----
    void updateTableDeviceInternal(const QString& sn) { updateTableDevice(sn); }
};

#endif // MAINWINDOW_H
