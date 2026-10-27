#pragma once
#include <QVariantList>
#include <QVariantMap>
#include <QMap>
#include <QSet>
#include <memory>
class QPluginLoader;
class NativePlugin;

class NativePluginManager {
public:
    explicit NativePluginManager(QString root);
    ~NativePluginManager();
    QVariantList plugins() const;
    QVariantMap install(const QString &path);
    void enable(const QString &id);
    void disable(const QString &id);
    void remove(const QString &id);
    QVariantMap execute(const QString &id, const QString &command, const QVariantMap &args);
private:
    QString m_root;
    QVariantMap m_installed;
    QSet<QString> m_loadedIds;
    QMap<QString, std::shared_ptr<QPluginLoader>> m_loaders;
    QString path(const QString &name) const;
    QVariantMap entry(const QString &id) const;
    static QVariantMap inspect(const QString &path);
};
