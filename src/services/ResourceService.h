#pragma once
#include "FeatureService.h"
class ResourceService : public FeatureService {
    Q_OBJECT
public:
    explicit ResourceService(QString root,QObject *parent=nullptr):FeatureService(std::move(root),parent){}
    QVariantList actions() const override;
    void execute(const QString &,const QVariantMap &) override;
};
