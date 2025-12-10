#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QDateTime>
#include <QRegularExpression>
#include <QHash>
#include <QMutex>
#include <QNetworkInterface>
struct DeviceInfo {
    QString      sn;
    QHostAddress ip;
    quint16      lastPort = 0;         // 最近一次发包的源端口（可用于单播回包）
    qint64       lastSeenMs = 0;       // 最近在线时间戳
};

class UdpDeviceManager : public QObject {
    Q_OBJECT
public:
    explicit UdpDeviceManager(QObject* parent=nullptr);

    // 新增：同时启动“发现端口”和“心跳端口”
    bool start(quint16 discoverPort, quint16 heartbeatPort);
    void stop();

    // 发送：按 SN 发指令
    //  - 优先使用参数 port；否则用设备 lastPort；还没有则用 defaultCmdPort_
    //  - 返回写入字节数；<0 表示失败
    qint64 sendCommandToSn(const QString& sn, const QByteArray& payload, quint16 port = 0);

    // 广播/组播（可选）
    qint64 broadcastCommand(const QByteArray& payload, quint16 port = 0);

    // 设备表查询
    QList<QString> allSns() const;
    bool getDevice(const QString& sn, DeviceInfo& out) const;

    // 配置
    void setDefaultCmdPort(quint16 p) { defaultCmdPort_ = p; }
    quint16 defaultCmdPort() const { return defaultCmdPort_; }

    qint64 sendSetIp(const QString& sn, const QString& ip, int mask, const QString& iface = QString());
    // 发送相机曝光/增益设置命令到指定 SN 的下位机
    qint64 sendSetCameraParams(const QString& sn,int exposureUs, double gainDb);


signals:
    // 发现新设备或已存在设备信息更新（UI 用这个把 SN 填到 ComboBox）
    void snDiscoveredOrUpdated(const QString& sn);
    // 收到任意 UDP 数据（便于调试/日志/上层处理）
    void datagramReceived(const QString& sn, const QHostAddress& ip, quint16 port, const QByteArray& payload);
    void logLine(const QString& line);

private slots:
    void onReadyRead();
     void onReadyReadHb();  // 新增：心跳端口回调

private:
    void upsertDevice(const QString& sn, const QHostAddress& ip, quint16 srcPort);
    QString parseSn(const QString& msg) const;

private:
    QUdpSocket* sock_ = nullptr;
    quint16     listenPort_ = 0;
    quint16     defaultCmdPort_ = 7777; // 没有明确端口时，用它发指令
    QHash<QString, DeviceInfo> devices_; // key=SN
    mutable QMutex mtx_;                 // 保护 devices_

    // 新增：心跳端口（默认 8888）
    QUdpSocket* sockHb_  = nullptr;
    quint16     hbPort_  = 8888;

    QHostAddress pickLocalIpSameSubnet(const QHostAddress& peer) const;

};
