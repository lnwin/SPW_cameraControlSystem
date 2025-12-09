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



void systemsetting::on_selectcapturePath_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("选择单帧图像保存目录"),
        QDir::homePath(),              // 可改为默认路径
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );

    if (dir.isEmpty())
        return; // 用户取消选择

    ui->capturePath->setText(dir);
}


void systemsetting::on_selectrecordPath_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("选择视频保存目录"),
        QDir::homePath(),              // 可改为默认路径
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );

    if (dir.isEmpty())
        return; // 用户取消选择

    ui->recordPath->setText(dir);
}


void systemsetting::on_buttonBox_clicked(QAbstractButton *button)
{
    myrecordOptions.capturePath=ui->capturePath->text();
    myrecordOptions.recordPath=ui->recordPath->text();

   // myrecordOptions.capturType=ui->comboBox_captureType->currentIndex();
  //  myrecordOptions.recordType=ui->comboBox_captureType->currentIndex();

    emit sendRecordOptions(myrecordOptions);

}

