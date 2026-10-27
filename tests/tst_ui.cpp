#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QColor>
#include <cmath>
#include <functional>
#include <QElapsedTimer>
#include "LauncherController.h"
#include "services/AccountService.h"
#include "services/ServiceHub.h"
#include "services/MemoryAdvisor.h"
#include "services/ServiceUtils.h"
#include <QJsonObject>
#include <QCryptographicHash>
#include <QDir>
#include "TestGameServer.h"
#include "TestCommunityServer.h"
Q_IMPORT_QML_PLUGIN(XMCLPlugin)

namespace {
double relativeLuminance(const QColor &color) {
    const auto channel = [](double value) {
        value /= 255.0;
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) + 0.0722 * channel(color.blue());
}
double contrastRatio(const QColor &a, const QColor &b) {
    const auto hi = std::max(relativeLuminance(a), relativeLuminance(b));
    const auto lo = std::min(relativeLuminance(a), relativeLuminance(b));
    return (hi + 0.05) / (lo + 0.05);
}
}

class UiTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    TestGameServer gameServer;
    TestCommunityServer communityServer;
    std::unique_ptr<LauncherController> controller;
    std::unique_ptr<ServiceHub> features;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;
    QList<QQmlError> warnings;
    QQuickItem *findVisual(QQuickItem *parent, const QString &name) {
        if (parent->objectName() == name) return parent;
        for (auto *child : parent->childItems())
            if (auto *found = findVisual(child, name)) return found;
        return nullptr;
    }
    QObject *find(const QString &name) {
        if (auto *found = window->findChild<QObject *>(name)) return found;
        return findVisual(window->contentItem(), name);
    }
    bool click(const QString &name) {
        auto *item = qobject_cast<QQuickItem *>(find(name));
        if (!item || !item->isVisible() || !item->isEnabled()) return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                         item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        return true;
    }
    void saveShot(const QString &name) {
        const QString directory = QCoreApplication::applicationDirPath() + "/ui-verified";
        QDir().mkpath(directory);
        window->grabWindow().save(directory + '/' + name + ".png");
    }
    bool waitUntil(const std::function<bool()> &predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (predicate()) return true;
            QTest::qWait(50);
        }
        return predicate();
    }
    QVariantMap instanceNamed(const QString &name) {
        for (const auto &value : features->instances())
            if (value.toMap().value("name").toString() == name) return value.toMap();
        return {};
    }
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        controller = std::make_unique<LauncherController>(dir.filePath("settings.ini"));
        features = std::make_unique<ServiceHub>(dir.filePath("data"), nullptr, gameServer.url("/manifest.json"), communityServer.url(""));
        engine = std::make_unique<QQmlApplicationEngine>();
        connect(engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &items) { warnings.append(items); });
        engine->rootContext()->setContextProperty("launcher", controller.get());
        engine->rootContext()->setContextProperty("features", features.get());
        engine->loadFromModule("XMCL", "Main");
        QVERIFY2(!engine->rootObjects().isEmpty(), "Production QML must load");
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    void cleanup() {
        for (int attempt = 0; attempt < 5; ++attempt) {
            bool open = false;
            for (const auto &name : {"informationDialog", "installDialog", "communityDialog", "instanceSettingsDialog", "launchCheckDialog", "featurePanel"})
                if (auto *object = find(name); object && object->property("visible").toBool()) open = true;
            if (!open) break;
            QTest::keyClick(window, Qt::Key_Escape);
            QTest::qWait(80);
        }
        QTest::qWait(30);
    }
    void navigationAndSelection() {
        QVERIFY(click("nav1"));
        QCOMPARE(window->property("currentPage").toInt(), 1);
        QTest::qWait(100);
        QVERIFY(click("instance2Select"));
        QCOMPARE(controller->selectedIndex(), 2);
        QVERIFY(click("nav0"));
        QCOMPARE(find("currentInstanceName")->property("text").toString(), QStringLiteral("云上建筑师"));
    }
    void launchExplainsPreview() {
        QVERIFY(click("launchButton"));
        auto *dialog = find("informationDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(dialog->property("message").toString().contains(QStringLiteral("不会启动真实游戏")));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
    void searchAndEmptyState() {
        QVERIFY(click("nav1"));
        auto *search = find("instanceSearch");
        QVERIFY(search);
        search->setProperty("text", QStringLiteral("云上"));
        QTRY_VERIFY(!find("instance0")->property("visible").toBool());
        QVERIFY(find("instance2")->property("visible").toBool());
        search->setProperty("text", "not-a-real-instance");
        QTRY_VERIFY(!find("instance2")->property("visible").toBool());
        search->setProperty("text", "");
    }
    void downloadOpensCommunityDialog() {
        QVERIFY(click("nav2"));
        QTest::qWait(100);
        QVERIFY(click("openCommunityDownloads"));
        auto *dialog = find("communityDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
    void dropdownPopupContrastAndLoginEntry() {
        // 未登录时点击右上角账号入口，应直接进入微软设备码登录，并预填内置 Client ID。
        QVERIFY(click("accountButton"));
        auto *panel = find("featurePanel");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        QTRY_COMPARE(panel->property("activeAction").toMap().value("id").toString(), QString("microsoft.login"));
        QTRY_VERIFY(find("featureField_clientId") != nullptr);
        auto *clientField = find("featureField_clientId");
        QCOMPARE(clientField->property("text").toString(), AccountService::defaultMicrosoftClientId());
        // 折叠态文字与底色必须有足够对比。
        auto *current = find("featureActionChoiceCurrentText");
        auto *closedBackground = find("featureActionChoiceBackground");
        QVERIFY(current && closedBackground);
        const auto closedContrast = contrastRatio(current->property("color").value<QColor>(), closedBackground->property("color").value<QColor>());
        QVERIFY2(closedContrast >= 4.5, qPrintable(QString("closed combo contrast %1").arg(closedContrast)));
        // 展开弹层：逐项检查文字、选中与悬停底色。
        auto *combo = find("featureActionChoice");
        QVERIFY(combo);
        auto *comboItem = qobject_cast<QQuickItem *>(combo);
        QVERIFY(comboItem);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          comboItem->mapToScene(QPointF(comboItem->width() / 2, comboItem->height() / 2)).toPoint());
        auto *popup = find("featureActionChoicePopup");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        auto *popupBackground = find("featureActionChoicePopupBackground");
        QVERIFY(popupBackground);
        QTest::qWait(200);
        const int currentIndex = combo->property("currentIndex").toInt();
        for (int offset = 0; offset < 3; ++offset) {
            const int index = currentIndex + offset;
            auto *text = find(QStringLiteral("featureActionChoiceOptionText%1").arg(index));
            auto *background = find(QStringLiteral("featureActionChoiceOptionBackground%1").arg(index));
            QVERIFY2(text && background, qPrintable(QStringLiteral("option %1 must be created").arg(index)));
            const auto ratio = contrastRatio(text->property("color").value<QColor>(), background->property("color").value<QColor>());
            QVERIFY2(ratio >= 4.5, qPrintable(QStringLiteral("option %1 contrast %2").arg(index).arg(ratio)));
            const auto popupRatio = contrastRatio(text->property("color").value<QColor>(), popupBackground->property("color").value<QColor>());
            QVERIFY2(popupRatio >= 4.5, qPrintable(QStringLiteral("option %1 popup contrast %2").arg(index).arg(popupRatio)));
        }
        saveShot("dropdown-popup");
        // 登录提示区把设备码单独显示出来。
        panel->setProperty("userCode", "TESTCODE");
        panel->setProperty("verificationUri", "https://www.microsoft.com/link");
        panel->setProperty("resultUrl", "https://www.microsoft.com/link");
        QTRY_VERIFY(find("deviceCodeBox")->property("visible").toBool());
        QCOMPARE(find("deviceCodeValue")->property("text").toString(), QString("TESTCODE"));
        saveShot("accounts-device-code");
        panel->setProperty("userCode", "");
        panel->setProperty("verificationUri", "");
        panel->setProperty("resultUrl", "");
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::qWait(100);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("visible").toBool());
    }
    void settingsAreConnected() {
        QVERIFY(click("nav3"));
        QTest::qWait(100);
        // 下载源：官方 / BMCLAPI 镜像切换，并在下载器界面标明来源。
        QCOMPARE(features->appearance().value("downloadSource").toString(), QString("official"));
        QVERIFY(click("downloadSourceBmclapi"));
        QTRY_COMPARE(features->appearance().value("downloadSource").toString(), QString("bmclapi"));
        QTRY_COMPARE(features->downloadSource(), QString("bmclapi"));
        QTRY_VERIFY(find("downloadSourceInfo")->property("text").toString().contains(QStringLiteral("BMCLAPI")));
        QTest::qWait(450);
        saveShot("settings-bmclapi");
        QVERIFY(click("effectsToggle"));
        QVERIFY(!controller->effectsEnabled());
        QVERIFY(click("animationsToggle"));
        QVERIFY(!controller->animationsEnabled());
        auto *slider = qobject_cast<QQuickItem *>(find("memorySlider"));
        QVERIFY(slider);
        slider->forceActiveFocus();
        const int memoryBeforeSlider = controller->memoryGb();
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(controller->memoryGb(), qMin(memoryBeforeSlider + 1, controller->maxMemoryGb()));
        QVERIFY(click("smartMemoryGlobal"));
        QTRY_COMPARE(controller->memoryGb(), controller->suggestedMemoryGb());
        QVERIFY(click("nav2"));
        QTest::qWait(120);
        QVERIFY(find("activeDownloadSource") != nullptr);
        QVERIFY(find("activeDownloadSource")->property("text").toString().contains(QStringLiteral("BMCLAPI")));
        QVERIFY(click("openGameDownloads"));
        auto *dialog = find("installDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *sourceTag = find("installDownloadSource");
        QVERIFY(sourceTag);
        QVERIFY(sourceTag->property("visible").toBool());
        QVERIFY(sourceTag->property("text").toString().contains(QStringLiteral("BMCLAPI")));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(click("nav3"));
        QTest::qWait(120);
        QVERIFY(click("downloadSourceOfficial"));
        QTRY_COMPARE(features->appearance().value("downloadSource").toString(), QString("official"));
        QTRY_COMPARE(features->downloadSource(), QString("official"));
        QVERIFY(find("installDownloadSource") == nullptr || !find("installDownloadSource")->property("visible").toBool());
    }
    void communityDialogDownloadsResource() {
        QVariantMap args;
        args["name"] = "资源实例"; args["version"] = "1.21.1"; args["loader"] = "fabric";
        const int before = features->instances().size();
        QVERIFY(QMetaObject::invokeMethod(features.get(), "execute", Q_ARG(QString, "instances"),
                                          Q_ARG(QString, "instance.create"), Q_ARG(QVariantMap, args)));
        QTRY_COMPARE(features->instances().size(), before + 1);
        const auto resourceInstance = instanceNamed("资源实例");
        QVERIFY(!resourceInstance.isEmpty());
        QVERIFY(click("nav2"));
        QTest::qWait(100);
        QVERIFY(click("openCommunityDownloads"));
        auto *dialog = find("communityDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *field = find("communitySearchField");
        QVERIFY(field);
        field->setProperty("text", "demo");
        QVERIFY(click("communitySearchButton"));
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("searchResults").toList().size() > 0, 10000);
        QCOMPARE(communityServer.searchRequests, 1);
        saveShot("community-search");
        QVERIFY(click("communityResult0"));
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("versionResults").toList().size() > 0, 10000);
        QVERIFY(click("communityVersion0"));
        QTRY_VERIFY(!dialog->property("selectedVersionId").toString().isEmpty());
        QVERIFY(click("communityDownloadButton"));
        const auto installMessage = [dialog] { return dialog->property("status").toString(); };
        const auto installDiagnostics = [&] {
            return "status=" + installMessage() + " gameVersion=" + dialog->property("gameVersion").toString()
                   + " loader=" + dialog->property("loader").toString()
                   + " selectedVersionId=" + dialog->property("selectedVersionId").toString()
                   + " instances=" + QString::number(features->instances().size())
                   + " chosen=" + dialog->property("chosenInstanceId").toString();
        };
        QVERIFY2(waitUntil([&] { return installMessage().contains(QStringLiteral("安装")); }, 20000),
                 qPrintable("community install " + installDiagnostics()));
        saveShot("community-installed");
        const auto modsPath = resourceInstance.value("path").toString() + "/mods/" + communityServer.fileName;
        QVERIFY2(QFile::exists(modsPath), qPrintable(modsPath));
        QFile mod(modsPath);
        QVERIFY(mod.open(QIODevice::ReadOnly));
        QCOMPARE(mod.readAll(), communityServer.modFile);
        QVERIFY(!QFile::exists(modsPath + ".part"));
        bool taskVisible = false;
        for (const auto &task : features->downloadTasks())
            if (task.toMap().value("module").toString() == "community" && task.toMap().value("phase").toString() == "complete") taskVisible = true;
        QVERIFY(taskVisible);
        QTRY_VERIFY(find("communityDownloadStatus") != nullptr);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
    void installDialogDownloadsVersion() {
        QVERIFY(click("nav2"));
        QTest::qWait(100);
        QVERIFY(click("openGameDownloads"));
        auto *dialog = find("installDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("versions").toList().size() > 0, 10000);
        QTest::qWait(200);
        saveShot("install-dialog");
        auto *nameField = find("installInstanceName");
        QVERIFY(nameField);
        nameField->setProperty("text", "安装验证实例");
        QVERIFY(click("confirmGameInstall"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!instanceNamed("安装验证实例").isEmpty(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(instanceNamed("安装验证实例").value("installed").toBool(), 30000);
        saveShot("downloads-progress");
        QCOMPARE(window->property("currentPage").toInt(), 2);
        const auto jar = dir.filePath("data/minecraft/versions/1.21.1/1.21.1.jar");
        QVERIFY(QFile::exists(jar));
        QFile client(jar);
        QVERIFY(client.open(QIODevice::ReadOnly));
        QCOMPARE(client.readAll(), gameServer.jar);
        QTRY_VERIFY_WITH_TIMEOUT(find("downloadStatus") != nullptr, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(find("downloadStatus")->property("text").toString().contains(QStringLiteral("安装完成")), 5000);
    }
    void downloadFailureCanBeRetriedFromUi() {
        // 上一个安装测试已写入相同的客户端文件；先移除，确保本次真的走到下载与失败分支。
        QFile::remove(dir.filePath("data/minecraft/versions/1.21.1/1.21.1.jar"));
        gameServer.rejectClient = true;
        QVERIFY(click("nav2"));
        QTest::qWait(100);
        QVERIFY(click("openGameDownloads"));
        auto *dialog = find("installDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("versions").toList().size() > 0, 10000);
        auto *nameField = find("installInstanceName");
        QVERIFY(nameField);
        nameField->setProperty("text", "重试验证实例");
        QVERIFY(click("confirmGameInstall"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(find("downloadStatus") != nullptr, 10000);
        const auto failureText = [this] { return find("downloadStatus")->property("text").toString(); };
        QVERIFY2(waitUntil([&] { return failureText().contains(QStringLiteral("下载失败")); }, 20000),
                 qPrintable("download failure status: " + failureText()));
        saveShot("download-error");
        gameServer.rejectClient = false;
        QVERIFY(click("retryDownload"));
        QVERIFY(!instanceNamed("重试验证实例").isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(instanceNamed("重试验证实例").value("installed").toBool(), 30000);
        const auto jar = dir.filePath("data/minecraft/versions/1.21.1/1.21.1.jar");
        QVERIFY(QFile::exists(jar));
    }
    void nativePanelCreatesAnInstance() {
        const int before = features->instances().size();
        QVERIFY(QMetaObject::invokeMethod(window, "openFeatures", Q_ARG(QVariant, "instances"), Q_ARG(QVariant, QVariantMap{})));
        auto *panel = find("featurePanel");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        auto *choice = find("featureActionChoice");
        QVERIFY(choice);
        choice->setProperty("currentIndex", 1);
        QTest::qWait(100);
        auto *name = qobject_cast<QQuickItem *>(find("featureField_name"));
        QVERIFY(name);
        name->forceActiveFocus();
        for (QChar c : QStringLiteral("nativetest"))
            QTest::keyClick(window, Qt::Key(c.toUpper().unicode()), c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier);
        QVERIFY(click("executeFeatureAction"));
        QTRY_COMPARE(features->instances().size(), before + 1);
        QVERIFY(!instanceNamed("nativetest").isEmpty());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("visible").toBool());
    }
    void instanceSettingsConfigureSmartMemory() {
        const int before = features->instances().size();
        features->execute("instances", "instance.create", {{"name", "实例设置测试"}, {"version", "1.21.1"}, {"loader", "vanilla"}});
        QTRY_COMPARE(features->instances().size(), before + 1);
        QString instanceId;
        for (const auto &value : features->instances()) if (value.toMap().value("name").toString() == QString("实例设置测试")) instanceId = value.toMap().value("id").toString();
        QVERIFY(!instanceId.isEmpty());
        features->execute("instances", "instance.create", {{"name", "另一个实例"}, {"version", "1.20.1"}, {"loader", "fabric"}});
        QTRY_COMPARE(features->instances().size(), before + 2);
        controller->setNativeInstances(features->instances());
        QTest::qWait(150);
        QVERIFY(click("nav0"));
        QTest::qWait(150);
        saveShot("home-instance-memory");
        QVERIFY(click("nav1"));
        QTest::qWait(200);
        QObject *settingsButton = nullptr;
        for (int i = 0; i < controller->instances().size(); ++i) {
            auto *card = find(QString("instance%1").arg(i));
            if (!card || !card->property("visible").toBool()) continue;
            if (card->property("instance").toMap().value("id").toString() != instanceId) continue;
            settingsButton = find(QString("instance%1Settings").arg(i));
            break;
        }
        QVERIFY(settingsButton);
        auto *buttonItem = qobject_cast<QQuickItem *>(settingsButton);
        QVERIFY(buttonItem);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, buttonItem->mapToScene(QPointF(buttonItem->width() / 2, buttonItem->height() / 2)).toPoint());
        auto *dialog = find("instanceSettingsDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTest::qWait(250);
        saveShot("instance-settings");
        auto *slider = find("instanceMemorySlider");
        QVERIFY(slider);
        QCOMPARE(slider->property("value").toInt(), MemoryAdvisor::suggestedGb());
        slider->setProperty("value", 2);
        QTest::qWait(80);
        QCOMPARE(find("instanceMemoryValue")->property("text").toString(), QString("2 GB"));
        QVERIFY(click("instanceSmartMemory"));
        QTest::qWait(100);
        QCOMPARE(slider->property("value").toInt(), MemoryAdvisor::suggestedGb());
        auto *javaField = qobject_cast<QQuickItem *>(find("instanceJavaPath"));
        QVERIFY(javaField);
        javaField->forceActiveFocus();
        QTest::qWait(80);
        for (const QChar character : QString("/custom/java")) QTest::keyClick(window, character.toLatin1());
        QTest::qWait(80);
        QCOMPARE(javaField->property("text").toString(), QString("/custom/java"));
        QVERIFY(click("saveInstanceSettings"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVariantMap saved;
        for (const auto &value : features->instances()) if (value.toMap().value("id").toString() == instanceId) saved = value.toMap();
        QVERIFY(!saved.isEmpty());
        QCOMPARE(saved.value("memoryGb").toInt(), MemoryAdvisor::suggestedGb());
        QCOMPARE(saved.value("javaPath").toString(), QString("/custom/java"));
        QCOMPARE(saved.value("version").toString(), QString("1.21.1"));
        QCOMPARE(saved.value("loader").toString(), QString("vanilla"));
    }
    void launchCheckShowsJavaAndFiles() {
        const int before = features->instances().size();
        features->execute("instances", "instance.create", {{"name", "启动检查测试"}, {"version", "launchf"}, {"loader", "vanilla"}});
        QTRY_COMPARE(features->instances().size(), before + 1);
        QString id;
        for (const auto &value : features->instances()) if (value.toMap().value("name").toString() == QString("启动检查测试")) id = value.toMap().value("id").toString();
        QVERIFY(!id.isEmpty());
        const QString root = features->dataDirectory();
        const QString instancePath = root + "/instances/" + id;
        const QByteArray jar = gameServer.jar;
        const auto jarPath = root + "/minecraft/versions/launchf/launchf.jar";
        QVERIFY(QDir().mkpath(QFileInfo(jarPath).absolutePath()));
        QFile jarFile(jarPath); QVERIFY(jarFile.open(QIODevice::WriteOnly)); jarFile.write(jar); jarFile.close();
        QVERIFY(QFile::remove(jarPath)); // 让启动检查发现缺失，再由「一键修复」真实下载回来
        const auto javaPath = root + "/fake-java";
        QFile java(javaPath); QVERIFY(java.open(QIODevice::WriteOnly));
        java.write("#!/bin/sh\ncat >&2 <<'EOF'\n    java.version = 17.0.9\n    java.vendor = TestVendor\n    os.arch = aarch64\n    sun.arch.data.model = 64\nEOF\n");
        java.close(); QFile::setPermissions(javaPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        auto metadata = ServiceUtils::read(instancePath + "/instance.json");
        metadata["installed"] = true; metadata["javaPath"] = javaPath; metadata["memoryGb"] = 2; metadata["profileId"] = "launchf";
        ServiceUtils::write(instancePath + "/instance.json", metadata);
        const QJsonObject client{{"url", gameServer.url("/client.jar").toString()}, {"sha1", QString::fromLatin1(QCryptographicHash::hash(jar, QCryptographicHash::Sha1).toHex())}, {"size", jar.size()}};
        const QJsonObject profile{{"id", "launchf"}, {"mainClass", "test.Main"}, {"javaVersion", QJsonObject{{"majorVersion", 17}}}, {"downloads", QJsonObject{{"client", client}}}};
        ServiceUtils::write(root + "/minecraft/versions/launchf/launchf.json", profile.toVariantMap());
        controller->setNativeInstances(features->instances());
        int index = -1;
        for (int i = 0; i < controller->instances().size(); ++i) if (controller->instances().at(i).toMap().value("id").toString() == id) index = i;
        QVERIFY(index >= 0);
        controller->setSelectedIndex(index);
        QVERIFY(click("nav0"));
        QTest::qWait(150);
        QVERIFY(click("launchButton"));
        auto *dialog = find("launchCheckDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTRY_VERIFY(!find("launchCheckSummary")->property("text").toString().isEmpty()
                    && !find("launchCheckSummary")->property("text").toString().contains(QStringLiteral("正在检查")));
        QVERIFY(find("launchCheckJavaTag")->property("text").toString().contains(QStringLiteral("可用")));
        QVERIFY(find("launchCheckFilesTag")->property("text").toString().contains(QStringLiteral("需修复")));
        QVERIFY(!find("launchCheckStart")->property("enabled").toBool());
        QVERIFY(find("launchCheckRepair")->property("enabled").toBool());
        saveShot("launch-check");
        QVERIFY(click("launchCheckRepair"));
        QTRY_VERIFY_WITH_TIMEOUT(find("launchCheckFilesTag")->property("text").toString().contains(QStringLiteral("通过")), 20000);
        QVERIFY(QFile::exists(jarPath));
        QTRY_VERIFY(find("launchCheckStart")->property("enabled").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
    void settingsRemainReachableInSmallWindow() {
        // 最小窗口下设置页可滚动到“下载源”，不会因为新增面板被裁掉。
        window->resize(1060, 720);
        QTest::qWait(300);
        QVERIFY(click("nav3"));
        QTest::qWait(200);
        auto *flickable = find("settingsFlickable");
        QVERIFY(flickable);
        const auto before = flickable->property("contentY").toReal();
        flickable->setProperty("contentY", 400);
        QTest::qWait(250);
        QVERIFY(flickable->property("contentY").toReal() > before);
        QVERIFY(click("downloadSourceBmclapi"));
        QTRY_COMPARE(features->appearance().value("downloadSource").toString(), QString("bmclapi"));
        QTRY_COMPARE(features->downloadSource(), QString("bmclapi"));
        QVERIFY(click("downloadSourceOfficial"));
        QTRY_COMPARE(features->appearance().value("downloadSource").toString(), QString("official"));
        QTRY_COMPARE(features->downloadSource(), QString("official"));
        flickable->setProperty("contentY", 0);
        QTest::qWait(150);
        window->resize(1320, 860);
        QTest::qWait(300);
    }
    void dropdownKeyboardNavigationInSmallWindow() {
        window->resize(1060, 720);
        QTest::qWait(300);
        QVERIFY(click("accountButton"));
        auto *panel = find("featurePanel");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("visible").toBool());
        auto *comboObject = find("featureActionChoice");
        auto *combo = qobject_cast<QQuickItem *>(comboObject);
        QVERIFY(combo);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          combo->mapToScene(QPointF(combo->width() / 2, combo->height() / 2)).toPoint());
        auto *popup = find("featureActionChoicePopup");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        const int count = comboObject->property("count").toInt();
        QVERIFY(count > 8);
        // 小窗口里弹层保持可键盘操作：End 跳到最后一个操作，回车选中并收起。
        QTest::keyClick(window, Qt::Key_End);
        QTRY_COMPARE(comboObject->property("highlightedIndex").toInt(), count - 1);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTRY_COMPARE(comboObject->property("currentIndex").toInt(), count - 1);
        QCOMPARE(panel->property("activeAction").toMap().value("id").toString(), QString("cape.select"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("visible").toBool());
        window->resize(1320, 860);
        QTest::qWait(300);
    }
    void noQmlWarnings() {
        for (const auto &warning : warnings) qWarning().noquote() << warning.toString();
        QVERIFY(warnings.isEmpty());
    }
};
QTEST_MAIN(UiTest)
#include "tst_ui.moc"
