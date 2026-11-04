#include "multicameravideogrid.h"
#include "udpserver.h"
#include <QPainter>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QInputDialog>

// ==================== ClickableLabel ====================

void ClickableLabel::mouseDoubleClickEvent(QMouseEvent* event)
{
    qDebug() << "[ClickableLabel] mouseDoubleClickEvent triggered!";
    emit doubleClicked();
    QLabel::mouseDoubleClickEvent(event);
}

// ==================== VideoWindowWidget ====================

VideoWindowWidget::VideoWindowWidget(CameraView* cameraView, QWidget* parent)
    : QWidget(parent)
    , cameraView_(cameraView)
{
    setMinimumSize(160, 120);

    // 设置默认叠加文字
    overlayText_ = tr("双击改动文字信息");

    // 关闭 CameraView 的 overlay 功能（不在视频画面上叠加文字）
    cameraView_->setOverlayEnabled(false);

    // 为视频视图安装事件过滤器，捕获双击事件
    ZoomPanImageView* videoView = cameraView_->view();
    if (videoView) {
        videoView->installEventFilter(this);
        qDebug() << "[VideoWidget] Event filter installed on ZoomPanImageView for" << cameraView_->sn();
    }

    createInfoOverlay();

    // 连接帧率更新信号
    connect(cameraView_, &CameraView::fpsUpdated, this, [this](){
        updateInfoText();
    });
}

void VideoWindowWidget::createInfoOverlay()
{
    // 信息标签（左上角）- 使用可双击的标签
    infoLabel_ = new ClickableLabel(this);
    infoLabel_->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  color: #00cc88;"
        "  padding: 4px 8px;"
        "  font-size: 11px;"
        "  font-family: 'Microsoft YaHei UI';"
        "  border: 1px solid #00cc88;"
        "}"
    );
    infoLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    // 连接双击信号
    connect(static_cast<ClickableLabel*>(infoLabel_), &ClickableLabel::doubleClicked, this, [this](){
        qDebug() << "[VideoWidget] ClickableLabel doubleClicked signal received!";

        // 双击编辑叠加文字
        bool ok;
        QString newText = QInputDialog::getText(this, tr("编辑顶部文字"),
                                                 tr("顶部叠加文字："),
                                                 QLineEdit::Normal,
                                                 overlayText_, &ok);
        qDebug() << "[VideoWidget] Dialog returned: ok=" << ok << "newText=" << newText;

        if (ok) {
            qDebug() << "[VideoWidget] Calling setOverlayText with:" << newText;
            setOverlayText(newText);
        }
    });

    qDebug() << "[VideoWidget] ClickableLabel created and doubleClicked signal connected";

    updateInfoText();
    infoLabel_->adjustSize();

    // 不创建全屏按钮 - 使用原有的全屏按钮
}

void VideoWindowWidget::updateInfoText()
{
    // 黑条信息：只显示提示文字，不显示 SN 和 FPS
    QString text = tr("双击图像编辑文字信息");

    infoLabel_->setText(text);
    infoLabel_->adjustSize();

    // 确保信息标签和全屏按钮始终在最上层
    infoLabel_->raise();
    if (fullscreenBtn_) {
        fullscreenBtn_->raise();
    }
}

void VideoWindowWidget::updateInfo()
{
    updateInfoText();
}

void VideoWindowWidget::setSelected(bool selected)
{
    if (selected_ == selected) {
        return;
    }
    selected_ = selected;
    update();
}

void VideoWindowWidget::mousePressEvent(QMouseEvent* event)
{
    qDebug() << "[VideoWidget] mousePressEvent: button=" << event->button();
    if (event->button() == Qt::LeftButton) {
        emit clicked(sn());
    }
    QWidget::mousePressEvent(event);
}

void VideoWindowWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    qDebug() << "[VideoWidget] mouseDoubleClickEvent triggered! button=" << event->button();
    if (event->button() == Qt::LeftButton) {
        qDebug() << "[VideoWidget] Double-click on left button, current overlayText:" << overlayText_;

        // 双击编辑叠加文字
        bool ok;
        QString newText = QInputDialog::getText(this, tr("编辑顶部文字"),
                                                 tr("顶部叠加文字："),
                                                 QLineEdit::Normal,
                                                 overlayText_, &ok);
        qDebug() << "[VideoWidget] Dialog returned: ok=" << ok << "newText=" << newText;

        if (ok) {
            qDebug() << "[VideoWidget] Calling setOverlayText with:" << newText;
            setOverlayText(newText);
        }
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void VideoWindowWidget::setOverlayText(const QString& text)
{
    qDebug() << "[VideoWidget] setOverlayText called: old=" << overlayText_ << "new=" << text;
    overlayText_ = text;
    updateInfoText();
    qDebug() << "[VideoWidget] overlayText updated and updateInfoText called";
}

bool VideoWindowWidget::eventFilter(QObject* obj, QEvent* event)
{
    // 调试所有鼠标事件
    if (obj == cameraView_->view()) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            qDebug() << "[VideoWidget] eventFilter caught MouseButtonPress on ZoomPanImageView, button=" << mouseEvent->button();
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            qDebug() << "[VideoWidget] eventFilter caught MouseButtonDblClick on ZoomPanImageView, button=" << mouseEvent->button();

            if (mouseEvent->button() == Qt::LeftButton) {
                qDebug() << "[VideoWidget] Left button double-click detected, current overlayText:" << overlayText_;

                // 双击编辑叠加文字
                QInputDialog dlg(this);
                dlg.setWindowTitle(tr("编辑顶部文字"));
                dlg.setLabelText(tr("顶部叠加文字："));
                dlg.setTextValue(overlayText_);
                // 设置输入框样式为绿色文字
                dlg.setStyleSheet("QLineEdit { color: #00cc88; background: white; }");

                if (dlg.exec() == QDialog::Accepted) {
                    QString newText = dlg.textValue();
                    qDebug() << "[VideoWidget] Dialog returned: ok=true newText=" << newText;
                    qDebug() << "[VideoWidget] Calling setOverlayText with:" << newText;
                    setOverlayText(newText);

                    // 同时更新 CameraView 的叠加文字
                    cameraView_->setOverlayText(newText);
                } else {
                    qDebug() << "[VideoWidget] Dialog returned: ok=false";
                }
                return true;  // 事件已处理
            }
        }
    }

    if (obj == infoLabel_ && event->type() == QEvent::MouseButtonDblClick) {
        qDebug() << "[VideoWidget] Double-click detected on infoLabel_!";
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            qDebug() << "[VideoWidget] Left button double-click, current overlayText:" << overlayText_;

            // 双击编辑叠加文字
            bool ok;
            QString newText = QInputDialog::getText(this, tr("编辑顶部文字"),
                                                     tr("顶部叠加文字："),
                                                     QLineEdit::Normal,
                                                     overlayText_, &ok);
            qDebug() << "[VideoWidget] Dialog returned: ok=" << ok << "newText=" << newText;

            if (ok) {
                qDebug() << "[VideoWidget] Calling setOverlayText with:" << newText;
                setOverlayText(newText);
            }
            return true;  // 事件已处理
        }
    }
    return QWidget::eventFilter(obj, event);
}

void VideoWindowWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    // 显示选中状态文字
    if (selected_) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 设置文字样式
        QFont font("Microsoft YaHei UI", 16, QFont::Bold);
        p.setFont(font);

        // 设置文字颜色和描边
        QPen pen(QColor(0, 255, 153));  // 绿色文字
        pen.setWidth(2);
        p.setPen(pen);

        // 在中心位置显示文字
        QString text = tr("当前选中相机");
        QFontMetrics fm(font);
        QRect textRect = fm.boundingRect(text);

        // 计算文字位置（居中上方）
        int x = (width() - textRect.width()) / 2;
        int y = 40;

        // 绘制文字背景（半透明黑色）
        QRect bgRect(x - 10, y - textRect.height() - 5, textRect.width() + 20, textRect.height() + 10);
        p.fillRect(bgRect, QColor(0, 0, 0, 180));

        // 绘制文字
        p.drawText(x, y, text);
    }
}

void VideoWindowWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // 重新定位信息标签和全屏按钮
    if (infoLabel_) {
        infoLabel_->move(8, 8);
    }
    if (fullscreenBtn_) {
        fullscreenBtn_->move(width() - fullscreenBtn_->width() - 8, 8);
    }
}

// ==================== MultiCameraVideoGrid ====================

MultiCameraVideoGrid::MultiCameraVideoGrid(QWidget* parent)
    : QWidget(parent)
{
    // 设置黑色背景
    setStyleSheet("background-color: #000000;");

    // 设置最小尺寸，确保窗口可见
    setMinimumSize(640, 480);
}

void MultiCameraVideoGrid::setLayoutManager(MultiCameraLayoutManager* layoutMgr)
{
    layoutMgr_ = layoutMgr;
}

void MultiCameraVideoGrid::addCameraWindow(CameraView* cameraView)
{
    QString sn = cameraView->sn();

    qDebug() << "[VideoGrid] addCameraWindow called for SN:" << sn;

    if (windows_.contains(sn)) {
        qDebug() << "[VideoGrid] WARNING: Window already exists for SN:" << sn << "- skipping duplicate creation";
        return; // 已存在
    }

    qDebug() << "[VideoGrid] Creating new VideoWindowWidget for SN:" << sn;

    // 创建窗口包装器
    VideoWindowWidget* window = new VideoWindowWidget(cameraView, this);
    window->hide(); // 初始隐藏，等待布局计算后显示

    // 连接信号
    connect(window, &VideoWindowWidget::clicked, this, &MultiCameraVideoGrid::onWindowClicked);
    connect(window, &VideoWindowWidget::requestFullscreen, this, &MultiCameraVideoGrid::onFullscreenRequested);

    windows_[sn] = window;

    qDebug() << "[VideoGrid] VideoWindowWidget created and added to windows_ map. Total windows:" << windows_.size();

    // 将 ZoomPanImageView 设置为窗口的子控件
    ZoomPanImageView* videoView = cameraView->view();
    if (videoView) {
        videoView->setParent(window);
        // 视频视图填满整个窗口，不内缩
        videoView->setGeometry(0, 0, window->width(), window->height());
        videoView->show();
    }
}

void MultiCameraVideoGrid::removeCameraWindow(const QString& sn)
{
    VideoWindowWidget* window = windows_.value(sn, nullptr);
    if (!window) {
        return;
    }

    windows_.remove(sn);
    window->deleteLater();

    // 如果移除的是选中的窗口，清除选中状态
    if (selectedSn_ == sn) {
        selectedSn_.clear();
    }

    // 重新计算布局
    applyCurrentLayout();
}

void MultiCameraVideoGrid::setSelectedCamera(const QString& sn)
{
    qDebug() << "[VideoGrid] setSelectedCamera called, SN:" << sn << "current selectedSn_:" << selectedSn_;
    qDebug() << "[VideoGrid] windows_.contains(sn):" << windows_.contains(sn) << "windows_.size():" << windows_.size();

    if (selectedSn_ == sn) {
        qDebug() << "[VideoGrid] Same camera already selected, returning";
        return;
    }

    // 取消之前选中的窗口
    if (!selectedSn_.isEmpty()) {
        VideoWindowWidget* prevWindow = windows_.value(selectedSn_, nullptr);
        if (prevWindow) {
            qDebug() << "[VideoGrid] Deselecting previous window:" << selectedSn_;
            prevWindow->setSelected(false);
        } else {
            qDebug() << "[VideoGrid] Previous window not found for SN:" << selectedSn_;
        }
    }

    // 选中新窗口
    selectedSn_ = sn;
    VideoWindowWidget* newWindow = windows_.value(sn, nullptr);
    if (newWindow) {
        qDebug() << "[VideoGrid] Selecting new window:" << sn;
        newWindow->setSelected(true);
    } else {
        qDebug() << "[VideoGrid] ERROR: New window not found for SN:" << sn;
    }

    emit cameraSelected(sn);
}

VideoWindowWidget* MultiCameraVideoGrid::getWindow(const QString& sn) const
{
    return windows_.value(sn, nullptr);
}

void MultiCameraVideoGrid::updateLayout(const QList<QString>& activeStreamingSns)
{
    currentActiveSns_ = activeStreamingSns;
    applyCurrentLayout();
}

void MultiCameraVideoGrid::applyCurrentLayout()
{
    if (!layoutMgr_) {
        return;
    }

    // 更新布局管理器的显示区域
    layoutMgr_->setDisplayArea(rect());

    // 计算布局
    QList<VideoWindowLayout> layouts = layoutMgr_->calculateLayout(currentActiveSns_);

    // 隐藏所有窗口
    for (auto* window : windows_) {
        window->hide();
    }

    // 应用布局
    for (const VideoWindowLayout& layout : layouts) {
        VideoWindowWidget* window = windows_.value(layout.sn, nullptr);
        if (window && layout.visible) {
            window->setGeometry(layout.geometry);
            window->show();

            // 同步 ZoomPanImageView 的尺寸
            ZoomPanImageView* videoView = window->videoView();
            if (videoView) {
                // 视频视图填满窗口几何区域，不内缩
                videoView->setGeometry(0, 0, layout.geometry.width(), layout.geometry.height());
            }

            // 更新信息
            window->updateInfo();
        }
    }
}

void MultiCameraVideoGrid::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    applyCurrentLayout();
}

void MultiCameraVideoGrid::onWindowClicked(const QString& sn)
{
    setSelectedCamera(sn);
}

void MultiCameraVideoGrid::onFullscreenRequested(const QString& sn)
{
    emit cameraFullscreenRequested(sn);
}
