#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "ConnectivityService.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QUrlQuery>
#ifdef Q_OS_UNIX
#include <signal.h>
#endif
class ConnectivityTest : public QObject {
    Q_OBJECT
private slots:
    void directAddresses() {
        QTemporaryDir root; ConnectivityService service(root.path());
        QSignalSpy done(&service,&FeatureService::completed), failed(&service,&FeatureService::failed);
        service.execute("connectivity.direct",{{"address","[2001:db8::1]:25566"}});
        QCOMPARE(failed.size(),0); QCOMPARE(done.size(),1);
        auto data=done.takeFirst()[1].toMap(); QCOMPARE(data.value("host").toString(),"2001:db8::1");
        QCOMPARE(data.value("port").toInt(),25566); QCOMPARE(data.value("address").toString(),"[2001:db8::1]:25566");
        service.execute("connectivity.direct",{{"address","example.org"}});
        QCOMPARE(done.takeFirst()[1].toMap().value("port").toInt(),25565);
    }
    void unsafeAddresses() {
        QTemporaryDir root; ConnectivityService service(root.path());
        QSignalSpy done(&service,&FeatureService::completed), failed(&service,&FeatureService::failed);
        for(const auto &value:QStringList{"host:0","host:65536","host:abc","user@host:25565","https://host/path","host:25565/path","host\nfoo","", "-evil"})
            service.execute("connectivity.direct",{{"address",value}});
        QCOMPARE(done.size(),0); QCOMPARE(failed.size(),9);
    }
    void configurationValidation() {
        QTemporaryDir root; ConnectivityService service(root.path());
        QSignalSpy done(&service,&FeatureService::completed), failed(&service,&FeatureService::failed);
        service.execute("frp.configure",{{"binary","/nonexistent/frpc"},{"server","localhost"},{"serverPort",7000},{"localPort",25565},{"remotePort",25565}});
        service.execute("terracotta.configure",{{"binary","/nonexistent/terracotta"}});
        service.execute("terracotta.join",{{"room","madeup"}});
        service.execute("voxlink.host",{});
        QCOMPARE(done.size(),0); QCOMPARE(failed.size(),4);
    }
    void frpLifecycle() {
        QTemporaryDir root; ConnectivityService service(root.path());
        QSignalSpy done(&service,&FeatureService::completed), failed(&service,&FeatureService::failed);
        service.execute("frp.configure",{{"binary",QCoreApplication::applicationFilePath()},{"server","127.0.0.1"},{"serverPort",7000},{"localPort",25565},{"remotePort",25566}});
        QCOMPARE(done.size(),1); done.clear();
        service.execute("frp.start",{});QTRY_COMPARE(done.size(),1);
        QCOMPARE(done.first()[1].toMap().value("tunnelVerified").toBool(),false);
        service.execute("frp.start",{}); QCOMPARE(failed.size(),1);
        service.execute("frp.stop",{});QTRY_COMPARE(done.size(),2);
        QCOMPARE(done.last()[0].toString(),"frp.stop");
        service.execute("frp.status",{});QCOMPARE(done.last()[1].toMap().value("frpRunning").toBool(),false);
    }
    void terracottaHostAndJoin() {
        QTemporaryDir root; ConnectivityService service(root.path());
        QSignalSpy done(&service,&FeatureService::completed), failed(&service,&FeatureService::failed);
        service.execute("terracotta.configure",{{"binary",QCoreApplication::applicationFilePath()}});done.clear();
        service.execute("terracotta.host",{{"player","test player"}});
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);QCOMPARE(failed.size(),0);
        QCOMPARE(done.first()[1].toMap().value("room").toString(),"U/AAAA-BBBB-CCCC-DDDD");
        QCOMPARE(done.first()[1].toMap().value("phase").toString(),"ready");
        service.execute("terracotta.stop",{});QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,5000);
        done.clear();service.execute("terracotta.join",{{"room","u/aaaa-bbbb-cccc-dddd"},{"player","test"}});
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);QCOMPARE(failed.size(),0);
        QCOMPARE(done.first()[1].toMap().value("address").toString(),"127.0.0.1:34567");
        service.execute("terracotta.reset",{});QTRY_COMPARE(done.size(),2);
        QCOMPARE(done.last()[1].toMap().value("room").toString(),QString());
        service.execute("terracotta.stop",{});QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,5000);
    }
    void terracottaRejectsOversizedResponse() {
        qputenv("XMCL_TC_FIXTURE_SCENARIO","oversized");
        QTemporaryDir root; ConnectivityService service(root.path());
        QSignalSpy done(&service,&FeatureService::completed), failed(&service,&FeatureService::failed);
        service.execute("terracotta.configure",{{"binary",QCoreApplication::applicationFilePath()}});done.clear();
        service.execute("terracotta.host",{});QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,5000);
        QVERIFY(failed.first()[1].toString().contains("64 KiB"));QCOMPARE(done.size(),0);
        qunsetenv("XMCL_TC_FIXTURE_SCENARIO");
    }
    void cancellationCannotCompleteAStaleRoom() {
        QTemporaryDir root; ConnectivityService service(root.path());
        QSignalSpy done(&service,&FeatureService::completed), failed(&service,&FeatureService::failed);
        service.execute("terracotta.configure",{{"binary",QCoreApplication::applicationFilePath()}});done.clear();
        service.execute("terracotta.host",{});service.execute("terracotta.stop",{});
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);QCOMPARE(done.first()[0].toString(),"terracotta.stop");
        QCOMPARE(failed.size(),1);QCOMPARE(failed.first()[0].toString(),"terracotta.host");
        service.execute("terracotta.status",{});QVERIFY(!done.last()[1].toMap().value("terracottaRunning").toBool());
    }
    void cleansOwnedChildrenAfterDaemonExit() {
#ifdef Q_OS_UNIX
        QTemporaryDir root; const auto pidFile=root.path()+"/child-pid";
        qputenv("XMCL_TC_FIXTURE_CHILD_PID",pidFile.toUtf8());
        qputenv("XMCL_TC_FIXTURE_SCENARIO","exit-with-child");
        ConnectivityService service(root.path()); QSignalSpy done(&service,&FeatureService::completed),failed(&service,&FeatureService::failed);
        service.execute("terracotta.configure",{{"binary",QCoreApplication::applicationFilePath()}});
        service.execute("terracotta.host",{});
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(pidFile),5000);
        QFile file(pidFile);QVERIFY(file.open(QIODevice::ReadOnly));const auto pid=file.readAll().toLongLong();QVERIFY(pid>1);
        QTRY_VERIFY_WITH_TIMEOUT(failed.size()>0,5000);
        bool terminated=false;
        for(int i=0;i<40;++i){if(::kill(static_cast<pid_t>(pid),0)!=0){terminated=true;break;}QTest::qWait(50);}
        if(!terminated)::kill(static_cast<pid_t>(pid),SIGKILL);
        qunsetenv("XMCL_TC_FIXTURE_CHILD_PID");qunsetenv("XMCL_TC_FIXTURE_SCENARIO");
        QVERIFY2(terminated,"Owned descendant survived the daemon's exit");
#endif
    }
    void capabilitiesAreHonest() {
        QTemporaryDir root; ConnectivityService service(root.path()); QSignalSpy done(&service,&FeatureService::completed);
        service.execute("connectivity.capabilities",{}); QCOMPARE(done.size(),1);
        QCOMPARE(done.first()[1].toMap().value("voxlinkAvailable").toBool(),false);
    }
};
// The test executable doubles as a local CLI protocol fixture. No Internet or
// installed Terracotta/frpc is needed, and production never selects this mode.
int main(int argc,char **argv) {
    QCoreApplication application(argc,argv);const auto arguments=application.arguments();
    if(arguments.contains("-c")||arguments.contains("--fixture-child"))return application.exec();
    const bool daemon=arguments.contains("--daemon"),discovery=arguments.contains("--hmcl")||arguments.contains("--hmcl2");
#ifdef Q_OS_MACOS
    if(discovery){
        QFile source(QDir::homePath()+"/fixture-port");if(!source.open(QIODevice::ReadOnly))return 2;
        QFile destination(arguments.last());if(!destination.open(QIODevice::WriteOnly))return 3;
        destination.write(source.readAll());return 0;
    }
#endif
    if(daemon||discovery){
        QTcpServer server;if(!server.listen(QHostAddress::LocalHost))return 4;
        if(qgetenv("XMCL_TC_FIXTURE_SCENARIO")=="exit-with-child"){
            auto child=new QProcess;child->start(QCoreApplication::applicationFilePath(),{"--fixture-child"});if(!child->waitForStarted())return 7;
            QFile record(QString::fromUtf8(qgetenv("XMCL_TC_FIXTURE_CHILD_PID")));if(record.open(QIODevice::WriteOnly)){record.write(QByteArray::number(child->processId()));record.close();}
            QTimer::singleShot(1200,&application,&QCoreApplication::quit);
        }
        QFile portFile(daemon?QDir::homePath()+"/fixture-port":arguments.last());
        if(!portFile.open(QIODevice::WriteOnly))return 5;
        portFile.write(QJsonDocument(QJsonObject{{"port",server.serverPort()}}).toJson());portFile.close();
        QString mode="idle";
        QObject::connect(&server,&QTcpServer::newConnection,&application,[&]{
            while(server.hasPendingConnections()){
                auto socket=server.nextPendingConnection();auto request=std::make_shared<QByteArray>();
                QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket,request]{
                    request->append(socket->readAll());if(!request->contains("\r\n\r\n"))return;
                    const auto target=QUrl::fromEncoded(request->split(' ').value(1));const auto path=target.path();
                    QByteArray body="{}";
                    if(path=="/state/scanning")mode="host";
                    if(path=="/state/guesting")mode="guest";
                    if(path=="/state/ide")mode="idle";
                    if(path=="/state"){
                        if(qgetenv("XMCL_TC_FIXTURE_SCENARIO")=="oversized")body=QByteArray(70000,'x');
                        else body=QJsonDocument(QJsonObject{{"state",mode=="host"?"host-ok":mode=="guest"?"guest-ok":"ide"},{"room","U/AAAA-BBBB-CCCC-DDDD"},{"url","127.0.0.1:34567"},{"token","must-not-be-forwarded"}}).toJson();
                    }
                    socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);socket->disconnectFromHost();
                    if(path=="/panic")QTimer::singleShot(20,&application,&QCoreApplication::quit);
                });
                QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
        return application.exec();
    }
    ConnectivityTest test;return QTest::qExec(&test,argc,argv);
}
#include "tst_connectivity.moc"
