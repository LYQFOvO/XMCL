#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFile>
#include <archive.h>
#include <archive_entry.h>
#include "InstanceService.h"
#include "ResourceService.h"
#include "ArchiveUtils.h"
#include "MemoryAdvisor.h"

class InstanceTests : public QObject {
    Q_OBJECT
    QVariantMap run(FeatureService &service,const QString &action,QVariantMap args={}) {
        QSignalSpy ok(&service,&FeatureService::completed), bad(&service,&FeatureService::failed);
        service.execute(action,args);
        if(ok.isEmpty() && bad.isEmpty()) ok.wait(3000);
        if(!bad.isEmpty()) return {{"error",bad.first().at(1)}};
        if(ok.isEmpty()) return {{"error","no result"}};
        return ok.first().at(1).toMap();
    }
    QString create(InstanceService &s) { return run(s,"instance.create",{{"name","Test"},{"version","1.21.1"}}).value("id").toString(); }
    void write(const QString &path,QByteArray data) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(data),data.size()); }
private slots:
    void smartMemoryDefaultsAndValidation() {
        QTemporaryDir tmp; InstanceService s(tmp.path()); QString id=create(s); QVERIFY(!id.isEmpty());
        auto meta=s.list().first().toMap();
        QCOMPARE(meta.value("memoryGb").toInt(), MemoryAdvisor::suggestedGb());
        const int limit=MemoryAdvisor::maxGb();
        QVERIFY(!run(s,"instance.configure",{{"instanceId",id},{"memoryGb",limit}}).contains("error"));
        auto got=run(s,"instance.get",{{"instanceId",id}}).value("instance").toMap();
        QCOMPARE(got.value("memoryGb").toInt(),limit);
        QVERIFY(run(s,"instance.configure",{{"instanceId",id},{"memoryGb",limit+1}}).contains("error"));
        QVERIFY(run(s,"instance.configure",{{"instanceId",id},{"memoryGb",0}}).contains("error"));
        QVERIFY(run(s,"instance.configure",{{"instanceId",id},{"width",100}}).contains("error"));
        QVERIFY(run(s,"instance.configure",{{"instanceId",id},{"height",10}}).contains("error"));
        QVERIFY(!run(s,"instance.configure",{{"instanceId",id},{"width",1600},{"height",900}}).contains("error"));
        got=run(s,"instance.get",{{"instanceId",id}}).value("instance").toMap();
        QCOMPARE(got.value("width").toInt(),1600); QCOMPARE(got.value("height").toInt(),900);
    }
    void persistenceAndConfiguration() {
        QTemporaryDir tmp; InstanceService s(tmp.path()); QString id=create(s); QVERIFY(!id.isEmpty());
        QVERIFY(!run(s,"instance.rename",{{"instanceId",id},{"name","Renamed"}}).contains("error"));
        QVERIFY(!run(s,"instance.configure",{{"instanceId",id},{"javaPath","/usr/bin/java"},{"jvmArgs","-Xmx2G"},{"gameArgs","--demo"}}).contains("error"));
        InstanceService again(tmp.path()); QCOMPARE(again.list().size(),1);
        QCOMPARE(again.list().first().toMap().value("name").toString(),QString("Renamed"));
        QCOMPARE(again.list().first().toMap().value("jvmArgs").toString(),QString("-Xmx2G"));
    }
    void backupRestoreAndCopy() {
        QTemporaryDir tmp; InstanceService s(tmp.path()); QString id=create(s); QVERIFY(!id.isEmpty());
        write(s.gamePath(id)+"/options.txt","original");
        auto backup=run(s,"instance.backup",{{"instanceId",id}}); QVERIFY2(!backup.contains("error"),qPrintable(backup.value("error").toString()));
        auto restored=run(s,"instance.restore",{{"archivePath",backup.value("path")},{"name","Restored"}});
        QVERIFY2(!restored.contains("error"),qPrintable(restored.value("error").toString()));
        QString second=restored.value("id").toString(); QVERIFY(second!=id);
        QFile f(s.gamePath(second)+"/options.txt"); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),QByteArray("original"));
        auto copied=run(s,"instance.copy",{{"instanceId",id},{"name","Copy"}}); QVERIFY(!copied.contains("error"));
        QCOMPARE(s.list().size(),3);
        QVERIFY(run(s,"instance.remove",{{"instanceId",id}}).contains("error"));
        QVERIFY(!run(s,"instance.remove",{{"instanceId",id},{"confirm",true}}).contains("error")); QCOMPARE(s.list().size(),2);
    }
    void resourceSafetyAndDuplicates() {
        QTemporaryDir tmp,source; InstanceService s(tmp.path()); ResourceService r(tmp.path()); QString id=create(s);
        write(source.path()+"/safe.jar","jar"); QVariantMap args{{"instanceId",id},{"kind","mods"},{"sourcePath",source.path()+"/safe.jar"}};
        QVERIFY(!run(r,"resource.import",args).contains("error")); QVERIFY(run(r,"resource.import",args).contains("error"));
        auto list=run(r,"resource.list",{{"instanceId",id},{"kind","mods"}}); QCOMPARE(list.value("items").toList().size(),1);
        args={{"instanceId",id},{"kind","mods"},{"name","safe.jar"},{"enabled",false}};
        QVERIFY(!run(r,"resource.toggle",args).contains("error")); QVERIFY(QFile::exists(s.gamePath(id)+"/mods/safe.jar.disabled"));
        args["name"]="../../instance.json"; QVERIFY(run(r,"resource.remove",args).contains("error"));
        QVERIFY(run(s,"instance.remove",{{"instanceId","../outside"},{"confirm",true}}).contains("error"));
        QVERIFY(QFile::link(source.path(),s.gamePath(id)+"/shaderpacks"));
        args={{"instanceId",id},{"kind","shaderpacks"},{"sourcePath",source.path()+"/safe.jar"}};
        QVERIFY(run(r,"resource.import",args).contains("error"));
    }
    void migrationAndIsolation() {
        QTemporaryDir tmp,source; write(source.path()+"/options.txt","migrated"); InstanceService s(tmp.path());
        auto imported=run(s,"instance.migrate",{{"sourcePath",source.path()},{"name","Imported"}}); QVERIFY(!imported.contains("error")); QString id=imported.value("id").toString();
        QFile f(s.gamePath(id)+"/options.txt"); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),QByteArray("migrated"));
        QVERIFY(!run(s,"instance.isolation",{{"instanceId",id},{"isolated",false}}).contains("error")); QCOMPARE(s.gamePath(id),tmp.path()+"/minecraft");
        QVERIFY(!run(s,"instance.isolation",{{"instanceId",id},{"isolated",true}}).contains("error")); QVERIFY(s.gamePath(id).contains(id));
    }
    void serverPersistence() {
        QTemporaryDir tmp; InstanceService s(tmp.path()); ResourceService r(tmp.path()); QString id=create(s);
        QVERIFY(!run(r,"server.save",{{"instanceId",id},{"name","Local"},{"address","localhost:25565"}}).contains("error"));
        ResourceService again(tmp.path()); auto entries=run(again,"server.list",{{"instanceId",id}}).value("items").toList(); QCOMPARE(entries.size(),1); QCOMPARE(entries.first().toMap().value("address").toString(),QString("localhost:25565"));
        QVERIFY(QFile::exists(s.gamePath(id)+"/servers.dat"));
        QVERIFY(!run(r,"server.remove",{{"instanceId",id},{"address","localhost:25565"},{"confirm",true}}).contains("error"));
        QCOMPARE(run(r,"server.list",{{"instanceId",id}}).value("items").toList().size(),0);
    }
    void archiveRejectsTraversalAndLimits() {
        QTemporaryDir tmp;
        auto make=[&](const QString &path,const char *name,int type=AE_IFREG) {
            archive *a=archive_write_new(); archive_write_set_format_pax_restricted(a); archive_write_open_filename(a,path.toUtf8().constData());
            archive_entry *e=archive_entry_new(); archive_entry_set_pathname(e,name); archive_entry_set_filetype(e,type); archive_entry_set_perm(e,0644); archive_entry_set_size(e,type==AE_IFREG?4:0);
            if(type==AE_IFLNK) archive_entry_set_symlink(e,"/tmp"); archive_write_header(a,e); if(type==AE_IFREG) archive_write_data(a,"data",4); archive_entry_free(e); archive_write_close(a); archive_write_free(a);
        };
        make(tmp.path()+"/bad.tar","../escaped");
        QVERIFY_EXCEPTION_THROWN(ArchiveUtils::extract(tmp.path()+"/bad.tar",tmp.path()+"/out"),std::runtime_error); QVERIFY(!QFile::exists(tmp.path()+"/escaped"));
        make(tmp.path()+"/link.tar","link",AE_IFLNK); QVERIFY_EXCEPTION_THROWN(ArchiveUtils::extract(tmp.path()+"/link.tar",tmp.path()+"/out2"),std::runtime_error);
        make(tmp.path()+"/limit.tar","file"); QVERIFY_EXCEPTION_THROWN(ArchiveUtils::extract(tmp.path()+"/limit.tar",tmp.path()+"/out3",3),std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(InstanceTests)
#include "tst_instances.moc"
