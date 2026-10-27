#pragma once
#include "FeatureService.h"
#include "SecretStore.h"
#include <QNetworkAccessManager>
#include <QTimer>
#include <QJsonObject>
#include <QUrl>
#include <functional>

class AccountService : public FeatureService {
    Q_OBJECT
public:
    // Microsoft 桌面公共客户端身份。设备码授权只需要一个公共客户端，不包含任何密钥；
    // 该标识参考 KAMUCL 启动器（见 THIRD_PARTY_NOTICES.md），可在登录表单中覆盖为自己的应用。
    static QString defaultMicrosoftClientId();
    // Minecraft 档案 ID 在认证服务中可能是 32 位十六进制；启动参数和认证库使用带连字符形式。
    static QString normalizedMinecraftUuid(const QString &id);
    // 微软/Xbox/Minecraft 认证端点。默认指向官方 HTTPS 服务；测试可使用本地回环夹具。
    struct Endpoints {
        QUrl microsoftAuthority{"https://login.microsoftonline.com/consumers/oauth2/v2.0"};
        QUrl xboxUserAuth{"https://user.auth.xboxlive.com/user/authenticate"};
        QUrl xboxXsts{"https://xsts.auth.xboxlive.com/xsts/authorize"};
        QUrl minecraftLogin{"https://api.minecraftservices.com/authentication/login_with_xbox"};
        QUrl minecraftEntitlements{"https://api.minecraftservices.com/entitlements/mcstore"};
        QUrl minecraftProfile{"https://api.minecraftservices.com/minecraft/profile"};
        QUrl minecraftSkins{"https://api.minecraftservices.com/minecraft/profile/skins"};
        QUrl minecraftCapes{"https://api.minecraftservices.com/minecraft/profile/capes/active"};
    };
    explicit AccountService(QString root,QObject *parent=nullptr);
    AccountService(QString root,QObject *parent,Endpoints endpoints,std::unique_ptr<SecretStore> secrets);
    QVariantList actions() const override;
    void execute(const QString &,const QVariantMap &) override;
    QVariantMap launchIdentity() const;
    QString displayName()const{return selected().value("name").toString();}
private:
    using Callback=std::function<void(QJsonObject)>;
    QNetworkAccessManager m_network;
    Endpoints m_endpoints;
    std::unique_ptr<SecretStore> m_secrets;
    QVariantMap m_data;
    QTimer m_poll;
    QString m_deviceCode,m_clientId;
    QVariantMap m_yggPending;
    quint64 m_loginGeneration=0;
    qint64 m_expires=0;
    int m_pollIntervalMs=5000;
    int m_transientFailures=0;
    void save();
    QVariantMap selected() const;
    QVariantMap secret(const QString &) const;
    void storeAccount(QVariantMap account,QVariantMap credentials,const QString &action);
    void request(const QString &action,const QUrl &url,const QByteArray &method,const QByteArray &body,const QByteArray &contentType,Callback callback,const QString &bearer={});
    void microsoftExchange(const QString &action,const QJsonObject &tokens,const QString &clientId);
    void microsoftPoll();
    void failMicrosoft(const QString &message);
    void wardrobe(const QString &,const QVariantMap &);
    QUrl endpoint(const QUrl &base,const QString &path) const;
};
