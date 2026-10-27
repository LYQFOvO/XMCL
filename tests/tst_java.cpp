#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include "services/JavaRuntimeService.h"
#include "services/MemoryAdvisor.h"

class InstanceFixture {
public:
    explicit InstanceFixture(const QString &root):m_root(root) {}
    QString createInstance(const QString &version,const QString &state,const QVariantMap &extra={}) {
        static const QString id="12345678-1234-1234-1234-123456789abc";
        QDir().mkpath(m_root + "/instances/" + id);
        QVariantMap meta{{"id",id},{"name","Fixture"},{"version",version},{"loader","vanilla"},{"isolated",true},{"installed",state=="installed"}};
        for (auto it=extra.begin();it!=extra.end();++it) meta[it.key()]=it.value();
        QFile file(m_root + "/instances/" + id + "/instance.json");
        file.open(QIODevice::WriteOnly);file.write(QJsonDocument(QJsonObject::fromVariantMap(meta)).toJson());file.close();
        QDir().mkpath(m_root + "/minecraft/versions");
        return id;
    }
    void writeVersion(const QString &id,const QJsonObject &object) {
        const auto dir=m_root + "/minecraft/versions/" + id;
        QDir().mkpath(dir);
        QFile file(dir + '/' + id + ".json");
        file.open(QIODevice::WriteOnly);file.write(QJsonDocument(object).toJson());file.close();
    }
    static QString missingId() { return "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"; }
private:
    QString m_root;
};


class JavaTest : public QObject {
    Q_OBJECT
    static QString writeJava(const QString &path,const QString &version,const QString &extra={}) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        const auto body = QString("#!/bin/sh\n"
                                  "if [ -n \"$XMCL_JAVA_COUNTER\" ]; then echo x >> \"$XMCL_JAVA_COUNTER\"; fi\n"
                                  "cat >&2 <<'EOF'\n"
                                  "Property settings:\n"
                                  "    java.version = %1\n"
                                  "    java.vendor = TestVendor\n"
                                  "    os.arch = aarch64\n"
                                  "    sun.arch.data.model = 64\n"
                                  "EOF\n%2").arg(version).arg(extra);
        file.write(body.toUtf8());
        file.close();
        QFile::setPermissions(path,QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }
private slots:
    void parsesJavaVersions() {
        QCOMPARE(JavaRuntimeService::parseMajor("17.0.9"),17);
        QCOMPARE(JavaRuntimeService::parseMajor("1.8.0_292"),8);
        QCOMPARE(JavaRuntimeService::parseMajor("21"),21);
        QCOMPARE(JavaRuntimeService::parseMajor("garbage"),0);
    }
    void probesFakeRuntimes() {
        QTemporaryDir dir;const auto java=writeJava(dir.filePath("jdk/bin/java"),"17.0.9");QVERIFY(!java.isEmpty());
        const auto result=JavaRuntimeService::probe(java);
        QVERIFY2(result.value("valid").toBool(),qPrintable(result.value("problem").toString()));
        QCOMPARE(result.value("major").toInt(),17);
        QCOMPARE(result.value("bits").toInt(),64);
        QCOMPARE(result.value("vendor").toString(),QString("TestVendor"));
        QCOMPARE(result.value("arch").toString(),QString("aarch64"));
        QVERIFY(result.value("title").toString().contains("Java 17"));
    }
    void rejectsUnusableJava() {
        QTemporaryDir dir;
        const auto path=dir.filePath("broken/java");
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("#!/bin/sh\nexit 3\n");file.close();
        QFile::setPermissions(path,QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        auto result=JavaRuntimeService::probe(path);
        QVERIFY(!result.value("valid").toBool());
        QVERIFY(!result.value("problem").toString().isEmpty());
        result=JavaRuntimeService::probe(dir.filePath("missing-java"));
        QVERIFY(!result.value("valid").toBool());
    }
    void discoversJavaHomeAndCachesProbes() {
        QTemporaryDir dir;
        const auto java=writeJava(dir.filePath("fakejdk/bin/java"),"17.0.9");
        QVERIFY(!java.isEmpty());
        const auto counter=dir.filePath("probe-counter.txt");
        const auto previousHome=qgetenv("JAVA_HOME");
        qputenv("JAVA_HOME",dir.filePath("fakejdk").toUtf8());
        qputenv("XMCL_JAVA_COUNTER",counter.toUtf8());
        const auto candidates=JavaRuntimeService::discoverCandidates();
        QVERIFY(candidates.contains(QFileInfo(java).canonicalFilePath()));
        JavaRuntimeService service(dir.path());
        const auto first=service.runtimes();
        QVERIFY(!first.isEmpty());
        const auto fakeCount=[&](){QFile file(counter);if(!file.open(QIODevice::ReadOnly))return qint64(0);return file.readAll().count('x');};
        const auto probesAfterFirst=fakeCount();
        QVERIFY(probesAfterFirst>=1);
        service.runtimes();
        QCOMPARE(fakeCount(),probesAfterFirst); // 缓存命中，不重复探测
        service.runtimes(true);
        QVERIFY(fakeCount()>probesAfterFirst); // refresh 强制重新探测
        qputenv("JAVA_HOME",previousHome);
        qunsetenv("XMCL_JAVA_COUNTER");
    }
    void matchesExactThenHigher() {
        const QVariantList items{
            QVariantMap{{"path","/jdk8/bin/java"},{"major",8},{"bits",64},{"valid",true},{"title","Java 8"}},
            QVariantMap{{"path","/jdk17/bin/java"},{"major",17},{"bits",64},{"valid",true},{"title","Java 17"}},
            QVariantMap{{"path","/jdk21/bin/java"},{"major",21},{"bits",64},{"valid",true},{"title","Java 21"}},
            QVariantMap{{"path","/broken/bin/java"},{"major",0},{"bits",64},{"valid",false}}
        };
        auto exact=JavaRuntimeService::matchRuntimes(items,17);
        QCOMPARE(exact.value("matched").toMap().value("path").toString(),QString("/jdk17/bin/java"));
        QCOMPARE(exact.value("warnings").toList().size(),0);
        auto higher=JavaRuntimeService::matchRuntimes(items,19);
        QCOMPARE(higher.value("matched").toMap().value("path").toString(),QString("/jdk21/bin/java"));
        QVERIFY(!higher.value("warnings").toList().isEmpty());
        auto none=JavaRuntimeService::matchRuntimes(items,25);
        QVERIFY(none.value("matched").toMap().isEmpty());
        QVERIFY(!none.value("warnings").toList().isEmpty());
        auto unknown=JavaRuntimeService::matchRuntimes(items,0);
        QVERIFY(unknown.value("matched").toMap().isEmpty());
    }
    void resolvesRequiredMajorThroughInheritance() {
        QTemporaryDir dir;InstanceFixture fixture(dir.path());
        const auto id=fixture.createInstance("fabric-loader-0.16.5-1.21.1","installed");
        fixture.writeVersion("fabric-loader-0.16.5-1.21.1",{{"id","fabric-loader-0.16.5-1.21.1"},{"inheritsFrom","1.21.1"}});
        fixture.writeVersion("1.21.1",{{"id","1.21.1"},{"javaVersion",QJsonObject{{"majorVersion",21},{"component","java-runtime-delta"}}}});
        QString profileId;
        QCOMPARE(JavaRuntimeService::requiredMajorForInstance(dir.path(),id,&profileId),21);
        QCOMPARE(profileId,QString("fabric-loader-0.16.5-1.21.1"));
        QCOMPARE(JavaRuntimeService::requiredMajorForInstance(dir.path(),InstanceFixture::missingId()),-1);
    }
    void liveFindsInstalledJava() {
        if (qEnvironmentVariableIsEmpty("XMCL_LIVE_JAVA")) QSKIP("live Java discovery disabled; set XMCL_LIVE_JAVA=1");
        QTemporaryDir dir;JavaRuntimeService service(dir.path());
        const auto items=service.runtimes(true);
        QVERIFY(!items.isEmpty());
        bool valid=false;for(const auto &item:items)if(item.toMap().value("valid").toBool())valid=true;
        QVERIFY2(valid,"no usable Java found by discovery");
        const auto match=service.match(17);
        QVERIFY(!match.value("matched").toMap().isEmpty());
        qInfo().noquote()<< "matched" << match.value("matched").toMap().value("title").toString()
                         << match.value("matched").toMap().value("path").toString();
    }
    void manualJavaWinsWithMismatchWarning() {
        QTemporaryDir dir;InstanceFixture fixture(dir.path());
        const auto java=writeJava(dir.filePath("jdk8/bin/java"),"1.8.0_292");
        const auto id=fixture.createInstance("1.20.1","installed",{{"javaPath",java}});
        fixture.writeVersion("1.20.1",{{"id","1.20.1"},{"javaVersion",QJsonObject{{"majorVersion",17}}}});
        JavaRuntimeService service(dir.path());
        const auto match=service.matchForInstance(id);
        QVERIFY(match.value("usedManual").toBool());
        QCOMPARE(match.value("matched").toMap().value("major").toInt(),8);
        QCOMPARE(match.value("requiredMajor").toInt(),17);
        bool warned=false;
        for (const auto &warning : match.value("warnings").toList()) if (warning.toString().contains("不一致")) warned=true;
        QVERIFY(warned);
    }
private:

};
QTEST_GUILESS_MAIN(JavaTest)
#include "tst_java.moc"
