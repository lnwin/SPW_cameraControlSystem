#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "gstrtsprecordserverqt.h"
#include <QDebug>
#include <QSizePolicy>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ensurePreviewLabel();

    // —— 启动内嵌 RTSP-RECORD 服务器（你已跑通 mystream）——
    srv_ = new GstRtspRecordServerQt(this);
    srv_->configure("192.168.194.77", "10000", "/mystream");

    // —— 连接帧信号：Queued，避免跨线程 UI 操作问题 ——
    connect(srv_, &GstRtspRecordServerQt::frameReady,
        this, [this](const QImage& img){
            // UI 节流：~30 FPS（或根据 uiMinIntervalMs_）
            if (!uiFpsTimer_.isValid() || uiFpsTimer_.elapsed() >= uiMinIntervalMs_) {
                showFrameOnLabel(img);
                uiFpsTimer_.restart();
            }
        },
        Qt::QueuedConnection);

    // 可选：连上/断开事件做日志
    connect(srv_, &GstRtspRecordServerQt::publisherConnected, this, []{
            qInfo() << "[RTSP] publisher connected (RECORD)";
        }, Qt::QueuedConnection);
    connect(srv_, &GstRtspRecordServerQt::publisherDisconnected, this, []{
            qInfo() << "[RTSP] publisher disconnected";
        }, Qt::QueuedConnection);

    srv_->start();               // 开始监听
    uiFpsTimer_.start();         // 启动 UI 节流计时器
}

MainWindow::~MainWindow()
{
    if (srv_) { srv_->stopAsync(); srv_->wait(); srv_ = nullptr; }
    delete ui;
}

void MainWindow::ensurePreviewLabel()
{
    // 如果 .ui 里没有放 QLabel，就动态创建一个占满窗口的
    if (!ui->label) {
        previewLabel_ = new QLabel(this);
        previewLabel_->setObjectName(QStringLiteral("label"));
        setCentralWidget(previewLabel_);
    } else {
        previewLabel_ = ui->label;
    }

    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumSize(320, 240);
    previewLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewLabel_->setStyleSheet("background:#111; color:#ccc;"); // 暗底更友好
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);
    // 尺寸变化时，若有缓存帧，重新按新尺寸渲染一次
    if (!lastPixmap_.isNull() && previewLabel_) {
        const auto scaled = lastPixmap_.scaled(previewLabel_->size(),
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
        previewLabel_->setPixmap(scaled);
    }
}

void MainWindow::showFrameOnLabel(const QImage& img)
{
    if (!previewLabel_) return;
    // 确保格式适配：我们的 appsink 输出 BGR888；若不同也兼容
    QImage toShow = img;
    if (toShow.format() != QImage::Format_BGR888 &&
        toShow.format() != QImage::Format_RGB888 &&
        toShow.format() != QImage::Format_ARGB32 &&
        toShow.format() != QImage::Format_RGB32) {
        toShow = toShow.convertToFormat(QImage::Format_RGB888);
    }

    // 转 QPixmap 并缓存（避免频繁深拷贝：Qt 会共享数据）
    lastPixmap_ = QPixmap::fromImage(toShow);
    const auto scaled = lastPixmap_.scaled(previewLabel_->size(),
                                           Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
    previewLabel_->setPixmap(scaled);
}
