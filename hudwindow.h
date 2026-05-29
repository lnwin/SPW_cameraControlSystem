#pragma once

#include <QQuickWidget>
#include <QWidget>
#include "uicontroller.h"

class ZoomPanImageView;

class HudWindow : public QQuickWidget
{
    Q_OBJECT
public:
    explicit HudWindow(UiController* ctrl, const QString& greenLogoPath = {}, QWidget* parent = nullptr);
    void embedVideoWidget(QWidget* videoWidget);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void syncVideoGeometry();
    QWidget* videoContainer_ = nullptr;
};
