#include "LauncherController.h"

LauncherController::LauncherController(const QString &settingsPath, QObject *parent)
    : QObject(parent), m_settings(settingsPath.isEmpty()
          ? std::make_unique<QSettings>()
          : std::make_unique<QSettings>(settingsPath, QSettings::IniFormat)) {
    const int savedIndex = m_settings->value("instance", 0).toInt();
    m_selectedIndex = savedIndex >= 0 && savedIndex < instances().size() ? savedIndex : 0;
    m_memoryGb = qBound(2, m_settings->value("memoryGb", MemoryAdvisor::suggestedGb()).toInt(), MemoryAdvisor::maxGb());
    m_effectsEnabled = m_settings->value("effects", true).toBool();
    m_animationsEnabled = m_settings->value("animations", true).toBool();
}

QVariantList LauncherController::instances() const {
    if (!m_nativeInstances.isEmpty()) return m_nativeInstances;
    return {
        QVariantMap{{"name", QStringLiteral("原野漫游")}, {"version", "1.21.1"},
                    {"loader", "Fabric"}, {"description", QStringLiteral("走进森林，开启一段自在的探索。")},
                    {"color", "#a9e5ba"}, {"icon", "grass"}, {"category", QStringLiteral("生存 · 轻量模组")}},
        QVariantMap{{"name", QStringLiteral("纯净世界")}, {"version", "1.21.1"},
                    {"loader", "Vanilla"}, {"description", QStringLiteral("从第一块木头开始，创造你的世界。")},
                    {"color", "#e5c58d"}, {"icon", "cube"}, {"category", QStringLiteral("原版 · 自由探索")}},
        QVariantMap{{"name", QStringLiteral("云上建筑师")}, {"version", "1.20.1"},
                    {"loader", "Forge"}, {"description", QStringLiteral("让每一个天马行空的想法落地。")},
                    {"color", "#b9bfdc"}, {"icon", "layers"}, {"category", QStringLiteral("创造 · 建筑灵感")}}
    };
}
QVariantMap LauncherController::currentInstance() const { return instances().at(m_selectedIndex).toMap(); }
void LauncherController::setSelectedIndex(int value) {
    if (value < 0 || value >= instances().size() || value == m_selectedIndex) return;
    m_selectedIndex = value;
    m_settings->setValue("instance", value);
    if (!m_nativeInstances.isEmpty())
        m_settings->setValue("selectedInstanceId", m_nativeInstances.at(value).toMap().value("id"));
    emit selectedIndexChanged();
}

void LauncherController::setNativeInstances(const QVariantList &instances) {
    m_nativeInstances.clear();
    for (auto value : instances) {
        auto item = value.toMap();
        QString loader = item.value("loader", "vanilla").toString();
        if (!loader.isEmpty()) loader[0] = loader.at(0).toUpper();
        item["loader"] = loader;
        item["icon"] = loader == "Vanilla" ? "cube" : "grass";
        item["color"] = "#a9e5ba";
        item["category"] = item.value("installed").toBool() ? "已安装 · 本地实例" : "本地配置 · 待安装";
        item["description"] = "独立管理你的游戏、模组与世界。";
        m_nativeInstances.append(item);
    }
    m_selectedIndex = 0;
    const QString saved = m_settings->value("selectedInstanceId").toString();
    for (qsizetype i = 0; i < m_nativeInstances.size(); ++i)
        if (m_nativeInstances.at(i).toMap().value("id").toString() == saved) m_selectedIndex = int(i);
    emit instancesChanged();
    emit selectedIndexChanged();
}
void LauncherController::setMemoryGb(int value) {
    value = qBound(2, value, MemoryAdvisor::maxGb());
    if (value == m_memoryGb) return;
    m_memoryGb = value;
    m_settings->setValue("memoryGb", value);
    emit memoryGbChanged();
}
void LauncherController::setEffectsEnabled(bool value) {
    if (value == m_effectsEnabled) return;
    m_effectsEnabled = value;
    m_settings->setValue("effects", value);
    emit effectsEnabledChanged();
}
void LauncherController::setAnimationsEnabled(bool value) {
    if (value == m_animationsEnabled) return;
    m_animationsEnabled = value;
    m_settings->setValue("animations", value);
    emit animationsEnabledChanged();
}
