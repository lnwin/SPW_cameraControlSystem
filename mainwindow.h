#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QElapsedTimer>
#include <QPixmap>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class GstRtspRecordServerQt; // 前向声明

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    Ui::MainWindow* ui = nullptr;

    // 预览显示
    QLabel* previewLabel_ = nullptr;
    QPixmap lastPixmap_;                 // 缓存最后一帧，窗口尺寸变化时复用
    QElapsedTimer uiFpsTimer_;           // UI 刷新节流
    int uiMinIntervalMs_ = 30;           // 约 33ms ≈ 30FPS；你的源是 21FPS，可以设 40~45

    // RTSP RECORD 服务器
    GstRtspRecordServerQt* srv_ = nullptr;

    void ensurePreviewLabel();
    void showFrameOnLabel(const QImage& img);
};
