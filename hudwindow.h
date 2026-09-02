#pragma once

#include <QQuickWidget>
#include <QWidget>
#include <QPoint>
#include "uicontroller.h"

class QToolButton;

class HudWindow : public QQuickWidget
{
    Q_OBJECT
public:
    explicit HudWindow(UiController* ctrl, const QString& greenLogoPath = {}, const QString& appIconDir = {}, QWidget* parent = nullptr);
    void embedVideoWidget(QWidget* videoWidget);

    // 视频区全屏（铺满整个主界面，保持图像比例）；再次调用恢复
    bool isVideoFullscreen() const { return videoFullscreen_; }
    void setVideoFullscreen(bool on);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent_cursor(QMouseEvent* e);
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void syncVideoGeometry();
    void layoutFullscreenButton();
    void updateFullscreenButtonUi();
    bool inResizeZone(const QPoint& pos) const;

    QWidget*     videoContainer_  = nullptr;
    QToolButton* fsBtn_           = nullptr;   // 视频区右上角全屏切换按钮
    bool         videoFullscreen_ = false;
    // 全屏态在视频上左键拖动移动窗口（仅图像未放大时，避免与平移冲突）
    bool         winDragging_     = false;
    QPoint       winDragStart_;
    QPoint       winPosAtStart_;
    // 全屏态拖拽视频四角缩放窗口
    enum Corner { NoCorner = 0, TopLeft, TopRight, BottomLeft, BottomRight };
    Corner       cornerAt(const QPoint& localPos) const;
    Corner       fsResizeCorner_  = NoCorner;
    QPoint       fsResizeStart_;
    QRect        fsGeomAtStart_;
    bool     resizing_       = false;
    QPoint   resizeStart_;
    QSize    sizeAtStart_;
    static constexpr int kEdge = 12; // 右下角热区大小
    static constexpr int kCornerZone = 16;   // 全屏态四角缩放热区
    static constexpr int kMinW = 640, kMinH = 400;
};
