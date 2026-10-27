#include "CommunityService.h"
#include "ServiceUtils.h"
#include "InstanceService.h"
#include "ArchiveUtils.h"
#include <QJsonArray>
#include <QUrlQuery>
#include <QTemporaryDir>
#include <QRegularExpression>
using namespace ServiceUtils;
CommunityService::CommunityService(QString root,QObject*p,QUrl apiBase):FeatureService(std::move(root),p),m_network(this),m_download(this),m_secrets(platformSecretStore(m_root+"/community")),m_apiBase(std::move(apiBase)){
    connect(&m_download,&DownloadEngine::changed,this,&CommunityService::downloadChanged);
}
QVariantMap CommunityService::downloadState()const{
    auto state=m_download.state();
    if(!m_installTitle.isEmpty())state["title"]=m_installTitle;
    if(!m_installKind.isEmpty())state["kind"]=m_installKind;
    if(!m_lastError.isEmpty()&&!state.value("active").toBool())state["error"]=m_lastError;
    if(m_preparing&&!m_download.busy()){state["active"]=true;state["phase"]="metadata";state["canPause"]=false;}
    else if(m_download.busy()){state["phase"]=m_download.state().value("paused").toBool()?"paused":"downloading";state["canPause"]=true;}
    else if(!state.value("error").toString().isEmpty()){state["phase"]="error";state["canRetry"]=true;state["canPause"]=false;}
    else if(state.value("totalFiles").toInt()>0&&state.value("completedFiles").toInt()==state.value("totalFiles").toInt()){state["phase"]="complete";}
    return state;
}
void CommunityService::updateInstallState(){emit downloadChanged(downloadState());}
QVariantList CommunityService::actions()const{auto provider=field("provider","平台","choice","modrinth",false,{"modrinth","curseforge"});auto game=field("gameVersion","Minecraft 版本","text","1.21.1");auto loader=field("loader","加载器","choice","fabric",false,{"fabric","forge","neoforge","quilt","vanilla"});return {
    action("community.search","搜索社区资源",{provider,field("query","关键词","text",{},true),field("kind","资源类别","choice","mod",false,{"mod","resourcepack","shader","modpack","datapack"}),game,loader}),
    action("community.versions","查看项目文件",{provider,field("projectId","项目 ID","text",{},true),game,loader}),
    action("community.install","安装选中文件",{provider,field("instanceId","实例 ID","text",{},true),field("projectId","项目 ID（CurseForge 需要）"),field("versionId","文件 / 版本 ID","text",{},true),field("kind","目标资源类型","choice","mods",false,{"mods","resourcepacks","shaderpacks","datapacks"})},"下载完成并校验后写入；已有同名文件会拒绝覆盖。Modrinth 按实例版本与加载器解析必需依赖。"),
    action("community.key","保存 CurseForge API Key",{field("key","你的 CurseForge API Key","password",{},true)},"密钥保存在系统钥匙串。未配置时可以使用 Modrinth。"),
    action("community.pause","暂停社区下载"),action("community.resume","继续社区下载"),action("community.cancel","取消社区下载"),action("community.retry","重试上次社区下载")};}
void CommunityService::request(const QString&a,QUrl url,std::function<void(QJsonDocument)>callback,bool curse){
    if(url.host()=="api.modrinth.com"&&m_apiBase!=QUrl("https://api.modrinth.com")){auto path=url.path();auto query=url.query();url=m_apiBase;url.setPath(path);url.setQuery(query);}
    if(!https(url,true)){if(a=="community.install"){m_preparing=false;m_lastError="社区请求只允许 HTTPS";updateInstallState();}emit failed(a,"社区请求只允许 HTTPS");return;}QNetworkRequest req(url);req.setRawHeader("User-Agent","XMCL/0.2 (native launcher)");req.setTransferTimeout(30000);req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    if(curse){QString error;auto key=m_secrets->get("curseforge",&error);if(key.isEmpty()){if(a=="community.install"){m_preparing=false;m_lastError="请先配置自己的 CurseForge API Key";updateInstallState();}emit failed(a,"请先配置自己的 CurseForge API Key");return;}req.setRawHeader("x-api-key",key);}
    const auto generation=m_generation;auto*reply=m_network.get(req);connect(reply,&QNetworkReply::downloadProgress,this,[reply](qint64 count,qint64){if(count>16*1024*1024)reply->abort();});connect(reply,&QNetworkReply::finished,this,[this,a,reply,callback,generation]{if(a=="community.install"&&generation!=m_generation){reply->deleteLater();return;}auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();auto doc=QJsonDocument::fromJson(reply->readAll());reply->deleteLater();if(reply->error()!=QNetworkReply::NoError||status<200||status>=300||doc.isNull()){const auto message=QString("社区服务请求失败（HTTP %1）").arg(status);if(a=="community.install"){m_preparing=false;m_lastError=message;updateInstallState();}emit failed(a,message);return;}try{callback(doc);}catch(const std::exception&e){const auto message=QString::fromUtf8(e.what());if(a=="community.install"){m_preparing=false;m_lastError=message;updateInstallState();}emit failed(a,message);}});
}
void CommunityService::execute(const QString&a,const QVariantMap&args){bool started=false;try{
    if(a=="community.key"){const auto key=args.value("key").toString().trimmed();require(!key.isEmpty(),"密钥不能为空");QString error;require(m_secrets->put("curseforge",key.toUtf8(),&error),error);emit completed(a,{{"message","API Key 已保存至系统钥匙串"}});return;}
    if(a=="community.pause"||a=="community.resume"){if(a=="community.pause")m_download.pause();else m_download.resume();emit completed(a,{{"message","社区下载状态已更新"}});return;}
    if(a=="community.cancel"){
        ++m_generation;const bool wasRunning=m_preparing||m_download.busy();m_preparing=false;if(wasRunning)m_lastError="已取消社区下载";m_download.cancel();updateInstallState();
        emit completed(a,{{"message",wasRunning?"已取消社区下载":"没有正在进行的社区下载"}});return;
    }
    if(a=="community.retry"){
        require(!m_preparing&&!m_download.busy(),"社区资源下载正在进行");require(!m_retryArguments.isEmpty(),"没有可重试的社区下载");
        require(!m_lastError.isEmpty()||!m_download.state().value("error").toString().isEmpty(),"上次社区下载没有失败，无需重试");
        const auto retry=m_retryArguments;m_lastError.clear();execute("community.install",retry);return;
    }
    const bool curse=args.value("provider","modrinth")=="curseforge";const auto game=args.value("gameVersion").toString(),loader=args.value("loader").toString();
    if(a=="community.search"){
        QUrl url(curse?"https://api.curseforge.com/v1/mods/search":"https://api.modrinth.com/v2/search");QUrlQuery query;
        if(curse){query.addQueryItem("gameId","432");query.addQueryItem("searchFilter",args.value("query").toString());query.addQueryItem("pageSize","30");if(!game.isEmpty())query.addQueryItem("gameVersion",game);const QMap<QString,QString> categories{{"mod","6"},{"resourcepack","12"},{"modpack","4471"},{"shader","6552"},{"datapack","6945"}};query.addQueryItem("classId",categories.value(args.value("kind","mod").toString(),"6"));}
        else{query.addQueryItem("query",args.value("query").toString());query.addQueryItem("limit","30");QJsonArray facets{QJsonArray{"project_type:"+args.value("kind","mod").toString()}};if(!game.isEmpty())facets.append(QJsonArray{"versions:"+game});if(!loader.isEmpty()&&loader!="vanilla"&&args.value("kind","mod")=="mod")facets.append(QJsonArray{"categories:"+loader});query.addQueryItem("facets",QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));}url.setQuery(query);
        request(a,url,[this,a,curse](QJsonDocument doc){QVariantList items;const auto array=doc.object().value(curse?"data":"hits").toArray();for(auto v:array){auto m=v.toObject();QString id=curse?QString::number(m.value("id").toInteger()):m.value("project_id").toString();items.append(QVariantMap{{"id",id},{"projectId",id},{"title",m.value(curse?"name":"title").toString()},{"subtitle",m.value(curse?"summary":"description").toString()},{"provider",curse?"curseforge":"modrinth"}});}emit completed(a,{{"items",items},{"message",QString("找到 %1 个项目").arg(items.size())}});},curse);return;
    }
    auto project=args.value("projectId").toString();require(project.isEmpty()||QRegularExpression("^[A-Za-z0-9_-]+$").match(project).hasMatch(),"项目 ID 无效");
    if(a=="community.versions"){
        require(!project.isEmpty(),"请选择项目");QUrl url(curse?"https://api.curseforge.com/v1/mods/"+project+"/files":"https://api.modrinth.com/v2/project/"+project+"/version");QUrlQuery query;if(curse){if(!game.isEmpty())query.addQueryItem("gameVersion",game);query.addQueryItem("pageSize","50");}else{if(!game.isEmpty())query.addQueryItem("game_versions",QString::fromUtf8(QJsonDocument(QJsonArray{game}).toJson(QJsonDocument::Compact)));if(!loader.isEmpty()&&loader!="vanilla")query.addQueryItem("loaders",QString::fromUtf8(QJsonDocument(QJsonArray{loader}).toJson(QJsonDocument::Compact)));}url.setQuery(query);
        request(a,url,[this,a,curse,project](QJsonDocument doc){QVariantList items;auto versions=curse?doc.object().value("data").toArray():doc.array();for(auto v:versions){auto m=v.toObject();QString id=curse?QString::number(m.value("id").toInteger()):m.value("id").toString();items.append(QVariantMap{{"id",id},{"versionId",id},{"projectId",project},{"title",m.value(curse?"displayName":"name").toString()},{"subtitle",m.value(curse?"fileDate":"date_published").toString().left(10)},{"provider",curse?"curseforge":"modrinth"}});}emit completed(a,{{"items",items},{"message","选择文件后切换到安装操作"}});},curse);return;
    }
    require(a=="community.install","未知社区操作");require(!m_preparing&&!m_download.busy(),"已有社区资源下载正在进行");const auto versionId=args.value("versionId").toString();require(QRegularExpression("^[A-Za-z0-9_-]+$").match(versionId).hasMatch(),"文件 ID 无效");const auto instanceId=args.value("instanceId").toString();const auto kind=args.value("kind","mods").toString();require(QStringList{"mods","resourcepacks","shaderpacks","datapacks"}.contains(kind),"资源类型无效");
    m_retryArguments=args;m_installKind=kind;m_installTitle=args.value("title").toString().isEmpty()?QString("社区资源 · "+kind):args.value("title").toString();m_lastError.clear();
    ++m_generation;m_preparing=true;started=true;updateInstallState();
    if(!curse){installModrinth(instanceId,versionId,kind);return;}
    require(QRegularExpression("^[0-9]+$").match(project).hasMatch(),"CurseForge 项目 ID 需为数字");
    request(a,QUrl("https://api.curseforge.com/v1/mods/"+project+"/files/"+versionId),[this,a,instanceId,kind](QJsonDocument doc){auto file=doc.object().value("data").toObject();const auto url=QUrl(file.value("downloadUrl").toString());require(https(url),"作者禁止第三方下载，请前往项目网站获取文件");InstanceService instances(m_root);const auto target=ArchiveUtils::safePath(instances.gamePath(instanceId),kind+'/'+file.value("fileName").toString());require(!QFile::exists(target),"目标文件已存在，拒绝覆盖");QByteArray hash;for(auto v:file.value("hashes").toArray())if(v.toObject().value("algo").toInt()==1)hash=v.toObject().value("value").toString().toLatin1();require(!hash.isEmpty(),"下载元数据缺少 SHA-1");m_download.start({{url,target,hash,"sha1"}},[this,a](QString error){m_preparing=false;m_lastError=error;updateInstallState();if(error.isEmpty())emit completed(a,{{"message","资源已校验并安装；请按项目说明检查必需依赖"}});else emit failed(a,error);});},true);
}catch(const std::exception&e){const auto message=QString::fromUtf8(e.what());if(started){m_preparing=false;m_lastError=message;updateInstallState();}emit failed(a,message);}}
void CommunityService::installModrinth(const QString&instanceId,const QString&versionId,const QString&kind){
    InstanceService instances(m_root);const auto game=instances.gamePath(instanceId);const auto metadata=read(instances.instancePath(instanceId)+"/instance.json");const auto gameVersion=metadata.value("version").toString(),loader=metadata.value("loader","vanilla").toString().toLower();const QString action="community.install";
    auto stage=std::make_shared<QTemporaryDir>(m_root+"/community-XXXXXX");require(stage->isValid(),"无法创建资源安装临时目录");
    struct Plan{QList<Transfer> jobs;QStringList targets;QSet<QString> visited;QMap<QString,QString> projectVersions;};auto plan=std::make_shared<Plan>();auto queue=std::make_shared<QStringList>();queue->append(versionId);
    auto step=std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakStep=step;
    *step=[this,stage,plan,queue,weakStep,game,kind,action,gameVersion,loader]{
        auto step=weakStep.lock(); if(!step)return;
        if(queue->isEmpty()){
            auto jobs=plan->jobs;m_download.start(jobs,[this,stage,plan,action](QString error){m_preparing=false;m_lastError=error;if(!error.isEmpty()){updateInstallState();emit failed(action,error);return;}QStringList committed;try{for(qsizetype i=0;i<plan->jobs.size();++i){const auto target=plan->targets[i];require(!QFile::exists(target),"目标文件在下载期间发生变化");require(QDir().mkpath(QFileInfo(target).absolutePath())&&QFile::rename(plan->jobs[i].destination,target),"无法提交资源文件");committed<<target;}updateInstallState();emit completed(action,{{"message",QString("已安装 %1 个文件（包含必需依赖）").arg(committed.size())}});}catch(const std::exception&e){for(const auto&p:committed)QFile::remove(p);m_lastError=QString::fromUtf8(e.what());updateInstallState();emit failed(action,m_lastError);}});return;
        }
        const auto current=queue->takeFirst();if(plan->visited.contains(current)){(*step)();return;}plan->visited.insert(current);if(plan->visited.size()>100){m_preparing=false;m_lastError="依赖数量超过安全上限";updateInstallState();emit failed(action,m_lastError);return;}
        if(current.startsWith("project:")){
            const auto project=current.mid(8);QUrl url("https://api.modrinth.com/v2/project/"+project+"/version");QUrlQuery query;query.addQueryItem("game_versions",QString::fromUtf8(QJsonDocument(QJsonArray{gameVersion}).toJson(QJsonDocument::Compact)));if(loader!="vanilla")query.addQueryItem("loaders",QString::fromUtf8(QJsonDocument(QJsonArray{loader}).toJson(QJsonDocument::Compact)));url.setQuery(query);
            request(action,url,[this,queue,step,project](QJsonDocument doc){require(doc.isArray()&&!doc.array().isEmpty(),"未找到兼容的必需依赖："+project);const auto id=doc.array().first().toObject().value("id").toString();require(QRegularExpression("^[A-Za-z0-9_-]+$").match(id).hasMatch(),"依赖版本标识无效");queue->prepend(id);(*step)();});return;
        }
        request(action,QUrl("https://api.modrinth.com/v2/version/"+current),[this,stage,plan,queue,step,game,kind,action,gameVersion,loader](QJsonDocument doc){auto version=doc.object();
            require(version.value("game_versions").toArray().contains(gameVersion),"选中文件与实例 Minecraft 版本不兼容");
            if(kind=="mods")require(version.value("loaders").toArray().contains(loader),"选中文件与实例加载器不兼容");
            const auto project=version.value("project_id").toString(),versionId=version.value("id").toString();require(!plan->projectVersions.contains(project)||plan->projectVersions.value(project)==versionId,"必需依赖存在版本冲突："+project);plan->projectVersions[project]=versionId;QJsonObject file;auto files=version.value("files").toArray();for(auto v:files)if(v.toObject().value("primary").toBool()){file=v.toObject();break;}if(file.isEmpty()&&files.size()==1)file=files.first().toObject();require(!file.isEmpty(),"项目需要手动选择文件，无法确定主文件");const auto name=file.value("filename").toString();require(!name.contains('/')&&!name.contains('\\'),"社区文件名无效");const auto target=ArchiveUtils::safePath(game,kind+'/'+name);const auto hash=file.value("hashes").toObject().value("sha512").toString().toLatin1();require(hash.size()==128,"资源缺少 SHA-512 校验");
            if(QFile::exists(target)){require(DownloadEngine::verified(target,hash,"sha512"),"已有同名但不同内容的资源："+name);}else{require(!plan->targets.contains(target),"依赖产生同名文件冲突");plan->jobs.append({QUrl(file.value("url").toString()),stage->path()+'/'+name,hash,"sha512"});plan->targets.append(target);}
            for(auto dep:version.value("dependencies").toArray()){auto d=dep.toObject();if(d.value("dependency_type").toString()!="required")continue;const auto exact=d.value("version_id").toString(),project=d.value("project_id").toString();require(!exact.isEmpty()||!project.isEmpty(),"依赖缺少项目及版本标识");const auto key=exact.isEmpty()?project:exact;require(QRegularExpression("^[A-Za-z0-9_-]+$").match(key).hasMatch(),"依赖标识无效");queue->append(exact.isEmpty()?"project:"+project:exact);}(*step)();
        });
    };(*step)();
}
