#pragma once
#include "FeatureService.h"
#include "NativePluginManager.h"

class PersonalizationService : public FeatureService {
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
public:
    explicit PersonalizationService(QString root, QObject *parent = nullptr);
    QVariantList actions() const override;
    void execute(const QString &action, const QVariantMap &args) override;
    QVariantMap settings() const;
signals:
    void changed();
private:
    QVariantMap m_values;
    NativePluginManager m_plugins;
    QString path(const QString &name) const;
    QString importImage(const QString &source, const QString &prefix) const;
    QVariantMap validated(const QVariantMap &values) const;
};
