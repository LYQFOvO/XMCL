#pragma once
#include "FeatureService.h"
#include <QNetworkAccessManager>
#include <QNetworkProxy>
class BridgeService:public FeatureService {
    Q_OBJECT
public:
    explicit BridgeService(QString root,QObject*parent=nullptr):FeatureService(std::move(root),parent),m_network(this){m_network.setProxy(QNetworkProxy::NoProxy);}
    QVariantList actions()const override;
    void execute(const QString&,const QVariantMap&)override;
private:
    QNetworkAccessManager m_network;
};
