#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>
#include "FeatureService.h"
class AccountService;
class GameService;
class JavaRuntimeService;
class CommunityService;
class PackService;
class PersonalizationService;
class ServiceHub : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accountName READ selectedAccountName NOTIFY accountChanged)
    Q_PROPERTY(QVariantMap appearance READ appearance NOTIFY appearanceChanged)
    Q_PROPERTY(QVariantList modules READ modules CONSTANT)
    Q_PROPERTY(QVariantList instances READ instances NOTIFY instancesChanged)
    Q_PROPERTY(QVariantList downloadTasks READ downloadTasks NOTIFY downloadStateChanged)
    Q_PROPERTY(QVariantMap downloadState READ downloadState NOTIFY downloadStateChanged)
public:
    explicit ServiceHub(QString root,QObject*parent=nullptr,QUrl manifestUrl=QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"),QUrl communityApiBase=QUrl("https://api.modrinth.com"));
    QVariantList modules() const;
    QVariantMap appearance()const;
    QVariantList instances() const;
    QVariantMap downloadState()const;
    QVariantList downloadTasks()const;
    Q_INVOKABLE QVariantList actions(const QString &module)const;
    Q_INVOKABLE void execute(const QString &module,const QString &action,const QVariantMap &args={});
    Q_INVOKABLE QString dataDirectory()const{return m_root;}
    Q_INVOKABLE QString downloadSource()const;
    QString selectedAccountName()const;
    void setDefaultMemory(int gb);
signals:
    void completed(const QString &module,const QString &action,const QVariantMap &data);
    void failed(const QString &module,const QString &action,const QString &message);
    void activity(const QString &message);
    void accountChanged();
    void appearanceChanged();
    void instancesChanged();
    void downloadStateChanged();
private:
    QString m_root;
    QMap<QString,FeatureService*> m_services;
    AccountService *m_accounts=nullptr;
    GameService *m_game=nullptr;
    JavaRuntimeService *m_java=nullptr;
    CommunityService *m_community=nullptr;
    PackService *m_packs=nullptr;
    PersonalizationService *m_personalization=nullptr;
    void add(const QString &,FeatureService*);
};
