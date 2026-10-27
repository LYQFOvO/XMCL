#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QTimer>
// Real HTTP fixture, exercising the same manifest -> profile -> file path as production.
class TestGameServer : public QTcpServer {
public:
    QByteArray jar="XMCL verified test client";
    QByteArray library="XMCL verified test library";
    QByteArray logConfig="<log/>";
    bool rejectClient=false;
    bool includeLibrary=false;
    int profileDelay=0;
    explicit TestGameServer(QObject *parent=nullptr):QTcpServer(parent){
        listen(QHostAddress::LocalHost);
        connect(this,&QTcpServer::newConnection,this,[this]{
            auto *socket=nextPendingConnection();
            auto request=std::make_shared<QByteArray>();
            connect(socket,&QTcpSocket::readyRead,socket,[this,socket,request]{
                request->append(socket->readAll());if(!request->contains("\r\n\r\n"))return;
                const auto path=request->split(' ').value(1);request->clear();
                QByteArray body;int status=200;int delay=0;
                QJsonArray libraries;
                if(includeLibrary)libraries.append(QJsonObject{{"name","demo:lib:1.0"},{"downloads",QJsonObject{{"artifact",QJsonObject{{"url",url("/libs/lib.jar").toString()},{"sha1",QString::fromLatin1(QCryptographicHash::hash(library,QCryptographicHash::Sha1).toHex())},{"size",library.size()},{"path","demo/lib/lib.jar"}}}}}});
                QJsonObject profile{{"id","1.21.1"},{"mainClass","test.Main"},{"libraries",libraries},{"downloads",QJsonObject{{"client",QJsonObject{{"url",url("/client.jar").toString()},{"sha1",QString::fromLatin1(QCryptographicHash::hash(jar,QCryptographicHash::Sha1).toHex())},{"size",jar.size()}}}}}};
                if(includeLibrary)profile["logging"]=QJsonObject{{"client",QJsonObject{{"file",QJsonObject{{"url",url("/log.xml").toString()},{"id","client-1.xml"},{"sha1",QString::fromLatin1(QCryptographicHash::hash(logConfig,QCryptographicHash::Sha1).toHex())},{"size",logConfig.size()}}}}}};
                auto profileBytes=QJsonDocument(profile).toJson(QJsonDocument::Compact);
                if(path=="/manifest.json")body=QJsonDocument(QJsonObject{{"versions",QJsonArray{QJsonObject{{"id","1.21.1"},{"type","release"},{"releaseTime","2024-08-08"},{"url",url("/profile.json").toString()},{"sha1",QString::fromLatin1(QCryptographicHash::hash(profileBytes,QCryptographicHash::Sha1).toHex())}}}}}).toJson();
                else if(path=="/profile.json"){body=profileBytes;delay=profileDelay;}
                else if(path=="/client.jar"){body=jar;if(rejectClient){status=503;body="unavailable";}}
                else if(path=="/libs/lib.jar")body=library;
                else if(path=="/log.xml")body=logConfig;
                else {status=404;body="not found";}
                QTimer::singleShot(delay,socket,[socket,body,status]{socket->write("HTTP/1.1 "+QByteArray::number(status)+" Test\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();});
            });
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
    }
    QUrl url(const QString &path)const{return QUrl(QString("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));}
};
