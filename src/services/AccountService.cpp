#include "AccountService.h"
#include "ServiceUtils.h"
#include "ArchiveUtils.h"
#include <QJsonArray>
#include <QUrlQuery>
#include <QUuid>
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QImage>
#include <QDateTime>
#include <QRegularExpression>
using namespace ServiceUtils;
namespace {
// KAMUCL 的公开桌面客户端身份（不是密钥）。设备码授权只要求公共客户端 ID，
// 用户可以在账号面板里换用自己的 Microsoft OAuth 应用。
constexpr auto kDefaultMicrosoftClientId = "06cc8cf6-4b8f-4631-8459-b2bd828808f8";
QByteArray json(const QVariantMap &map){return QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact);}
QByteArray form(const QVariantMap &map){QUrlQuery q;for(auto i=map.begin();i!=map.end();++i)q.addQueryItem(i.key(),i.value().toString());return q.query(QUrl::FullyEncoded).toUtf8();}
QString id(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QString offlineUuid(const QString &name){auto b=QCryptographicHash::hash(("OfflinePlayer:"+name).toUtf8(),QCryptographicHash::Md5);b[6]=char((b[6]&0x0f)|0x30);b[8]=char((b[8]&0x3f)|0x80);return QString::fromLatin1(b.toHex());}
bool loopback(const QUrl &url){const auto host=url.host();return host=="127.0.0.1"||host=="::1"||host=="localhost";}
QString canonicalUuid(const QString &value){return AccountService::normalizedMinecraftUuid(value);}
QString oauthFailure(int status,const QJsonObject &data,const QString &fallback){
    const auto code=data.value("error").toString();
    const auto fullDescription=data.value("error_description").toString().trimmed();
    const auto description=fullDescription.section('\n',0,0).left(180);
    // /consumers 端点只接受个人 Microsoft 账户；工作/学校账号（含 xxx.onmicrosoft.com）会被微软拒绝。
    // 微软常把这类拒绝包在 invalid_grant 里，所以先按 AADSTS 描述识别，再走通用错误映射。
    const auto lowerDescription=fullDescription.toLower();
    if(fullDescription.contains("AADSTS50020")||fullDescription.contains("AADSTS50059")||fullDescription.contains("AADSTS50105")||fullDescription.contains("AADSTS90072")
       ||lowerDescription.contains("work or school account")||lowerDescription.contains("does not exist in tenant 'microsoft accounts'"))
        return "这是工作/学校 Microsoft 账户（例如 xxx.onmicrosoft.com），本项目只支持个人 Microsoft 账户。请改用个人 Microsoft 账户登录；如果只是想购买/游玩 Minecraft Java Edition，也需要个人 Microsoft 账户。"+(description.isEmpty()?QString():QString("（"+description+"）"));
    if(code=="invalid_client"||code=="unauthorized_client"||code=="invalid_scope")
        return "微软应用身份被拒绝（"+code+"）。请确认 Client ID 是允许公共客户端设备码登录的应用，或在账号面板填写自己的 Client ID。";
    if(code=="access_denied"||code=="authorization_declined")return "已拒绝微软授权，可重新开始登录。";
    if(code=="expired_token"||code=="bad_verification_code")return "设备码已过期或无效，请重新开始登录。";
    if(code=="invalid_grant"||code=="interaction_required"||code=="login_required")return "微软授权已失效，请重新登录。";
    if(status==429)return "微软认证请求过于频繁，请稍后再试。";
    if(status>=500)return "微软认证服务暂时不可用，请稍后重试。";
    if(!description.isEmpty())return fallback+"："+description;
    return fallback+(status>0?QString("（HTTP %1）").arg(status):QString());
}
QString xboxFailure(const QJsonObject &data){
    const auto raw=data.value("XErr").toVariant();
    const auto xerr=raw.toString().isEmpty()?raw.toLongLong():raw.toString().toLongLong();
    switch(xerr){
    case 2148916233:return "此微软账号还没有 Xbox 档案。请先在 xbox.com 登录并创建档案后重试。";
    case 2148916235:return "此微软账号所在地区无法使用 Xbox Live。";
    case 2148916238:return "此微软账号是儿童账号，需要成人账号在家庭组中允许后重试。";
    default:
        if(!raw.toString().isEmpty()&&raw.toString()!="0")return "Xbox 授权失败（XErr "+raw.toString()+"），请确认该账号可以登录 Xbox Live。";
        return "Xbox 授权失败，请确认该账号可以登录 Xbox Live。";
    }
}
}
QString AccountService::defaultMicrosoftClientId(){return QString::fromLatin1(kDefaultMicrosoftClientId);}
QString AccountService::normalizedMinecraftUuid(const QString &value){
    auto hex=value;hex.remove('-');hex=hex.toLower();
    if(!QRegularExpression("^[0-9a-f]{32}$").match(hex).hasMatch())return value.toLower();
    return hex.left(8)+'-'+hex.mid(8,4)+'-'+hex.mid(12,4)+'-'+hex.mid(16,4)+'-'+hex.mid(20);
}
AccountService::AccountService(QString root,QObject *parent)
    :AccountService(std::move(root),parent,Endpoints{},nullptr) {}
AccountService::AccountService(QString root,QObject *parent,Endpoints endpoints,std::unique_ptr<SecretStore> secrets)
    :FeatureService(std::move(root),parent),m_network(this),m_endpoints(std::move(endpoints)),
     m_secrets(secrets?std::move(secrets):platformSecretStore(m_root)) {
    try{m_data=read(m_root+"/accounts.json",true);}catch(...){m_data={};}
    connect(&m_poll,&QTimer::timeout,this,&AccountService::microsoftPoll);m_poll.setSingleShot(true);
}
void AccountService::save(){write(m_root+"/accounts.json",m_data);}
QVariantMap AccountService::selected()const{for(auto v:m_data.value("accounts").toList())if(v.toMap().value("id")==m_data.value("selected"))return v.toMap();return {};}
QVariantMap AccountService::secret(const QString &key)const{QString error;auto bytes=m_secrets->get(key,&error);require(!bytes.isEmpty(),error);auto doc=QJsonDocument::fromJson(bytes);require(doc.isObject(),"凭据数据损坏");return doc.object().toVariantMap();}
QVariantMap AccountService::launchIdentity()const{try{auto account=selected();require(!account.isEmpty(),"请先添加并选择账号");if(account.value("type")=="offline")return {{"name",account.value("name")},{"uuid",account.value("uuid")},{"type","offline"},{"accessToken","0"}};auto credentials=secret(account.value("id").toString());require(credentials.value("expires",QDateTime::currentSecsSinceEpoch()+60).toLongLong()>QDateTime::currentSecsSinceEpoch(),"账号凭据已过期，请先刷新账号");return {{"name",account.value("name")},{"uuid",account.value("uuid")},{"type",account.value("type")},{"accessToken",credentials.value("accessToken")},{"providerRoot",account.value("providerRoot")}};}catch(const std::exception&e){return {{"error",QString::fromUtf8(e.what())}};}}
QUrl AccountService::endpoint(const QUrl &base,const QString &path)const{
    auto text=base.toString();while(text.endsWith('/'))text.chop(1);
    return QUrl(text+path);
}
QVariantList AccountService::actions()const{
    const auto savedClient=m_data.value("clientId").toString().trimmed();
    const auto clientField=field("clientId","Microsoft OAuth Client ID（默认使用内置公共客户端，可覆盖）","text",savedClient.isEmpty()?defaultMicrosoftClientId():savedClient);
    return {
    action("account.list","账号列表"),action("offline.add","添加离线账号",{field("name","角色名（字母、数字、下划线）","text",{},true)}),
    action("microsoft.login","微软设备码登录",{clientField},"点击执行后按提示在浏览器输入设备码；需要拥有 Minecraft Java Edition。默认使用内置公共客户端，无需自己注册 OAuth 应用。仅支持个人 Microsoft 账户；工作/学校账号（含 xxx.onmicrosoft.com）不可用，请改用个人账户。"),
    action("microsoft.cancel","取消设备码登录"),action("account.select","选择账号",{field("id","账号 ID","text",{},true)}),action("account.refresh","刷新已选账号"),action("account.remove","移除账号",{field("id","账号 ID","text",{},true)}),
    action("provider.list","认证提供商"),action("provider.add","添加 Yggdrasil 提供商",{field("name","名称","text",{},true),field("url","认证根地址（HTTPS）","text",{},true)}),action("provider.remove","删除提供商",{field("id","提供商 ID","text",{},true)}),
    action("yggdrasil.login","Yggdrasil 登录",{field("providerId","提供商 ID","text",{},true),field("username","用户名 / 邮箱","text",{},true),field("password","密码","password",{},true)}),
    action("yggdrasil.select","选择登录角色",{field("profileId","角色 ID","text",{},true)},"多角色账号登录后，点击结果中的角色，再执行此操作。"),action("skin.list","皮肤历史"),action("skin.import","导入本地皮肤",{field("path","皮肤 PNG","path",{},true),field("variant","模型","choice","classic",false,{"classic","slim"})}),
    action("skin.upload","上传微软账号皮肤",{field("path","皮肤 PNG","path",{},true),field("variant","模型","choice","classic",false,{"classic","slim"})}),
    action("cape.list","查看拥有的披风"),action("cape.select","切换披风",{field("capeId","披风 ID（留空移除）")},"官方账号只能选择已拥有的披风，不能上传任意披风。")};}
void AccountService::storeAccount(QVariantMap account,QVariantMap credentials,const QString &a){
    QString key=account.value("id").toString();if(key.isEmpty())key=id();account["id"]=key;
    if(!credentials.isEmpty()){QString error;require(m_secrets->put(key,json(credentials),&error),error);}
    auto items=m_data.value("accounts").toList();for(qsizetype i=items.size()-1;i>=0;--i)if(items[i].toMap().value("id")==key)items.removeAt(i);items.append(account);m_data["accounts"]=items;m_data["selected"]=key;save();
    emit completed(a,{{"accountId",key},{"message","账号已保存并选中："+account.value("name").toString()},{"items",items}});
}
void AccountService::request(const QString &a,const QUrl &url,const QByteArray &method,const QByteArray &body,const QByteArray &contentType,Callback callback,const QString &bearer){
    // 认证端点固定为官方 HTTPS 服务；仅在测试注入回环夹具时允许本机 HTTP。
    const bool allowLoopback=loopback(url);
    if(!https(url,allowLoopback)){emit failed(a,"认证地址必须是没有嵌入凭据的 HTTPS 地址");return;}
    QNetworkRequest req(url);req.setTransferTimeout(30000);req.setHeader(QNetworkRequest::ContentTypeHeader,QString::fromUtf8(contentType));req.setRawHeader("User-Agent","XMCL/0.2");req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);if(!bearer.isEmpty())req.setRawHeader("Authorization",("Bearer "+bearer).toUtf8());
    const auto generation=m_loginGeneration;auto *reply=m_network.sendCustomRequest(req,method,body);connect(reply,&QNetworkReply::downloadProgress,this,[reply](qint64 n,qint64){if(n>4*1024*1024)reply->abort();});connect(reply,&QNetworkReply::finished,this,[this,a,reply,callback,generation]{if(a=="microsoft.login"&&generation!=m_loginGeneration){reply->deleteLater();return;}auto bytes=reply->readAll();auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();auto obj=QJsonDocument::fromJson(bytes).object();const auto networkError=reply->error();reply->deleteLater();if(networkError!=QNetworkReply::NoError||status<200||status>=300){emit failed(a,oauthFailure(status,obj,"认证请求失败"));return;}try{callback(obj);}catch(const std::exception&e){emit failed(a,QString::fromUtf8(e.what()));}});
}
void AccountService::failMicrosoft(const QString &message){
    m_poll.stop();m_deviceCode.clear();m_transientFailures=0;emit failed("microsoft.login",message);
}
void AccountService::microsoftExchange(const QString &a,const QJsonObject &tokens,const QString &clientId){
    const auto access=tokens.value("access_token").toString(),refresh=tokens.value("refresh_token").toString();require(!access.isEmpty(),"微软登录未返回访问令牌");
    QVariantMap xbl{{"Properties",QVariantMap{{"AuthMethod","RPS"},{"SiteName","user.auth.xboxlive.com"},{"RpsTicket","d="+access}}},{"RelyingParty","http://auth.xboxlive.com"},{"TokenType","JWT"}};
    request(a,m_endpoints.xboxUserAuth,"POST",json(xbl),"application/json",[this,a,refresh,clientId](QJsonObject x){const auto token=x.value("Token").toString();require(!token.isEmpty(),"Xbox 登录未返回令牌");
        request(a,m_endpoints.xboxXsts,"POST",json({{"Properties",QVariantMap{{"SandboxId","RETAIL"},{"UserTokens",QVariantList{token}}}},{"RelyingParty","rp://api.minecraftservices.com/"},{"TokenType","JWT"}}),"application/json",[this,a,refresh,clientId](QJsonObject xsts){const auto token=xsts.value("Token").toString();require(!token.isEmpty(),xboxFailure(xsts));const auto xui=xsts.value("DisplayClaims").toObject().value("xui").toArray();const auto uhs=xui.isEmpty()?QString():xui.first().toObject().value("uhs").toString();require(!uhs.isEmpty(),"Xbox 授权信息不完整");
            request(a,m_endpoints.minecraftLogin,"POST",json({{"identityToken","XBL3.0 x="+uhs+";"+token}}),"application/json",[this,a,refresh,clientId](QJsonObject mc){const auto token=mc.value("access_token").toString();require(!token.isEmpty(),"Minecraft 授权失败，请确认账号可以登录游戏服务");
                const auto expiresIn=mc.value("expires_in").toInt(86400);
                request(a,m_endpoints.minecraftEntitlements,"GET",{},"application/json",[this,a,token,refresh,clientId,expiresIn](QJsonObject entitlements){bool owned=false;for(auto v:entitlements.value("items").toArray())if(v.toObject().value("name").toString()=="product_minecraft")owned=true;require(owned,"此账号没有 Minecraft Java Edition 所有权；请确认登录的是已购买游戏的微软账号");
                    request(a,m_endpoints.minecraftProfile,"GET",{},"application/json",[this,a,token,refresh,clientId,expiresIn](QJsonObject profile){const auto rawId=profile.value("id").toString();const auto uuid=normalizedMinecraftUuid(rawId);require(QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$").match(uuid).hasMatch(),"Minecraft 档案 ID 无效");const auto name=profile.value("name").toString();require(QRegularExpression("^[A-Za-z0-9_]{1,16}$").match(name).hasMatch(),"Minecraft 档案名称无效或尚未创建");QString key;for(auto v:m_data.value("accounts").toList()){const auto row=v.toMap();if(row.value("type")=="microsoft"&&canonicalUuid(row.value("uuid").toString())==uuid)key=row.value("id").toString();}
                        storeAccount({{"id",key},{"name",name},{"title",name},{"subtitle","微软账号"},{"uuid",uuid},{"type","microsoft"},{"profile",profile.toVariantMap()}},{{"accessToken",token},{"refreshToken",refresh},{"clientId",clientId},{"expires",QDateTime::currentSecsSinceEpoch()+expiresIn-60}},a);},token);
                },token);
            });
        });
    });
}
void AccountService::microsoftPoll(){
    if(m_deviceCode.isEmpty())return;
    if(QDateTime::currentSecsSinceEpoch()>=m_expires){failMicrosoft("设备码已过期，请重新开始微软登录。");return;}
    QNetworkRequest req(endpoint(m_endpoints.microsoftAuthority,"/token"));req.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");req.setTransferTimeout(30000);
    const auto generation=m_deviceCode;auto *reply=m_network.post(req,form({{"grant_type","urn:ietf:params:oauth:grant-type:device_code"},{"client_id",m_clientId},{"device_code",m_deviceCode}}));
    connect(reply,&QNetworkReply::finished,this,[this,reply,generation]{
        auto value=QJsonDocument::fromJson(reply->readAll()).object();const auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const auto networkError=reply->error();reply->deleteLater();if(generation!=m_deviceCode)return;
        const auto error=value.value("error").toString();
        if(error=="authorization_pending"){m_transientFailures=0;m_poll.start(m_pollIntervalMs);return;}
        if(error=="slow_down"){m_transientFailures=0;m_pollIntervalMs=qMin(30000,m_pollIntervalMs+5000);m_poll.start(m_pollIntervalMs);return;}
        if(error.isEmpty()&&(status==429||status==502||status==503||status==504||(status==0&&networkError!=QNetworkReply::NoError))){
            if(++m_transientFailures>=3){failMicrosoft(status==429?"微软认证请求过于频繁，请稍后再试。":"微软认证服务暂时不可用，请检查网络后重试。");return;}
            m_poll.start(qMin(30000,5000*(1<<(m_transientFailures-1))));return;
        }
        m_transientFailures=0;m_poll.stop();
        if(!error.isEmpty()||networkError!=QNetworkReply::NoError||status<200||status>=300){failMicrosoft(oauthFailure(status,value,error.isEmpty()?"设备码登录请求失败":error));return;}
        m_deviceCode.clear();
        try{microsoftExchange("microsoft.login",value,m_clientId);}catch(const std::exception&e){emit failed("microsoft.login",QString::fromUtf8(e.what()));}
    });
}
void AccountService::wardrobe(const QString &a,const QVariantMap &args){
    auto history=read(m_root+"/wardrobe/history.json",true).value("items").toList();
    if(a=="skin.list"){emit completed(a,{{"items",history},{"message","本地皮肤历史"}});return;}
    const auto path=localPath(args.value("path"));require(QFileInfo(path).size()<1024*1024,"皮肤文件超过 1 MB");QImage image(path);require(!image.isNull()&&image.width()==64&&(image.height()==64||image.height()==32),"皮肤必须是 64×64 或 64×32 PNG");require(QFileInfo(path).size()<1024*1024,"皮肤文件超过 1 MB");
    const auto variant=args.value("variant","classic").toString();require(variant=="classic"||variant=="slim","模型类型无效");
    require(QDir().mkpath(m_root+"/wardrobe"),"无法创建衣柜");const auto target=m_root+"/wardrobe/"+id()+".png";require(image.save(target,"PNG"),"皮肤保存失败");
    QVariantMap entry{{"id",QFileInfo(target).baseName()},{"title",QFileInfo(path).baseName()},{"subtitle",variant+" · "+QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm")},{"path",target},{"previewPath",target},{"variant",variant}};history.prepend(entry);write(m_root+"/wardrobe/history.json",{{"items",history}});
    if(a=="skin.import"){emit completed(a,{{"previewPath",target},{"variant",variant},{"items",history},{"message","皮肤已加入本地衣柜"}});return;}
    auto account=selected();require(account.value("type")=="microsoft","皮肤上传需要选中微软账号");const auto credentials=secret(account.value("id").toString());auto *multi=new QHttpMultiPart(QHttpMultiPart::FormDataType,this);
    QHttpPart model;model.setHeader(QNetworkRequest::ContentDispositionHeader,"form-data; name=\"variant\"");model.setBody(variant.toUtf8());multi->append(model);
    auto *file=new QFile(target,multi);require(file->open(QIODevice::ReadOnly),"无法读取皮肤");QHttpPart part;part.setHeader(QNetworkRequest::ContentTypeHeader,"image/png");part.setHeader(QNetworkRequest::ContentDispositionHeader,"form-data; name=\"file\"; filename=\"skin.png\"");part.setBodyDevice(file);multi->append(part);
    QNetworkRequest req(m_endpoints.minecraftSkins);req.setRawHeader("Authorization",("Bearer "+credentials.value("accessToken").toString()).toUtf8());req.setTransferTimeout(30000);auto *reply=m_network.post(req,multi);multi->setParent(reply);connect(reply,&QNetworkReply::finished,this,[this,reply,a,target]{const auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const auto body=QJsonDocument::fromJson(reply->readAll()).object();const auto ok=reply->error()==QNetworkReply::NoError&&status>=200&&status<300;reply->deleteLater();if(ok)emit completed(a,{{"message","皮肤已上传"},{"previewPath",target}});else emit failed(a,oauthFailure(status,body,"皮肤上传失败，请刷新账号后重试"));});
}
void AccountService::execute(const QString &a,const QVariantMap &args){try{
    auto items=m_data.value("accounts").toList();
    if(a=="account.list"){emit completed(a,{{"items",items},{"message",selected().isEmpty()?"尚未选择账号":"当前账号："+selected().value("name").toString()}});return;}
    if(a=="offline.add"){
        const auto name=args.value("name").toString().trimmed();require(QRegularExpression("^[A-Za-z0-9_]{1,16}$").match(name).hasMatch(),"离线名称需为 1–16 位字母、数字或下划线");
        for(auto v:items)require(v.toMap().value("name").toString().compare(name,Qt::CaseInsensitive)!=0,"该名称账号已存在");
        storeAccount({{"id",id()},{"type","offline"},{"name",name},{"uuid",offlineUuid(name)},{"title",name},{"subtitle","离线账号"}},{},a);return;
    }
    if(a=="account.select"||a=="account.remove"){
        auto key=args.value("id").toString();bool found=false;
        for(qsizetype i=items.size()-1;i>=0;--i)if(items[i].toMap().value("id")==key){found=true;if(a=="account.remove"){QString error;require(m_secrets->remove(key,&error),error);items.removeAt(i);}}
        require(found,"账号不存在");m_data["accounts"]=items;if(a=="account.select")m_data["selected"]=key;else if(m_data.value("selected")==key)m_data["selected"]=items.isEmpty()?QString():items.first().toMap().value("id");save();emit completed(a,{{"items",items},{"message","账号已更新"}});return;
    }
    if(a.startsWith("provider.")){
        auto providers=m_data.value("providers").toList();
        if(a=="provider.add"){auto url=QUrl(args.value("url").toString());require(https(url),"提供商地址必须为 HTTPS，且不能包含用户名或密码");require(!args.value("name").toString().isEmpty(),"提供商名称不能为空");providers.append(QVariantMap{{"id",id()},{"name",args.value("name")},{"title",args.value("name")},{"url",url.toString(QUrl::RemoveFragment)},{"subtitle",url.host()}});}
        else if(a=="provider.remove"){bool found=false;for(qsizetype i=providers.size()-1;i>=0;--i)if(providers[i].toMap().value("id")==args.value("id")){providers.removeAt(i);found=true;}require(found,"提供商不存在");}else require(a=="provider.list","未知提供商操作");
        m_data["providers"]=providers;save();emit completed(a,{{"items",providers},{"message","认证提供商配置"}});return;
    }
    if(a=="microsoft.cancel"){++m_loginGeneration;m_poll.stop();m_deviceCode.clear();m_transientFailures=0;emit completed(a,{{"message","登录已取消"}});return;}
    if(a=="microsoft.login"){
        const auto requested=args.value("clientId").toString().trimmed();
        m_clientId=requested.isEmpty()?defaultMicrosoftClientId():requested;
        require(!QUuid(m_clientId).isNull(),"Microsoft OAuth Client ID 格式无效");
        require(m_deviceCode.isEmpty(),"设备码登录正在进行，请先完成或取消当前登录");
        if(!requested.isEmpty()&&requested!=defaultMicrosoftClientId())m_data["clientId"]=m_clientId;else m_data.remove("clientId");
        save();++m_loginGeneration;m_deviceCode.clear();m_transientFailures=0;
        request(a,endpoint(m_endpoints.microsoftAuthority,"/devicecode"),"POST",form({{"client_id",m_clientId},{"scope","XboxLive.signin offline_access"}}),"application/x-www-form-urlencoded",[this,a](QJsonObject v){
            m_deviceCode=v.value("device_code").toString();require(!m_deviceCode.isEmpty(),"微软未返回设备码");
            m_expires=QDateTime::currentSecsSinceEpoch()+qBound(60,v.value("expires_in").toInt(900),3600);
            const auto userCode=v.value("user_code").toString();const auto uri=v.value("verification_uri").toString();
            require(!userCode.isEmpty(),"微软未返回设备码中的用户代码");
            m_pollIntervalMs=qBound(1000,v.value("interval").toInt(5)*1000,30000);
            emit completed(a,{{"message","微软设备码登录：请在浏览器输入代码 "+userCode},
                              {"details","1. 打开 "+uri+"\n2. 输入代码："+userCode+"\n3. 使用拥有 Minecraft Java Edition 的微软账号完成授权；本窗口会自动等待并保存账号。"},
                              {"url",uri},{"userCode",userCode},{"verificationUri",uri},{"expiresIn",v.value("expires_in").toInt(900)}});
            m_poll.start(m_pollIntervalMs);
        });return;
    }
    if(a=="yggdrasil.login"){
        QVariantMap provider;for(auto v:m_data.value("providers").toList())if(v.toMap().value("id")==args.value("providerId"))provider=v.toMap();require(!provider.isEmpty(),"请先添加认证提供商");require(!args.value("username").toString().isEmpty()&&!args.value("password").toString().isEmpty(),"用户名和密码不能为空");
        QString base=provider.value("url").toString();while(base.endsWith('/'))base.chop(1);const auto clientToken=id();
        request(a,QUrl(base+"/authserver/authenticate"),"POST",json({{"agent",QVariantMap{{"name","Minecraft"},{"version",1}}},{"username",args.value("username")},{"password",args.value("password")},{"clientToken",clientToken},{"requestUser",true}}),"application/json",[this,a,base,clientToken](QJsonObject v){auto profiles=v.value("availableProfiles").toArray();auto selected=v.value("selectedProfile").toObject();if(selected.isEmpty()&&profiles.size()==1)selected=profiles.first().toObject();require(!v.value("accessToken").toString().isEmpty(),"提供商未返回令牌");
            if(selected.isEmpty()){
                require(!profiles.isEmpty(),"认证账号没有 Minecraft 角色");QVariantList choices;for(const auto &entry:profiles){auto row=entry.toObject().toVariantMap();row["profileId"]=row.value("id");row["title"]=row.value("name");row["subtitle"]="选择此角色登录";choices.append(row);}
                m_yggPending={{"base",base},{"accessToken",v.value("accessToken").toString()},{"clientToken",v.value("clientToken").toString(clientToken)},{"profiles",profiles.toVariantList()},{"expires",QDateTime::currentSecsSinceEpoch()+300}};
                emit completed(a,{{"items",choices},{"message","请选择一个角色，再执行「选择登录角色」"}});return;
            }storeAccount({{"id",id()},{"type","yggdrasil"},{"providerRoot",base},{"name",selected.value("name").toString()},{"title",selected.value("name").toString()},{"uuid",selected.value("id").toString()},{"subtitle",QUrl(base).host()}},{{"accessToken",v.value("accessToken").toString()},{"clientToken",v.value("clientToken").toString(clientToken)},{"expires",QDateTime::currentSecsSinceEpoch()+86400}},a);});return;
    }
    if(a=="yggdrasil.select"){
        require(m_yggPending.value("expires").toLongLong()>QDateTime::currentSecsSinceEpoch(),"角色选择已过期，请重新登录");
        QVariantMap profile;for(const auto &entry:m_yggPending.value("profiles").toList())if(entry.toMap().value("id")==args.value("profileId"))profile=entry.toMap();require(!profile.isEmpty(),"请选择登录结果中的角色");
        const auto pending=m_yggPending;m_yggPending.clear();const auto base=pending.value("base").toString();
        request(a,QUrl(base+"/authserver/refresh"),"POST",json({{"accessToken",pending.value("accessToken")},{"clientToken",pending.value("clientToken")},{"selectedProfile",profile},{"requestUser",true}}),"application/json",[this,a,base,pending,profile](QJsonObject v){const auto token=v.value("accessToken").toString();require(!token.isEmpty(),"角色选择未返回访问令牌");storeAccount({{"id",id()},{"type","yggdrasil"},{"providerRoot",base},{"name",profile.value("name")},{"title",profile.value("name")},{"uuid",profile.value("id")},{"subtitle",QUrl(base).host()}},{{"accessToken",token},{"clientToken",v.value("clientToken").toString(pending.value("clientToken").toString())},{"expires",QDateTime::currentSecsSinceEpoch()+86400}},a);});return;
    }
    if(a=="account.refresh"){
        const auto account=selected();require(!account.isEmpty(),"请先选择账号");if(account.value("type")=="offline"){emit completed(a,{{"message","离线账号无需刷新"}});return;}
        const auto creds=secret(account.value("id").toString());if(account.value("type")=="microsoft"){
            const auto clientId=creds.value("clientId").toString().isEmpty()?defaultMicrosoftClientId():creds.value("clientId").toString();
            const auto refreshToken=creds.value("refreshToken").toString();require(!refreshToken.isEmpty(),"账号缺少刷新令牌，请重新登录");
            request(a,endpoint(m_endpoints.microsoftAuthority,"/token"),"POST",form({{"client_id",clientId},{"refresh_token",refreshToken},{"grant_type","refresh_token"},{"scope","XboxLive.signin offline_access"}}),"application/x-www-form-urlencoded",[this,a,creds,clientId](QJsonObject v){if(v.value("refresh_token").toString().isEmpty())v["refresh_token"]=creds.value("refreshToken").toString();microsoftExchange(a,v,clientId);});
        }else request(a,QUrl(account.value("providerRoot").toString()+"/authserver/refresh"),"POST",json({{"accessToken",creds.value("accessToken")},{"clientToken",creds.value("clientToken")},{"selectedProfile",QVariantMap{{"id",account.value("uuid")},{"name",account.value("name")}}}}),"application/json",[this,a,account,creds](QJsonObject v){auto updated=creds;updated["accessToken"]=v.value("accessToken").toString();require(!updated.value("accessToken").toString().isEmpty(),"刷新未返回令牌");updated["expires"]=QDateTime::currentSecsSinceEpoch()+86400;storeAccount(account,updated,a);});return;
    }
    if(a.startsWith("skin.")){wardrobe(a,args);return;}
    if(a=="cape.list"||a=="cape.select"){
        const auto account=selected();require(account.value("type")=="microsoft","请先选择微软账号");const auto creds=secret(account.value("id").toString());
        if(a=="cape.list")request(a,m_endpoints.minecraftProfile,"GET",{},"application/json",[this,a](QJsonObject v){QVariantList items;for(auto value:v.value("capes").toArray()){auto m=value.toObject().toVariantMap();m["title"]=m.value("alias");m["subtitle"]=m.value("state");items.append(m);}emit completed(a,{{"items",items},{"message","账号已拥有的披风"}});},creds.value("accessToken").toString());
        else{const auto cape=args.value("capeId").toString();request(a,m_endpoints.minecraftCapes,cape.isEmpty()?"DELETE":"PUT",cape.isEmpty()?QByteArray():json({{"capeId",cape}}),"application/json",[this,a](QJsonObject){emit completed(a,{{"message","披风选择已更新"}});},creds.value("accessToken").toString());}return;
    }
    throw std::runtime_error("未知账号操作");
}catch(const std::exception&e){emit failed(a,QString::fromUtf8(e.what()));}}
