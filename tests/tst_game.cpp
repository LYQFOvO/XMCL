#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QDirIterator>
#include <QJsonArray>
#include "TestGameServer.h"
#include "services/GameService.h"
#include "services/InstanceService.h"
#include "services/ServiceUtils.h"
#include "services/DownloadEngine.h"
#include "services/JavaRuntimeService.h"
#include "services/DownloadSource.h"
#include <QSet>
class GameTest:public QObject{
    Q_OBJECT

private slots:
void installCreatesInstanceAndReportsFailureAndRetry(){
    QTemporaryDir dir;TestGameServer server;QVERIFY(server.isListening());server.rejectClient=true;server.profileDelay=100;
    GameService game(dir.path(),nullptr,server.url("/manifest.json"));QSignalSpy ok(&game,&FeatureService::completed),bad(&game,&FeatureService::failed);
    game.execute("game.install",{{"createInstance",true},{"name","My world"},{"version","1.21.1"},{"loader","vanilla"}});
    QVERIFY(game.downloadState().value("active").toBool());QCOMPARE(game.downloadState().value("phase").toString(),QString("metadata"));
    QTRY_COMPARE(bad.size(),1);QCOMPARE(game.downloadState().value("phase").toString(),QString("error"));
    InstanceService instances(dir.path());QCOMPARE(instances.list().size(),1);const auto id=instances.list().first().toMap().value("id").toString();
    QVERIFY(!QFile::exists(dir.filePath("minecraft/versions/1.21.1/1.21.1.jar")));
    server.rejectClient=false;game.execute("download.retry",{});QTRY_COMPARE(game.downloadState().value("phase").toString(),QString("complete"));
    QCOMPARE(instances.list().size(),1);QVERIFY(instances.list().first().toMap().value("installed").toBool());
    QCOMPARE(game.downloadState().value("instanceId").toString(),id);QFile client(dir.filePath("minecraft/versions/1.21.1/1.21.1.jar"));QVERIFY(client.open(QIODevice::ReadOnly));QCOMPARE(client.readAll(),server.jar);
}
void launchesConfiguredJavaWithSeparatedArguments(){
#ifdef Q_OS_WIN
    QSKIP("POSIX fake Java fixture");
#else
    QTemporaryDir dir(QDir::tempPath()+"/xmcl game XXXXXX");InstanceService instances(dir.path());QSignalSpy created(&instances,&FeatureService::completed);instances.execute("instance.create",{{"name","Test"},{"version","fixture"}});const auto id=created.first().at(1).toMap().value("id").toString();
    const auto java=dir.filePath("fake java");{QFile f(java);QVERIFY(f.open(QIODevice::WriteOnly));f.write("#!/bin/sh\nprintf '%s\\n' \"$@\"\n");}QVERIFY(QFile::setPermissions(java,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
    const auto metadataPath=instances.instancePath(id)+"/instance.json";auto metadata=ServiceUtils::read(metadataPath);metadata["installed"]=true;metadata["javaPath"]=java;metadata["memoryGb"]=3;ServiceUtils::write(metadataPath,metadata);
    ServiceUtils::write(dir.filePath("minecraft/versions/fixture/fixture.json"),{{"id","fixture"},{"mainClass","fixture.Main"},{"minecraftArguments","--username ${auth_player_name} --gameDir ${game_directory}"}});{QFile f(dir.filePath("minecraft/versions/fixture/fixture.jar"));QVERIFY(f.open(QIODevice::WriteOnly));f.write("fixture");}
    GameService game(dir.path());game.setIdentityProvider([]{return QVariantMap{{"name","Tester"},{"uuid",QString(32,'0')},{"type","offline"},{"accessToken","0"}};});QSignalSpy ok(&game,&FeatureService::completed),bad(&game,&FeatureService::failed),state(&game,&GameService::gameStateChanged);game.execute("game.launch",{{"instanceId",id},{"server","localhost:25566"}});QTRY_COMPARE(state.size(),2);QCOMPARE(bad.size(),0);QCOMPARE(ok.size(),1);QFile log(dir.filePath("logs/game.log"));QVERIFY(log.open(QIODevice::ReadOnly));auto output=log.readAll();QVERIFY(output.contains("--server\nlocalhost\n--port\n25566"));QVERIFY(output.contains("--gameDir\n"+instances.gamePath(id).toUtf8()+"\n"));QVERIFY2(output.contains("-Xmx3G\n"),output.constData());
#endif
}
void resolvesArgumentsWithoutShell(){QJsonArray values{"--username","${auth_player_name}","--gameDir","${game_directory}"};auto args=GameService::arguments(values,{{"auth_player_name","Test"},{"game_directory","/tmp/path with spaces;whoami"}});QCOMPARE(args,QStringList({"--username","Test","--gameDir","/tmp/path with spaces;whoami"}));}
void filtersFeatureArguments(){QJsonObject value{{"rules",QJsonArray{QJsonObject{{"action","allow"},{"features",QJsonObject{{"is_demo_user",true}}}}}},{"value","--demo"}};QCOMPARE(GameService::arguments(QJsonArray{value},{ }).size(),0);}
void refusesInvalidInstallBeforeNetwork(){QTemporaryDir dir;GameService game(dir.path());QSignalSpy failures(&game,&FeatureService::failed);game.execute("game.install",{{"instanceId","../../outside"},{"version","1.21.1"}});QCOMPARE(failures.size(),1);QVERIFY(!game.downloadState().value("active").toBool());}
void downloadSourceMirrorMapping(){
    using namespace DownloadSource;
    QCOMPARE(mirrorHost(),QString("bmclapi2.bangbang93.com"));
    QCOMPARE(mirror(QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json")).toString(),QString("https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json"));
    QCOMPARE(mirror(QUrl("https://piston-meta.mojang.com/v1/packages/abc/1.21.1.json")).path(),QString("/v1/packages/abc/1.21.1.json"));
    QCOMPARE(mirror(QUrl("https://piston-data.mojang.com/v1/objects/hash/client.jar")).path(),QString("/v1/objects/hash/client.jar"));
    QCOMPARE(mirror(QUrl("https://launchermeta.mojang.com/v1/packages/hash/pre-1.6.json")).host(),mirrorHost());
    QCOMPARE(mirror(QUrl("https://resources.download.minecraft.net/ab/abcdef")).path(),QString("/assets/ab/abcdef"));
    QCOMPARE(mirror(QUrl("https://libraries.minecraft.net/a/b/c.jar")).path(),QString("/maven/a/b/c.jar"));
    QCOMPARE(mirror(QUrl("https://meta.fabricmc.net/v2/versions/loader/1.21.1")).path(),QString("/fabric-meta/v2/versions/loader/1.21.1"));
    QCOMPARE(mirror(QUrl("https://maven.fabricmc.net/a/b/c.jar")).path(),QString("/maven/a/b/c.jar"));
    QCOMPARE(mirror(QUrl("https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml")).path(),QString("/maven/net/minecraftforge/forge/maven-metadata.xml"));
    QCOMPARE(mirror(QUrl("https://files.minecraftforge.net/maven/net/minecraftforge/forge/x.jar")).path(),QString("/maven/net/minecraftforge/forge/x.jar"));
    QCOMPARE(mirror(QUrl("https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml")).path(),QString("/maven/net/neoforged/neoforge/maven-metadata.xml"));
    QCOMPARE(mirror(QUrl("https://authlib-injector.yushi.moe/artifact/latest.json")).path(),QString("/mirrors/authlib-injector/artifact/latest.json"));
    // Quilt 官方文档标注镜像暂不可用，保持官方；未知主机（社区 CDN/本机夹具）也不改写。
    QCOMPARE(mirror(QUrl("https://meta.quiltmc.org/v3/versions/loader/1.21.1")).host(),QString("meta.quiltmc.org"));
    QCOMPARE(mirror(QUrl("https://cdn.modrinth.com/data/x/versions/y/mod.jar")).host(),QString("cdn.modrinth.com"));
    QCOMPARE(mirror(QUrl("http://127.0.0.1:9/x")),QUrl("http://127.0.0.1:9/x"));
    QCOMPARE(resolve(QUrl("https://meta.fabricmc.net/v2/x"),"official").host(),QString("meta.fabricmc.net"));
    QCOMPARE(resolve(QUrl("https://meta.fabricmc.net/v2/x"),"bmclapi").host(),mirrorHost());
    QTemporaryDir dir;GameService game(dir.path());QCOMPARE(game.downloadSource(),officialId());
    game.setDownloadSource("bmclapi");QCOMPARE(game.downloadSource(),mirrorId());
    QCOMPARE(game.mapDownloadUrl(QUrl("https://libraries.minecraft.net/a.jar")).host(),mirrorHost());
    game.setDownloadSource("not-a-source");QCOMPARE(game.downloadSource(),mirrorId());
    game.setDownloadSource("official");QCOMPARE(game.mapDownloadUrl(QUrl("https://libraries.minecraft.net/a.jar")).host(),QString("libraries.minecraft.net"));
}
void liveMirrorVersionListAndAuthlib(){
    if(qEnvironmentVariableIsEmpty("XMCL_LIVE_MIRROR"))QSKIP("live BMCLAPI probe disabled; set XMCL_LIVE_MIRROR=1");
    QTemporaryDir dir;GameService game(dir.path());game.setDownloadSource(DownloadSource::mirrorId());
    QSignalSpy ok(&game,&FeatureService::completed),bad(&game,&FeatureService::failed);
    game.execute("version.list",{{"type","release"}});
    QTRY_VERIFY_WITH_TIMEOUT(ok.size()>0||bad.size()>0,30000);
    QVERIFY2(bad.isEmpty(),qPrintable(bad.isEmpty()?QString():bad.last().at(1).toString()));
    QVERIFY(ok.first().at(1).toMap().value("items").toList().size()>100);
    game.execute("authlib.prepare",{});
    QTRY_VERIFY_WITH_TIMEOUT(ok.size()>1||bad.size()>0,60000);
    QVERIFY2(bad.isEmpty(),qPrintable(bad.isEmpty()?QString():bad.last().at(1).toString()));
    QVERIFY(QFile::exists(dir.filePath("runtime/authlib-injector.jar")));
}
void liveVersionListFromMojang(){
    if(qEnvironmentVariableIsEmpty("XMCL_LIVE"))QSKIP("live Mojang probe disabled; set XMCL_LIVE=1");
    QTemporaryDir dir;GameService game(dir.path());QSignalSpy ok(&game,&FeatureService::completed),bad(&game,&FeatureService::failed);
    game.execute("version.list",{{"type","release"}});
    QTRY_VERIFY_WITH_TIMEOUT(ok.size()>0||bad.size()>0,20000);
    QCOMPARE(bad.size(),0);
    QVERIFY(ok.first().at(1).toMap().value("items").toList().size()>100);
}
void liveInstallDownloadsRealFiles(){
    if(qEnvironmentVariableIsEmpty("XMCL_LIVE_OLD_INSTALL"))QSKIP("live old-version install probe disabled; set XMCL_LIVE_OLD_INSTALL=1");
    QTemporaryDir dir;GameService game(dir.path());QSignalSpy ok(&game,&FeatureService::completed),bad(&game,&FeatureService::failed);
    game.execute("game.install",{{"createInstance",true},{"name","Real 1.0"},{"version","1.0"},{"loader","vanilla"},{"loaderVersion",""}});
    QTRY_VERIFY_WITH_TIMEOUT(bad.size()>0||game.downloadState().value("phase").toString()==QString("complete"),600000);
    QVERIFY2(bad.isEmpty(),qPrintable(bad.isEmpty()?QString():bad.last().at(1).toString()));
    InstanceService instances(dir.path());QCOMPARE(instances.list().size(),1);
    QVERIFY(instances.list().first().toMap().value("installed").toBool());
    QVERIFY(QFile::exists(dir.filePath("minecraft/versions/1.0/1.0.jar")));
    // 旧版资源索引里不同名称可能共享同一个哈希；对象目录只需要保存唯一哈希对应文件。
    const auto index=ServiceUtils::read(dir.filePath("minecraft/assets/indexes/pre-1.6.json"));
    QSet<QString> hashes;for(const auto &value:index.value("objects").toMap())hashes.insert(value.toMap().value("hash").toString());
    int assetFiles=0;QDirIterator assets(dir.filePath("minecraft/assets/objects"),QDir::Files,QDirIterator::Subdirectories);while(assets.hasNext()){assets.next();++assetFiles;}
    QVERIFY2(assetFiles==hashes.size(),qPrintable(QString("asset files %1 expected %2").arg(assetFiles).arg(hashes.size())));
    for(const auto &hash:hashes)QVERIFY(DownloadEngine::verified(dir.filePath("minecraft/assets/objects/"+hash.left(2)+'/'+hash),hash.toLatin1(),"sha1"));
    QVERIFY(!QDir(dir.filePath("minecraft/assets/indexes")).entryList(QDir::Files).isEmpty());
}

void checksAndRepairsMissingFiles(){
    QTemporaryDir dir;TestGameServer server;server.includeLibrary=true;
    GameService game(dir.path(),nullptr,server.url("/manifest.json"));
    QSignalSpy ok(&game,&FeatureService::completed),bad(&game,&FeatureService::failed);
    game.execute("game.install",{{"createInstance",true},{"name","Repair"},{"version","1.21.1"},{"loader","vanilla"}});
    QTRY_VERIFY_WITH_TIMEOUT(game.downloadState().value("phase").toString()==QString("complete"),20000);
    InstanceService instances(dir.path());QCOMPARE(instances.list().size(),1);const auto id=instances.list().first().toMap().value("id").toString();
    const auto libraryPath=dir.filePath("minecraft/libraries/demo/lib/lib.jar");
    QVERIFY(QFile::exists(libraryPath));
    QVERIFY(QFile::remove(libraryPath));
    ok.clear();
    game.execute("launch.check",{{"instanceId",id}});
    QTRY_VERIFY_WITH_TIMEOUT(!ok.isEmpty()||!bad.isEmpty(),10000);
    QVERIFY2(bad.isEmpty(),qPrintable(bad.isEmpty()?QString():bad.last().at(1).toString()));
    const auto check=ok.last().at(1).toMap();
    QVERIFY2(check.value("fixable").toBool(),qPrintable(QString::number(check.value("files").toMap().value("missing").toInt())));
    QVERIFY(check.value("files").toMap().value("missing").toInt()>=1);
    ok.clear();bad.clear();
    game.execute("launch.repair",{{"instanceId",id}});
    QTRY_VERIFY_WITH_TIMEOUT(!ok.isEmpty()||!bad.isEmpty(),30000);
    QVERIFY2(bad.isEmpty(),qPrintable(bad.isEmpty()?QString():bad.last().at(1).toString()));
    QCOMPARE(ok.last().at(0).toString(),QString("launch.repair"));
    QVERIFY(QFile::exists(libraryPath));
    ok.clear();
    game.execute("launch.check",{{"instanceId",id}});
    QTRY_VERIFY_WITH_TIMEOUT(!ok.isEmpty(),10000);
    QCOMPARE(ok.last().at(1).toMap().value("fixable").toBool(),false);
    QCOMPARE(ok.last().at(1).toMap().value("files").toMap().value("missing").toInt(),0);
}
void diagnosesKnownLogProblems(){
    QTemporaryDir dir;QDir().mkpath(dir.filePath("logs"));
    {QFile log(dir.filePath("logs/game.log"));QVERIFY(log.open(QIODevice::WriteOnly));
     log.write("java.lang.UnsupportedClassVersionError: test/Main has been compiled by a more recent version of the Java Runtime (class file version 61.0)\njava.lang.OutOfMemoryError: Java heap space\n");}
    GameService game(dir.path());
    QSignalSpy ok(&game,&FeatureService::completed),bad(&game,&FeatureService::failed);
    game.execute("diagnostic.analyze",{{"instanceId",""}});
    QTRY_VERIFY_WITH_TIMEOUT(!ok.isEmpty()||!bad.isEmpty(),10000);
    QVERIFY2(bad.isEmpty(),qPrintable(bad.isEmpty()?QString():bad.last().at(1).toString()));
    const auto data=ok.last().at(1).toMap();const auto findings=data.value("findings").toList();
    QVERIFY(!findings.isEmpty());
    bool javaRule=false,memoryRule=false;
    for(const auto &finding:findings){const auto item=finding.toMap();if(item.value("id").toString()=="java-class-version"){javaRule=true;QVERIFY(item.value("advice").toString().contains("Java 17"));}if(item.value("id").toString()=="out-of-memory")memoryRule=true;}
    QVERIFY(javaRule);QVERIFY(memoryRule);
    QVERIFY(data.value("logExists").toBool());
}
void quickPlayIsUsedWhenVersionSupportsIt(){
#ifdef Q_OS_WIN
    QSKIP("POSIX fake Java fixture");
#else
    QTemporaryDir dir(QDir::tempPath()+"/xmcl quickplay XXXXXX");InstanceService instances(dir.path());QSignalSpy created(&instances,&FeatureService::completed);instances.execute("instance.create",{{"name","Quick"},{"version","fixture"}});const auto id=created.first().at(1).toMap().value("id").toString();
    const auto java=dir.filePath("fake java");{QFile f(java);QVERIFY(f.open(QIODevice::WriteOnly));f.write("#!/bin/sh\nprintf '%s\\n' \"$@\"\n");}QVERIFY(QFile::setPermissions(java,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
    const auto metadataPath=instances.instancePath(id)+"/instance.json";auto metadata=ServiceUtils::read(metadataPath);metadata["installed"]=true;metadata["javaPath"]=java;metadata["memoryGb"]=2;ServiceUtils::write(metadataPath,metadata);
    const QJsonObject quickRule{{"action","allow"},{"features",QJsonObject{{"is_quick_play_multiplayer",true}}}};
    const QJsonObject quickArgument{{"rules",QJsonArray{quickRule}},{"value","--quickPlayMultiplayer"}};
    const QJsonObject quickArguments{{"game",QJsonArray{quickArgument}}};
    ServiceUtils::write(dir.filePath("minecraft/versions/fixture/fixture.json"),QJsonObject{{"id","fixture"},{"mainClass","fixture.Main"},{"arguments",quickArguments}}.toVariantMap());
    {QFile f(dir.filePath("minecraft/versions/fixture/fixture.jar"));QVERIFY(f.open(QIODevice::WriteOnly));f.write("fixture");}
    GameService game(dir.path());game.setIdentityProvider([]{return QVariantMap{{"name","Tester"},{"uuid",QString(32,'0')},{"type","offline"},{"accessToken","0"}};});
    QSignalSpy ok(&game,&FeatureService::completed),bad(&game,&FeatureService::failed),state(&game,&GameService::gameStateChanged);
    game.execute("game.launch",{{"instanceId",id},{"server","localhost:25566"}});
    QTRY_COMPARE_WITH_TIMEOUT(state.size(),2,10000);QCOMPARE(bad.size(),0);
    QFile log(dir.filePath("logs/game.log"));QVERIFY(log.open(QIODevice::ReadOnly));const auto output=log.readAll();
    QVERIFY2(output.contains("--quickPlayMultiplayer\nlocalhost:25566"),output.constData());
    QVERIFY(!output.contains("--server\nlocalhost"));
#endif
}
void needsAccountToLaunch(){QTemporaryDir dir;GameService game(dir.path());QSignalSpy failures(&game,&FeatureService::failed);game.execute("game.launch",{{"instanceId","id"}});QCOMPARE(failures.size(),1);QVERIFY(failures.first().at(1).toString().contains("账号"));}
};
QTEST_GUILESS_MAIN(GameTest)
#include "tst_game.moc"
