#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "themedmessagedialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QPainter>
#include <QProgressDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

// ── 静态帧状态表（避免污染头文件）──────────────────────────────────────────
static QHash<const MainWindow*, qint64> g_dropUntilMs;
static QHash<const MainWindow*, int>    g_noFrameStreak;
static QHash<const MainWindow*, qint64> g_lastNewFrameMs;
static QHash<const MainWindow*, bool>   g_previewLoopOn;
static QHash<const MainWindow*, qint64> g_streamStartMs;
static QHash<const MainWindow*, qint64> g_viewerStartMs;

static int calcNextPullIntervalMs(int streak)
{
    if (streak <= 2)  return 0;
    if (streak <= 8)  return 2;
    if (streak <= 20) return 8;
    if (streak <= 60) return 16;
    return 33;
}

// ── 叠加时间文字 ─────────────────────────────────────────────────────────────
static QImage applyOverlay(const QImage& src, const QString& topText)
{
    QImage dst = src.copy();
    QPainter p(&dst);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QFont font("Arial", 20, QFont::Bold);
    p.setFont(font);
    const QFontMetrics fm(font);
    const int pad = 6;
    const QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                       + (topText.isEmpty() ? "" : "  " + topText);
    const int w = fm.horizontalAdvance(line) + pad * 2;
    const int h = fm.height() + pad * 2;
    const QRect bg((dst.width() - w) / 2, 6, w, h);
    p.fillRect(bg, Qt::black);
    p.setPen(Qt::white);
    p.drawText(bg, Qt::AlignCenter, line);
    return dst;
}

// ── 构造 ─────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    qRegisterMetaType<QSharedPointer<QImage>>("QSharedPointer<QImage>");

    ui->setupUi(this);

    // 用 ZoomPanImageView 替换 .ui 里的占位 label
    if (ui->label) {
        view_ = new ZoomPanImageView(ui->label->parentWidget());
        view_->setObjectName(ui->label->objectName());
        view_->setZoomRange(1.0, 3.0);
        view_->setDisplayCrop(290, 290);
        if (auto* lay = ui->label->parentWidget()->layout())
            lay->replaceWidget(ui->label, view_);
        ui->label->hide();
        ui->label->deleteLater();
        ui->label = nullptr;
    }
    if (view_) view_->installEventFilter(this);

    // 录像线程
    myVideoRecorder = new VideoRecorder;
    recThread_ = new QThread(this);
    myVideoRecorder->moveToThread(recThread_);
    recThread_->start();

    // 恢复 overlay 设置
    {
        QSettings s("SPwater", "CameraControl");
        overlayEnabled_ = s.value("overlay/enabled", true).toBool();
        overlayTopText_ = s.value("overlay/topText", tr("双击改动文字信息")).toString();
    }

    connect(this, &MainWindow::sendFrame2Capture, myVideoRecorder, &VideoRecorder::receiveFrame2Save);
    connect(this, &MainWindow::sendFrame2Record,  myVideoRecorder, &VideoRecorder::receiveFrame2Record);
    connect(this, &MainWindow::startRecord,       myVideoRecorder, &VideoRecorder::startRecording);
    connect(this, &MainWindow::stopRecord,        myVideoRecorder, &VideoRecorder::stopRecording);

    // UDP 设备发现
    mgr_ = new UdpDeviceManager(this);
    mgr_->setDefaultCmdPort(10000);
    if (!mgr_->start(7777, 8888))
        qWarning() << "UdpDeviceManager start failed";

    connect(mgr_, &UdpDeviceManager::snDiscoveredOrUpdated, this, [this](const QString& sn){
        QMetaObject::invokeMethod(this, [this, sn]{
            if (!camOnlineSinceMs_.contains(sn))
                camOnlineSinceMs_.insert(sn, QDateTime::currentMSecsSinceEpoch());
        }, Qt::QueuedConnection);
    });
    connect(mgr_, &UdpDeviceManager::snDiscoveredOrUpdated, this, &MainWindow::onSnUpdatedForIpChange);
    connect(this, &MainWindow::sendCameraExporeGain, mgr_, &UdpDeviceManager::sendSetCameraParams);

    // 设备存活定时器
    devAliveTimer_ = new QTimer(this);
    devAliveTimer_->setInterval(1000);
    connect(devAliveTimer_, &QTimer::timeout, this, &MainWindow::onCheckDeviceAlive);
    devAliveTimer_->start();

    // 自适应拉帧定时器
    previewPullTimer_ = new QTimer(this);
    previewPullTimer_->setTimerType(Qt::PreciseTimer);
    previewPullTimer_->setSingleShot(true);
    connect(previewPullTimer_, &QTimer::timeout, this, [this](){
        if (!viewer_ || !g_previewLoopOn.value(this, false)) {
            g_previewLoopOn[this] = false;
            g_noFrameStreak[this] = 0;
            return;
        }

        QSharedPointer<QImage> img = viewer_->takeLatestFrameIfNew();
        if (img && !img->isNull()) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const qint64 until = g_dropUntilMs.value(this, 0);
            if (!(until > 0 && now < until)) {
                g_lastNewFrameMs[this] = now;
                g_noFrameStreak[this] = 0;
                lastFrameMs_ = now;
                if (g_streamStartMs.value(this, 0) == 0) g_streamStartMs[this] = now;

                // 帧率统计（1秒窗口）
                if (fpsWindowStart_ == 0) fpsWindowStart_ = now;
                fpsFrameCount_++;
                if (now - fpsWindowStart_ >= 1000) {
                    lastFps_ = fpsFrameCount_;
                    fpsFrameCount_ = 0;
                    fpsWindowStart_ = now;
                }

                if (view_) view_->setImage(applyOverlay(*img, overlayTopText_));

                if (isRecording_) {
                    if (overlayEnabled_)
                        emit sendFrame2Record(QSharedPointer<QImage>::create(applyOverlay(*img, overlayTopText_)));
                    else
                        emit sendFrame2Record(img);
                }
                if (iscapturing_) {
                    if (overlayEnabled_)
                        emit sendFrame2Capture(QSharedPointer<QImage>::create(applyOverlay(*img, overlayTopText_)));
                    else
                        emit sendFrame2Capture(img);
                    iscapturing_ = false;
                }
            } else {
                g_noFrameStreak[this] = g_noFrameStreak.value(this, 0) + 1;
            }
        } else {
            g_noFrameStreak[this] = g_noFrameStreak.value(this, 0) + 1;
        }

        const int nextMs = (g_lastNewFrameMs.value(this, 0) == 0)
                         ? qMin(calcNextPullIntervalMs(g_noFrameStreak.value(this, 0)), 8)
                         : calcNextPullIntervalMs(g_noFrameStreak.value(this, 0));
        if (g_previewLoopOn.value(this, false)) previewPullTimer_->start(nextMs);
    });

    // IP 修改超时定时器
    ipChangeTimer_ = new QTimer(this);
    ipChangeTimer_->setSingleShot(true);
    connect(ipChangeTimer_, &QTimer::timeout, this, &MainWindow::onIpChangeTimeout);
}

MainWindow::~MainWindow() { shutdownAllThreads(); delete ui; }

void MainWindow::closeEvent(QCloseEvent* e) { shutdownAllThreads(); e->accept(); }

void MainWindow::applyRecordOptions(const myRecordOptions& opt)
{
    overlayEnabled_ = opt.overlayEnabled;
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == view_ && event->type() == QEvent::MouseButtonDblClick) {
        QInputDialog dlg(this);
        dlg.setWindowTitle(tr("编辑顶部文字"));
        dlg.setLabelText(tr("顶部叠加文字："));
        dlg.setTextValue(overlayTopText_);
        dlg.setStyleSheet("QLineEdit { color: black; background: white; }");
        if (dlg.exec() == QDialog::Accepted) {
            overlayTopText_ = dlg.textValue();
            QSettings s("SPwater", "CameraControl");
            s.setValue("overlay/topText", overlayTopText_);
        }
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}

// ── 拉帧定时器 ───────────────────────────────────────────────────────────────
void MainWindow::startPreviewPullTimer()
{
    g_previewLoopOn[this] = true;
    g_noFrameStreak[this] = 0;
    if (!previewPullTimer_->isActive()) previewPullTimer_->start(0);
}

void MainWindow::stopPreviewPullTimer()
{
    if (previewPullTimer_) previewPullTimer_->stop();
    g_previewLoopOn[this] = false;
    g_noFrameStreak[this] = 0;
}

// ── 设备存活检测 ─────────────────────────────────────────────────────────────
void MainWindow::onCheckDeviceAlive()
{
    if (curSelectedSn_.isEmpty()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool viewerRunning = (viewer_ != nullptr);
    const bool everGotFrame  = (g_streamStartMs.value(this, 0) > 0);
    const bool videoAlive    = (lastFrameMs_ > 0 && (now - lastFrameMs_) <= 1200);
    const bool controlOnline = isControlOnline(curSelectedSn_);
    const bool uiOnline      = controlOnline && (!viewerRunning || videoAlive);
    const qint64 viewerStartMs = g_viewerStartMs.value(this, 0);
    const bool neverGotFrameTimeout = viewerRunning && !everGotFrame
                                   && viewerStartMs > 0 && (now - viewerStartMs) > 8000;

    if (viewerRunning && (everGotFrame || neverGotFrameTimeout) && !uiOnline) {
        // 硬件触发切换期间无帧是预期行为，不弹视频中断
        if (uiCtrl_ && uiCtrl_->triggerWaitingAck()) {
            qInfo("[TRIGGER_UI] suppress video lost dialog during trigger switching");
            return;
        }
        if (isRecording_) on_action_stopRecord_triggered();
        if (!offlinePopupShown_.value(curSelectedSn_, false)) {
            offlinePopupShown_[curSelectedSn_] = true;
            QString dur = tr("未知");
            const qint64 t0 = g_streamStartMs.value(this, 0);
            if (t0 > 0) {
                const qint64 m = (now - t0) / 60000;
                dur = tr("%1小时%2分钟").arg(m / 60).arg(m % 60);
            }
            ThemedMessageDialog::openNonModal(this, tr("提示"),
                tr("设备 [%1] 网络中断或视频断流。\n持续：%2。\n请检查网络后重新打开相机。")
                    .arg(curSelectedSn_, dur));
        }
    }
}

// ── 相机控制 ─────────────────────────────────────────────────────────────────
bool MainWindow::isControlOnline(const QString& sn, DeviceInfo* outDev) const
{
    if (!mgr_) return false;
    DeviceInfo dev;
    if (!mgr_->getDevice(sn, dev)) return false;
    const bool online = (QDateTime::currentMSecsSinceEpoch() - dev.lastSeenMs) <= 10000;
    if (outDev) *outDev = dev;
    return online;
}

bool MainWindow::openCameraForSelected(bool showMsgBox)
{
    g_noFrameStreak[this] = 0; g_lastNewFrameMs[this] = 0;
    g_streamStartMs[this] = 0; g_viewerStartMs[this]  = 0;
    if (viewer_) return true;

    if (curSelectedSn_.isEmpty()) {
        if (showMsgBox) ThemedMessageDialog::warning(this, tr("提示"), tr("请先在列表中选择一台相机。"));
        return false;
    }
    DeviceInfo dev;
    if (!isControlOnline(curSelectedSn_, &dev)) {
        if (showMsgBox) ThemedMessageDialog::warning(this, tr("提示"), tr("设备离线，请确认心跳在线后再打开。"));
        return false;
    }

    const QString url = QString("rtsp://%1:8554/%2").arg(dev.ip.toString(), curSelectedSn_);
    qInfo().noquote() << "[UI] open rtsp url =" << url;

    viewer_ = new RtspViewerQt(this);
    lastFrameMs_ = 0;
    g_noFrameStreak[this] = 0; g_lastNewFrameMs[this] = 0;
    g_dropUntilMs[this]   = QDateTime::currentMSecsSinceEpoch() + 800;
    g_viewerStartMs[this] = QDateTime::currentMSecsSinceEpoch();

    connect(viewer_, &RtspViewerQt::logLine, this, [](const QString& s){ qInfo().noquote() << s; });
    viewer_->setUrl(url);
    viewer_->start();
    startPreviewPullTimer();
    return true;
}

void MainWindow::doStopViewer()
{
    stopPreviewPullTimer();
    if (!viewer_) return;
    RtspViewerQt* v = viewer_;
    viewer_ = nullptr;
    lastFrameMs_ = 0;
    g_streamStartMs[this] = 0;
    g_viewerStartMs[this] = 0;
    connect(v, &QThread::finished, v, &QObject::deleteLater);
    v->stop(); v->quit(); v->wait(1200);
}

void MainWindow::on_action_openCamera_triggered()  { openCameraForSelected(true); }

void MainWindow::on_action_closeCamera_triggered()
{
    doStopViewer();
    g_previewLoopOn[this] = false;
    g_noFrameStreak[this] = 0;
    g_lastNewFrameMs[this] = 0;
}

void MainWindow::on_action_grap_triggered() { iscapturing_ = true; }

void MainWindow::on_action_startRecord_triggered()
{
    if (isRecording_) return;
    if (!viewer_) {
        ThemedMessageDialog::information(this, tr("提示"), tr("请先打开相机预览再开始录制。"));
        return;
    }
    isRecording_ = true;
    emit startRecord();
}

void MainWindow::on_action_stopRecord_triggered()
{
    if (!isRecording_) return;
    isRecording_ = false;

    recSaveDlg_ = new QProgressDialog(tr("正在保存录像，请稍候..."), QString(), 0, 0, this);
    recSaveDlg_->setWindowModality(Qt::WindowModal);
    recSaveDlg_->setCancelButton(nullptr);
    recSaveDlg_->show();

    auto* conn = new QMetaObject::Connection;
    *conn = connect(myVideoRecorder, &VideoRecorder::recordingStopped, this, [this, conn](){
        if (recSaveDlg_) { recSaveDlg_->close(); recSaveDlg_->deleteLater(); recSaveDlg_ = nullptr; }
        disconnect(*conn); delete conn;
    });
    emit stopRecord();
}

// ── IP 修改 ──────────────────────────────────────────────────────────────────
void MainWindow::changeIp(const QString& sn, const QString& newIp)
{
    static const QRegularExpression re(R"(^((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)$)");
    if (!re.match(newIp).hasMatch()) {
        ThemedMessageDialog::warning(nullptr, tr("错误"), tr("IP 地址格式不正确。"));
        return;
    }
    if (curSelectedSn_ == sn && viewer_) doStopViewer();

    const qint64 n = mgr_->sendSetIp(sn, newIp, 16);
    if (n <= 0) { ThemedMessageDialog::warning(nullptr, tr("错误"), tr("发送改 IP 命令失败。")); return; }

    pendingIpSn_ = sn; pendingIpNew_ = newIp; ipChangeWaiting_ = true;

    if (uiCtrl_) {
        uiCtrl_->notifyIpWaiting(tr("正在将 [%1] 的 IP 修改为 %2...").arg(sn, newIp));
        uiCtrl_->appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ") + tr("正在修改 IP：%1 → %2").arg(sn, newIp));
    }
    if (ipChangeTimer_) ipChangeTimer_->start(15000);
}

void MainWindow::onSnUpdatedForIpChange(const QString& sn)
{
    if (!ipChangeWaiting_ || sn != pendingIpSn_ || !mgr_) return;
    DeviceInfo dev;
    if (mgr_->getDevice(sn, dev) && dev.ip.toString() == pendingIpNew_)
        finishIpChange(true, tr("设备 [%1] 的 IP 已成功修改为 %2。").arg(sn, pendingIpNew_));
}

void MainWindow::onIpChangeTimeout()
{
    finishIpChange(false, tr("等待设备 [%1] 使用新 IP [%2] 上线超时。").arg(pendingIpSn_, pendingIpNew_));
}

void MainWindow::finishIpChange(bool ok, const QString& msg)
{
    ipChangeWaiting_ = false;
    if (ipChangeTimer_) ipChangeTimer_->stop();
    if (uiCtrl_) {
        uiCtrl_->notifyIpDone(msg, ok);
        uiCtrl_->appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ") + msg);
    }

    // IP 修改成功后，更新选中 SN 并自动重连
    if (ok && !pendingIpSn_.isEmpty()) {
        curSelectedSn_ = pendingIpSn_;
        // 延迟 1 秒等设备用新 IP 稳定后再拉流
        QTimer::singleShot(1000, this, [this](){
            openCameraForSelected(false);
        });
    }
}

// ── bindUiController ─────────────────────────────────────────────────────────
void MainWindow::bindUiController(UiController* ctrl)
{
    if (!ctrl) return;
    uiCtrl_ = ctrl;

    connect(ctrl, &UiController::requestOpenCamera, this, [this, ctrl](){
        ctrl->setConnecting(true);
        ctrl->appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ") + "正在连接相机...");
        on_action_openCamera_triggered();
        // 30 秒超时：若仍未收到第一帧则复原
        QTimer::singleShot(30000, this, [this, ctrl](){
            if (ctrl->connecting()) {
                ctrl->setConnecting(false);
                ctrl->appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ") + "连接超时，请检查设备和网络");
                doStopViewer();
            }
        });
    });
    connect(ctrl, &UiController::requestCloseCamera, this, [this, ctrl](){
        ctrl->setConnecting(false);
        ctrl->appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ") + "断开相机连接");
        on_action_closeCamera_triggered();
    });
    connect(ctrl, &UiController::requestStartRecord,   this, &MainWindow::on_action_startRecord_triggered);
    connect(ctrl, &UiController::requestStopRecord,    this, &MainWindow::on_action_stopRecord_triggered);
    connect(ctrl, &UiController::requestSnapshot,      this, &MainWindow::on_action_grap_triggered);
    connect(ctrl, &UiController::requestRefreshDevices, this, [this](){ if (mgr_) mgr_->start(7777, 8888); });
    connect(ctrl, &UiController::requestSelectDevice,  this, [this](const QString& sn){
        curSelectedSn_ = sn;
        offlinePopupShown_.remove(sn);
    });
    connect(ctrl, &UiController::requestOpenFolder, this, [this](){
        QDesktopServices::openUrl(QUrl::fromLocalFile("D:/SP_camera_record"));
    });
    connect(ctrl, &UiController::brightnessChanged, this, [this, ctrl](){
        emit sendCameraExporeGain(curSelectedSn_, 0, ctrl->brightness());
    });
    connect(ctrl, &UiController::requestToggleCrosshair, this, [this](bool en){
        if (view_) view_->setCrosshairEnabled(en);
    });

    connect(ctrl, &UiController::requestSetLed, this, [this, ctrl](bool en){
        if (curSelectedSn_.isEmpty() || !isControlOnline(curSelectedSn_)) {
            ThemedMessageDialog::warning(this, tr("提示"), tr("设备未连接，无法下发控制命令。"));
            return;
        }
        const qint64 n = mgr_->sendSetLed(curSelectedSn_, en);
        ctrl->appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ")
            + tr("LED %1").arg(en ? tr("开") : tr("关")) + QString(" bytes=%1").arg(n));
    });

    connect(ctrl, &UiController::requestSetTrigger, this, [this, ctrl](int mode){
        if (curSelectedSn_.isEmpty() || !isControlOnline(curSelectedSn_)) {
            ThemedMessageDialog::warning(this, tr("提示"), tr("设备未连接，无法下发控制命令。"));
            ctrl->handleTriggerTimeout(); // 解锁 UI
            return;
        }
        const QString modeStr = (mode == 0) ? "software" : "hardware";
        const qint64 n = mgr_->sendSetTrigger(curSelectedSn_, modeStr);
        ctrl->appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ")
            + tr("触发模式: %1").arg(modeStr) + QString(" bytes=%1").arg(n));
        if (triggerAckTimer_) triggerAckTimer_->start(5000); // 5s 超时保护
    });

    connect(ctrl, &UiController::noHardwareTriggerFallback, this, [this](){
        qDebug("[TRIGGER_UI] show no hardware trigger dialog");
        ThemedMessageDialog::warning(this, tr("提示"), tr("当前相机不具备硬件触发功能，已退回软件触发。"));
    });

    // 触发模式确认定时器（5s 超时保护）
    triggerAckTimer_ = new QTimer(this);
    triggerAckTimer_->setSingleShot(true);
    connect(triggerAckTimer_, &QTimer::timeout, this, [ctrl](){
        ctrl->handleTriggerTimeout();
    });

    // 解析下位机回传的 TRIGGER_STATUS
    connect(mgr_, &UdpDeviceManager::datagramReceived, this,
            [this, ctrl](const QString&, const QHostAddress& ip, quint16 port, const QByteArray& payload){
        // 只把 JSON 类报文转发到 UI 日志，心跳文本包跳过，避免日志刷屏
        if (payload.trimmed().startsWith('{')) {
            ctrl->appendLog(QString("[UDP_RAW] from=%1:%2 size=%3 data=%4")
                .arg(ip.toString()).arg(port).arg(payload.size())
                .arg(QString::fromUtf8(payload.left(100)).trimmed()));
        }
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject()) return;
        const QJsonObject json = doc.object();
        if (json.value("cmd").toString() != "TRIGGER_STATUS") return;

        ctrl->appendLog(QString("[TRIGGER_UI] TRIGGER_STATUS: requested=%1 current=%2 fallback=%3")
            .arg(json["requested_mode"].toString(), json["current_mode"].toString())
            .arg(json["fallback"].toBool() ? "true" : "false"));

        if (triggerAckTimer_) triggerAckTimer_->stop();
        offlinePopupShown_[curSelectedSn_] = false;
        ctrl->handleTriggerStatus(json);
    });

    connect(myVideoRecorder, &VideoRecorder::recordingStarted, ctrl, [ctrl](const QString& path){
        ctrl->setRecording(true);
        ctrl->setRecordFileName(QFileInfo(path).fileName());
        ctrl->setRecordSegmentIndex(ctrl->recordSegmentIndex() + 1);
        ctrl->appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ") + "开始录像：" + QFileInfo(path).fileName());
    });
    connect(myVideoRecorder, &VideoRecorder::recordingStopped, ctrl, [ctrl](const QString& path){
        ctrl->setRecording(false);
        ctrl->setRecordSegmentIndex(0);
        ctrl->setRecordSegmentElapsed("00:00");
        ctrl->setRecordTotalElapsed("00:00");
        ctrl->appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ") + "录像已保存：" + QFileInfo(path).fileName());
    });
    connect(myVideoRecorder, &VideoRecorder::sendMSG2ui, ctrl, [ctrl](const QString& msg){
        ctrl->appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ") + msg);
    });

    connect(devAliveTimer_, &QTimer::timeout, ctrl, [this, ctrl](){
        DeviceInfo dev;
        const bool online = !curSelectedSn_.isEmpty() && isControlOnline(curSelectedSn_, &dev);
        ctrl->setDeviceOnline(online);
        ctrl->setRtspConnected(viewer_ != nullptr && lastFrameMs_ > 0
                               && (QDateTime::currentMSecsSinceEpoch() - lastFrameMs_) <= 1200);
        ctrl->setDeviceName(online ? dev.sn : (curSelectedSn_.isEmpty() ? "未连接" : curSelectedSn_));
        if (online) ctrl->setDeviceIp(dev.ip.toString());
        if (mgr_) ctrl->setDeviceList(mgr_->allSns());
        ctrl->setSelectedSn(curSelectedSn_);
        ctrl->setCurrentFps(lastFps_);
        // 收到第一帧后清除 connecting 状态
        if (ctrl->connecting() && lastFrameMs_ > 0
            && (QDateTime::currentMSecsSinceEpoch() - lastFrameMs_) <= 1200) {
            ctrl->setConnecting(false);
            ctrl->appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ") + "相机连接成功，视频流已建立");
        }
    });
}

// ── 关闭 ─────────────────────────────────────────────────────────────────────
void MainWindow::shutdownAllThreads()
{
    if (previewPullTimer_) previewPullTimer_->stop();
    if (devAliveTimer_)    devAliveTimer_->stop();
    if (ipChangeTimer_)    ipChangeTimer_->stop();

    if (viewer_) {
        RtspViewerQt* v = viewer_; viewer_ = nullptr;
        v->stop(); v->quit(); v->wait(2000); v->deleteLater();
    }
    if (recThread_) {
        if (isRecording_ && myVideoRecorder) {
            QProgressDialog dlg(tr("正在保存录像..."), QString(), 0, 0, this);
            dlg.setWindowModality(Qt::WindowModal);
            dlg.setCancelButton(nullptr); dlg.show();
            QApplication::processEvents();
            QMetaObject::invokeMethod(myVideoRecorder, "stopRecording", Qt::BlockingQueuedConnection);
        }
        recThread_->quit(); recThread_->wait(5000); recThread_ = nullptr;
    }
    g_dropUntilMs.remove(this); g_previewLoopOn.remove(this);
    g_noFrameStreak.remove(this); g_lastNewFrameMs.remove(this);
    g_streamStartMs.remove(this); g_viewerStartMs.remove(this);
    offlinePopupShown_.clear();
}
