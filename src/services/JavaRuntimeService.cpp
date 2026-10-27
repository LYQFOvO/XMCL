#include "JavaRuntimeService.h"
#include "InstanceService.h"
#include "ServiceUtils.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
using namespace ServiceUtils;
namespace {
void addCandidate(QStringList &out,const QString &path) {
    if (path.isEmpty() || out.size() >= 40) return;
    QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isExecutable()) return;
    const auto canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || out.contains(canonical)) return;
    out << canonical;
}
void addVariants(QStringList &out,const QStringList &roots,const QStringList &relatives) {
    for (const auto &root : roots) for (const auto &relative : relatives) addCandidate(out, root + '/' + relative);
}
void addChildVariants(QStringList &out,const QStringList &roots,const QString &childFilter,const QString &relative) {
    for (const auto &root : roots) {
        const QDir dir(root);
        if (!dir.exists()) continue;
        for (const auto &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,QDir::Name)) {
            if (!childFilter.isEmpty() && !name.startsWith(childFilter,Qt::CaseInsensitive)) continue;
            addCandidate(out,dir.absoluteFilePath(name + '/' + relative));
        }
    }
}
QString javaProperty(const QString &text,const QString &key) {
    const QRegularExpression pattern("(?m)^\\s*" + QRegularExpression::escape(key) + "\\s*=\\s*(\\S+)");
    const auto match = pattern.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}
}

JavaRuntimeService::JavaRuntimeService(QString root,QObject *parent)
    : FeatureService(std::move(root),parent), m_cachePath(m_root + "/java-runtimes.json") {}

QVariantList JavaRuntimeService::actions() const {
    return {
        action("java.scan","扫描本机 Java",{field("refresh","强制重新探测","bool",false)},"扫描 JAVA_HOME、PATH、系统与常见安装目录中的 Java，探测版本和架构并缓存结果。"),
        action("java.match","匹配实例 Java",{field("instanceId","实例 ID","text"),field("requiredMajor","所需主版本（可留空）","number",0)},"优先精确主版本；没有精确版本时给出较高版本回退建议，不宣称普遍向上兼容。"),
        action("java.health","检查 Java 健康",{field("path","Java 路径","path",{},true)},"重新探测指定 Java 的版本、厂商、架构与可执行性。")
    };
}
void JavaRuntimeService::loadCache() {
    if (m_loaded) return;
    m_loaded = true;
    QFile file(m_cachePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject()) m_cache = doc.object().value("runtimes").toArray().toVariantList();
}
void JavaRuntimeService::saveCache() const {
    QJsonArray array;
    for (const auto &item : m_cache) array.append(QJsonObject::fromVariantMap(item.toMap()));
    QSaveFile file(m_cachePath);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(QJsonObject{{"schema",1},{"runtimes",array}}).toJson(QJsonDocument::Compact));
    file.commit();
}
QVariantMap JavaRuntimeService::cachedFor(const QVariantList &cache,const QString &path) {
    for (const auto &item : cache) {
        const auto map = item.toMap();
        if (map.value("path").toString() == path) return map;
    }
    return {};
}
bool JavaRuntimeService::sameFingerprint(const QVariantMap &entry) {
    const QFileInfo info(entry.value("path").toString());
    return info.exists() && info.isFile() && info.size() == entry.value("size").toLongLong()
        && info.lastModified().toMSecsSinceEpoch() == entry.value("mtime").toLongLong();
}
QStringList JavaRuntimeService::discoverCandidates() {
    QStringList out;
    const auto home = qEnvironmentVariable("JAVA_HOME");
    if (!home.isEmpty()) addCandidate(out, home + "/bin/java");
    addCandidate(out, QStandardPaths::findExecutable("java"));
    const QStringList homeDirs{QDir::homePath()};
#ifdef Q_OS_MACOS
    addChildVariants(out,{"/Library/Java/JavaVirtualMachines",QDir::homePath() + "/Library/Java/JavaVirtualMachines"},QString(),"Contents/Home/bin/java");
    for (const auto &root : QStringList{"/opt/homebrew/opt","/usr/local/opt"}) {
        const QDir dir(root);
        for (const auto &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!name.startsWith("openjdk",Qt::CaseInsensitive) && !name.startsWith("java",Qt::CaseInsensitive)) continue;
            addCandidate(out,dir.absoluteFilePath(name + "/bin/java"));
        }
    }
#endif
#ifdef Q_OS_LINUX
    addChildVariants(out,{"/usr/lib/jvm","/usr/java"},"","bin/java");
#endif
#ifdef Q_OS_WIN
    const QString programFiles = qEnvironmentVariable("ProgramFiles"), localAppData = qEnvironmentVariable("LOCALAPPDATA");
    addChildVariants(out,{programFiles + "/Java",programFiles + "/Eclipse Adoptium",programFiles + "/Microsoft",localAppData + "/Programs/Eclipse Adoptium"},"","bin/java.exe");
    for (const auto &vendor : {"JavaSoft/JRE","JavaSoft/JDK"}) {
        QSettings registry(QString::fromLatin1("HKEY_LOCAL_MACHINE\\SOFTWARE\\") + QString::fromLatin1(vendor),QSettings::NativeFormat);
        for (const auto &key : registry.childGroups()) {
            const auto javaHome = registry.value(key + "/JavaHome").toString();
            if (!javaHome.isEmpty()) addCandidate(out,javaHome + "/bin/java.exe");
        }
    }
#endif
    for (const auto &sdkm : homeDirs) {
        addChildVariants(out,{sdkm + "/.sdkman/candidates/java"}, "","bin/java");
        addChildVariants(out,{sdkm + "/.jdks",sdkm + "/.gradle/jdks"},"","bin/java");
    }
    return out;
}
QVariantMap JavaRuntimeService::probe(const QString &path) {
    QVariantMap result{{"path",path},{"id",path},{"title",path},{"subtitle",path},{"javaPath",path},
                       {"version",""},{"major",0},{"vendor",""},{"arch",""},{"bits",0},
                       {"valid",false},{"problem",""},{"probed",false}};
    const QFileInfo info(path);
    result["size"] = info.exists() ? info.size() : 0;
    result["mtime"] = info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
    if (!info.exists() || !info.isFile() || !info.isExecutable()) { result["problem"] = "文件不存在或不可执行"; return result; }
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(path,{"-XshowSettings:properties","-version"});
    if (!process.waitForStarted(1500)) { result["problem"] = "无法启动 Java：" + process.errorString(); return result; }
    if (!process.waitForFinished(4000)) { process.kill(); process.waitForFinished(500); result["problem"] = "Java 探测超时"; return result; }
    const auto text = QString::fromUtf8(process.readAll());
    QString version = javaProperty(text,"java.version");
    if (version.isEmpty()) {
        const auto legacy = QRegularExpression("version\\s+\"([^\"]+)\"").match(text);
        if (legacy.hasMatch()) version = legacy.captured(1);
    }
    const int major = parseMajor(version);
    result["version"] = version;
    result["major"] = major;
    result["vendor"] = javaProperty(text,"java.vendor");
    result["arch"] = javaProperty(text,"os.arch");
    int bits = javaProperty(text,"sun.arch.data.model").toInt();
    if (bits <= 0 && result.value("arch").toString().contains("64")) bits = 64;
    if (bits <= 0) bits = 32;
    result["bits"] = bits;
    result["valid"] = major > 0;
    result["probed"] = true;
    result["title"] = major > 0 ? QString("Java %1%2").arg(major).arg(bits >= 64 ? " · 64 位" : " · 32 位") : "不可用的 Java";
    result["subtitle"] = path;
    if (major <= 0) result["problem"] = process.exitStatus() == QProcess::NormalExit ? "无法解析 Java 版本输出" : "Java 进程异常退出";
    return result;
}
int JavaRuntimeService::parseMajor(const QString &version) {
    const auto match = QRegularExpression("^(\\d+)(?:\\.(\\d+))?").match(version.trimmed());
    if (!match.hasMatch()) return 0;
    const int first = match.captured(1).toInt();
    if (first == 1) return match.capturedLength(2) ? match.captured(2).toInt() : 0;
    return first;
}
QVariantList JavaRuntimeService::runtimes(bool refresh) {
    loadCache();
    QVariantList result;
    const auto candidates = discoverCandidates();
    for (const auto &path : candidates) {
        auto entry = refresh ? QVariantMap{} : cachedFor(m_cache,path);
        if (entry.isEmpty() || !sameFingerprint(entry)) entry = probe(path);
        result << entry;
    }
    m_cache = result;
    saveCache();
    return result;
}
QVariantMap JavaRuntimeService::match(int requiredMajor) {
    const auto items = runtimes();
    return matchRuntimes(items,requiredMajor);
}
QVariantMap JavaRuntimeService::matchRuntimes(const QVariantList &items,int requiredMajor) {
    QVariantList valid, exact, higher;
    for (const auto &item : items) {
        const auto runtime = item.toMap();
        if (!runtime.value("valid").toBool()) continue;
        valid << runtime;
        if (requiredMajor > 0 && runtime.value("major").toInt() == requiredMajor) exact << runtime;
        if (requiredMajor > 0 && runtime.value("major").toInt() > requiredMajor) higher << runtime;
    }
    auto rank = [](const QVariant &left,const QVariant &right) {
        const auto a = left.toMap(), b = right.toMap();
        if (a.value("bits").toInt() != b.value("bits").toInt()) return a.value("bits").toInt() > b.value("bits").toInt();
        return a.value("major").toInt() > b.value("major").toInt();
    };
    std::sort(exact.begin(),exact.end(),rank);
    std::sort(higher.begin(),higher.end(),rank);
    QVariantList candidates;
    QVariantMap matched;
    QStringList warnings;
    if (requiredMajor <= 0) {
        warnings << "无法确定该实例需要的 Java 主版本；请先安装游戏或手动指定 Java。";
    } else if (!exact.isEmpty()) {
        QVariantList sameBits;
        for (const auto &item : exact) if (item.toMap().value("bits").toInt() >= 64) sameBits << item;
        candidates = sameBits.isEmpty() ? exact : sameBits;
        matched = candidates.first().toMap();
    } else if (!higher.isEmpty()) {
        QVariantList sameBits;
        for (const auto &item : higher) if (item.toMap().value("bits").toInt() >= 64) sameBits << item;
        candidates = sameBits.isEmpty() ? higher : sameBits;
        matched = candidates.first().toMap();
        warnings << QString("未找到 Java %1，将使用 Java %2；较高版本不保证兼容所有旧版本或模组。")
                        .arg(requiredMajor).arg(matched.value("major").toInt());
    } else {
        warnings << QString("未找到可用的 Java %1，请安装后点击扫描，或在实例设置中手动指定路径。").arg(requiredMajor);
    }
    QVariantList all;
    for (const auto &item : items) { auto runtime = item.toMap(); runtime["matched"] = matched.value("path").toString() == runtime.value("path").toString(); all << runtime; }
    return {{"requiredMajor",requiredMajor},{"matched",matched},{"candidates",candidates},{"items",all},
            {"warnings",warnings},{"available",candidates.size()}};
}
int JavaRuntimeService::requiredMajorForInstance(const QString &root,const QString &instanceId,QString *profileId) {
    try {
        InstanceService instances(root);
        const auto meta = read(instances.instancePath(instanceId) + "/instance.json");
        const auto id = meta.value("profileId",meta.value("version")).toString();
        if (profileId) *profileId = id;
        if (!meta.value("installed").toBool() || id.isEmpty()) return -1;
        QString current = id;
        for (int depth = 0; depth < 8 && !current.isEmpty(); ++depth) {
            const auto path = root + "/minecraft/versions/" + current + '/' + current + ".json";
            if (!QFileInfo::exists(path)) break;
            const auto object = QJsonObject::fromVariantMap(read(path));
            const int major = object.value("javaVersion").toObject().value("majorVersion").toInt();
            if (major > 0) return major;
            current = object.value("inheritsFrom").toString();
        }
    } catch (...) {}
    return -1;
}
QVariantMap JavaRuntimeService::matchForInstance(const QString &instanceId,int requiredMajor) {
    InstanceService instances(m_root);
    const auto meta = read(instances.instancePath(instanceId) + "/instance.json");
    QString profileId;
    const int required = requiredMajor > 0 ? requiredMajor : requiredMajorForInstance(m_root,instanceId,&profileId);
    auto result = match(required);
    const auto manual = meta.value("javaPath").toString();
    QVariantMap manualProbe;
    if (!manual.isEmpty()) {
        manualProbe = probe(manual);
        manualProbe["source"] = "manual";
        manualProbe["matched"] = manualProbe.value("valid").toBool();
        result["manual"] = manualProbe;
        if (manualProbe.value("valid").toBool()) {
            result["matched"] = manualProbe;
            result["usedManual"] = true;
            if (required > 0 && manualProbe.value("major").toInt() != required) {
                auto warnings = result.value("warnings").toList();
                warnings << QString("实例指定的 Java %1 与版本要求的 Java %2 不一致，启动可能失败或表现异常。")
                                .arg(manualProbe.value("major").toInt()).arg(required);
                result["warnings"] = warnings;
            }
        } else {
            auto warnings = result.value("warnings").toList();
            warnings << "实例指定的 Java 无效：" + manualProbe.value("problem").toString();
            result["warnings"] = warnings;
            result["matched"] = QVariantMap{};
            result["manualInvalid"] = true;
        }
        if (manualProbe.value("valid").toBool() && manualProbe.value("bits").toInt() < 64) {
            auto warnings = result.value("warnings").toList();
            warnings << "实例指定的 Java 是 32 位，现代 Minecraft 需要 64 位 Java。";
            result["warnings"] = warnings;
        }
    }
    result["instanceId"] = instanceId;
    result["profileId"] = profileId;
    result["installed"] = meta.value("installed").toBool();
    return result;
}
QString JavaRuntimeService::bestPath(int requiredMajor,QString *problem) {
    const auto result = match(requiredMajor);
    const auto matched = result.value("matched").toMap();
    if (!matched.value("path").toString().isEmpty()) return matched.value("path").toString();
    if (problem) *problem = result.value("warnings").toList().isEmpty()
        ? QStringLiteral("没有可用的 Java %1").arg(requiredMajor)
        : result.value("warnings").toList().first().toString();
    return {};
}
void JavaRuntimeService::execute(const QString &actionId,const QVariantMap &args) {
    try {
        if (actionId == "java.scan") {
            const bool refresh = args.value("refresh").toBool();
            const auto items = runtimes(refresh);
            emit completed(actionId,{{"items",items},{"message",QString("检测到 %1 个 Java 运行时").arg(items.size())},{"cached",!refresh}});
            return;
        }
        if (actionId == "java.match") {
            const auto instanceId = args.value("instanceId").toString();
            const int required = args.value("requiredMajor").toInt();
            auto result = instanceId.isEmpty() ? match(required) : matchForInstance(instanceId,required);
            const auto matched = result.value("matched").toMap();
            result["message"] = matched.isEmpty() ? "没有匹配的 Java"
                : QString("已匹配 Java %1（%2 位）").arg(matched.value("major").toInt()).arg(matched.value("bits").toInt());
            emit completed(actionId,result);
            return;
        }
        if (actionId == "java.health") {
            const auto path = localPath(args.value("path"));
            require(!path.isEmpty(),"请选择 Java 路径");
            const auto probeResult = probe(path);
            emit completed(actionId,{{"runtime",probeResult},{"message",probeResult.value("valid").toBool() ? "Java 可用" : "Java 不可用：" + probeResult.value("problem").toString()}});
            return;
        }
        require(false,"未知 Java 操作");
    } catch (const std::exception &error) { emit failed(actionId,QString::fromUtf8(error.what())); }
}
