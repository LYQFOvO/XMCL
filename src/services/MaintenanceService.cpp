#include "MaintenanceService.h"
#include "ServiceUtils.h"
#include "ArchiveUtils.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUuid>
#include <QVersionNumber>
#include <memory>
using namespace ServiceUtils;
namespace {
constexpr qint64 maxLogBytes=8LL*1024*1024;
constexpr qint64 maxPackageBytes=2LL*1024*1024*1024;
struct Rule { const char *pattern; const char *code; const char *suggestion; };
const Rule rules[]={
    {"UnsupportedClassVersionError","java-version","检查实例要求的 Java 主版本，并选择对应 Java；这不是自动修复。"},
    {"OutOfMemoryError|Could not reserve enough space|insufficient memory","memory","检查可用内存和实例 JVM 内存设置；调整后重新启动验证。"},
    {"UnsatisfiedLinkError|GLFW error|Failed to create window|Pixel format not accelerated","graphics-native","检查显卡驱动、系统架构以及原生库；保留原环境再逐项验证。"},
    {"ClassNotFoundException|NoClassDefFoundError|Mod resolution encountered|Incompatible mod set","dependency","检查 Minecraft、加载器和模组的版本及必需依赖；备份后逐项排查。"},
    {"Invalid session|AuthenticationException|Failed to verify username","authentication","重新登录相应账号，检查网络和服务状态；不要分享登录凭据。"},
    {"AccessDeniedException|Permission denied","permission","检查实例目录写入权限，避免使用系统保护目录。"},
    {"ZipException|Invalid or corrupt jarfile","corrupt-file","通过已配置来源重新校验或下载报错的文件，保留原文件供排查。"}
};
QString digest(const QString &path) {
    QFile f(path);require(f.open(QIODevice::ReadOnly),"无法读取校验文件");QCryptographicHash h(QCryptographicHash::Sha256);require(h.addData(&f),"无法计算文件校验");return QString::fromLatin1(h.result().toHex());
}
QString readLog(const QString &path) {
    QFileInfo info(path);require(info.isFile()&&!info.isSymLink(),"日志必须是普通文件");QFile f(path);require(f.open(QIODevice::ReadOnly),"无法读取日志");
    if(f.size()>maxLogBytes){f.seek(f.size()-maxLogBytes);f.readLine();}
    return QString::fromUtf8(f.read(maxLogBytes));
}
QVariantList inventory(const QString &root) {
    QVariantList files;QDirIterator it(root,QDir::Files|QDir::Dirs|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(it.hasNext()){it.next();const auto info=it.fileInfo();require(!info.isSymLink(),"备份不支持符号链接");if(info.isDir())continue;require(info.isFile(),"备份不支持特殊文件");files.append(QVariantMap{{"path",QDir(root).relativeFilePath(info.filePath())},{"sha256",digest(info.filePath())},{"permissions",int(info.permissions())}});require(files.size()<=100000,"备份文件数量超限");}
    return files;
}
void verifyInventory(const QString &root,const QVariantList &files) {
    require(!files.isEmpty(),"备份清单为空");require(inventory(root).size()==files.size(),"备份目录文件数量已变化");
    for(const auto &v:files){const auto row=v.toMap();const auto path=ArchiveUtils::safePath(root,row.value("path").toString());require(digest(path)==row.value("sha256").toString(),"备份摘要不匹配");}
}
QString uniqueId(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
}
MaintenanceService::MaintenanceService(QString root,QObject *p):FeatureService(std::move(root),p),m_network(this),m_download(this) {
    connect(&m_download,&DownloadEngine::changed,this,[this](const QVariantMap &s){if(s.value("active").toBool())emit activity(QString("XMCL 更新包下载：%1 字节").arg(s.value("bytes").toLongLong()));});
}
QString MaintenanceService::statePath()const{return m_root+"/maintenance/update-state.json";}
QString MaintenanceService::platform(){
#ifdef Q_OS_MACOS
    return "macos";
#elif defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_LINUX)
    return "linux";
#else
    return "unsupported";
#endif
}
QString MaintenanceService::architecture(){auto arch=QSysInfo::buildCpuArchitecture();return arch=="arm64"?"arm64":arch=="aarch64"?"arm64":arch;}
QVariantList MaintenanceService::actions()const {
    auto log=field("path","日志文件","path",{},true);
    return {
        action("maintenance.logs","列出日志",{field("path","游戏或日志目录","directory",m_root)}),
        action("maintenance.read","查看隐私过滤日志",{log},"仅显示固定故障类别和 Minecraft/Java 栈帧；未知原始行隐藏。最多读取文件尾部 8 MiB。"),
        action("maintenance.export","导出隐私过滤报告",{log,field("destination","报告保存路径","text",{},true)},"不会导出原始命令、账号、路径、聊天或未分类日志行。已有文件不覆盖。"),
        action("maintenance.diagnose","分析启动日志",{log},"根据日志提供排查建议，不会更改实例，也不代表问题已修复。"),
        action("update.configure","配置 XMCL 更新源",{field("feed","自有 HTTPS manifest URL","text",{},true),field("expectedSha256","独立获取的包 SHA-256（推荐）")},"没有内置发行源。来源须为 XMCL 自有发布；SHA-256 只校验完整性，不能替代发布签名。"),
        action("update.check","检查 XMCL 更新"),
        action("update.download","下载并验证更新包",{},"按最近检查的 manifest 下载，验证 SHA-256 和包内应用标识，完成后仅暂存。"),
        action("update.prepare","校验本地更新包",{field("manifest","XMCL manifest 文件","path",{},true),field("package","更新压缩包","path",{},true)},"只解压到独立暂存目录，不执行脚本或替换当前应用。"),
        action("update.status","查看更新与恢复状态"),
        action("update.cancel","取消更新下载"),
        action("update.backup","备份当前 XMCL 应用目录",{field("path","应用目录（含可执行文件）","directory",{},true)},"复制到独立备份，记录每个文件 SHA-256 与权限。含符号链接的应用目录会明确拒绝。"),
        action("update.rollback","从备份恢复到新目录",{field("backupId","备份 ID","text",{},true),field("destination","尚不存在的恢复目录","directory",{},true)},"校验备份后恢复到新目录；保留运行中的应用。需要退出后人工检查并替换。",true)
    };
}
QVariantList MaintenanceService::diagnose(const QString &text) {
    QVariantList result;for(const auto &rule:rules){QRegularExpression rx(QString::fromUtf8(rule.pattern),QRegularExpression::CaseInsensitiveOption);if(rx.match(text).hasMatch())result.append(QVariantMap{{"code",rule.code},{"title",rule.code},{"evidence",QString::fromUtf8(rule.pattern).split('|').first()},{"suggestion",QString::fromUtf8(rule.suggestion)},{"subtitle",QString::fromUtf8(rule.suggestion)},{"repaired",false},{"confidence","heuristic"}});}return result;
}
QString MaintenanceService::redactLog(const QString &text) {
    // Never emit arbitrary log text: credentials can appear on separate lines or use unknown keys.
    QStringList out;int hidden=0;
    const QRegularExpression frame(R"(^\s*at ((?:java|javax|net\.minecraft|com\.mojang)\.[A-Za-z0-9_.$]+)\(([A-Za-z0-9_$]+\.java:[0-9]{1,7}|Native Method|Unknown Source)\)\s*$)");
    for(const auto &line:text.split('\n')){
        QStringList labels;for(const auto &rule:rules){QRegularExpression rx(QString::fromUtf8(rule.pattern),QRegularExpression::CaseInsensitiveOption);if(rx.match(line).hasMatch())labels<<QString::fromUtf8(rule.pattern).split('|').first();}
        const auto match=frame.match(line);if(labels.isEmpty()&&!match.hasMatch()){if(!line.trimmed().isEmpty())++hidden;continue;}
        if(hidden){out<<QString("[已隐藏 %1 行未分类或敏感原文]").arg(hidden);hidden=0;}
        if(!labels.isEmpty())out<<"[故障证据] "+labels.join(", ");
        else out<<"at "+match.captured(1)+"("+match.captured(2)+")";
    }
    if(hidden)out<<QString("[已隐藏 %1 行未分类或敏感原文]").arg(hidden);
    return out.join('\n');
}
void MaintenanceService::validateManifest(const QVariantMap &m,const QString &expected) {
    require(m.value("schema").toInt()==1,"不支持的更新 manifest schema");require(m.value("appId")=="com.xmcl.launcher","更新包不是 XMCL");
    require(m.value("platform").toString()==platform()&&platform()!="unsupported","更新包平台与当前系统不匹配");require(m.value("arch").toString()==architecture(),"更新包架构与当前应用不匹配");
    require(QRegularExpression(R"(^[0-9]+\.[0-9]+\.[0-9]+$)").match(m.value("version").toString()).hasMatch(),"更新版本必须为三段数字");
    const auto entry=m.value("entryPoint").toString();require(!entry.isEmpty()&&entry.size()<512,"缺少更新入口文件");ArchiveUtils::safePath(QDir::tempPath(),entry);
    const auto package=m.value("package").toMap();require(https(QUrl(package.value("url").toString())),"更新包只允许不含凭据的 HTTPS URL");
    const auto hash=package.value("sha256").toString();require(QRegularExpression("^[a-fA-F0-9]{64}$").match(hash).hasMatch(),"更新包缺少有效 SHA-256");
    require(expected.isEmpty()||hash.compare(expected,Qt::CaseInsensitive)==0,"更新包与独立配置的 SHA-256 不匹配");
    const auto size=package.value("bytes").toLongLong();require(size>0&&size<=maxPackageBytes,"更新包大小必须在 1 字节至 2 GiB 内");
}
QVariantMap MaintenanceService::prepare(const QVariantMap &manifest,const QString &package) {
    validateManifest(manifest,read(m_root+"/maintenance/config.json",true).value("expectedSha256").toString());
    require(QFileInfo(package).isFile()&&!QFileInfo(package).isSymLink(),"更新包必须为普通文件");const auto meta=manifest.value("package").toMap();
    require(QFileInfo(package).size()==meta.value("bytes").toLongLong(),"更新包大小不匹配");require(DownloadEngine::verified(package,meta.value("sha256").toString().toLatin1(),"sha256"),"更新包 SHA-256 不匹配");
    require(QDir().mkpath(m_root+"/maintenance/staging"),"无法创建暂存目录");QTemporaryDir stage(m_root+"/maintenance/staging/update-XXXXXX");require(stage.isValid(),"无法创建更新暂存目录");
    QVariantMap state{{"phase","extracting"},{"applied",false},{"stagePath",stage.path()},{"manifest",manifest}};write(statePath(),state);
    try {
        ArchiveUtils::extract(package,stage.path(),4LL*1024*1024*1024);
        const auto innerPath=ArchiveUtils::safePath(stage.path(),"xmcl-update.json");require(QFileInfo(innerPath).size()<=65536,"包内 manifest 过大");const auto inner=read(innerPath);
        for(const auto &key:QStringList{"schema","appId","version","platform","arch","entryPoint"})require(inner.value(key)==manifest.value(key),"包内应用标识或版本与 manifest 不一致："+key);
        const auto entry=ArchiveUtils::safePath(stage.path(),manifest.value("entryPoint").toString());require(QFileInfo(entry).isFile()&&QFileInfo(entry).size()>0,"更新入口文件缺失或为空");
        state["phase"]="prepared";state["details"]=stage.path();state["message"]="更新已校验并暂存，尚未应用。此版本没有自动替换 helper；请先备份、退出应用并由用户审阅安装。";state["verifiedAt"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);write(statePath(),state);stage.setAutoRemove(false);return state;
    } catch(...) {state["phase"]="failed";state.remove("stagePath");write(statePath(),state);throw;}
}
void MaintenanceService::execute(const QString &a,const QVariantMap &args) {
    try {
        if(a=="maintenance.logs"){
            QString root=localPath(args.value("path",m_root));require(QFileInfo(root).isDir()&&!QFileInfo(root).isSymLink(),"日志目录无效");QVariantList items;
            QDirIterator it(root,{"*.log","crash-*.txt","hs_err_pid*.log"},QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
            while(it.hasNext()&&items.size()<500){it.next();items.append(QVariantMap{{"title",it.fileName()},{"path",it.filePath()},{"bytes",it.fileInfo().size()}});}emit completed(a,{{"items",items},{"message","最多显示 500 个日志文件"}});return;
        }
        if(a=="maintenance.read"||a=="maintenance.diagnose"||a=="maintenance.export"){
            const auto text=readLog(localPath(args.value("path")));const auto findings=diagnose(text);const auto clean=redactLog(text);
            if(a=="maintenance.export"){
                const auto dest=localPath(args.value("destination"));require(!dest.isEmpty()&&!QFileInfo::exists(dest),"导出目标不能为空或覆盖已有文件");QFile file(dest);require(file.open(QIODevice::WriteOnly|QIODevice::NewOnly),"无法创建导出文件");file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
                const auto report=QJsonDocument::fromVariant(QVariantMap{{"schema",1},{"redaction","strict-allowlist"},{"log",clean},{"findings",findings},{"scope","最多分析末尾 8 MiB；建议是启发式证据，不代表已修复。未知原始行已隐藏。"}}).toJson();require(file.write(report)==report.size()&&file.flush(),"无法写入完整报告");emit completed(a,{{"message","隐私过滤报告已导出"},{"path",dest}});
            }else emit completed(a,{{"text",clean},{"details",clean},{"items",findings},{"findings",findings},{"message",findings.isEmpty()?"未识别到已知故障；不能据此确认启动正常。":"已找到排查线索，尚未执行修复。"}});return;
        }
        const auto base=m_root+"/maintenance";const auto configPath=base+"/config.json";
        if(a=="update.cancel"){m_download.cancel();emit completed(a,{{"message","已请求取消更新下载"}});return;}
        if(a=="update.status"){
            auto state=read(statePath(),true);state["download"]=m_download.state();state["config"]=read(configPath,true);state["restore"]=read(base+"/restore-state.json",true);QVariantList backups;for(const auto &id:QDir(base+"/backups").entryList(QDir::Dirs|QDir::NoDotAndDotDot)){auto path=ArchiveUtils::safePath(base+"/backups",id+"/backup.json");if(QFile::exists(path)){auto record=read(path);backups.append(QVariantMap{{"backupId",id},{"title",id},{"createdAt",record.value("createdAt")}});}}state["backups"]=backups;state["items"]=backups;state["details"]=QString::fromUtf8(QJsonDocument::fromVariant(state).toJson());state["message"]=state.value("message","没有待应用更新；自动替换未启用。");emit completed(a,state);return;
        }
        if(a=="update.configure"){
            require(!m_download.busy()&&!m_checking,"更新任务进行中，请稍后修改配置");const auto feed=args.value("feed").toString().trimmed(),hash=args.value("expectedSha256").toString().trimmed().toLower();require(https(QUrl(feed)),"更新源必须为不含凭据的 HTTPS URL");require(hash.isEmpty()||QRegularExpression("^[0-9a-f]{64}$").match(hash).hasMatch(),"预期 SHA-256 必须为 64 位十六进制");write(configPath,{{"feed",feed},{"expectedSha256",hash}});QFile::remove(base+"/checked-manifest.json");emit completed(a,{{"message","XMCL 更新源已保存；需重新检查更新"}});return;
        }
        if(a=="update.check"){
            require(!m_checking&&!m_download.busy(),"更新任务正在进行");const auto config=read(configPath,true);const QUrl feed(config.value("feed").toString());require(https(feed),"尚未配置 XMCL HTTPS 更新源");QNetworkRequest req(feed);req.setTransferTimeout(30000);req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);req.setRawHeader("User-Agent","XMCL native updater");auto *reply=m_network.get(req);m_checking=true;
            connect(reply,&QNetworkReply::readyRead,this,[reply]{if(reply->bytesAvailable()>1024*1024)reply->abort();});
            connect(reply,&QNetworkReply::finished,this,[this,a,reply,config,base]{m_checking=false;reply->deleteLater();try{const auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();require(reply->error()==QNetworkReply::NoError&&status==200,"XMCL 更新源请求失败（要求 HTTP 200，拒绝重定向）");const auto bytes=reply->readAll();require(bytes.size()<=1024*1024,"更新 manifest 超过 1 MiB");const auto doc=QJsonDocument::fromJson(bytes);require(doc.isObject(),"更新 manifest 必须为 JSON 对象");auto manifest=doc.object().toVariantMap();validateManifest(manifest,config.value("expectedSha256").toString());write(base+"/checked-manifest.json",manifest);const auto current=QCoreApplication::applicationVersion();const bool newer=QVersionNumber::fromString(manifest.value("version").toString())>QVersionNumber::fromString(current);emit completed(a,{{"manifest",manifest},{"details",QString::fromUtf8(QJsonDocument::fromVariant(manifest).toJson())},{"updateAvailable",newer},{"currentVersion",current},{"message",newer?"找到可审阅的 XMCL 更新 manifest":"来源版本不高于当前版本"}});}catch(const std::exception &e){emit failed(a,QString::fromUtf8(e.what()));}});return;
        }
        if(a=="update.download"){
            require(!m_download.busy()&&!m_checking,"更新任务正在进行");const auto manifest=read(base+"/checked-manifest.json");validateManifest(manifest,read(configPath,true).value("expectedSha256").toString());const auto meta=manifest.value("package").toMap();const auto path=base+"/downloads/"+meta.value("sha256").toString().toLower()+".package";
            write(statePath(),{{"phase","downloading"},{"manifest",manifest},{"applied",false}});
            const auto size=meta.value("bytes").toLongLong();auto limitConnection=std::make_shared<QMetaObject::Connection>();*limitConnection=connect(&m_download,&DownloadEngine::changed,this,[this,size](const QVariantMap &s){if(s.value("active").toBool()&&s.value("bytes").toLongLong()>size)m_download.cancel();});
            m_download.start({{QUrl(meta.value("url").toString()),path,meta.value("sha256").toString().toLatin1(),"sha256"}},[this,a,manifest,path,limitConnection](QString error){disconnect(*limitConnection);try{if(!error.isEmpty()){write(statePath(),{{"phase","download-failed"},{"applied",false}});emit failed(a,"更新包下载失败或已取消；未应用任何更新。");return;}emit completed(a,prepare(manifest,path));}catch(const std::exception &e){emit failed(a,QString::fromUtf8(e.what()));}});return;
        }
        if(a=="update.prepare"){require(!m_download.busy()&&!m_checking,"更新任务正在进行");const auto mp=localPath(args.value("manifest"));require(QFileInfo(mp).size()<=1024*1024,"更新 manifest 过大");emit completed(a,prepare(read(mp),localPath(args.value("package"))));return;}
        if(a=="update.backup"){
            const auto src=localPath(args.value("path"));require(QFileInfo(src).isDir()&&!QFileInfo(src).isSymLink(),"应用目录无效");require(QDir().mkpath(base+"/backups"),"无法创建备份目录");const auto id=uniqueId();const auto target=base+"/backups/"+id;QVariantMap record{{"backupId",id},{"createdAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"phase","copying"}};write(target+"/backup.json",record);
            try{ArchiveUtils::copyTree(src,target+"/files");const auto files=inventory(src);verifyInventory(target+"/files",files);record["files"]=files;record["phase"]="verified";write(target+"/backup.json",record);}catch(...){QDir(target).removeRecursively();throw;}emit completed(a,{{"backupId",id},{"details",id},{"message","应用目录已复制并校验；备份不修改当前应用。"}});return;
        }
        if(a=="update.rollback"){
            require(args.value("confirm").toBool(),"请确认恢复备份");
            const auto id=args.value("backupId").toString();require(QRegularExpression("^[a-fA-F0-9-]{36}$").match(id).hasMatch(),"备份 ID 无效");const auto source=ArchiveUtils::safePath(base+"/backups",id);const auto record=read(source+"/backup.json");require(record.value("phase")=="verified","备份尚未完成验证");const auto files=record.value("files").toList();verifyInventory(source+"/files",files);
            const auto dest=QDir(localPath(args.value("destination"))).absolutePath();require(!args.value("destination").toString().isEmpty()&&!QFileInfo::exists(dest)&&!QFileInfo(dest).isSymLink(),"恢复目录必须尚不存在，不能覆盖当前应用");const auto parent=QFileInfo(dest).absolutePath();require(QFileInfo(parent).isDir()&&!QFileInfo(parent).isSymLink(),"恢复目录的父目录无效");QTemporaryDir temp(parent+"/.xmcl-restore-XXXXXX");require(temp.isValid(),"无法创建恢复暂存目录");
            QVariantMap state{{"phase","restoring"},{"backupId",id},{"destination",dest},{"applied",false}};write(base+"/restore-state.json",state);
            try{ArchiveUtils::copyTree(source+"/files",temp.path()+"/files");verifyInventory(temp.path()+"/files",files);for(const auto &v:files){auto row=v.toMap();require(QFile::setPermissions(ArchiveUtils::safePath(temp.path()+"/files",row.value("path").toString()),QFileDevice::Permissions(row.value("permissions").toInt())),"无法恢复文件权限");}require(!QFileInfo::exists(dest)&&QDir().rename(temp.path()+"/files",dest),"恢复目标已存在或无法提交");state["phase"]="restored";state["message"]="已恢复到新的目录。当前应用未修改，请退出后人工检查并替换。";write(base+"/restore-state.json",state);emit completed(a,state);}catch(...){state["phase"]="failed";write(base+"/restore-state.json",state);throw;}return;
        }
        require(false,"未知维护操作");
    }catch(const std::exception &e){emit failed(a,QString::fromUtf8(e.what()));}
}
