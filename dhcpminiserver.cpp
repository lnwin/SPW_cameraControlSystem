#include "dhcpminiserver.h"

DhcpMiniServer::DhcpMiniServer(QObject* parent) : QObject(parent) {
    connect(&m_gcTimer, &QTimer::timeout, this, &DhcpMiniServer::reapLeases);
    m_gcTimer.setInterval(5000); // 5 秒回收一次
}
DhcpMiniServer::~DhcpMiniServer() {
    stop();                               // ← 退出时必关
}
void DhcpMiniServer::setInterfaceIp(const QString& ip){ m_ifaceIp = QHostAddress(ip); if (!m_router.toIPv4Address()) m_router = m_ifaceIp; }
void DhcpMiniServer::setPool(const QString& s, const QString& e){ m_poolStart=QHostAddress(s); m_poolEnd=QHostAddress(e); }
void DhcpMiniServer::setMask(const QString& m){ m_mask=QHostAddress(m); }
void DhcpMiniServer::setRouter(const QString& r){ m_router=QHostAddress(r); }
void DhcpMiniServer::setLeaseSeconds(quint32 secs){ m_leaseSecs = secs; }
void DhcpMiniServer::setPreferMac(const QString& mac){ m_preferMac = strMacToBytes(mac); }

bool DhcpMiniServer::start(QString* errMsg){
    if (m_running) return true;
    if (m_sock) { m_sock->deleteLater(); m_sock=nullptr; }

    m_sock = new QUdpSocket(this);
    // 绑定前显式允许广播（有些环境需要）
    m_sock->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);

    if (!m_sock->bind(QHostAddress::AnyIPv4, 67,
                      QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        if (errMsg) *errMsg = m_sock->errorString();
        emit error(QString("Bind UDP/67 failed: %1").arg(m_sock->errorString()));
        m_sock->deleteLater(); m_sock=nullptr;
        return false;
    }
    connect(m_sock, &QUdpSocket::readyRead, this, &DhcpMiniServer::onReadyRead);
    m_gcTimer.start();
    m_running = true;
    emit log("DHCP server started");
    return true;
}

void DhcpMiniServer::stop(){
    if (!m_running && !m_sock) return;
    m_running = false;
    m_gcTimer.stop();
    if (m_sock) {
        // 断开信号，关闭并销毁 socket，释放 67 端口
        disconnect(m_sock, nullptr, this, nullptr);
        m_sock->close();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    m_leases.clear();
    emit log("DHCP server stopped and UDP/67 released");
}
void DhcpMiniServer::disableRouterOption(bool disable){
    m_disableRouterOpt = disable;
}

void DhcpMiniServer::onReadyRead(){

    while (m_sock && m_sock->hasPendingDatagrams()){
        QByteArray buf; buf.resize(int(m_sock->pendingDatagramSize()));
        QHostAddress peer; quint16 port=0;
        m_sock->readDatagram(buf.data(), buf.size(), &peer, &port);

        if (buf.size() < int(sizeof(DhcpMsg))) continue;
        const DhcpMsg* req = reinterpret_cast<const DhcpMsg*>(buf.constData());
        if (req->op != BOOTREQUEST) continue;
        if (qFromBigEndian(req->magic) != MAGIC) continue;

        QByteArray mac(reinterpret_cast<const char*>(req->chaddr), 6);
        if (!m_preferMac.isEmpty() && mac.left(6) != m_preferMac) {
            // 开了 MAC 白名单则忽略其他客户端
            continue;
        }

        int msgType = dhcpMessageType(buf);
        QHostAddress requested = optRequestedIp(buf);
        QHostAddress serverId  = optServerId(buf);

        QHostAddress yiaddr;
        if (msgType == DHCPDISCOVER) {
            yiaddr = assignFor(mac);
            if (!yiaddr.toIPv4Address()) { emit error("No free IP in pool"); continue; }
            QByteArray offer = buildReply(*req, yiaddr, DHCPOFFER);
            sendBroadcast(offer);
            emit log(QString("OFFER %1 to %2").arg(yiaddr.toString(), macToStr(mac)));
        } else if (msgType == DHCPREQUEST) {
            if (serverId.toIPv4Address() && serverId != m_ifaceIp) {
                // 客户端指向了别的 server
                continue;
            }
            yiaddr = requested.toIPv4Address() ? requested : assignFor(mac);
            if (!yiaddr.toIPv4Address()) { emit error("No free IP for REQUEST"); continue; }

            // 记录/续租
            Lease L; L.ip=yiaddr; L.mac=mac; L.expiry = QDateTime::currentSecsSinceEpoch() + m_leaseSecs;
            m_leases[macToStr(mac)] = L;

            QByteArray ack = buildReply(*req, yiaddr, DHCPACK);
            sendBroadcast(ack);
            emit log(QString("ACK %1 to %2 (lease %3s)").arg(yiaddr.toString(), macToStr(mac)).arg(m_leaseSecs));
        } else {
            // 其他类型按需扩展
        }
    }
}

void DhcpMiniServer::reapLeases(){
    qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it=m_leases.begin(); it!=m_leases.end(); ){
        if (it->expiry <= now) it = m_leases.erase(it);
        else ++it;
    }
}

// ===== 工具实现 =====
QString DhcpMiniServer::macToStr(const QByteArray& mac){
    QString s; for (int i=0;i<mac.size();++i){ if(i) s+=':'; s+=QString("%1").arg(quint8(mac[i]),2,16,QChar('0')); }
    return s;
}
QByteArray DhcpMiniServer::strMacToBytes(const QString& mac){
    QByteArray out; out.resize(6); out.fill('\0');
    auto parts = mac.split(':');
    for (int i=0;i<qMin(6, parts.size()); ++i) out[i] = char(parts[i].toUShort(nullptr,16));
    return out;
}
quint32 DhcpMiniServer::toBE32(const QHostAddress& ip){ return qToBigEndian((quint32)ip.toIPv4Address()); }
QHostAddress DhcpMiniServer::fromBE32(quint32 be){ return QHostAddress(qFromBigEndian(be)); }

int DhcpMiniServer::dhcpMessageType(const QByteArray& pkt) const {
    const uchar* p = reinterpret_cast<const uchar*>(pkt.constData());
    int i = sizeof(DhcpMsg);
    while (i < pkt.size()){
        if (p[i]==0xff) break;
        if (p[i]==0) { ++i; continue; }
        int code=p[i++], len = (i<pkt.size()? p[i++]:0);
        if (i+len > pkt.size()) break;
        if (code==53 && len==1) return p[i];
        i += len;
    }
    return 0;
}
QHostAddress DhcpMiniServer::optRequestedIp(const QByteArray& pkt) const {
    const uchar* p = reinterpret_cast<const uchar*>(pkt.constData());
    int i = sizeof(DhcpMsg);
    while (i < pkt.size()){
        if (p[i]==0xff) break;
        if (p[i]==0) { ++i; continue; }
        int code=p[i++], len = (i<pkt.size()? p[i++]:0);
        if (i+len > pkt.size()) break;
        if (code==50 && len==4){ quint32 v; memcpy(&v,p+i,4); return fromBE32(v); }
        i += len;
    }
    return QHostAddress();
}
QHostAddress DhcpMiniServer::optServerId(const QByteArray& pkt) const {
    const uchar* p = reinterpret_cast<const uchar*>(pkt.constData());
    int i = sizeof(DhcpMsg);
    while (i < pkt.size()){
        if (p[i]==0xff) break;
        if (p[i]==0) { ++i; continue; }
        int code=p[i++], len = (i<pkt.size()? p[i++]:0);
        if (i+len > pkt.size()) break;
        if (code==54 && len==4){ quint32 v; memcpy(&v,p+i,4); return fromBE32(v); }
        i += len;
    }
    return QHostAddress();
}

QHostAddress DhcpMiniServer::assignFor(const QByteArray& mac){
    QString key = macToStr(mac);
    if (m_leases.contains(key) && m_leases[key].expiry > QDateTime::currentSecsSinceEpoch())
        return m_leases[key].ip;

    quint32 s = m_poolStart.toIPv4Address();
    quint32 e = m_poolEnd.toIPv4Address();
    for (quint32 ip=s; ip<=e; ++ip){
        QHostAddress cand(ip);
        if (!ipTaken(cand)){
            Lease L; L.ip=cand; L.mac=mac; L.expiry = QDateTime::currentSecsSinceEpoch() + m_leaseSecs;
            m_leases[key] = L;
            return cand;
        }
    }
    return QHostAddress(); // 无空闲
}

bool DhcpMiniServer::ipTaken(const QHostAddress& ip) const {
    qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const auto& L : m_leases)
        if (L.ip == ip && L.expiry > now) return true;
    return false;
}

QByteArray DhcpMiniServer::buildReply(const DhcpMsg& in, const QHostAddress& yi, int type) const {
    QByteArray out; out.resize(sizeof(DhcpMsg));
    DhcpMsg* r = reinterpret_cast<DhcpMsg*>(out.data());
    memset(r, 0, sizeof(DhcpMsg));
    r->op=BOOTREPLY; r->htype=1; r->hlen=6;
    r->xid=in.xid; r->flags=in.flags;
    r->yiaddr = toBE32(yi);
    r->siaddr = toBE32(m_ifaceIp);
    memcpy(r->chaddr, in.chaddr, 16);
    r->magic = qToBigEndian(MAGIC);

    auto push = [&](uchar code, const QByteArray& v){ out.append(char(code)); out.append(char(v.size())); out.append(v); };
    auto u32  = [&](quint32 v){ QByteArray b(4,0); qToBigEndian(v, (uchar*)b.data()); return b; };

    // 53: Message Type
    push(53, QByteArray(1, char(type)));
    // 54: Server Identifier
    push(54, u32(m_ifaceIp.toIPv4Address()));
    // 1: Subnet Mask
    push(1,  u32(m_mask.toIPv4Address()));
    // 3: Router (Gateway)
    if (!m_disableRouterOpt && m_router.toIPv4Address())
        push(3,  u32(m_router.toIPv4Address()));

    // 51: Lease Time
    push(51, u32(m_leaseSecs));
    // 58 / 59: T1 / T2
    push(58, u32(m_leaseSecs/2));
    push(59, u32(m_leaseSecs*7/8));

    out.append(char(0xff)); // END
    return out;
}

void DhcpMiniServer::sendBroadcast(const QByteArray& payload){
    if (!m_sock) return;
    // 全局广播
    m_sock->writeDatagram(payload, QHostAddress::Broadcast, 68);
    // 子网广播
    quint32 ip = m_ifaceIp.toIPv4Address();
    quint32 mk = m_mask.toIPv4Address();
    if (ip && mk) {
        quint32 bcast = (ip & mk) | (~mk);
        QHostAddress subnetBcast(bcast);
        if (subnetBcast != QHostAddress::Broadcast)
            m_sock->writeDatagram(payload, subnetBcast, 68);
    }
}

