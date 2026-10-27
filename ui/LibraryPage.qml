import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    property var app
    property Item backdrop
    property int filterIndex: 0
    readonly property string query: search.text.trim().toLowerCase()
    function matches(instance) {
        const inCategory = filterIndex === 0 || (filterIndex === 1 ? instance.loader === "Vanilla" : instance.loader !== "Vanilla")
        return inCategory && (instance.name + instance.version + instance.loader).toLowerCase().includes(query)
    }
    readonly property int resultCount: launcher.instances.filter(instance => matches(instance)).length
    ColumnLayout {
        anchors.fill: parent; spacing: 22
        RowLayout {
            Layout.fillWidth: true
            Column { spacing: 8
                Text { text: "你的世界，各有精彩。"; color: Theme.text; font.pixelSize: 29; font.weight: Font.DemiBold }
                Text { text: "把不同的玩法，安放在各自的小天地。"; color: Theme.muted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: "探索新版本"; iconName: "plus"; primary: true; onClicked: page.app.openInstaller() }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Repeater {
                model: ["全部实例", "原版", "模组"]
                ActionButton {
                    required property string modelData
                    required property int index
                    text: modelData; compact: true; primary: page.filterIndex === index
                    onClicked: page.filterIndex = index
                }
            }
            Item { Layout.fillWidth: true }
            TextField {
                id: search; objectName: "instanceSearch"
                Layout.preferredWidth: 235; implicitHeight: 39
                leftPadding: 37; rightPadding: 12
                color: Theme.text; placeholderTextColor: Theme.subtle; selectionColor: "#46735a"
                placeholderText: "搜索名称、版本或加载器"
                font.pixelSize: 11
                background: Rectangle { radius: 12; color: "#35162920"; border.color: search.activeFocus ? Theme.accent : "#28ffffff" }
                Icon { x: 12; anchors.verticalCenter: parent.verticalCenter; width: 15; height: 15; name: "search"; color: Theme.subtle }
            }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.preferredHeight: 381
            Layout.fillHeight: page.resultCount > 3
            visible: page.resultCount > 0
            clip: true; contentWidth: availableWidth
            GridLayout {
            width: parent.width; columns: 3; columnSpacing: 16; rowSpacing: 16
            Repeater {
                model: launcher.instances
                InstanceCard {
                    required property var modelData
                    required property int index
                    objectName: "instance" + index
                    Layout.fillWidth: true
                    Layout.maximumWidth: (page.width - 32) / 3
                    Layout.preferredHeight: 381
                    instance: modelData; backdrop: page.backdrop
                    selected: launcher.selectedIndex === index
                    visible: page.matches(modelData)
                    onChosen: launcher.selectedIndex = index
                    onManageRequested: page.app.openFeatures("instances", modelData.id ? {instanceId: modelData.id} : {})
                    onSettingsRequested: page.app.openInstanceSettings(modelData)
                }
            }
            Item { Layout.fillWidth: true; visible: page.resultCount < 3 }
            }
        }
        GlassPanel {
            visible: page.resultCount === 0
            Layout.fillWidth: true; Layout.preferredHeight: 235; backdrop: page.backdrop
            Column { anchors.centerIn: parent; spacing: 15
                Icon { name: "search"; color: Theme.subtle; width: 32; height: 32; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "还没找到这个世界"; color: Theme.text; font.pixelSize: 19; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "试试其他名称、版本，或切换分类。"; color: Theme.muted; font.pixelSize: 12; anchors.horizontalCenter: parent.horizontalCenter }
                ActionButton { text: "清除筛选"; compact: true; anchors.horizontalCenter: parent.horizontalCenter; onClicked: { search.clear(); page.filterIndex = 0 } }
            }
        }
        Row { spacing: 8
            Icon { name: "info"; width: 14; height: 14; color: Theme.subtle }
            Text { text: features.instances.length ? "右键实例可管理文件、备份与启动配置。" : "这里展示的是示例实例。通过探索新版本或功能面板创建你的实例。"; color: Theme.subtle; font.pixelSize: 10 }
        }
        Item { Layout.fillHeight: true }
    }
}
