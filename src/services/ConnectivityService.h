#pragma once
#include "FeatureService.h"
#include "SecretStore.h"
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTimer>
#include <QTemporaryDir>
#include <functional>
#include <QUrlQuery>

class ConnectivityService final : public FeatureService {
    Q_OBJECT
public:
    explicit ConnectivityService(QString root, QObject *parent = nullptr);
    ~ConnectivityService() override;
    QVariantList actions() const override;
    void execute(const QString &action, const QVariantMap &args) override;
private:
    QNetworkAccessManager m_network;
    std::unique_ptr<SecretStore> m_secrets;
    QProcess m_frp, m_tc, m_discovery;
    QHash<QProcess *, qint64> m_processGroups;
    QTimer m_poll, m_deadline;
    std::unique_ptr<QTemporaryDir> m_frpDir, m_tcDir;
    QString m_tcAction, m_tcMode, m_tcPlayer, m_tcPhase = "idle", m_tcRoom, m_tcAddress;
    QString m_frpStartAction, m_frpStopAction, m_tcStopAction;
    int m_tcPort = 0;
    quint64 m_generation = 0;
    bool m_requestPending = false, m_stopping = false;
    void startFrp(const QString &action);
    void startTerracotta(const QString &action, const QVariantMap &args);
    void pollTerracotta();
    void requestTerracotta(const QString &path, const QUrlQuery &query,
                           std::function<void(QByteArray)> done,
                           std::function<void(QString)> error);
    void stopTerracotta(const QString &action = {});
    void failTerracotta(const QString &message);
    void killOwned(QProcess &process, bool force);
    QVariantMap status() const;
};
