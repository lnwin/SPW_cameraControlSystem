#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>
Q_OS_WIN

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
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // ==== 表格：4 列，多一列“操作” ====
    ui->tableWidget->setColumnCount(4);
    ui->tableWidget->setHorizontalHeaderLabels(QStringList()
                                               << "设备SN"
                                               << "IP地址"
                                               << "状态"
                                               << "操作");
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 选中行变化 → 更新当前 SN + 刷新按钮状态
    connect(ui->tableWidget, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::onTableSelectionChanged);

    mgr_ = new UdpDeviceManager(this);
    mgr_->setDefaultCmdPort(10000);
    if (!mgr_->start(7777, 8888)) {
        qWarning() << "UdpDeviceManager start failed";
        return;
    }
    // 日志
    connect(mgr_, &UdpDeviceManager::logLine, this, [](const QString& s){
        qDebug().noquote() << s;
    });
    // 发现 SN → 更新 UI 下拉框
    connect(mgr_, &UdpDeviceManager::snDiscoveredOrUpdated, this, [this](const QString& sn){
        QMetaObject::invokeMethod(this, [this, sn](){ upsertCameraSN(sn); }, Qt::QueuedConnection);
    });
    // 发现 SN → 额外用于判断“改 IP 是否已经用新 IP 上线”
    connect(mgr_, &UdpDeviceManager::snDiscoveredOrUpdated,
            this, &MainWindow::onSnUpdatedForIpChange);
    // 选中行变化时，更新当前 SN + 按钮状态
    connect(ui->tableWidget->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onTableSelectionChanged);
    // === 周期检查设备是否离线 ===
    devAliveTimer_ = new QTimer(this);
    devAliveTimer_->setInterval(2000); // 每 2 秒检查一次
    connect(devAliveTimer_, &QTimer::timeout,
            this, &MainWindow::onCheckDeviceAlive);
    devAliveTimer_->start();

    // 初始化等待改 IP 的计时器
    ipChangeTimer_ = new QTimer(this);
    ipChangeTimer_->setSingleShot(true);
    connect(ipChangeTimer_, &QTimer::timeout,
            this, &MainWindow::onIpChangeTimeout);

    //========================================================================
    updateSystemIP();
    probeWiredIPv4s();
    startMediaMTX();

    // ==== 初始状态：没有选中任何相机 → 打开/关闭按钮全部禁用 ====
    curSelectedSn_.clear();
    previewActive_ = false;
    updateCameraButtons();
}


void MainWindow::upsertCameraSN(const QString& sn)
{
    if (sn.isEmpty())
        return;

    // 直接更新下面的设备表即可
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
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    ui->label->setPixmap(pm);

    if (!previewActive_) {
        previewActive_ = true;
        updateCameraButtons();   // 第一次收到图像时，刷新一次按钮状态
    }
}


void MainWindow::startMediaMTX()
{
    if (mtxProc_) return;

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
                onMediaMtxLogLine(s);   // ★ 关键：在这里顺手解析有没有 publisher
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

void MainWindow::on_openCamera_clicked()
{
    qInfo() << "[UI] on_openCamera_clicked, curSelectedSn_=" << curSelectedSn_
            << " viewer_=" << viewer_;

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






void MainWindow::on_closeCamera_clicked()
{
    doStopViewer();
}



void MainWindow::on_changeCameraIP_clicked()
{
    // 现在没有 cameraIPCombox，就直接用当前选中的 SN
    QString sn = curSelectedSn_.trimmed();

    changeCameraIpForSn(sn);
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
        ui->messageBox->append("[IP] 未发现可用的有线 IPv4 地址");
    }
    return out;
}
void MainWindow::updateSystemIP()
{
    const QStringList ips = probeWiredIPv4s();
    if (ips.isEmpty()) {
        // 没找到可用 IP，就先清空 / 或保持原值都行
        // 这里我选择不动 curBindIp_，只打个日志
        qWarning() << "[IP] no usable wired IPv4 found, keep curBindIp_ =" << curBindIp_;
        return;
    }

    // 取第一个有线 IPv4 作为 RTSP 监听 IP
    curBindIp_ = ips.first();
    qInfo() << "[IP] curBindIp_ set to" << curBindIp_;
}


void MainWindow::on_changeSystemIP_clicked()
{

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

    QString ip = dev.ip.toString();
    QString status = QStringLiteral("在线");

    // 找这一行是否已经存在
    int row = -1;
    for (int r = 0; r < ui->tableWidget->rowCount(); ++r) {
        QTableWidgetItem *item = ui->tableWidget->item(r, 0);
        if (item && item->text() == sn) {
            row = r;
            break;
        }
    }

    // 不存在则新增
    if (row < 0) {
        row = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(row);
    }

    auto setTextItem = [&](int col, const QString &text) {
        QTableWidgetItem *it = ui->tableWidget->item(row, col);
        if (!it) {
            it = new QTableWidgetItem;
            ui->tableWidget->setItem(row, col, it);
        }
        it->setText(text);
    };

    setTextItem(0, sn);
    setTextItem(1, ip);
    setTextItem(2, status);

    // 在线显示绿色
    QTableWidgetItem* stItem = ui->tableWidget->item(row, 2);
    if (stItem)
        stItem->setForeground(Qt::darkGreen);

    // === 第 3 列：操作按钮“修改IP” ===
    if (!ui->tableWidget->cellWidget(row, 3)) {
        QPushButton* btn = new QPushButton(QStringLiteral("修改IP"), ui->tableWidget);
        // 捕获当前 SN，点击时修改该相机 IP
        connect(btn, &QPushButton::clicked, this, [this, sn]() {
            changeCameraIpForSn(sn);
        });
        ui->tableWidget->setCellWidget(row, 3, btn);
    }

    // 行内容变化后，刷新一下按钮状态（比如刚上线）
    updateCameraButtons();
}

void MainWindow::onCheckDeviceAlive()
{
    if (!mgr_) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 offlineMs = 10000; // 超过 10 秒没心跳就判离线，可按需调整

    const int rows = ui->tableWidget->rowCount();
    for (int r = 0; r < rows; ++r) {
        QTableWidgetItem *snItem = ui->tableWidget->item(r, 0);
        if (!snItem) continue;

        const QString sn = snItem->text();
        DeviceInfo dev;
        QString status;

        bool exists = mgr_->getDevice(sn, dev);
        bool online = exists && (now - dev.lastSeenMs <= offlineMs);

        if (online) status = QStringLiteral("在线");
        else        status = QStringLiteral("离线");

        QTableWidgetItem *stItem = ui->tableWidget->item(r, 2);
        QString oldStatus;
        if (!stItem) {
            stItem = new QTableWidgetItem;
            ui->tableWidget->setItem(r, 2, stItem);
        } else {
            oldStatus = stItem->text();
        }

        stItem->setText(status);
        if (status == QStringLiteral("在线")) {
            stItem->setForeground(Qt::darkGreen);
        } else {
            stItem->setForeground(Qt::red);
        }

        // 在线 → 离线 的边沿：如果是当前预览相机，则自动关闭预览
        if (oldStatus == QStringLiteral("在线") &&
            status   == QStringLiteral("离线") &&
            !curSelectedSn_.isEmpty() &&
            sn == curSelectedSn_ &&
            viewer_) {

            QMessageBox::information(this, tr("提示"),
                                     tr("设备 [%1] 网络中断，预览已自动停止。").arg(sn));
            doStopViewer();
        }
    }

    // 整体状态更新后，统一再刷一次按钮状态
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

    if (!ui || !ui->tableWidget) {
        updateCameraButtons();
        return;
    }

    QItemSelectionModel* sel = ui->tableWidget->selectionModel();
    if (!sel) {
        updateCameraButtons();
        return;
    }

    const QModelIndexList rows = sel->selectedRows();
    if (!rows.isEmpty()) {
        int row = rows.first().row();
        QTableWidgetItem *item = ui->tableWidget->item(row, 0); // 第 0 列是 SN
        if (item) {
            curSelectedSn_ = item->text().trimmed();
        }
    }

    qInfo() << "[UI] selection changed, curSelectedSn_=" << curSelectedSn_;
    updateCameraButtons();
}

void MainWindow::updateCameraButtons()
{
    if (!ui) return;

    QPushButton* btnOpen  = ui->openCamera;
    QPushButton* btnClose = ui->closeCamera;

    // 默认全部禁用
    if (btnOpen)  btnOpen->setEnabled(false);
    if (btnClose) btnClose->setEnabled(false);

    // 0) 没选中任何相机：按钮全禁用（即使 viewer_ 在跑也一样）
    if (curSelectedSn_.isEmpty()) {
        qInfo() << "[UI] updateCameraButtons: no SN selected, all disabled";
        return;
    }

    // 1) 正在修改 IP：强制禁用
    if (ipChangeWaiting_) {
        qInfo() << "[UI] updateCameraButtons: IP change in progress, buttons disabled";
        return;
    }

    // 2) 如果当前已经在预览：允许关闭相机，禁止再打开
    if (viewer_) {
        if (btnClose) btnClose->setEnabled(true);
        if (btnOpen)  btnOpen->setEnabled(false);
        qInfo() << "[UI] updateCameraButtons: viewer active -> enable CLOSE only";
        return;
    }

    // ========= 下面是“没在预览时，决定能不能打开相机”的逻辑 =========

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 3) 心跳判断在线
    DeviceInfo dev;
    bool online = false;
    if (mgr_ && mgr_->getDevice(curSelectedSn_, dev)) {
        const qint64 offlineMs = 10000;
        online = (now - dev.lastSeenMs <= offlineMs);
    }
    if (!online) {
        qInfo() << "[UI] device offline, keep buttons disabled";
        return;
    }

    // 4) MediaMTX 判断有没有 publisher
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
        qInfo() << "[UI] device online but stream not ready, keep buttons disabled";
        return;
    }

    // 5) 在线 + 有 publisher + 没在预览 -> 允许“打开相机”
    if (btnOpen)  btnOpen->setEnabled(true);
    if (btnClose) btnClose->setEnabled(false);
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

    // 弹出输入 IP 的对话框（默认值为当前 IP）
    bool ok = false;
    QString newIp = QInputDialog::getText(
        this,
        tr("修改相机 IP"),
        tr("设备 SN: %1\n当前 IP: %2\n\n请输入新的 IP：").arg(trimmedSn, curIp),
        QLineEdit::Normal,
        curIp,
        &ok
        );

    if (!ok) {
        // 用户按了“取消”
        return;
    }

    newIp = newIp.trimmed();

    // IP 格式简单校验
    QRegularExpression re(R"(^((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)$)");
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
