#ifndef SYSTEMSETTING_H
#define SYSTEMSETTING_H

#include <QWidget>
#include<QAbstractButton>
#include <myStruct.h>
#include <QFileDialog>
namespace Ui {
class systemsetting;
}

class systemsetting : public QWidget
{
    Q_OBJECT

public:
    explicit systemsetting(QWidget *parent = nullptr);
    ~systemsetting();

private slots:

    void on_selectcapturePath_clicked();

    void on_selectrecordPath_clicked();

    void on_buttonBox_clicked(QAbstractButton *button);
signals:

    void sendRecordOptions(myRecordOptions);


private:
    Ui::systemsetting *ui;
    myRecordOptions myrecordOptions;
};

#endif // SYSTEMSETTING_H
