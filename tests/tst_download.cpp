#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include "services/DownloadEngine.h"
class DownloadTest:public QObject{
    Q_OBJECT

private slots:
void completedPartialFileDoesNotNeedNetwork(){
    QTemporaryDir dir;const QByteArray body="already downloaded";const auto path=dir.filePath("file");
    {QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write("old valid content");}
    {QFile f(path+".part");QVERIFY(f.open(QIODevice::WriteOnly));f.write(body);}
    DownloadEngine engine;bool done=false;QString error;
    engine.start({{QUrl("http://127.0.0.1:1/unreachable"),path,QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex(),"sha256"}},[&](QString e){error=e;done=true;});
    QTRY_VERIFY(done);QVERIFY2(error.isEmpty(),qPrintable(error));QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),body);QVERIFY(!QFile::exists(path+".part"));
}
void verifyAndResume(){QTemporaryDir dir;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));QByteArray body="real download content";QByteArray range;
connect(&server,&QTcpServer::newConnection,&server,[&]{auto*s=server.nextPendingConnection();connect(s,&QTcpSocket::readyRead,s,[&,s]{auto request=s->readAll();range=request;QByteArray data=body;QByteArray head="HTTP/1.1 200 OK\r\n";if(request.toLower().contains("range: bytes=5-")){data=body.mid(5);head="HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 5-"+QByteArray::number(body.size()-1)+"/"+QByteArray::number(body.size())+"\r\n";}s->write(head+"Content-Length: "+QByteArray::number(data.size())+"\r\nConnection: close\r\n\r\n"+data);s->disconnectFromHost();});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);});
auto path=dir.filePath("game.jar");{QFile f(path+".part");QVERIFY(f.open(QIODevice::WriteOnly));f.write(body.left(5));}
DownloadEngine engine;bool done=false;QString error;engine.start({{QUrl(QString("http://127.0.0.1:%1/file").arg(server.serverPort())),path,QCryptographicHash::hash(body,QCryptographicHash::Sha1).toHex(),"sha1"}},[&](QString e){error=e;done=true;});QTRY_VERIFY(done);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(range.toLower().contains("range: bytes=5-"));QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),body);
}
void fallsBackToOfficialUrl(){QTemporaryDir dir;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));const QByteArray body="official fallback content";int badRequests=0;
connect(&server,&QTcpServer::newConnection,&server,[&]{auto*s=server.nextPendingConnection();connect(s,&QTcpSocket::readyRead,s,[&,s]{const auto request=s->readAll();if(request.startsWith("GET /mirror ")){++badRequests;s->write("HTTP/1.1 503 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");}else{s->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);}s->disconnectFromHost();});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);});
DownloadEngine engine;bool done=false;QString error;const auto path=dir.filePath("file");
Transfer row;row.url=QUrl(QString("http://127.0.0.1:%1/mirror").arg(server.serverPort()));row.fallback=QUrl(QString("http://127.0.0.1:%1/official").arg(server.serverPort()));row.destination=path;row.digest=QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex();row.algorithm="sha256";
engine.start({row},[&](QString e){error=e;done=true;});
QTRY_VERIFY_WITH_TIMEOUT(done,20000);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(badRequests,3);QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),body);}
void fallbackIsRememberedForLaterFiles(){QTemporaryDir dir;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));int bad=0,good=0;
connect(&server,&QTcpServer::newConnection,&server,[&]{auto*s=server.nextPendingConnection();connect(s,&QTcpSocket::readyRead,s,[&,s]{const auto request=s->readAll();if(request.startsWith("GET /mirror ")){++bad;s->write("HTTP/1.1 503 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");}else{++good;const QByteArray body="content "+QByteArray::number(good);s->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);}s->disconnectFromHost();});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);});
const auto first=QByteArray("content 1"),second=QByteArray("content 2");QList<Transfer> rows;
for(int i=0;i<2;++i){Transfer row;row.url=QUrl(QString("http://127.0.0.1:%1/mirror").arg(server.serverPort()));row.fallback=QUrl(QString("http://127.0.0.1:%1/official").arg(server.serverPort()));row.destination=dir.filePath("file"+QString::number(i));row.digest=QCryptographicHash::hash(i==0?first:second,QCryptographicHash::Sha256).toHex();row.algorithm="sha256";rows<<row;}
DownloadEngine engine;bool done=false;QString error;engine.start(rows,[&](QString e){error=e;done=true;});
QTRY_VERIFY_WITH_TIMEOUT(done,20000);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(bad,3);QCOMPARE(good,2);}
void liveMirrorFileDownload(){
    if(qEnvironmentVariableIsEmpty("XMCL_LIVE_MIRROR"))QSKIP("live BMCLAPI file probe disabled; set XMCL_LIVE_MIRROR=1");
    QTemporaryDir dir;DownloadEngine engine;bool done=false;QString error;
    Transfer row;row.url=QUrl("https://bmclapi2.bangbang93.com/maven/org/ow2/asm/asm/9.6/asm-9.6.jar");
    row.fallback=QUrl("https://maven.fabricmc.net/org/ow2/asm/asm/9.6/asm-9.6.jar");
    row.destination=dir.filePath("asm-9.6.jar");row.digest="aa205cf0a06dbd8e04ece91c0b37c3f5d567546a";row.algorithm="sha1";
    engine.start({row},[&](QString e){error=e;done=true;});
    QTRY_VERIFY_WITH_TIMEOUT(done,120000);QVERIFY2(error.isEmpty(),qPrintable(error));
    QVERIFY(QFileInfo(row.destination).size()>120000);QVERIFY(DownloadEngine::verified(row.destination,row.digest,"sha1"));
}
void followsThemedRedirect(){QTemporaryDir dir;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));const QByteArray body="redirected download content";QByteArray requested;
connect(&server,&QTcpServer::newConnection,&server,[&]{auto*s=server.nextPendingConnection();connect(s,&QTcpSocket::readyRead,s,[&,s]{const auto request=s->readAll();requested=request;if(request.startsWith("GET /start ")){s->write("HTTP/1.1 302 Found\r\nLocation: "+QString("http://127.0.0.1:%1/final").arg(server.serverPort()).toUtf8()+"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");}else{s->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);}s->disconnectFromHost();});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);});
DownloadEngine engine;bool done=false;QString error;const auto path=dir.filePath("file");engine.start({{QUrl(QString("http://127.0.0.1:%1/start").arg(server.serverPort())),path,QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex(),"sha256"}},[&](QString e){error=e;done=true;});
QTRY_VERIFY_WITH_TIMEOUT(done,10000);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(requested.startsWith("GET /final "));QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),body);}
void transientServerErrorIsRetried(){QTemporaryDir dir;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));int requests=0;const QByteArray body="retry download content";
connect(&server,&QTcpServer::newConnection,&server,[&]{auto*s=server.nextPendingConnection();connect(s,&QTcpSocket::readyRead,s,[&,s]{s->readAll();++requests;if(requests==1){s->write("HTTP/1.1 503 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");}else{s->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);}s->disconnectFromHost();});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);});
DownloadEngine engine;bool done=false;QString error;const auto path=dir.filePath("file");engine.start({{QUrl(QString("http://127.0.0.1:%1/file").arg(server.serverPort())),path,QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex(),"sha256"}},[&](QString e){error=e;done=true;});
QTRY_VERIFY_WITH_TIMEOUT(done,15000);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(requests,2);QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),body);}
void corruptDownloadIsNotPublished(){QTemporaryDir dir;QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));connect(&server,&QTcpServer::newConnection,&server,[&]{auto*s=server.nextPendingConnection();connect(s,&QTcpSocket::readyRead,s,[s]{s->readAll();s->write("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\nbad");s->disconnectFromHost();});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);});DownloadEngine engine;bool done=false;QString error;auto path=dir.filePath("file");engine.start({{QUrl(QString("http://127.0.0.1:%1/file").arg(server.serverPort())),path,QByteArray(40,'0'),"sha1"}},[&](QString e){error=e;done=true;});QTRY_VERIFY(done);QVERIFY(!error.isEmpty());QVERIFY(!QFile::exists(path));QVERIFY(!QFile::exists(path+".part"));}
};
QTEST_GUILESS_MAIN(DownloadTest)
#include "tst_download.moc"
