#pragma once
#include "FeatureService.h"
#include "DownloadEngine.h"
#include "DownloadSource.h"
#include <QProcess>
#include <QJsonObject>
#include <functional>
class JavaRuntimeService;
class GameService : public FeatureService {
    Q_OBJECT
public:
    explicit GameService(QString root,QObject *parent=nullptr,QUrl manifestUrl=QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"));
    QVariantList actions() const override;
    void execute(const QString &,const QVariantMap &) override;
    void setDefaultMemory(int gb){m_defaultMemory=gb;}
    void setJavaRuntimeService(JavaRuntimeService *service){m_java=service;}
    // 下载源：official（默认）或 bmclapi；非法值会被忽略。
    void setDownloadSource(const QString &source){if(DownloadSource::valid(source))m_downloadSource=source;}
    QString downloadSource()const{return m_downloadSource;}
    // 按当前下载源改写官方 URL；未知主机（社区 CDN、本机夹具）保持原样。供 UI/测试使用。
    QUrl mapDownloadUrl(const QUrl &official)const{return DownloadSource::resolve(official,m_downloadSource);}
    void setIdentityProvider(std::function<QVariantMap()> provider){m_identity=std::move(provider);}
    QVariantMap downloadState() const;
    // 启动前检查 / 修复 / 诊断：供 UI 与测试直接调用（action 同步转发）。
    QVariantMap launchCheck(const QString &instanceId) const;
    QVariantMap diagnostics(const QString &instanceId) const;
    static QStringList arguments(const QJsonValue &,const QVariantMap &);
    static bool allowed(const QJsonArray &);
signals:
    void downloadChanged(const QVariantMap &);
    void gameStateChanged(bool running);
private:
    QNetworkAccessManager m_network;
    DownloadEngine m_download;
    QProcess m_game,m_installer;
    std::function<QVariantMap()> m_identity;
    bool m_installing=false;
    quint64 m_installGeneration=0;
    int m_defaultMemory=4;
    QString m_downloadSource=DownloadSource::officialId();
    bool m_preferOfficialFallback=false;
    QString m_installAction,m_installId,m_targetVersion,m_loader,m_loaderVersion;
    QString m_installPhase,m_installError;
    QVariantMap m_retryArguments;
    QUrl m_manifestUrl;
    JavaRuntimeService *m_java=nullptr;
    bool m_repairing=false;
    int m_repairDepth=0;
    QString m_repairInstance;
    QHash<QString,quint64> m_catalogRequests;
    void get(const QUrl &,std::function<void(QByteArray)> callback,const QString &action,QByteArray expected={});
    void json(const QUrl &,std::function<void(QJsonObject)> callback,const QString &action,QByteArray expected={});
    void installBase(const QVariantMap &);
    void installLibraries(QJsonObject profile,std::function<void()> done);
    void installLoader();
    void installationFinished(const QString &profileId);
    void launch(const QString &,const QString &server={});
    QJsonObject version(const QString &,int depth=0)const;
    QString javaFor(const QVariantMap &,int major=0)const;
    QList<Transfer> libraries(const QJsonObject &) const;
    QList<Transfer> requiredFiles(const QJsonObject &,const QString &baseVersion) const;
    QList<Transfer> assetFiles(const QJsonObject &index) const;
    void launchRepair(const QString &instanceId,int depth=0);
    Transfer transfer(const QUrl &official,const QString &destination,const QByteArray &digest,const QString &algorithm,qint64 expectedSize=-1)const;
    void installationError(const QString &);
};
