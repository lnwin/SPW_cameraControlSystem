#include "systemsetting.h"
#include "ui_systemsetting.h"

systemsetting::systemsetting(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::systemsetting)
{
    ui->setupUi(this);
}

systemsetting::~systemsetting()
{
    delete ui;
}

void systemsetting::on_comboBox_captureType_currentIndexChanged(int index)
{
    myrecordOptions.capturTpye=ui->comboBox_captureType->currentIndex();
}


void systemsetting::on_comboBox_recordType_currentIndexChanged(int index)
{
    myrecordOptions.recordTpye=ui->comboBox_captureType->currentIndex();
}


void systemsetting::on_selectcapturePath_clicked()
{
    myrecordOptions.capturePath=ui->capturePath->text();
}


void systemsetting::on_selectrecordPath_clicked()
{
    myrecordOptions.recordPath=ui->recordPath->text();
}


void systemsetting::on_buttonBox_clicked(QAbstractButton *button)
{
    emit sendRecordOptions(myrecordOptions);

}

