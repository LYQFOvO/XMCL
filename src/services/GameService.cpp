#include "GameService.h"
#include "InstanceService.h"
#include "ArchiveUtils.h"
#include "ServiceUtils.h"
#include "MemoryAdvisor.h"
#include "JavaRuntimeService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QSysInfo>
#include <QXmlStreamReader>
#include <QDateTime>
#include <QRegularExpression>
#include <QDirIterator>
#include <QTemporaryDir>
#include <QTimer>
using namespace ServiceUtils;
namespace {
QString osName(){
#if defined(Q_OS_MACOS)
return "osx";
#elif defined(Q_OS_WIN)
return "windows";
#else
return "linux";
#endif
}
void safeVersion(const QString &v){require(QRegularExpression("^[A-Za-z0-9][A-Za-z0-9._-]{0,119}$").match(v).hasMatch()&&v!="..","版本标识无效");}
QString mavenPath(const QString &name){auto ext=name.section('@',1);if(ext.isEmpty())ext="jar";auto parts=name.section('@',0,0).split(':');require(parts.size()>=3&&parts.size()<=4,"Maven 坐标无效");QString group=parts[0];group.replace('.','/');return group+'/'+parts[1]+'/'+parts[2]+'/'+parts[1]+'-'+parts[2]+(parts.size()==4?'-'+parts[3]:QString())+'.'+ext;}
QString subst(QString s,const QVariantMap&vars){for(auto i=vars.begin();i!=vars.end();++i)s.replace("${"+i.key()+"}",i.value().toString());return s;}
}
GameService::GameService(QString root,QObject*p,QUrl manifestUrl):FeatureService(std::move(root),p),m_network(this),m_download(this),m_game(this),m_installer(this),m_manifestUrl(std::move(manifestUrl)){
    connect(&m_download,&DownloadEngine::changed,this,[this](const QVariantMap&){emit downloadChanged(downloadState());});
    connect(&m_game,&QProcess::started,this,[this]{emit gameStateChanged(true);emit completed("game.launch",{{"message","Minecraft 进程已启动"}});});
    connect(&m_game,&QProcess::readyReadStandardOutput,this,[this]{auto bytes=m_game.readAllStandardOutput();QFile log(m_root+"/logs/game.log");if(log.open(QIODevice::Append))log.write(bytes);});
    m_game.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_game,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){emit gameStateChanged(false);emit activity(QString("游戏进程退出，代码 %1；日志：%2/logs/game.log").arg(code).arg(m_root));});
    connect(&m_game,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){emit failed("game.launch",m_game.errorString());});
}
QVariantList GameService::actions()const{
    auto id=field("instanceId","实例 ID","text",{},true);auto ver=field("version","Minecraft 版本","text","1.21.1",true);auto loader=field("loader","加载器","choice","vanilla",false,{"vanilla","fabric","forge","neoforge","quilt"});
    return {action("version.list","获取 Minecraft 版本",{field("type","类型","choice","release",false,{"release","snapshot","old_beta","old_alpha","all"}),field("query","搜索版本")}),action("loader.list","获取加载器版本",{ver,loader}),action("game.install","安装游戏版本",{id,ver,loader,field("loaderVersion","加载器版本（原版留空）")},"将真实下载游戏、库和资源。Forge / NeoForge 需要可用的 Java。"),action("authlib.prepare","准备外置认证组件",{},"从 authlib-injector 官方源下载并校验 SHA-256。"),action("java.scan","扫描 Java"),action("launch.check","启动前检查",{id},"检查实例的 Java 匹配、客户端/库/资源等文件是否缺失或大小不符，并给出可修复项。"),action("launch.repair","修复缺失文件",{id},"只下载缺失或大小不符的文件，不删除未知模组或用户文件。"),action("diagnostic.analyze","启动诊断",{id},"根据最近游戏日志与启动环境给出有证据的原因与建议。"),action("game.launch","启动游戏",{id,field("server","连接服务器（可留空）")}),action("game.stop","结束游戏",{},"向正在运行的游戏进程发送结束请求。",true),action("download.status","当前下载任务"),action("download.pause","暂停下载"),action("download.resume","继续下载"),action("download.cancel","取消下载")};
}
bool GameService::allowed(const QJsonArray &rules){if(rules.isEmpty())return true;bool yes=false;for(auto v:rules){auto rule=v.toObject();auto os=rule.value("os").toObject();bool match=true;if(os.contains("name"))match=match&&os.value("name").toString()==osName();if(os.contains("arch")){auto arch=os.value("arch").toString();match=match&&(arch==QSysInfo::currentCpuArchitecture()||(arch=="x86_64"&&QSysInfo::currentCpuArchitecture()=="x86_64"));}if(os.contains("version"))match=match&&QRegularExpression(os.value("version").toString()).match(QSysInfo::kernelVersion()).hasMatch();const auto features=rule.value("features").toObject();for(auto i=features.begin();i!=features.end();++i)if(i.value().toBool())match=false;if(match)yes=rule.value("action").toString()=="allow";}return yes;}
QStringList GameService::arguments(const QJsonValue &value,const QVariantMap &vars){QStringList args;for(auto v:value.toArray()){if(v.isString())args<<subst(v.toString(),vars);else{auto obj=v.toObject();if(!allowed(obj.value("rules").toArray()))continue;auto val=obj.value("value");if(val.isArray())for(auto part:val.toArray())args<<subst(part.toString(),vars);else args<<subst(val.toString(),vars);}}return args;}
void GameService::get(const QUrl &url,std::function<void(QByteArray)>callback,const QString &a,QByteArray expected){
    // 元数据/校验文件读取。镜像节点抖动时先有限退避重试，仍失败则回退官方地址。
    const QUrl official=url;
    const QUrl mapped=mapDownloadUrl(url);
    if(!https(mapped,true)){emit failed(a,"不安全的网络地址");return;}
    const auto generation=m_installGeneration;
    const auto catalogRequest=++m_catalogRequests[a];
    auto attempt=std::make_shared<std::function<void(int,bool)>>();
    *attempt=[this,official,mapped,a,callback,expected,generation,catalogRequest,attempt](int tryIndex,bool fallback){
        const QUrl target=(fallback||m_preferOfficialFallback)?official:mapped;
        QNetworkRequest req(target);req.setRawHeader("User-Agent","XMCL/0.2");req.setTransferTimeout(30000);req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        auto*r=m_network.get(req);
        connect(r,&QNetworkReply::downloadProgress,this,[r](qint64 n,qint64){if(n>32*1024*1024)r->abort();});
        connect(r,&QNetworkReply::finished,this,[this,r,a,callback,expected,generation,catalogRequest,attempt,tryIndex,fallback,mapped,official]{
            if((a=="version.list"||a=="loader.list")&&catalogRequest!=m_catalogRequests.value(a)){r->deleteLater();return;}
            if(a=="game.install"&&generation!=m_installGeneration){r->deleteLater();return;}
            const auto status=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto networkError=r->error();
            const auto errorText=r->errorString();
            auto bytes=r->readAll();r->deleteLater();
            const bool transient=(networkError!=QNetworkReply::NoError&&networkError!=QNetworkReply::OperationCanceledError)
                ||status==408||status==429||status==500||status==502||status==503||status==504;
            if(transient&&tryIndex<2){QTimer::singleShot(700*(tryIndex+1),this,[attempt,tryIndex,fallback]{(*attempt)(tryIndex+1,fallback);});return;}
            const auto fail=[this,a](const QString &message){if(a=="game.install")installationError(message);else emit failed(a,message);};
            const bool failed=networkError!=QNetworkReply::NoError||status<200||status>=300;
            if(failed&&!fallback&&mapped!=official){
                emit activity("镜像下载失败，改用官方源重试");
                m_preferOfficialFallback=true;
                (*attempt)(0,true);return;
            }
            if(failed){fail(QString("网络请求失败（HTTP %1）：%2").arg(status).arg(errorText));return;}
            try{require(expected.isEmpty()||QCryptographicHash::hash(bytes,QCryptographicHash::Sha1).toHex()==expected.toLower(),"元数据校验失败");callback(bytes);}catch(const std::exception&e){fail(QString::fromUtf8(e.what()));}
        });
    };
    (*attempt)(0,false);
}
void GameService::json(const QUrl&url,std::function<void(QJsonObject)>cb,const QString&a,QByteArray expected){get(url,[cb](QByteArray data){QJsonParseError e;auto doc=QJsonDocument::fromJson(data,&e);require(e.error==QJsonParseError::NoError&&doc.isObject(),"服务器返回的 JSON 无效");cb(doc.object());},a,expected);}
Transfer GameService::transfer(const QUrl &official,const QString &destination,const QByteArray &digest,const QString &algorithm,qint64 expectedSize)const{
    Transfer row;row.url=mapDownloadUrl(official);row.destination=destination;row.digest=digest;row.algorithm=algorithm;row.expectedSize=expectedSize;
    if(row.url!=official)row.fallback=official;
    return row;
}
QList<Transfer> GameService::requiredFiles(const QJsonObject &profile,const QString &baseVersion)const{
    auto jobs=libraries(profile);
    const auto downloads=profile.value("downloads").toObject();
    const auto client=downloads.value("client").toObject();
    if(!client.isEmpty())jobs.prepend(transfer(QUrl(client.value("url").toString()),m_root+"/minecraft/versions/"+baseVersion+'/'+baseVersion+".jar",client.value("sha1").toString().toLatin1(),"sha1",qint64(client.value("size").toInteger(-1))));
    const auto logging=profile.value("logging").toObject().value("client").toObject().value("file").toObject();
    if(!logging.isEmpty())jobs.append(transfer(QUrl(logging.value("url").toString()),ArchiveUtils::safePath(m_root+"/minecraft/assets/log_configs",logging.value("id").toString()),logging.value("sha1").toString().toLatin1(),"sha1",qint64(logging.value("size").toInteger(-1))));
    return jobs;
}
QList<Transfer> GameService::assetFiles(const QJsonObject &index)const{
    QList<Transfer> jobs;
    const auto objects=index.value("objects").toObject();
    for(auto it=objects.begin();it!=objects.end();++it){const auto hash=it.value().toObject().value("hash").toString();require(QRegularExpression("^[a-f0-9]{40}$").match(hash).hasMatch(),"资源哈希无效");jobs.append(transfer(QUrl("https://resources.download.minecraft.net/"+hash.left(2)+'/'+hash),m_root+"/minecraft/assets/objects/"+hash.left(2)+'/'+hash,hash.toLatin1(),"sha1",qint64(it.value().toObject().value("size").toInteger(-1))));}
    return jobs;
}
QList<Transfer> GameService::libraries(const QJsonObject &profile)const{QList<Transfer> jobs;for(auto v:profile.value("libraries").toArray()){auto lib=v.toObject();if(!allowed(lib.value("rules").toArray()))continue;auto downloads=lib.value("downloads").toObject();auto artifact=downloads.value("artifact").toObject();if(!artifact.isEmpty()){auto relative=artifact.value("path").toString();jobs.append(transfer(QUrl(artifact.value("url").toString()),ArchiveUtils::safePath(m_root+"/minecraft/libraries",relative),artifact.value("sha1").toString().toLatin1(),"sha1",qint64(artifact.value("size").toInteger(-1))));}else if(!lib.contains("downloads")){const auto relative=mavenPath(lib.value("name").toString());QString base=lib.value("url").toString("https://libraries.minecraft.net/");if(!base.endsWith('/'))base+='/';jobs.append(transfer(QUrl(base+relative),ArchiveUtils::safePath(m_root+"/minecraft/libraries",relative),{},"sha1"));}
    auto natives=lib.value("natives").toObject().value(osName()).toString();natives.replace("${arch}",QSysInfo::currentCpuArchitecture().contains("64")?"64":"32");if(!natives.isEmpty()){auto item=downloads.value("classifiers").toObject().value(natives).toObject();if(!item.isEmpty())jobs.append(transfer(QUrl(item.value("url").toString()),ArchiveUtils::safePath(m_root+"/minecraft/libraries",item.value("path").toString()),item.value("sha1").toString().toLatin1(),"sha1",qint64(item.value("size").toInteger(-1))));}}
return jobs;}
QJsonObject GameService::version(const QString &v,int depth)const{safeVersion(v);require(depth<8,"版本继承循环或过深");auto obj=QJsonObject::fromVariantMap(read(m_root+"/minecraft/versions/"+v+'/'+v+".json"));auto parent=obj.value("inheritsFrom").toString();if(parent.isEmpty())return obj;auto merged=version(parent,depth+1);auto libs=merged.value("libraries").toArray();for(auto item:obj.value("libraries").toArray())libs.append(item);auto parentArgs=merged.value("arguments").toObject();for(const auto&key:{"jvm","game"}){auto values=parentArgs.value(key).toArray();for(auto val:obj.value("arguments").toObject().value(key).toArray())values.append(val);parentArgs[key]=values;}for(auto i=obj.begin();i!=obj.end();++i)merged[i.key()]=i.value();merged["libraries"]=libs;merged["arguments"]=parentArgs;return merged;}
QString GameService::javaFor(const QVariantMap&meta,int major)const{const auto configured=meta.value("javaPath").toString();if(!configured.isEmpty()){require(QFileInfo(configured).isExecutable(),"配置的 Java 路径不可执行");return configured;}if(m_java){QString problem;const auto path=m_java->bestPath(major,&problem);require(!path.isEmpty(),problem.isEmpty()?QString("未找到可用的 Java %1").arg(major):problem);return path;}
#ifdef Q_OS_MACOS
    if(major>0){QProcess process;process.start("/usr/libexec/java_home",{"-v",QString::number(major)});if(process.waitForFinished(3000)&&process.exitCode()==0){const auto path=QString::fromUtf8(process.readAllStandardOutput()).trimmed()+"/bin/java";if(QFileInfo(path).isExecutable())return path;}}
#endif
    const auto path=QStandardPaths::findExecutable("java");require(!path.isEmpty(),"未找到 Java，请在实例设置中指定路径");return path;
}
QVariantMap GameService::downloadState()const{
    auto state=m_download.state();
    if(m_installPhase.isEmpty())return state;
    state["instanceId"]=m_installId;
    state["title"]=(m_installPhase=="repair"||m_installAction=="launch.repair")?"启动修复 · Minecraft "+m_targetVersion:"Minecraft "+m_targetVersion+(m_loader=="vanilla"?QString():" · "+m_loader);
    state["active"]=m_installing||m_download.busy();
    state["phase"]=m_download.busy()?(state.value("paused").toBool()?"paused":"downloading"):m_installPhase;
    state["canPause"]=m_download.busy();
    state["canRetry"]=!m_installing&&!m_download.busy()&&(m_installPhase=="error"||m_installPhase=="cancelled");
    state["error"]=m_installError;
    if(m_installing&&!m_download.busy())state["paused"]=false;
    return state;
}
void GameService::installationError(const QString &message){m_installing=false;m_installError=message;m_installPhase=message.contains("取消")?"cancelled":"error";emit downloadChanged(downloadState());emit failed(m_installAction,message);}
void GameService::installBase(const QVariantMap &args){
    m_preferOfficialFallback=false;m_repairInstance.clear();
    require(!m_installing&&!m_download.busy()&&m_installer.state()==QProcess::NotRunning,"已有安装任务正在进行");
    const auto targetVersion=args.value("version").toString();safeVersion(targetVersion);
    const auto loader=args.value("loader","vanilla").toString(),loaderVersion=args.value("loaderVersion").toString();
    require(QStringList{"vanilla","fabric","quilt","forge","neoforge"}.contains(loader),"加载器无效");if(loader!="vanilla")safeVersion(loaderVersion);
    InstanceService instances(m_root);QString instanceId=args.value("instanceId").toString();
    if(args.value("createInstance").toBool()){
        QString error;
        connect(&instances,&FeatureService::completed,&instances,[&](const QString&,const QVariantMap &data){instanceId=data.value("id").toString();});
        connect(&instances,&FeatureService::failed,&instances,[&](const QString&,const QString &message){error=message;});
        instances.execute("instance.create",{{"name",args.value("name")},{"version",targetVersion},{"loader",loader}});
        require(error.isEmpty()&&!instanceId.isEmpty(),error.isEmpty()?"无法创建实例":error);
    }
    auto meta=read(instances.instancePath(instanceId)+"/instance.json");
    m_installId=instanceId;m_targetVersion=targetVersion;m_loader=loader;m_loaderVersion=loaderVersion;
    m_installAction="game.install";++m_installGeneration;m_installing=true;m_installPhase="metadata";m_installError.clear();
    m_retryArguments=args;m_retryArguments.remove("createInstance");m_retryArguments["instanceId"]=instanceId;
    emit downloadChanged(downloadState());
    emit completed("game.queued",{{"instanceId",instanceId},{"message","开始准备 Minecraft "+targetVersion}});
    emit activity("正在获取官方版本清单");
    json(m_manifestUrl,[this](QJsonObject manifest){QJsonObject entry;for(auto v:manifest.value("versions").toArray())if(v.toObject().value("id")==m_targetVersion){entry=v.toObject();break;}require(!entry.isEmpty(),"官方版本清单中未找到该版本");
        json(QUrl(entry.value("url").toString()),[this](QJsonObject profile){require(!profile.value("downloads").toObject().value("client").toObject().isEmpty(),"该版本没有客户端下载信息");write(m_root+"/minecraft/versions/"+m_targetVersion+'/'+m_targetVersion+".json",profile.toVariantMap());auto jobs=requiredFiles(profile,m_targetVersion);auto assets=profile.value("assetIndex").toObject();
            auto begin=[this](QList<Transfer> files){emit activity(QString("开始安装：%1 个文件").arg(files.size()));m_download.start(files,[this](QString e){if(!e.isEmpty()){installationError(e);return;}try{installLoader();}catch(const std::exception&e){installationError(QString::fromUtf8(e.what()));}});};
            if(assets.isEmpty()){begin(jobs);return;}
            json(QUrl(assets.value("url").toString()),[this,assets,jobs,begin](QJsonObject index)mutable{const auto indexId=assets.value("id").toString();safeVersion(indexId);write(m_root+"/minecraft/assets/indexes/"+indexId+".json",index.toVariantMap());jobs+=assetFiles(index);begin(jobs);},m_installAction,assets.value("sha1").toString().toLatin1());
        },m_installAction,entry.value("sha1").toString().toLatin1());
    },m_installAction);
}
void GameService::installLibraries(QJsonObject profile,std::function<void()>done){auto jobs=libraries(profile);m_download.start(jobs,[this,done](QString error){if(!error.isEmpty())installationError(error);else done();});}
void GameService::installationFinished(const QString &profileId){InstanceService instances(m_root);auto path=instances.instancePath(m_installId)+"/instance.json";auto meta=read(path);meta["version"]=m_targetVersion;meta["profileId"]=profileId;meta["loader"]=m_loader;meta["installed"]=true;write(path,meta);m_installing=false;m_installPhase="complete";m_installError.clear();emit downloadChanged(downloadState());emit completed(m_installAction,{{"message","游戏版本安装完成"},{"instanceId",m_installId},{"profileId",profileId}});}
void GameService::installLoader(){
    m_installPhase="loader";emit downloadChanged(downloadState());
    if(m_loader=="vanilla"){installationFinished(m_targetVersion);return;}
    if(m_loader=="fabric"||m_loader=="quilt"){
        const auto base=m_loader=="fabric"?"https://meta.fabricmc.net/v2":"https://meta.quiltmc.org/v3";
        json(QUrl(QString(base)+"/versions/loader/"+m_targetVersion+'/'+m_loaderVersion+"/profile/json"),[this](QJsonObject profile){const auto id=profile.value("id").toString();safeVersion(id);write(m_root+"/minecraft/versions/"+id+'/'+id+".json",profile.toVariantMap());installLibraries(profile,[this,id]{installationFinished(id);});},m_installAction);return;
    }
    const QString coordinate=m_loader=="forge"?m_targetVersion+'-'+m_loaderVersion:m_loaderVersion;
    const QString base=m_loader=="forge"?"https://maven.minecraftforge.net/net/minecraftforge/forge/":"https://maven.neoforged.net/releases/net/neoforged/neoforge/";
    const QString jarName=(m_loader=="forge"?"forge-":"neoforge-")+coordinate+"-installer.jar";
    const QString url=base+coordinate+'/'+jarName,target=m_root+"/cache/"+jarName;
    get(QUrl(url+".sha1"),[this,url,target](QByteArray checksum){auto sha=checksum.trimmed().split(' ').first();require(QRegularExpression("^[a-fA-F0-9]{40}$").match(QString::fromLatin1(sha)).hasMatch(),"安装器校验文件无效");m_download.start({{transfer(QUrl(url),target,sha,"sha1")}},[this,target](QString error){if(!error.isEmpty()){installationError(error);return;}try{
        InstanceService instances(m_root);auto meta=read(instances.instancePath(m_installId)+"/instance.json");const auto java=javaFor(meta,version(m_targetVersion).value("javaVersion").toObject().value("majorVersion").toInt(17));const auto game=m_root+"/minecraft";if(!QFile::exists(game+"/launcher_profiles.json"))write(game+"/launcher_profiles.json",{{"profiles",QVariantMap{}}});
        disconnect(&m_installer,nullptr,this,nullptr);m_installer.setProcessChannelMode(QProcess::MergedChannels);
        connect(&m_installer,&QProcess::readyReadStandardOutput,this,[this]{QDir().mkpath(m_root+"/logs");QFile f(m_root+"/logs/installer.log");if(f.open(QIODevice::Append))f.write(m_installer.readAllStandardOutput());});
        connect(&m_installer,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){installationError(m_installer.errorString());});
        connect(&m_installer,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){try{require(code==0,"加载器安装器失败，请查看 installer.log");QString selected;for(const auto&name:QDir(m_root+"/minecraft/versions").entryList(QDir::Dirs|QDir::NoDotAndDotDot)){if(name.contains(m_loader,Qt::CaseInsensitive)&&name.contains(m_loaderVersion)){selected=name;break;}}require(!selected.isEmpty(),"安装器未生成预期版本配置");installLibraries(version(selected),[this,selected]{installationFinished(selected);});}catch(const std::exception&e){installationError(QString::fromUtf8(e.what()));}});
        m_installPhase="installing";emit downloadChanged(downloadState());m_installer.setWorkingDirectory(game);m_installer.start(java,{"-jar",target,"--installClient",game});emit activity("正在运行官方加载器安装器");
    }catch(const std::exception&e){installationError(QString::fromUtf8(e.what()));}});},m_installAction);
}
void GameService::launch(const QString &instanceId,const QString &server){
    require(m_game.state()==QProcess::NotRunning,"已有游戏进程正在运行");require(!m_installing,"请等待安装完成");require(bool(m_identity),"账号模块未接入");const auto account=m_identity();require(!account.contains("error"),account.value("error").toString());
    InstanceService instances(m_root);const auto meta=read(instances.instancePath(instanceId)+"/instance.json");require(meta.value("installed").toBool(),"请先安装此实例的游戏版本");const auto profileId=meta.value("profileId",meta.value("version")).toString();const auto profile=version(profileId);const auto baseVersion=meta.value("version").toString();const auto game=instances.gamePath(instanceId);QDir().mkpath(game);const auto natives=instances.instancePath(instanceId)+"/natives";QDir().mkpath(natives);
    QStringList classpath;QSet<QString> coordinates;
    auto allLibraries=profile.value("libraries").toArray();
    for(qsizetype i=allLibraries.size()-1;i>=0;--i){const auto lib=allLibraries[i].toObject();if(!allowed(lib.value("rules").toArray()))continue;const auto name=lib.value("name").toString();const auto coordinate=name.section(':',0,1)+':'+name.section(':',3,3);if(coordinates.contains(coordinate))continue;coordinates.insert(coordinate);auto artifact=lib.value("downloads").toObject().value("artifact").toObject();const auto relative=artifact.isEmpty()?mavenPath(name):artifact.value("path").toString();const auto path=ArchiveUtils::safePath(m_root+"/minecraft/libraries",relative);if(!lib.contains("natives")||!artifact.isEmpty()){require(QFile::exists(path),"缺少游戏依赖，请重新安装："+name);classpath.prepend(path);}
        auto classifier=lib.value("natives").toObject().value(osName()).toString();classifier.replace("${arch}",QSysInfo::currentCpuArchitecture().contains("64")?"64":"32");if(!classifier.isEmpty()){auto native=lib.value("downloads").toObject().value("classifiers").toObject().value(classifier).toObject();if(!native.isEmpty()){QTemporaryDir staging;ArchiveUtils::extract(ArchiveUtils::safePath(m_root+"/minecraft/libraries",native.value("path").toString()),staging.path());QDirIterator files(staging.path(),QDir::Files,QDirIterator::Subdirectories);while(files.hasNext()){auto source=files.next();auto info=QFileInfo(source);if(info.suffix()=="dylib"||info.suffix()=="jnilib"||info.suffix()=="dll"||info.suffix()=="so"){const auto target=ArchiveUtils::safePath(natives,info.fileName());if(!QFile::exists(target))require(QFile::copy(source,target),"无法准备本机依赖");}}}}
    }
    const auto client=m_root+"/minecraft/versions/"+baseVersion+'/'+baseVersion+".jar";require(QFile::exists(client),"缺少客户端 JAR");classpath<<client;
    const auto assetId=profile.value("assetIndex").toObject().value("id").toString(profile.value("assets").toString());
    QString gameAssets=m_root+"/minecraft/assets";
    if(!assetId.isEmpty()){
        safeVersion(assetId);const auto index=QJsonObject::fromVariantMap(read(m_root+"/minecraft/assets/indexes/"+assetId+".json",true));
        if(index.value("virtual").toBool()||index.value("map_to_resources").toBool()){
            gameAssets=index.value("map_to_resources").toBool()?game+"/resources":m_root+"/minecraft/assets/virtual/"+assetId;
            auto objects=index.value("objects").toObject();for(auto it=objects.begin();it!=objects.end();++it){const auto hash=it.value().toObject().value("hash").toString();require(QRegularExpression("^[a-f0-9]{40}$").match(hash).hasMatch(),"资源索引哈希无效");const auto target=ArchiveUtils::safePath(gameAssets,it.key());if(!DownloadEngine::verified(target,hash.toLatin1(),"sha1")){const auto source=m_root+"/minecraft/assets/objects/"+hash.left(2)+'/'+hash;require(DownloadEngine::verified(source,hash.toLatin1(),"sha1"),"缺少旧版游戏资源，请重新安装");QFile in(source);QSaveFile out(target);require(QDir().mkpath(QFileInfo(target).absolutePath())&&in.open(QIODevice::ReadOnly)&&out.open(QIODevice::WriteOnly),"无法准备旧版资源");while(!in.atEnd()){auto bytes=in.read(1024*1024);require(!bytes.isEmpty()&&out.write(bytes)==bytes.size(),"复制游戏资源失败");}require(out.commit(),"提交游戏资源失败");}}
        }
    }
    QVariantMap vars{{"game_assets",gameAssets},{"auth_player_name",account.value("name")},{"auth_uuid",account.value("uuid")},{"auth_access_token",account.value("accessToken")},{"auth_session",account.value("accessToken")},{"user_type",account.value("type")=="microsoft"?"msa":"legacy"},{"user_properties","{}"},{"version_name",profileId},{"version_type","release"},{"game_directory",game},{"assets_root",m_root+"/minecraft/assets"},{"assets_index_name",profile.value("assets").toString()},{"natives_directory",natives},{"launcher_name","XMCL"},{"launcher_version","0.2.0"},{"classpath",classpath.join(QDir::listSeparator())},{"library_directory",m_root+"/minecraft/libraries"},{"classpath_separator",QString(QDir::listSeparator())},{"resolution_width",meta.value("width",1280)},{"resolution_height",meta.value("height",720)},{"clientid",""},{"auth_xuid",""}};
    QStringList args=arguments(profile.value("arguments").toObject().value("jvm"),vars);if(args.isEmpty()){
#ifdef Q_OS_MACOS
        args<<"-XstartOnFirstThread";
#endif
        args<<"-Djava.library.path="+natives<<"-cp"<<classpath.join(QDir::listSeparator());
    }
    args.prepend(QString("-Xmx%1G").arg(qBound(1,meta.value("memoryGb",m_defaultMemory).toInt(),qMax(1,qMin(64,MemoryAdvisor::maxGb())))));
    args+=QProcess::splitCommand(meta.value("jvmArgs").toString());
    if(account.value("type")=="yggdrasil"){const auto agent=m_root+"/runtime/authlib-injector.jar";require(QFile::exists(agent),"请先在安装面板准备 authlib-injector（外置认证）");args<<"-javaagent:"+agent+'='+account.value("providerRoot").toString();}
    auto log=profile.value("logging").toObject().value("client").toObject();if(!log.isEmpty()){auto path=ArchiveUtils::safePath(m_root+"/minecraft/assets/log_configs",log.value("file").toObject().value("id").toString());args<<log.value("argument").toString().replace("${path}",path);}
    const auto mainClass=profile.value("mainClass").toString();require(!mainClass.isEmpty(),"版本缺少主类");args<<mainClass;
    auto gameArgs=arguments(profile.value("arguments").toObject().value("game"),vars);if(gameArgs.isEmpty()){for(const auto &argument:QProcess::splitCommand(profile.value("minecraftArguments").toString()))gameArgs<<subst(argument,vars);}args+=gameArgs;args+=QProcess::splitCommand(meta.value("gameArgs").toString());
    if(!server.isEmpty()){
        require(!server.contains('/')&&!server.contains('@')&&!server.contains('?')&&!server.contains('#')&&!server.contains(QRegularExpression("\\s")),"服务器地址无效");
        QUrl address("minecraft://"+server);require(address.isValid()&&!address.host().isEmpty()&&address.port(25565)>0,"服务器地址无效");
        bool quickPlay=false;
        for(const auto &value:profile.value("arguments").toObject().value("game").toArray()){
            const auto object=value.toObject();
            if(object.value("value").toString()=="--quickPlayMultiplayer"){quickPlay=true;break;}
            for(const auto &rule:object.value("rules").toArray())if(rule.toObject().value("features").toObject().value("is_quick_play_multiplayer").toBool()){quickPlay=true;break;}
            if(quickPlay)break;
        }
        if(quickPlay)args<<"--quickPlayMultiplayer"<<(address.host()+':'+QString::number(address.port(25565)));
        else args<<"--server"<<address.host()<<"--port"<<QString::number(address.port(25565));
    }
    QDir().mkpath(m_root+"/logs");m_game.setWorkingDirectory(game);m_game.start(javaFor(meta,profile.value("javaVersion").toObject().value("majorVersion").toInt(8)),args);
}

QVariantMap GameService::launchCheck(const QString &instanceId)const{
    InstanceService instances(m_root);
    const auto meta=read(instances.instancePath(instanceId)+"/instance.json");
    require(meta.value("installed").toBool(),"请先安装此实例的游戏版本");
    const auto profileId=meta.value("profileId",meta.value("version")).toString();
    require(!profileId.isEmpty(),"实例缺少版本配置");
    const auto profile=version(profileId);
    const auto baseVersion=meta.value("version").toString();
    QVariantList issues,fileIssues;int missing=0,mismatched=0,checked=0;
    const auto needsRepair=[&](const Transfer &row){
        ++checked;const QFileInfo info(row.destination);
        if(!info.exists()||!info.isFile()){
            ++missing;if(fileIssues.size()<40)fileIssues.append(QVariantMap{{"kind","file"},{"path",row.destination},{"title",info.fileName()},{"reason","文件缺失"},{"fixable",true}});return true;}
        if(row.expectedSize>=0&&info.size()!=row.expectedSize){
            ++mismatched;if(fileIssues.size()<40)fileIssues.append(QVariantMap{{"kind","file"},{"path",row.destination},{"title",info.fileName()},{"reason",QString("大小不符（期望 %1，实际 %2）").arg(row.expectedSize).arg(info.size())},{"fixable",true}});return true;}
        return false;
    };
    for(const auto &row:requiredFiles(profile,baseVersion))needsRepair(row);
    const auto assets=profile.value("assetIndex").toObject();int assetObjects=0;bool indexMissing=false;
    if(!assets.isEmpty()){
        const auto indexId=assets.value("id").toString();safeVersion(indexId);
        const auto indexPath=m_root+"/minecraft/assets/indexes/"+indexId+".json";
        const bool indexOk=QFileInfo(indexPath).isFile()&&DownloadEngine::verified(indexPath,assets.value("sha1").toString().toLatin1(),"sha1");
        if(indexOk){const auto index=QJsonObject::fromVariantMap(read(indexPath));assetObjects=index.value("objects").toObject().size();for(const auto &row:assetFiles(index))needsRepair(row);}
        else{indexMissing=true;if(fileIssues.size()<40)fileIssues.append(QVariantMap{{"kind","index"},{"path",indexPath},{"title",indexId+".json"},{"reason","资源索引缺失或校验失败"},{"fixable",true}});}
    }
    QVariantMap java;
    if(m_java)java=m_java->matchForInstance(instanceId);
    else java=QVariantMap{{"requiredMajor",-1},{"matched",QVariantMap{}},{"warnings",QStringList{"Java 运行时服务未接入"}}};
    const auto matched=java.value("matched").toMap();
    if(matched.isEmpty()){
        const auto warnings=java.value("warnings").toList();
        issues.append(QVariantMap{{"code","java"},{"severity","error"},{"title","没有匹配的 Java"},{"detail",warnings.isEmpty()?QString("未找到可用 Java"):warnings.first().toString()},{"fixable",false},{"fixAction","instance.settings"}});
    }
    for(const auto &warning:java.value("warnings").toList())if(!matched.isEmpty())issues.append(QVariantMap{{"code","java-warning"},{"severity","warning"},{"title","Java 版本提示"},{"detail",warning.toString()},{"fixable",false},{"fixAction","instance.settings"}});
    if(missing+mismatched>0)issues.prepend(QVariantMap{{"code","files"},{"severity","error"},{"title",QString("有 %1 个缺失、%2 个大小不符的文件").arg(missing).arg(mismatched)},{"detail","只下载缺失或损坏的文件，不删除未知模组或用户文件。"},{"fixable",true},{"fixAction","launch.repair"}});
    if(indexMissing)issues.prepend(QVariantMap{{"code","assets-index"},{"severity","error"},{"title","资源索引缺失"},{"detail","需要先修复资源索引，再补齐游戏资源。"},{"fixable",true},{"fixAction","launch.repair"}});
    const bool fixable=(missing+mismatched>0||indexMissing);
    QString message;
    if(fixable)message=QString("发现 %1 个需要修复的文件").arg(missing+mismatched+(indexMissing?1:0));
    else if(matched.isEmpty())message="没有匹配的 Java，请安装或手动指定";
    else message="启动检查通过";
    return {{"instanceId",instanceId},{"profileId",profileId},{"installed",true},{"java",java},
            {"files",QVariantMap{{"checked",checked},{"missing",missing},{"mismatched",mismatched},{"assetObjects",assetObjects}}},
            {"issues",issues},{"fileIssues",fileIssues},{"warnings",java.value("warnings")},{"fixable",fixable},{"message",message}};
}
QVariantMap GameService::diagnostics(const QString &instanceId)const{
    InstanceService instances(m_root);
    QVariantMap meta;
    try{meta=read(instances.instancePath(instanceId)+"/instance.json");}catch(...){}
    const auto logPath=m_root+"/logs/game.log";
    QFile log(logPath);QString text;qint64 scanned=0;
    if(log.open(QIODevice::ReadOnly)){
        const auto size=log.size();const auto start=qMax<qint64>(0,size-256*1024);log.seek(start);const auto bytes=log.readAll();text=QString::fromUtf8(bytes);scanned=bytes.size();
    }
    QFile launcher(m_root+"/logs/launcher.log");
    if(launcher.open(QIODevice::ReadOnly)){const auto size=launcher.size();launcher.seek(qMax<qint64>(0,size-64*1024));text+="\n"+QString::fromUtf8(launcher.readAll());}
    QVariantList findings;
    const auto add=[&](const QString &id,const QString &severity,const QString &title,const QString &evidence,const QString &advice,const QString &fixAction){
        findings.append(QVariantMap{{"id",id},{"severity",severity},{"title",title},{"evidence",evidence.left(320)},{"advice",advice},{"fixAction",fixAction}});
    };
    const auto firstLine=[&](const QRegularExpression &pattern)->QString{const auto match=pattern.match(text);return match.hasMatch()?match.captured(0).trimmed():QString();};
    static const QRegularExpression classVersion("(?s)UnsupportedClassVersionError.{0,500}?class file version (\\d+)");
    const auto classMatch=classVersion.match(text);
    if(classMatch.hasMatch()){
        static const QHash<int,int> map{{52,8},{53,9},{54,10},{55,11},{56,12},{57,13},{58,14},{59,15},{60,16},{61,17},{62,18},{63,19},{64,20},{65,21},{66,22}};
        const int needed=map.value(classMatch.captured(1).toInt(),0);
        add("java-class-version","error","Java 版本不匹配",classMatch.captured(0),needed>0?QString("该游戏或模组需要 Java %1 或更高；请在实例设置中选择匹配的 Java，然后重新检查。").arg(needed):"游戏使用了更高版本的 class 文件；请升级 Java。","instance.settings");
    }
    const auto oom=firstLine(QRegularExpression("(?s)OutOfMemoryError.{0,200}"));
    if(!oom.isEmpty())add("out-of-memory","error","游戏内存不足",oom,QString("提高该实例内存（当前配置 %1 GB），并检查是否装了过多模组；保存后重新启动。").arg(meta.value("memoryGb",m_defaultMemory).toInt()),"instance.settings");
    const auto missingClass=firstLine(QRegularExpression("(?s)(?:NoClassDefFoundError|ClassNotFoundException).{0,200}"));
    if(!missingClass.isEmpty())add("missing-class","error","缺少库或模组前置",missingClass,"先运行“修复缺失文件”；若仍失败，检查模组依赖是否完整。","launch.repair");
    const auto natives=firstLine(QRegularExpression("(?s)(?:UnsatisfiedLinkError|Failed to locate library|java\\.library\\.path).{0,240}"));
    if(!natives.isEmpty())add("native-library","error","本机库加载失败",natives,"检查 Java 架构（64 位）与系统架构是否匹配；重新运行修复以补齐库文件。","launch.repair");
    const auto jvm=firstLine(QRegularExpression("(?s)(?:Unrecognized VM option|Invalid maximum heap size|Invalid initial heap size|Could not reserve enough space).{0,200}"));
    if(!jvm.isEmpty())add("jvm-arguments","error","JVM 参数或内存设置无效",jvm,"检查实例设置中的 JVM 参数与内存；内存不能超过系统可用量。","instance.settings");
    const auto dependency=firstLine(QRegularExpression("(?s)(?:Missing or unsupported mandatory dependencies|ModResolutionException|Incompatible mod set|No compatible mod).{0,300}"));
    if(!dependency.isEmpty())add("mod-dependencies","error","模组依赖冲突",dependency,"检查模组版本与前置是否匹配当前 Minecraft/加载器；不要直接删除用户模组。","");
    const auto crash=firstLine(QRegularExpression("(?s)(?:A fatal error has been detected by the Java Runtime Environment|EXCEPTION_ACCESS_VIOLATION|SIGSEGV).{0,200}"));
    if(!crash.isEmpty())add("jvm-crash","error","JVM 崩溃",crash,"查看完整日志与崩溃报告；不要仅凭崩溃增加内存。","");
    const auto exitCode=firstLine(QRegularExpression("游戏进程退出，代码 \\d+"));
    if(findings.isEmpty()&&!exitCode.isEmpty())add("game-exit","info","游戏进程已退出",exitCode,"日志中没有匹配到已知错误规则；请查看完整日志或导出诊断信息。","");
    return {{"instanceId",instanceId},{"logPath",logPath},{"logExists",QFileInfo::exists(logPath)},{"scannedBytes",scanned},
            {"findings",findings},{"message",findings.isEmpty()?"没有匹配到已知问题":"发现 "+QString::number(findings.size())+" 条可解释的原因"}};
}
void GameService::launchRepair(const QString &instanceId,int depth){
    try{
        require(!m_installing&&!m_download.busy(),"已有下载或安装任务正在进行");
        require(m_game.state()==QProcess::NotRunning,"游戏正在运行，无法修复文件");
        InstanceService instances(m_root);const auto meta=read(instances.instancePath(instanceId)+"/instance.json");
        require(meta.value("installed").toBool(),"请先安装此实例的游戏版本");
        const auto profileId=meta.value("profileId",meta.value("version")).toString();const auto profile=version(profileId);const auto baseVersion=meta.value("version").toString();
        const auto needsRepair=[](const Transfer &row){const QFileInfo info(row.destination);return !info.exists()||!info.isFile()||(row.expectedSize>=0&&info.size()!=row.expectedSize);};
        QList<Transfer> todo;for(const auto &row:requiredFiles(profile,baseVersion))if(needsRepair(row))todo.append(row);
        bool secondPass=false;
        const auto assets=profile.value("assetIndex").toObject();
        if(!assets.isEmpty()){
            const auto indexId=assets.value("id").toString();safeVersion(indexId);
            const auto indexPath=m_root+"/minecraft/assets/indexes/"+indexId+".json";
            if(!QFileInfo(indexPath).isFile()||!DownloadEngine::verified(indexPath,assets.value("sha1").toString().toLatin1(),"sha1")){
                secondPass=true;todo.append(transfer(QUrl(assets.value("url").toString()),indexPath,assets.value("sha1").toString().toLatin1(),"sha1",qint64(assets.value("size").toInteger(-1))));
            }else{const auto index=QJsonObject::fromVariantMap(read(indexPath));for(const auto &row:assetFiles(index))if(needsRepair(row))todo.append(row);}
        }
        m_repairing=true;m_installing=true;m_installId=instanceId;m_repairInstance=instanceId;m_repairDepth=depth;
        m_installAction="launch.repair";m_targetVersion=baseVersion;m_loader=meta.value("loader","vanilla").toString();m_installPhase="repair";m_installError.clear();
        emit downloadChanged(downloadState());
        if(todo.isEmpty()){
            m_installing=false;m_repairing=false;m_installPhase="complete";emit downloadChanged(downloadState());
            emit completed("launch.repair",{{"instanceId",instanceId},{"repairedFiles",0},{"message","没有需要修复的文件"}});return;
        }
        const int planned=todo.size();
        m_download.start(todo,[this,instanceId,depth,secondPass,planned](QString error){
            m_installing=false;m_repairing=false;
            if(!error.isEmpty()){m_installPhase="error";m_installError=error;emit downloadChanged(downloadState());emit failed("launch.repair",error);return;}
            if(secondPass&&depth<1){launchRepair(instanceId,depth+1);return;}
            m_installPhase="complete";m_installError.clear();emit downloadChanged(downloadState());
            emit completed("launch.repair",{{"instanceId",instanceId},{"repairedFiles",planned},{"message",QString("已修复 %1 个文件").arg(planned)}});
        });
    }catch(const std::exception &e){m_installing=false;m_repairing=false;m_installPhase="error";m_installError=QString::fromUtf8(e.what());emit downloadChanged(downloadState());emit failed("launch.repair",QString::fromUtf8(e.what()));}
}
void GameService::execute(const QString&a,const QVariantMap&args){try{
    if(a=="version.list"){
        json(m_manifestUrl,[this,a,args](QJsonObject v){QVariantList items;auto type=args.value("type","release").toString();auto query=args.value("query").toString();for(auto entry:v.value("versions").toArray()){auto m=entry.toObject();if(type!="all"&&m.value("type").toString()!=type)continue;if(!m.value("id").toString().contains(query,Qt::CaseInsensitive))continue;items.append(QVariantMap{{"id",m.value("id").toString()},{"title",m.value("id").toString()},{"version",m.value("id").toString()},{"type",m.value("type").toString()},{"subtitle",m.value("type").toString()+" · "+m.value("releaseTime").toString().left(10)}});}emit completed(a,{{"items",items},{"message",QString("获取到 %1 个版本").arg(items.size())}});},a);return;
    }
    if(a=="loader.list"){
        const auto mc=args.value("version").toString(),loader=args.value("loader").toString();safeVersion(mc);require(QStringList{"fabric","quilt","forge","neoforge"}.contains(loader),"请选择需要查询的加载器");
        if(loader=="fabric"||loader=="quilt")get(QUrl((loader=="fabric"?QString("https://meta.fabricmc.net/v2"):QString("https://meta.quiltmc.org/v3"))+"/versions/loader/"+mc),[this,a,mc,loader](QByteArray bytes){const auto doc=QJsonDocument::fromJson(bytes);require(doc.isArray(),"加载器清单响应无效");QVariantList items;for(auto v:doc.array()){auto version=v.toObject().value("loader").toObject().value("version").toString();items.append(QVariantMap{{"id",version},{"title",version},{"loaderVersion",version},{"subtitle","加载器"}});}emit completed(a,{{"items",items},{"version",mc},{"loader",loader}});},a);
        else get(QUrl(loader=="forge"?"https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml":"https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml"),[this,a,mc,loader](QByteArray bytes){QXmlStreamReader xml(bytes);QVariantList items;const auto neoPrefix=mc.section('.',1);while(!xml.atEnd()){xml.readNext();if(xml.isStartElement()&&xml.name()==QStringLiteral("version")){auto v=xml.readElementText();if((loader=="forge"&&v.startsWith(mc+'-'))||(loader=="neoforge"&&v.startsWith(neoPrefix+'.'))){auto display=loader=="forge"?v.mid(mc.size()+1):v;items.prepend(QVariantMap{{"id",display},{"title",display},{"loaderVersion",display},{"subtitle",loader+" · "+mc}});}}}require(!xml.hasError(),"加载器 XML 无效");emit completed(a,{{"items",items},{"version",mc},{"loader",loader}});},a);return;
    }
    if(a=="authlib.prepare"){
        require(!m_installing&&!m_download.busy(),"请等待当前下载完成");
        json(QUrl("https://authlib-injector.yushi.moe/artifact/latest.json"),[this,a](QJsonObject info){
            const auto rawUrl=QUrl(info.value("download_url").toString());const auto sha=info.value("checksums").toObject().value("sha256").toString();
            QUrl official=rawUrl;
            if(m_downloadSource==DownloadSource::mirrorId()&&rawUrl.host()==DownloadSource::mirrorHost()&&rawUrl.path().startsWith("/mirrors/authlib-injector")){
                official=QUrl("https://authlib-injector.yushi.moe"+rawUrl.path().mid(QString("/mirrors/authlib-injector").size()));official.setQuery(rawUrl.query());
            }
            require(https(official)&&official.host()=="authlib-injector.yushi.moe","外置认证下载地址无效");
            require(QRegularExpression("^[a-fA-F0-9]{64}$").match(sha).hasMatch(),"外置认证缺少 SHA-256 校验");
            m_download.start({{transfer(official,m_root+"/runtime/authlib-injector.jar",sha.toLatin1(),"sha256")}},[this,a](QString error){if(error.isEmpty())emit completed(a,{{"message","外置认证组件已校验并安装"}});else emit failed(a,error);});
        },a);return;
    }
    if(a=="launch.check"){require(!args.value("instanceId").toString().isEmpty(),"请选择实例");emit completed(a,launchCheck(args.value("instanceId").toString()));return;}
    if(a=="launch.repair"){require(!args.value("instanceId").toString().isEmpty(),"请选择实例");launchRepair(args.value("instanceId").toString());return;}
    if(a=="diagnostic.analyze"){emit completed(a,diagnostics(args.value("instanceId").toString()));return;}
    if(a=="download.retry"){if(!m_repairInstance.isEmpty()){launchRepair(m_repairInstance);return;}require(!m_retryArguments.isEmpty(),"没有可重试的安装任务");installBase(m_retryArguments);return;}
    if(a=="game.install"){installBase(args);return;}
    if(a=="game.launch"){launch(args.value("instanceId").toString(),args.value("server").toString());return;}
    if(a=="game.stop"){require(args.value("confirm").toBool(),"请确认结束游戏");m_game.terminate();emit completed(a,{{"message","已向游戏发送退出请求"}});return;}
    if(a=="java.scan"){
        if(m_java){const auto items=m_java->runtimes(args.value("refresh").toBool());emit completed(a,{{"items",items},{"message",QString("检测到 %1 个 Java 运行时").arg(items.size())}});return;}
        QVariantList items;QStringList paths;auto current=QStandardPaths::findExecutable("java");if(!current.isEmpty())paths<<current;
#ifdef Q_OS_MACOS
        for(auto root:{QString("/Library/Java/JavaVirtualMachines"),QDir::homePath()+"/Library/Java/JavaVirtualMachines"})for(auto name:QDir(root).entryList(QDir::Dirs|QDir::NoDotAndDotDot)){auto path=root+'/'+name+"/Contents/Home/bin/java";if(QFileInfo(path).isExecutable())paths<<path;}
#endif
        paths.removeDuplicates();for(auto path:paths){QProcess process;process.setProcessChannelMode(QProcess::MergedChannels);process.start(path,{"-version"});if(!process.waitForFinished(2000)){process.kill();process.waitForFinished(500);continue;}auto text=QString::fromUtf8(process.readAll()).trimmed();items.append(QVariantMap{{"id",path},{"title",text.section('\n',0,0)},{"subtitle",path},{"javaPath",path},{"path",path}});}emit completed(a,{{"items",items},{"message","选择 Java 路径并保存到实例启动配置"}});return;
    }
    if(a=="download.pause")m_download.pause();else if(a=="download.resume")m_download.resume();else if(a=="download.cancel"){++m_installGeneration;m_installing=false;m_installPhase="cancelled";m_installError="安装已取消";disconnect(&m_installer,nullptr,this,nullptr);m_download.cancel();if(m_installer.state()!=QProcess::NotRunning)m_installer.terminate();emit downloadChanged(downloadState());}else require(a=="download.status","未知游戏操作");emit completed(a,{{"message","下载任务状态"},{"task",m_download.state()}});
}catch(const std::exception&e){emit failed(a,QString::fromUtf8(e.what()));}}
