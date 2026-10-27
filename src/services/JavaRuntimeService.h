#pragma once
#include "FeatureService.h"
#include <QProcess>
#include <QVariantList>
#include <QVariantMap>

// Java 运行时发现、探测、缓存与实例匹配。
// 只探测本机已安装的 Java；不自动下载运行时（见 docs/migration/STATUS.md 的缺口）。
class JavaRuntimeService : public FeatureService {
    Q_OBJECT
public:
    explicit JavaRuntimeService(QString root,QObject *parent=nullptr);
    QVariantList actions() const override;
    void execute(const QString &,const QVariantMap &) override;
    // 已缓存/按需探测的运行时列表；refresh 强制重新探测。
    QVariantList runtimes(bool refresh=false);
    // 按所需主版本匹配；requiredMajor<=0 表示未知，返回空匹配与说明。
    QVariantMap match(int requiredMajor);
    static QVariantMap matchRuntimes(const QVariantList &items,int requiredMajor);
    // 结合实例元数据（手填 javaPath、profile javaVersion）匹配。
    QVariantMap matchForInstance(const QString &instanceId,int requiredMajor=-1);
    // 启动准备使用：返回匹配的 java 路径；失败时写 problem。
    QString bestPath(int requiredMajor,QString *problem=nullptr);
    static int requiredMajorForInstance(const QString &root,const QString &instanceId,QString *profileId=nullptr);
    static QStringList discoverCandidates();
    static QVariantMap probe(const QString &path);
    static int parseMajor(const QString &version);
private:
    QString m_cachePath;
    QVariantList m_cache;
    bool m_loaded=false;
    void loadCache();
    void saveCache() const;
    static QVariantMap cachedFor(const QVariantList &cache,const QString &path);
    static bool sameFingerprint(const QVariantMap &entry);
};
