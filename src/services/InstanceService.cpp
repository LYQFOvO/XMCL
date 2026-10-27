#include "InstanceService.h"
#include "ArchiveUtils.h"
#include "ServiceUtils.h"
#include "MemoryAdvisor.h"
#include <QUuid>
#include <QDateTime>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QProcess>
using namespace ServiceUtils;
namespace {
QString uuid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QVariantMap identityField(){return field("instanceId","实例 ID","text",{},true);}
void validName(const QString &name){require(!name.trimmed().isEmpty()&&name.size()<=100&&!name.contains(QChar(0)),"名称不能为空且最多 100 字符");}
}
QString InstanceService::instancePath(const QString &id) const {
    require(QRegularExpression("^[a-fA-F0-9]{8}(-[a-fA-F0-9]{4}){3}-[a-fA-F0-9]{12}$").match(id).hasMatch(),"无效的实例 ID");
    return ArchiveUtils::safePath(m_root,"instances/"+id);
}
QString InstanceService::gamePath(const QString &id) const {
    const auto meta=read(instancePath(id)+"/instance.json");
    return ArchiveUtils::safePath(m_root,meta.value("isolated",true).toBool()?"instances/"+id+"/game":"minecraft");
}
QVariantList InstanceService::list() const {
    QVariantList result;
    for(const auto &name:QDir(m_root+"/instances").entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name)) {
        try { auto meta=read(instancePath(name)+"/instance.json");meta["id"]=name; meta["title"]=meta.value("name");meta["subtitle"]=meta.value("version").toString()+" · "+meta.value("loader","vanilla").toString();meta["path"]=gamePath(name);result.append(meta); } catch(const std::exception &){}
    }
    return result;
}
QVariantList InstanceService::actions() const {
    const auto id=identityField();
    return {
        action("instance.list","实例列表"),
        action("instance.create","创建实例",{field("name","名称","text",{},true),field("version","Minecraft 版本","text","1.21.1",true),field("loader","加载器","choice","vanilla",false,{"vanilla","fabric","forge","neoforge","quilt"})}),
        action("instance.get","查看实例配置",{id}),
        action("instance.rename","重命名",{id,field("name","新名称","text",{},true)}),
        action("instance.configure","启动配置（Java / 内存 / 参数）",{id,field("javaPath","Java 可执行文件（留空自动选择）","path"),field("jvmArgs","额外 JVM 参数"),field("gameArgs","额外游戏参数"),field("memoryGb","内存 GB","number",MemoryAdvisor::suggestedGb()),field("width","窗口宽度","number",1280),field("height","窗口高度","number",720)},"每个实例独立保存 Java、内存与启动参数；内存建议值由本机物理内存计算。"),
        action("instance.isolation","目录隔离",{id,field("isolated","使用实例独立目录","bool",true)},"切换游戏目录；原目录文件保留。已有共享目录不会被覆盖。"),
        action("instance.migrate","导入游戏目录",{field("sourcePath","源游戏目录","directory",{},true),field("name","实例名称","text",{},true),field("version","版本","text","1.21.1")}),
        action("instance.copy","复制实例",{id,field("name","副本名称","text",{},true)}),
        action("instance.backup","备份实例",{id}),
        action("instance.restore","恢复备份",{field("archivePath","备份 ZIP","path",{},true),field("name","恢复后的名称","text","恢复的实例")},"创建新实例，不覆盖原实例。"),
        action("instance.diagnose","运行诊断",{id}),
        action("instance.remove","删除实例",{id},"只删除该实例的独立目录；共享游戏目录不会删除。",true)
    };
}
void InstanceService::execute(const QString &actionId,const QVariantMap &args) {
    try {
        if(actionId=="instance.list") {emit completed(actionId,{{"items",list()},{"message","选择实例后可进行管理"}});return;}
        if(actionId=="instance.create"||actionId=="instance.migrate") {
            const QString name=args.value("name").toString().trimmed();validName(name);
            const QString id=uuid(), path=instancePath(id);require(QDir().mkpath(path+"/game"),"无法创建实例目录");
            try {
                if(actionId=="instance.migrate") ArchiveUtils::copyTree(localPath(args.value("sourcePath")),path+"/game");
                write(path+"/instance.json",{{"id",id},{"name",name},{"version",args.value("version","1.21.1")},{"loader",args.value("loader","vanilla")},{"isolated",true},{"javaPath",""},{"jvmArgs",""},{"gameArgs",""},{"memoryGb",MemoryAdvisor::suggestedGb()},{"width",1280},{"height",720},{"createdAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
            } catch(...) {QDir(path).removeRecursively();throw;}
            emit completed(actionId,{{"id",id},{"message","实例已创建，可继续安装游戏版本"},{"items",list()}});return;
        }
        if(actionId=="instance.restore") {
            QTemporaryDir staging(m_root+"/restore-XXXXXX");require(staging.isValid(),"无法创建恢复目录");
            ArchiveUtils::extract(localPath(args.value("archivePath")),staging.path());
            auto meta=read(staging.path()+"/instance.json");const auto id=uuid();const auto dest=instancePath(id);
            meta["id"]=id;meta["name"]=args.value("name",meta.value("name"));validName(meta.value("name").toString());meta["isolated"]=true;
            require(QFileInfo(staging.path()+"/game").isDir(),"备份缺少游戏目录");
            try { ArchiveUtils::copyTree(staging.path(),dest);write(dest+"/instance.json",meta); } catch(...) {QDir(dest).removeRecursively();throw;}
            emit completed(actionId,{{"id",id},{"message","备份已恢复为新实例"},{"items",list()}});return;
        }
        const auto id=args.value("instanceId").toString(), path=instancePath(id);auto meta=read(path+"/instance.json");
        if(actionId=="instance.get") {auto data=meta;data["id"]=id;data["title"]=data.value("name");emit completed(actionId,{{"instance",data},{"message","实例配置已读取"}});return;}
        if(actionId=="instance.remove") {require(args.value("confirm").toBool(),"请确认删除实例");require(QDir(path).removeRecursively(),"删除失败");}
        else if(actionId=="instance.rename") {validName(args.value("name").toString());meta["name"]=args.value("name");write(path+"/instance.json",meta);}
        else if(actionId=="instance.configure") {
            for(const auto &key:{"javaPath","jvmArgs","gameArgs","memoryGb","width","height"}) if(args.contains(key)) meta[key]=args.value(key);
            if(args.contains("javaPath")) meta["javaPath"] = localPath(args.value("javaPath"));
            if(args.contains("memoryGb")) {const int memory=args.value("memoryGb").toInt(),limit=MemoryAdvisor::maxGb();require(memory>=1&&memory<=limit,QString("内存需在 1–%1 GB 内（本机物理内存 %2 GB）").arg(limit).arg(MemoryAdvisor::totalGb()));}
            if(args.contains("width")) {const int width=args.value("width").toInt();require(width>=320&&width<=7680,"窗口宽度需在 320–7680 内");}
            if(args.contains("height")) {const int height=args.value("height").toInt();require(height>=240&&height<=4320,"窗口高度需在 240–4320 内");}
            write(path+"/instance.json",meta);
        } else if(actionId=="instance.isolation") {
            bool isolated=args.value("isolated",true).toBool();
            if(isolated!=meta.value("isolated",true).toBool()) {
                const auto dest=isolated?path+"/game":m_root+"/minecraft";
                if(!QFileInfo(dest).exists()) ArchiveUtils::copyTree(gamePath(id),dest);
                meta["isolated"]=isolated;write(path+"/instance.json",meta);
            }
        } else if(actionId=="instance.copy") {
            const auto newId=uuid(),target=instancePath(newId);validName(args.value("name").toString());
            try {require(QDir().mkpath(target),"无法创建副本目录");ArchiveUtils::copyTree(gamePath(id),target+"/game");meta["id"]=newId;meta["name"]=args.value("name");meta["isolated"]=true;write(target+"/instance.json",meta);}catch(...){QDir(target).removeRecursively();throw;}
            emit completed(actionId,{{"id",newId},{"message","实例已复制"},{"items",list()}});return;
        } else if(actionId=="instance.backup") {
            require(QDir().mkpath(m_root+"/backups"),"无法创建备份目录");QTemporaryDir staging(m_root+"/backup-XXXXXX");require(staging.isValid(),"无法创建临时目录");
            ArchiveUtils::copyTree(gamePath(id),staging.path()+"/game");meta["isolated"]=true;write(staging.path()+"/instance.json",meta);
            const auto archive=m_root+"/backups/"+id+"-"+QString::number(QDateTime::currentMSecsSinceEpoch())+".zip";
            try {ArchiveUtils::pack(staging.path(),archive);}catch(...){QFile::remove(archive);throw;}
            emit completed(actionId,{{"path",archive},{"message","备份已保存"},{"details",archive}});return;
        } else if(actionId=="instance.diagnose") {
            const auto game=gamePath(id);QStringList lines{"实例："+meta.value("name").toString(),"目录："+game,"版本："+meta.value("version").toString(),"加载器："+meta.value("loader").toString()};
            lines << (QFileInfo(game).isWritable()?"游戏目录可写":"游戏目录不可写");
            const auto java=meta.value("javaPath").toString();lines << (java.isEmpty()?"Java：使用全局自动选择":QFileInfo(java).isExecutable()?"Java 可执行文件存在":"Java 路径无效");
            emit completed(actionId,{{"message","实例诊断完成"},{"details",lines.join('\n')}});return;
        } else throw std::runtime_error("未知实例操作");
        emit completed(actionId,{{"message","操作已保存"},{"items",list()}});
    } catch(const std::exception &e) {emit failed(actionId,QString::fromUtf8(e.what()));}
}
