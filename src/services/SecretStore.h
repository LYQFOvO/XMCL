#pragma once
#include <QString>
#include <QByteArray>
class SecretStore {
public:
    virtual ~SecretStore() = default;
    virtual bool put(const QString &,const QByteArray &,QString *) = 0;
    virtual QByteArray get(const QString &,QString *) const = 0;
    virtual bool remove(const QString &,QString *) = 0;
};
#include <memory>
std::unique_ptr<SecretStore> platformSecretStore(const QString &namespacePath);
