#pragma once

#include <QQuickWidget>
#include <QWidget>
#include <QPoint>
#include "uicontroller.h"

class HudWindow : public QQuickWidget
{
    Q_OBJECT
public:
    explicit HudWindow(UiController* ctrl, const QString& greenLogoPath = {}, QWidget* parent = nullptr);
    void embedVideoWidget(QWidget* videoWidget);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent_cursor(QMouseEvent* e);

private:
    void syncVideoGeometry();
    bool inResizeZone(const QPoint& pos) const;

    QWidget* videoContainer_ = nullptr;
    bool     resizing_       = false;
    QPoint   resizeStart_;
    QSize    sizeAtStart_;
    static constexpr int kEdge = 12; // 右下角热区大小
};
