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
