#pragma once

#include <QWidget>
#include <QImage>
#include <QPointF>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QtMath>

class ZoomPanImageView : public QWidget
{
    Q_OBJECT
public:
    explicit ZoomPanImageView(QWidget* parent=nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    void setZoomRange(double minZ, double maxZ) { minZoom_ = minZ; maxZoom_ = maxZ; clampZoom(); }
    double zoom() const { return zoom_; }

    void setImage(const QImage& img)
    {
        img_ = img;
        if (img_.isNull()) return;

        if (img_.size() != lastImgSize_) {
            lastImgSize_ = img_.size();
            resetView();
        }
        update();
    }

    void resetView()
    {
        zoom_ = 1.0;          // 1.0 = fit-to-widget
        pan_  = QPointF(0,0); // 在 fit 后坐标系里平移
        clampPan();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.fillRect(rect(), Qt::black);

        if (img_.isNull()) return;

        const QSizeF vw = size();
        const QSizeF iw = img_.size();

        const double sFit = qMin(vw.width()/iw.width(), vw.height()/iw.height());
        const double s    = sFit * zoom_;
        const QSizeF drawSize(iw.width()*s, iw.height()*s);

        const QPointF baseTopLeft((vw.width()-drawSize.width())*0.5,
                                  (vw.height()-drawSize.height())*0.5);

        const QPointF topLeft = baseTopLeft + pan_;
        p.drawImage(QRectF(topLeft, drawSize), img_);
    }

    void wheelEvent(QWheelEvent* e) override
    {
        if (img_.isNull()) return;

#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        const QPointF pos = e->position();
#else
        const QPointF pos = e->posF();
#endif
        const int delta = e->angleDelta().y();
        if (delta == 0) return;

        const double factor = (delta > 0) ? 1.15 : (1.0/1.15);
        zoomAt(pos, factor);

        e->accept();
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton) {
            if (zoom_ > 1.0 + 1e-6) {
                dragging_ = true;
                lastMousePos_ = e->pos();
                setCursor(Qt::ClosedHandCursor);
            }
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (dragging_) {
            const QPointF cur = e->pos();
            const QPointF d = cur - lastMousePos_;
            lastMousePos_ = cur;

            pan_ += d;
            clampPan();
            update();

            e->accept();
            return;
        }
        QWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            unsetCursor();
            e->accept();
            return;
        }
        QWidget::mouseReleaseEvent(e);
    }

    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        clampPan();
    }

private:
    void clampZoom()
    {
        if (zoom_ < minZoom_) zoom_ = minZoom_;
        if (zoom_ > maxZoom_) zoom_ = maxZoom_;
    }

    void clampPan()
    {
        if (img_.isNull()) return;

        const QSizeF vw = size();
        const QSizeF iw = img_.size();

        const double sFit = qMin(vw.width()/iw.width(), vw.height()/iw.height());
        const double s    = sFit * zoom_;
        const QSizeF drawSize(iw.width()*s, iw.height()*s);

        auto clampAxis = [](double viewLen, double imgLen, double panVal)->double{
            if (imgLen <= viewLen) return 0.0;           // 图像比窗口小：强制居中
            const double over = imgLen - viewLen;
            const double lo = -over*0.5;
            const double hi =  over*0.5;
            if (panVal < lo) panVal = lo;
            if (panVal > hi) panVal = hi;
            return panVal;
        };

        pan_.setX(clampAxis(vw.width(),  drawSize.width(),  pan_.x()));
        pan_.setY(clampAxis(vw.height(), drawSize.height(), pan_.y()));
    }

    void zoomAt(const QPointF& pos, double factor)
    {
        const double oldZoom = zoom_;
        double newZoom = zoom_ * factor;
        if (newZoom < minZoom_) newZoom = minZoom_;
        if (newZoom > maxZoom_) newZoom = maxZoom_;
        if (qFuzzyCompare(newZoom, oldZoom)) return;

        const QSizeF vw = size();
        const QSizeF iw = img_.size();

        const double sFit = qMin(vw.width()/iw.width(), vw.height()/iw.height());

        const double sOld = sFit * oldZoom;
        const double sNew = sFit * newZoom;

        const QSizeF drawOld(iw.width()*sOld, iw.height()*sOld);
        const QSizeF drawNew(iw.width()*sNew, iw.height()*sNew);

        const QPointF baseOld((vw.width()-drawOld.width())*0.5,
                              (vw.height()-drawOld.height())*0.5);

        // 鼠标在 old 绘制图像上的归一化位置 (0~1)，注意：不能用 QPointF/ QPointF
        const QPointF localOld = pos - (baseOld + pan_);
        const double u = (drawOld.width()  > 1e-9) ? (localOld.x() / drawOld.width())  : 0.5;
        const double v = (drawOld.height() > 1e-9) ? (localOld.y() / drawOld.height()) : 0.5;

        zoom_ = newZoom;

        const QPointF baseNew((vw.width()-drawNew.width())*0.5,
                              (vw.height()-drawNew.height())*0.5);

        // 反推 pan_，保证 pos 指向的图像点不变
        pan_ = pos - baseNew - QPointF(u*drawNew.width(), v*drawNew.height());

        clampPan();
        update();
    }

private:
    QImage  img_;
    QSize   lastImgSize_;

    double  minZoom_ = 1.0;
    double  maxZoom_ = 3.0;
    double  zoom_    = 1.0;

    QPointF pan_ = QPointF(0,0);

    bool    dragging_ = false;
    QPointF lastMousePos_;
};
