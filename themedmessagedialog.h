#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>

// 与软件现有主题一致的轻量弹窗（替代 QMessageBox）
class ThemedMessageDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ThemedMessageDialog(QWidget* parent,
                                 const QString& title,
                                 const QString& message);

    // 阻塞式调用（同 QMessageBox::warning / information）
    static void warning    (QWidget* parent, const QString& title, const QString& msg);
    static void information(QWidget* parent, const QString& title, const QString& msg);

    // 非阻塞式（同 box->open()）：堆分配，WA_DeleteOnClose
    static ThemedMessageDialog* openNonModal(QWidget* parent,
                                             const QString& title,
                                             const QString& msg);
private:
    QPushButton* okBtn_;
};
