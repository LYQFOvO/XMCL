#include "ResourceService.h"
#include "InstanceService.h"
#include "ArchiveUtils.h"
#include "ServiceUtils.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QTemporaryDir>
using namespace ServiceUtils;
namespace {
QByteArray hashFile(const QString &path){QFile f(path);require(f.open(QIODevice::ReadOnly),"文件无法读取");QCryptographicHash hash(QCryptographicHash::Sha256);require(hash.addData(&f),"读取文件摘要失败");return hash.result();}
void nbtString(QDataStream &s,const QString &value){auto utf=value.toUtf8();require(utf.size()<65536,"服务器字段过长");s<<quint16(utf.size());s.writeRawData(utf.constData(),utf.size());}
QString readNbtString(QDataStream &s){quint16 n;s>>n;QByteArray v(n,0);require(s.readRawData(v.data(),n)==n,"服务器数据不完整");return QString::fromUtf8(v);}
QVariant nbtValue(QDataStream &s,int type,int depth=0){require(depth<24,"NBT 层级超限");switch(type){
case 1:{qint8 n;s>>n;return n;}case 2:{qint16 n;s>>n;return n;}case 3:{qint32 n;s>>n;return n;}case 4:{qint64 n;s>>n;return n;}
case 5:{float n;s.setFloatingPointPrecision(QDataStream::SinglePrecision);s>>n;s.setFloatingPointPrecision(QDataStream::DoublePrecision);return n;}case 6:{double n;s>>n;return n;}
case 8:return readNbtString(s);
case 10:{QVariantMap value;while(true){quint8 child;s>>child;require(s.status()==QDataStream::Ok,"服务器数据损坏");if(!child)break;auto key=readNbtString(s);value[key]=nbtValue(s,child,depth+1);}return value;}
case 9:{quint8 child;qint32 n;s>>child>>n;require(n>=0&&n<100000,"列表数量超限");QVariantList v;for(int i=0;i<n;++i)v.append(nbtValue(s,child,depth+1));return v;}
case 7:case 11:case 12:{qint32 n;s>>n;require(n>=0&&n<1000000,"NBT 数组超限");const int size=type==7?1:type==11?4:8;require(s.skipRawData(n*size)==n*size,"NBT 数组不完整");return {};}
default:throw std::runtime_error("不支持的 NBT 类型");}}
QVariantList readServers(const QString &path){QFile f(path);require(f.size()<16*1024*1024&&f.open(QIODevice::ReadOnly),"无法读取 servers.dat");QDataStream s(&f);quint8 type;s>>type;require(type==10,"servers.dat 不是未压缩 NBT");readNbtString(s);return nbtValue(s,10).toMap().value("servers").toList();}
void writeServers(const QString &path,const QVariantList &items){QByteArray bytes;QDataStream s(&bytes,QIODevice::WriteOnly);s<<quint8(10);nbtString(s,"");s<<quint8(9);nbtString(s,"servers");s<<quint8(10)<<qint32(items.size());for(const auto &item:items){auto m=item.toMap();for(const auto &key:{"name","ip","icon"})if(m.contains(key)){s<<quint8(8);nbtString(s,key);nbtString(s,m.value(key).toString());}s<<quint8(0);}s<<quint8(0);QSaveFile file(path);require(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit(),"无法写入 servers.dat");}
QPair<QString,quint16> serverAddress(QString address){require(!address.isEmpty()&&!address.contains('/')&&!address.contains('@')&&!address.contains(' '),"服务器地址无效");QString host=address;int port=25565;if(address.startsWith('[')){int end=address.indexOf(']');require(end>1,"IPv6 地址需使用 [地址]:端口");host=address.mid(1,end-1);if(end+1<address.size()){require(address.at(end+1)==':',"端口格式错误");port=address.mid(end+2).toInt();}QHostAddress ipv6;require(ipv6.setAddress(host),"IPv6 地址无效");}else if(address.count(':')==1){host=address.section(':',0,0);port=address.section(':',1).toInt();}else if(address.contains(':')){QHostAddress ip;require(ip.setAddress(address),"IPv6 地址无效");}require(!host.isEmpty()&&port>0&&port<=65535,"端口需在 1–65535 之间");return {host,quint16(port)};}
}
QVariantList ResourceService::actions() const {
    auto id=field("instanceId","实例 ID","text",{},true);auto kind=field("kind","资源类型","choice","mods",true,{"mods","resourcepacks","shaderpacks","saves","datapacks"});
    return {action("resource.list","资源列表",{id,kind}),action("resource.import","导入资源",{id,kind,field("sourcePath","资源文件或世界目录","path",{},true),field("name","目标名称（可留空）")}),action("resource.toggle","启用 / 禁用模组",{id,field("name","模组文件名","text",{},true),field("enabled","启用","bool",true)}),action("resource.remove","移除资源",{id,kind,field("name","资源文件名","text",{},true)},"删除指定实例中的资源。",true),action("resource.duplicates","检测重复模组",{id}),action("server.list","服务器列表",{id}),action("server.save","添加 / 编辑服务器",{id,field("name","名称","text",{},true),field("address","地址，例如 localhost:25565","text",{},true)}),action("server.remove","删除服务器",{id,field("address","服务器地址","text",{},true)},"同步从 servers.dat 中移除。",true),action("server.sync","读取 servers.dat",{id}),action("server.ping","检测连接延迟",{field("address","服务器地址","text",{},true)},"检测 TCP 建连延迟，不代表游戏内延迟。")};
}
void ResourceService::execute(const QString &a,const QVariantMap &args){try{
    if(a=="server.ping"){
        auto address=serverAddress(args.value("address").toString());auto *socket=new QTcpSocket(this);auto timer=std::make_shared<QElapsedTimer>();timer->start();auto done=std::make_shared<bool>(false);
        auto finish=[this,socket,done,a,timer](QString error){if(*done)return;*done=true;if(error.isEmpty())emit completed(a,{{"message",QString("TCP 已连接，延迟 %1 ms").arg(timer->elapsed())}});else emit failed(a,error);socket->abort();socket->deleteLater();};
        connect(socket,&QTcpSocket::connected,this,[finish]{finish({});});connect(socket,&QTcpSocket::errorOccurred,this,[finish,socket](QAbstractSocket::SocketError){finish(socket->errorString());});QTimer::singleShot(5000,socket,[finish]{finish("连接超时");});socket->connectToHost(address.first,address.second);return;
    }
    InstanceService instances(m_root);const auto id=args.value("instanceId").toString();const auto game=instances.gamePath(id);
    if(a.startsWith("server.")){
        const auto path=ArchiveUtils::safePath(game,"servers.dat");QVariantList servers=QFile::exists(path)?readServers(path):QVariantList{};
        if(a=="server.save"||a=="server.remove"){
            auto address=args.value("address").toString();serverAddress(address);if(a=="server.remove")require(args.value("confirm").toBool(),"请确认删除服务器");
            for(qsizetype i=servers.size()-1;i>=0;--i)if(servers[i].toMap().value("ip")==address)servers.removeAt(i);
            if(a=="server.save"){require(!args.value("name").toString().trimmed().isEmpty(),"服务器名称不能为空");servers.append(QVariantMap{{"name",args.value("name")},{"ip",address}});}require(QDir().mkpath(game),"目录无法创建");writeServers(path,servers);
        } else require(a=="server.list"||a=="server.sync","未知服务器操作");
        QVariantList items;for(auto v:servers){auto m=v.toMap();m["title"]=m.value("name");m["subtitle"]=m.value("ip");m["address"]=m.value("ip");m["id"]=m.value("ip");items.append(m);}emit completed(a,{{"items",items},{"message","服务器已同步到游戏目录"}});return;
    }
    QString kind=args.value("kind","mods").toString();require(QStringList{"mods","resourcepacks","shaderpacks","saves","datapacks"}.contains(kind),"未知资源类型");
    const auto directory=ArchiveUtils::safePath(game,kind);require(QDir().mkpath(directory),"无法创建资源目录");
    if(a=="resource.import"){
        const auto source=localPath(args.value("sourcePath"));const auto info=QFileInfo(source);require(info.exists()&&!info.isSymLink(),"源资源不存在或为符号链接");
        QString name=args.value("name").toString().trimmed();if(name.isEmpty())name=info.fileName();if(kind=="saves"&&info.isFile())name=info.completeBaseName();require(!name.contains('/')&&!name.contains('\\')&&name!="."&&name!="..","目标名称无效");
        const auto target=ArchiveUtils::safePath(directory,name);require(!QFileInfo::exists(target),"同名资源已存在");
        if(info.isDir()){require(kind=="saves"||kind=="datapacks","该资源类型必须是文件");try{ArchiveUtils::copyTree(source,target);}catch(...){QDir(target).removeRecursively();throw;}}
        else if(kind=="saves"){try{ArchiveUtils::extract(source,target);require(QFile::exists(target+"/level.dat"),"世界压缩包根目录需包含 level.dat");}catch(...){QDir(target).removeRecursively();throw;}}
        else{require(kind!="mods"||info.suffix().toLower()=="jar","模组必须是 JAR 文件");require(info.size()<=2LL*1024*1024*1024,"资源文件超过 2 GB");require(QFile::copy(source,target),"资源复制失败");}
    }else if(a=="resource.toggle"||a=="resource.remove"){
        const QString name=args.value("name").toString();require(!name.contains('/')&&!name.contains('\\')&&!name.isEmpty(),"资源名称无效");const auto path=ArchiveUtils::safePath(directory,name);require(QFileInfo::exists(path),"资源不存在");
        if(a=="resource.remove"){require(args.value("confirm").toBool(),"请确认删除资源");require(QFileInfo(path).isDir()?QDir(path).removeRecursively():QFile::remove(path),"删除失败");}
        else{require(kind=="mods"&&QFileInfo(path).isFile(),"只支持切换模组文件");bool enabled=args.value("enabled").toBool();const auto target=enabled?(path.endsWith(".disabled")?path.left(path.size()-9):path):(path.endsWith(".disabled")?path:path+".disabled");if(target!=path)require(!QFile::exists(target)&&QFile::rename(path,target),"切换状态失败或目标已存在");}
    }else require(a=="resource.list"||a=="resource.duplicates","未知资源操作");
    QVariantList items;QHash<QByteArray,QString> seen;for(const auto &file:QDir(directory).entryInfoList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name)){
        if(file.isSymLink())continue;QString subtitle=file.isDir()?"目录":QString::number(file.size()/1024.0,'f',1)+" KB";if(file.fileName().endsWith(".disabled"))subtitle+=" · 已禁用";
        if(a=="resource.duplicates"){if(!file.isFile())continue;const auto hash=hashFile(file.filePath());if(!seen.contains(hash)){seen[hash]=file.fileName();continue;}subtitle="与 "+seen[hash]+" 内容相同";}
        items.append(QVariantMap{{"id",file.fileName()},{"title",file.fileName()},{"name",file.fileName()},{"subtitle",subtitle},{"path",file.filePath()}});
    }emit completed(a,{{"items",items},{"message",QString("共 %1 项").arg(items.size())}});
}catch(const std::exception &e){emit failed(a,QString::fromUtf8(e.what()));}}
