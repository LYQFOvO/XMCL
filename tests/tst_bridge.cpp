#include <QtTest>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include "services/BridgeService.h"
#include "services/InstanceService.h"
#include "services/ServiceUtils.h"
class BridgeTest: public QObject {
    Q_OBJECT
private slots:
void installsBundledBridge(){
    QTemporaryDir dir;InstanceService instances(dir.path());QSignalSpy created(&instances,&FeatureService::completed);
    instances.execute("instance.create",{{"name","Bridge"},{"loader","fabric"}});QCOMPARE(created.size(),1);
    auto id=created.first().at(1).toMap().value("id").toString();BridgeService bridge(dir.path());QSignalSpy ok(&bridge,&FeatureService::completed),bad(&bridge,&FeatureService::failed);
    bridge.execute("bridge.install",{{"instanceId",id}});QCOMPARE(bad.size(),0);QCOMPARE(ok.size(),1);
    QFile jar(instances.gamePath(id)+"/mods/kamucl-bridge-1.0.1.jar");QVERIFY(jar.open(QIODevice::ReadOnly));QCOMPARE(jar.read(2),QByteArray("PK"));
    bridge.execute("bridge.install",{{"instanceId",id}});QCOMPARE(bad.size(),1);
}
void authenticatesMutationAndHandlesRejection(){
    QTemporaryDir dir;InstanceService instances(dir.path());QSignalSpy created(&instances,&FeatureService::completed);instances.execute("instance.create",{{"name","Bridge"}});
    auto id=created.first().at(1).toMap().value("id").toString();QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
    const QString secret="test-only-bridge-token";ServiceUtils::write(instances.gamePath(id)+"/.kamucl-bridge.json",{{"protocol",1},{"port",server.serverPort()},{"token",secret}});
    QByteArray captured;connect(&server,&QTcpServer::newConnection,&server,[&]{auto *socket=server.nextPendingConnection();connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{captured+=socket->readAll();if(!captured.contains("\r\n\r\n")||!captured.endsWith('}'))return;QByteArray body=R"({"ok":false,"error":"server parameter is read-only"})";socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);socket->disconnectFromHost();});connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);});
    BridgeService bridge(dir.path());QSignalSpy bad(&bridge,&FeatureService::failed),ok(&bridge,&FeatureService::completed);
    bridge.execute("bridge.set",{{"instanceId",id},{"paramId","sample"},{"value","true"}});QTRY_COMPARE(bad.size(),1);QCOMPARE(ok.size(),0);
    QVERIFY(captured.toLower().contains("x-kamucl-token: "+secret.toUtf8()));QVERIFY(captured.contains("POST /kamucl/v1/set"));QVERIFY(captured.contains("\"value\":true"));QVERIFY(!bad.first().at(1).toString().contains(secret));
}
};
QTEST_GUILESS_MAIN(BridgeTest)
#include "tst_bridge.moc"
