#include "ServiceHub.h"
#include "InstanceService.h"
#include "ResourceService.h"
#include "AccountService.h"
#include "GameService.h"
#include "JavaRuntimeService.h"
#include "BridgeService.h"
#include "CommunityService.h"
#include "PackService.h"
#include "PersonalizationService.h"
#include "ConnectivityService.h"
#include "MaintenanceService.h"
#include "SkinGeometry.h"
#include <qqml.h>
#include <QDir>
#include <QFile>
#include <QDateTime>
ServiceHub::ServiceHub(QString root,QObject*p,QUrl manifestUrl,QUrl communityApiBase):QObject(p),m_root(std::move(root)){
    QDir().mkpath(m_root+"/logs");
    static bool registered = false;
    if (!registered) { qmlRegisterType<SkinGeometry>("XMCL.Native",1,0,"SkinGeometry"); registered=true; }
    add("instances",new InstanceService(m_root,this));add("resources",new ResourceService(m_root,this));
    m_accounts=new AccountService(m_root,this);add("accounts",m_accounts);
    m_java=new JavaRuntimeService(m_root,this);add("java",m_java);
    m_game=new GameService(m_root,this,manifestUrl);m_game->setJavaRuntimeService(m_java);m_game->setIdentityProvider([this]{return m_accounts->launchIdentity();});add("game",m_game);
    m_personalization=new PersonalizationService(m_root,this);add("personalization",m_personalization);
    m_game->setDownloadSource(m_personalization->settings().value("downloadSource").toString());
    connect(m_personalization,&PersonalizationService::changed,this,[this]{
        m_game->setDownloadSource(m_personalization->settings().value("downloadSource").toString());
        emit appearanceChanged();
    });
    add("connectivity",new ConnectivityService(m_root,this));
    add("maintenance",new MaintenanceService(m_root,this));
    add("bridge",new BridgeService(m_root,this));
    m_community=new CommunityService(m_root,this,communityApiBase);add("community",m_community);
    connect(m_community,&CommunityService::downloadChanged,this,[this](const QVariantMap&){emit downloadStateChanged();});
    m_packs=new PackService(m_root,this);add("packs",m_packs);
    connect(m_packs,&PackService::downloadChanged,this,[this](const QVariantMap&){emit downloadStateChanged();});
    connect(m_game,&GameService::downloadChanged,this,[this](const QVariantMap&){emit downloadStateChanged();});
}
void ServiceHub::add(const QString &name,FeatureService*service){m_services[name]=service;
    connect(service,&FeatureService::completed,this,[this,name](const QString&a,const QVariantMap&data){emit completed(name,a,data);if(name=="accounts")emit accountChanged();if(name=="instances"||a=="game.install"||a=="game.queued"||a=="pack.import")emit instancesChanged();});
    connect(service,&FeatureService::failed,this,[this,name](const QString&a,const QString&message){emit failed(name,a,message);});
    connect(service,&FeatureService::activity,this,[this](const QString&message){QFile file(m_root+"/logs/launcher.log");if(file.open(QIODevice::Append))file.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODate)+" "+message+'\n').toUtf8());emit activity(message);});
}
QVariantList ServiceHub::modules()const{return {QVariantMap{{"id","instances"},{"title","游戏实例"},{"icon","grid"}},QVariantMap{{"id","game"},{"title","版本与启动"},{"icon","play"}},QVariantMap{{"id","java"},{"title","Java 运行时"},{"icon","cpu"}},QVariantMap{{"id","accounts"},{"title","账号与衣柜"},{"icon","user"}},QVariantMap{{"id","resources"},{"title","资源与服务器"},{"icon","folder"}},QVariantMap{{"id","community"},{"title","社区资源"},{"icon","globe"}},QVariantMap{{"id","packs"},{"title","整合包"},{"icon","layers"}},QVariantMap{{"id","personalization"},{"title","个性化与插件"},{"icon","sparkles"}},QVariantMap{{"id","connectivity"},{"title","联机"},{"icon","link"}},QVariantMap{{"id","maintenance"},{"title","诊断与更新"},{"icon","settings"}},QVariantMap{{"id","bridge"},{"title","MOD 面板"},{"icon","cpu"}}};}
QVariantList ServiceHub::instances()const{return static_cast<InstanceService*>(m_services.value("instances"))->list();}
QVariantList ServiceHub::actions(const QString &module)const{return m_services.contains(module)?m_services[module]->actions():QVariantList{};}
QVariantMap ServiceHub::downloadState()const{return m_game->downloadState();}
QString ServiceHub::selectedAccountName()const{return m_accounts->displayName();}
void ServiceHub::execute(const QString &module,const QString&a,const QVariantMap&args){if(!m_services.contains(module)){emit failed(module,a,"功能模块不存在");return;}m_services[module]->execute(a,args);}

QVariantList ServiceHub::downloadTasks()const{
    QVariantList result;
    auto game=m_game->downloadState();game["module"]="game";game["prefix"]="download";if(!game.contains("title"))game["title"]="游戏安装";
    auto community=m_community->downloadState();community["module"]="community";community["prefix"]="community";if(!community.contains("title"))community["title"]="社区资源";
    auto packs=m_packs->downloadState();packs["module"]="packs";packs["prefix"]="pack";if(!packs.contains("title"))packs["title"]="整合包";
    for(const auto &item:{game,community,packs})if(item.value("active").toBool()||item.value("totalFiles").toInt()>0||!item.value("phase").toString().isEmpty())result.append(item);
    return result;
}

QVariantMap ServiceHub::appearance()const{return m_personalization->settings();}
QString ServiceHub::downloadSource()const{return m_game->downloadSource();}

void ServiceHub::setDefaultMemory(int gb){m_game->setDefaultMemory(gb);}
