#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QCryptographicHash>
#include "MaintenanceService.h"
#include "ServiceUtils.h"
#include "ArchiveUtils.h"

class MaintenanceTest : public QObject {
    Q_OBJECT
    static void put(const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath()); QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes),bytes.size());
    }
    static QVariantMap manifest(const QString &hash = QString(64,'a')) {
        return {{"schema",1},{"appId","com.xmcl.launcher"},{"version","0.2.0"},
            {"platform",MaintenanceService::platform()},{"arch",MaintenanceService::architecture()},
            {"entryPoint","XMCL"},{"package",QVariantMap{{"url","https://updates.example.org/xmcl.zip"},{"sha256",hash},{"bytes",123}}}};
    }
private slots:
    void noSecretsInExportText() {
        const QString input="Authorization: Bearer secret-token\n{\"refresh_token\":\n\"multiline-secret\"}\n--username Alice --accessToken top-secret\nPlayer Alice: private chat\n/home/Alice/game\nC:\\Users\\Alice\\game\nme@example.org 192.168.1.20 123e4567-e89b-12d3-a456-426614174000\njava.lang.OutOfMemoryError: Java heap space\n\tat net.minecraft.Main.main(Main.java:42)\nhttps://example.org/?token=secret\n";
        const auto clean=MaintenanceService::redactLog(input);
        for(const auto &secret:QStringList{"secret","Alice","private chat","example.org","192.168","123e4567","/home","C:\\Users"}) QVERIFY2(!clean.contains(secret),qPrintable(clean));
        QVERIFY(clean.contains("OutOfMemoryError")); QVERIFY(clean.contains("Main.java:42"));
    }
    void diagnosisIsEvidenceNotRepair() {
        const auto findings=MaintenanceService::diagnose("java.lang.UnsupportedClassVersionError\nCould not reserve enough space for object heap\n");
        QCOMPARE(findings.size(),2); for(const auto &v:findings) {const auto f=v.toMap(); QVERIFY(!f.value("suggestion").toString().isEmpty()); QCOMPARE(f.value("repaired").toBool(),false);}
        QVERIFY(MaintenanceService::diagnose("ordinary startup").isEmpty());
    }
    void manifestRejectsWrongIdentityAndUnsafeSource() {
        auto m=manifest(); MaintenanceService::validateManifest(m);
        m["appId"]="KAMUCL"; QVERIFY_THROWS_EXCEPTION(std::runtime_error,MaintenanceService::validateManifest(m));
        m=manifest(); m["platform"]="wrong"; QVERIFY_THROWS_EXCEPTION(std::runtime_error,MaintenanceService::validateManifest(m));
        m=manifest(); m["entryPoint"]="../run"; QVERIFY_THROWS_EXCEPTION(std::runtime_error,MaintenanceService::validateManifest(m));
        m=manifest(); auto p=m["package"].toMap();p["url"]="http://updates.example.org/x.zip";m["package"]=p;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,MaintenanceService::validateManifest(m));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,MaintenanceService::validateManifest(manifest(),QString(64,'b')));
    }
    void prepareVerifiesOuterAndInnerManifest() {
        QTemporaryDir d; const auto src=d.filePath("package");QDir().mkpath(src);
        auto m=manifest();auto inner=m;inner.remove("package");ServiceUtils::write(src+"/xmcl-update.json",inner);put(src+"/XMCL","not executed");
        auto archive=d.filePath("package.zip");ArchiveUtils::pack(src,archive);QFile f(archive);QVERIFY(f.open(QIODevice::ReadOnly));auto bytes=f.readAll();
        auto p=m["package"].toMap();p["sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());p["bytes"]=bytes.size();m["package"]=p;
        auto mp=d.filePath("manifest.json");ServiceUtils::write(mp,m);
        MaintenanceService service(d.filePath("data"));QSignalSpy ok(&service,&FeatureService::completed),bad(&service,&FeatureService::failed);
        service.execute("update.prepare",{{"manifest",mp},{"package",archive}});QCOMPARE(bad.size(),0);QCOMPARE(ok.size(),1);
        auto state=ok.last().at(1).toMap();QCOMPARE(state["phase"].toString(),QString("prepared"));QVERIFY(QFile::exists(state["stagePath"].toString()+"/XMCL"));QCOMPARE(state["applied"].toBool(),false);
        put(archive,"corrupt");service.execute("update.prepare",{{"manifest",mp},{"package",archive}});QCOMPARE(bad.size(),1);
    }
    void backupAndRestoreDoNotOverwrite() {
        QTemporaryDir d;put(d.filePath("original/XMCL"),"original");MaintenanceService service(d.filePath("data"));
        QSignalSpy ok(&service,&FeatureService::completed),bad(&service,&FeatureService::failed);
        service.execute("update.backup",{{"path",d.filePath("original")}});QCOMPARE(bad.size(),0);QCOMPARE(ok.size(),1);auto id=ok.last().at(1).toMap()["backupId"].toString();
        service.execute("update.rollback",{{"confirm",true},{"backupId",id},{"destination",d.filePath("restored")}});QCOMPARE(bad.size(),0);QVERIFY(QFile::exists(d.filePath("restored/XMCL")));
        service.execute("update.rollback",{{"confirm",true},{"backupId",id},{"destination",d.filePath("original")}});QCOMPARE(bad.size(),1);
        put(d.filePath("data/maintenance/backups/")+id+"/files/XMCL","tampered");
        service.execute("update.rollback",{{"confirm",true},{"backupId",id},{"destination",d.filePath("other")}});QCOMPARE(bad.size(),2);QVERIFY(!QFile::exists(d.filePath("other")));
    }
    void exportAndSettingsPersist() {
        QTemporaryDir d;put(d.filePath("latest.log"),"refresh_token=private-secret\njava.lang.UnsupportedClassVersionError: user Alice\n");
        MaintenanceService service(d.filePath("data"));QSignalSpy ok(&service,&FeatureService::completed),bad(&service,&FeatureService::failed);
        service.execute("maintenance.export",{{"path",d.filePath("latest.log")},{"destination",d.filePath("report.json")}});QCOMPARE(bad.size(),0);
        QFile file(d.filePath("report.json"));QVERIFY(file.open(QIODevice::ReadOnly));const auto data=file.readAll();QVERIFY(!data.contains("private-secret"));QVERIFY(!data.contains("Alice"));QVERIFY(data.contains("UnsupportedClassVersionError"));QVERIFY(!data.contains(d.path().toUtf8()));
        service.execute("maintenance.export",{{"path",d.filePath("latest.log")},{"destination",d.filePath("report.json")}});QCOMPARE(bad.size(),1);
        service.execute("update.configure",{{"feed","http://example.org/feed"}});QCOMPARE(bad.size(),2);
        service.execute("update.configure",{{"feed","https://example.org/feed"},{"expectedSha256",QString(64,'a')}});QCOMPARE(bad.size(),2);
        MaintenanceService reload(d.filePath("data"));QSignalSpy status(&reload,&FeatureService::completed);reload.execute("update.status",{});
        QCOMPARE(status.last().at(1).toMap()["config"].toMap()["feed"].toString(),QString("https://example.org/feed"));
    }
    void rejectInnerMismatchEvenWithCorrectArchiveHash() {
        QTemporaryDir d;const auto src=d.filePath("package");QDir().mkpath(src);auto m=manifest();auto inner=m;inner.remove("package");inner["appId"]="KAMUCL";
        ServiceUtils::write(src+"/xmcl-update.json",inner);put(src+"/XMCL","not executed");const auto archive=d.filePath("package.zip");ArchiveUtils::pack(src,archive);
        QFile f(archive);QVERIFY(f.open(QIODevice::ReadOnly));const auto bytes=f.readAll();auto package=m["package"].toMap();package["bytes"]=bytes.size();package["sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());m["package"]=package;ServiceUtils::write(d.filePath("manifest.json"),m);
        MaintenanceService service(d.filePath("data"));QSignalSpy ok(&service,&FeatureService::completed),bad(&service,&FeatureService::failed);
        service.execute("update.prepare",{{"manifest",d.filePath("manifest.json")},{"package",archive}});QCOMPARE(ok.size(),0);QCOMPARE(bad.size(),1);
        QCOMPARE(ServiceUtils::read(d.filePath("data/maintenance/update-state.json"))["phase"].toString(),QString("failed"));
        QCOMPARE(QDir(d.filePath("data/maintenance/staging")).entryList(QDir::Dirs|QDir::NoDotAndDotDot).size(),0);
    }
};
QTEST_GUILESS_MAIN(MaintenanceTest)
#include "tst_maintenance.moc"
