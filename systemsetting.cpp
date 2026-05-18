#include "systemsetting.h"
#include "ui_systemsetting.h"

static QSettings& cfg() {
    static QSettings s("SPwater", "CameraControl");
    return s;
}

systemsetting::systemsetting(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::systemsetting)
{
    ui->setupUi(this);
    loadSettings();
}

systemsetting::~systemsetting()
{
    delete ui;
}

void systemsetting::loadSettings()
{
    ui->capturePath->setText(cfg().value("paths/capturePath", "D:/SP_camera_capture").toString());
    ui->recordPath->setText(cfg().value("paths/recordPath", "D:/SP_camera_record").toString());
    ui->comboBox_captureType->setCurrentIndex(cfg().value("format/captureType", 0).toInt());
    ui->comboBox_recordType->setCurrentIndex(cfg().value("format/recordType", 0).toInt());
    ui->checkBox_overlayEnabled->setChecked(cfg().value("overlay/enabled", false).toBool());
}

QString systemsetting::loadTopText()
{
    return cfg().value("overlay/topText", QString()).toString();
}

void systemsetting::saveTopText(const QString& text)
{
    cfg().setValue("overlay/topText", text);
    cfg().sync();
}

void systemsetting::on_selectcapturePath_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("选择单帧图像保存目录"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );

    if (dir.isEmpty())
        return;

    ui->capturePath->setText(dir);
}


void systemsetting::on_selectrecordPath_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("选择视频保存目录"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );

    if (dir.isEmpty())
        return;

    ui->recordPath->setText(dir);
}


void systemsetting::on_buttonBox_clicked(QAbstractButton *button)
{
    Q_UNUSED(button)

    myrecordOptions.capturePath = ui->capturePath->text();
    myrecordOptions.recordPath  = ui->recordPath->text();
    myrecordOptions.overlayEnabled = ui->checkBox_overlayEnabled->isChecked();

    switch (ui->comboBox_captureType->currentIndex()) {
    case 0: myrecordOptions.capturType = ImageFormat::PNG;  break;
    case 1: myrecordOptions.capturType = ImageFormat::JPG;  break;
    case 2: myrecordOptions.capturType = ImageFormat::BMP;  break;
    default: break;
    }
    switch (ui->comboBox_recordType->currentIndex()) {
    case 0: myrecordOptions.recordType = VideoContainer::MP4; break;
    case 1: myrecordOptions.recordType = VideoContainer::AVI; break;
    default: break;
    }

    // 持久化
    cfg().setValue("paths/capturePath",  myrecordOptions.capturePath);
    cfg().setValue("paths/recordPath",   myrecordOptions.recordPath);
    cfg().setValue("format/captureType", ui->comboBox_captureType->currentIndex());
    cfg().setValue("format/recordType",  ui->comboBox_recordType->currentIndex());
    cfg().setValue("overlay/enabled",    myrecordOptions.overlayEnabled);
    cfg().sync();

    emit sendRecordOptions(myrecordOptions);
}



void systemsetting::on_buttonBox_accepted()
{
    this->hide();
}

