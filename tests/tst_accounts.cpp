#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QImage>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include "services/AccountService.h"
#include "services/SecretStore.h"
#include "TestMicrosoftServer.h"
class MemorySecrets : public SecretStore {
public:
    QMap<QString,QByteArray> entries;
    bool put(const QString &id,const QByteArray &v,QString *) override { entries[id]=v; return true; }
    QByteArray get(const QString &id,QString *) const override { return entries.value(id); }
    bool remove(const QString &id,QString *) override { entries.remove(id); return true; }
};
namespace {
AccountService::Endpoints fixtureEndpoints(const TestMicrosoftServer &server) {
    AccountService::Endpoints endpoints;
    endpoints.microsoftAuthority = server.url("");
    endpoints.xboxUserAuth = server.url("/xbox/user/authenticate");
    endpoints.xboxXsts = server.url("/xbox/xsts/authorize");
    endpoints.minecraftLogin = server.url("/minecraft/login");
    endpoints.minecraftEntitlements = server.url("/minecraft/entitlements");
    endpoints.minecraftProfile = server.url("/minecraft/profile");
    return endpoints;
}
}
class AccountsTest : public QObject {
    Q_OBJECT
private slots:
    void defaultMicrosoftIdentityAndUuid() {
        QCOMPARE(AccountService::defaultMicrosoftClientId(), QStringLiteral("06cc8cf6-4b8f-4631-8459-b2bd828808f8"));
        QCOMPARE(AccountService::normalizedMinecraftUuid("0123456789abcdef0123456789abcdef"),
                 QStringLiteral("01234567-89ab-cdef-0123-456789abcdef"));
        QCOMPARE(AccountService::normalizedMinecraftUuid("01234567-89AB-CDEF-0123-456789ABCDEF"),
                 QStringLiteral("01234567-89ab-cdef-0123-456789abcdef"));
        QCOMPARE(AccountService::normalizedMinecraftUuid("not-a-uuid"), QStringLiteral("not-a-uuid"));
    }
    void offlinePersistence() {
        QTemporaryDir dir; AccountService service(dir.path());
        QSignalSpy ok(&service,&FeatureService::completed), errors(&service,&FeatureService::failed);
        service.execute("offline.add",{{"name","Notch"}});
        QCOMPARE(errors.size(),0); QCOMPARE(ok.size(),1);
        const auto identity=service.launchIdentity();
        QCOMPARE(identity.value("name").toString(),QString("Notch"));
        QCOMPARE(identity.value("uuid").toString(),QString("b50ad385829d3141a2167e7d7539ba7f"));
        QCOMPARE(identity.value("type").toString(),QString("offline"));
        AccountService reload(dir.path()); QCOMPARE(reload.launchIdentity(),identity);
        service.execute("offline.add",{{"name","bad name"}}); QCOMPARE(errors.size(),1);
        service.execute("offline.add",{{"name","Notch"}}); QCOMPARE(errors.size(),2);
        const auto id=ok.first().at(1).toMap().value("accountId").toString(); QVERIFY(!id.isEmpty());
        service.execute("account.remove",{{"id",id}}); QVERIFY(service.launchIdentity().contains("error"));
    }
    void wardrobeValidation() {
        QTemporaryDir dir; AccountService service(dir.path());
        QSignalSpy ok(&service,&FeatureService::completed), errors(&service,&FeatureService::failed);
        QImage skin(64,64,QImage::Format_ARGB32); skin.fill(Qt::green);
        auto path=dir.filePath("skin.png"); QVERIFY(skin.save(path));
        if(!qEnvironmentVariable("XMCL_TEST_SKIN").isEmpty()){
            QImage fixture(64,64,QImage::Format_ARGB32);fixture.fill(Qt::transparent);
            for(int y=0;y<32;++y)for(int x=0;x<64;++x)fixture.setPixelColor(x,y,y<16?QColor("#e7bb8e"):x<16?QColor("#52699e"):x<40?QColor("#59b9ac"):QColor("#e7bb8e"));
            for(int y=48;y<64;++y)for(int x=16;x<48;++x)fixture.setPixelColor(x,y,x<32?QColor("#52699e"):QColor("#e7bb8e"));
            QVERIFY(fixture.save(qEnvironmentVariable("XMCL_TEST_SKIN")));
        }
        service.execute("skin.import",{{"path",path},{"variant","slim"}});
        QCOMPARE(errors.size(),0); QCOMPARE(ok.size(),1);
        auto data=ok.last().at(1).toMap(); QVERIFY(QFile::exists(data.value("previewPath").toString()));
        service.execute("skin.list",{}); QCOMPARE(ok.last().at(1).toMap().value("items").toList().size(),1);
        QImage invalid(32,32,QImage::Format_ARGB32); invalid.fill(Qt::red); QVERIFY(invalid.save(path));
        service.execute("skin.import",{{"path",path}}); QCOMPARE(errors.size(),1);
        service.execute("skin.import",{{"path",dir.filePath("missing.png")}}); QCOMPARE(errors.size(),2);
        AccountService reload(dir.path()); QSignalSpy reopened(&reload,&FeatureService::completed);
        reload.execute("skin.list",{}); QCOMPARE(reopened.last().at(1).toMap().value("items").toList().size(),1);
    }
    void unsafeProviderAndInvalidClientId() {
        QTemporaryDir dir; AccountService service(dir.path()); QSignalSpy errors(&service,&FeatureService::failed);
        service.execute("microsoft.login",{{"clientId","not-a-client-id"}}); QCOMPARE(errors.size(),1);
        QVERIFY(errors.last().at(1).toString().contains(QStringLiteral("Client ID")));
        service.execute("provider.add",{{"name","Unsafe"},{"url","http://example.org"}}); QCOMPARE(errors.size(),2);
        service.execute("provider.add",{{"name","Credentials"},{"url","https://u:p@example.org"}}); QCOMPARE(errors.size(),3);
    }
    void microsoftDeviceCodeLoginWithoutUserClientId() {
        QTemporaryDir dir; TestMicrosoftServer server;
        auto secrets=std::make_unique<MemorySecrets>();
        auto *secretsView=secrets.get();
        AccountService service(dir.path(),nullptr,fixtureEndpoints(server),std::move(secrets));
        QSignalSpy ok(&service,&FeatureService::completed),errors(&service,&FeatureService::failed);
        service.execute("microsoft.login",{});
        QTRY_VERIFY_WITH_TIMEOUT(ok.size()>=1 || errors.size()>0, 5000);
        QCOMPARE(errors.size(),0);
        const auto prompt=ok.last().at(1).toMap();
        QCOMPARE(prompt.value("userCode").toString(),QString("TESTCODE"));
        QCOMPARE(prompt.value("url").toString(),QString("https://www.microsoft.com/link"));
        QVERIFY(prompt.value("details").toString().contains(QStringLiteral("TESTCODE")));
        QTRY_VERIFY_WITH_TIMEOUT(ok.size()>=2 || errors.size()>0, 15000);
        QCOMPARE(errors.size(),0);
        const auto saved=ok.last().at(1).toMap();
        const auto accountId=saved.value("accountId").toString();
        QVERIFY(!accountId.isEmpty());
        QCOMPARE(server.deviceCodeRequests,1);
        QTRY_VERIFY(server.tokenRequests>=2);
        const auto identity=service.launchIdentity();
        QCOMPARE(identity.value("name").toString(),QString("TestPlayer"));
        QCOMPARE(identity.value("uuid").toString(),QString("01234567-89ab-cdef-0123-456789abcdef"));
        QCOMPARE(identity.value("type").toString(),QString("microsoft"));
        QCOMPARE(identity.value("accessToken").toString(),QString("minecraft-access-token"));
        QVERIFY(secretsView->entries.contains(accountId));
        const auto credentials=QJsonDocument::fromJson(secretsView->entries.value(accountId)).object();
        QCOMPARE(credentials.value("refreshToken").toString(),QString("ms-refresh-token"));
        // 凭据不能写进通用账号清单 JSON。
        QFile accounts(dir.filePath("accounts.json")); QVERIFY(accounts.open(QIODevice::ReadOnly));
        const auto raw=accounts.readAll();
        QVERIFY(!raw.contains("ms-access-token"));
        QVERIFY(!raw.contains("minecraft-access-token"));
        QVERIFY(!raw.contains("ms-refresh-token"));
        QCOMPARE(server.paths.filter(QRegularExpression("xsts")).size(),1);
        QCOMPARE(server.paths.filter(QRegularExpression("entitlements")).size(),1);
        // 长期授权刷新：使用保存的 refresh token 走完整链并更新凭据。
        const int refreshesBefore=ok.size();
        service.execute("account.refresh",{});
        QTRY_VERIFY_WITH_TIMEOUT(ok.size()>refreshesBefore||errors.size()>0,15000);
        QCOMPARE(errors.size(),0);
        QCOMPARE(service.launchIdentity().value("accessToken").toString(),QString("minecraft-access-token"));
    }
    void liveMicrosoftDeviceCodeStartsWithBuiltInClientId() {
        if (qEnvironmentVariableIsEmpty("XMCL_LIVE")) QSKIP("live Microsoft probe disabled; set XMCL_LIVE=1");
        QTemporaryDir dir; AccountService service(dir.path());
        QSignalSpy ok(&service,&FeatureService::completed),errors(&service,&FeatureService::failed);
        service.execute("microsoft.login",{});
        QTRY_VERIFY_WITH_TIMEOUT(ok.size()>0||errors.size()>0,20000);
        QCOMPARE(errors.size(),0);
        const auto prompt=ok.first().at(1).toMap();
        QVERIFY(!prompt.value("userCode").toString().isEmpty());
        QVERIFY(prompt.value("url").toString().startsWith("https://"));
        service.execute("microsoft.cancel",{});
    }
    void workSchoolAccountGetsClearMessage() {
        QTemporaryDir dir; TestMicrosoftServer server;
        server.deviceCodeError = QJsonObject{{"error","invalid_grant"},
            {"error_description","AADSTS50020: User account 'user@contoso.onmicrosoft.com' from identity provider 'https://sts.windows.net/00000000-0000-0000-0000-000000000000/' does not exist in tenant 'Microsoft Accounts' and cannot access the application in that tenant."}};
        auto secrets=std::make_unique<MemorySecrets>();
        AccountService service(dir.path(),nullptr,fixtureEndpoints(server),std::move(secrets));
        QSignalSpy errors(&service,&FeatureService::failed);
        service.execute("microsoft.login",{});
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(),5000);
        const auto message=errors.first().at(1).toString();
        QVERIFY2(message.contains(QStringLiteral("个人 Microsoft 账户")),qPrintable(message));
        QVERIFY(message.contains(QStringLiteral("onmicrosoft.com")));
        QVERIFY(service.launchIdentity().contains("error"));
    }
    void microsoftLoginCanBeCancelled() {
        QTemporaryDir dir; TestMicrosoftServer server; server.pendingPolls=1000;
        auto secrets=std::make_unique<MemorySecrets>();
        AccountService service(dir.path(),nullptr,fixtureEndpoints(server),std::move(secrets));
        QSignalSpy ok(&service,&FeatureService::completed),errors(&service,&FeatureService::failed);
        service.execute("microsoft.login",{});
        QTRY_COMPARE(ok.size(),1);
        service.execute("microsoft.cancel",{});
        QVERIFY(ok.last().at(1).toMap().value("message").toString().contains(QStringLiteral("取消")));
        const auto tokenRequestsAtCancel=server.tokenRequests;
        QTest::qWait(1500);
        QCOMPARE(errors.size(),0);
        QVERIFY(server.tokenRequests<=tokenRequestsAtCancel+1);
        QVERIFY(service.launchIdentity().contains("error"));
    }
};
QTEST_GUILESS_MAIN(AccountsTest)
#include "tst_accounts.moc"
