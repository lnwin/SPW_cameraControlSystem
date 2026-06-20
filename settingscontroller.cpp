#include "settingscontroller.h"
#include "languagemanager.h"
#include <QSettings>
#include <QFileDialog>
#include <QRegularExpression>

// 去掉路径末尾的日期子目录，如 /2025-12-27 或 \2026-05-22
static QString stripDateSuffix(const QString& path)
{
    static const QRegularExpression re(R"([/\\]\d{4}-\d{2}-\d{2}$)");
    return QString(path).remove(re);
}

SettingsController::SettingsController(QObject* parent) : QObject(parent)
{
    // 跟随 LanguageManager 的语言切换，保持 language_ 属性同步
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this](){
        const QString loc = LanguageManager::instance().currentLocale();
        if (language_ != loc) { language_ = loc; emit languageChanged(); }
    });
    load();
}

void SettingsController::load()
{
    QSettings s("SPwater", "CameraControl");
    setCapturePath(stripDateSuffix(s.value("paths/capturePath", "D:/SP_camera_capture").toString()));
    setRecordPath(stripDateSuffix(s.value("paths/recordPath",   "D:/SP_camera_record").toString()));
    setCaptureType(s.value("format/captureType", 0).toInt());
    setRecordType(s.value("format/recordType",   0).toInt());
    setOverlayEnabled(s.value("overlay/enabled", true).toBool());
    language_ = s.value("language/locale", "zh_CN").toString();
}

void SettingsController::save()
{
    QSettings s("SPwater", "CameraControl");
    s.setValue("paths/capturePath",  capturePath_);
    s.setValue("paths/recordPath",   recordPath_);
    s.setValue("format/captureType", captureType_);
    s.setValue("format/recordType",  recordType_);
    s.setValue("overlay/enabled",    overlayEnabled_);

    myRecordOptions opt;
    opt.capturePath     = capturePath_;
    opt.recordPath      = recordPath_;
    opt.capturType      = static_cast<ImageFormat>(captureType_);
    opt.recordType      = static_cast<VideoContainer>(recordType_);
    opt.overlayEnabled  = overlayEnabled_;
    emit settingsSaved(opt);
}

void SettingsController::browseCaptureDir()
{
    QString dir = QFileDialog::getExistingDirectory(nullptr, tr("选择截图保存目录"), capturePath_);
    if (!dir.isEmpty()) setCapturePath(dir);
}

void SettingsController::browseRecordDir()
{
    QString dir = QFileDialog::getExistingDirectory(nullptr, tr("选择录像保存目录"), recordPath_);
    if (!dir.isEmpty()) setRecordPath(dir);
}

void SettingsController::setLanguage(const QString& locale)
{
    // showErrors=true：用户主动切换时，加载失败弹窗告知
    LanguageManager::instance().switchLanguage(locale, true);
    // language_ 由构造里的 connect(languageChanged) 自动同步，此处无需重复赋值
}
