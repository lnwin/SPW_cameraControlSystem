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
    if (updatingTriggerUi_) return;                          // 程序回退时，不响应
    if (triggerUiState_ == TriggerUiState::WaitingAck) return; // pending 期间忽略重复点击
    if (triggerMode_ == mode) return;

    if (!deviceOnline_) {
        emit requestSetTrigger(mode);  // mainwindow 会弹框，不改本地状态
        return;
    }

    requestedTriggerMode_ = mode;
    qDebug("[TRIGGER_UI] user request mode=%s", mode == 1 ? "hardware" : "software");

    triggerSwitchLocked_ = true;
    triggerUiState_      = TriggerUiState::WaitingAck;
    triggerStatusMsg_    = tr("等待相机确认...");
    emit triggerSwitchLockedChanged();
    emit triggerStatusMsgChanged();
    qDebug("[TRIGGER_UI] lock trigger switch, waiting device status");

    emit requestSetTrigger(mode);
}

void UiController::handleTriggerStatus(const QJsonObject& json)
{
    const QString currentMode   = json["current_mode"].toString();
    const QString requestedMode = json["requested_mode"].toString();
    const bool    fallback      = json["fallback"].toBool();

    qDebug("[TRIGGER_UI] recv TRIGGER_STATUS requested=%s current=%s fallback=%s reason=%s",
           qPrintable(requestedMode), qPrintable(currentMode),
           fallback ? "true" : "false",
           qPrintable(json["reason"].toString()));

    updatingTriggerUi_ = true;

    if (!fallback && currentMode == "hardware") {
        stableTriggerMode_ = 1;
        triggerMode_       = 1;
        triggerStatusMsg_  = tr("当前：硬件触发");
        qDebug("[TRIGGER_UI] hardware trigger confirmed");
    } else {
        stableTriggerMode_ = 0;
        triggerMode_       = 0;
        triggerStatusMsg_  = tr("当前：软件触发");
        if (fallback) {
            qDebug("[TRIGGER_UI] no hardware signal, rollback to software");
            qDebug("[TRIGGER_UI] append prominent system log: hardware trigger unavailable");
            appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ")
                      + tr("⚠ 【硬件触发不可用】当前相机不具备硬件触发功能，已退回软件触发。"));
            emit noHardwareTriggerFallback();
        } else {
            qDebug("[TRIGGER_UI] software trigger confirmed");
        }
    }

    triggerUiState_      = TriggerUiState::Idle;
    triggerSwitchLocked_ = false;
    updatingTriggerUi_   = false;

    emit triggerModeChanged();
    emit triggerSwitchLockedChanged();
    emit triggerStatusMsgChanged();
}

void UiController::handleTriggerTimeout()
{
    qDebug("[TRIGGER_UI] wait trigger status timeout");
    updatingTriggerUi_ = true;

    triggerMode_         = stableTriggerMode_;
    triggerStatusMsg_    = (stableTriggerMode_ == 1) ? tr("当前：硬件触发") : tr("当前：软件触发");
    triggerUiState_      = TriggerUiState::Idle;
    triggerSwitchLocked_ = false;
    updatingTriggerUi_   = false;

    toastMsg_ = tr("相机未返回触发状态，已恢复到上一次模式。");
    toastSuccess_ = false; toastVisible_ = true;
    emit toastChanged();
    toastTimer_.start(3000);

    // 硬件触发请求超时 → 写入醒目系统日志
    if (requestedTriggerMode_ == 1) {
        appendLog(QDateTime::currentDateTime().toString("[hh:mm:ss] ")
            + tr("⚠【硬件触发状态未知】相机未返回硬件触发确认，已恢复到软件触发。"
                 "请检查相机是否支持硬件触发或硬件触发信号是否接入。"));
    }

    emit triggerModeChanged();
    emit triggerSwitchLockedChanged();
    emit triggerStatusMsgChanged();
}

void UiController::tickTime()
{
    const QString t = QDateTime::currentDateTime().toString("yyyy-MM-dd  hh:mm:ss");
    if (currentTime_ == t) return;
    currentTime_ = t;
    emit currentTimeChanged();
}
