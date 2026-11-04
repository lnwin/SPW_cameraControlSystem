#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QWidget>
#include "cameraview.h"
#include "udpserver.h"

// 多相机管理器：统一管理所有 CameraView 实例
class CameraManager : public QObject
{
    Q_OBJECT
public:
    explicit CameraManager(QWidget* parentWidget, QObject* parent = nullptr);
    ~CameraManager() override;

    // 绑定设备管理器（监听设备发现信号）
    void bindDeviceManager(UdpDeviceManager* mgr);

    // 相机访问
    CameraView* getCamera(const QString& sn) const;
    QList<QString> allCameraSns() const;
    QList<CameraView*> allCameras() const;
    int cameraCount() const { return cameras_.size(); }

    // 活跃相机（成功拉流且有有效帧）
    QList<QString> activeStreamingSns() const;
    int activeStreamingCount() const;

    // 当前选中的相机
    void setSelectedCamera(const QString& sn);
    QString selectedCameraSn() const { return selectedSn_; }
    CameraView* selectedCamera() const;

    // 手动添加/移除相机（通常由自动发现驱动）
    void addCamera(const QString& sn, const QString& rtspUrl);
    void removeCamera(const QString& sn);

signals:
    // 相机列表变化
    void cameraAdded(const QString& sn);
    void cameraRemoved(const QString& sn);

    // 相机状态变化
    void cameraConnected(const QString& sn);
    void cameraDisconnected(const QString& sn);
    void cameraStreamingStatusChanged(const QString& sn, bool isStreaming);

    // 选中相机变化
    void selectedCameraChanged(const QString& sn);

    // 活跃相机数量变化（用于触发布局切换）
    void activeStreamingCountChanged(int count);

    // 转发日志
    void logMessage(const QString& sn, const QString& msg);

private slots:
    void onDeviceDiscovered(const QString& sn);
    void onCameraConnected();
    void onCameraDisconnected();
    void onCameraFpsUpdated(int fps);
    void onCameraLogMessage(const QString& sn, const QString& msg);

private:
    QString buildRtspUrl(const QString& sn);
    void updateActiveStreamingCount();

    QWidget* parentWidget_;
    UdpDeviceManager* deviceMgr_ = nullptr;

    // SN -> CameraView
    QMap<QString, CameraView*> cameras_;

    // 当前选中的相机
    QString selectedSn_;

    // 活跃流计数（用于检测变化）
    int lastActiveCount_ = 0;
};
