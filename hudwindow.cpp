#include "hudwindow.h"
#include "ZoomPanImageView.h"
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QDebug>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QToolButton>
#include <QPainter>
#include <QPixmap>
#include <QIcon>

namespace {
constexpr int kFsBtnSize   = 28;   // 全屏按钮边长
constexpr int kFsBtnMargin = 18;   // 与视频区右上角的间距（大于四角缩放热区，避免重叠）

// 程序绘制全屏/恢复图标（四角括号：expand 朝外，restore 朝内），与 HUD 配色一致
QPixmap paintFsPixmap(bool expand, const QColor& c)
{
    const int n = 16;
    QPixmap pm(n, n);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));

    const qreal o = 2.0, i = 6.5;      // 外边距 / 内边距
    const qreal f = n - o, g = n - i;  // 对应的远端坐标
    auto arm = [&p](QPointF a, QPointF b, QPointF c) {
        const QPointF pts[3] = { a, b, c };
        p.drawPolyline(pts, 3);
    };
    if (expand) {
        // 角点在外侧，臂指向中心方向的边
        arm(QPointF(o, i), QPointF(o, o), QPointF(i, o));
        arm(QPointF(g, o), QPointF(f, o), QPointF(f, i));
        arm(QPointF(f, g), QPointF(f, f), QPointF(g, f));
        arm(QPointF(i, f), QPointF(o, f), QPointF(o, g));
    } else {
        // 角点在内侧，臂指向外边
        arm(QPointF(o, i), QPointF(i, i), QPointF(i, o));
        arm(QPointF(g, o), QPointF(g, i), QPointF(f, i));
        arm(QPointF(f, g), QPointF(g, g), QPointF(g, f));
        arm(QPointF(i, f), QPointF(i, g), QPointF(o, g));
    }
    return pm;
}

QIcon makeFsIcon(bool expand)
{
    QIcon ic;
    ic.addPixmap(paintFsPixmap(expand, QColor("#00cc88")), QIcon::Normal);
    ic.addPixmap(paintFsPixmap(expand, QColor("#00ff99")), QIcon::Active);
    return ic;
}
} // namespace

HudWindow::HudWindow(UiController* ctrl, const QString& greenLogoPath, const QString& appIconDir, QWidget* parent)
    : QQuickWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setMouseTracking(true);
    rootContext()->setContextProperty("uiCtrl", ctrl);
    rootContext()->setContextProperty("greenLogoUrl",
        greenLogoPath.isEmpty() ? QString() : QUrl::fromLocalFile(greenLogoPath).toString());
    rootContext()->setContextProperty("appIconDir", appIconDir);
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
    videoContainer_->installEventFilter(this);   // 跟随尺寸变化重排按钮 / Esc 退出全屏

    // 右上角全屏切换按钮：作为视频 widget 的子控件，始终浮在图像之上
    if (!fsBtn_) {
        fsBtn_ = new QToolButton(videoContainer_);
        fsBtn_->setObjectName("videoFullscreenBtn");
        fsBtn_->setFixedSize(kFsBtnSize, kFsBtnSize);
        fsBtn_->setIconSize(QSize(16, 16));
        fsBtn_->setCursor(Qt::PointingHandCursor);
        fsBtn_->setFocusPolicy(Qt::NoFocus);
        fsBtn_->setAutoRaise(true);
        fsBtn_->setStyleSheet(
            "QToolButton { background:#a0020806; border:1px solid #00cc88; border-radius:2px; }"
            "QToolButton:hover { background:#0d2a1e; border-color:#00ff99; }");
        connect(fsBtn_, &QToolButton::clicked, this, [this](){ setVideoFullscreen(!videoFullscreen_); });
        updateFullscreenButtonUi();
    }

    videoContainer_->show();
    syncVideoGeometry();
}

void HudWindow::setVideoFullscreen(bool on)
{
    if (videoFullscreen_ == on) return;
    videoFullscreen_ = on;
    if (!on) {
        // 退出全屏：复位拖动/缩放状态与光标
        winDragging_    = false;
        fsResizeCorner_ = NoCorner;
        if (videoContainer_) videoContainer_->unsetCursor();
    }
    updateFullscreenButtonUi();
    syncVideoGeometry();
    if (on && videoContainer_) videoContainer_->setFocus();   // 让 Esc 直接可用
}

void HudWindow::updateFullscreenButtonUi()
{
    if (!fsBtn_) return;
    fsBtn_->setIcon(makeFsIcon(!videoFullscreen_));
    fsBtn_->setToolTip(videoFullscreen_ ? tr("恢复布局 (Esc)") : tr("全屏显示"));
}

void HudWindow::layoutFullscreenButton()
{
    if (!fsBtn_ || !videoContainer_) return;
    fsBtn_->move(videoContainer_->width() - fsBtn_->width() - kFsBtnMargin, kFsBtnMargin);
    fsBtn_->raise();
}

HudWindow::Corner HudWindow::cornerAt(const QPoint& p) const
{
    if (!videoContainer_) return NoCorner;
    const int w = videoContainer_->width(), h = videoContainer_->height();
    const bool l = p.x() < kCornerZone,  r = p.x() >= w - kCornerZone;
    const bool t = p.y() < kCornerZone,  b = p.y() >= h - kCornerZone;
    if (t && l) return TopLeft;
    if (t && r) return TopRight;
    if (b && l) return BottomLeft;
    if (b && r) return BottomRight;
    return NoCorner;
}

bool HudWindow::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == videoContainer_) {
        switch (e->type()) {
        case QEvent::Resize:
            layoutFullscreenButton();
            break;

        case QEvent::KeyPress:
            if (videoFullscreen_ && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
                setVideoFullscreen(false);
                return true;
            }
            break;

        // 全屏态：标题栏与右下角 resize 热区都被视频盖住，改为在视频上操作窗口：
        //  - 四角热区左键拖动 → 缩放窗口（优先）
        //  - 其余位置左键拖动 → 移动窗口，仅在图像未放大（zoom==1，无平移需求）时生效；
        //    已放大时保持原有平移图像行为。
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(e);
            if (videoFullscreen_ && me->button() == Qt::LeftButton && !isMaximized()) {
                const Corner c = cornerAt(me->pos());
                if (c != NoCorner) {
                    fsResizeCorner_ = c;
                    fsResizeStart_  = me->globalPos();
                    fsGeomAtStart_  = geometry();
                    return true;
                }
                auto* view = qobject_cast<ZoomPanImageView*>(videoContainer_);
                if (!view || view->zoom() <= 1.0 + 1e-6) {
                    winDragging_   = true;
                    winDragStart_  = me->globalPos();
                    winPosAtStart_ = pos();
                    videoContainer_->setCursor(Qt::SizeAllCursor);
                }
            }
            break;
        }
        case QEvent::MouseMove: {
            auto* me = static_cast<QMouseEvent*>(e);
            if (fsResizeCorner_ != NoCorner) {
                const QPoint d = me->globalPos() - fsResizeStart_;
                QRect r = fsGeomAtStart_;
                const bool left = (fsResizeCorner_ == TopLeft || fsResizeCorner_ == BottomLeft);
                const bool top  = (fsResizeCorner_ == TopLeft || fsResizeCorner_ == TopRight);
                if (left) r.setLeft(r.left() + d.x());  else r.setRight(r.right() + d.x());
                if (top)  r.setTop(r.top() + d.y());    else r.setBottom(r.bottom() + d.y());
                // 最小尺寸：固定不动的对角不变，只回收移动的那条边
                if (r.width() < kMinW)  { if (left) r.setLeft(r.right() - kMinW + 1);  else r.setRight(r.left() + kMinW - 1); }
                if (r.height() < kMinH) { if (top)  r.setTop(r.bottom() - kMinH + 1);  else r.setBottom(r.top() + kMinH - 1); }
                setGeometry(r);
                return true;
            }
            if (winDragging_) {
                move(winPosAtStart_ + (me->globalPos() - winDragStart_));
                return true;
            }
            // 悬停反馈：仅在无按键时接管光标，避免干扰 ZoomPanImageView 平移时的手形光标
            if (videoFullscreen_ && me->buttons() == Qt::NoButton && !isMaximized()) {
                switch (cornerAt(me->pos())) {
                case TopLeft: case BottomRight: videoContainer_->setCursor(Qt::SizeFDiagCursor); break;
                case TopRight: case BottomLeft: videoContainer_->setCursor(Qt::SizeBDiagCursor); break;
                default: videoContainer_->unsetCursor(); break;
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            if (static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
                if (fsResizeCorner_ != NoCorner) {
                    fsResizeCorner_ = NoCorner;
                    return true;
                }
                if (winDragging_) {
                    winDragging_ = false;
                    videoContainer_->unsetCursor();
                }
            }
            break;

        default:
            break;
        }
    }
    return QQuickWidget::eventFilter(obj, e);
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

    // 全屏态：铺满整个主界面；ZoomPanImageView 自身按 fit 比例绘制，图像比例不变
    if (videoFullscreen_) {
        videoContainer_->setGeometry(rect());
        videoContainer_->raise();
        layoutFullscreenButton();
        return;
    }

    QQuickItem* area = rootObject()->findChild<QQuickItem*>("videoArea");
    if (!area) return;

    QPointF pos = area->mapToScene(QPointF(0, 0));
    videoContainer_->setGeometry(
        qRound(pos.x()), qRound(pos.y()),
        qRound(area->width()), qRound(area->height())
    );
    videoContainer_->raise();
    layoutFullscreenButton();
}
