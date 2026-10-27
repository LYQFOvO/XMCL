#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QTimer>
#include <memory>
// 本机 HTTP 夹具：完整模拟微软设备码 -> Xbox Live -> XSTS -> Minecraft 授权链。
// 与生产代码走同一套请求与账号保存路径，不访问公网。
class TestMicrosoftServer : public QTcpServer {
public:
    QByteArray msAccessToken = "ms-access-token";
    QByteArray msRefreshToken = "ms-refresh-token";
    QByteArray mcAccessToken = "minecraft-access-token";
    QByteArray profileId = "0123456789abcdef0123456789abcdef";
    QByteArray playerName = "TestPlayer";
    int pendingPolls = 1;
    QJsonObject deviceCodeError;
    int tokenRequests = 0;
    int deviceCodeRequests = 0;
    QStringList paths;
    explicit TestMicrosoftServer(QObject *parent = nullptr) : QTcpServer(parent) {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto *socket = nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                buffer->append(socket->readAll());
                if (!buffer->contains("\r\n\r\n")) return;
                const auto requestLine = buffer->left(buffer->indexOf("\r\n")).split(' ');
                const auto method = requestLine.value(0);
                const auto path = QString::fromLatin1(requestLine.value(1)).section('?', 0, 0);
                buffer->clear();
                paths.append(QString::fromLatin1(method) + ' ' + path);
                int status = 200;
                QByteArray body;
                const auto respond = [this, socket](int code, const QByteArray &payload) {
                    socket->write("HTTP/1.1 " + QByteArray::number(code) + " Test\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload);
                    socket->disconnectFromHost();
                };
                if (path.endsWith("/devicecode")) {
                    ++deviceCodeRequests;
                    if (!deviceCodeError.isEmpty()) { status = 400; body = QJsonDocument(deviceCodeError).toJson(QJsonDocument::Compact); }
                    else body = QJsonDocument(QJsonObject{
                        {"device_code", "device-code-fixture"},
                        {"user_code", "TESTCODE"},
                        {"verification_uri", "https://www.microsoft.com/link"},
                        {"expires_in", 900},
                        {"interval", 1}}).toJson(QJsonDocument::Compact);
                } else if (path.endsWith("/token")) {
                    ++tokenRequests;
                    if (tokenRequests <= pendingPolls) {
                        status = 400;
                        body = QJsonDocument(QJsonObject{{"error", "authorization_pending"}}).toJson(QJsonDocument::Compact);
                    } else {
                        body = QJsonDocument(QJsonObject{
                            {"access_token", QString::fromLatin1(msAccessToken)},
                            {"refresh_token", QString::fromLatin1(msRefreshToken)},
                            {"expires_in", 3600}}).toJson(QJsonDocument::Compact);
                    }
                } else if (path == "/xbox/user/authenticate") {
                    body = QJsonDocument(QJsonObject{{"Token", "xbl-token"}, {"DisplayClaims", QJsonObject{{"xui", QJsonArray{QJsonObject{{"uhs", "uhs-1"}}}}}}}).toJson(QJsonDocument::Compact);
                } else if (path == "/xbox/xsts/authorize") {
                    body = QJsonDocument(QJsonObject{{"Token", "xsts-token"}, {"DisplayClaims", QJsonObject{{"xui", QJsonArray{QJsonObject{{"uhs", "uhs-1"}}}}}}}).toJson(QJsonDocument::Compact);
                } else if (path == "/minecraft/login") {
                    body = QJsonDocument(QJsonObject{{"access_token", QString::fromLatin1(mcAccessToken)}, {"expires_in", 86400}}).toJson(QJsonDocument::Compact);
                } else if (path == "/minecraft/entitlements") {
                    body = QJsonDocument(QJsonObject{
                        {"items", QJsonArray{QJsonObject{{"name", "product_minecraft"}, {"signature", "fixture"}}}}}).toJson(QJsonDocument::Compact);
                } else if (path == "/minecraft/profile") {
                    body = QJsonDocument(QJsonObject{{"id", QString::fromLatin1(profileId)}, {"name", QString::fromLatin1(playerName)}}).toJson(QJsonDocument::Compact);
                } else {
                    status = 404;
                    body = R"({"error":"not_found"})";
                }
                QTimer::singleShot(0, socket, [respond, status, body] { respond(status, body); });
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    }
    QUrl url(const QString &path) const { return QUrl(QString("http://127.0.0.1:%1%2").arg(serverPort()).arg(path)); }
};
