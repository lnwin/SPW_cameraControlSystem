#pragma once
#include <QtCore>
#include <QtNetwork>

class DhcpMiniServer : public QObject {
    Q_OBJECT
public:
    explicit DhcpMiniServer(QObject* parent=nullptr);
   ~DhcpMiniServer() override;          // ← 新增
    // 基本配置（在 start() 之前设置）
    void setInterfaceIp(const QString& ip);                          // 例如 "192.168.194.77"
    void setPool(const QString& startIp, const QString& endIp);      // 例如 "192.168.194.100" ~ "192.168.194.150"
    void setMask(const QString& maskIp);                             // 例如 "255.255.255.0"
    void setRouter(const QString& routerIp);                         // 通常与 interfaceIp 一样
    void setLeaseSeconds(quint32 secs);                              // 例如 3600
    void setPreferMac(const QString& mac);                           // 可选：只服务指定 MAC（aa:bb:cc:dd:ee:ff）

    bool start(QString* errMsg=nullptr);   // 绑定 0.0.0.0:67，返回是否成功
    void stop();

    bool isRunning() const { return m_running; }
    void disableRouterOption(bool disable = true); // 新增
signals:
    void log(const QString& line);
    void error(const QString& line);

private slots:
    void onReadyRead();
    void reapLeases();  // 定期回收过期租约（单线程定时器）

private:

    bool m_disableRouterOpt = false;               // 新增成员变量


// === DHCP/BOOTP 报文 ===
#pragma pack(push, 1)
    struct DhcpMsg {
        quint8  op; quint8 htype; quint8 hlen; quint8 hops;
        quint32 xid; quint16 secs; quint16 flags;
        quint32 ciaddr, yiaddr, siaddr, giaddr;
        quint8  chaddr[16];
        quint8  sname[64];
        quint8  file[128];
        quint32 magic; // 0x63825363
        // 后面紧跟 options
    };
#pragma pack(pop)

    static constexpr quint32 MAGIC = 0x63825363;
    enum { BOOTREQUEST=1, BOOTREPLY=2 };
    enum { DHCPDISCOVER=1, DHCPOFFER=2, DHCPREQUEST=3, DHCPDECLINE=4, DHCPACK=5, DHCPNAK=6 };

    struct Lease {
        QHostAddress ip;
        QByteArray   mac;     // 6 字节
        qint64       expiry;  // 过期时间（unix 秒）
    };

    // === 辅助 ===
    static QString macToStr(const QByteArray& mac);
    static QByteArray strMacToBytes(const QString& mac); // "aa:bb:..." → 6字节
    static quint32 toBE32(const QHostAddress& ip);
    static QHostAddress fromBE32(quint32 be);

    int  dhcpMessageType(const QByteArray& pkt) const;
    QHostAddress optRequestedIp(const QByteArray& pkt) const;
    QHostAddress optServerId(const QByteArray& pkt) const;

    QHostAddress assignFor(const QByteArray& mac); // 找已有租约或分配新 IP
    bool ipTaken(const QHostAddress& ip) const;

    QByteArray buildReply(const DhcpMsg& req, const QHostAddress& yiaddr, int type) const;
    void       sendBroadcast(const QByteArray& payload);

    // === 成员变量 ===
    QUdpSocket*   m_sock = nullptr;
    QTimer        m_gcTimer; // 回收租约
    bool          m_running = false;

    QHostAddress  m_ifaceIp = QHostAddress("192.168.194.77");
    QHostAddress  m_poolStart = QHostAddress("192.168.194.100");
    QHostAddress  m_poolEnd   = QHostAddress("192.168.194.150");
    QHostAddress  m_mask      = QHostAddress("255.255.255.0");
    QHostAddress  m_router    = QHostAddress("192.168.194.77");
    quint32       m_leaseSecs = 3600;
    QByteArray    m_preferMac;  // 若设置，则只响应该 MAC

    QMap<QString, Lease> m_leases; // key=MAC 字符串
};
