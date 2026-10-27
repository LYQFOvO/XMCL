#pragma once
#include "FeatureService.h"
#include "DownloadEngine.h"
#include <QNetworkAccessManager>

class MaintenanceService : public FeatureService {
    Q_OBJECT
public:
    explicit MaintenanceService(QString root, QObject *parent = nullptr);
    QVariantList actions() const override;
    void execute(const QString &action, const QVariantMap &args) override;
    static QString redactLog(const QString &text);
    static QVariantList diagnose(const QString &text);
    static QString platform();
    static QString architecture();
    static void validateManifest(const QVariantMap &manifest, const QString &expectedSha256 = {});
private:
    QNetworkAccessManager m_network;
    DownloadEngine m_download;
    bool m_checking = false;
    QString statePath() const;
    QVariantMap prepare(const QVariantMap &manifest, const QString &package);
};
