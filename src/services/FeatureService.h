#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QString>

class FeatureService : public QObject {
    Q_OBJECT
public:
    explicit FeatureService(QString dataRoot, QObject *parent = nullptr)
        : QObject(parent), m_root(std::move(dataRoot)) {}
    virtual QVariantList actions() const = 0;
    virtual void execute(const QString &action, const QVariantMap &args) = 0;
    QString dataRoot() const { return m_root; }
signals:
    void completed(const QString &action, const QVariantMap &data);
    void failed(const QString &action, const QString &message);
    void activity(const QString &message);
protected:
    QString m_root;
};
