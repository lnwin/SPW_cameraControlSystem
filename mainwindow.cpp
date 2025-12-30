#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// -------------------- Windows: kill process tree --------------------
#ifdef Q_OS_WIN
static void killProcessTreeWindows(qint64 pid){
    QProcess p;
    p.start("cmd.exe", {"/C", QString("taskkill /PID %1 /T /F").arg(pid)});
    p.waitForFinished(3000);
}
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

// -------------------- QImage <-> cv::Mat --------------------
static inline cv::Mat QImageToBgr8(const QImage& img)
{
    QImage im = img.convertToFormat(QImage::Format_RGB888);
    cv::Mat rgb(im.height(), im.width(), CV_8UC3, (void*)im.bits(), im.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    return bgr.clone();
}

static inline QImage Bgr8ToQImage(const cv::Mat& bgr)
{
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    QImage out((const uchar*)rgb.data, rgb.cols, rgb.rows, (int)rgb.step, QImage::Format_RGB888);
    return out.copy();
}

// -------------------- meanAB stride --------------------
static inline void meanAB_stride(const cv::Mat& lab_u8, int stride, float& meanA, float& meanB)
{
    const int rows = lab_u8.rows, cols = lab_u8.cols;
    uint64_t sumA = 0, sumB = 0, cnt = 0;
    if (stride < 1) stride = 1;

    for (int y = 0; y < rows; y += stride) {
        const cv::Vec3b* row = lab_u8.ptr<cv::Vec3b>(y);
        for (int x = 0; x < cols; x += stride) {
            sumA += row[x][1];
            sumB += row[x][2];
            cnt++;
        }
    }
    if (!cnt) { meanA = 128.f; meanB = 128.f; return; }
    meanA = (float)sumA / (float)cnt;
    meanB = (float)sumB / (float)cnt;
}

// -------------------- build / apply LUT --------------------
static inline void buildAB_LUT(std::vector<uint16_t>& lut,
                               const LabABFixed& p,
                               float corrA, float corrB)
{
    lut.resize(65536);
    const float clampShift = p.abShiftClamp;
    const bool doChroma = (p.chromaGain != 1.0f || p.chromaGamma != 1.0f);

    for (int au=0; au<256; ++au) {
        for (int bu=0; bu<256; ++bu) {
            const float a = (float)au;
            const float b = (float)bu;

            float a0 = (a - 128.0f) + corrA;
            float b0 = (b - 128.0f) + corrB;

            float a_lin = a0 * p.ga + p.da;
            float b_lin = b0 * p.gb + p.db;

            if (doChroma) {
                const float eps = 1e-6f;
                float r = std::sqrt(a_lin*a_lin + b_lin*b_lin) + eps;
                float rn = std::min(r / 128.0f, 1.5f);
                float rn2 = std::pow(std::max(rn, 0.0f), p.chromaGamma) * p.chromaGain;
                float r2 = rn2 * 128.0f;
                r2 = std::min(r2, p.chromaMax);
                float scale = r2 / r;
                a_lin *= scale;
                b_lin *= scale;
            }

            float a2 = a_lin + 128.0f;
            float b2 = b_lin + 128.0f;

            float da2 = std::clamp(a2 - a, -clampShift, clampShift);
            float db2 = std::clamp(b2 - b, -clampShift, clampShift);
            a2 = a + da2;
            b2 = b + db2;

            int ai = (int)std::lround(std::clamp(a2, 0.f, 255.f));
            int bi = (int)std::lround(std::clamp(b2, 0.f, 255.f));

            lut[(au<<8) | bu] = (uint16_t)((ai & 255) | ((bi & 255) << 8));
        }
    }
}

static inline void applyAB_LUT_inplace(cv::Mat& lab_u8, const std::vector<uint16_t>& lut)
{
    const int rows = lab_u8.rows, cols = lab_u8.cols;
    for (int y=0; y<rows; ++y) {
        cv::Vec3b* row = lab_u8.ptr<cv::Vec3b>(y);
        for (int x=0; x<cols; ++x) {
            const uint8_t a = row[x][1];
            const uint8_t b = row[x][2];
            const uint16_t v = lut[(a<<8) | b];
            row[x][1] = (uint8_t)(v & 255);
            row[x][2] = (uint8_t)(v >> 8);
        }
    }
}

// -------------------- Dialog: gain only --------------------
class CameraParamDialog : public QDialog
{
public:
    explicit CameraParamDialog(const QString& sn, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr("相机参数配置 - %1").arg(sn));
        setStyleSheet(
            "QLabel { color: #ffffff; }"
            "QDoubleSpinBox { color: #000000; background: #ffffff; }"
            );

        gainSpinDb_ = new QDoubleSpinBox(this);
        gainSpinDb_->setRange(0.0, 47.0);
        gainSpinDb_->setDecimals(1);
        gainSpinDb_->setSingleStep(0.5);
        gainSpinDb_->setValue(s_lastGainDb_);

        auto *form = new QFormLayout;
        form->addRow(tr("增益："), gainSpinDb_);

        auto *mainLayout = new QVBoxLayout;
        mainLayout->addLayout(form);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        mainLayout->addWidget(buttons);

        setLayout(mainLayout);
    }

    double gainDb() const {
        double g = gainSpinDb_->value();
        if (g < 0.0) g = 0.0;
        if (g > 47.0) g = 47.0;
        return g;
    }

protected:
    void accept() override
    {
        double g = gainSpinDb_->value();
        if (g < 0.0) g = 0.0;
        if (g > 47.0) g = 47.0;
        gainSpinDb_->setValue(g);
        s_lastGainDb_ = g;
        QDialog::accept();
    }

private:
    QDoubleSpinBox* gainSpinDb_ = nullptr;
    static double s_lastGainDb_;
};

double CameraParamDialog::s_lastGainDb_ = 5.0;

// -------------------- MediaMTX helpers (Windows) --------------------
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
    if (outExitCode) *outExitCode = p.exitCode();
    return true;
}

static bool isMediaMtxRunningOnce()
{
#ifdef Q_OS_WIN
    QString out;
    int exitCode = 0;
    if (!runAndCapture("tasklist", {"/FI", "IMAGENAME eq mediamtx.exe"}, 3000, &out, &exitCode))
        return false;
    if (exitCode != 0) return false;
    return out.contains("mediamtx.exe", Qt::CaseInsensitive);
#else
    return false;
#endif
}

static bool killMediaMtxBlocking(int timeoutMs = 5000)
{
#ifdef Q_OS_WIN
    QElapsedTimer timer;
    timer.start();
    while (isMediaMtxRunningOnce()) {
        QProcess::execute("taskkill", {"/F", "/IM", "mediamtx.exe"});
        if (!isMediaMtxRunningOnce()) return true;
        if (timer.elapsed() > timeoutMs) return false;
        QThread::msleep(200);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
#else
    return true;
#endif
}

// ===================================================================
//                          MainWindow
// ===================================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);

    ui->setupUi(this);
    // ====== ColorTune worker thread ======
    qRegisterMetaType<QImage>("QImage"); // 保守起见（跨线程传递 QImage）

    colorWorker_ = new ColorTuneWorker();
    colorThread_ = new QThread(this);
    colorWorker_->moveToThread(colorThread_);
    colorThread_->start();

    // 参数下发（和你原来 tuneParams_ 默认值保持一致）
    colorWorker_->setEnabled(enableColorTune_);
    colorWorker_->setParams(tuneParams_);
    colorWorker_->setMeanStride(meanStride_);
    colorWorker_->setCorrRebuildThr(corrRebuildThr_);

    // 输入帧 -> worker
    connect(this, &MainWindow::sendFrameToColorTune,
            colorWorker_, &ColorTuneWorker::onFrameIn,
            Qt::QueuedConnection);

    // worker 输出 -> UI
    connect(colorWorker_, &ColorTuneWorker::frameOut,
            this, &MainWindow::onColorTunedFrame,
            Qt::QueuedConnection);

    // 清理
    connect(colorThread_, &QThread::finished, colorWorker_, &QObject::deleteLater);

    titleForm();

    // label 自适应
    if (ui->label) {
        ui->label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        ui->label->setMinimumSize(0, 0);
    }

    // 录制指示灯
    recIndicator_ = new QLabel(ui->label);
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

    connect(myVideoRecorder, &VideoRecorder::sendMSG2ui,
            this, &MainWindow::getMSG);

    // splitter
    if (ui->deviceSplitter) {
        ui->deviceSplitter->setHandleWidth(3);
        ui->deviceSplitter->setStretchFactor(0, 3);
        ui->deviceSplitter->setStretchFactor(1, 2);
    }

    // device list
    if (ui->deviceList) {
        ui->deviceList->setColumnCount(4);
        ui->deviceList->setHorizontalHeaderLabels({ "设备名称", "设备状态", "修改IP", "配置" });

        auto* header = ui->deviceList->horizontalHeader();
        header->setSectionResizeMode(0, QHeaderView::Stretch);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);

        ui->deviceList->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->deviceList->setEditTriggers(QAbstractItemView::NoEditTriggers);

        connect(ui->deviceList, &QTableWidget::itemSelectionChanged,
                this, &MainWindow::onTableSelectionChanged);
    }

    // status icons
    auto makeDotIcon = [](const QColor& fill, const QColor& border) -> QIcon {
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
    };
    iconOnline_  = makeDotIcon(QColor(0, 200, 0), QColor(0, 120, 0));
    iconOffline_ = makeDotIcon(QColor(180, 180, 180), QColor(120, 120, 120));

    // UdpDeviceManager
    mgr_ = new UdpDeviceManager(this);
    mgr_->setDefaultCmdPort(10000);
    if (!mgr_->start(7777, 8888)) {
        qWarning() << "UdpDeviceManager start failed";
        return;
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
    devAliveTimer_->setInterval(2000);
    connect(devAliveTimer_, &QTimer::timeout,
            this, &MainWindow::onCheckDeviceAlive);
    devAliveTimer_->start();

    ipChangeTimer_ = new QTimer(this);
    ipChangeTimer_->setSingleShot(true);
    connect(ipChangeTimer_, &QTimer::timeout,
            this, &MainWindow::onIpChangeTimeout);

    // color tune defaults
    enableColorTune_ = true;
    tuneParams_.ga = 1.00f;
    tuneParams_.gb = 1.00f;
    tuneParams_.da = +2.0f;
    tuneParams_.db = +4.0f;
    tuneParams_.chromaGain  = 1.45f;
    tuneParams_.chromaGamma = 0.80f;
    tuneParams_.chromaMax   = 145.0f;
    tuneParams_.abShiftClamp = 55.0f;
    tuneParams_.keepL = true;

    meanStride_ = 4;
    corrRebuildThr_ = 0.5f;
    lutValid_ = false;
    lastCorrA_ = 1e9f;
    lastCorrB_ = 1e9f;
    abLut_.clear();

    // system ip + mediamtx
    updateSystemIP();
    startMediaMTX();

    // init state
    curSelectedSn_.clear();
    previewActive_ = false;
    clearDeviceInfoPanel();
    updateCameraButtons();
}

MainWindow::~MainWindow()
{
    if (viewer_) {
        viewer_->stop();
        viewer_->wait(1000);
        viewer_ = nullptr;
    }
    stopMediaMTX();
    if (colorThread_) {
        colorThread_->quit();
        colorThread_->wait(1000);
        colorThread_ = nullptr;
        colorWorker_ = nullptr; // worker 已通过 finished->deleteLater 清理
    }

    delete ui;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (viewer_) {
        viewer_->stop();
        viewer_->wait(1500);
        viewer_ = nullptr;
    }
    stopMediaMTXBlocking();
    event->accept();
}

// -------------------- title bar --------------------
void MainWindow::titleForm()
{
    TitleBar *title = new TitleBar(this);
    setMenuWidget(title);

    connect(title, &TitleBar::minimizeRequested, this, &MainWindow::showMinimized);
    connect(title, &TitleBar::maximizeRequested, [this](){
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(title, &TitleBar::closeRequested, this, &MainWindow::close);
}

// -------------------- RTSP frame --------------------
void MainWindow::onFrame(const QImage& img)
{
    if (!ui || !ui->label) return;

    // UI线程只负责把帧送到 worker（极轻）
    emit sendFrameToColorTune(img);
}

// -------------------- messages --------------------
void MainWindow::getMSG(const QString& sn)
{
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    if (ui->messageBox) ui->messageBox->append(QString("[%1] %2").arg(timeStr, sn));
}

// -------------------- MediaMTX --------------------
void MainWindow::startMediaMTX()
{
    if (mtxProc_) return;

    if (!killMediaMtxBlocking(5000)) {
        qWarning().noquote() << "[MediaMTX] abort start: existing mediamtx.exe cannot be terminated.";
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
    if (QFileInfo::exists(mtxCfg)) args << mtxCfg;
    mtxProc_->setArguments(args);

    mtxProc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(mtxProc_, &QProcess::readyReadStandardOutput, this, [this]{
        const QByteArray all = mtxProc_->readAllStandardOutput();
        for (const QByteArray& line : all.split('\n')) {
            const auto s = QString::fromLocal8Bit(line).trimmed();
            if (!s.isEmpty()) {
                qInfo().noquote() << "[MediaMTX]" << s;
                onMediaMtxLogLine(s);
            }
        }
    });

    connect(mtxProc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e){
        qWarning() << "[MediaMTX] QProcess error:" << e << mtxProc_->errorString();
    });

    connect(mtxProc_, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus st){
                qWarning() << "[MediaMTX] exited, code=" << code << "status=" << st;
                mtxProc_->deleteLater();
                mtxProc_ = nullptr;
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
}

void MainWindow::stopMediaMTX()
{
    if (!mtxProc_) return;
    mtxProc_->terminate();
    if (!mtxProc_->waitForFinished(2000)) {
#ifdef Q_OS_WIN
        const qint64 pid = mtxProc_->processId();
        if (pid > 0) killProcessTreeWindows(pid);
        mtxProc_->waitForFinished(2000);
#else
        mtxProc_->kill();
        mtxProc_->waitForFinished(1000);
#endif
    }
    mtxProc_->deleteLater();
    mtxProc_ = nullptr;
}

bool MainWindow::stopMediaMTXBlocking(int gracefulMs, int killMs)
{
    if (!mtxProc_) return true;

    QMessageBox tip(this);
    tip.setIcon(QMessageBox::Information);
    tip.setWindowTitle(QString::fromUtf8(u8"正在退出"));
    tip.setText(QString::fromUtf8(u8"请等待系统断开…"));
    tip.show();
    QCoreApplication::processEvents();

    mtxProc_->terminate();
    if (!mtxProc_->waitForFinished(gracefulMs)) {
#ifdef Q_OS_WIN
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
// -------------------- network ip --------------------
QStringList MainWindow::probeWiredIPv4s()
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

void MainWindow::updateSystemIP()
{
    const QStringList ips = probeWiredIPv4s();
    if (ips.isEmpty()) {
        qWarning() << "[IP] no usable wired IPv4 found, keep curBindIp_ =" << curBindIp_;
        if (ui->lblHostIp) ui->lblHostIp->setText(tr("无可用 IP"));
        return;
    }
    curBindIp_ = ips.first();
    if (ui->lblHostIp) ui->lblHostIp->setText(curBindIp_);
}

// -------------------- device discovery upsert --------------------
void MainWindow::upsertCameraSN(const QString& sn)
{
    if (sn.isEmpty() || !mgr_) return;
    updateTableDevice(sn);
}

// -------------------- updateTableDevice (IMPLEMENTED) --------------------
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

    // col2: ip button
    if (!ui->deviceList->cellWidget(row, 2)) {
        auto* btnIp = new QPushButton(QStringLiteral("修改IP"), ui->deviceList);
        connect(btnIp, &QPushButton::clicked, this, [this, sn] { changeCameraIpForSn(sn); });
        ui->deviceList->setCellWidget(row, 2, btnIp);
    }

    // col3: cfg button
    if (!ui->deviceList->cellWidget(row, 3)) {
        auto* btnCfg = new QPushButton(QStringLiteral("配置"), ui->deviceList);
        connect(btnCfg, &QPushButton::clicked, this, [this, sn] { configCameraForSn(sn); });
        ui->deviceList->setCellWidget(row, 3, btnCfg);
    }

    updateCameraButtons();
}

// -------------------- alive check (IMPLEMENTED) --------------------
void MainWindow::onCheckDeviceAlive()
{
    if (!ui || !ui->deviceList || !mgr_) return;

    const qint64 now       = QDateTime::currentMSecsSinceEpoch();
    const qint64 offlineMs = 10000;

    const int rows = ui->deviceList->rowCount();
    for (int r = 0; r < rows; ++r) {
        QTableWidgetItem* nameItem = ui->deviceList->item(r, 0);
        if (!nameItem) continue;

        const QString sn = nameItem->data(Qt::UserRole).toString();
        if (sn.isEmpty()) continue;

        DeviceInfo dev;
        const bool exists = mgr_->getDevice(sn, dev);
        const bool online = exists && (now - dev.lastSeenMs <= offlineMs);

        QTableWidgetItem* stItem = ui->deviceList->item(r, 1);
        if (!stItem) {
            stItem = new QTableWidgetItem;
            ui->deviceList->setItem(r, 1, stItem);
        }

        const QString oldStatus = stItem->text();
        const QString newStatus = online ? QStringLiteral("在线") : QStringLiteral("离线");

        stItem->setText(newStatus);
        stItem->setTextAlignment(Qt::AlignCenter);

        if (online) {
            stItem->setIcon(iconOnline_);
            stItem->setForeground(Qt::black);
        } else {
            stItem->setIcon(iconOffline_);
            stItem->setForeground(Qt::gray);
        }

        if (online && oldStatus == QStringLiteral("离线")) camOnlineSinceMs_[sn] = now;

        if (!curSelectedSn_.isEmpty() && sn == curSelectedSn_) {
            if (exists) updateDeviceInfoPanel(&dev, online);
            else clearDeviceInfoPanel();

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

// -------------------- stop viewer --------------------
void MainWindow::doStopViewer()
{
    if (!viewer_) return;
    viewer_->stop();
    viewer_->wait(1500);
    viewer_->deleteLater();
    viewer_ = nullptr;
    previewActive_ = false;
    updateCameraButtons();
}

// -------------------- update buttons (IMPLEMENTED) --------------------
void MainWindow::updateCameraButtons()
{
    if (!ui) return;

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
    };

    setAllEnabled(false);

    if (curSelectedSn_.isEmpty()) return;
    if (ipChangeWaiting_) return;

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

    // not previewing: must be online + pushing
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    DeviceInfo dev;
    bool online = false;
    if (mgr_ && mgr_->getDevice(curSelectedSn_, dev)) {
        online = (now - dev.lastSeenMs <= 10000);
    }
    if (!online) return;

    bool pushing = false;
    auto it = pathStates_.find(curSelectedSn_);
    if (it != pathStates_.end()) pushing = it.value().hasPublisher;
    if (!pushing) return;

    if (actOpen)     actOpen->setEnabled(true);
    if (actClose)    actClose->setEnabled(false);
    if (actGrab)     actGrab->setEnabled(false);
    if (actStartRec) actStartRec->setEnabled(false);
    if (actStopRec)  actStopRec->setEnabled(false);
}

// -------------------- open/close/grab/record --------------------
void MainWindow::on_action_openCamera_triggered()
{
    if (viewer_) return;
    if (curSelectedSn_.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先在列表中选择一台相机。"));
        return;
    }

    curPath_ = curSelectedSn_;
    const QString url = QString("rtsp://%1:%2/%3").arg(curBindIp_).arg(curRtspPort_).arg(curPath_);

    viewer_ = new RtspViewerQt(this);
    previewActive_ = false;

    connect(viewer_, &RtspViewerQt::frameReady, this, &MainWindow::onFrame);

    viewer_->setUrl(url);
    viewer_->start();

    updateCameraButtons();
}

void MainWindow::on_action_closeCamera_triggered()
{
    doStopViewer();
}

void MainWindow::on_action_grap_triggered()
{
    iscapturing_ = true;
}

void MainWindow::on_action_startRecord_triggered()
{
    if (isRecording_) return;
    isRecording_ = true;

    if (ui->action_startRecord) ui->action_startRecord->setEnabled(false);
    if (ui->action_stopRecord)  ui->action_stopRecord->setEnabled(true);

    if (recIndicator_) recIndicator_->show();
    if (recBlinkTimer_) recBlinkTimer_->start();

    emit startRecord();
}void MainWindow::on_action_stopRecord_triggered()
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

// -------------------- IP change completion check (IMPLEMENTED) --------------------
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

// -------------------- config camera --------------------
void MainWindow::configCameraForSn(const QString& sn)
{
    const QString trimmedSn = sn.trimmed();
    if (trimmedSn.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择一个设备 ID (SN)。"));
        return;
    }
    if (!mgr_) {
        QMessageBox::warning(this, tr("提示"), tr("设备管理器未启动。"));
        return;
    }

    DeviceInfo dev;
    if (!mgr_->getDevice(trimmedSn, dev)) {
        QMessageBox::warning(this, tr("提示"),
                             tr("未找到设备 [%1]，请确认设备在线。").arg(trimmedSn));
        return;
    }

    CameraParamDialog dlg(trimmedSn, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const double gainDb = dlg.gainDb();
    const qint64 n = mgr_->sendSetCameraParams(trimmedSn, 0, gainDb);
    if (n <= 0) {
        QMessageBox::warning(this, tr("提示"),
                             tr("发送曝光/增益设置命令失败（ret=%1）。").arg(n));
        return;
    }

    getMSG(tr("已发送配置命令：SN=%1 增益=%2 dB").arg(trimmedSn).arg(gainDb, 0, 'f', 1));
}

// -------------------- parse mediamtx logs --------------------
void MainWindow::onMediaMtxLogLine(const QString& s)
{
    static QRegularExpression rePub(R"(is publishing to path '([^']+)')",
                                    QRegularExpression::CaseInsensitiveOption);
    static QRegularExpression reClose(R"(\[path ([^]]+)\] closing existing publisher)",
                                      QRegularExpression::CaseInsensitiveOption);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    auto m1 = rePub.match(s);
    if (m1.hasMatch()) {
        const QString path = m1.captured(1).trimmed();
        PathState &ps = pathStates_[path];
        ps.hasPublisher = true;
        ps.lastPubMs = now;
        if (!curSelectedSn_.isEmpty() && curSelectedSn_ == path) updateCameraButtons();
        return;
    }

    auto m2 = reClose.match(s);
    if (m2.hasMatch()) {
        const QString path = m2.captured(1).trimmed();
        auto it = pathStates_.find(path);
        if (it != pathStates_.end()) it->hasPublisher = false;
        if (!curSelectedSn_.isEmpty() && curSelectedSn_ == path) updateCameraButtons();
        return;
    }
}

// -------------------- info panel --------------------
void MainWindow::updateDeviceInfoPanel(const DeviceInfo* dev, bool /*online*/)
{
    if (ui->lblHostIp) {
        ui->lblHostIp->setText("当前主机IP：" + (curBindIp_.isEmpty() ? tr("--") : curBindIp_));
    }

    if (!dev) {
        if (ui->lblCamIp) ui->lblCamIp->setText("当前相机IP：--");
        if (ui->lblCamLastSeen) ui->lblCamLastSeen->setText("相机上线时间：--");
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

// -------------------- Color tune --------------------
QImage MainWindow::applyColorTuneFast(const QImage& in)
{
    if (!enableColorTune_) return in;
    if (in.isNull()) return in;

    cv::Mat bgr = QImageToBgr8(in);

    cv::Mat lab_u8;
    cv::cvtColor(bgr, lab_u8, cv::COLOR_BGR2Lab);

    float meanA=128.f, meanB=128.f;
    meanAB_stride(lab_u8, meanStride_, meanA, meanB);

    const float kNeutral  = 0.45f;
    const float corrClamp = 10.0f;
    float corrA = (128.0f - meanA) * kNeutral;
    float corrB = (128.0f - meanB) * kNeutral;
    corrA = std::clamp(corrA, -corrClamp, corrClamp);
    corrB = std::clamp(corrB, -corrClamp, corrClamp);

    if (!lutValid_ ||
        std::fabs(corrA - lastCorrA_) > corrRebuildThr_ ||
        std::fabs(corrB - lastCorrB_) > corrRebuildThr_)
    {
        buildAB_LUT(abLut_, tuneParams_, corrA, corrB);
        lastCorrA_ = corrA;
        lastCorrB_ = corrB;
        lutValid_  = true;
    }

    applyAB_LUT_inplace(lab_u8, abLut_);

    cv::Mat out_bgr;
    cv::cvtColor(lab_u8, out_bgr, cv::COLOR_Lab2BGR);

    return Bgr8ToQImage(out_bgr);
}
void MainWindow::onColorTunedFrame(const QImage& showImg)
{
    if (!ui || !ui->label) return;
    if (showImg.isNull()) return;

    QPixmap pm = QPixmap::fromImage(showImg).scaled(
        ui->label->size(),
        Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation);

    ui->label->setPixmap(pm);

    if (!previewActive_) {
        previewActive_ = true;
        updateCameraButtons();
    }

    if (isRecording_) emit sendFrame2Record(showImg);

    if (iscapturing_) {
        emit sendFrame2Capture(showImg);
        iscapturing_ = false;
    }
}
