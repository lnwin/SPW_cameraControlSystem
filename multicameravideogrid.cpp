#include "multicameravideogrid.h"
#include "udpserver.h"
#include <QPainter>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QCoreApplication>

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

    // 启用 CameraView 的 overlay 功能，并设置默认叠加文字
    cameraView_->setOverlayEnabled(true);
    cameraView_->setOverlayText(overlayText_);

    // 为视频视图安装事件过滤器，捕获双击事件
    ZoomPanImageView* videoView = cameraView_->view();
    if (videoView) {
        videoView->installEventFilter(this);
        qDebug() << "[VideoWidget] Event filter installed on ZoomPanImageView for" << cameraView_->sn();
    }

    // 不创建 infoLabel_ - CameraView 内部的 applyOverlayInto() 会绘制叠加信息
}

void VideoWindowWidget::updateInfo()
{
    // 不再需要更新 infoLabel_，CameraView 内部会绘制叠加信息
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
    qDebug() << "[VideoWidget] mousePressEvent: button=" << event->button() << "pos=" << event->pos();
    if (event->button() == Qt::LeftButton) {
        emit clicked(sn());
    }

    // 将事件转发给父控件（MultiCameraVideoGrid），让它可以处理窗口拖动
    if (parentWidget()) {
        QMouseEvent parentEvent(event->type(), mapToParent(event->pos()), event->globalPos(),
                               event->button(), event->buttons(), event->modifiers());
        QCoreApplication::sendEvent(parentWidget(), &parentEvent);
        if (parentEvent.isAccepted()) {
            event->accept();
            return;
        }
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

    // 更新 CameraView 的叠加文字
    if (cameraView_) {
        cameraView_->setOverlayText(overlayText_);
    }

    qDebug() << "[VideoWidget] overlayText updated and sent to CameraView";
}

bool VideoWindowWidget::eventFilter(QObject* obj, QEvent* event)
{
    // 处理 ZoomPanImageView 上的鼠标事件
    if (obj == cameraView_->view()) {
        // 双击事件用于编辑文字
        if (event->type() == QEvent::MouseButtonDblClick) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            qDebug() << "[VideoWidget] eventFilter caught MouseButtonDblClick on ZoomPanImageView, button=" << mouseEvent->button();

            if (mouseEvent->button() == Qt::LeftButton) {
                qDebug() << "[VideoWidget] Left button double-click detected, current overlayText:" << overlayText_;

                // 创建自定义样式的输入对话框
                QDialog dlg(this);
                dlg.setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);  // 无边框
                dlg.setModal(true);
                dlg.setFixedSize(400, 180);

                // 设置主对话框背景
                dlg.setStyleSheet("QDialog { background-color: #1e1e1e; }");

                // 创建布局
                QVBoxLayout* mainLayout = new QVBoxLayout(&dlg);
                mainLayout->setContentsMargins(0, 0, 0, 0);
                mainLayout->setSpacing(0);

                // 自定义标题栏
                QWidget* titleBar = new QWidget(&dlg);
                titleBar->setFixedHeight(40);
                titleBar->setStyleSheet("QWidget { background-color: #0d2a1e; border-bottom: 1px solid #00cc88; }");
                QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
                titleLayout->setContentsMargins(15, 0, 10, 0);

                QLabel* titleLabel = new QLabel(tr("编辑顶部文字"), titleBar);
                titleLabel->setStyleSheet("QLabel { color: #00cc88; font-size: 14px; font-weight: bold; }");
                titleLayout->addWidget(titleLabel);
                titleLayout->addStretch();

                // 关闭按钮
                QPushButton* closeBtn = new QPushButton("×", titleBar);
                closeBtn->setFixedSize(30, 30);
                closeBtn->setStyleSheet(
                    "QPushButton { background: transparent; color: #00cc88; border: none; font-size: 20px; }"
                    "QPushButton:hover { background-color: #ff4444; color: white; }"
                );
                connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
                titleLayout->addWidget(closeBtn);

                mainLayout->addWidget(titleBar);

                // 内容区域
                QWidget* contentWidget = new QWidget(&dlg);
                contentWidget->setStyleSheet("QWidget { background-color: #1e1e1e; }");
                QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
                contentLayout->setContentsMargins(20, 20, 20, 20);
                contentLayout->setSpacing(15);

                // 提示文字
                QLabel* promptLabel = new QLabel(tr("顶部叠加文字："), contentWidget);
                promptLabel->setStyleSheet("QLabel { color: #e0e0e0; font-size: 13px; }");
                contentLayout->addWidget(promptLabel);

                // 输入框
                QLineEdit* inputEdit = new QLineEdit(overlayText_, contentWidget);
                inputEdit->setFixedHeight(36);  // 增加输入框高度到 36px
                inputEdit->setAlignment(Qt::AlignCenter);  // 文字居中显示
                inputEdit->setStyleSheet(
                    "QLineEdit {"
                    "    background-color: white;"  // 白色背景
                    "    color: black;"  // 黑色文字
                    "    border: 1px solid #00cc88;"
                    "    border-radius: 3px;"
                    "    padding: 8px;"
                    "    font-size: 14px;"  // 字体也增大到 14px
                    "    selection-background-color: #00cc88;"
                    "}"
                );
                contentLayout->addWidget(inputEdit);

                contentLayout->addStretch();

                // 按钮区域
                QHBoxLayout* buttonLayout = new QHBoxLayout();
                buttonLayout->setSpacing(10);
                buttonLayout->addStretch();

                QPushButton* okBtn = new QPushButton(tr("确定"), contentWidget);
                okBtn->setFixedSize(80, 32);
                okBtn->setStyleSheet(
                    "QPushButton {"
                    "    background-color: #0d2a1e;"
                    "    color: #00cc88;"
                    "    border: 1px solid #00cc88;"
                    "    border-radius: 3px;"
                    "    font-size: 13px;"
                    "}"
                    "QPushButton:hover {"
                    "    background-color: #00cc88;"
                    "    color: #1e1e1e;"
                    "}"
                );
                connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
                buttonLayout->addWidget(okBtn);

                QPushButton* cancelBtn = new QPushButton(tr("取消"), contentWidget);
                cancelBtn->setFixedSize(80, 32);
                cancelBtn->setStyleSheet(
                    "QPushButton {"
                    "    background-color: #2d2d2d;"
                    "    color: #a0a0a0;"
                    "    border: 1px solid #555;"
                    "    border-radius: 3px;"
                    "    font-size: 13px;"
                    "}"
                    "QPushButton:hover {"
                    "    background-color: #3d3d3d;"
                    "    color: #e0e0e0;"
                    "}"
                );
                connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
                buttonLayout->addWidget(cancelBtn);

                contentLayout->addLayout(buttonLayout);
                mainLayout->addWidget(contentWidget);

                // 标题栏拖动功能 - 使用 lambda 和成员变量实现
                // 注意：这些变量需要在 lambda 外部声明以便正确捕获
                QPoint* dragPos = new QPoint();
                bool* isDragging = new bool(false);

                // 为标题栏安装事件过滤器以实现拖动
                auto* filter = new QObject(&dlg);
                QObject::connect(filter, &QObject::destroyed, [dragPos, isDragging]() {
                    delete dragPos;
                    delete isDragging;
                });

                titleBar->installEventFilter(filter);
                QObject* filterObj = filter;

                // 使用 lambda 覆盖 eventFilter
                struct TitleBarEventFilter : public QObject {
                    QDialog* dialog;
                    QPoint* dragPosition;
                    bool* dragging;

                    bool eventFilter(QObject* obj, QEvent* event) override {
                        if (event->type() == QEvent::MouseButtonPress) {
                            QMouseEvent* me = static_cast<QMouseEvent*>(event);
                            if (me->button() == Qt::LeftButton) {
                                *dragging = true;
                                *dragPosition = me->globalPos() - dialog->frameGeometry().topLeft();
                                return true;
                            }
                        } else if (event->type() == QEvent::MouseMove && *dragging) {
                            QMouseEvent* me = static_cast<QMouseEvent*>(event);
                            dialog->move(me->globalPos() - *dragPosition);
                            return true;
                        } else if (event->type() == QEvent::MouseButtonRelease) {
                            *dragging = false;
                        }
                        return QObject::eventFilter(obj, event);
                    }
                };

                auto* titleBarFilter = new TitleBarEventFilter();
                titleBarFilter->dialog = &dlg;
                titleBarFilter->dragPosition = dragPos;
                titleBarFilter->dragging = isDragging;
                titleBarFilter->setParent(&dlg);
                titleBar->installEventFilter(titleBarFilter);

                if (dlg.exec() == QDialog::Accepted) {
                    QString newText = inputEdit->text();
                    qDebug() << "[VideoWidget] Dialog returned: ok=true newText=" << newText;
                    setOverlayText(newText);
                    cameraView_->setOverlayText(newText);
                } else {
                    qDebug() << "[VideoWidget] Dialog returned: ok=false";
                }
                return true;
            }
        }

        // 单击、移动、释放事件转发给父控件用于窗口拖动
        if (event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseMove ||
            event->type() == QEvent::MouseButtonRelease) {

            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            qDebug() << "[VideoWidget] eventFilter caught" << event->type() << "button=" << mouseEvent->button() << "pos=" << mouseEvent->pos();

            // 转发给父控件（MultiCameraVideoGrid）
            if (parentWidget()) {
                QMouseEvent parentEvent(event->type(), mapToParent(mouseEvent->pos()), mouseEvent->globalPos(),
                                       mouseEvent->button(), mouseEvent->buttons(), mouseEvent->modifiers());
                if (QCoreApplication::sendEvent(parentWidget(), &parentEvent)) {
                    qDebug() << "[VideoWidget] Parent handled the event";
                    return true;  // 父控件处理了事件
                }
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

// 重写鼠标事件，让父窗口（HudWindow）的 eventFilter 能够处理全屏模式下的窗口拖动和调整尺寸
void MultiCameraVideoGrid::mousePressEvent(QMouseEvent* event)
{
    qDebug() << "[VideoGrid] mousePressEvent - pos:" << event->pos() << "button:" << event->button() << "globalPos:" << event->globalPos();

    // 先让父窗口的事件过滤器处理（用于全屏模式下的窗口拖动）
    // 父窗口是 HudWindow，它在 videoContainer_ (this) 上安装了 eventFilter
    // 我们需要手动触发那个 eventFilter
    if (parent()) {
        // 创建一个MouseButtonPress事件并发送给 parent，这会触发 HudWindow::eventFilter
        QMouseEvent parentEvent(event->type(), event->pos(), event->globalPos(),
                               event->button(), event->buttons(), event->modifiers());
        // 发送给父窗口，让它的 eventFilter(this, &parentEvent) 被调用
        if (QCoreApplication::sendEvent(parent(), &parentEvent)) {
            qDebug() << "[VideoGrid] Parent handled the event";
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void MultiCameraVideoGrid::mouseMoveEvent(QMouseEvent* event)
{
    // 同样转发移动事件给父窗口
    if (parent()) {
        QMouseEvent parentEvent(event->type(), event->pos(), event->globalPos(),
                               event->button(), event->buttons(), event->modifiers());
        if (QCoreApplication::sendEvent(parent(), &parentEvent)) {
            event->accept();
            return;
        }
    }

    QWidget::mouseMoveEvent(event);
}

void MultiCameraVideoGrid::mouseReleaseEvent(QMouseEvent* event)
{
    // 同样转发释放事件给父窗口
    if (parent()) {
        QMouseEvent parentEvent(event->type(), event->pos(), event->globalPos(),
                               event->button(), event->buttons(), event->modifiers());
        if (QCoreApplication::sendEvent(parent(), &parentEvent)) {
            event->accept();
            return;
        }
    }

    QWidget::mouseReleaseEvent(event);
}

void MultiCameraVideoGrid::onWindowClicked(const QString& sn)
{
    setSelectedCamera(sn);
}

void MultiCameraVideoGrid::onFullscreenRequested(const QString& sn)
{
    emit cameraFullscreenRequested(sn);
}
