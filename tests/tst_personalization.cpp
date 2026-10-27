#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QImage>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "services/PersonalizationService.h"

class PersonalizationTest : public QObject {
    Q_OBJECT
private slots:
    void defaultsAndPersistence() {
        QTemporaryDir root;
        PersonalizationService service(root.path());
        QVERIFY(service.settings().value("accentColor").toString().isEmpty());
        QVERIFY(service.settings().value("backgroundUrl").toString().isEmpty());
        QCOMPARE(service.settings().value("shortcuts").toMap().value("home").toString(), QString("Ctrl+1"));
        QSignalSpy changed(&service, &PersonalizationService::changed);
        service.execute("personalization.theme", {{"color", "#ab1234"}});
        QCOMPARE(changed.count(), 1);
        PersonalizationService restored(root.path());
        QCOMPARE(restored.settings().value("accentColor").toString(), QString("#ab1234"));
        QTemporaryDir other;
        PersonalizationService isolated(other.path());
        QVERIFY(isolated.settings().value("accentColor").toString().isEmpty());
    }
    void imagesAndPaths() {
        QTemporaryDir root;
        const QString source = root.filePath("image.png");
        QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::red); QVERIFY(image.save(source));
        PersonalizationService service(root.path());
        QSignalSpy failed(&service, &FeatureService::failed);
        service.execute("personalization.background", {{"path", source}});
        const QString owned = QUrl(service.settings().value("backgroundUrl").toString()).toLocalFile();
        QVERIFY(QFile::exists(owned)); QVERIFY(owned != source);
        QVERIFY(QFile::remove(source));
        PersonalizationService restored(root.path());
        QCOMPARE(restored.settings().value("backgroundUrl"), service.settings().value("backgroundUrl"));
        service.execute("personalization.thumbnail", {{"instanceId", "../outside"}, {"path", owned}});
        QCOMPARE(failed.count(), 1);
        service.execute("personalization.thumbnail", {{"instanceId", "instance_1"}, {"path", owned}});
        QVERIFY(service.settings().value("thumbnails").toMap().contains("instance_1"));
        QFile bad(root.filePath("bad.png")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("not an image"); bad.close();
        service.execute("personalization.background", {{"path", bad.fileName()}});
        service.execute("personalization.background", {{"path", "https://example.com/image.png"}});
        QCOMPARE(failed.count(), 3);
        QCOMPARE(service.settings().value("backgroundUrl").toString(), QUrl::fromLocalFile(owned).toString());
    }
    void shortcutValidation() {
        QTemporaryDir root; PersonalizationService service(root.path());
        QSignalSpy failed(&service, &FeatureService::failed);
        service.execute("personalization.shortcut", {{"target", "home"}, {"sequence", "Ctrl+2"}});
        service.execute("personalization.shortcut", {{"target", "home"}, {"sequence", "not a real key"}});
        service.execute("personalization.shortcut", {{"target", "unknown"}, {"sequence", "Ctrl+9"}});
        QCOMPARE(failed.count(), 3);
        service.execute("personalization.shortcut", {{"target", "home"}, {"sequence", "Ctrl+9"}});
        QCOMPARE(service.settings().value("shortcuts").toMap().value("home").toString(), QString("Ctrl+9"));
        PersonalizationService restored(root.path());
        QCOMPARE(restored.settings().value("shortcuts"), service.settings().value("shortcuts"));
        service.execute("personalization.reset", {{"confirm",true}});
        QCOMPARE(service.settings().value("shortcuts").toMap().value("home").toString(), QString("Ctrl+1"));
    }
    void corruptSettingsAndSymlinks() {
        QTemporaryDir root, outside;
        QDir().mkpath(root.filePath("personalization"));
        QFile config(root.filePath("personalization/settings.json"));
        QVERIFY(config.open(QIODevice::WriteOnly)); config.write("bad json"); config.close();
        PersonalizationService service(root.path());
        QVERIFY(service.settings().value("accentColor").toString().isEmpty());
        QSignalSpy failed(&service, &FeatureService::failed);
        service.execute("personalization.theme", {{"color", "red"}});
        QCOMPARE(failed.count(), 1);
        QVERIFY(QFile::remove(config.fileName()));
        QVERIFY(QFile::link(outside.filePath("untouched.json"), config.fileName()));
        service.execute("personalization.theme", {{"color", "#123456"}});
        QCOMPARE(failed.count(), 2);
        QVERIFY(!QFile::exists(outside.filePath("untouched.json")));
    }
    void nativePluginLifecycle() {
        const auto fixture = qEnvironmentVariable("XMCL_TEST_PLUGIN");
        if (fixture.isEmpty()) QSKIP("Set XMCL_TEST_PLUGIN to the trusted, locally built echo example");
        QTemporaryDir root; PersonalizationService service(root.path());
        QSignalSpy failed(&service, &FeatureService::failed), completed(&service, &FeatureService::completed);
        const QVariantMap id{{"id", "org.xmcl.example.echo"},{"confirm",true}};
        service.execute("plugin.install", {{"path", fixture}});
        QCOMPARE(failed.count(), 0);
        service.execute("plugin.execute", {{"id", id.value("id")}, {"command", "echo"}, {"text", "hello"}});
        QCOMPARE(failed.count(), 1); // Installing does not run code.
        service.execute("plugin.enable", id);
        QCOMPARE(failed.count(), 1);
        service.execute("plugin.execute", {{"id", id.value("id")}, {"command", "echo"}, {"text", "hello"}});
        QCOMPARE(completed.last().at(1).toMap().value("message").toString(), QString("hello"));
        QCOMPARE(completed.last().at(1).toMap().value("running").toBool(), true);
        { PersonalizationService reopened(root.path()); QSignalSpy list(&reopened, &FeatureService::completed);
          reopened.execute("plugin.list", {});
          QVERIFY(!list.last().at(1).toMap().value("items").toList().first().toMap().value("enabled").toBool()); }
        service.execute("plugin.disable", id);
        service.execute("plugin.execute", {{"id", id.value("id")}, {"command", "echo"}});
        QCOMPARE(failed.count(), 2);
        service.execute("plugin.enable", id); QCOMPARE(failed.count(), 2);
        service.execute("plugin.disable", id);
        const QString file = root.filePath("native-plugins/org.xmcl.example.echo." + QFileInfo(fixture).suffix());
        QFile tamper(file); QVERIFY(tamper.open(QIODevice::Append)); tamper.write("modified"); tamper.close();
        service.execute("plugin.enable", id); QCOMPARE(failed.count(), 3);
        service.execute("plugin.remove", id); QCOMPARE(failed.count(), 3);
        service.execute("plugin.list", {});
        QVERIFY(completed.last().at(1).toMap().value("items").toList().isEmpty());
        service.execute("plugin.install", {{"path", fixture}});
        QCOMPARE(failed.count(), 4); // Never reuse Qt's cached old module when replacing a loaded plugin.
    }
    void downloadSourceSelection() {
        QTemporaryDir root; PersonalizationService service(root.path());
        QCOMPARE(service.settings().value("downloadSource").toString(), QString("official"));
        bool hasAction=false;
        for (const auto &value : service.actions()) if (value.toMap().value("id") == "personalization.downloadSource") hasAction = value.toMap().value("fields").toList().first().toMap().value("options").toList().contains("bmclapi");
        QVERIFY(hasAction);
        QSignalSpy failed(&service, &FeatureService::failed), changed(&service, &PersonalizationService::changed);
        service.execute("personalization.downloadSource", {{"source", "bmclapi"}});
        QCOMPARE(failed.count(), 0); QCOMPARE(changed.count(), 1);
        QCOMPARE(service.settings().value("downloadSource").toString(), QString("bmclapi"));
        PersonalizationService restored(root.path());
        QCOMPARE(restored.settings().value("downloadSource").toString(), QString("bmclapi"));
        service.execute("personalization.downloadSource", {{"source", "mirror.example.com"}});
        QCOMPARE(failed.count(), 1);
        QCOMPARE(service.settings().value("downloadSource").toString(), QString("bmclapi"));
        service.execute("personalization.downloadSource", {{"source", "official"}});
        PersonalizationService restoredOfficial(root.path());
        QCOMPARE(restoredOfficial.settings().value("downloadSource").toString(), QString("official"));
    }
    void invalidPluginRejected() {
        QTemporaryDir root; PersonalizationService service(root.path());
        QFile fake(root.filePath("fake.dylib")); QVERIFY(fake.open(QIODevice::WriteOnly)); fake.write("{\"IID\":\"org.xmcl.NativePlugin/1.0\"}"); fake.close();
        QSignalSpy failed(&service, &FeatureService::failed);
        service.execute("plugin.install", {{"path", fake.fileName()}});
        service.execute("plugin.enable", {{"id", "../fake"}});
        QCOMPARE(failed.count(), 2);
        QSignalSpy completed(&service, &FeatureService::completed);
        service.execute("plugin.list", {});
        QVERIFY(completed.last().at(1).toMap().value("items").toList().isEmpty());
        bool confirmation = false;
        for (const auto &value : service.actions()) if (value.toMap().value("id") == "plugin.enable") confirmation = value.toMap().value("confirm").toBool();
        QVERIFY(confirmation);
    }
};
QTEST_GUILESS_MAIN(PersonalizationTest)
#include "tst_personalization.moc"
