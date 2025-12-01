#include "udpserver.h"
#include <QCoreApplication>
#include <winsock2.h>              // 必须在 <windows.h> 之前
#pragma comment(lib, "Ws2_32.lib") // VS 自动链接 winsock 库


static QRegularExpression kReSn(R"(sn\s*=\s*([A-Za-z0-9\-_]+))", QRegularExpression::CaseInsensitiveOption);

UdpDeviceManager::UdpDeviceManager(QObject* parent)
    : QObject(parent)
{
}

bool UdpDeviceManager::start(quint16 discoverPort, quint16 heartbeatPort){
    stop();

    listenPort_ = discoverPort;
    hbPort_     = heartbeatPort;

    // 1) 发现口 7777
    sock_ = new QUdpSocket(this);
    sock_->setSocketOption(QUdpSocket::ReceiveBufferSizeSocketOption, 1<<20);
    bool ok1 = sock_->bind(QHostAddress::AnyIPv4, listenPort_,
                           QUdpSocket::DontShareAddress);   // ← 建议改为 DontShareAddress
    emit logLine(QString("[UDP-Mgr] bind DISC 0.0.0.0:%1 %2").arg(listenPort_).arg(ok1?"OK":"FAIL"));
    if (!ok1){ sock_->deleteLater(); sock_=nullptr; return false; }

    connect(sock_, &QUdpSocket::readyRead, this, &UdpDeviceManager::onReadyRead);

    // 2) 心跳口 8888
    sockHb_ = new QUdpSocket(this);
    sockHb_->setSocketOption(QUdpSocket::ReceiveBufferSizeSocketOption, 1<<20);
    bool ok2 = sockHb_->bind(QHostAddress::AnyIPv4, hbPort_,
                             QUdpSocket::DontShareAddress); // ← 同上
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

        emit logLine(QString("[HB-SRV] <-- RECV '%1' from %2:%3")
                         .arg(msg, peer.toString()).arg(peerPort));
        // 从 msg 里提取 sn=... 的值，然后 emit
        static const QRegularExpression kSnRe(R"(\bsn=([^\s]+))",
                                              QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch m = kSnRe.match(msg);
        if (m.hasMatch()) {
            const QString sn = m.captured(1);
            upsertDevice(sn, peer, peerPort);   // ← 补这一行，刷新 SN→IP
            emit snDiscoveredOrUpdated(sn);
        }

        if (msg.startsWith("HB_PING", Qt::CaseInsensitive)) {

            // （可选）只在同网段时才回 ACK；不同网段只记录不响应
            QHostAddress local = pickLocalIpSameSubnet(peer);
            QByteArray rep = "DISCOVER_REPLY ";
            if (local.isNull() || local.isLoopback()) {
                // 找不到同网段地址 -> 不写 host=（交给下位机用 peer）
                rep += "port=" + QByteArray::number(hbPort_);
            } else {
                // 再保险：确认 local 与 peer 同网段，才写 host=
                bool same = false;
                for (const auto& nic : QNetworkInterface::allInterfaces()){
                    if (!(nic.flags() & QNetworkInterface::IsUp) || !(nic.flags() & QNetworkInterface::IsRunning)) continue;
                    for (const auto& e : nic.addressEntries()){
                        if (e.ip() != local || e.ip().protocol()!=QAbstractSocket::IPv4Protocol) continue;
                        const auto mask = e.netmask().isNull()? 0u : e.netmask().toIPv4Address();
                        if (mask && ((local.toIPv4Address() & mask) == (peer.toIPv4Address() & mask))) { same = true; break; }
                    }
                    if (same) break;
                }
                if (same) {
                    rep += "host=" + local.toString().toUtf8()
                           + " port=" + QByteArray::number(hbPort_);
                } else {
                    // 不同网段也不要写 host=，避免回 26.26.26.1 这种错误地址
                    rep += "port=" + QByteArray::number(hbPort_);
                }
            }

            const qint64 n = sock_->writeDatagram(rep, peer, listenPort_);

            emit logLine(QString("[HB-SRV] --> ACK %1 bytes to %2:%3")
                             .arg(n).arg(peer.toString()).arg(peerPort));
        }
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
           QHostAddress local = pickLocalIpSameSubnet(peer);
            if (local == QHostAddress::LocalHost || local.isNull() || local.isLoopback()) {
                local = peer;
            }

            // 用 local 构造 host= 字段
            const QByteArray rep = QByteArray("DISCOVER_REPLY host=")
                                   + local.toString().toUtf8()
                                   + " port=" + QByteArray::number(hbPort_); // 建议用心跳端口
            emit logLine(QString("[UDP-Mgr] --> DISCOVER_REPLY '%1' to %2:%3 via %4")
                             .arg(QString::fromUtf8(rep), peer.toString()).arg(listenPort_).arg(local.toString()));


            // ★★ 关键：回到固定的发现端口，而不是 peer 的临时端口 ★★
            const qint64 n = sock_->writeDatagram(rep, peer, listenPort_);  // ← 改这里

            emit logLine(QString("[UDP-Mgr] --> DISCOVER_REPLY (%1 bytes) to %2:%3 via %4")
                             .arg(n).arg(peer.toString()).arg(listenPort_).arg(local.toString()));
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

    // 🔥 打印一条映射日志，方便你确认抓到的 IP 是多少
    emit logLine(QString("[UDP-Mgr] device map: SN=%1 -> %2:%3")
                     .arg(sn, ip.toString()).arg(srcPort));
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

// static void enableBroadcast(QUdpSocket* sock){
//     if (!sock) return;
//     qintptr fd = sock->socketDescriptor();
//     if (fd < 0) return;

// #if defined(Q_OS_WIN)
//     // WinSock 需要 const char*；用 BOOL 也行，但要强转为 const char*
//     BOOL opt = TRUE;
//     ::setsockopt((SOCKET)fd, SOL_SOCKET, SO_BROADCAST,
//                  reinterpret_cast<const char*>(&opt), sizeof(opt));
// #else
//     int opt = 1;
//     ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
// #endif
// }
qint64 UdpDeviceManager::sendSetIp(const QString& sn, const QString& ip, int mask, const QString& iface)
{
    QByteArray payload = "CMD_SET_IP sn=" + sn.toUtf8()
                         + " ip=" + ip.toUtf8()
                         + " mask=" + QByteArray::number(mask);
    if (!iface.isEmpty())
        payload += " iface=" + iface.toUtf8();

    // 1) 先从设备表里找到这个 SN 对应的当前 IP
    DeviceInfo dev;
    if (!getDevice(sn, dev)) {
        emit logLine(QString("[UDP-Mgr] setip fail: SN '%1' not found in devices_").arg(sn));
        return -1;
    }

    if (!sock_) {
        emit logLine("[UDP-Mgr] setip fail: DISC socket not started");
        return -2;
    }

    // 2) 直接用发现口的 socket（已绑定在 0.0.0.0:7777）
    //    单播到“下位机当前 IP:发现端口(7777)”
    const QHostAddress dstIp   = dev.ip;
    const quint16      dstPort = listenPort_;   // == 7777

    const qint64 n = sock_->writeDatagram(payload, dstIp, dstPort);

    emit logLine(QString("[UDP-Mgr] CMD_SET_IP(unicast via DISC) SN=%1 -> %2:%3 bytes=%4")
                     .arg(sn, dstIp.toString())
                     .arg(dstPort)
                     .arg(static_cast<qlonglong>(n)));

    return n;
}




