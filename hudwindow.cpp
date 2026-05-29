#include "hudwindow.h"
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QDebug>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QCursor>

HudWindow::HudWindow(UiController* ctrl, const QString& greenLogoPath, QWidget* parent)
    : QQuickWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setMouseTracking(true);
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

bool HudWindow::inResizeZone(const QPoint& pos) const
{
    return pos.x() >= width() - kEdge && pos.y() >= height() - kEdge;
}

void HudWindow::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && inResizeZone(e->pos())) {
        resizing_    = true;
        resizeStart_ = e->globalPos();
        sizeAtStart_ = size();
        e->accept();
        return;
    }
    QQuickWidget::mousePressEvent(e);
}

void HudWindow::mouseMoveEvent(QMouseEvent* e)
{
    if (resizing_) {
        QPoint delta = e->globalPos() - resizeStart_;
        int w = qMax(640, sizeAtStart_.width()  + delta.x());
        int h = qMax(400, sizeAtStart_.height() + delta.y());
        resize(w, h);
        e->accept();
        return;
    }
    // 更新鼠标指针
    setCursor(inResizeZone(e->pos()) ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
    QQuickWidget::mouseMoveEvent(e);
}

void HudWindow::mouseReleaseEvent(QMouseEvent* e)
{
    if (resizing_) {
        resizing_ = false;
        e->accept();
        return;
    }
    QQuickWidget::mouseReleaseEvent(e);
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
