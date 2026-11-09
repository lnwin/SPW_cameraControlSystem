#pragma once

#include <QWidget>
#include <QMap>
#include <QString>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QDebug>
#include "cameraview.h"
#include "multicameralayoutmanager.h"

// 可双击编辑的标签
class ClickableLabel : public QLabel
{
    Q_OBJECT
public:
    explicit ClickableLabel(QWidget* parent = nullptr) : QLabel(parent) {}

signals:
    void doubleClicked();

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
};

// 单个视频窗口的包装器（包含视频视图 + 信息叠加层）
class VideoWindowWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoWindowWidget(CameraView* cameraView, QWidget* parent = nullptr);

    QString sn() const { return cameraView_->sn(); }
    CameraView* cameraView() const { return cameraView_; }
    ZoomPanImageView* videoView() const { return cameraView_->view(); }

    void setSelected(bool selected);
    bool isSelected() const { return selected_; }

    void updateInfo();

    // 设置/获取叠加文字
    void setOverlayText(const QString& text);
    QString overlayText() const { return overlayText_; }

    // 更新按钮层级（在视频view添加后调用）
    void updateButtonLayers();

signals:
    void clicked(const QString& sn);
    void requestFullscreen(const QString& sn);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void createInfoOverlay();
    void updateInfoText();

    CameraView* cameraView_;
    bool selected_ = false;
    QString overlayText_;  // 叠加文字
    bool isDefaultOverlayText_ = true;  // 标记是否为默认文字（未被用户修改）

    // 信息叠加层
    QLabel* infoLabel_ = nullptr;
    QPushButton* fullscreenBtn_ = nullptr;
};

// 多相机视频网格容器
class MultiCameraVideoGrid : public QWidget
{
    Q_OBJECT
public:
    explicit MultiCameraVideoGrid(QWidget* parent = nullptr);

    // 设置布局管理器
    void setLayoutManager(MultiCameraLayoutManager* layoutMgr);

    // 添加/移除相机窗口
    void addCameraWindow(CameraView* cameraView);
    void removeCameraWindow(const QString& sn);

    // 选中相机
    void setSelectedCamera(const QString& sn);
    QString selectedCamera() const { return selectedSn_; }

    // 更新布局（根据活跃相机列表）
    void updateLayout(const QList<QString>& activeStreamingSns);

    // 获取窗口
    VideoWindowWidget* getWindow(const QString& sn) const;

signals:
    void cameraSelected(const QString& sn);
    void cameraFullscreenRequested(const QString& sn);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onWindowClicked(const QString& sn);
    void onFullscreenRequested(const QString& sn);

private:
    void applyCurrentLayout();

    MultiCameraLayoutManager* layoutMgr_ = nullptr;

    // SN -> VideoWindowWidget
    QMap<QString, VideoWindowWidget*> windows_;

    QString selectedSn_;

    // 当前活跃相机列表（用于布局计算）
    QList<QString> currentActiveSns_;
};
