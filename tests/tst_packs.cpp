#include <QtTest>
#include <QTemporaryDir>
#include "services/PackService.h"
#include "services/ArchiveUtils.h"
#include "services/ServiceUtils.h"
#include "services/InstanceService.h"
class PackTest: public QObject {
    Q_OBJECT
private slots:
void importsClientOverlayIntoNewInstance(){
    QTemporaryDir dir,source;ServiceUtils::write(source.path()+"/modrinth.index.json",{{"formatVersion",1},{"game","minecraft"},{"name","Pack"},{"dependencies",QVariantMap{{"minecraft","1.21.1"},{"fabric-loader","0.16.5"}}},{"files",QVariantList{}}});
    ServiceUtils::write(source.path()+"/overrides/config/test.json",{{"value","shared"}});ServiceUtils::write(source.path()+"/client-overrides/config/test.json",{{"value","client"}});ServiceUtils::write(source.path()+"/server-overrides/server-only.json",{{"value",1}});
    const auto archive=dir.filePath("test.mrpack");ArchiveUtils::pack(source.path(),archive);PackService packs(dir.filePath("data"));QSignalSpy ok(&packs,&FeatureService::completed),bad(&packs,&FeatureService::failed);packs.execute("pack.import",{{"path",archive}});QTRY_COMPARE(ok.size(),1);QCOMPARE(bad.size(),0);
    InstanceService instances(dir.filePath("data"));QCOMPARE(instances.list().size(),1);auto instance=instances.list().first().toMap();QCOMPARE(instance.value("loaderVersion").toString(),QString("0.16.5"));QVERIFY(!instance.value("installed").toBool());auto game=instances.gamePath(instance.value("id").toString());QCOMPARE(ServiceUtils::read(game+"/config/test.json").value("value").toString(),QString("client"));QVERIFY(!QFile::exists(game+"/server-only.json"));
}
void rejectsTraversingFileBeforePublishing(){
    QTemporaryDir dir,source;QVariantList files{QVariantMap{{"path","../outside"},{"hashes",QVariantMap{{"sha512",QString(128,'0')}}},{"downloads",QVariantList{"https://cdn.modrinth.com/test"}}}};
    ServiceUtils::write(source.path()+"/modrinth.index.json",{{"formatVersion",1},{"game","minecraft"},{"name","Unsafe"},{"dependencies",QVariantMap{{"minecraft","1.21.1"}}},{"files",files}});auto archive=dir.filePath("test.mrpack");ArchiveUtils::pack(source.path(),archive);PackService packs(dir.filePath("data"));QSignalSpy bad(&packs,&FeatureService::failed);packs.execute("pack.import",{{"path",archive}});QCOMPARE(bad.size(),1);InstanceService instances(dir.filePath("data"));QVERIFY(instances.list().isEmpty());
}
};
QTEST_GUILESS_MAIN(PackTest)
#include "tst_packs.moc"
