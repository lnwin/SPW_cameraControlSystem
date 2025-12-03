#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>

class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr);

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

private:
    bool m_pressed = false;
    QPoint m_pressPos;
    QLabel *m_titleLabel;
};
