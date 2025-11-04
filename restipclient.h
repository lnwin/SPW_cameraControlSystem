#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>

struct SetIpArgs {
    QString ifname;          // 例如 "enp3s0"
    QString ip;              // "192.168.194.54"
    int     prefix = 24;     // 24
    QString gateway;         // "192.168.194.1"
    QString oldCidr;         // "192.168.194.77/24" (可空)
};

class RestIpClient : public QObject {
    Q_OBJECT
public:
    explicit RestIpClient(QObject* parent=nullptr);

    // 同步调用：返回 true 表示 200 且 JSON ok；respJson 带回服务端回包
    // host 可填 "192.168.194.77"，port 默认 8088；token 要和代理一致
    bool setIpBlocking(const QString& host, quint16 port,
                       const QString& token, const SetIpArgs& args,
                       QJsonObject* respJson = nullptr,
                       int httpTimeoutMs = 8000);

    // （可选）在变更后，尝试连接新 IP 的 8088 端口，确认服务可达
    bool probeAgentReachable(const QString& host, quint16 port, int timeoutMs = 1500);

private:
    QNetworkAccessManager nam_;
};
