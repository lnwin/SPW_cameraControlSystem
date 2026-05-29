#include "uicontroller.h"
#include <QTimer>

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

void UiController::tickTime()
{
    const QString t = QDateTime::currentDateTime().toString("yyyy-MM-dd  hh:mm:ss");
    if (currentTime_ == t) return;
    currentTime_ = t;
    emit currentTimeChanged();
}
