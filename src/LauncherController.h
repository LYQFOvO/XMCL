#pragma once
#include <QObject>
#include <QSettings>
#include "services/MemoryAdvisor.h"
#include <QVariantList>
#include <memory>

class LauncherController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList instances READ instances NOTIFY instancesChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(QVariantMap currentInstance READ currentInstance NOTIFY selectedIndexChanged)
    Q_PROPERTY(int memoryGb READ memoryGb WRITE setMemoryGb NOTIFY memoryGbChanged)
    Q_PROPERTY(int totalMemoryGb READ totalMemoryGb CONSTANT)
    Q_PROPERTY(int suggestedMemoryGb READ suggestedMemoryGb CONSTANT)
    Q_PROPERTY(int maxMemoryGb READ maxMemoryGb CONSTANT)
    Q_PROPERTY(bool effectsEnabled READ effectsEnabled WRITE setEffectsEnabled NOTIFY effectsEnabledChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled WRITE setAnimationsEnabled NOTIFY animationsEnabledChanged)
public:
    explicit LauncherController(const QString &settingsPath = {}, QObject *parent = nullptr);
    QVariantList instances() const;
    QVariantMap currentInstance() const;
    int selectedIndex() const { return m_selectedIndex; }
    int memoryGb() const { return m_memoryGb; }
    int totalMemoryGb() const { return MemoryAdvisor::totalGb(); }
    int suggestedMemoryGb() const { return MemoryAdvisor::suggestedGb(); }
    int maxMemoryGb() const { return MemoryAdvisor::maxGb(); }
    bool effectsEnabled() const { return m_effectsEnabled; }
    bool animationsEnabled() const { return m_animationsEnabled; }
    void setSelectedIndex(int value);
    void setMemoryGb(int value);
    void setEffectsEnabled(bool value);
    void setAnimationsEnabled(bool value);
    void setNativeInstances(const QVariantList &instances);
signals:
    void selectedIndexChanged();
    void memoryGbChanged();
    void effectsEnabledChanged();
    void animationsEnabledChanged();
    void instancesChanged();
private:
    std::unique_ptr<QSettings> m_settings;
    int m_selectedIndex = 0;
    int m_memoryGb = 4;
    bool m_effectsEnabled = true;
    bool m_animationsEnabled = true;
    QVariantList m_nativeInstances;
};
