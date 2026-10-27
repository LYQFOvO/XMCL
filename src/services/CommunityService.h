#pragma once
#include "FeatureService.h"
#include "DownloadEngine.h"
#include "SecretStore.h"
#include <QJsonDocument>
#include <functional>
class CommunityService:public FeatureService {
    Q_OBJECT
public:
    explicit CommunityService(QString root,QObject*parent=nullptr,QUrl apiBase=QUrl("https://api.modrinth.com"));
    QVariantList actions()const override;
    void execute(const QString&,const QVariantMap&)override;
    QVariantMap downloadState()const;
signals:
    void downloadChanged(const QVariantMap &);
private:
    QNetworkAccessManager m_network;
    DownloadEngine m_download;
    std::unique_ptr<SecretStore> m_secrets;
    QUrl m_apiBase;
    bool m_preparing=false;
    quint64 m_generation=0;
    QString m_installTitle,m_installKind,m_lastError;
    QVariantMap m_retryArguments;
    void request(const QString&,QUrl,std::function<void(QJsonDocument)>,bool curse=false);
    void installModrinth(const QString&,const QString&,const QString&);
    void updateInstallState();
};
