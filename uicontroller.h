#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QTimer>

class UiController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString  currentTime        READ currentTime        NOTIFY currentTimeChanged)
    Q_PROPERTY(QString  deviceName         READ deviceName         NOTIFY deviceNameChanged)
    Q_PROPERTY(QString  deviceIp           READ deviceIp           NOTIFY deviceIpChanged)
    Q_PROPERTY(bool     rtspConnected      READ rtspConnected      NOTIFY rtspConnectedChanged)
    Q_PROPERTY(bool     deviceOnline       READ deviceOnline       NOTIFY deviceOnlineChanged)
    Q_PROPERTY(int      currentFps         READ currentFps         NOTIFY currentFpsChanged)
    Q_PROPERTY(QString  resolution         READ resolution         NOTIFY resolutionChanged)
    Q_PROPERTY(bool     recording          READ recording          NOTIFY recordingChanged)
    Q_PROPERTY(QString  recordFileName     READ recordFileName     NOTIFY recordFileNameChanged)
    Q_PROPERTY(int      recordSegmentIndex READ recordSegmentIndex NOTIFY recordSegmentIndexChanged)
    Q_PROPERTY(QString  recordSegmentElapsed READ recordSegmentElapsed NOTIFY recordSegmentElapsedChanged)
    Q_PROPERTY(QString  recordTotalElapsed READ recordTotalElapsed NOTIFY recordTotalElapsedChanged)
    Q_PROPERTY(QString  screenshotPath     READ screenshotPath     NOTIFY screenshotPathChanged)
    Q_PROPERTY(QString  recordSavePath     READ recordSavePath     NOTIFY recordSavePathChanged)
    Q_PROPERTY(QStringList deviceList      READ deviceList         NOTIFY deviceListChanged)
    Q_PROPERTY(QString  selectedSn         READ selectedSn         NOTIFY selectedSnChanged)
    // IP 修改等待状态
    Q_PROPERTY(bool     ipWaiting          READ ipWaiting          NOTIFY ipWaitingChanged)
    Q_PROPERTY(QString  ipWaitingMsg       READ ipWaitingMsg       NOTIFY ipWaitingChanged)
    // Toast 提示
    Q_PROPERTY(bool     toastVisible       READ toastVisible       NOTIFY toastChanged)
    Q_PROPERTY(QString  toastMsg           READ toastMsg           NOTIFY toastChanged)
    Q_PROPERTY(bool     toastSuccess       READ toastSuccess       NOTIFY toastChanged)
    // 连接中状态（防重复点击）
    Q_PROPERTY(bool     connecting         READ connecting         NOTIFY connectingChanged)

public:
    explicit UiController(QObject* parent = nullptr);

    QString     currentTime()          const { return currentTime_; }
    QString     deviceName()           const { return deviceName_; }
    QString     deviceIp()             const { return deviceIp_; }
    bool        rtspConnected()        const { return rtspConnected_; }
    bool        deviceOnline()         const { return deviceOnline_; }
    int         currentFps()           const { return currentFps_; }
    QString     resolution()           const { return resolution_; }
    bool        recording()            const { return recording_; }
    QString     recordFileName()       const { return recordFileName_; }
    int         recordSegmentIndex()   const { return recordSegmentIndex_; }
    QString     recordSegmentElapsed() const { return recordSegmentElapsed_; }
    QString     recordTotalElapsed()   const { return recordTotalElapsed_; }
    QString     screenshotPath()       const { return screenshotPath_; }
    QString     recordSavePath()       const { return recordSavePath_; }
    QStringList deviceList()           const { return deviceList_; }
    QString     selectedSn()           const { return selectedSn_; }
    bool        ipWaiting()            const { return ipWaiting_; }
    QString     ipWaitingMsg()         const { return ipWaitingMsg_; }
    bool        toastVisible()         const { return toastVisible_; }
    QString     toastMsg()             const { return toastMsg_; }
    bool        toastSuccess()         const { return toastSuccess_; }
    bool        connecting()           const { return connecting_; }

public slots:
    void setDeviceName(const QString& v)    { if (deviceName_ == v) return; deviceName_ = v; emit deviceNameChanged(); }
    void setDeviceIp(const QString& v)      { if (deviceIp_ == v) return; deviceIp_ = v; emit deviceIpChanged(); }
    void setRtspConnected(bool v)           { if (rtspConnected_ == v) return; rtspConnected_ = v; emit rtspConnectedChanged(); }
    void setDeviceOnline(bool v)            { if (deviceOnline_ == v) return; deviceOnline_ = v; emit deviceOnlineChanged(); }
    void setCurrentFps(int v)               { if (currentFps_ == v) return; currentFps_ = v; emit currentFpsChanged(); }
    void setResolution(const QString& v)    { if (resolution_ == v) return; resolution_ = v; emit resolutionChanged(); }
    void setRecording(bool v)               { if (recording_ == v) return; recording_ = v; emit recordingChanged(); }
    void setRecordFileName(const QString& v){ if (recordFileName_ == v) return; recordFileName_ = v; emit recordFileNameChanged(); }
    void setRecordSegmentIndex(int v)       { if (recordSegmentIndex_ == v) return; recordSegmentIndex_ = v; emit recordSegmentIndexChanged(); }
    void setRecordSegmentElapsed(const QString& v){ if (recordSegmentElapsed_ == v) return; recordSegmentElapsed_ = v; emit recordSegmentElapsedChanged(); }
    void setRecordTotalElapsed(const QString& v)  { if (recordTotalElapsed_ == v) return; recordTotalElapsed_ = v; emit recordTotalElapsedChanged(); }
    void setScreenshotPath(const QString& v){ if (screenshotPath_ == v) return; screenshotPath_ = v; emit screenshotPathChanged(); }
    void setRecordSavePath(const QString& v){ if (recordSavePath_ == v) return; recordSavePath_ = v; emit recordSavePathChanged(); }
    void setDeviceList(const QStringList& v){ if (deviceList_ == v) return; deviceList_ = v; emit deviceListChanged(); }
    void setSelectedSn(const QString& v)   { if (selectedSn_ == v) return; selectedSn_ = v; emit selectedSnChanged(); }
    void setConnecting(bool v)             { if (connecting_ == v) return; connecting_ = v; emit connectingChanged(); }

    void notifyIpWaiting(const QString& msg) {
        ipWaiting_ = true; ipWaitingMsg_ = msg; emit ipWaitingChanged();
    }
    void notifyIpDone(const QString& msg, bool ok) {
        ipWaiting_ = false; ipWaitingMsg_.clear(); emit ipWaitingChanged();
        toastMsg_ = msg; toastSuccess_ = ok; toastVisible_ = true; emit toastChanged();
        toastTimer_.start(3000);
    }

    Q_INVOKABLE void cmdOpenCamera()    { emit requestOpenCamera(); }
    Q_INVOKABLE void cmdCloseCamera()   { emit requestCloseCamera(); }
    Q_INVOKABLE void cmdStartRecord()   { emit requestStartRecord(); }
    Q_INVOKABLE void cmdStopRecord()    { emit requestStopRecord(); }
    Q_INVOKABLE void cmdSnapshot()      { emit requestSnapshot(); }
    Q_INVOKABLE void cmdRefreshDevices(){ emit requestRefreshDevices(); }
    Q_INVOKABLE void cmdSelectDevice(const QString& sn) { emit requestSelectDevice(sn); }
    Q_INVOKABLE void cmdChangeIp(const QString& sn)    { emit requestChangeIp(sn); }
    Q_INVOKABLE void cmdOpenFolder()    { emit requestOpenFolder(); }
    Q_INVOKABLE void cmdOpenSettings()  { emit requestOpenSettings(); }
    Q_INVOKABLE void appendLog(const QString& msg) { emit logAppended(msg); }
    Q_INVOKABLE void cmdWinMinimize()   { emit requestWinMinimize(); }
    Q_INVOKABLE void cmdWinMaximize()   { emit requestWinMaximize(); }
    Q_INVOKABLE void cmdWinClose()      { emit requestWinClose(); }
    Q_INVOKABLE void cmdWinDrag(int dx, int dy) { emit requestWinDrag(dx, dy); }

signals:
    void currentTimeChanged();
    void deviceNameChanged();
    void deviceIpChanged();
    void rtspConnectedChanged();
    void deviceOnlineChanged();
    void currentFpsChanged();
    void resolutionChanged();
    void recordingChanged();
    void recordFileNameChanged();
    void recordSegmentIndexChanged();
    void recordSegmentElapsedChanged();
    void recordTotalElapsedChanged();
    void screenshotPathChanged();
    void recordSavePathChanged();
    void deviceListChanged();
    void selectedSnChanged();
    void ipWaitingChanged();
    void toastChanged();
    void connectingChanged();
    void logAppended(const QString& msg);

    void requestOpenCamera();
    void requestCloseCamera();
    void requestStartRecord();
    void requestStopRecord();
    void requestSnapshot();
    void requestRefreshDevices();
    void requestSelectDevice(const QString& sn);
    void requestChangeIp(const QString& sn);
    void requestOpenFolder();
    void requestOpenSettings();
    void requestWinMinimize();
    void requestWinMaximize();
    void requestWinClose();
    void requestWinDrag(int dx, int dy);

private slots:
    void tickTime();

private:
    QString     currentTime_;
    QString     deviceName_          = "未连接";
    QString     deviceIp_            = "--";
    bool        rtspConnected_       = false;
    bool        deviceOnline_        = false;
    int         currentFps_          = 0;
    QString     resolution_          = "1920x1080";
    bool        recording_           = false;
    QString     recordFileName_;
    int         recordSegmentIndex_  = 0;
    QString     recordSegmentElapsed_ = "00:00";
    QString     recordTotalElapsed_   = "00:00";
    QString     screenshotPath_;
    QString     recordSavePath_;
    QStringList deviceList_;
    QString     selectedSn_;
    bool        ipWaiting_           = false;
    QString     ipWaitingMsg_;
    bool        toastVisible_        = false;
    QString     toastMsg_;
    bool        toastSuccess_        = true;
    QTimer      toastTimer_;
    bool        connecting_          = false;
};
