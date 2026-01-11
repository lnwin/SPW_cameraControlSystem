// udpserver.cpp (FULL REPLACEABLE)

#include "udpserver.h"

#include <QCoreApplication>
#include <QDebug>

#ifdef Q_OS_WIN
#include <winsock2.h>              // must be before <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

// -------------------- parsers --------------------
static QString parseRtspPath(const QString& msg){
    static QRegularExpression re(R"(rtsp_path\s*=\s*([^\s]+))",
                                 QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(msg);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

static int parseRtspPort(const QString& msg){
    static QRegularExpression re(R"(rtsp_port\s*=\s*(\d+))",
                                 QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(msg);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

static QRegularExpression kReSn(R"(sn\s*=\s*([A-Za-z0-9\-_]+))",
                                QRegularExpression::CaseInsensitiveOption);

// -------------------- ctor/dtor --------------------
UdpDeviceManager::UdpDeviceManager(QObject* parent)
    : QObject(parent)
{
}

// -------------------- start/stop --------------------
bool UdpDeviceManager::start(quint16 discoverPort, quint16 heartbeatPort)
{
    stop();

    listenPort_ = discoverPort;
    hbPort_     = heartbeatPort;

    // DISC socket
    sock_ = new QUdpSocket(this);
    sock_->setSocketOption(QUdpSocket::ReceiveBufferSizeSocketOption, 1<<20);

    const bool ok1 = sock_->bind(QHostAddress::AnyIPv4,
                                 listenPort_,
                                 QUdpSocket::DontShareAddress);
    emit logLine(QString("[UDP-Mgr] bind DISC 0.0.0.0:%1 %2")
                     .arg(listenPort_)
                     .arg(ok1 ? "OK" : "FAIL"));

    if (!ok1) {
        sock_->deleteLater();
        sock_ = nullptr;
        return false;
    }

    connect(sock_, &QUdpSocket::readyRead, this, &UdpDeviceManager::onReadyRead);

    // HB socket
    sockHb_ = new QUdpSocket(this);
    sockHb_->setSocketOption(QUdpSocket::ReceiveBufferSizeSocketOption, 1<<20);

    const bool ok2 = sockHb_->bind(QHostAddress::AnyIPv4,
                                   hbPort_,
                                   QUdpSocket::DontShareAddress);
    emit logLine(QString("[UDP-Mgr] bind HB   0.0.0.0:%1 %2")
                     .arg(hbPort_)
                     .arg(ok2 ? "OK" : "FAIL"));

    if (!ok2) {
        disconnect(sock_, nullptr, this, nullptr);
        sock_->close();
        sock_->deleteLater();
        sock_ = nullptr;

        sockHb_->deleteLater();
        sockHb_ = nullptr;
        return false;
    }

    connect(sockHb_, &QUdpSocket::readyRead, this, &UdpDeviceManager::onReadyReadHb);

    return true;
}

void UdpDeviceManager::stop()
{
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

// -------------------- HB read --------------------
void UdpDeviceManager::onReadyReadHb()
{
    while (sockHb_ && sockHb_->hasPendingDatagrams()) {

        QHostAddress peer;
        quint16 peerPort = 0;

        QByteArray buf;
        buf.resize(int(sockHb_->pendingDatagramSize()));
        sockHb_->readDatagram(buf.data(), buf.size(), &peer, &peerPort);

        const QString msg = QString::fromUtf8(buf).trimmed();
        emit logLine(QString("[HB-SRV] <-- RECV '%1' from %2:%3")
                         .arg(msg, peer.toString()).arg(peerPort));

        // parse sn + upsert (msg may contain rtsp_port/rtsp_path)
        static const QRegularExpression kSnRe(R"(\bsn=([^\s]+))",
                                              QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = kSnRe.match(msg);
        if (m.hasMatch()) {
            const QString sn = m.captured(1).trimmed();
            if (!sn.isEmpty()) {
                upsertDevice(sn, peer, peerPort, msg);
                emit snDiscoveredOrUpdated(sn);
            }
        }

        // optional ACK for HB_PING
        if (msg.startsWith("HB_PING", Qt::CaseInsensitive)) {

            if (!sock_) {
                emit logLine("[HB-SRV] DISC socket not ready, skip ACK");
                continue;
            }

            QHostAddress local = pickLocalIpSameSubnet(peer);

            QByteArray rep = "DISCOVER_REPLY ";
            if (local.isNull() || local.isLoopback()) {
                rep += "port=" + QByteArray::number(hbPort_);
            } else {
                bool same = false;
                for (const auto& nic : QNetworkInterface::allInterfaces()) {
                    if (!(nic.flags() & QNetworkInterface::IsUp) ||
                        !(nic.flags() & QNetworkInterface::IsRunning)) continue;

                    for (const auto& e : nic.addressEntries()) {
                        if (e.ip() != local || e.ip().protocol()!=QAbstractSocket::IPv4Protocol) continue;
                        const auto mask = e.netmask().isNull()? 0u : e.netmask().toIPv4Address();
                        if (mask && ((local.toIPv4Address() & mask) == (peer.toIPv4Address() & mask))) {
                            same = true; break;
                        }
                    }
                    if (same) break;
                }
                if (same) {
                    rep += "host=" + local.toString().toUtf8()
                           +  " port=" + QByteArray::number(hbPort_);
                } else {
                    rep += "port=" + QByteArray::number(hbPort_);
                }
            }

            const qint64 n = sock_->writeDatagram(rep, peer, listenPort_);
            emit logLine(QString("[HB-SRV] --> ACK %1 bytes to %2:%3")
                             .arg(n).arg(peer.toString()).arg(peerPort));
        }
    }
}

// -------------------- DISC read --------------------
void UdpDeviceManager::onReadyRead()
{
    while (sock_ && sock_->hasPendingDatagrams()) {

        QHostAddress peer;
        quint16 peerPort = 0;

        QByteArray buf;
        buf.resize(int(sock_->pendingDatagramSize()));
        sock_->readDatagram(buf.data(), buf.size(), &peer, &peerPort);

        const QString msg = QString::fromUtf8(buf).trimmed();

        emit logLine(QString("[UDP-Mgr] <-- RECV '%1' from %2:%3")
                         .arg(msg, peer.toString()).arg(peerPort));

        // ① discover request -> reply
        if (msg.startsWith("DISCOVER_REQUEST", Qt::CaseInsensitive)) {

            QHostAddress local = pickLocalIpSameSubnet(peer);
            if (local == QHostAddress::LocalHost || local.isNull() || local.isLoopback())
                local = peer;

            const QByteArray rep =
                QByteArray("DISCOVER_REPLY host=")
                + local.toString().toUtf8()
                + " port=" + QByteArray::number(hbPort_);

            emit logLine(QString("[UDP-Mgr] --> DISCOVER_REPLY '%1' to %2:%3 via %4")
                             .arg(QString::fromUtf8(rep), peer.toString())
                             .arg(listenPort_)
                             .arg(local.toString()));

            // critical: reply to fixed discover port
            const qint64 n = sock_->writeDatagram(rep, peer, listenPort_);

            emit logLine(QString("[UDP-Mgr] --> DISCOVER_REPLY (%1 bytes) to %2:%3 via %4")
                             .arg(n)
                             .arg(peer.toString())
                             .arg(listenPort_)
                             .arg(local.toString()));
        }

        // ② parse SN from any msg (DISCOVER_REQUEST/HB etc)
        const QString sn = parseSn(msg);
        if (!sn.isEmpty()) {
            upsertDevice(sn, peer, peerPort, msg);
            emit snDiscoveredOrUpdated(sn);
        }

        emit datagramReceived(sn, peer, peerPort, buf);
    }
}

// -------------------- device map --------------------
void UdpDeviceManager::upsertDevice(const QString& sn,
                                    const QHostAddress& ip,
                                    quint16 srcPort,
                                    const QString& rawMsg)
{
    QString log;

    {
        QMutexLocker lk(&mtx_);
        auto& d = devices_[sn];

        d.sn = sn;
        d.ip = ip;
        d.lastPort = srcPort;
        d.lastSeenMs = QDateTime::currentMSecsSinceEpoch();

        // parse rtsp endpoint if present
        const int rp = parseRtspPort(rawMsg);
        const QString rpath = parseRtspPath(rawMsg);

        if (rp > 0 && rp <= 65535) d.rtspPort = (quint16)rp;
        if (!rpath.isEmpty()) d.rtspPath = rpath;

        log = QString("[UDP-Mgr] device map: SN=%1 -> %2:%3 rtsp=%4%5")
                  .arg(sn)
                  .arg(ip.toString()).arg(srcPort)
                  .arg(d.rtspPort)
                  .arg(d.rtspPath);
    }

    // emit OUTSIDE lock (avoid deadlock)
    emit logLine(log);
}

QString UdpDeviceManager::parseSn(const QString& msg) const
{
    const QRegularExpressionMatch m = kReSn.match(msg);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

// -------------------- choose local ip --------------------
QHostAddress UdpDeviceManager::pickLocalIpSameSubnet(const QHostAddress& peer) const
{
    if (peer.protocol() != QAbstractSocket::IPv4Protocol) return QHostAddress::LocalHost;
    const quint32 peerIp = peer.toIPv4Address();

    // prefer same-subnet IPv4
    for (const auto& nic : QNetworkInterface::allInterfaces()) {
        if (!(nic.flags() & QNetworkInterface::IsUp) ||
            !(nic.flags() & QNetworkInterface::IsRunning))
            continue;

        for (const auto& e : nic.addressEntries()) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            const quint32 ip   = e.ip().toIPv4Address();
            const quint32 mask = e.netmask().isNull() ? 0u : e.netmask().toIPv4Address();
            if (mask && ((ip & mask) == (peerIp & mask)))
                return e.ip();
        }
    }

    // fallback: first non-loopback IPv4
    for (const auto& nic : QNetworkInterface::allInterfaces()) {
        for (const auto& e : nic.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol &&
                e.ip() != QHostAddress::LocalHost)
                return e.ip();
        }
    }
    return QHostAddress::LocalHost;
}

// -------------------- send --------------------
qint64 UdpDeviceManager::sendCommandToSn(const QString& sn, const QByteArray& payload, quint16 port)
{
    QHostAddress ip;
    quint16 lastPort = 0;

    {
        QMutexLocker lk(&mtx_);
        auto it = devices_.find(sn);
        if (it == devices_.end()) {
            emit logLine(QString("[UDP-Mgr] send failed: SN=%1 not found").arg(sn));
            return -1;
        }
        ip = it->ip;
        lastPort = it->lastPort;
    }

    if (!sock_) {
        emit logLine("[UDP-Mgr] send failed: socket not started");
        return -2;
    }

    const quint16 dstPort = port ? port : (lastPort ? lastPort : defaultCmdPort_);
    const qint64 n = sock_->writeDatagram(payload, ip, dstPort);

    emit logLine(QString("[UDP-Mgr] send %1 bytes to %2:%3 (SN=%4)")
                     .arg(n).arg(ip.toString()).arg(dstPort).arg(sn));
    return n;
}

qint64 UdpDeviceManager::broadcastCommand(const QByteArray& payload, quint16 port)
{
    if (!sock_) return -1;
    const quint16 p = port ? port : defaultCmdPort_;
    return sock_->writeDatagram(payload, QHostAddress("255.255.255.255"), p);
}

QList<QString> UdpDeviceManager::allSns() const
{
    QMutexLocker lk(&mtx_);
    return devices_.keys();
}

bool UdpDeviceManager::getDevice(const QString& sn, DeviceInfo& out) const
{
    QMutexLocker lk(&mtx_);
    auto it = devices_.find(sn);
    if (it == devices_.end()) return false;
    out = it.value();
    return true;
}

// -------------------- commands --------------------
qint64 UdpDeviceManager::sendSetIp(const QString& sn, const QString& ip, int mask, const QString& iface)
{
    QByteArray payload = "CMD_SET_IP sn=" + sn.toUtf8()
                         + " ip=" + ip.toUtf8()
                         + " mask=" + QByteArray::number(mask);
    if (!iface.isEmpty())
        payload += " iface=" + iface.toUtf8();

    DeviceInfo dev;
    if (!getDevice(sn, dev)) {
        emit logLine(QString("[UDP-Mgr] setip fail: SN '%1' not found in devices_").arg(sn));
        return -1;
    }
    if (!sock_) {
        emit logLine("[UDP-Mgr] setip fail: DISC socket not started");
        return -2;
    }

    const QHostAddress dstIp   = dev.ip;
    const quint16      dstPort = listenPort_; // 7777

    const qint64 n = sock_->writeDatagram(payload, dstIp, dstPort);

    emit logLine(QString("[UDP-Mgr] CMD_SET_IP(unicast via DISC) SN=%1 -> %2:%3 bytes=%4")
                     .arg(sn, dstIp.toString())
                     .arg(dstPort)
                     .arg(static_cast<qlonglong>(n)));
    return n;
}

qint64 UdpDeviceManager::sendSetCameraParams(const QString& sn, int exposureUs, double gainDb)
{
    QByteArray payload = "CMD_SET_CAMERA sn=" + sn.toUtf8()
                         + " exposure_us=" + QByteArray::number(exposureUs)
                         + " gain_db=" + QByteArray::number(gainDb, 'f', 2);

    DeviceInfo dev;
    if (!getDevice(sn, dev)) {
        emit logLine(QString("[UDP-Mgr] set-camera fail: SN '%1' not found in devices_").arg(sn));
        return -1;
    }
    if (!sock_) {
        emit logLine("[UDP-Mgr] set-camera fail: DISC socket not started");
        return -2;
    }

    const QHostAddress dstIp   = dev.ip;
    const quint16      dstPort = listenPort_;

    const qint64 n = sock_->writeDatagram(payload, dstIp, dstPort);

    emit logLine(QString("[UDP-Mgr] CMD_SET_CAMERA SN=%1 exposure_us=%2 gain_db=%3 bytes=%4 -> %5:%6")
                     .arg(sn)
                     .arg(exposureUs)
                     .arg(gainDb, 0, 'f', 2)
                     .arg(static_cast<qlonglong>(n))
                     .arg(dstIp.toString())
                     .arg(dstPort));
    return n;
}
