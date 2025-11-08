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
    updateSystemIP();
    probeWiredIPv4s();
    startMediaMTX();

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
    const QString mtxDir = QDir(appDir).filePath("mediamtx");   // ✅ FIX: 正确拼接子目录
    const QString mtxExe = QDir(mtxDir).filePath("mediamtx.exe");
    const QString mtxCfg = QDir(mtxDir).filePath("mediamtx.yml");

    if (!QFileInfo::exists(mtxExe)) {
        qWarning().noquote() << "[MediaMTX] not found exe:" << mtxExe;
        return;
    }

    mtxProc_ = new QProcess(this);
    mtxProc_->setWorkingDirectory(mtxDir);  // ✅ 让 yml 里的相对路径按此目录解析
    mtxProc_->setProgram(mtxExe);

    // ✅ FIX: v1.15.3 不要 --config；如果 yml 存在，作为“位置参数”传入
    QStringList args;
    if (QFileInfo::exists(mtxCfg)) {
        args << mtxCfg;                      // 位置参数
    }
    mtxProc_->setArguments(args);

    // 合并 stdout/stderr 并逐行打印
    mtxProc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(mtxProc_, &QProcess::readyReadStandardOutput, this, [this]{
        const QByteArray all = mtxProc_->readAllStandardOutput();
        for (const QByteArray& line : all.split('\n')) {
            const auto s = QString::fromLocal8Bit(line).trimmed();
            if (!s.isEmpty()) qInfo().noquote() << "[MediaMTX]" << s;
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
    if(viewer_==NULL)
    {
        viewer_ = new RtspViewerQt(this);
        connect(viewer_, &RtspViewerQt::frameReady, this, &MainWindow::onFrame);

        curPath_=ui->cameraIPCombox->currentText();

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
    const QString sn   = ui->cameraIPCombox->currentText().trimmed();
    const QString ip   = ui->cameraTargetIP->text().trimmed();
    const int     mask = 16;        // 比如 24
    QRegularExpression re(R"(^((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)$)");
    if (!re.match(ip).hasMatch()){ qDebug("IP 格式不对"); return; }
    if (mask < 1 || mask > 30){ qDebug("mask 建议 1~30"); return; }

    const qint64 n = mgr_->sendSetIp(sn, ip, mask);
    qDebug()<<(QString("sendSetIp ret=%1").arg(n));

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

    // 将结果写入 UI 下拉框
    if (ui->systemIPcomboBox) {
        ui->systemIPcomboBox->clear();
        ui->systemIPcomboBox->addItems(ips);
        if (!ips.isEmpty())
            ui->systemIPcomboBox->setCurrentIndex(0);
    }
     curBindIp_  = ips.first();
}


void MainWindow::on_changeSystemIP_clicked()
{

}

