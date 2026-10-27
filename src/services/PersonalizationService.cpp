#include "PersonalizationService.h"
#include "ServiceUtils.h"
#include "DownloadSource.h"
#include <QBuffer>
#include <QColor>
#include <QCryptographicHash>
#include <QImageReader>
#include <QKeySequence>
#include <QRegularExpression>
using namespace ServiceUtils;
namespace {
QVariantMap defaultShortcuts() { return {{"home", "Ctrl+1"}, {"library", "Ctrl+2"}, {"downloads", "Ctrl+3"}, {"settings", "Ctrl+4"}, {"features", "Ctrl+Shift+M"}}; }
bool validInstance(const QString &id) { return QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_-]{0,127}$").match(id).hasMatch(); }
QString shortcut(const QString &text) {
    const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    require(!sequence.isEmpty() && sequence.count() == 1 && sequence[0].key() != Qt::Key_unknown && sequence[0].key() != 0, "快捷键必须是一个有效按键组合");
    return sequence.toString(QKeySequence::PortableText);
}
}
PersonalizationService::PersonalizationService(QString root, QObject *parent)
    : FeatureService(std::move(root), parent), m_plugins(m_root) {
    try { m_values = validated(read(path("settings.json"), true)); }
    catch (const std::exception &) { m_values = {}; } // Corrupt settings never change the baseline visual.
}
QString PersonalizationService::path(const QString &name) const {
    const QString directory = QDir(m_root).absoluteFilePath("personalization");
    require(!QFileInfo(directory).isSymLink(), "个性化目录不能是符号链接");
    require(!name.contains('/') && !name.contains('\\') && name != "..", "个性化路径无效");
    const QString result = directory + '/' + name;
    require(!QFileInfo(result).isSymLink(), "个性化文件不能是符号链接");
    return result;
}
QVariantMap PersonalizationService::settings() const {
    auto result = m_values;
    result["accentColor"] = m_values.value("accentColor", "");
    result["backgroundUrl"] = m_values.value("backgroundUrl", "");
    auto keys = defaultShortcuts();
    const auto custom = m_values.value("shortcuts").toMap();
    for (auto it = custom.begin(); it != custom.end(); ++it) keys[it.key()] = it.value();
    result["shortcuts"] = keys;
    result["thumbnails"] = m_values.value("thumbnails", QVariantMap{});
    result["downloadSource"] = DownloadSource::normalized(m_values.value("downloadSource", DownloadSource::officialId()).toString());
    return result;
}
QVariantMap PersonalizationService::validated(const QVariantMap &values) const {
    QVariantMap result;
    const QString color = values.value("accentColor").toString();
    require(color.isEmpty() || QRegularExpression("^#[0-9a-fA-F]{6}$").match(color).hasMatch(), "主题色需为 #RRGGBB");
    result["accentColor"] = color.toLower();
    const QString downloadSource = values.value("downloadSource").toString();
    require(downloadSource.isEmpty() || DownloadSource::valid(downloadSource), "未知下载源");
    result["downloadSource"] = DownloadSource::normalized(downloadSource);
    auto keys = defaultShortcuts(); const auto custom = values.value("shortcuts").toMap();
    for (auto it = custom.begin(); it != custom.end(); ++it) { require(keys.contains(it.key()), "未知快捷键目标"); keys[it.key()] = shortcut(it.value().toString()); }
    QStringList seen;
    for (const auto &value : keys) { const auto key = shortcut(value.toString()); require(!seen.contains(key, Qt::CaseInsensitive), "快捷键与其他操作冲突"); seen << key; }
    result["shortcuts"] = keys;
    const auto ownedImage = [this](const QString &url) {
        if (url.isEmpty()) return QString();
        const QUrl parsed(url); require(parsed.isLocalFile(), "图片必须来自本地管理目录");
        const QString file = parsed.toLocalFile(), name = QFileInfo(file).fileName();
        require(file == path(name) && name.endsWith(".png") && QFileInfo(file).isFile(), "个性化图片不存在或不在管理目录");
        return QUrl::fromLocalFile(file).toString();
    };
    result["backgroundUrl"] = ownedImage(values.value("backgroundUrl").toString());
    QVariantMap thumbnails;
    const auto input = values.value("thumbnails").toMap();
    for (auto it = input.begin(); it != input.end(); ++it) { require(validInstance(it.key()), "实例 ID 无效"); thumbnails[it.key()] = ownedImage(it.value().toString()); }
    result["thumbnails"] = thumbnails; return result;
}
QString PersonalizationService::importImage(const QString &source, const QString &prefix) const {
    const QString file = localPath(source);
    require(!file.isEmpty() && QFileInfo(file).isAbsolute() && QFileInfo(file).isFile(), "请选择存在的本地图片绝对路径");
    require(QFileInfo(file).size() <= 20 * 1024 * 1024, "图片超过 20 MiB");
    QImageReader reader(file); reader.setAutoTransform(true);
    const auto size = reader.size();
    require(size.isValid() && size.width() <= 8192 && size.height() <= 8192 && qint64(size.width()) * size.height() <= 16000000, "图片尺寸无效或超过 1600 万像素");
    const auto image = reader.read(); require(!image.isNull(), "无法解码图片：" + reader.errorString());
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    require(image.save(&buffer, "PNG"), "无法编码图片");
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const QString target = path(prefix + '-' + hash + ".png");
    require(QDir().mkpath(QFileInfo(target).absolutePath()), "无法创建图片目录");
    QSaveFile output(target); require(output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit(), "无法保存图片");
    return QUrl::fromLocalFile(target).toString();
}
QVariantList PersonalizationService::actions() const { return {
    action("personalization.get", "查看个性化配置"),
    action("personalization.theme", "设置主题色", {field("color", "#RRGGBB（留空恢复默认）")}),
    action("personalization.background", "设置本地背景", {field("path", "本地图片路径（留空恢复默认）", "path")}),
    action("personalization.thumbnail", "设置实例缩略图", {field("instanceId", "实例 ID", "text", {}, true), field("path", "本地图片路径（留空恢复默认）", "path")}),
    action("personalization.shortcut", "设置快捷键", {field("target", "操作", "choice", "home", true, {"home", "library", "downloads", "settings", "features"}), field("sequence", "快捷键（如 Ctrl+9，留空恢复默认）")}),
    action("personalization.downloadSource", "选择下载源", {field("source", "下载源", "choice", DownloadSource::officialId(), true, {DownloadSource::officialId(), DownloadSource::mirrorId()})}, "官方源直连上游；BMCLAPI 镜像由 bangbang93 提供 (https://bmclapidoc.bangbang93.com/)，用于网络受限地区加速。Quilt、社区资源与整合包仍使用官方源，下载仍按官方摘要校验。"),
    action("personalization.reset", "恢复默认个性化", {}, "恢复主题、背景、缩略图和快捷键。", true),
    action("plugin.list", "查看原生插件"),
    action("plugin.install", "安装原生 Qt 插件", {field("path", "本地插件路径", "path", {}, true)}, "安装仅检查元数据，不执行代码。不支持 KAMUCL Electron/JavaScript 插件。"),
    action("plugin.enable", "启用原生插件", {field("id", "插件 ID", "text", {}, true)}, "此操作将在启动器进程执行本机代码，具有当前用户的文件和网络权限，没有沙箱。仅启用可信来源；重启后需要再次明确启用。", true),
    action("plugin.disable", "关闭原生插件", {field("id", "插件 ID", "text", {}, true)}, "要求插件停止任务；库保持映射直到退出启动器。"),
    action("plugin.remove", "卸载原生插件", {field("id", "插件 ID", "text", {}, true)}, "请先关闭插件。插件数据保留。", true),
    action("plugin.execute", "调用已启用插件", {field("id", "插件 ID", "text", {}, true), field("command", "命令", "text", {}, true), field("text", "文本参数")})
}; }
void PersonalizationService::execute(const QString &actionId, const QVariantMap &args) {
    try {
        if(actionId=="plugin.enable"||actionId=="plugin.remove"||actionId=="personalization.reset") require(args.value("confirm").toBool(),"请先确认此操作");
        if (actionId.startsWith("plugin.")) {
            QVariantMap result;
            const auto id = args.value("id").toString();
            if (actionId == "plugin.list") result["items"] = m_plugins.plugins();
            else if (actionId == "plugin.install") result = m_plugins.install(localPath(args.value("path")));
            else if (actionId == "plugin.enable") m_plugins.enable(id);
            else if (actionId == "plugin.disable") m_plugins.disable(id);
            else if (actionId == "plugin.remove") m_plugins.remove(id);
            else if (actionId == "plugin.execute") result = m_plugins.execute(id, args.value("command").toString(), args);
            else require(false, "未知插件操作");
            result["message"] = result.value("message", "插件操作完成"); emit completed(actionId, result); return;
        }
        if (actionId == "personalization.get") { emit completed(actionId, settings()); return; }
        auto next = m_values;
        if (actionId == "personalization.theme") next["accentColor"] = args.value("color").toString().trimmed();
        else if (actionId == "personalization.background") {
            const QString source = args.value("path").toString(); next["backgroundUrl"] = source.isEmpty() ? QString() : importImage(source, "background");
        } else if (actionId == "personalization.thumbnail") {
            const auto id = args.value("instanceId").toString(); require(validInstance(id), "实例 ID 无效");
            auto thumbnails = next.value("thumbnails").toMap(); const QString source = args.value("path").toString();
            if (source.isEmpty()) thumbnails.remove(id); else thumbnails[id] = importImage(source, "instance-" + id);
            next["thumbnails"] = thumbnails;
        } else if (actionId == "personalization.shortcut") {
            const auto target = args.value("target").toString(); require(defaultShortcuts().contains(target), "未知快捷键目标");
            auto keys = settings().value("shortcuts").toMap(); const auto sequence = args.value("sequence").toString().trimmed();
            keys[target] = sequence.isEmpty() ? defaultShortcuts().value(target) : shortcut(sequence); next["shortcuts"] = keys;
        } else if (actionId == "personalization.downloadSource") next["downloadSource"] = args.value("source", DownloadSource::officialId()).toString();
        else if (actionId == "personalization.reset") next = {};
        else require(false, "未知个性化操作");
        next = validated(next); write(path("settings.json"), next); m_values = next;
        emit changed(); auto result = settings(); result["message"] = "个性化配置已保存"; emit completed(actionId, result);
    } catch (const std::exception &error) { emit failed(actionId, QString::fromUtf8(error.what())); }
    catch (...) { emit failed(actionId, "插件抛出未知异常"); }
}
