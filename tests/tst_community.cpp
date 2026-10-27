#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "services/CommunityService.h"
#include "services/InstanceService.h"
#include "services/ServiceUtils.h"
namespace {
// 极简 Modrinth 夹具：版本元数据 + CDN 文件，可切换文件下载失败以验证重试。
class ModrinthFixture : public QTcpServer {
public:
    QByteArray fileBytes = "community fixture mod";
    bool failFile = false;
    int fileRequests = 0;
    explicit ModrinthFixture(QObject *parent = nullptr) : QTcpServer(parent) {
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
                if (path == "/v2/version/version-1") {
                    const QJsonObject file{{"primary", true}, {"filename", "retry-mod.jar"},
                                           {"url", url("/files/retry-mod.jar").toString()},
                                           {"hashes", QJsonObject{{"sha512", QString::fromLatin1(QCryptographicHash::hash(fileBytes, QCryptographicHash::Sha512).toHex())}}}};
                    const QJsonObject version{{"id", "version-1"}, {"project_id", "retry-project"},
                                              {"game_versions", QJsonArray{"1.21.1"}}, {"loaders", QJsonArray{"fabric"}},
                                              {"dependencies", QJsonArray{}}, {"files", QJsonArray{file}}};
                    body = QJsonDocument(version).toJson(QJsonDocument::Compact);
                } else if (path == "/files/retry-mod.jar") {
                    ++fileRequests;
                    if (failFile) { status = 503; body = "temporarily unavailable"; }
                    else body = fileBytes;
                } else {
                    status = 404;
                    body = R"({"error":"not_found"})";
                }
                QTimer::singleShot(0, socket, [socket, status, body] {
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    }
    QUrl url(const QString &path) const { return QUrl(QString("http://127.0.0.1:%1%2").arg(serverPort()).arg(path)); }
};
}
class CommunityTest:public QObject{
    Q_OBJECT
private slots:
void cancelDuringMetadataNeverDownloads(){
    QTemporaryDir dir;InstanceService instances(dir.path());QSignalSpy created(&instances,&FeatureService::completed);instances.execute("instance.create",{{"name","Test"},{"version","1.21.1"},{"loader","fabric"}});auto id=created.first().at(1).toMap().value("id").toString();
    QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));int requests=0;QTcpSocket *held=nullptr;
    connect(&server,&QTcpServer::newConnection,&server,[&]{auto*s=server.nextPendingConnection();connect(s,&QTcpSocket::readyRead,s,[&,s]{s->readAll();++requests;held=s;});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);});
    CommunityService community(dir.path(),nullptr,QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort())));QSignalSpy ok(&community,&FeatureService::completed),bad(&community,&FeatureService::failed);
    QVariantMap args{{"instanceId",id},{"versionId","abc"},{"kind","mods"}};community.execute("community.install",args);QTRY_COMPARE(requests,1);
    community.execute("community.install",args);QCOMPARE(bad.size(),1);community.execute("community.install",args);QCOMPARE(bad.size(),2); // A rejected second command must not release the first operation's lock.
    community.execute("community.cancel",{});QCOMPARE(ok.size(),1);QVERIFY(held);
    QByteArray body=R"({"id":"abc","project_id":"project","game_versions":["1.21.1"],"loaders":["fabric"],"files":[{"primary":true,"filename":"mod.jar","url":"https://cdn.modrinth.com/test","hashes":{"sha512":"HASH"}}],"dependencies":[]})";body.replace("HASH",QByteArray(128,'0'));
    held->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);held->disconnectFromHost();QTest::qWait(150);
    QVERIFY(!community.downloadState().value("active").toBool());QCOMPARE(community.downloadState().value("totalFiles").toInt(),0);QCOMPARE(ok.size(),1);QVERIFY(!QFile::exists(instances.gamePath(id)+"/mods/mod.jar"));
}
void liveModrinthDownload(){
    if(qEnvironmentVariableIsEmpty("XMCL_LIVE"))QSKIP("live Modrinth probe disabled; set XMCL_LIVE=1");
    QTemporaryDir dir;InstanceService instances(dir.path());QSignalSpy created(&instances,&FeatureService::completed);instances.execute("instance.create",{{"name","Live"},{"version","1.21.1"},{"loader","fabric"}});const auto id=created.first().at(1).toMap().value("id").toString();
    CommunityService community(dir.path());QSignalSpy ok(&community,&FeatureService::completed),bad(&community,&FeatureService::failed);
    community.execute("community.search",{{"provider","modrinth"},{"query","sodium"},{"kind","mod"},{"gameVersion","1.21.1"},{"loader","fabric"}});
    QTRY_VERIFY_WITH_TIMEOUT(ok.size()>0||bad.size()>0,30000);QCOMPARE(bad.size(),0);
    const auto project=ok.last().at(1).toMap().value("items").toList().first().toMap().value("projectId").toString();
    community.execute("community.versions",{{"provider","modrinth"},{"projectId",project},{"gameVersion","1.21.1"},{"loader","fabric"}});
    QTRY_VERIFY_WITH_TIMEOUT(ok.size()>1||bad.size()>0,30000);QCOMPARE(bad.size(),0);
    const auto version=ok.last().at(1).toMap().value("items").toList().first().toMap().value("versionId").toString();
    community.execute("community.install",{{"provider","modrinth"},{"instanceId",id},{"projectId",project},{"versionId",version},{"kind","mods"}});
    QTRY_VERIFY_WITH_TIMEOUT(ok.size()>2||bad.size()>0,60000);
    QVERIFY2(bad.isEmpty(),qPrintable(bad.isEmpty()?QString():bad.last().at(1).toString()));
    const auto mods=QDir(instances.gamePath(id)+"/mods").entryList(QDir::Files);
    QVERIFY(!mods.isEmpty());
}
void failedDownloadCanBeRetried(){
    QTemporaryDir dir;InstanceService instances(dir.path());QSignalSpy created(&instances,&FeatureService::completed);instances.execute("instance.create",{{"name","Retry"},{"version","1.21.1"},{"loader","fabric"}});const auto id=created.first().at(1).toMap().value("id").toString();
    ModrinthFixture fixture;fixture.failFile=true;
    CommunityService community(dir.path(),nullptr,fixture.url(""));QSignalSpy ok(&community,&FeatureService::completed),bad(&community,&FeatureService::failed);
    community.execute("community.install",{{"instanceId",id},{"versionId","version-1"},{"kind","mods"}});
    QTRY_COMPARE(bad.size(),1);
    // 5xx 会有限重试（初始请求 + 两次退避重试），随后保留失败状态等待用户重试。
    QVERIFY(fixture.fileRequests>=2);
    auto state=community.downloadState();
    QCOMPARE(state.value("phase").toString(),QString("error"));
    QVERIFY(state.value("canRetry").toBool());
    QVERIFY(!QFile::exists(instances.gamePath(id)+"/mods/retry-mod.jar"));
    fixture.failFile=false;
    community.execute("community.retry",{});
    QTRY_COMPARE(ok.size(),1);
    QCOMPARE(community.downloadState().value("phase").toString(),QString("complete"));
    QVERIFY(!community.downloadState().value("canRetry").toBool());
    QFile installed(instances.gamePath(id)+"/mods/retry-mod.jar");
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(),fixture.fileBytes);
}
};
QTEST_GUILESS_MAIN(CommunityTest)
#include "tst_community.moc"
