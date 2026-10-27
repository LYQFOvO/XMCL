#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QTimer>
// 本机 HTTP 夹具：模拟 Modrinth 搜索、项目文件列表、版本元数据与 CDN 下载，
// 让社区资源下载从生产界面走完整的“搜索 -> 选项目 -> 选文件 -> 校验并安装”路径。
class TestCommunityServer : public QTcpServer {
public:
    QByteArray modFile = "fixture mod jar bytes";
    QString projectId = "demo-mod";
    QString versionId = "demo-v1";
    QString fileName = "demo-mod.jar";
    int searchRequests = 0;
    explicit TestCommunityServer(QObject *parent = nullptr) : QTcpServer(parent) {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto *socket = nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                buffer->append(socket->readAll());
                if (!buffer->contains("\r\n\r\n")) return;
                const auto requestLine = buffer->left(buffer->indexOf("\r\n")).split(' ');
                const auto path = QString::fromLatin1(requestLine.value(1)).section('?', 0, 0);
                buffer->clear();
                int status = 200;
                QByteArray body;
                QByteArray contentType = "application/json";
                if (path == "/v2/search") {
                    ++searchRequests;
                    const QJsonObject hit{{"project_id", projectId}, {"title", "Demo Mod"},
                                          {"description", "A fixture mod used by the native download test."}};
                    body = QJsonDocument(QJsonObject{{"hits", QJsonArray{hit}}}).toJson(QJsonDocument::Compact);
                } else if (path == QString("/v2/project/%1/version").arg(projectId)) {
                    body = QJsonDocument(QJsonArray{versionObject()}).toJson(QJsonDocument::Compact);
                } else if (path == QString("/v2/version/%1").arg(versionId)) {
                    body = QJsonDocument(versionObject()).toJson(QJsonDocument::Compact);
                } else if (path == "/files/" + fileName) {
                    contentType = "application/java-archive";
                    body = modFile;
                } else {
                    status = 404;
                    body = R"({"error":"not_found"})";
                }
                QTimer::singleShot(0, socket, [socket, status, body, contentType] {
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\nContent-Type: " + contentType
                                  + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    }
    QJsonObject versionObject() const {
        const QJsonObject file{{"primary", true}, {"filename", fileName},
                               {"url", url("/files/" + fileName).toString()},
                               {"hashes", QJsonObject{{"sha512", QString::fromLatin1(QCryptographicHash::hash(modFile, QCryptographicHash::Sha512).toHex())}}}};
        return QJsonObject{{"id", versionId}, {"project_id", projectId}, {"name", "Demo Mod " + versionId},
                           {"date_published", "2026-10-01T00:00:00Z"}, {"game_versions", QJsonArray{"1.21.1"}},
                           {"loaders", QJsonArray{"fabric"}}, {"dependencies", QJsonArray{}},
                           {"files", QJsonArray{file}}};
    }
    QUrl url(const QString &path) const { return QUrl(QString("http://127.0.0.1:%1%2").arg(serverPort()).arg(path)); }
};
