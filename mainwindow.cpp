#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>
Q_OS_WIN
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QElapsedTimer>
#include <QThread>
// 简单封装：启动一个进程并捕获输出
static bool runAndCapture(const QString& program,
                          const QStringList& args,
                          int timeoutMs,
                          QString* outStd = nullptr,
                          int* outExitCode = nullptr)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished();
        return false;
    }
    if (outStd) {
        *outStd = QString::fromLocal8Bit(p.readAllStandardOutput())
                  + QString::fromLocal8Bit(p.readAllStandardError());
    }
    if (outExitCode) {
        *outExitCode = p.exitCode();
    }
    return true;
}

// 查询一次系统里是否有 mediamtx.exe
static bool isMediaMtxRunningOnce()
{
#ifdef Q_OS_WIN
    QString out;
    int exitCode = 0;
    // tasklist /FI "IMAGENAME eq mediamtx.exe"
    if (!runAndCapture("tasklist",
                       {"/FI", "IMAGENAME eq mediamtx.exe"},
                       3000,
                       &out,
                       &exitCode)) {
        return false;
    }
    if (exitCode != 0) {
        return false;
    }
    return out.contains("mediamtx.exe", Qt::CaseInsensitive);
#else
    return false;
#endif
}

// 阻塞式杀掉所有 mediamtx.exe，直到不在运行或超时
static bool killMediaMtxBlocking(int timeoutMs = 5000)
{
#ifdef Q_OS_WIN
    QElapsedTimer timer;
    timer.start();

    bool everFound = false;

    while (isMediaMtxRunningOnce()) {
        everFound = true;
        qInfo().noquote() << "[MediaMTX] found running mediamtx.exe, try kill...";

        // taskkill /F /IM mediamtx.exe
        int exitCode = QProcess::execute("taskkill",
                                         {"/F", "/IM", "mediamtx.exe"});
        qInfo().noquote()
            << "[MediaMTX] taskkill exitCode=" << exitCode;

        // 再检查一次，如果已经没了就 OK
        if (!isMediaMtxRunningOnce()) {
            qInfo().noquote() << "[MediaMTX] all mediamtx.exe killed.";
            return true;
        }

        if (timer.elapsed() > timeoutMs) {
            qWarning().noquote()
                << "[MediaMTX] kill timeout, mediamtx.exe still running!";
            return false;
        }

        QThread::msleep(200);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    if (everFound) {
        qInfo().noquote() << "[MediaMTX] mediamtx.exe not found after kill check.";
    } else {
        qInfo().noquote() << "[MediaMTX] no existing mediamtx.exe running.";
    }
    return true;
#else
    // 非 Windows 平台暂不处理，直接返回 true
    return true;
#endif
}
// 过滤：排除 127.0.0.1 / 169.254.x.x / 0.0.0.0
static bool isUsableIPv4(const QHostAddress& ip) {
    if (ip.protocol() != QAbstractSocket::IPv4Protocol) return false;
    const quint32 v = ip.toIPv4Address();
    if ((v & 0xFF000000u) == 0x7F000000u) return false; // 127.0.0.0/8
    if ((v & 0xFFFF0000u) == 0xA9FE0000u) return false; // 169.254.0.0/16
    if (v == 0u) return false;                           // 0.0.0.0
    return true;
}
#ifdef Q_OS_WIN
#include <windows.h>
static void killProcessTreeWindows(qint64 pid){
    QProcess p;
    p.start("cmd.exe", {"/C", QString("taskkill /PID %1 /T /F").arg(pid)});
    p.waitForFinished(3000);
}
#endif
//===================================================================================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    mysystemsetting=new systemsetting();
    myVideoRecorder=new VideoRecorder;
    recThread_ = new QThread(this);
    myVideoRecorder->moveToThread(recThread_);
    recThread_->start();
    ui->setupUi(this);   
    titleForm();
    // 允许 label 被压缩，不以 pixmap 大小作为最小尺寸
    ui->label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    ui->label->setMinimumSize(0, 0);

    // ====== 录制指示灯：叠在预览 label 左上角 ======
    recIndicator_ = new QLabel(ui->label);      // 作为 label 的子控件
    recIndicator_->setFixedSize(32, 32);
    recIndicator_->move(8, 8);                  // 距离左上角 8 像素
    recIndicator_->setStyleSheet(
        "background-color: red;"
        "border-radius: 8px;"                   // ★ 半径 = 宽高的一半 -> 正圆
        "border: 1px solid white;"
        );
    recIndicator_->hide();
    recIndicator_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    recBlinkTimer_ = new QTimer(this);
    recBlinkTimer_->setInterval(500);           // 500ms 闪烁
    connect(recBlinkTimer_, &QTimer::timeout, this, [this](){
        if (!recIndicator_) return;
        if (!isRecording_) {
            recIndicator_->hide();
            return;
        }
        recIndicator_->setVisible(!recIndicator_->isVisible());
    });

    // ====== 上下分割条样式 ======
    ui->deviceSplitter->setHandleWidth(3);
    ui->deviceSplitter->setStyleSheet(
        "QSplitter::handle {"
        "    background: #aaaaaa;"
        "}"
        "QSplitter::handle:vertical {"
        "    height: 4px;"
        "}"
        );
    ui->deviceSplitter->setStretchFactor(0, 3);
    ui->deviceSplitter->setStretchFactor(1, 2);

    // ====== 设备列表：QTableWidget，3 列 ======
    ui->deviceList->setColumnCount(3);
    ui->deviceList->setHorizontalHeaderLabels(
        { "设备名称", "设备状态", "功能" });

    auto* header = ui->deviceList->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);          // 名称：拉伸
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents); // 状态
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents); // 按钮

    ui->deviceList->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->deviceList->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->deviceList, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::onTableSelectionChanged);
    connect(mysystemsetting, &systemsetting::sendRecordOptions,myVideoRecorder, &VideoRecorder::receiveRecordOptions);


    connect(this, &MainWindow::sendFrame2Capture,myVideoRecorder, &VideoRecorder::receiveFrame2Save);
    connect(this, &MainWindow::sendFrame2Record,myVideoRecorder, &VideoRecorder::receiveFrame2Record);

    connect(this, &MainWindow::startRecord, myVideoRecorder, &VideoRecorder::startRecording);

    connect(this, &MainWindow::stopRecord,  myVideoRecorder, &VideoRecorder::stopRecording);
    connect(myVideoRecorder, &VideoRecorder::sendMSG2ui,this, &MainWindow::getMSG);


    // ====== 状态小圆点图标（在线 / 离线） ======
    auto makeDotIcon = [](const QColor& fill, const QColor& border) -> QIcon {
        const int size = 12;
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(border);
        pen.setWidth(1);
        p.setPen(pen);
        p.setBrush(fill);
        p.drawEllipse(1, 1, size - 2, size - 2);
        p.end();

        return QIcon(pm);
    };

    iconOnline_  = makeDotIcon(QColor(0, 200, 0),   QColor(0, 120, 0));    // 绿色圆点
    iconOffline_ = makeDotIcon(QColor(180, 180, 180), QColor(120, 120, 120)); // 灰色圆点

    // ================== UdpDeviceManager ==================
    mgr_ = new UdpDeviceManager(this);
    mgr_->setDefaultCmdPort(10000);
    if (!mgr_->start(7777, 8888)) {
        qWarning() << "UdpDeviceManager start failed";
        return;
    }

    // 日志
    connect(mgr_, &UdpDeviceManager::logLine, this, [](const QString& s) {
        qDebug().noquote() << s;
    });

    // 发现 SN → 更新设备表
    connect(mgr_, &UdpDeviceManager::snDiscoveredOrUpdated,
            this, [this](const QString& sn) {
                QMetaObject::invokeMethod(this, [this, sn] {
                        upsertCameraSN(sn);
                    }, Qt::QueuedConnection);
            });

    // 改 IP 完成检测
    connect(mgr_, &UdpDeviceManager::snDiscoveredOrUpdated,
            this, &MainWindow::onSnUpdatedForIpChange);

    // === 周期检查设备是否离线 ===
    devAliveTimer_ = new QTimer(this);
    devAliveTimer_->setInterval(2000);
    connect(devAliveTimer_, &QTimer::timeout,
            this, &MainWindow::onCheckDeviceAlive);
    devAliveTimer_->start();

    // === 等待改 IP 的计时器 ===
    ipChangeTimer_ = new QTimer(this);
    ipChangeTimer_->setSingleShot(true);
    connect(ipChangeTimer_, &QTimer::timeout,
            this, &MainWindow::onIpChangeTimeout);

    // ==== 系统 IP + MediaMTX ====
    updateSystemIP();   // 里面会顺手更新 lblHostIp
    startMediaMTX();

    // ==== 初始状态 ====
    curSelectedSn_.clear();
    previewActive_ = false;
     clearDeviceInfoPanel();
    updateCameraButtons();
}


void MainWindow::upsertCameraSN(const QString& sn)
{
    if (sn.isEmpty() || !mgr_)
        return;

    updateTableDevice(sn);
}

bool MainWindow::stopMediaMTXBlocking(int gracefulMs, int killMs)
{
    if (!mtxProc_) return true;

    // 弹出“请等待系统断开…”的进度对话框（不可取消）
    QMessageBox tip(this);
    tip.setIcon(QMessageBox::Information);
    tip.setWindowTitle(QString::fromUtf8(u8"正在退出"));
    tip.setText(QString::fromUtf8(u8"请等待系统断开…"));
    tip.show();
    QApplication::processEvents();

    // 先优雅退出
    mtxProc_->terminate();
    if (!mtxProc_->waitForFinished(gracefulMs)) {
#ifdef Q_OS_WIN
        // Windows 下强杀进程树，避免残留占端口
        const qint64 pid = mtxProc_->processId();
        if (pid > 0) killProcessTreeWindows(pid);
        mtxProc_->waitForFinished(killMs);
#else
        mtxProc_->kill();
        mtxProc_->waitForFinished(killMs);
#endif
    }
    tip.close();
    mtxProc_->deleteLater();
    mtxProc_ = nullptr;
    return true;
}
void MainWindow::closeEvent(QCloseEvent* event)
{
    // 停止拉流线程
    if (viewer_) {
        viewer_->stop();
        viewer_->wait(1500);
        viewer_ = nullptr;
    }
   // dhcp_->stop();
    // 停止 MediaMTX（阻塞直到退出）
    stopMediaMTXBlocking();
    event->accept();
}
MainWindow::~MainWindow()
{
    // 程序退出前关闭 MediaMTX
    if (viewer_)
    {
        viewer_->stop();
        viewer_->wait(1000);
    }
    stopMediaMTX();
    delete ui;
}



void MainWindow::onFrame(const QImage& img)
{

    if (!ui->label) return;

    QPixmap pm = QPixmap::fromImage(img).scaled(
        ui->label->size(),
       // Qt::KeepAspectRatio,
        Qt::IgnoreAspectRatio,        //不保持比例
        Qt::SmoothTransformation);
    ui->label->setPixmap(pm);

    if (!previewActive_) {
        previewActive_ = true;
        updateCameraButtons();   // 第一次收到图像时，刷新一次按钮状态
    }

    if( isRecording_ )
    {
        emit sendFrame2Record(img);
    }

    if(iscapturing_)
    {
        emit sendFrame2Capture(img);
        qDebug()<<" emit sendFrame2Capture(img);";
        iscapturing_=false;

    }


}
void MainWindow::getMSG(const QString& sn)
{
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    ui->messageBox->append(QString("[%1] %2").arg(timeStr, sn));
};

void MainWindow::startMediaMTX()
{
    // 如果我们自己已经有一个 QProcess 在跑，就不用再启动
    if (mtxProc_) return;

    // ★ 先杀掉系统里所有已有的 mediamtx.exe，直到都退出或超时
    if (!killMediaMtxBlocking(5000)) {
        qWarning().noquote()
            << "[MediaMTX] abort start: existing mediamtx.exe cannot be terminated.";
        return;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString mtxDir = QDir(appDir).filePath("mediamtx");
    const QString mtxExe = QDir(mtxDir).filePath("mediamtx.exe");
    const QString mtxCfg = QDir(mtxDir).filePath("mediamtx.yml");

    if (!QFileInfo::exists(mtxExe)) {
        qWarning().noquote() << "[MediaMTX] not found exe:" << mtxExe;
        return;
    }

    mtxProc_ = new QProcess(this);
    mtxProc_->setWorkingDirectory(mtxDir);
    mtxProc_->setProgram(mtxExe);

    QStringList args;
    if (QFileInfo::exists(mtxCfg)) {
        args << mtxCfg;
    }
    mtxProc_->setArguments(args);

    // 合并 stdout/stderr 并逐行打印 + 解析
    mtxProc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(mtxProc_, &QProcess::readyReadStandardOutput, this, [this]{
        const QByteArray all = mtxProc_->readAllStandardOutput();
        for (const QByteArray& line : all.split('\n')) {
            const auto s = QString::fromLocal8Bit(line).trimmed();
            if (!s.isEmpty()) {
                qInfo().noquote() << "[MediaMTX]" << s;
                onMediaMtxLogLine(s);   // ★ 这里顺手解析有没有 publisher
            }
        }
    });

    connect(mtxProc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e){
        qWarning() << "[MediaMTX] QProcess error:" << e << mtxProc_->errorString();
    });

    connect(mtxProc_, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus st){
                qWarning() << "[MediaMTX] exited, code=" << code << "status=" << st;
                qWarning().noquote()
                    << "[MediaMTX] hint: check port conflicts (554/8554/8000/9000/10000), and"
                    << "relative paths in mediamtx.yml (base dir):" << mtxProc_->workingDirectory();

                mtxProc_->deleteLater();
                mtxProc_ = nullptr;

                // MediaMTX 掉了，所有 path 的 publisher 状态也就无效了
                pathStates_.clear();
                updateCameraButtons();
            });

    mtxProc_->start();

    if (!mtxProc_->waitForStarted(3000)) {
        qWarning() << "[MediaMTX] failed to start:" << mtxProc_->errorString();
        delete mtxProc_;
        mtxProc_ = nullptr;
        return;
    }

    qInfo().noquote() << "[MediaMTX] start OK ->" << mtxExe
                      << (QFileInfo::exists(mtxCfg) ? QString(" \"%1\"").arg(mtxCfg)
                                                    : " (no explicit yml; using default search)");
}




void MainWindow::stopMediaMTX()
{
    if (!mtxProc_) return;

    // 尝试优雅退出，超时则 kill
    mtxProc_->terminate();
    if (!mtxProc_->waitForFinished(2000))
    {
        mtxProc_->kill();
        mtxProc_->waitForFinished(1000);
    }
    mtxProc_->deleteLater();
    mtxProc_ = nullptr;
}









QStringList MainWindow::probeWiredIPv4s()
{
    QStringList out;
    QSet<QString> dedup;

    const auto ifs = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : ifs) {
        // 1) 仅 Ethernet；有些驱动可能标 Unknown，这里兼容性更强一点
        const bool likelyEthernet =
            (iface.type() == QNetworkInterface::Ethernet) ||
            iface.humanReadableName().contains("Ethernet", Qt::CaseInsensitive) ||
            iface.humanReadableName().contains(QStringLiteral("以太网"));

        if (!likelyEthernet) continue;

        // 2) 必须是启用且运行，且不是回环
        const auto flags = iface.flags();
        if (!(flags.testFlag(QNetworkInterface::IsUp) &&
              flags.testFlag(QNetworkInterface::IsRunning)) ) continue;
        if (flags.testFlag(QNetworkInterface::IsLoopBack)) continue;

        // 3) 取 IPv4
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

    // 没找到时兜底：给出常见私网段提示（可选）
    if (out.isEmpty()) {
        //
        //ui->textEdit->append("[IP] 未发现可用的有线 IPv4 地址");
    }
    return out;
}
void MainWindow::updateSystemIP()
{
    const QStringList ips = probeWiredIPv4s();
    if (ips.isEmpty()) {
        qWarning() << "[IP] no usable wired IPv4 found, keep curBindIp_ =" << curBindIp_;
        if (ui->lblHostIp)
            ui->lblHostIp->setText(tr("无可用 IP"));
        return;
    }

    curBindIp_ = ips.first();
    qInfo() << "[IP] curBindIp_ set to" << curBindIp_;

    if (ui->lblHostIp)
        ui->lblHostIp->setText(curBindIp_);
}



void MainWindow::onSnUpdatedForIpChange(const QString& sn)
{
    if (!ipChangeWaiting_)
        return;
    if (sn != pendingIpSn_)
        return;
    if (!mgr_)
        return;

    DeviceInfo dev;
    if (!mgr_->getDevice(sn, dev))
        return;

    const QString curIp = dev.ip.toString();
    if (curIp == pendingIpNew_) {
        // 成功：同一个 SN，已经用目标 IP 出现
        finishIpChange(true,
                       tr("设备 [%1] 的 IP 已成功修改为 %2。")
                           .arg(sn, curIp));
    }
}
// 等待改IP超时
void MainWindow::onIpChangeTimeout()
{
    if (!ipChangeWaiting_)
        return;

    finishIpChange(false,
                   tr("等待设备 [%1] 使用新 IP [%2] 上线超时，"
                      "可能修改失败。\n请检查网络或设备状态后重试。")
                       .arg(pendingIpSn_, pendingIpNew_));
}

// 统一收尾逻辑：停止计时器、关闭等待框、弹提示
void MainWindow::finishIpChange(bool ok, const QString& msg)
{
    ipChangeWaiting_ = false;
    if (ipChangeTimer_)
        ipChangeTimer_->stop();

    if (ipWaitDlg_)
        ipWaitDlg_->hide();

    QMessageBox::information(this,
                             ok ? tr("修改成功") : tr("修改超时"),
                             msg);
}
void MainWindow::updateTableDevice(const QString& sn)
{
    DeviceInfo dev;
    if (!mgr_->getDevice(sn, dev))
        return;

    // 记录“本次上线开始时间”：第一次发现这个 SN 时写入
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!camOnlineSinceMs_.contains(sn)) {
        camOnlineSinceMs_.insert(sn, now);
    }

    // 名称 和 SN
    QString name = dev.sn;      // 按你的 DeviceInfo 实际字段改
    if (name.isEmpty())
        name = sn;

    QString displayName;
    if (!name.isEmpty() && name != sn)
        displayName = QString("%1 | %2").arg(name, sn);
    else
        displayName = sn;

    // 查找是否已有这一行（通过 SN）
    int row = -1;
    for (int r = 0; r < ui->deviceList->rowCount(); ++r) {
        auto* item = ui->deviceList->item(r, 0);
        if (!item) continue;
        if (item->data(Qt::UserRole).toString() == sn) {
            row = r;
            break;
        }
    }

    if (row < 0) {
        row = ui->deviceList->rowCount();
        ui->deviceList->insertRow(row);
    }

    // 第 0 列：设备名称（显示 名称 | SN），data 里存 SN
    QTableWidgetItem* nameItem = ui->deviceList->item(row, 0);
    if (!nameItem) {
        nameItem = new QTableWidgetItem;
        ui->deviceList->setItem(row, 0, nameItem);
    }
    nameItem->setText(displayName);
    nameItem->setData(Qt::UserRole, sn);
    nameItem->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    // 第 1 列：状态（刚发现默认在线） -> 绿色圆点 + “在线”
    QTableWidgetItem* stItem = ui->deviceList->item(row, 1);
    if (!stItem) {
        stItem = new QTableWidgetItem;
        ui->deviceList->setItem(row, 1, stItem);
    }
    stItem->setIcon(iconOnline_);
    stItem->setText(QStringLiteral("在线"));
    stItem->setTextAlignment(Qt::AlignCenter);
    stItem->setForeground(Qt::black);  // 字体颜色，用黑色即可
    // 不再 setBackground()，避免被选中状态覆盖

    // 第 2 列：功能按钮（修改 IP）
    if (!ui->deviceList->cellWidget(row, 2)) {
        auto* btn = new QPushButton(QStringLiteral("修改IP"), ui->deviceList);
        connect(btn, &QPushButton::clicked, this, [this, sn] {
            changeCameraIpForSn(sn);
        });
        ui->deviceList->setCellWidget(row, 2, btn);
    }

    updateCameraButtons();
}


void MainWindow::onCheckDeviceAlive()
{
    if (!mgr_)
        return;

    const qint64 now       = QDateTime::currentMSecsSinceEpoch();
    const qint64 offlineMs = 10000;

    const int rows = ui->deviceList->rowCount();
    for (int r = 0; r < rows; ++r) {
        QTableWidgetItem* nameItem = ui->deviceList->item(r, 0);
        if (!nameItem)
            continue;

        const QString sn = nameItem->data(Qt::UserRole).toString();
        if (sn.isEmpty())
            continue;

        DeviceInfo dev;
        const bool exists = mgr_->getDevice(sn, dev);
        const bool online = exists && (now - dev.lastSeenMs <= offlineMs);

        // 状态单元格
        QTableWidgetItem* stItem = ui->deviceList->item(r, 1);
        if (!stItem) {
            stItem = new QTableWidgetItem;
            ui->deviceList->setItem(r, 1, stItem);
        }

        const QString oldStatus = stItem->text();
        const QString newStatus = online ? QStringLiteral("在线")
                                         : QStringLiteral("离线");

        stItem->setText(newStatus);
        stItem->setTextAlignment(Qt::AlignCenter);

        if (online) {
            stItem->setIcon(iconOnline_);
            stItem->setForeground(Qt::black);
        } else {
            stItem->setIcon(iconOffline_);
            stItem->setForeground(Qt::gray);
        }
        // 不再 setBackground()，全靠图标 + 字体颜色

        // ★ 1) 离线 -> 在线：表示本次新上线，更新时间戳
        if (online && oldStatus == QStringLiteral("离线")) {
            camOnlineSinceMs_[sn] = now;
        }

        // ★ 2) 如果是当前选中设备，同步刷新下方信息区
        if (!curSelectedSn_.isEmpty() && sn == curSelectedSn_) {

            if (exists) {
                updateDeviceInfoPanel(&dev, online);
            } else {
                clearDeviceInfoPanel();
            }

            // 在线 -> 离线 且正在预览：自动关闭预览
            if (oldStatus == QStringLiteral("在线") &&
                newStatus == QStringLiteral("离线") &&
                viewer_) {

                QMessageBox::information(this, tr("提示"),
                                         tr("设备 [%1] 网络中断，预览已自动停止。").arg(sn));
                doStopViewer();
            }
        }
    }

    updateCameraButtons();
}


void MainWindow::doStopViewer()
{
    if (!viewer_) return;

    qInfo() << "[RTSP] doStopViewer(): stopping viewer thread";
    viewer_->stop();
    viewer_->wait(1500);   // 你之前 closeEvent 里就是这么用的

    viewer_->deleteLater();
    viewer_ = nullptr;
    previewActive_ = false;

    updateCameraButtons();
}


void MainWindow::onTableSelectionChanged()
{
    curSelectedSn_.clear();

    if (!ui || !ui->deviceList) {
        clearDeviceInfoPanel();
        updateCameraButtons();
        return;
    }

    auto* sel = ui->deviceList->selectionModel();
    if (!sel) {
        clearDeviceInfoPanel();
        updateCameraButtons();
        return;
    }

    const QModelIndexList rows = sel->selectedRows();
    if (!rows.isEmpty()) {
        const int row = rows.first().row();
        QTableWidgetItem* nameItem = ui->deviceList->item(row, 0);
        if (nameItem)
            curSelectedSn_ = nameItem->data(Qt::UserRole).toString().trimmed();
    }

    qInfo() << "[UI] selection changed, curSelectedSn_=" << curSelectedSn_;

    // 根据当前选中的 SN，更新下方信息区
    if (curSelectedSn_.isEmpty() || !mgr_) {
        clearDeviceInfoPanel();
    } else {
        DeviceInfo dev;
        if (mgr_->getDevice(curSelectedSn_, dev)) {
            const qint64 now       = QDateTime::currentMSecsSinceEpoch();
            const qint64 offlineMs = 10000;
            const bool online      = (now - dev.lastSeenMs <= offlineMs);
            updateDeviceInfoPanel(&dev, online);
        } else {
            clearDeviceInfoPanel();
        }
    }

    updateCameraButtons();
}


void MainWindow::updateCameraButtons()
{
    if (!ui) return;

    // toolbar 上的所有 QAction
    QAction* actOpen      = ui->action_openCamera;
    QAction* actClose     = ui->action_closeCamera;
    QAction* actGrab      = ui->action_grap;
    QAction* actStartRec  = ui->action_startRecord;
    QAction* actStopRec   = ui->action_stopRecord;

    auto setAllEnabled = [&](bool en) {
        if (actOpen)     actOpen->setEnabled(en);
        if (actClose)    actClose->setEnabled(en);
        if (actGrab)     actGrab->setEnabled(en);
        if (actStartRec) actStartRec->setEnabled(en);
        //if (actStopRec)  actStopRec->setEnabled(!en);
    };

    // 默认：全部禁用
    setAllEnabled(false);

    // 0) 没选中任何相机：全部 disabled
    if (curSelectedSn_.isEmpty()) {
        qInfo() << "[UI] updateCameraButtons: no SN selected, all disabled";
        return;
    }

    // 1) 正在修改 IP：强制禁用
    if (ipChangeWaiting_) {
        qInfo() << "[UI] updateCameraButtons: IP change in progress, buttons disabled";
        return;
    }

    // 2) 如果当前已经在预览：允许关闭相机，
    //    抓图和录制根据录制状态控制
    //
    // 这里假设有一个成员变量 bool isRecording_ 表示“当前是否正在录制”
    // 如果你项目里变量名不一样，改成你自己的即可。
    if (viewer_) {
        // 打开/关闭
        if (actOpen)  actOpen->setEnabled(false);
        if (actClose) actClose->setEnabled(true);

        // 抓图：只要在预览就允许
        if (actGrab)  actGrab->setEnabled(true);

        // 录制：根据 isRecording_ 控制
        if (isRecording_) {
            if (actStartRec) actStartRec->setEnabled(false);
            if (actStopRec)  actStopRec->setEnabled(true);
        } else {
            if (actStartRec) actStartRec->setEnabled(true);
            if (actStopRec)  actStopRec->setEnabled(false);
        }

        qInfo() << "[UI] updateCameraButtons: viewer active, "
                   "grab & record enabled according to recording state";
        return;
    }

    // ========= 下面是“没在预览时，决定能不能打开相机”的逻辑 =========

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 3) 心跳判断在线：不在线 -> 所有保持 disabled
    DeviceInfo dev;
    bool online = false;
    if (mgr_ && mgr_->getDevice(curSelectedSn_, dev)) {
        const qint64 offlineMs = 10000;
        online = (now - dev.lastSeenMs <= offlineMs);
    }
    if (!online) {
        qInfo() << "[UI] device offline, keep all actions disabled";
        return;
    }

    // 4) MediaMTX 判断有没有 publisher：没有 -> 所有保持 disabled
    bool pushing = false;
    {
        const QString path = curSelectedSn_;
        auto it = pathStates_.find(path);
        if (it != pathStates_.end()) {
            const PathState &ps = it.value();
            pushing = ps.hasPublisher;    // 不再做 5 秒过期
        }
    }
    if (!pushing) {
        qInfo() << "[UI] device online but stream not ready, keep all actions disabled";
        return;
    }

    // 5) 在线 + 有 publisher + 没在预览：
    //    只允许“打开相机”，其他都禁用
    if (actOpen)     actOpen->setEnabled(true);
    if (actClose)    actClose->setEnabled(false);
    if (actGrab)     actGrab->setEnabled(false);
    if (actStartRec) actStartRec->setEnabled(false);
    if (actStopRec)  actStopRec->setEnabled(false);

    qInfo() << "[UI] device online & pushing, viewer not active -> enable OPEN only";
}




void MainWindow::changeCameraIpForSn(const QString& sn)
{
    if (ipChangeWaiting_) {
        QMessageBox::information(this, tr("提示"),
                                 tr("已有一个修改 IP 操作正在进行，请稍候。"));
        return;
    }

    const QString trimmedSn = sn.trimmed();
    if (trimmedSn.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择一个设备 ID (SN)。"));
        return;
    }

    // 🔴 关键：如果当前正在预览这台相机，先把预览停掉
    if (!curSelectedSn_.isEmpty() &&
        curSelectedSn_ == trimmedSn &&
        viewer_) {
        doStopViewer();   // 会把 viewer_ 置空、previewActive_ = false，并刷新按钮
    }

    // 取当前 IP（用于弹窗里显示 & 默认值）
    QString curIp;
    if (mgr_) {
        DeviceInfo dev;
        if (mgr_->getDevice(trimmedSn, dev)) {
            curIp = dev.ip.toString();
        }
    }

    if (curIp.isEmpty())
        curIp = "192.168.0.100";   // 找不到就给个默认值，防止为空

    // ==== 自己构造 QInputDialog，强制输入框字体为黑色 ====
    QInputDialog dlg(this);
    dlg.setWindowTitle(tr("修改相机 IP"));
    dlg.setLabelText(tr("设备 SN: %1\n当前 IP: %2\n\n请输入新的 IP：")
                         .arg(trimmedSn, curIp));
    dlg.setTextValue(curIp);

    // 把输入框字体颜色改成黑色
    if (QLineEdit* edit = dlg.findChild<QLineEdit*>()) {
        edit->setStyleSheet("color: #000000;");  // 纯黑
    }

    if (dlg.exec() != QDialog::Accepted) {
        // 用户按了“取消”
        return;
    }

    QString newIp = dlg.textValue().trimmed();

    // IP 格式简单校验
    static const QRegularExpression re(
        R"(^((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)$)");
    if (!re.match(newIp).hasMatch()) {
        QMessageBox::warning(this, tr("错误"), tr("IP 地址格式不正确，请重新输入。"));
        return;
    }

    const int mask = 16;  // 你原来就是 16，我保持不变

    const qint64 n = mgr_->sendSetIp(trimmedSn, newIp, mask);
    qDebug() << QString("sendSetIp ret=%1").arg(n);

    if (n <= 0) {
        QMessageBox::warning(this, tr("错误"),
                             tr("发送改 IP 命令失败（ret=%1）。").arg(n));
        return;
    }

    // === 命令已发出，进入“等待设备用新 IP 上线”的阶段 ===
    pendingIpSn_     = trimmedSn;
    pendingIpNew_    = newIp;
    ipChangeWaiting_ = true;

    // 懒加载等待对话框
    if (!ipWaitDlg_) {
        ipWaitDlg_ = new QProgressDialog(this);
        ipWaitDlg_->setWindowModality(Qt::ApplicationModal);
        ipWaitDlg_->setCancelButton(nullptr);        // 不允许取消按钮
        ipWaitDlg_->setMinimum(0);
        ipWaitDlg_->setMaximum(0);                   // 0~0 表示“忙碌”样式
        ipWaitDlg_->setAutoClose(false);
        ipWaitDlg_->setAutoReset(false);
    }

    ipWaitDlg_->setWindowTitle(tr("正在修改 IP"));
    ipWaitDlg_->setLabelText(
        tr("正在将设备 [%1] 的 IP 从 %2 修改为 %3...\n"
           "请等待设备使用新 IP 重新上线。")
            .arg(trimmedSn, curIp, newIp)
        );
    ipWaitDlg_->show();

    // 启动超时计时（例如 15 秒）
    if (ipChangeTimer_)
        ipChangeTimer_->start(15000);
}

void MainWindow::onMediaMtxLogLine(const QString& s)
{
    // 只关心两类日志：
    // 1) session ... is publishing to path 'PATH'
    // 2) [path PATH] closing existing publisher

    static QRegularExpression rePub(
        R"(is publishing to path '([^']+)')",
        QRegularExpression::CaseInsensitiveOption);

    static QRegularExpression reClose(
        R"(\[path ([^]]+)\] closing existing publisher)",
        QRegularExpression::CaseInsensitiveOption);

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // ① publisher 建立
    QRegularExpressionMatch m1 = rePub.match(s);
    if (m1.hasMatch()) {
        const QString path = m1.captured(1).trimmed();
        PathState &ps = pathStates_[path];
        ps.hasPublisher = true;
        ps.lastPubMs    = now;

        qInfo().noquote() << "[UI] MediaMTX: path" << path
                          << "has publisher=1 at" << now;

        // 如果当前选中的 SN 对应这个 path，刷新一次按钮状态
        if (!curSelectedSn_.isEmpty() && curSelectedSn_ == path) {
            updateCameraButtons();
        }
        return;
    }

    // ② publisher 被关闭（可选，用于更快反应）
    QRegularExpressionMatch m2 = reClose.match(s);
    if (m2.hasMatch()) {
        const QString path = m2.captured(1).trimmed();
        auto it = pathStates_.find(path);
        if (it != pathStates_.end()) {
            it->hasPublisher = false;
        }

        qInfo().noquote() << "[UI] MediaMTX: path" << path
                          << "publisher closed";

        if (!curSelectedSn_.isEmpty() && curSelectedSn_ == path) {
            updateCameraButtons();
        }
        return;
    }

    // 其他日志不处理
}
void MainWindow::updateDeviceInfoPanel(const DeviceInfo* dev, bool online)
{


    // 1) 当前主机 IP（始终显示）
    if (ui->lblHostIp) {
        ui->lblHostIp->setText("当前主机IP：" +
                               (curBindIp_.isEmpty() ? tr("--") : curBindIp_));
    }

    // 2) 没有设备（未选中）时
    if (!dev) {
        if (ui->lblCamIp)
            ui->lblCamIp->setText("当前相机IP：--");

        if (ui->lblCamLastSeen)
            ui->lblCamLastSeen->setText("相机上线时间：--");

        return;
    }

    // 3) 有选中设备：显示相机 IP 和状态
    if (ui->lblCamIp)
        ui->lblCamIp->setText("当前相机IP：" + dev->ip.toString());

    // 4) 该相机“本次上线开始时间”：来自 camOnlineSinceMs_
    if (ui->lblCamLastSeen) {
        QString sn;
        // 优先用 dev 里的 sn，如果没有就退回当前选中 SN
        sn = dev->sn;
        if (sn.isEmpty())
            sn = curSelectedSn_;

        QString tsText = "该相机本次上线时间：--";

        if (!sn.isEmpty()) {
            const qint64 t0 = camOnlineSinceMs_.value(sn, 0);
            if (t0 > 0) {
                QDateTime dt = QDateTime::fromMSecsSinceEpoch(t0);
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


void MainWindow::on_action_openCamera_triggered()
{
    if (viewer_) {
        // 已经有 viewer 在跑了，防止重复点击
        return;
    }
    if (curSelectedSn_.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先在列表中选择一台相机。"));
        return;
    }

    // RTSP path 就用 SN
    curPath_ = curSelectedSn_;

    const QString url = QString("rtsp://%1:%2/%3")
                            .arg(curBindIp_)
                            .arg(curRtspPort_)
                            .arg(curPath_);

    qInfo().noquote() << "[RTSP] start viewer url =" << url;

    viewer_ = new RtspViewerQt(this);
    previewActive_ = false;

    connect(viewer_, &RtspViewerQt::frameReady,
            this, &MainWindow::onFrame);

    viewer_->setUrl(url);
    viewer_->start();

    // 创建 viewer 后，按钮状态交给统一逻辑
    updateCameraButtons();
}


void MainWindow::on_action_closeCamera_triggered()
{
    doStopViewer();
}


void MainWindow::on_action_grap_triggered()
{

    iscapturing_ = true;
    qDebug()<<"on_action_grap_triggered";

}


void MainWindow::on_action_startRecord_triggered()
{
    if (isRecording_) return;

    isRecording_ = true;
    ui->action_startRecord->setEnabled(false);
    ui->action_stopRecord->setEnabled(true);

    if (recIndicator_) recIndicator_->show();
    if (recBlinkTimer_) recBlinkTimer_->start();

    emit startRecord();
}

void MainWindow::on_action_stopRecord_triggered()
{
    if (!isRecording_) return;

    isRecording_ = false;
    ui->action_startRecord->setEnabled(true);
    ui->action_stopRecord->setEnabled(false);

    emit stopRecord();

    if (recBlinkTimer_) recBlinkTimer_->stop();
    if (recIndicator_)  recIndicator_->hide();
}



void MainWindow::on_action_triggered()
{
    mysystemsetting->show();
}

void MainWindow::titleForm()
{
    // 创建自定义标题栏
    TitleBar *title = new TitleBar(this);

    // 把它放到 QMainWindow 的“菜单栏区域”，会自动在所有 toolbar 上面
    setMenuWidget(title);

    // 按钮信号 -> 窗口行为
    connect(title, &TitleBar::minimizeRequested, this, &MainWindow::showMinimized);
    connect(title, &TitleBar::maximizeRequested, [this](){
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(title, &TitleBar::closeRequested, this, &MainWindow::close);
}
