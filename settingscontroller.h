#pragma once
#include <QObject>
#include <QSettings>
#include "myStruct.h"

class SettingsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString capturePath   READ capturePath   WRITE setCapturePath   NOTIFY capturePathChanged)
    Q_PROPERTY(QString recordPath    READ recordPath    WRITE setRecordPath    NOTIFY recordPathChanged)
    Q_PROPERTY(int     captureType   READ captureType   WRITE setCaptureType   NOTIFY captureTypeChanged)
    Q_PROPERTY(int     recordType    READ recordType    WRITE setRecordType    NOTIFY recordTypeChanged)
    Q_PROPERTY(bool    overlayEnabled READ overlayEnabled WRITE setOverlayEnabled NOTIFY overlayEnabledChanged)
    Q_PROPERTY(QString language      READ language                              NOTIFY languageChanged)

public:
    explicit SettingsController(QObject* parent = nullptr);

    QString capturePath()    const { return capturePath_; }
    QString recordPath()     const { return recordPath_; }
    int     captureType()    const { return captureType_; }
    int     recordType()     const { return recordType_; }
    bool    overlayEnabled() const { return overlayEnabled_; }
    QString language()       const { return language_; }

    void setCapturePath(const QString& v)  { if (capturePath_ == v) return; capturePath_ = v; emit capturePathChanged(); }
    void setRecordPath(const QString& v)   { if (recordPath_ == v) return; recordPath_ = v; emit recordPathChanged(); }
    void setCaptureType(int v)             { if (captureType_ == v) return; captureType_ = v; emit captureTypeChanged(); }
    void setRecordType(int v)              { if (recordType_ == v) return; recordType_ = v; emit recordTypeChanged(); }
    void setOverlayEnabled(bool v)         { if (overlayEnabled_ == v) return; overlayEnabled_ = v; emit overlayEnabledChanged(); }

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void browseCaptureDir();
    Q_INVOKABLE void browseRecordDir();
    Q_INVOKABLE void cmdClose() { emit requestClose(); }
    Q_INVOKABLE void cmdDrag(int dx, int dy) { emit requestDrag(dx, dy); }
    Q_INVOKABLE void setLanguage(const QString& locale);

signals:
    void capturePathChanged();
    void recordPathChanged();
    void captureTypeChanged();
    void recordTypeChanged();
    void overlayEnabledChanged();
    void languageChanged();
    void settingsSaved(myRecordOptions opts);
    void requestClose();
    void requestDrag(int dx, int dy);

private:
    QString capturePath_    = "D:/SP_camera_capture";
    QString recordPath_     = "D:/SP_camera_record";
    int     captureType_    = 0;
    int     recordType_     = 0;
    bool    overlayEnabled_ = false;
    QString language_       = "zh_CN";
};
