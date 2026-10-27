#include "ConnectivityService.h"
#include "ServiceUtils.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QPointer>
#include <QRegularExpression>
#include <QUrlQuery>
#include <utility>
#ifdef Q_OS_UNIX
#include <signal.h>
#endif
using namespace ServiceUtils;
namespace {
int port(const QVariant &value) {
    bool ok = false; const auto result = value.toString().toInt(&ok);
    require(ok && result > 0 && result <= 65535, "端口必须介于 1 与 65535 之间");
    return result;
}
QString host(QString value) {
    require(!value.isEmpty() && value.size() <= 253 && !value.contains(QRegularExpression("[\\s/@?#\\\\]")), "主机地址无效");
    QHostAddress ip;
    if (ip.setAddress(value)) return ip.toString();
    auto ace = QString::fromLatin1(QUrl::toAce(value));
    require(QRegularExpression("^(?=.{1,253}$)[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*\\.?$").match(ace).hasMatch(), "主机名无效");
    return ace.toLower();
}
QVariantMap endpoint(QString value) {
    value = value.trimmed(); int number = 25565; QString name;
    if (value.startsWith('[')) {
        auto end = value.indexOf(']'); require(end > 1, "IPv6 地址缺少右方括号");
        name = value.mid(1,end-1); QHostAddress ip;
        require(ip.setAddress(name) && ip.protocol() == QAbstractSocket::IPv6Protocol, "方括号内需要 IPv6 地址");
        auto tail = value.mid(end+1);
        if (!tail.isEmpty()) { require(tail.startsWith(':'), "地址格式无效"); number = port(tail.mid(1)); }
    } else if (value.count(':') == 1) {
        name = value.section(':',0,0); number = port(value.section(':',1));
    } else name = value;
    name = host(name);
    const auto address=(name.contains(':') ? '['+name+']' : name)+':'+QString::number(number);
    return {{"host",name},{"port",number},{"address",address},{"server",address}};
}
QString digest(const QString &file) {
    QFile input(file); require(input.open(QIODevice::ReadOnly),"无法读取联机工具");
    QCryptographicHash hash(QCryptographicHash::Sha256); require(hash.addData(&input),"联机工具校验失败");
    return QString::fromLatin1(hash.result().toHex());
}
QVariantMap binaryConfig(const QVariantMap &args) {
    QFileInfo file(localPath(args.value("binary")));
    require(file.isAbsolute() && file.isFile() && file.isExecutable(), "请选择本机可执行的官方联机工具绝对路径");
    auto path = file.canonicalFilePath(), hash = digest(path);
    const auto expected = args.value("sha256").toString().trimmed().toLower();
    require(expected.isEmpty() || (QRegularExpression("^[a-f0-9]{64}$").match(expected).hasMatch() && expected == hash), "工具 SHA-256 与提供的校验值不符");
    return {{"binary",path},{"sha256",hash}};
}
void verifyBinary(const QVariantMap &config) {
    require(!config.value("binary").toString().isEmpty(), "请先配置本地官方工具");
    require(digest(config.value("binary").toString()) == config.value("sha256").toString(), "工具内容已变化，请核对来源并重新配置");
}
QByteArray quoted(const QString &value) {
    auto data = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return data.mid(1,data.size()-2);
}
void privateProcess(QProcess &process) {
#ifdef Q_OS_UNIX
    QProcess::UnixProcessParameters parameters;
    parameters.flags = QProcess::UnixProcessFlag::CreateNewSession;
    process.setUnixProcessParameters(parameters);
#endif
    // Tools may print credentials; no raw child output crosses the service API.
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
}
}

ConnectivityService::ConnectivityService(QString root,QObject *parent)
    : FeatureService(std::move(root),parent),m_network(this),m_secrets(platformSecretStore(m_root+"/connectivity")) {
    m_network.setProxy(QNetworkProxy::NoProxy);
    privateProcess(m_frp); privateProcess(m_tc); privateProcess(m_discovery);
    for(auto *process:{&m_frp,&m_tc,&m_discovery}) {
        connect(process,&QProcess::started,this,[this,process]{m_processGroups[process]=process->processId();});
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process]{
            const auto group=m_processGroups.take(process);
#ifdef Q_OS_UNIX
            // A daemon's exit does not imply its EasyTier children exited. This
            // is only the new session created for our own QProcess above.
            if(group>1)::kill(-static_cast<pid_t>(group),SIGKILL);
#else
            Q_UNUSED(group);
#endif
        });
    }
    m_poll.setInterval(500); m_deadline.setSingleShot(true);
    connect(&m_poll,&QTimer::timeout,this,&ConnectivityService::pollTerracotta);
    connect(&m_deadline,&QTimer::timeout,this,[this]{failTerracotta("陶瓦操作超时，已终止本次自有进程");});
    connect(&m_frp,&QProcess::started,this,[this]{
        const auto action = std::exchange(m_frpStartAction,{});
        if (!action.isEmpty()) emit completed(action,{{"message","frpc 进程已启动；隧道是否注册成功请在自己的 FRP 服务端确认"},{"processRunning",true},{"tunnelVerified",false}});
    });
    connect(&m_frp,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error!=QProcess::FailedToStart)return;
        auto action=std::exchange(m_frpStartAction,{});m_frpDir.reset();
        if(!action.isEmpty())emit failed(action,"无法启动 frpc；请检查工具架构与执行权限");
    });
    connect(&m_frp,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){
        m_frpDir.reset(); auto start=std::exchange(m_frpStartAction,{}),stop=std::exchange(m_frpStopAction,{});
        if(!start.isEmpty())emit failed(start,"frpc 在启动期间退出");
        if(!stop.isEmpty())emit completed(stop,{{"message","自有 frpc 进程已停止"}});
        else emit activity(QString("frpc 已退出（%1），当前没有运行中的 FRP 隧道").arg(code));
    });
    connect(&m_tc,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart)failTerracotta("无法启动陶瓦工具；请检查架构与执行权限");
    });
    connect(&m_tc,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int,QProcess::ExitStatus){
        if(!m_stopping)failTerracotta("陶瓦进程退出，房间连接已关闭");
    });
}
ConnectivityService::~ConnectivityService() {
    ++m_generation; m_stopping=true; m_poll.stop();m_deadline.stop();
    for(auto *process:{&m_discovery,&m_tc,&m_frp}){
        process->disconnect(this);killOwned(*process,true);process->waitForFinished(1000);
    }
}
QVariantList ConnectivityService::actions()const {
    const auto binary=field("binary","官方可执行文件","path",{},true);
    const auto sha=field("sha256","官方 SHA-256（可选，用于核对来源）");
    return {
        action("connectivity.direct","解析 Minecraft 连接地址",{field("address","主机名或 IP:端口","text",{},true)},"返回可复制的游戏直连地址；不创建中继或自动开放路由器端口。"),
        action("connectivity.interfaces","显示本机局域网地址",{field("port","游戏局域网端口","number",25565)}),
        action("connectivity.resolve","解析服务器 DNS",{field("address","服务器地址","text",{},true)}),
        action("frp.configure","配置自己的 FRP",{binary,sha,field("server","FRP 服务端主机","text",{},true),field("serverPort","服务端端口","number",7000),field("localPort","本地游戏端口","number",25565),field("remotePort","远程映射端口","number",25565),field("token","服务端令牌（留空保留）","password"),field("clearToken","移除已保存令牌","bool",false)},"仅使用自己的 frpc 与服务端；令牌保存至系统钥匙串。"),
        action("frp.start","启动自己的 FRP 隧道",{},"启动时才连接已配置的 FRP 服务端。"),
        action("frp.stop","停止自己的 FRP 隧道"),
        action("frp.status","FRP 状态"),
        action("terracotta.configure","配置陶瓦官方工具",{binary,sha},"从官方发行页取得适合本机的工具；XMCL 记录摘要并在每次启动前检查，配置不会启动联网。"),
        action("terracotta.host","创建陶瓦房间",{field("player","玩家名称","text","XMCL",true)},"先在 Minecraft 中开放局域网；陶瓦扫描游戏并建立公网联机。"),
        action("terracotta.join","加入陶瓦房间",{field("room","房间码 U/XXXX-XXXX-XXXX-XXXX","text",{},true),field("player","玩家名称","text","XMCL",true)}),
        action("terracotta.reset","复位陶瓦房间"),
        action("terracotta.stop","关闭陶瓦房间"),
        action("terracotta.status","陶瓦状态"),
        action("connectivity.capabilities","查看联机支持范围")
    };
}
QVariantMap ConnectivityService::status()const {
    return {{"frpRunning",m_frp.state()!=QProcess::NotRunning},{"terracottaRunning",m_tc.state()!=QProcess::NotRunning},
            {"phase",m_tcPhase},{"room",m_tcRoom},{"address",m_tcAddress},{"server",m_tcAddress}};
}
void ConnectivityService::execute(const QString &action,const QVariantMap &args) {
    try {
        const auto folder=m_root+"/connectivity";
        if(action=="connectivity.capabilities") {
            emit completed(action,{{"directAvailable",true},{"frpAvailable",true},{"terracottaAvailable",true},{"voxlinkAvailable",false},
                {"message","支持直连地址、DNS、局域网地址、自有 frpc、陶瓦官方 CLI。VoxLink 原生信令、认证穿透、RUDP/TURN、心跳重连与模组同步协议尚未实现。不会返回模拟房间。"}});return;
        }
        if(action=="connectivity.direct") {auto data=endpoint(args.value("address").toString());data["message"]="在 Minecraft 多人游戏中使用此地址；能否连接取决于服务端、路由和防火墙";emit completed(action,data);return;}
        if(action=="connectivity.interfaces") {
            int number=port(args.value("port",25565));QVariantList items;
            for(const auto &iface:QNetworkInterface::allInterfaces()){
                if(!(iface.flags()&QNetworkInterface::IsUp)||iface.flags()&QNetworkInterface::IsLoopBack)continue;
                for(const auto &entry:iface.addressEntries()){
                    const auto ip=entry.ip();if(ip.isNull()||ip.isLoopback()||ip.isLinkLocal()||ip.isMulticast())continue;
                    auto data=endpoint((ip.protocol()==QAbstractSocket::IPv6Protocol?'['+ip.toString()+']':ip.toString())+':'+QString::number(number));
                    data["title"]=data.value("address");data["subtitle"]=iface.humanReadableName();items.append(data);
                }
            }
            emit completed(action,{{"items",items},{"message","同一局域网的玩家可尝试这些地址；此操作未开放公网端口"}});return;
        }
        if(action=="connectivity.resolve") {
            auto data=endpoint(args.value("address").toString());auto finished=std::make_shared<bool>(false);
            auto timer=new QTimer(this);timer->setSingleShot(true);
            int id=QHostInfo::lookupHost(data.value("host").toString(),this,[this,action,data,finished,timer](const QHostInfo &info)mutable{
                if(*finished)return;*finished=true;timer->stop();timer->deleteLater();
                if(info.error()!=QHostInfo::NoError){emit failed(action,"DNS 解析失败");return;}
                QVariantList ips;for(auto ip:info.addresses())ips.append(ip.toString());data["addresses"]=ips;
                data["message"]="已解析 A/AAAA 地址；当前不执行 Minecraft SRV 记录查询";emit completed(action,data);
            });
            connect(timer,&QTimer::timeout,this,[this,action,finished,timer,id]{if(*finished)return;*finished=true;QHostInfo::abortHostLookup(id);timer->deleteLater();emit failed(action,"DNS 解析超时");});timer->start(10000);return;
        }
        if(action=="frp.configure") {
            require(m_frp.state()==QProcess::NotRunning,"请先停止 FRP 隧道再配置");auto config=binaryConfig(args);
            config["server"]=host(args.value("server").toString().trimmed());config["serverPort"]=port(args.value("serverPort",7000));
            config["localPort"]=port(args.value("localPort",25565));config["remotePort"]=port(args.value("remotePort",25565));
            auto old=read(folder+"/frp.json",true);config["hasToken"]=old.value("hasToken",false);
            const auto token=args.value("token").toString();QString error;
            require(token.size()<=4096&&!token.contains(QChar(0))&&!token.contains('\n')&&!token.contains('\r'),"FRP 令牌格式无效");
            if(args.value("clearToken").toBool()){require(m_secrets->remove("frp-token",&error),error);config["hasToken"]=false;}
            if(!token.isEmpty()){require(m_secrets->put("frp-token",token.toUtf8(),&error),error);config["hasToken"]=true;}
            write(folder+"/frp.json",config);emit completed(action,{{"message","FRP 配置已保存，尚未启动连接"}});return;
        }
        if(action=="frp.start"){startFrp(action);return;}
        if(action=="frp.stop") {
            require(m_frpStopAction.isEmpty(),"FRP 正在停止");
            if(m_frp.state()==QProcess::NotRunning){emit completed(action,{{"message","FRP 已停止"}});return;}
            m_frpStopAction=action;killOwned(m_frp,false);
            QTimer::singleShot(1500,this,[this]{if(!m_frpStopAction.isEmpty())killOwned(m_frp,true);});return;
        }
        if(action=="terracotta.configure") {
            require(m_tc.state()==QProcess::NotRunning&&!m_stopping,"请先关闭陶瓦房间");
            write(folder+"/terracotta.json",binaryConfig(args));emit completed(action,{{"message","陶瓦工具已配置，尚未启动联网"}});return;
        }
        if(action=="terracotta.host"||action=="terracotta.join"){startTerracotta(action,args);return;}
        if(action=="terracotta.stop"){stopTerracotta(action);return;}
        if(action=="terracotta.reset") {
            require(m_tcPort>0&&!m_stopping&&m_tcAction.isEmpty(),"陶瓦尚未就绪或正忙");
            requestTerracotta("/state/ide",{},[this,action](QByteArray){m_tcRoom.clear();m_tcAddress.clear();m_tcPhase="idle";emit completed(action,status());},[this,action](QString error){emit failed(action,error);});return;
        }
        if(action=="frp.status"||action=="terracotta.status"){emit completed(action,status());return;}
        require(false,action.startsWith("voxlink.")?"VoxLink 完整原生协议尚未实现；请使用真实直连、FRP 或陶瓦联机":"未知联机操作");
    }catch(const std::exception &error){emit failed(action,QString::fromUtf8(error.what()));}
}

void ConnectivityService::startFrp(const QString &action) {
    require(m_frp.state()==QProcess::NotRunning&&m_frpStartAction.isEmpty()&&m_frpStopAction.isEmpty(),"已有自有 FRP 进程正在运行");
    const auto config=read(m_root+"/connectivity/frp.json",true);verifyBinary(config);
    const auto server=host(config.value("server").toString());
    const auto serverPort=port(config.value("serverPort")),localPort=port(config.value("localPort")),remotePort=port(config.value("remotePort"));
    require(QDir().mkpath(m_root+"/connectivity"),"无法创建联机目录");
    auto directory=std::make_unique<QTemporaryDir>(m_root+"/connectivity/frpc-XXXXXX");require(directory->isValid(),"无法创建 FRP 运行目录");
    QByteArray text="serverAddr = "+quoted(server)+"\nserverPort = "+QByteArray::number(serverPort)+"\ntransport.tls.enable = true\n";
    auto environment=QProcessEnvironment::systemEnvironment();environment.remove("FRP_TOKEN");
    if(config.value("hasToken").toBool()) {
        QString error;auto token=m_secrets->get("frp-token",&error);require(!token.isEmpty(),error.isEmpty()?"FRP 令牌不可用":error);
        environment.insert("FRP_TOKEN",QString::fromUtf8(token));text+="auth.method = \"token\"\nauth.token = \"{{ .Envs.FRP_TOKEN }}\"\n";
    }
    text+="\n[[proxies]]\nname = \"xmcl-minecraft\"\ntype = \"tcp\"\nlocalIP = \"127.0.0.1\"\nlocalPort = "+QByteArray::number(localPort)+"\nremotePort = "+QByteArray::number(remotePort)+"\n";
    const auto path=directory->path()+"/frpc.toml";QFile file(path);require(file.open(QIODevice::WriteOnly),"无法创建 FRP 配置");
    require(file.setPermissions(QFile::ReadOwner|QFile::WriteOwner)&&file.write(text)==text.size(),"无法写入私有 FRP 配置");file.close();
    m_frpDir=std::move(directory);m_frpStartAction=action;m_frp.setProcessEnvironment(environment);
    m_frp.setWorkingDirectory(m_frpDir->path());m_frp.start(config.value("binary").toString(),{"-c",path});
}

void ConnectivityService::startTerracotta(const QString &action,const QVariantMap &args) {
    require(m_tc.state()==QProcess::NotRunning&&m_tcAction.isEmpty()&&!m_stopping,"陶瓦已经运行或正在停止，请先关闭房间");
    const auto player=args.value("player","XMCL").toString().trimmed();
    require(!player.isEmpty()&&player.size()<=64&&!player.contains(QRegularExpression("[\\x00-\\x1f]")),"玩家名称无效");
    QString room;
    if(action=="terracotta.join") {
        room=args.value("room").toString().trimmed().toUpper();
        require(QRegularExpression("^U/[A-Z0-9]{4}(?:-[A-Z0-9]{4}){3}$").match(room).hasMatch(),"陶瓦房间码应为 U/XXXX-XXXX-XXXX-XXXX");
    }
    const auto config=read(m_root+"/connectivity/terracotta.json",true);verifyBinary(config);
    require(QDir().mkpath(m_root+"/connectivity"),"无法创建联机目录");
    auto directory=std::make_unique<QTemporaryDir>(m_root+"/connectivity/terracotta-XXXXXX");require(directory->isValid(),"无法创建陶瓦运行目录");
    m_tcDir=std::move(directory);++m_generation;m_tcAction=action;m_tcMode=action.endsWith("host")?"host":"join";
    m_tcPlayer=player;m_tcRoom=room;m_tcAddress.clear();m_tcPort=0;m_tcPhase="starting";m_requestPending=false;
    auto environment=QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_MACOS
    // Terracotta's Mac CLI discovers a daemon via its home directory. Isolate it
    // so neither startup nor shutdown can touch an existing HMCL daemon.
    environment.insert("HOME",m_tcDir->path());
    m_tc.setProcessEnvironment(environment);m_discovery.setProcessEnvironment(environment);
    m_tc.start(config.value("binary").toString(),{"--daemon"});
#else
    m_tc.setProcessEnvironment(environment);
#ifdef Q_OS_WIN
    m_tc.start(config.value("binary").toString(),{"--hmcl2",m_tcDir->path()+"/http"});
#else
    m_tc.start(config.value("binary").toString(),{"--hmcl",m_tcDir->path()+"/http"});
#endif
#endif
    m_deadline.start(120000);m_poll.start();emit activity("已启动自有陶瓦工具，等待本地控制端口");
}
void ConnectivityService::pollTerracotta() {
    if(m_stopping||m_tc.state()!=QProcess::Running||m_requestPending||!m_tcDir)return;
    if(!m_tcPort) {
        QFile file(m_tcDir->path()+"/http");
        if(file.open(QIODevice::ReadOnly)) {
            if(file.size()>4096){failTerracotta("陶瓦端口文件超过限制");return;}
            auto doc=QJsonDocument::fromJson(file.readAll());
            const auto value=doc.object().value("port");
            auto number=value.toInt();
            if(value.isDouble()&&number>0&&number<=65535&&value.toDouble()==number)m_tcPort=number;
        }
#ifdef Q_OS_MACOS
        if(!m_tcPort&&m_discovery.state()==QProcess::NotRunning) {
            m_discovery.start(m_tc.program(),{"--hmcl",m_tcDir->path()+"/http"});
            const auto generation=m_generation;
            QTimer::singleShot(5000,this,[this,generation]{if(generation==m_generation&&m_discovery.state()!=QProcess::NotRunning)killOwned(m_discovery,true);});
        }
#endif
        if(!m_tcPort)return;
        m_tcPhase=m_tcMode=="host"?"hosting":"joining";
        QUrlQuery query;query.addQueryItem("player",m_tcPlayer);if(m_tcMode=="join")query.addQueryItem("room",m_tcRoom);
        requestTerracotta(m_tcMode=="host"?"/state/scanning":"/state/guesting",query,[this](QByteArray){m_deadline.start(m_tcMode=="host"?30000:60000);},[this](QString error){failTerracotta(error);});return;
    }
    if(m_tcAction.isEmpty())return;
    requestTerracotta("/state",{},[this](QByteArray bytes){
        auto doc=QJsonDocument::fromJson(bytes);
        if(!doc.isObject()){failTerracotta("陶瓦状态响应不是 JSON 对象");return;}
        auto value=doc.object();auto state=value.value("state").toString();
        if(QStringList{"exception","fatal","failed"}.contains(state)){failTerracotta("陶瓦报告连接失败，房间已关闭");return;}
        if(m_tcMode=="host"&&state=="host-ok") {
            const auto room=value.value("room").toString();
            if(!QRegularExpression("^U/[A-Z0-9]{4}(?:-[A-Z0-9]{4}){3}$").match(room).hasMatch()){failTerracotta("陶瓦返回的房间码无效");return;}
            m_tcRoom=room;
        }else if(m_tcMode=="join"&&state=="guest-ok") {
            try{m_tcAddress=endpoint(value.value("url").toString()).value("address").toString();}
            catch(...){failTerracotta("陶瓦返回的游戏地址无效");return;}
        }else return;
        m_tcPhase="ready";m_poll.stop();m_deadline.stop();auto action=std::exchange(m_tcAction,{});
        auto result=status();result["message"]=m_tcMode=="host"?"陶瓦房间已就绪":"陶瓦已就绪，请在游戏内连接返回地址";emit completed(action,result);
    },[this](QString error){failTerracotta(error);});
}
void ConnectivityService::requestTerracotta(const QString &path,const QUrlQuery &query,
                                             std::function<void(QByteArray)> done,std::function<void(QString)> error) {
    if(m_requestPending){error("已有陶瓦控制请求正在进行");return;}
    if(m_tcPort<1){error("陶瓦控制端口尚未就绪");return;}
    QUrl url;url.setScheme("http");url.setHost("127.0.0.1");url.setPort(m_tcPort);url.setPath(path);url.setQuery(query);
    QNetworkRequest request(url);request.setTransferTimeout(8000);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    auto reply=m_network.get(request);m_requestPending=true;const auto generation=m_generation;
    auto buffer=std::make_shared<QByteArray>();auto oversized=std::make_shared<bool>(false);
    reply->setReadBufferSize(64*1024);
    auto consume=[reply,buffer,oversized]{
        buffer->append(reply->readAll());if(buffer->size()>64*1024){*oversized=true;buffer->clear();reply->abort();}
    };
    connect(reply,&QIODevice::readyRead,this,consume);
    auto deadline=new QTimer(reply);deadline->setSingleShot(true);connect(deadline,&QTimer::timeout,reply,&QNetworkReply::abort);deadline->start(10000);
    connect(reply,&QNetworkReply::finished,this,[this,reply,generation,buffer,oversized,done,error,consume]{
        if(reply->isOpen())consume();
        const auto code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();auto failure=reply->error();reply->deleteLater();
        if(generation!=m_generation)return;m_requestPending=false;
        if(*oversized){error("陶瓦控制响应超过 64 KiB 限制");return;}
        if(failure!=QNetworkReply::NoError||code<200||code>=300){error(QString("陶瓦本地请求失败（HTTP %1）").arg(code));return;}
        done(*buffer);
    });
}
void ConnectivityService::killOwned(QProcess &process,bool force) {
    if(process.state()==QProcess::NotRunning)return;
    if(process.state()==QProcess::Starting){process.kill();return;}
#ifdef Q_OS_UNIX
    const auto pid=m_processGroups.value(&process,process.processId());
    if(pid>1)::kill(-static_cast<pid_t>(pid),force?SIGKILL:SIGTERM);
#endif
    if(force)process.kill();else process.terminate();
}
void ConnectivityService::stopTerracotta(const QString &action) {
    if(m_stopping){if(!action.isEmpty())emit failed(action,"陶瓦正在停止");return;}
    m_stopping=true;m_tcStopAction=action;m_poll.stop();m_deadline.stop();
    const auto canceled=std::exchange(m_tcAction,{});if(!canceled.isEmpty())emit failed(canceled,"陶瓦操作已取消");
    ++m_generation;m_requestPending=false;
    auto finish=[this]{
        killOwned(m_discovery,true);killOwned(m_tc,false);
        QTimer::singleShot(1000,this,[this]{
            killOwned(m_tc,true);killOwned(m_discovery,true);
            // Keep the private directory alive until all owned processes have exited.
            auto reap=std::make_shared<std::function<void()>>();std::weak_ptr<std::function<void()>> weak=reap;
            *reap=[this,weak]{
                if(m_tc.state()!=QProcess::NotRunning||m_discovery.state()!=QProcess::NotRunning){
                    if(auto again=weak.lock())QTimer::singleShot(50,this,[again]{(*again)();});return;
                }
                m_tcDir.reset();m_tcPort=0;m_tcPhase="idle";m_tcRoom.clear();m_tcAddress.clear();m_stopping=false;
                auto action=std::exchange(m_tcStopAction,{});if(!action.isEmpty())emit completed(action,{{"message","自有陶瓦进程和房间已关闭"}});
            };(*reap)();
        });
    };
    if(m_tcPort>0&&m_tc.state()==QProcess::Running){QUrlQuery query;query.addQueryItem("peaceful","true");requestTerracotta("/panic",query,[finish](QByteArray){finish();},[finish](QString){finish();});}
    else finish();
}
void ConnectivityService::failTerracotta(const QString &message) {
    if(m_stopping)return;
    const auto action=std::exchange(m_tcAction,{});if(!action.isEmpty())emit failed(action,message);else emit activity(message);
    stopTerracotta();
}
