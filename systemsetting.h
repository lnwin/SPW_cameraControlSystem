#ifndef SYSTEMSETTING_H
#define SYSTEMSETTING_H

#include <QWidget>

namespace Ui {
class systemsetting;
}

class systemsetting : public QWidget
{
    Q_OBJECT

public:
    explicit systemsetting(QWidget *parent = nullptr);
    ~systemsetting();

private:
    Ui::systemsetting *ui;
};

#endif // SYSTEMSETTING_H
