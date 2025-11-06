#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>
Q_OS_WIN
//==================================================================================================================
static bool isValidIPv4(const QString& ip){
    // 简单 IPv4 校验
    static const QRegularExpression re(R"(^((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)$)");
    return re.match(ip).hasMatch();
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
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
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
    // 发现 SN → 更新 UI
    connect(mgr_, &UdpDeviceManager::snDiscoveredOrUpdated, this, [this](const QString& sn){
        QMetaObject::invokeMethod(this, [this, sn](){ upsertCameraSN(sn); }, Qt::QueuedConnection);
    });

    //========================================================================


    on_updateSystemIP_clicked();
    dhcp_ = new DhcpMiniServer(this);
    setCurBindIp(curBindIp_);   // 自动配置浑水相机的主机IP，跟随上位机的前三位，第四位配置为：200-250之间。
    probeWiredIPv4s();
    // relaunchMediaMTX(curBindIp_);
    // 程序启动即拉起 MediaMTX
    //  startMediaMTX();
    // viewer_->start();

}
// MainWindow.cpp
void MainWindow::upsertCameraSN(const QString& sn){
    if (sn.isEmpty()) return;
    auto cb = ui->cameraIPCombox;
    int idx = cb->findText(sn);
    if (idx < 0) cb->addItem(sn);
    else         cb->setItemText(idx, sn); // “相同则替换/刷新”
    cb->setCurrentText(sn);
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
    dhcp_->stop();
    // 停止 MediaMTX（阻塞直到退出）
    stopMediaMTXBlocking();

    // 一切正常，允许退出
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

QString MainWindow::writeMediaMtxYaml(const QString& dir, const MediaMtxRuntimeCfg& c)
{
    const QString cfgPath = QDir(dir).filePath("mediamtx_runtime.yml");

    QString yaml;
    yaml += "logLevel: " + c.logLevel + "\n\n";
    yaml += QString("rtsp: %1\n").arg(c.rtsp ? "yes" : "no");
    yaml += QString("rtmp: %1\n").arg(c.rtmp ? "yes" : "no");
    yaml += QString("hls:  %1\n").arg(c.hls  ? "yes" : "no");
    yaml += QString("webrtc: %1\n").arg(c.webrtc ? "yes" : "no");
    yaml += "\n";

    yaml += "rtspTransports: [";
    for (int i=0;i<c.rtspTransports.size();++i){
        yaml += c.rtspTransports[i];
        if (i+1<c.rtspTransports.size()) yaml += ", ";
    }
    yaml += "]\n";

    yaml += "rtspAddress: " + c.rtspAddress + "\n";
    yaml += "rtpAddress:  " + c.rtpAddress  + "\n";
    yaml += "rtcpAddress: " + c.rtcpAddress + "\n\n";

    yaml += "paths:\n";
    yaml += "  " + c.pathName + ":\n";
    yaml += "    source: publisher\n";
    yaml += QString("    sourceOnDemand: %1\n").arg(c.sourceOnDemand ? "yes" : "no");

    QFile f(cfgPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "[MediaMTX] cannot write yml:" << cfgPath << f.errorString();
        return QString();
    }
    f.write(yaml.toUtf8());
    f.close();
    return cfgPath;
}
void MainWindow::relaunchMediaMTX(const QString& bindIp)
{
    if (!isValidIPv4(bindIp)){
        qWarning() << "[MediaMTX] invalid IPv4:" << bindIp;
        ui->messageBox->append("无效的 IPv4 地址");
        return;
    }
    curBindIp_   = bindIp;
    // 停旧
    stopMediaMTX();

    // 生成新 YAML（全部绑定到指定 IP）
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString mtxDir = QDir(appDir).filePath("mediamtx");
    const QString mtxExe = QDir(mtxDir).filePath("mediamtx.exe");

    if (!QFileInfo::exists(mtxExe)) {
        qWarning() << "[MediaMTX] not found:" << mtxExe; return;
    }

    MediaMtxRuntimeCfg cfg;
    cfg.rtspAddress = QString("%1:%2").arg(curBindIp_).arg(curRtspPort_);
    cfg.rtpAddress  = QString("%1:%2").arg(curBindIp_).arg(curRtspPort_);
    cfg.rtcpAddress = QString("%1:%2").arg(curBindIp_).arg(curRtspPort_+1);

    const QString runtimeCfg = writeMediaMtxYaml(mtxDir, cfg);
    if (runtimeCfg.isEmpty()) return;

    // 起新
    mtxProc_ = new QProcess(this);
    mtxProc_->setWorkingDirectory(mtxDir);
    mtxProc_->setProgram(mtxExe);
    mtxProc_->setArguments({ runtimeCfg });
    mtxProc_->setProcessChannelMode(QProcess::MergedChannels);

    connect(mtxProc_, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus st){
                const QString msg = QString("[MediaMTX] exited, code=%1, status=%2")
                                        .arg(code).arg(st==QProcess::NormalExit?"Normal":"Crash");
                qWarning().noquote() << msg;
                statusBar()->showMessage(msg, 5000);
            });

    mtxProc_->start();
    if (!mtxProc_->waitForStarted(3000)) {
        qWarning() << "[MediaMTX] failed to start:" << mtxProc_->errorString();
        mtxProc_->deleteLater(); mtxProc_ = nullptr;
        ui->messageBox->append(QString::fromUtf8(u8"系统启动失败！请检查IP是否正确！"));
        return;
    }
    else
    {
        ui->messageBox->append(QString::fromUtf8(u8"系统启动成功！"));
    }

}

// 等比显示到 QLabel（不裁剪，留黑边）
void MainWindow::onFrame(const QImage& img)
{
    if (!ui->label) return;

    // Qt 自带保持比例缩放
    QPixmap pm = QPixmap::fromImage(img).scaled(
        ui->label->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    ui->label->setPixmap(pm);
}
void MainWindow::startMediaMTX()
{
    if (mtxProc_) return;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString mtxDir = QDir(appDir).filePath("mediamtx");
    const QString mtxExe = QDir(mtxDir).filePath("mediamtx.exe");
    const QString mtxCfg = QDir(mtxDir).filePath("mediamtx.yml");

    if (!QFileInfo::exists(mtxExe)) {
        qWarning() << "[MediaMTX] not found:" << mtxExe;
        return;
    }

    mtxProc_ = new QProcess(this);
    mtxProc_->setWorkingDirectory(mtxDir);
    mtxProc_->setProgram(mtxExe);

    QStringList args;
    if (QFileInfo::exists(mtxCfg)) {
        args << mtxCfg;               // 直接传配置文件路径（无任何 flag）
    }
    mtxProc_->setArguments(args);
    mtxProc_->setProcessChannelMode(QProcess::MergedChannels);

    connect(mtxProc_, &QProcess::readyReadStandardOutput, this, [this]{
        qDebug().noquote() << QString::fromLocal8Bit(mtxProc_->readAllStandardOutput());
    });
    connect(mtxProc_, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [](int code, QProcess::ExitStatus st){
                qWarning() << "[MediaMTX] exited, code =" << code << "status =" << st;
            });

    mtxProc_->start();
    if (!mtxProc_->waitForStarted(3000)) {
        qWarning() << "[MediaMTX] failed to start:" << mtxProc_->errorString();
        mtxProc_->deleteLater();
        mtxProc_ = nullptr;
        return;
    }
    else
    {
        qInfo() << "[MediaMTX] started at" << mtxExe
                << (QFileInfo::exists(mtxCfg) ? QString("with config \"%1\"").arg(mtxCfg)
                                              : "(no config arg)");
    }
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
    if(viewer_==NULL)
    {
        viewer_ = new RtspViewerQt(this);
        connect(viewer_, &RtspViewerQt::frameReady, this, &MainWindow::onFrame);
        const QString url = QString("rtsp://%1:%2/%3")
                                .arg(curBindIp_)
                                .arg(curRtspPort_)
                                .arg(curPath_);
        viewer_->setUrl(url);
        viewer_->start();
    }
}


void MainWindow::on_closeCamera_clicked()
{
    viewer_->stop();
    viewer_=NULL;
}


void MainWindow::on_changeCameraIP_clicked()
{

}

void MainWindow::setCurBindIp(const QString& ip){
    curBindIp_ = ip;
    // 若 DHCP 已在跑，先停再按新IP配置
    if (dhcp_->isRunning()) dhcp_->stop();
    applyDhcpFromCurBindIp();

    QString err;
    if (!dhcp_->start(&err)) {
        QMessageBox::critical(this, "DHCP 启动失败", err);
    }
}

void MainWindow::applyDhcpFromCurBindIp(){
    // 基于 curBindIp_ 生成同网段的池：.200 ~ .250（/24）
    const QStringList parts = curBindIp_.split('.');
    if (parts.size() != 4) {
        qWarning() << "curBindIp_ invalid:" << curBindIp_;
        return;
    }
    const QString prefix = QString("%1.%2.%3").arg(parts[0], parts[1], parts[2]);
    const QString poolStart = prefix + ".200";
    const QString poolEnd   = prefix + ".250";

    // 配 DHCP 基础参数
    dhcp_->setInterfaceIp(curBindIp_);
    dhcp_->setMask("255.255.255.0");
    dhcp_->setPool(poolStart, poolEnd);
    dhcp_->setLeaseSeconds(3600);
    dhcp_->disableRouterOption(true);   // 👈 关键：不发送默认路由


    qInfo().noquote() << QString("DHCP config by curBindIp_: iface=%1, pool=%2-%3, mask=255.255.255.0, gw=%4")
                             .arg(curBindIp_, poolStart, poolEnd, curBindIp_);
}
void MainWindow::on_updateCameraIP_clicked()
{

    //setCurBindIp(curBindIp_);   // 会自动 stop → 重新配置 → start

}


void MainWindow::on_changeSystemIP_clicked()
{
    curBindIp_   =ui->systemIPcomboBox->currentText();

    relaunchMediaMTX(curBindIp_);
    setCurBindIp(curBindIp_);
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
void MainWindow::on_updateSystemIP_clicked()
{
    const QStringList ips = probeWiredIPv4s();

    // 将结果写入 UI 下拉框
    if (ui->systemIPcomboBox) {
        ui->systemIPcomboBox->clear();
        ui->systemIPcomboBox->addItems(ips);
        if (!ips.isEmpty())
            ui->systemIPcomboBox->setCurrentIndex(0);
    }
     curBindIp_  = ips.first();
}

