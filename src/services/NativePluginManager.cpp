#include "NativePluginManager.h"
#include "NativePlugin.h"
#include "ServiceUtils.h"
#include <QCryptographicHash>
#include <QPluginLoader>
#include <QRegularExpression>
using namespace ServiceUtils;
namespace {
bool validId(const QString &id) { return QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]{0,79}$").match(id).hasMatch(); }
QString digest(const QString &file) {
    QFile input(file); require(input.open(QIODevice::ReadOnly), "无法读取插件文件");
    QCryptographicHash hash(QCryptographicHash::Sha256); require(hash.addData(&input), "无法校验插件");
    return QString::fromLatin1(hash.result().toHex());
}
}
NativePluginManager::NativePluginManager(QString root) : m_root(QDir(root).absoluteFilePath("native-plugins")) {
    try { m_installed = read(path("installed.json"), true); } catch (const std::exception &) { /* Never load code at startup. */ }
}
NativePluginManager::~NativePluginManager() {
    const auto ids = m_loaders.keys();
    for (const auto &id : ids) disable(id);
}
QString NativePluginManager::path(const QString &name) const {
    require(!QFileInfo(m_root).isSymLink(), "插件目录不能是符号链接");
    require(!name.contains('/') && !name.contains('\\') && name != "..", "插件路径无效");
    const QString result = m_root + '/' + name;
    require(!QFileInfo(result).isSymLink(), "插件文件不能是符号链接");
    return result;
}
QVariantMap NativePluginManager::inspect(const QString &file) {
    require(QFileInfo(file).isFile() && !QFileInfo(file).isSymLink(), "请选择本地原生插件文件");
    require(QFileInfo(file).size() <= 128 * 1024 * 1024, "插件超过 128 MiB");
    QPluginLoader loader(file); // metaData() reads the Qt header without executing plugin code.
    const auto metadata = loader.metaData();
    require(metadata.value("IID").toString() == XMCL_NATIVE_PLUGIN_IID, "不是兼容的 XMCL 原生 Qt 插件；不支持 Electron 插件");
    const auto values = metadata.value("MetaData").toObject().toVariantMap();
    require(values.value("apiVersion").toInt() == 1 && validId(values.value("id").toString())
            && !values.value("name").toString().trimmed().isEmpty()
            && !values.value("version").toString().trimmed().isEmpty(), "插件元数据无效（id/name/version/apiVersion）");
    return values;
}
QVariantMap NativePluginManager::entry(const QString &id) const {
    require(validId(id) && m_installed.contains(id), "插件未安装或 ID 无效");
    const auto value = m_installed.value(id).toMap();
    require(value.value("id").toString() == id && value.value("file").toString().startsWith(id + '.'), "插件安装记录无效");
    return value;
}
QVariantList NativePluginManager::plugins() const {
    QVariantList result;
    for (const auto &id : m_installed.keys()) {
        try { auto value = entry(id); value["enabled"] = m_loaders.contains(id); value["title"] = value.value("name"); value["subtitle"] = value.value("version"); result.append(value); }
        catch (const std::exception &) { }
    }
    return result;
}
QVariantMap NativePluginManager::install(const QString &source) {
    const auto sourcePath = QFileInfo(source).absoluteFilePath();
    auto metadata = inspect(sourcePath);
    const auto id = metadata.value("id").toString();
    require(!m_installed.contains(id), "插件已安装，请先关闭并卸载旧版本");
    require(!m_loadedIds.contains(id), "此插件在当前进程已加载过；请重启启动器后安装替换版本");
    const QString suffix = QFileInfo(sourcePath).suffix().toLower();
    require(QStringList{"dll", "so", "dylib"}.contains(suffix), "插件必须是 .dll/.so/.dylib 本机库");
    const QString file = id + '.' + suffix, target = path(file);
    require(QDir().mkpath(m_root), "无法创建插件目录");
    require(!QFile::exists(target), "插件目标已存在");
    require(QFile::copy(sourcePath, target), "无法复制插件");
    try {
        // Inspect the copy again, then bind activation to these exact bytes.
        metadata = inspect(target);
        require(metadata.value("id").toString() == id, "插件在安装期间发生变化");
        metadata["file"] = file; metadata["sha256"] = digest(target);
        auto next = m_installed; next[id] = metadata;
        write(path("installed.json"), next); m_installed = next;
    } catch (...) { QFile::remove(target); throw; }
    metadata["enabled"] = false; return metadata;
}
void NativePluginManager::enable(const QString &id) {
    auto metadata = entry(id); if (m_loaders.contains(id)) return;
    const QString file = path(metadata.value("file").toString());
    require(digest(file) == metadata.value("sha256").toString(), "插件文件发生改变，请重新安装并确认来源");
    require(inspect(file).value("id").toString() == id, "插件 ID 不匹配");
    auto loader = std::make_shared<QPluginLoader>(file);
    // Keep the library mapped after stop: pending Qt internals must not jump into unmapped code.
    loader->setLoadHints(QLibrary::PreventUnloadHint);
    m_loadedIds.insert(id); // Even a failed start may leave the library mapped by Qt.
    auto *object = loader->instance(); require(object, "无法加载插件：" + loader->errorString());
    auto *plugin = qobject_cast<NativePlugin *>(object); require(plugin, "插件未实现 NativePlugin 接口");
    QString error;
    const QString storage = path(id + ".data");
    require(QDir().mkpath(storage), "无法创建插件数据目录");
    bool started = false;
    try { started = plugin->start({{"dataDirectory", storage}, {"apiVersion", 1}}, &error); }
    catch (...) { plugin->stop(); throw; }
    if (!started) { plugin->stop(); require(false, error.isEmpty() ? "插件启动失败" : error); }
    m_loaders[id] = std::move(loader);
}
void NativePluginManager::disable(const QString &id) {
    require(validId(id), "插件 ID 无效");
    const auto loader = m_loaders.take(id); if (!loader) return;
    if (auto *plugin = qobject_cast<NativePlugin *>(loader->instance())) plugin->stop();
}
void NativePluginManager::remove(const QString &id) {
    const auto value = entry(id);
    require(!m_loaders.contains(id), "请先关闭插件");
    auto next = m_installed; next.remove(id);
    const QString file = path(value.value("file").toString());
    // Commit registry first. A failed file deletion leaves an inert orphan, never active code.
    write(path("installed.json"), next); m_installed = next;
    require(!QFile::exists(file) || QFile::remove(file), "插件记录已删除；文件仍被占用，请重启后手动删除：" + file);
}
QVariantMap NativePluginManager::execute(const QString &id, const QString &command, const QVariantMap &args) {
    require(m_loaders.contains(id), "请先明确启用此插件");
    auto *plugin = qobject_cast<NativePlugin *>(m_loaders.value(id)->instance());
    require(plugin, "插件接口不可用"); return plugin->execute(command, args);
}
