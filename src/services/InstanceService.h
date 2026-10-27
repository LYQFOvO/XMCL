#pragma once
#include "FeatureService.h"
class InstanceService : public FeatureService {
    Q_OBJECT
public:
    explicit InstanceService(QString root,QObject *parent=nullptr):FeatureService(std::move(root),parent){}
    QVariantList actions() const override;
    void execute(const QString &,const QVariantMap &) override;
    QString instancePath(const QString &) const;
    QString gamePath(const QString &) const;
    QVariantList list() const;
};
