#include "hudwindow.h"
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QDebug>
#include <QResizeEvent>

HudWindow::HudWindow(UiController* ctrl, const QString& greenLogoPath, QWidget* parent)
    : QQuickWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    rootContext()->setContextProperty("uiCtrl", ctrl);
    rootContext()->setContextProperty("greenLogoUrl",
        greenLogoPath.isEmpty() ? QString() : QUrl::fromLocalFile(greenLogoPath).toString());
    setSource(QUrl("qrc:/qml/Main.qml"));
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setWindowTitle("SPW 工业相机控制系统 - HUD");
    resize(1280, 800);

    for (const QQmlError& e : errors())
        qCritical() << "[QML ERROR]" << e.toString();
}

void HudWindow::embedVideoWidget(QWidget* videoWidget)
{
    if (!videoWidget) return;
    videoContainer_ = videoWidget;
    videoContainer_->setParent(this);
    videoContainer_->show();
    syncVideoGeometry();
}

void HudWindow::resizeEvent(QResizeEvent* e)
{
    QQuickWidget::resizeEvent(e);
    syncVideoGeometry();
}

void HudWindow::syncVideoGeometry()
{
    if (!videoContainer_ || !rootObject()) return;

    QQuickItem* area = rootObject()->findChild<QQuickItem*>("videoArea");
    if (!area) return;

    QPointF pos = area->mapToScene(QPointF(0, 0));
    videoContainer_->setGeometry(
        qRound(pos.x()), qRound(pos.y()),
        qRound(area->width()), qRound(area->height())
    );
    videoContainer_->raise();
}
