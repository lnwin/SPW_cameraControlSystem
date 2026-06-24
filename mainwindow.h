#pragma once

#include <QMainWindow>
#include <QThread>
#include <QTimer>
#include <QProgressDialog>
#include <QSharedPointer>
#include <QHash>
#include <QDateTime>

#include "udpserver.h"
#include "rtspviewerqt.h"
#include "ZoomPanImageView.h"
#include "videorecorder.h"
#include "uicontroller.h"
#include "myStruct.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void bindUiController(UiController* ctrl);
    ZoomPanImageView* videoView()             const { return view_; }
    VideoRecorder*    myVideoRecorderPublic() const { return myVideoRecorder; }
    UdpDeviceManager* deviceManager()         const { return mgr_; }

public slots:
    void changeIp(const QString& sn, const QString& newIp);
    void applyRecordOptions(const myRecordOptions& opt);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

signals:
    void sendFrame2Record(QSharedPointer<QImage> img);
    void sendFrame2Capture(QSharedPointer<QImage> img);
    void startRecord();
    void stopRecord();
    qint64 sendCameraExporeGain(const QString& sn, int exposureUs, double gainDb);

private slots:
    void onCheckDeviceAlive();
    void on_action_openCamera_triggered();
    void on_action_closeCamera_triggered();
    void on_action_grap_triggered();
    void on_action_startRecord_triggered();
    void on_action_stopRecord_triggered();
    void onSnUpdatedForIpChange(const QString& sn);
    void onIpChangeTimeout();

private:
    void startPreviewPullTimer();
    void stopPreviewPullTimer();
    void doStopViewer();
    void shutdownAllThreads();
    bool openCameraForSelected(bool showMsgBox);
    bool isControlOnline(const QString& sn, DeviceInfo* outDev = nullptr) const;
    void finishIpChange(bool ok, const QString& msg);

private:
    Ui::MainWindow* ui = nullptr;
    ZoomPanImageView* view_ = nullptr;
    UdpDeviceManager* mgr_ = nullptr;
    RtspViewerQt* viewer_ = nullptr;

    QTimer* devAliveTimer_ = nullptr;
    QTimer* previewPullTimer_ = nullptr;
    QTimer* ipChangeTimer_ = nullptr;
    QTimer* triggerAckTimer_ = nullptr;

    VideoRecorder* myVideoRecorder = nullptr;
    QThread* recThread_ = nullptr;
    QProgressDialog* recSaveDlg_ = nullptr;

    bool    isRecording_ = false;
    bool    iscapturing_ = false;
    qint64  lastFrameMs_ = 0;

    QString curSelectedSn_;
    QString overlayTopText_;
    bool    overlayEnabled_ = false;

    bool    ipChangeWaiting_ = false;
    QString pendingIpSn_;
    QString pendingIpNew_;

    QHash<QString, bool>   offlinePopupShown_;
    QHash<QString, qint64> camOnlineSinceMs_;
    UiController* uiCtrl_ = nullptr;

    int    fpsFrameCount_  = 0;
    qint64 fpsWindowStart_ = 0;
    int    lastFps_        = 0;
};
