#ifndef SYSTEMSETTING_H
#define SYSTEMSETTING_H

#include <QWidget>
#include<QAbstractButton>
#include <myStruct.h>
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
    void on_comboBox_captureType_currentIndexChanged(int index);

    void on_comboBox_recordType_currentIndexChanged(int index);

    void on_selectcapturePath_clicked();

    void on_selectrecordPath_clicked();

    void on_buttonBox_clicked(QAbstractButton *button);
signals:

    void sendRecordOptions(recordOptions);


private:
    Ui::systemsetting *ui;
    recordOptions myrecordOptions;
};

#endif // SYSTEMSETTING_H
