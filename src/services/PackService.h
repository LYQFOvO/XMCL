#pragma once
#include "FeatureService.h"
#include "DownloadEngine.h"
#include "SecretStore.h"
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <memory>
class PackService: public FeatureService {
    Q_OBJECT
public:
    explicit PackService(QString root,QObject *parent=nullptr);
    QVariantList actions()const override;
    void execute(const QString&,const QVariantMap&)override;
    QVariantMap downloadState()const;
signals:
    void downloadChanged(const QVariantMap &);
private:
    struct Import {
        std::shared_ptr<QTemporaryDir> stage;
        QVariantMap metadata;
        QList<Transfer> jobs;
        QStringList overrides;
        QJsonArray curseFiles;
        QSet<QString> paths;
        int next=0;
        bool cancelled=false;
    };
    QNetworkAccessManager m_network;
    DownloadEngine m_download;
    std::unique_ptr<SecretStore> m_secrets;
    std::shared_ptr<Import> m_import;
    QString m_lastError;
    QVariantMap m_lastArguments;
    void resolveCurse(const std::shared_ptr<Import>&);
    void download(const std::shared_ptr<Import>&);
    void fail(const QString&);
    static void overlay(const QString &,const QString &);
};
