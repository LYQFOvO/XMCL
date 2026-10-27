#pragma once
#include <QString>
#include <QUrl>

// 下载源选择：官方源直连上游；BMCLAPI 镜像把 Mojang / Fabric / Forge / NeoForge /
// authlib-injector 的官方地址改写为 https://bmclapi2.bangbang93.com 对应路由。
// 规则来自 BMCLAPI 官方文档 https://bmclapidoc.bangbang93.com/ 的“使用”一节：
//   launchermeta/launcher.mojang.com -> https://bmclapi2.bangbang93.com
//   resources.download.minecraft.net -> /assets
//   libraries.minecraft.net -> /maven
//   meta.fabricmc.net -> /fabric-meta，maven.fabricmc.net -> /maven
//   files/maven.minecraftforge.net -> /maven
//   maven.neoforged.net/releases -> /maven
//   authlib-injector.yushi.moe -> /mirrors/authlib-injector
// Quilt 官方文档标注镜像暂不可用，因此保持官方源；社区资源与整合包由
// Modrinth/CurseForge 提供，BMCLAPI 不镜像，也保持官方源。
namespace DownloadSource {
inline QString officialId() { return QStringLiteral("official"); }
inline QString mirrorId() { return QStringLiteral("bmclapi"); }
inline QString mirrorHost() { return QStringLiteral("bmclapi2.bangbang93.com"); }
inline QString mirrorBase() { return QStringLiteral("https://bmclapi2.bangbang93.com"); }
inline bool valid(const QString &id) { return id == officialId() || id == mirrorId(); }
inline QString normalized(const QString &id) { return valid(id) ? id : officialId(); }
inline QString label(const QString &id) { return id == mirrorId() ? QStringLiteral("BMCLAPI 镜像") : QStringLiteral("官方源"); }
inline QString description(const QString &id) {
    return id == mirrorId()
        ? QStringLiteral("Mojang/Fabric/Forge/NeoForge 等文件通过 BMCLAPI 镜像（bangbang93 提供）下载；Quilt、社区资源与整合包仍使用官方源。下载仍按官方摘要校验。")
        : QStringLiteral("直接连接 Mojang、Fabric、Quilt、Forge、NeoForge 等官方源下载。");
}
namespace detail {
inline QUrl withPath(const QUrl &official, const QString &path) {
    QUrl result = official;
    result.setScheme(QStringLiteral("https"));
    result.setHost(mirrorHost());
    result.setPort(-1);
    result.setPath(path);
    return result;
}
inline QUrl withPrefix(const QUrl &official, const QString &prefix) {
    return withPath(official, prefix + official.path());
}
}
// 把官方地址映射为 BMCLAPI 等价地址；未知主机（社区 CDN、本机夹具等）保持原样。
inline QUrl mirror(const QUrl &official) {
    if (!official.isValid() || official.scheme() != QStringLiteral("https")) return official;
    const auto host = official.host().toLower();
    if (host == "piston-meta.mojang.com" || host == "piston-data.mojang.com"
        || host == "launchermeta.mojang.com" || host == "launcher.mojang.com")
        return detail::withPath(official, official.path());
    if (host == "resources.download.minecraft.net") return detail::withPrefix(official, QStringLiteral("/assets"));
    if (host == "libraries.minecraft.net") return detail::withPrefix(official, QStringLiteral("/maven"));
    if (host == "meta.fabricmc.net") return detail::withPrefix(official, QStringLiteral("/fabric-meta"));
    if (host == "maven.fabricmc.net") return detail::withPrefix(official, QStringLiteral("/maven"));
    if (host == "maven.minecraftforge.net") return detail::withPrefix(official, QStringLiteral("/maven"));
    if (host == "files.minecraftforge.net") {
        auto path = official.path();
        if (path.startsWith(QStringLiteral("/maven"))) path = path.mid(6);
        return detail::withPath(official, QStringLiteral("/maven") + path);
    }
    if (host == "maven.neoforged.net") {
        auto path = official.path();
        if (path.startsWith(QStringLiteral("/releases"))) path = path.mid(9);
        return detail::withPath(official, QStringLiteral("/maven") + path);
    }
    if (host == "authlib-injector.yushi.moe") return detail::withPrefix(official, QStringLiteral("/mirrors/authlib-injector"));
    return official;
}
inline QUrl resolve(const QUrl &official, const QString &source) {
    return normalized(source) == mirrorId() ? mirror(official) : official;
}
}
