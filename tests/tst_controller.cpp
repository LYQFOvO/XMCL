#include <QtTest>
#include <QTemporaryDir>
#include "LauncherController.h"
#include "services/MemoryAdvisor.h"

class ControllerTest : public QObject {
    Q_OBJECT
private slots:
    void selectionAndBounds() {
        QTemporaryDir dir;
        LauncherController state(dir.filePath("settings.ini"));
        QCOMPARE(state.instances().size(), 3);
        QSignalSpy spy(&state, &LauncherController::selectedIndexChanged);
        state.setSelectedIndex(2);
        QCOMPARE(state.selectedIndex(), 2);
        QCOMPARE(state.currentInstance().value("name").toString(), QStringLiteral("云上建筑师"));
        state.setSelectedIndex(-1);
        state.setSelectedIndex(3);
        QCOMPARE(state.selectedIndex(), 2);
        QCOMPARE(spy.count(), 1);
    }
    void memoryBounds() {
        QTemporaryDir dir;
        LauncherController state(dir.filePath("settings.ini"));
        const int maxGb = MemoryAdvisor::maxGb();
        QCOMPARE(state.maxMemoryGb(), maxGb);
        QCOMPARE(state.totalMemoryGb(), MemoryAdvisor::totalGb());
        QCOMPARE(state.suggestedMemoryGb(), MemoryAdvisor::suggestedGb());
        state.setMemoryGb(qMin(8, maxGb));
        QCOMPARE(state.memoryGb(), qMin(8, maxGb));
        state.setMemoryGb(1);
        QCOMPARE(state.memoryGb(), 2);
        state.setMemoryGb(99);
        QCOMPARE(state.memoryGb(), maxGb);
        state.setMemoryGb(state.suggestedMemoryGb());
        QCOMPARE(state.memoryGb(), MemoryAdvisor::suggestedGb());
    }
    void preferencesPersist() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.ini");
        {
            LauncherController state(path);
            state.setSelectedIndex(1);
            state.setMemoryGb(qMin(6, state.maxMemoryGb()));
            state.setEffectsEnabled(false);
            state.setAnimationsEnabled(false);
        }
        LauncherController restored(path);
        QCOMPARE(restored.selectedIndex(), 1);
        QCOMPARE(restored.memoryGb(), qMin(6, restored.maxMemoryGb()));
        QVERIFY(!restored.effectsEnabled());
        QVERIFY(!restored.animationsEnabled());
    }
    void corruptPreferencesAreSanitized() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.ini");
        { QSettings settings(path, QSettings::IniFormat);
          settings.setValue("instance", 99); settings.setValue("memoryGb", -20); }
        LauncherController state(path);
        QCOMPARE(state.selectedIndex(), 0);
        QCOMPARE(state.memoryGb(), 2);
    }
};
QTEST_GUILESS_MAIN(ControllerTest)
#include "tst_controller.moc"
