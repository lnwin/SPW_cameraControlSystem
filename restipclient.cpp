#include "restipclient.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QTcpSocket>

RestIpClient::RestIpClient(QObject* parent) : QObject(parent) {}

bool RestIpClient::setIpBlocking(const QString& host, quint16 port,
                                 const QString& token, const SetIpArgs& a,
                                 QJsonObject* respJson, int httpTimeoutMs)
{
    // 1) 组装 URL 与 JSON
    const QUrl url(QStringLiteral("http://%1:%2").arg(host).arg(port));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject j{
        {"token",    token},
        {"ifname",   a.ifname},
        {"ip",       a.ip},
        {"prefix",   a.prefix},
        {"gateway",  a.gateway},
        {"old_cidr", a.oldCidr}
    };
    const QByteArray body = QJsonDocument(j).toJson(QJsonDocument::Compact);

    // 2) 发 POST + 同步等待（带超时）
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QNetworkReply* rep = nam_.post(req, body);
    bool timedOut = false, finished = false;

    QObject::connect(&timer, &QTimer::timeout, &loop, [&]{
        timedOut = true;
        if (rep) rep->abort();
        loop.quit();
    });
    QObject::connect(rep, &QNetworkReply::finished, &loop, [&]{
        finished = true;
        loop.quit();
    });

    timer.start(httpTimeoutMs);
    loop.exec();

    // 3) 结果处理
    if (!finished && timedOut) {
        if (rep) rep->deleteLater();
        return false; // 超时
    }
    if (rep->error() != QNetworkReply::NoError) {
        rep->deleteLater();
        return false; // 网络/HTTP 层错误
    }

    const int httpStatus = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = rep->readAll();
    rep->deleteLater();

    if (httpStatus != 200) {
        return false;
    }

    // 4) JSON 解包
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    if (respJson) *respJson = doc.object();

    // 约定：服务端 {"ok":true} 视为成功
    const bool ok = doc.object().value("ok").toBool(false);
    return ok;
}

bool RestIpClient::probeAgentReachable(const QString& host, quint16 port, int timeoutMs)
{
    QTcpSocket s;
    s.connectToHost(host, port);
    return s.waitForConnected(timeoutMs);
}
