#include "TitleBar.h"
#include <QMouseEvent>
#include <QStyle>

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(32);
    setObjectName("CustomTitleBar");
    // === ★ 在标题前加一个图标 Label ===
    QLabel *iconLabel = new QLabel(this);
    QPixmap iconPix(":/new/prefix1/release/icons/current/04.png");   // ★ 改为你的图标资源路径
    iconLabel->setPixmap(iconPix.scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
   // iconLabel->setFixedSize(30, 30);           // 给一点 padding，让位置更好看
    iconLabel->setContentsMargins(6, 6, 0, 6);   // 左侧 padding = 6 像素



    m_titleLabel = new QLabel("TurbidCamera - 浑水相机控制系统V2.7");
    m_titleLabel->setStyleSheet("color: white; font-size:14px; padding-left:30px;");
    QPushButton *btnMin = new QPushButton("-");
    QPushButton *btnMax = new QPushButton("□");
    QPushButton *btnClose = new QPushButton("×");

    btnMin->setObjectName("TitleMinBtn");
    btnMax->setObjectName("TitleMaxBtn");
    btnClose->setObjectName("TitleCloseBtn");

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 0, 0);
    layout->addWidget(m_titleLabel);
    layout->addStretch();
    layout->addWidget(btnMin);
    layout->addWidget(btnMax);
    layout->addWidget(btnClose);

    connect(btnMin, &QPushButton::clicked, this, &TitleBar::minimizeRequested);
    connect(btnMax, &QPushButton::clicked, this, &TitleBar::maximizeRequested);
    connect(btnClose, &QPushButton::clicked, this, &TitleBar::closeRequested);

    setStyleSheet(R"(
        #CustomTitleBar {
            background-color: #0b1120;
        }
        #TitleMinBtn, #TitleMaxBtn, #TitleCloseBtn {
            background-color: transparent;
            color: white;
            border: none;
            width: 32px;
            height: 28px;
            font-size: 14px;
        }
        #TitleMinBtn:hover, #TitleMaxBtn:hover {
            background-color: #1f2937;
        }
        #TitleCloseBtn:hover {
            background-color: #dc2626;
        }
    )");
}

void TitleBar::mousePressEvent(QMouseEvent *e)
{
    m_pressed = true;
    m_pressPos = e->globalPos();
}

void TitleBar::mouseMoveEvent(QMouseEvent *e)
{
    if (m_pressed)
    {
        QWidget *wnd = window();
        wnd->move(wnd->pos() + (e->globalPos() - m_pressPos));
        m_pressPos = e->globalPos();
    }
}

void TitleBar::mouseReleaseEvent(QMouseEvent *)
{
    m_pressed = false;
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *)
{
    emit maximizeRequested();
}
