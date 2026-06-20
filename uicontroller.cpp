#include "uicontroller.h"
#include <QTimer>
#include <QSettings>

UiController::UiController(QObject* parent) : QObject(parent)
{
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &UiController::tickTime);
    timer->start(1000);
    tickTime();

    toastTimer_.setSingleShot(true);
    connect(&toastTimer_, &QTimer::timeout, this, [this](){
        toastVisible_ = false; emit toastChanged();
    });
}

void UiController::cmdSetLed(bool en)
{
    if (ledEnabled_ == en) return;
    if (!deviceOnline_) { emit requestSetLed(en); return; } // 触发 mainwindow 弹窗，UI 不变
    ledEnabled_ = en;
    emit ledEnabledChanged();
    emit requestSetLed(en);
}

void UiController::cmdSetTrigger(int mode)
{
    if (triggerMode_ == mode) return;
    if (!deviceOnline_) { emit requestSetTrigger(mode); return; }
    triggerMode_ = mode;
    emit triggerModeChanged();
    emit requestSetTrigger(mode);
}

void UiController::tickTime()
{
    const QString t = QDateTime::currentDateTime().toString("yyyy-MM-dd  hh:mm:ss");
    if (currentTime_ == t) return;
    currentTime_ = t;
    emit currentTimeChanged();
}
