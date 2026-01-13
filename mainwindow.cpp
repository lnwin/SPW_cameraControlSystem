// mainwindow.cpp  (FULL REPLACEABLE, MATCHES YOUR mainwindow.h)

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QCloseEvent>
#include <QNetworkInterface>

// ---------- Optional TitleBar ----------
#if defined(__has_include)
#if __has_include("titlebar.h")
#include "titlebar.h"
#define HAVE_TITLEBAR 1
#else
#define HAVE_TITLEBAR 0
#endif
#else
#define HAVE_TITLEBAR 0
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// -------------------- Filter IPv4 --------------------
static bool isUsableIPv4(const QHostAddress& ip) {
    if (ip.protocol() != QAbstractSocket::IPv4Protocol) return false;
    const quint32 v = ip.toIPv4Address();
    if ((v & 0xFF000000u) == 0x7F000000u) return false; // 127.0.0.0/8
    if ((v & 0xFFFF0000u) == 0xA9FE0000u) return false; // 169.254.0.0/16
    if (v == 0u) return false;                           // 0.0.0.0
    return true;
}

// -------------------- dot icon helper --------------------
static QIcon makeDotIcon(const QColor& fill, const QColor& border) {
    const int size = 12;
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(border); pen.setWidth(1);
    p.setPen(pen);
    p.setBrush(fill);
    p.drawEllipse(1, 1, size - 2, size - 2);
    return QIcon(pm);
}

// -------------------- probe wired ipv4s (NOT a member) --------------------
static QStringList probeWiredIPv4sStatic()
{
    QStringList out;
    QSet<QString> dedup;

    const auto ifs = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : ifs) {
        const bool likelyEthernet =
            (iface.type() == QNetworkInterface::Ethernet) ||
            iface.humanReadableName().contains("Ethernet", Qt::CaseInsensitive) ||
            iface.humanReadableName().contains(QStringLiteral("以太网"));

        if (!likelyEthernet) continue;

        const auto flags = iface.flags();
        if (!(flags.testFlag(QNetworkInterface::IsUp) &&
              flags.testFlag(QNetworkInterface::IsRunning))) continue;
        if (flags.testFlag(QNetworkInterface::IsLoopBack)) continue;

        for (const QNetworkAddressEntry& e : iface.addressEntries()) {
            const QHostAddress ip = e.ip();
            if (!isUsableIPv4(ip)) continue;
            const QString s = ip.toString();
            if (!dedup.contains(s)) {
                dedup.insert(s);
                out << s;
            }
        }
    }
    return out;
}

// ===================== “绿屏/花屏”显示抑制（仅 UI 丢帧） =====================
// 不改头文件：用静态表记录每个 MainWindow 的 “丢帧截止时间”
static QHash<const MainWindow*, qint64> g_dropUntilMs;
// ===================== Adaptive preview pull (UI polling without fixed FPS) =====================
// 不改头文件：用静态表记录每个 MainWindow 的自适应拉帧状态
static QHash<const MainWindow*, int>    g_noFrameStreak;   // 连续无帧次数
static QHash<const MainWindow*, qint64> g_lastNewFrameMs;  // 上一次拿到新帧的时刻（UI侧）
static QHash<const MainWindow*, bool>   g_previewLoopOn;   // 是否已启动自适应循环

// 计算下一次拉帧间隔（毫秒）：有帧 -> 尽快；无帧 -> 退避省电（不固定FPS）
static int calcNextPullIntervalMs(int noFrameStreak)
{
    // 你可按功耗要求调大/调小
    if (noFrameStreak <= 2)  return 0;   // 刚启动/刚断一下：立即再试（更顺滑）
    if (noFrameStreak <= 8)  return 2;
    if (noFrameStreak <= 20) return 8;
    if (noFrameStreak <= 60) return 16;
    return 33; // 长时间无帧：进一步降频（省电）
}

// ===================================================================
//                          MainWindow
// ===================================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    qRegisterMetaType<QSharedPointer<QImage>>("QSharedPointer<QImage>");

    ui->setupUi(this);

    // replace label with ZoomPanImageView
    if (ui->label) {
        view_ = new ZoomPanImageView(ui->label->parentWidget());
        view_->setObjectName(ui->label->objectName());
        view_->setZoomRange(1.0, 3.0);

        if (auto* lay = ui->label->parentWidget()->layout()) {
            lay->replaceWidget(ui->label, view_);
        }
        ui->label->hide();
        ui->label->deleteLater();
        ui->label = nullptr;
    }

    // title bar (optional)
    titleForm();

    // recorder indicator
    recIndicator_ = new QLabel(view_);
    recIndicator_->setFixedSize(32, 32);
    recIndicator_->move(8, 8);
    recIndicator_->setStyleSheet(
        "background-color: red;"
        "border-radius: 16px;"
        "border: 1px solid white;"
        );
    recIndicator_->hide();
    recIndicator_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    recBlinkTimer_ = new QTimer(this);
    recBlinkTimer_->setInterval(500);
    connect(recBlinkTimer_, &QTimer::timeout, this, [this](){
        if (!recIndicator_) return;
        if (!isRecording_) { recIndicator_->hide(); return; }
        recIndicator_->setVisible(!recIndicator_->isVisible());
    });

    // systemsetting / recorder
    mysystemsetting = new systemsetting();
    myVideoRecorder = new VideoRecorder;
    recThread_ = new QThread(this);
    myVideoRecorder->moveToThread(recThread_);
    recThread_->start();

    connect(mysystemsetting, &systemsetting::sendRecordOptions,
            myVideoRecorder, &VideoRecorder::receiveRecordOptions);

    connect(this, &MainWindow::sendFrame2Capture,
            myVideoRecorder, &VideoRecorder::receiveFrame2Save);
    connect(this, &MainWindow::sendFrame2Record,
            myVideoRecorder, &VideoRecorder::receiveFrame2Record);

    connect(this, &MainWindow::startRecord,
            myVideoRecorder, &VideoRecorder::startRecording);
    connect(this, &MainWindow::stopRecord,
            myVideoRecorder, &VideoRecorder::stopRecording);

    // NOTE: header里没有 getMSG()，因此用 lambda 替代
    connect(myVideoRecorder, &VideoRecorder::sendMSG2ui,
            this, [this](const QString& s){
                if (!ui || !ui->messageBox) return;
                const QString t = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                ui->messageBox->append(QString("[%1] %2").arg(t, s));
            });

    // splitter
    if (ui->deviceSplitter) {
        ui->deviceSplitter->setHandleWidth(3);
        ui->deviceSplitter->setStretchFactor(0, 3);
        ui->deviceSplitter->setStretchFactor(1, 2);
    }

    // deviceList
    if (ui->deviceList) {
        ui->deviceList->setColumnCount(3);
        ui->deviceList->setHorizontalHeaderLabels({ "设备名称", "设备状态", "修改IP" });

        auto* header = ui->deviceList->horizontalHeader();
        header->setSectionResizeMode(0, QHeaderView::Stretch);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);

        ui->deviceList->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->deviceList->setEditTriggers(QAbstractItemView::NoEditTriggers);

        connect(ui->deviceList, &QTableWidget::itemSelectionChanged,
                this, &MainWindow::onTableSelectionChanged);
    }

    // icons
    iconOnline_       = makeDotIcon(QColor(0, 200, 0), QColor(0, 120, 0));
    iconOffline_      = makeDotIcon(QColor(180, 180, 180), QColor(120, 120, 120));

    // UdpDeviceManager
    mgr_ = new UdpDeviceManager(this);
    mgr_->setDefaultCmdPort(10000);
    if (!mgr_->start(7777, 8888)) {
        qWarning() << "UdpDeviceManager start failed";
    }

    connect(mgr_, &UdpDeviceManager::logLine, this, [](const QString& s) {
        qDebug().noquote() << s;
    });

    connect(mgr_, &UdpDeviceManager::snDiscoveredOrUpdated,
            this, [this](const QString& sn) {
                QMetaObject::invokeMethod(this, [this, sn]{
                        upsertCameraSN(sn);
                    }, Qt::QueuedConnection);
            });

    connect(mgr_, &UdpDeviceManager::snDiscoveredOrUpdated,
            this, &MainWindow::onSnUpdatedForIpChange);

    // timers
    devAliveTimer_ = new QTimer(this);
    devAliveTimer_->setInterval(1000);
    connect(devAliveTimer_, &QTimer::timeout, this, &MainWindow::onCheckDeviceAlive);
    devAliveTimer_->start();

    previewPullTimer_ = new QTimer(this);
    previewPullTimer_->setTimerType(Qt::PreciseTimer);
    previewPullTimer_->setSingleShot(true);   // 关键：singleShot + 自调度（不固定FPS）

    connect(previewPullTimer_, &QTimer::timeout, this, [this](){
        if (!previewPullTimer_) return;

        // 如果没在预览或 viewer 已空，停止循环（避免空转）
        if (!viewer_ || !g_previewLoopOn.value(this, false)) {
            g_previewLoopOn[this] = false;
            g_noFrameStreak[this] = 0;
            return;
        }

        bool gotNewFrame = false;

        // 取最新帧（如果没有新帧，takeLatestFrameIfNew 应该返回 null）
        QSharedPointer<QImage> img = viewer_->takeLatestFrameIfNew();
        if (img && !img->isNull()) {

            const qint64 now = QDateTime::currentMSecsSinceEpoch();

            // 冷启动/重连“丢帧窗口”：隐藏绿屏/半帧（你已有逻辑，保留）
            const qint64 until = g_dropUntilMs.value(this, 0);
            if (!(until > 0 && now < until)) {
                gotNewFrame = true;
                g_lastNewFrameMs[this] = now;
                g_noFrameStreak[this] = 0;

                // 关键：来一帧就立刻投喂处理线程
                emit sendFrameToColorTune(img);
            } else {
                // 仍处在丢帧窗口，视作“无帧”（继续快速拉，尽快出稳态）
                g_noFrameStreak[this] = g_noFrameStreak.value(this, 0) + 1;
            }

        } else {
            g_noFrameStreak[this] = g_noFrameStreak.value(this, 0) + 1;
        }

        // 自适应调度下一次拉帧：有帧尽快、无帧退避
        int nextMs = calcNextPullIntervalMs(g_noFrameStreak.value(this, 0));

        // 进一步优化“第一次卡”的主观体验：刚启动的前 1 秒，哪怕无帧也不要退避太快
        // 避免首个 IDR 来得慢时，UI 拉帧频率过低导致“更像卡住”
        const qint64 now2 = QDateTime::currentMSecsSinceEpoch();
        const qint64 t0   = g_lastNewFrameMs.value(this, 0);
        if (t0 == 0) {
            // 还没拿到过任何新帧：保持相对积极的拉取频率
            nextMs = qMin(nextMs, 8);
        } else {
            // 已经拿到过帧：正常自适应
            (void)gotNewFrame;
        }

        if (g_previewLoopOn.value(this, false) && previewPullTimer_) {
            previewPullTimer_->start(nextMs);
        }
    });


    ipChangeTimer_ = new QTimer(this);
    ipChangeTimer_->setSingleShot(true);
    connect(ipChangeTimer_, &QTimer::timeout, this, &MainWindow::onIpChangeTimeout);

    // start ColorTune worker
    startColorTuneThread();

    // init
    curSelectedSn_.clear();
    previewActive_ = false;
    clearDeviceInfoPanel();
    updateSystemIP();
    updateCameraButtons();
}

MainWindow::~MainWindow()
{
    shutdownAllThreads();

    delete ui;
    ui = nullptr;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    shutdownAllThreads();
    event->accept();
}

// -------------------- title bar --------------------
void MainWindow::titleForm()
{
#if HAVE_TITLEBAR
    TitleBar *title = new TitleBar(this);
    setMenuWidget(title);

    connect(title, &TitleBar::minimizeRequested, this, &MainWindow::showMinimized);
    connect(title, &TitleBar::maximizeRequested, [this](){
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(title, &TitleBar::closeRequested, this, &MainWindow::close);
#else
    // no titlebar
#endif
}

// ====== ColorTune worker thread ======
void MainWindow::startColorTuneThread()
{
    if (colorThread_) return;

    qRegisterMetaType<QImage>("QImage");

    colorWorker_ = new ColorTuneWorker();
    colorThread_ = new QThread(this);
    colorWorker_->moveToThread(colorThread_);

    connect(colorThread_, &QThread::finished, colorWorker_, &QObject::deleteLater);

    connect(this, &MainWindow::sendFrameToColorTune,
            colorWorker_, &ColorTuneWorker::onFrameIn,
            Qt::QueuedConnection);

    connect(colorWorker_, &ColorTuneWorker::frameOut,
            this, &MainWindow::onColorTunedFrame,
            Qt::QueuedConnection);

    colorThread_->start();
}

void MainWindow::stopColorTuneThread()
{
    if (!colorThread_) return;
    colorThread_->quit();
    colorThread_->wait(1500);
    colorThread_ = nullptr;
    colorWorker_ = nullptr;
}

void MainWindow::startPreviewPullTimer()
{
    if (!previewPullTimer_) return;

    // 启动自适应循环：立即拉一次（不固定FPS）
    g_previewLoopOn[this] = true;
    g_noFrameStreak[this] = 0;
    // 注意：g_lastNewFrameMs 这里不清零，让“重连”也能更平滑；
    // 但在 openCameraForSelected 里我们会主动清一次，作为“新会话”
    if (!previewPullTimer_->isActive())
        previewPullTimer_->start(0);
}

void MainWindow::stopPreviewPullTimer()
{
    if (previewPullTimer_) previewPullTimer_->stop();
    g_previewLoopOn[this] = false;
    g_noFrameStreak[this] = 0;
}


// -------------------- network ip --------------------
void MainWindow::updateSystemIP()
{
    const QStringList ips = probeWiredIPv4sStatic();
    if (ips.isEmpty()) {
        qWarning() << "[IP] no usable wired IPv4 found, keep curBindIp_ =" << curBindIp_;
        if (ui && ui->lblHostIp) ui->lblHostIp->setText(tr("无可用 IP"));
        return;
    }
    curBindIp_ = ips.first();
    if (ui && ui->lblHostIp) ui->lblHostIp->setText(curBindIp_);
}

// -------------------- device discovery upsert --------------------
void MainWindow::upsertCameraSN(const QString& sn)
{
    if (sn.isEmpty() || !mgr_) return;
    updateTableDevice(sn);
}

void MainWindow::updateTableDevice(const QString& sn)
{
    if (!ui || !ui->deviceList || !mgr_) return;

    DeviceInfo dev;
    if (!mgr_->getDevice(sn, dev)) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!camOnlineSinceMs_.contains(sn)) camOnlineSinceMs_.insert(sn, now);

    QString name = dev.sn;
    if (name.isEmpty()) name = sn;
    QString displayName = (name != sn) ? QString("%1 | %2").arg(name, sn) : sn;

    int row = -1;
    for (int r = 0; r < ui->deviceList->rowCount(); ++r) {
        auto* item = ui->deviceList->item(r, 0);
        if (item && item->data(Qt::UserRole).toString() == sn) { row = r; break; }
    }
    if (row < 0) {
        row = ui->deviceList->rowCount();
        ui->deviceList->insertRow(row);
    }

    // col0
    QTableWidgetItem* nameItem = ui->deviceList->item(row, 0);
    if (!nameItem) {
        nameItem = new QTableWidgetItem;
        ui->deviceList->setItem(row, 0, nameItem);
    }
    nameItem->setText(displayName);
    nameItem->setData(Qt::UserRole, sn);
    nameItem->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    // col1 status
    QTableWidgetItem* stItem = ui->deviceList->item(row, 1);
    if (!stItem) {
        stItem = new QTableWidgetItem;
        ui->deviceList->setItem(row, 1, stItem);
    }
    stItem->setIcon(iconOnline_);
    stItem->setText(QStringLiteral("在线"));
    stItem->setTextAlignment(Qt::AlignCenter);
    stItem->setForeground(Qt::black);

    // col2 button
    if (!ui->deviceList->cellWidget(row, 2)) {
        auto* btnIp = new QPushButton(QStringLiteral("修改IP"), ui->deviceList);
        connect(btnIp, &QPushButton::clicked, this, [this, sn] { changeCameraIpForSn(sn); });
        ui->deviceList->setCellWidget(row, 2, btnIp);
    }

    updateCameraButtons();
}

// -------------------- selection --------------------
void MainWindow::onTableSelectionChanged()
{
    curSelectedSn_.clear();
    if (!ui || !ui->deviceList) { clearDeviceInfoPanel(); updateCameraButtons(); return; }

    auto* sel = ui->deviceList->selectionModel();
    if (!sel) { clearDeviceInfoPanel(); updateCameraButtons(); return; }

    const QModelIndexList rows = sel->selectedRows();
    if (!rows.isEmpty()) {
        const int row = rows.first().row();
        QTableWidgetItem* nameItem = ui->deviceList->item(row, 0);
        if (nameItem) curSelectedSn_ = nameItem->data(Qt::UserRole).toString().trimmed();
    }

    if (curSelectedSn_.isEmpty()) {
        clearDeviceInfoPanel();
    } else {
        DeviceInfo dev;
        const bool online = isControlOnline(curSelectedSn_, &dev);
        updateDeviceInfoPanel(online ? &dev : nullptr, online);
    }

    updateCameraButtons();
}

// -------------------- helper: control online --------------------
bool MainWindow::isControlOnline(const QString& sn, DeviceInfo* outDev) const
{
    if (!mgr_) return false;
    DeviceInfo dev;
    if (!mgr_->getDevice(sn, dev)) return false;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 offlineMs = 10000;
    const bool online = (now - dev.lastSeenMs <= offlineMs);

    if (outDev) *outDev = dev;
    return online;
}

void MainWindow::onCheckDeviceAlive()
{
    if (!ui || !ui->deviceList || !mgr_) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 offlineMs = 10000;

    const int rows = ui->deviceList->rowCount();
    for (int r = 0; r < rows; ++r) {

        QTableWidgetItem* nameItem = ui->deviceList->item(r, 0);
        if (!nameItem) continue;

        const QString sn = nameItem->data(Qt::UserRole).toString();
        if (sn.isEmpty()) continue;

        DeviceInfo dev;
        const bool exists = mgr_->getDevice(sn, dev);
        const bool ctrlOnline = exists && (now - dev.lastSeenMs <= offlineMs);

        const bool isSelected = (!curSelectedSn_.isEmpty() && sn == curSelectedSn_);
        const bool viewerRunning = (viewer_ != nullptr) && isSelected;

        const bool everGotFrame = (viewerRunning && lastFrameMs_ > 0);
        const bool streamRecentlyAlive =
            (everGotFrame && (now - lastFrameMs_) <= 1200);

        // B方案：必须“曾经收到过至少 1 帧”后，才允许判定断流/累计 strike
        if (viewerRunning && ctrlOnline && everGotFrame && !streamRecentlyAlive) {
            streamStrikes_[sn] = streamStrikes_.value(sn, 0) + 1;
        } else {
            streamStrikes_[sn] = 0;
        }

        const bool streamDown = viewerRunning && ctrlOnline && everGotFrame
                                && (streamStrikes_.value(sn, 0) >= 3);


        // UI 统一在线态：控制在线且视频不处于断流
        const bool uiOnline = ctrlOnline && !streamDown;

        // status item
        QTableWidgetItem* stItem = ui->deviceList->item(r, 1);
        if (!stItem) {
            stItem = new QTableWidgetItem;
            ui->deviceList->setItem(r, 1, stItem);
        }
        const QString oldText = stItem->text();

        if (!uiOnline) {
            stItem->setIcon(iconOffline_);
            stItem->setText(QStringLiteral("离线"));
            stItem->setForeground(Qt::gray);
        } else {
            stItem->setIcon(iconOnline_);
            stItem->setText(QStringLiteral("在线"));
            stItem->setForeground(Qt::black);
        }
        stItem->setTextAlignment(Qt::AlignCenter);

        // 从离线恢复到在线：清除“只弹一次”门禁
        if (uiOnline && oldText == QStringLiteral("离线")) {
            offlinePopupShown_.remove(sn);
        }

        // online since（首次记录）
        if (uiOnline && !camOnlineSinceMs_.contains(sn))
            camOnlineSinceMs_[sn] = now;

        // selected info panel
        if (isSelected) {
            if (uiOnline) updateDeviceInfoPanel(&dev, true);
            else clearDeviceInfoPanel();
        }

        // B方案：从未出帧阶段不弹窗（避免“刚点开就弹”）
        if (viewerRunning && everGotFrame && !uiOnline) {

            if (!offlinePopupShown_.value(sn, false)) {
                offlinePopupShown_[sn] = true;

                auto* box = new QMessageBox(QMessageBox::Information,
                                            tr("提示"),
                                            tr("设备 [%1] 网络中断或视频断流。\n请检查网络后重新打开相机。").arg(sn),
                                            QMessageBox::Ok,
                                            this);
                box->setAttribute(Qt::WA_DeleteOnClose, true);
                box->open(); // 非阻塞
            }
        }
    }

    updateCameraButtons();
}

// -------------------- viewer stop --------------------
void MainWindow::doStopViewer()
{
    stopPreviewPullTimer();

    if (!viewer_) return;

    RtspViewerQt* v = viewer_;
    viewer_ = nullptr;
    previewActive_ = false;
    lastFrameMs_ = 0;

    connect(v, &QThread::finished, this, [this, v]() {
        v->deleteLater();
        updateCameraButtons();
    });

    v->stop();
    v->quit();
    v->wait(1200);

    updateCameraButtons();
}

// -------------------- New: open camera state machine --------------------
bool MainWindow::openCameraForSelected(bool showMsgBox)
{
    if (viewer_) return true;

    if (curSelectedSn_.isEmpty()) {
        if (showMsgBox) QMessageBox::warning(this, tr("提示"), tr("请先在列表中选择一台相机。"));
        return false;
    }

    DeviceInfo dev;
    if (!isControlOnline(curSelectedSn_, &dev)) {
        if (showMsgBox) QMessageBox::warning(this, tr("提示"), tr("设备离线，请确认心跳在线后再打开。"));
        return false;
    }

    const QString devIp = dev.ip.toString();
    const int rtspPort = 8554;
    const QString path = curSelectedSn_;
    const QString url = QString("rtsp://%1:%2/%3").arg(devIp).arg(rtspPort).arg(path);

    qInfo().noquote() << "[UI] open rtsp url =" << url;

    viewer_ = new RtspViewerQt(this);
    previewActive_ = false;
    lastFrameMs_ = 0;

    // 新会话：重置 UI 拉帧状态（避免上一次的退避影响“第一次体验”）
    g_noFrameStreak[this]  = 0;
    g_lastNewFrameMs[this] = 0;

    // drop UI frames for a short window after (re)start
    // 适当加到 800ms：更能覆盖“等IDR/解码器起帧”的不稳定期（你也可改回 600）
    g_dropUntilMs[this] = QDateTime::currentMSecsSinceEpoch() + 800;


    connect(viewer_, &RtspViewerQt::logLine, this, [](const QString& s){
        qInfo().noquote() << s;
    });

    viewer_->setUrl(url);
    viewer_->start();
    startPreviewPullTimer();

    updateCameraButtons();
    return true;
}


// -------------------- update buttons --------------------
void MainWindow::updateCameraButtons()
{
    if (!ui) return;

    QAction* actOpen      = ui->action_openCamera;
    QAction* actClose     = ui->action_closeCamera;
    QAction* actGrab      = ui->action_grap;
    QAction* actStartRec  = ui->action_startRecord;
    QAction* actStopRec   = ui->action_stopRecord;

    auto disableAll = [&](){
        if (actOpen)     actOpen->setEnabled(false);
        if (actClose)    actClose->setEnabled(false);
        if (actGrab)     actGrab->setEnabled(false);
        if (actStartRec) actStartRec->setEnabled(false);
        if (actStopRec)  actStopRec->setEnabled(false);
    };

    disableAll();

    if (curSelectedSn_.isEmpty()) return;
    if (ipChangeWaiting_) return;

    DeviceInfo dev;
    if (!isControlOnline(curSelectedSn_, &dev)) return;
    // 正在预览但视频已断流：也视为离线（禁用按钮）
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (viewer_ && lastFrameMs_ > 0 && (now - lastFrameMs_) > 1200) return;


    if (viewer_) {
        if (actOpen)  actOpen->setEnabled(false);
        if (actClose) actClose->setEnabled(true);
        if (actGrab)  actGrab->setEnabled(true);

        if (isRecording_) {
            if (actStartRec) actStartRec->setEnabled(false);
            if (actStopRec)  actStopRec->setEnabled(true);
        } else {
            if (actStartRec) actStartRec->setEnabled(true);
            if (actStopRec)  actStopRec->setEnabled(false);
        }
        return;
    }

    if (actOpen)  actOpen->setEnabled(true);
}

// -------------------- UI actions --------------------
void MainWindow::on_action_openCamera_triggered()
{
    openCameraForSelected(true);
}

void MainWindow::on_action_closeCamera_triggered()
{
    doStopViewer();
    g_previewLoopOn[this] = false;
    g_noFrameStreak[this] = 0;
    g_lastNewFrameMs[this] = 0;

}

void MainWindow::on_action_grap_triggered()
{
    iscapturing_ = true;
}

void MainWindow::on_action_startRecord_triggered()
{
    if (isRecording_) return;
    if (!viewer_) {
        QMessageBox::information(this, tr("提示"), tr("请先打开相机预览再开始录制。"));
        return;
    }

    isRecording_ = true;

    if (ui->action_startRecord) ui->action_startRecord->setEnabled(false);
    if (ui->action_stopRecord)  ui->action_stopRecord->setEnabled(true);

    if (recIndicator_) recIndicator_->show();
    if (recBlinkTimer_) recBlinkTimer_->start();

    emit startRecord();
}

void MainWindow::on_action_stopRecord_triggered()
{
    if (!isRecording_) return;
    isRecording_ = false;

    if (ui->action_startRecord) ui->action_startRecord->setEnabled(true);
    if (ui->action_stopRecord)  ui->action_stopRecord->setEnabled(false);

    emit stopRecord();

    if (recBlinkTimer_) recBlinkTimer_->stop();
    if (recIndicator_)  recIndicator_->hide();
}

void MainWindow::on_action_triggered()
{
    if (mysystemsetting) mysystemsetting->show();
}

// -------------------- IP change --------------------
void MainWindow::changeCameraIpForSn(const QString& sn)
{
    if (ipChangeWaiting_) {
        QMessageBox::information(this, tr("提示"), tr("已有一个修改 IP 操作正在进行，请稍候。"));
        return;
    }

    const QString trimmedSn = sn.trimmed();
    if (trimmedSn.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择一个设备 ID (SN)。"));
        return;
    }

    if (!curSelectedSn_.isEmpty() && curSelectedSn_ == trimmedSn && viewer_) {
        doStopViewer();
    }

    QString curIp;
    if (mgr_) {
        DeviceInfo dev;
        if (mgr_->getDevice(trimmedSn, dev)) curIp = dev.ip.toString();
    }
    if (curIp.isEmpty()) curIp = "192.168.0.100";

    QInputDialog dlg(this);
    dlg.setWindowTitle(tr("修改相机 IP"));
    dlg.setLabelText(tr("设备 SN: %1\n当前 IP: %2\n\n请输入新的 IP：").arg(trimmedSn, curIp));
    dlg.setTextValue(curIp);
    if (QLineEdit* edit = dlg.findChild<QLineEdit*>()) edit->setStyleSheet("color:#000000;");
    if (dlg.exec() != QDialog::Accepted) return;

    QString newIp = dlg.textValue().trimmed();

    static const QRegularExpression re(
        R"(^((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)$)");
    if (!re.match(newIp).hasMatch()) {
        QMessageBox::warning(this, tr("错误"), tr("IP 地址格式不正确，请重新输入。"));
        return;
    }

    const int mask = 16;
    const qint64 n = mgr_->sendSetIp(trimmedSn, newIp, mask);
    if (n <= 0) {
        QMessageBox::warning(this, tr("错误"), tr("发送改 IP 命令失败（ret=%1）。").arg(n));
        return;
    }

    pendingIpSn_     = trimmedSn;
    pendingIpNew_    = newIp;
    ipChangeWaiting_ = true;

    if (!ipWaitDlg_) {
        ipWaitDlg_ = new QProgressDialog(this);
        ipWaitDlg_->setWindowModality(Qt::ApplicationModal);
        ipWaitDlg_->setCancelButton(nullptr);
        ipWaitDlg_->setMinimum(0);
        ipWaitDlg_->setMaximum(0);
        ipWaitDlg_->setAutoClose(false);
        ipWaitDlg_->setAutoReset(false);
    }

    ipWaitDlg_->setWindowTitle(tr("正在修改 IP"));
    ipWaitDlg_->setLabelText(tr("正在将设备 [%1] 的 IP 从 %2 修改为 %3...\n请等待设备使用新 IP 重新上线。")
                                 .arg(trimmedSn, curIp, newIp));
    ipWaitDlg_->show();

    if (ipChangeTimer_) ipChangeTimer_->start(15000);

    updateCameraButtons();
}

void MainWindow::onSnUpdatedForIpChange(const QString& sn)
{
    if (!ipChangeWaiting_) return;
    if (sn != pendingIpSn_) return;
    if (!mgr_) return;

    DeviceInfo dev;
    if (!mgr_->getDevice(sn, dev)) return;

    const QString curIp = dev.ip.toString();
    if (curIp == pendingIpNew_) {
        finishIpChange(true, tr("设备 [%1] 的 IP 已成功修改为 %2。").arg(sn, curIp));
    }
}

void MainWindow::onIpChangeTimeout()
{
    if (!ipChangeWaiting_) return;
    finishIpChange(false, tr("等待设备 [%1] 使用新 IP [%2] 上线超时，可能修改失败。\n请检查网络或设备状态后重试。")
                              .arg(pendingIpSn_, pendingIpNew_));
}

void MainWindow::finishIpChange(bool ok, const QString& msg)
{
    ipChangeWaiting_ = false;
    if (ipChangeTimer_) ipChangeTimer_->stop();
    if (ipWaitDlg_) ipWaitDlg_->hide();

    QMessageBox::information(this, ok ? tr("修改成功") : tr("修改超时"), msg);
    updateCameraButtons();
}

// -------------------- info panel --------------------
void MainWindow::updateDeviceInfoPanel(const DeviceInfo* dev, bool /*online*/)
{
    if (!ui) return;

    if (ui->lblHostIp) {
        ui->lblHostIp->setText("当前主机IP：" + (curBindIp_.isEmpty() ? tr("--") : curBindIp_));
    }

    if (!dev) {
        if (ui->lblCamIp) ui->lblCamIp->setText("当前相机IP：--");
        if (ui->lblCamLastSeen) ui->lblCamLastSeen->setText("该相机本次上线时间：--");
        return;
    }

    if (ui->lblCamIp) ui->lblCamIp->setText("当前相机IP：" + dev->ip.toString());

    if (ui->lblCamLastSeen) {
        QString sn = dev->sn;
        if (sn.isEmpty()) sn = curSelectedSn_;

        QString tsText = "该相机本次上线时间：--";
        if (!sn.isEmpty()) {
            const qint64 t0 = camOnlineSinceMs_.value(sn, 0);
            if (t0 > 0) {
                const QDateTime dt = QDateTime::fromMSecsSinceEpoch(t0);
                tsText = "该相机本次上线时间：" + dt.toString("yyyy-MM-dd HH:mm:ss");
            }
        }
        ui->lblCamLastSeen->setText(tsText);
    }
}

void MainWindow::clearDeviceInfoPanel()
{
    updateDeviceInfoPanel(nullptr, false);
}

// -------------------- tuned frame out --------------------
void MainWindow::onColorTunedFrame(QSharedPointer<QImage> img)
{
    lastFrameMs_ = QDateTime::currentMSecsSinceEpoch();

    if (!view_) return;
    if (!img || img->isNull()) return;

    view_->setImage(*img);

    if (!previewActive_) {
        previewActive_ = true;
        updateCameraButtons();
    }

    if (isRecording_) emit sendFrame2Record(*img);

    if (iscapturing_) {
        emit sendFrame2Capture(*img);
        iscapturing_ = false;
    }
}

// -------------------- shutdown --------------------
void MainWindow::shutdownAllThreads()
{
    if (previewPullTimer_) previewPullTimer_->stop();
    if (devAliveTimer_)    devAliveTimer_->stop();
    if (ipChangeTimer_)    ipChangeTimer_->stop();
    if (recBlinkTimer_)    recBlinkTimer_->stop();

    if (viewer_) {
        RtspViewerQt* v = viewer_;
        viewer_ = nullptr;

        v->stop();
        v->quit();
        v->wait(2000);
        v->deleteLater();
    }

    if (recThread_) {
        recThread_->quit();
        recThread_->wait(2000);
        recThread_ = nullptr;
    }

    stopColorTuneThread();

    g_dropUntilMs.remove(this);
    g_previewLoopOn.remove(this);
    g_noFrameStreak.remove(this);
    g_lastNewFrameMs.remove(this);

    offlinePopupShown_.clear();

}
