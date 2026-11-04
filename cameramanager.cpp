#include "cameramanager.h"
#include <QTimer>

CameraManager::CameraManager(QWidget* parentWidget, QObject* parent)
    : QObject(parent)
    , parentWidget_(parentWidget)
{
}

CameraManager::~CameraManager()
{
    // 清理所有相机
    for (auto* cam : cameras_) {
        if (cam) {
            cam->disconnect();
            cam->deleteLater();
        }
    }
    cameras_.clear();
}

void CameraManager::bindDeviceManager(UdpDeviceManager* mgr)
{
    if (deviceMgr_) {
        disconnect(deviceMgr_, nullptr, this, nullptr);
    }

    deviceMgr_ = mgr;

    if (deviceMgr_) {
        connect(deviceMgr_, &UdpDeviceManager::snDiscoveredOrUpdated,
                this, &CameraManager::onDeviceDiscovered);
    }
}

CameraView* CameraManager::getCamera(const QString& sn) const
{
    return cameras_.value(sn, nullptr);
}

QList<QString> CameraManager::allCameraSns() const
{
    return cameras_.keys();
}

QList<CameraView*> CameraManager::allCameras() const
{
    return cameras_.values();
}

QList<QString> CameraManager::activeStreamingSns() const
{
    QList<QString> result;
    for (auto it = cameras_.constBegin(); it != cameras_.constEnd(); ++it) {
        CameraView* cam = it.value();
        // 判断条件：已连接 && 当前有帧率（说明有有效视频帧）
        if (cam && cam->isConnected() && cam->currentFps() > 0) {
            result.append(it.key());
        }
    }
    return result;
}

int CameraManager::activeStreamingCount() const
{
    return activeStreamingSns().count();
}

void CameraManager::setSelectedCamera(const QString& sn)
{
    qDebug() << "[CameraManager] setSelectedCamera called, SN:" << sn << "current selectedSn_:" << selectedSn_;

    if (selectedSn_ == sn) {
        qDebug() << "[CameraManager] Same camera already selected, returning";
        return;
    }

    selectedSn_ = sn;
    qDebug() << "[CameraManager] Emitting selectedCameraChanged signal for SN:" << sn;
    emit selectedCameraChanged(sn);
}

CameraView* CameraManager::selectedCamera() const
{
    return getCamera(selectedSn_);
}

void CameraManager::addCamera(const QString& sn, const QString& rtspUrl)
{
    if (cameras_.contains(sn)) {
        // 已存在，不重复创建
        return;
    }

    // 创建新的 CameraView
    CameraView* cam = new CameraView(sn, parentWidget_, this);

    // 连接信号
    connect(cam, &CameraView::connected, this, &CameraManager::onCameraConnected);
    connect(cam, &CameraView::disconnected, this, &CameraManager::onCameraDisconnected);
    connect(cam, &CameraView::fpsUpdated, this, &CameraManager::onCameraFpsUpdated);
    connect(cam, &CameraView::logMessage, this, &CameraManager::onCameraLogMessage);

    // 转发录像和截图信号
    connect(cam, &CameraView::recordingStarted, this, [this, sn](const QString& path){
        emit logMessage(sn, QString("Recording started: %1").arg(path));
    });
    connect(cam, &CameraView::recordingStopped, this, [this, sn](const QString& path){
        emit logMessage(sn, QString("Recording stopped: %1").arg(path));
    });
    connect(cam, &CameraView::snapshotSaved, this, [this, sn](const QString& path){
        emit logMessage(sn, QString("Snapshot saved: %1").arg(path));
    });

    cameras_[sn] = cam;
    emit cameraAdded(sn);

    // 自动建立 RTSP 连接
    cam->connectToCamera(rtspUrl);

    // 如果是第一台相机，自动选中
    if (selectedSn_.isEmpty()) {
        setSelectedCamera(sn);
    }
}

void CameraManager::removeCamera(const QString& sn)
{
    CameraView* cam = cameras_.value(sn, nullptr);
    if (!cam) {
        return;
    }

    // 断开连接
    cam->disconnect();

    // 从映射中移除
    cameras_.remove(sn);
    emit cameraRemoved(sn);

    // 如果移除的是当前选中的相机，选中其他相机
    if (selectedSn_ == sn) {
        if (!cameras_.isEmpty()) {
            setSelectedCamera(cameras_.firstKey());
        } else {
            selectedSn_.clear();
            emit selectedCameraChanged(QString());
        }
    }

    // 延迟删除
    cam->deleteLater();

    // 更新活跃流计数
    updateActiveStreamingCount();
}

QString CameraManager::buildRtspUrl(const QString& sn)
{
    if (!deviceMgr_) {
        return QString();
    }

    DeviceInfo dev;
    if (!deviceMgr_->getDevice(sn, dev)) {
        return QString();
    }

    // 构建 RTSP URL: rtsp://{ip}:8554/{sn}
    // 优先使用设备信息中的 rtspPath，如果没有则使用 SN
    QString path = dev.rtspPath.isEmpty() ? sn : dev.rtspPath;
    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    return QString("rtsp://%1:%2%3")
        .arg(dev.ip.toString())
        .arg(dev.rtspPort)
        .arg(path);
}

void CameraManager::updateActiveStreamingCount()
{
    int current = activeStreamingCount();
    if (current != lastActiveCount_) {
        lastActiveCount_ = current;
        emit activeStreamingCountChanged(current);
    }
}

void CameraManager::onDeviceDiscovered(const QString& sn)
{
    // 检查是否已经创建了该相机的 CameraView
    if (cameras_.contains(sn)) {
        // 已存在，检查连接状态
        CameraView* cam = cameras_[sn];
        if (!cam->isConnected()) {
            // 如果断开了，尝试重新连接
            QString rtspUrl = buildRtspUrl(sn);
            if (!rtspUrl.isEmpty()) {
                cam->connectToCamera(rtspUrl);
                emit logMessage(sn, QString("Device back online, reconnecting..."));
            }
        }
        return;
    }

    // 新设备：创建 CameraView 并自动连接
    CameraView* cam = new CameraView(sn, parentWidget_, this);

    // 连接信号
    connect(cam, &CameraView::connected, this, &CameraManager::onCameraConnected);
    connect(cam, &CameraView::disconnected, this, &CameraManager::onCameraDisconnected);
    connect(cam, &CameraView::fpsUpdated, this, &CameraManager::onCameraFpsUpdated);
    connect(cam, &CameraView::logMessage, this, &CameraManager::onCameraLogMessage);

    // 转发录像和截图信号
    connect(cam, &CameraView::recordingStarted, this, [this, sn](const QString& path){
        emit logMessage(sn, QString("Recording started: %1").arg(path));
    });
    connect(cam, &CameraView::recordingStopped, this, [this, sn](const QString& path){
        emit logMessage(sn, QString("Recording stopped: %1").arg(path));
    });
    connect(cam, &CameraView::snapshotSaved, this, [this, sn](const QString& path){
        emit logMessage(sn, QString("Snapshot saved: %1").arg(path));
    });

    cameras_[sn] = cam;
    emit cameraAdded(sn);

    // 自动建立 RTSP 连接
    QString rtspUrl = buildRtspUrl(sn);
    if (!rtspUrl.isEmpty()) {
        cam->connectToCamera(rtspUrl);
        emit logMessage(sn, QString("Camera discovered, connecting to %1").arg(rtspUrl));
    } else {
        emit logMessage(sn, QString("Camera discovered but failed to build RTSP URL"));
    }

    // 如果是第一台相机，自动选中
    if (selectedSn_.isEmpty()) {
        setSelectedCamera(sn);
    }
}

void CameraManager::onCameraConnected()
{
    CameraView* cam = qobject_cast<CameraView*>(sender());
    if (!cam) return;

    QString sn = cam->sn();
    emit cameraConnected(sn);
    emit cameraStreamingStatusChanged(sn, true);

    // 连接成功后可能会影响活跃流计数，延迟检查（等待第一帧到达）
    QTimer::singleShot(1000, this, [this](){
        updateActiveStreamingCount();
    });
}

void CameraManager::onCameraDisconnected()
{
    CameraView* cam = qobject_cast<CameraView*>(sender());
    if (!cam) return;

    QString sn = cam->sn();
    emit cameraDisconnected(sn);
    emit cameraStreamingStatusChanged(sn, false);

    // 更新活跃流计数
    updateActiveStreamingCount();
}

void CameraManager::onCameraFpsUpdated(int fps)
{
    CameraView* cam = qobject_cast<CameraView*>(sender());
    if (!cam) return;

    // FPS 变化可能影响活跃流状态（从0变为>0，或从>0变为0）
    // 这里做一个简单的检查
    static QHash<CameraView*, int> lastFpsMap;
    int lastFps = lastFpsMap.value(cam, 0);

    if ((lastFps == 0 && fps > 0) || (lastFps > 0 && fps == 0)) {
        updateActiveStreamingCount();
    }

    lastFpsMap[cam] = fps;
}

void CameraManager::onCameraLogMessage(const QString& sn, const QString& msg)
{
    emit logMessage(sn, msg);
}
