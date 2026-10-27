#include "BridgeService.h"
#include "ServiceUtils.h"
#include "InstanceService.h"
#include "ArchiveUtils.h"
#include <QNetworkReply>
#include <QJsonArray>
using namespace ServiceUtils;
QVariantList BridgeService::actions()const{auto id=field("instanceId","实例 ID","text",{},true);return {action("bridge.install","安装 Bridge MOD",{id,field("jarPath","Bridge JAR（留空使用内置版本）","path")},"适用于 Fabric；只有注册 Bridge 接口的模组参数可被管理。"),action("bridge.status","连接状态",{id}),action("bridge.manifest","读取 MOD 参数",{id,field("query","筛选 MOD、分组或参数")}),action("bridge.set","修改参数",{id,field("paramId","参数 ID","text",{},true),field("value","新值（布尔、数字或文本）","text",{},true)}),action("bridge.reset","恢复默认参数",{id,field("paramId","参数 ID（留空恢复全部）")},"参数的实际生效时机由 MOD 决定。",true)};}
void BridgeService::execute(const QString&a,const QVariantMap&args){try{
    InstanceService instances(m_root);const auto instanceId=args.value("instanceId").toString();const auto game=instances.gamePath(instanceId);
    if(a=="bridge.install"){
        const auto meta=read(instances.instancePath(instanceId)+"/instance.json");require(meta.value("loader").toString().compare("fabric",Qt::CaseInsensitive)==0,"Bridge 需要 Fabric 实例");
        auto source=localPath(args.value("jarPath"));if(source.isEmpty())source=":/bridge/kamucl-bridge.jar";
        require(QFile::exists(source),"内置 Bridge 尚未构建，请运行 bridge/build.py 或选择 Bridge JAR");
        const auto target=ArchiveUtils::safePath(game,"mods/kamucl-bridge-1.0.1.jar");require(!QFile::exists(target),"Bridge 已安装，请先通过资源管理移除旧版本");require(QDir().mkpath(QFileInfo(target).absolutePath())&&QFile::copy(source,target),"安装 Bridge 失败");emit completed(a,{{"message","Bridge 已安装，启动游戏后可读取参数"}});return;
    }
    const auto info=read(ArchiveUtils::safePath(game,".kamucl-bridge.json"));const int port=info.value("port").toInt();const auto token=info.value("token").toString();require(info.value("protocol").toInt()==1&&port>0&&port<=65535&&token.size()>=16,"未发现有效的本机 Bridge 服务，请先启动已安装 Bridge 的游戏");
    QString endpoint;QVariantMap body;
    if(a=="bridge.status")endpoint="ping";else if(a=="bridge.manifest")endpoint="manifest";else if(a=="bridge.set"){
        endpoint="set";auto id=args.value("paramId").toString();require(!id.isEmpty(),"参数 ID 不能为空");const auto text=args.value("value").toString();auto doc=QJsonDocument::fromJson(('['+text+']').toUtf8());body={{"id",id},{"value",doc.isArray()&&doc.array().size()==1?doc.array().first().toVariant():QVariant(text)}};
    }else if(a=="bridge.reset"){require(args.value("confirm").toBool(),"请确认恢复默认值");endpoint="reset";if(!args.value("paramId").toString().isEmpty())body["id"]=args.value("paramId");}else throw std::runtime_error("未知 Bridge 操作");
    QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1/kamucl/v1/%2").arg(port).arg(endpoint)));request.setTransferTimeout(4000);request.setRawHeader("X-Kamucl-Token",token.toUtf8());request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    auto*reply=(endpoint=="set"||endpoint=="reset")?m_network.post(request,QJsonDocument::fromVariant(body).toJson(QJsonDocument::Compact)):m_network.get(request);
    connect(reply,&QNetworkReply::downloadProgress,this,[reply](qint64 size,qint64){if(size>4*1024*1024)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,a,args,endpoint]{auto doc=QJsonDocument::fromJson(reply->readAll());reply->deleteLater();if(reply->error()!=QNetworkReply::NoError||!doc.isObject()){emit failed(a,"Bridge 连接失败或响应无效，请检查游戏是否正在运行");return;}auto data=doc.object();if(data.contains("ok")&&!data.value("ok").toBool()){emit failed(a,data.value("error").toString("MOD 拒绝了该操作"));return;}
        if(endpoint=="manifest"){if(data.value("protocol").toInt()!=1||!data.value("params").isArray()){emit failed(a,"Bridge 参数协议不兼容");return;}QVariantList items;auto query=args.value("query").toString();for(auto v:data.value("params").toArray()){auto row=v.toObject().toVariantMap();const auto search=row.value("label").toString()+row.value("modId").toString()+row.value("group").toString();if(!search.contains(query,Qt::CaseInsensitive))continue;row["paramId"]=row.value("id");row["title"]=row.value("label");row["subtitle"]=row.value("modId").toString()+" / "+row.value("group").toString()+" · "+row.value("value").toString();items.append(row);}emit completed(a,{{"items",items},{"message","已从运行中的 MOD 读取参数"}});}
        else emit completed(a,{{"message",endpoint=="ping"?"Bridge 已连接":data.value("notice").toString("MOD 操作已确认")}});
    });
}catch(const std::exception&e){emit failed(a,QString::fromUtf8(e.what()));}}
