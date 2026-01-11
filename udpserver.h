// udpserver.h (FULL REPLACEABLE)

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
    quint16      lastPort = 0;      // 最近一次发包的源端口
    qint64       lastSeenMs = 0;    // 最近在线时间戳 (ms)

    // RTSP endpoint (from msg)
    quint16 rtspPort = 8554;
    QString rtspPath;              // e.g. "/YSTech-TURBIDCAM-0001"
};

class UdpDeviceManager : public QObject {
    Q_OBJECT
public:
    explicit UdpDeviceManager(QObject* parent=nullptr);

    bool start(quint16 discoverPort, quint16 heartbeatPort);
    void stop();

    qint64 sendCommandToSn(const QString& sn, const QByteArray& payload, quint16 port = 0);
    qint64 broadcastCommand(const QByteArray& payload, quint16 port = 0);

    QList<QString> allSns() const;
    bool getDevice(const QString& sn, DeviceInfo& out) const;

    void setDefaultCmdPort(quint16 p) { defaultCmdPort_ = p; }
    quint16 defaultCmdPort() const { return defaultCmdPort_; }

    qint64 sendSetIp(const QString& sn, const QString& ip, int mask, const QString& iface = QString());
    qint64 sendSetCameraParams(const QString& sn, int exposureUs, double gainDb);

signals:
    void snDiscoveredOrUpdated(const QString& sn);
    void datagramReceived(const QString& sn, const QHostAddress& ip, quint16 port, const QByteArray& payload);
    void logLine(const QString& line);

private slots:
    void onReadyRead();
    void onReadyReadHb();

private:
    void upsertDevice(const QString& sn,
                      const QHostAddress& ip,
                      quint16 srcPort,
                      const QString& rawMsg);

    QString parseSn(const QString& msg) const;
    QHostAddress pickLocalIpSameSubnet(const QHostAddress& peer) const;

private:
    QUdpSocket* sock_ = nullptr;
    QUdpSocket* sockHb_ = nullptr;

    quint16 listenPort_ = 0;
    quint16 hbPort_ = 8888;

    quint16 defaultCmdPort_ = 7777;

    QHash<QString, DeviceInfo> devices_;
    mutable QMutex mtx_;
};
