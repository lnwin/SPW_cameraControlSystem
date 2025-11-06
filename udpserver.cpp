#include "udpserver.h"
#include <QCoreApplication>

static QRegularExpression kReSn(R"(sn\s*=\s*([A-Za-z0-9\-_]+))", QRegularExpression::CaseInsensitiveOption);

UdpDeviceManager::UdpDeviceManager(QObject* parent)
    : QObject(parent)
{
}

bool UdpDeviceManager::start(quint16 discoverPort, quint16 heartbeatPort){
    // 先停掉旧的
    stop();

    listenPort_ = discoverPort;
    hbPort_     = heartbeatPort;

    // 1) 发现/普通UDP（你已有）
    sock_ = new QUdpSocket(this);
    sock_->setSocketOption(QUdpSocket::ReceiveBufferSizeSocketOption, 1<<20);
    bool ok1 = sock_->bind(QHostAddress::AnyIPv4, listenPort_,
                           QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    emit logLine(QString("[UDP-Mgr] bind DISC 0.0.0.0:%1 %2").arg(listenPort_).arg(ok1?"OK":"FAIL"));
    if (!ok1){ sock_->deleteLater(); sock_=nullptr; return false; }

    connect(sock_, &QUdpSocket::readyRead, this, &UdpDeviceManager::onReadyRead);

    // 2) 心跳端口 8888
    sockHb_ = new QUdpSocket(this);
    sockHb_->setSocketOption(QUdpSocket::ReceiveBufferSizeSocketOption, 1<<20);
    bool ok2 = sockHb_->bind(QHostAddress::AnyIPv4, hbPort_,
                             QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    emit logLine(QString("[UDP-Mgr] bind HB   0.0.0.0:%1 %2").arg(hbPort_).arg(ok2?"OK":"FAIL"));
    if (!ok2){
        disconnect(sock_, nullptr, this, nullptr);
        sock_->close(); sock_->deleteLater(); sock_=nullptr;
        sockHb_->deleteLater(); sockHb_=nullptr;
        return false;
    }
    connect(sockHb_, &QUdpSocket::readyRead, this, &UdpDeviceManager::onReadyReadHb);

    return true;
}

void UdpDeviceManager::stop(){
    auto closeDel = [](QUdpSocket*& s){
        if (!s) return;
        QObject::disconnect(s, nullptr, nullptr, nullptr);
        s->close();
        s->deleteLater();
        s = nullptr;
    };
    closeDel(sock_);
    closeDel(sockHb_);
}
void UdpDeviceManager::onReadyReadHb(){
    while (sockHb_ && sockHb_->hasPendingDatagrams()){
        QHostAddress peer; quint16 peerPort = 0;
        QByteArray buf; buf.resize(int(sockHb_->pendingDatagramSize()));
        sockHb_->readDatagram(buf.data(), buf.size(), &peer, &peerPort);
        const QString msg = QString::fromUtf8(buf).trimmed();

        // 打印收到的心跳
        emit logLine(QString("[HB-SRV] <-- RECV '%1' from %2:%3")
                         .arg(msg, peer.toString()).arg(peerPort));

        if (msg.startsWith("HB_PING")){
            static const QByteArray pong = "HB_PONG";
            const qint64 n = sockHb_->writeDatagram(pong, peer, peerPort);
            emit logLine(QString("[HB-SRV] --> PONG %1 bytes to %2:%3")
                             .arg(n).arg(peer.toString()).arg(peerPort));
        }
        // 如需把心跳也透传给你的上层回调：
        // emit datagramReceived(QString(), peer, peerPort, buf);
    }
}


qint64 UdpDeviceManager::sendCommandToSn(const QString& sn, const QByteArray& payload, quint16 port){
    QMutexLocker lk(&mtx_);
    auto it = devices_.find(sn);
    if (it == devices_.end()){
        emit logLine(QString("[UDP-Mgr] send failed: SN=%1 not found").arg(sn));
        return -1;
    }
    const QHostAddress ip = it->ip;
    quint16 dstPort = port ? port : (it->lastPort ? it->lastPort : defaultCmdPort_);

    if (!sock_){
        emit logLine("[UDP-Mgr] send failed: socket not started");
        return -2;
    }
    const qint64 n = sock_->writeDatagram(payload, ip, dstPort);
    emit logLine(QString("[UDP-Mgr] send %1 bytes to %2:%3 (SN=%4)")
                     .arg(n).arg(ip.toString()).arg(dstPort).arg(sn));
    return n;
}

qint64 UdpDeviceManager::broadcastCommand(const QByteArray& payload, quint16 port){
    if (!sock_) return -1;
    const quint16 p = port ? port : defaultCmdPort_;
    return sock_->writeDatagram(payload, QHostAddress("255.255.255.255"), p);
}

QList<QString> UdpDeviceManager::allSns() const{
    QMutexLocker lk(&mtx_);
    return devices_.keys();
}

bool UdpDeviceManager::getDevice(const QString& sn, DeviceInfo& out) const{
    QMutexLocker lk(&mtx_);
    auto it = devices_.find(sn);
    if (it == devices_.end()) return false;
    out = it.value();
    return true;
}

void UdpDeviceManager::onReadyRead(){
    while (sock_ && sock_->hasPendingDatagrams()){
        QHostAddress peer; quint16 peerPort=0;
        QByteArray buf; buf.resize(int(sock_->pendingDatagramSize()));
        sock_->readDatagram(buf.data(), buf.size(), &peer, &peerPort);
        const QString msg = QString::fromUtf8(buf).trimmed();

        // 调试：看到收到什么
        emit logLine(QString("[UDP-Mgr] <-- RECV '%1' from %2:%3")
                         .arg(msg, peer.toString()).arg(peerPort));

        // ① 发现应答：收到 DISCOVER_REQUEST 就回 DISCOVER_REPLY
        if (msg.startsWith("DISCOVER_REQUEST")){
            const QHostAddress local = pickLocalIpSameSubnet(peer);
            // 下位机只需要 host 就能建立心跳；业务端口按需附带
            const QByteArray rep = QByteArray("DISCOVER_REPLY host=")
                                   + local.toString().toUtf8()
                                   + " hb=" + QByteArray::number(hbPort_)
                                   + " port=" + QByteArray::number(defaultCmdPort_);
            const qint64 n = sock_->writeDatagram(rep, peer, peerPort);
            emit logLine(QString("[UDP-Mgr] --> DISCOVER_REPLY (%1 bytes) to %2:%3 via %4")
                             .arg(n).arg(peer.toString()).arg(peerPort).arg(local.toString()));
        }

        // ② 解析 SN（例如 DISCOVER_REQUEST sn=YSMC300-SN0001 ts=...）
        const QString sn = parseSn(msg);
        if (!sn.isEmpty()){
            upsertDevice(sn, peer, peerPort);
            emit snDiscoveredOrUpdated(sn);
        }

        emit datagramReceived(sn, peer, peerPort, buf);
    }
}


void UdpDeviceManager::upsertDevice(const QString& sn, const QHostAddress& ip, quint16 srcPort){
    QMutexLocker lk(&mtx_);
    auto& d = devices_[sn];
    d.sn = sn;
    d.ip = ip;                 // 若同 SN IP 变动，这里会刷新
    d.lastPort = srcPort;      // 最近源端口（方便单播回包/发命令）
    d.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
}

QString UdpDeviceManager::parseSn(const QString& msg) const{
    auto m = kReSn.match(msg);
    return m.hasMatch() ? m.captured(1) : QString();
}
QHostAddress UdpDeviceManager::pickLocalIpSameSubnet(const QHostAddress& peer) const{
    if (peer.protocol() != QAbstractSocket::IPv4Protocol) return QHostAddress::LocalHost;
    const quint32 peerIp = peer.toIPv4Address();

    // 先找同网段的 IPv4
    for (const auto& nic : QNetworkInterface::allInterfaces()){
        if (!(nic.flags() & QNetworkInterface::IsUp) || !(nic.flags() & QNetworkInterface::IsRunning))
            continue;
        for (const auto& e : nic.addressEntries()){
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            const quint32 ip   = e.ip().toIPv4Address();
            const quint32 mask = e.netmask().isNull() ? 0u : e.netmask().toIPv4Address();
            if (mask && ((ip & mask) == (peerIp & mask))){
                return e.ip();
            }
        }
    }
    // 兜底：第一块非回环 IPv4
    for (const auto& nic : QNetworkInterface::allInterfaces()){
        for (const auto& e : nic.addressEntries()){
            if (e.ip().protocol()==QAbstractSocket::IPv4Protocol && e.ip()!=QHostAddress::LocalHost)
                return e.ip();
        }
    }
    return QHostAddress::LocalHost;
}
