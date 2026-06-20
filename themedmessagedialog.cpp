#include "themedmessagedialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

static const char* kStyle =
    "QDialog {"
    "  background:#020806; border:1px solid #00cc88; border-radius:4px; }"
    "QLabel#titleLabel {"
    "  color:#00cc88; font-family:'Microsoft YaHei UI'; font-size:12px;"
    "  padding:4px 8px; border-bottom:1px solid #00cc88; background:#07110e; }"
    "QLabel#msgLabel {"
    "  color:#9aa0a6; font-family:'Microsoft YaHei UI'; font-size:12px; padding:4px; }"
    "QPushButton {"
    "  background:transparent; color:#00cc88; border:1px solid #00cc88;"
    "  border-radius:2px; padding:4px 20px;"
    "  font-family:'Microsoft YaHei UI'; font-size:12px; }"
    "QPushButton:hover { background:#0d2a1e; color:#00ff99; border-color:#00ff99; }";

ThemedMessageDialog::ThemedMessageDialog(QWidget* parent,
                                         const QString& title,
                                         const QString& message)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog)
{
    setStyleSheet(kStyle);
    setFixedWidth(380);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* titleLabel = new QLabel(title, this);
    titleLabel->setObjectName("titleLabel");
    root->addWidget(titleLabel);

    auto* body = new QVBoxLayout;
    body->setContentsMargins(16, 14, 16, 14);
    body->setSpacing(14);

    auto* msgLabel = new QLabel(message, this);
    msgLabel->setObjectName("msgLabel");
    msgLabel->setWordWrap(true);
    body->addWidget(msgLabel);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    okBtn_ = new QPushButton(tr("确定"), this);
    connect(okBtn_, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(okBtn_);
    body->addLayout(btnRow);

    root->addLayout(body);
}

void ThemedMessageDialog::warning(QWidget* parent, const QString& title, const QString& msg)
{
    ThemedMessageDialog dlg(parent, title, msg);
    dlg.exec();
}

void ThemedMessageDialog::information(QWidget* parent, const QString& title, const QString& msg)
{
    ThemedMessageDialog dlg(parent, title, msg);
    dlg.exec();
}

ThemedMessageDialog* ThemedMessageDialog::openNonModal(QWidget* parent,
                                                        const QString& title,
                                                        const QString& msg)
{
    auto* dlg = new ThemedMessageDialog(parent, title, msg);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    return dlg;
}
