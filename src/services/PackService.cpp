#include "PackService.h"
#include "ArchiveUtils.h"
#include "ServiceUtils.h"
#include <QJsonArray>
#include <QDirIterator>
#include <QUuid>
#include <QRegularExpression>
using namespace ServiceUtils;
namespace {
void identifier(const QString &value){require(QRegularExpression("^[A-Za-z0-9][A-Za-z0-9._-]{0,119}$").match(value).hasMatch(),"整合包版本标识无效");}
}
PackService::PackService(QString root,QObject *parent):FeatureService(std::move(root),parent),m_network(this),m_download(this),m_secrets(platformSecretStore(m_root+"/community")){
    connect(&m_download,&DownloadEngine::changed,this,&PackService::downloadChanged);
}
QVariantList PackService::actions()const{return {
    action("pack.import","导入整合包",{field("path","Modrinth .mrpack / CurseForge .zip","path",{},true),field("name","新实例名称（留空使用包名）"),field("optional","包含可选客户端文件","bool",false)},"校验所有资源后创建独立实例；随后在版本面板安装包指定的游戏与加载器。CurseForge 需要先在社区资源保存 API Key。"),
    action("pack.pause","暂停整合包下载"),action("pack.resume","继续整合包下载"),action("pack.cancel","取消导入"),action("pack.retry","重试上次整合包导入")};}
QVariantMap PackService::downloadState()const{
    auto state=m_download.state();
    if(!m_lastError.isEmpty()&&!state.value("active").toBool())state["error"]=m_lastError;
    if(m_import&&!m_download.busy()){state["active"]=true;state["phase"]="metadata";state["canPause"]=false;}
    else if(m_download.busy()){state["phase"]="downloading";state["canPause"]=true;}
    else if(!state.value("error").toString().isEmpty()){state["phase"]="error";state["canRetry"]=true;state["canPause"]=false;}
    else if(state.value("totalFiles").toInt()>0&&state.value("completedFiles").toInt()==state.value("totalFiles").toInt())state["phase"]="complete";
    return state;
}
void PackService::fail(const QString &error){if(m_import)m_import->cancelled=true;m_import.reset();m_lastError=error;emit downloadChanged(downloadState());emit failed("pack.import",error);}
void PackService::overlay(const QString &source,const QString &target){
    if(!QFileInfo::exists(source))return;
    require(QFileInfo(source).isDir()&&!QFileInfo(source).isSymLink(),"整合包覆盖目录无效");
    QDirIterator it(source,QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(it.hasNext()){it.next();auto info=it.fileInfo();require(!info.isSymLink(),"覆盖文件不能是符号链接");auto path=ArchiveUtils::safePath(target,QDir(source).relativeFilePath(info.filePath()));if(info.isDir()){require(QDir().mkpath(path),"无法创建覆盖目录");continue;}require(info.isFile(),"覆盖文件类型无效");require(QDir().mkpath(QFileInfo(path).absolutePath()),"无法创建文件目录");QFile in(info.filePath());QSaveFile out(path);require(in.open(QIODevice::ReadOnly)&&out.open(QIODevice::WriteOnly),"无法写入覆盖文件");while(!in.atEnd()){auto b=in.read(1024*1024);require(!b.isEmpty()&&out.write(b)==b.size(),"复制覆盖文件失败");}require(out.commit(),"提交覆盖文件失败");}
}
void PackService::execute(const QString &a,const QVariantMap &args){try{
    if(a=="pack.pause"||a=="pack.resume"){if(a=="pack.pause")m_download.pause();else m_download.resume();emit completed(a,{{"message","整合包下载状态已更新"}});return;}
    if(a=="pack.cancel"){if(m_import)m_import->cancelled=true;m_import.reset();m_lastError="导入已取消";m_download.cancel();emit downloadChanged(downloadState());emit completed(a,{{"message","导入已取消，现有实例未改变"}});return;}
    if(a=="pack.retry"){
        require(!m_import&&!m_download.busy(),"整合包导入正在进行");require(!m_lastArguments.isEmpty(),"没有可重试的整合包导入");
        require(!m_lastError.isEmpty()||!m_download.state().value("error").toString().isEmpty(),"上次整合包导入没有失败，无需重试");
        const auto retry=m_lastArguments;m_lastError.clear();execute("pack.import",retry);return;
    }
    require(a=="pack.import","未知整合包操作");require(!m_import&&!m_download.busy(),"整合包导入正在进行");
    require(QDir().mkpath(m_root),"无法创建工作目录");auto work=std::make_shared<Import>();work->stage=std::make_shared<QTemporaryDir>(m_root+"/pack-XXXXXX");require(work->stage->isValid(),"无法创建整合包临时目录");
    const auto extracted=work->stage->path()+"/source",game=work->stage->path()+"/instance/game";ArchiveUtils::extract(localPath(args.value("path")),extracted);require(QDir().mkpath(game),"无法创建临时实例");
    QVariantMap deps;QString packName;
    if(QFile::exists(extracted+"/modrinth.index.json")){
        const auto manifest=read(extracted+"/modrinth.index.json");require(manifest.value("formatVersion").toInt()==1&&manifest.value("game")=="minecraft","不支持的 Modrinth 整合包格式");deps=manifest.value("dependencies").toMap();packName=manifest.value("name").toString();
        require(manifest.value("files").metaType().id()==QMetaType::QVariantList,"整合包 files 不是列表");auto files=manifest.value("files").toList();require(files.size()<=10000,"整合包文件数量过多");
        for(const auto &entry:files){auto file=entry.toMap();const auto env=file.value("env").toMap().value("client","required").toString();require(QStringList{"required","optional","unsupported"}.contains(env),"客户端环境标识无效");if(env=="unsupported"||(env=="optional"&&!args.value("optional").toBool()))continue;
            const auto path=ArchiveUtils::safePath(game,file.value("path").toString());require(!work->paths.contains(path),"整合包包含重复路径");work->paths.insert(path);const auto hash=file.value("hashes").toMap().value("sha512").toString();require(QRegularExpression("^[a-fA-F0-9]{128}$").match(hash).hasMatch(),"整合包文件缺少有效 SHA-512");
            QUrl url;for(const auto &candidate:file.value("downloads").toList()){const QUrl value(candidate.toString());if(https(value)&&QStringList{"cdn.modrinth.com","github.com","raw.githubusercontent.com","gitlab.com"}.contains(value.host().toLower())){url=value;break;}}require(!url.isEmpty(),"整合包文件没有受支持的 HTTPS 下载源");work->jobs.append({url,path,hash.toLatin1(),"sha512"});
        }
        work->overrides={extracted+"/overrides",extracted+"/client-overrides"};
    }else{
        const auto manifest=read(extracted+"/manifest.json");require(manifest.value("manifestType")=="minecraftModpack"&&manifest.value("manifestVersion").toInt()==1,"不支持的 CurseForge 整合包格式");packName=manifest.value("name").toString();const auto mc=manifest.value("minecraft").toMap();deps["minecraft"]=mc.value("version");
        for(const auto &entry:mc.value("modLoaders").toList()){const auto loader=entry.toMap();if(!loader.value("primary").toBool())continue;const auto id=loader.value("id").toString();const auto kind=id.section('-',0,0);require(QStringList{"forge","neoforge","fabric","quilt"}.contains(kind),"不支持的整合包加载器");deps[kind=="fabric"||kind=="quilt"?kind+"-loader":kind]=id.mid(kind.size()+1);}
        require(manifest.value("files").metaType().id()==QMetaType::QVariantList,"整合包 files 不是列表");for(const auto &entry:manifest.value("files").toList()){auto file=entry.toMap();if(file.value("required",true).toBool()||args.value("optional").toBool())work->curseFiles.append(QJsonObject::fromVariantMap(file));}require(work->curseFiles.size()<=10000,"整合包文件数量过多");
        const auto overrides=manifest.value("overrides","overrides").toString();work->overrides={ArchiveUtils::safePath(extracted,overrides)};
    }
    const auto mc=deps.take("minecraft").toString();identifier(mc);QString loader="vanilla",loaderVersion;require(deps.size()<=1,"整合包要求多个或不支持的加载器");for(auto it=deps.begin();it!=deps.end();++it){require(QStringList{"fabric-loader","quilt-loader","forge","neoforge"}.contains(it.key()),"未知整合包依赖："+it.key());loader=it.key().section('-',0,0);loaderVersion=it.value().toString();identifier(loaderVersion);}
    auto name=args.value("name").toString().trimmed();if(name.isEmpty())name=packName;require(!name.isEmpty()&&name.size()<=100&&!name.contains(QChar(0)),"实例名称无效");
    work->metadata={{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"name",name},{"version",mc},{"loader",loader},{"loaderVersion",loaderVersion},{"isolated",true},{"installed",false}};
    m_import=work;m_lastArguments=args;m_lastError.clear();emit downloadChanged(downloadState());emit activity("正在解析整合包文件，现有实例保持不变");resolveCurse(work);
}catch(const std::exception&e){emit failed(a,QString::fromUtf8(e.what()));}}
void PackService::resolveCurse(const std::shared_ptr<Import>&work){
    if(work->cancelled)return;if(work->next==work->curseFiles.size()){download(work);return;}
    const auto file=work->curseFiles[work->next++].toObject();const auto project=file.value("projectID").toInteger(),id=file.value("fileID").toInteger();if(project<=0||id<=0){fail("整合包项目或文件 ID 无效");return;}
    QString error;const auto key=m_secrets->get("curseforge",&error);if(key.isEmpty()){fail("请先在社区资源模块保存你的 CurseForge API Key");return;}
    QNetworkRequest request(QUrl(QString("https://api.curseforge.com/v1/mods/%1/files/%2").arg(project).arg(id)));request.setRawHeader("x-api-key",key);request.setRawHeader("User-Agent","XMCL/0.2");request.setTransferTimeout(30000);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    auto *reply=m_network.get(request);connect(reply,&QNetworkReply::downloadProgress,this,[reply](qint64 bytes,qint64){if(bytes>4*1024*1024)reply->abort();});connect(reply,&QNetworkReply::finished,this,[this,work,reply]{reply->deleteLater();if(work->cancelled)return;try{require(reply->error()==QNetworkReply::NoError,"CurseForge 文件解析失败");auto object=QJsonDocument::fromJson(reply->readAll()).object().value("data").toObject();const auto name=object.value("fileName").toString();require(!name.contains('/')&&!name.contains('\\'),"无效的社区文件名");const auto target=ArchiveUtils::safePath(work->stage->path()+"/instance/game","mods/"+name);require(!work->paths.contains(target),"整合包包含重复文件");work->paths.insert(target);const auto url=QUrl(object.value("downloadUrl").toString());require(https(url),"作者禁止第三方下载该文件，无法自动导入");QByteArray hash;for(const auto &v:object.value("hashes").toArray())if(v.toObject().value("algo").toInt()==1)hash=v.toObject().value("value").toString().toLatin1();require(QRegularExpression("^[a-fA-F0-9]{40}$").match(QString::fromLatin1(hash)).hasMatch(),"文件缺少 SHA-1 校验");work->jobs.append({url,target,hash,"sha1"});resolveCurse(work);}catch(const std::exception&e){fail(QString::fromUtf8(e.what()));}});
}
void PackService::download(const std::shared_ptr<Import>&work){
    if(work->cancelled)return;m_download.start(work->jobs,[this,work](QString error){if(work->cancelled)return;if(!error.isEmpty()){fail(error);return;}try{
        const auto staged=work->stage->path()+"/instance";for(const auto &source:work->overrides)overlay(source,staged+"/game");write(staged+"/instance.json",work->metadata);
        const auto id=work->metadata.value("id").toString();const auto target=ArchiveUtils::safePath(m_root,"instances/"+id);require(QDir().mkpath(QFileInfo(target).absolutePath())&&!QFile::exists(target)&&QDir().rename(staged,target),"无法提交整合包实例");m_import.reset();m_lastError.clear();emit downloadChanged(downloadState());auto data=work->metadata;data["instanceId"]=id;data["message"]="整合包已导入。前往版本与启动，按以下版本安装游戏。";data["details"]=work->metadata.value("version").toString()+" / "+work->metadata.value("loader").toString()+" "+work->metadata.value("loaderVersion").toString();emit completed("pack.import",data);
    }catch(const std::exception&e){fail(QString::fromUtf8(e.what()));}});
}
