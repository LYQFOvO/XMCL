#include "SecretStore.h"
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QSaveFile>
#ifdef Q_OS_MACOS
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincrypt.h>
#endif
namespace {
class PlatformSecrets final : public SecretStore {
    QString m_space;
#ifdef Q_OS_MACOS
    CFMutableDictionaryRef query(const QString &id) const {
        auto q=CFDictionaryCreateMutable(nullptr,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(q,kSecClass,kSecClassGenericPassword);
        CFDictionarySetValue(q,kSecAttrService,CFSTR("com.xmcl.launcher.credentials"));
        const auto name=(m_space+":"+id).toUtf8();auto account=CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8*>(name.constData()),name.size(),kCFStringEncodingUTF8,false);
        CFDictionarySetValue(q,kSecAttrAccount,account);CFRelease(account);return q;
    }
#endif
public:
    explicit PlatformSecrets(const QString &path):m_space(QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(),QCryptographicHash::Sha256).toHex())){}
    bool put(const QString &id,const QByteArray &value,QString *error) override {
#ifdef Q_OS_MACOS
        auto q=query(id);auto data=CFDataCreate(nullptr,reinterpret_cast<const UInt8*>(value.constData()),value.size());
        auto update=CFDictionaryCreateMutable(nullptr,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);CFDictionarySetValue(update,kSecValueData,data);
        OSStatus status=SecItemUpdate(q,update);if(status==errSecItemNotFound){CFDictionarySetValue(q,kSecValueData,data);status=SecItemAdd(q,nullptr);}CFRelease(data);CFRelease(update);CFRelease(q);
        if(status!=errSecSuccess&&error)*error=QString("系统钥匙串保存失败（%1）").arg(status);return status==errSecSuccess;
#else
        Q_UNUSED(id);Q_UNUSED(value);if(error)*error="本平台凭据保险库尚未接入，拒绝明文保存";return false;
#endif
    }
    QByteArray get(const QString &id,QString *error) const override {
#ifdef Q_OS_MACOS
        auto q=query(id);CFDictionarySetValue(q,kSecReturnData,kCFBooleanTrue);CFDictionarySetValue(q,kSecMatchLimit,kSecMatchLimitOne);CFTypeRef result=nullptr;auto status=SecItemCopyMatching(q,&result);CFRelease(q);
        if(status!=errSecSuccess){if(error)*error=QString("无法读取钥匙串凭据（%1）").arg(status);return {};}
        auto data=static_cast<CFDataRef>(result);QByteArray bytes(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),CFDataGetLength(data));CFRelease(result);return bytes;
#else
        Q_UNUSED(id);if(error)*error="本平台凭据保险库尚未接入";return {};
#endif
    }
    bool remove(const QString &id,QString *error) override {
#ifdef Q_OS_MACOS
        auto q=query(id);auto status=SecItemDelete(q);CFRelease(q);if(status!=errSecSuccess&&status!=errSecItemNotFound&&error)*error="无法删除钥匙串条目";return status==errSecSuccess||status==errSecItemNotFound;
#else
        Q_UNUSED(id);Q_UNUSED(error);return true;
#endif
    }
};
}
std::unique_ptr<SecretStore> platformSecretStore(const QString &path){return std::make_unique<PlatformSecrets>(path);}
