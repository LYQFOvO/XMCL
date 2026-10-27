import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    property var app
    property Item backdrop
    readonly property bool compact: height < 650
    ColumnLayout {
        anchors.fill: parent
        spacing: page.compact ? 14 : 19
        RowLayout {
            Layout.fillWidth: true
            Column {
                spacing: 8
                Text { text: "你好，探险家。"; color: Theme.text; font.pixelSize: page.compact ? 27 : 30; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                Text { text: "把日常留在身后，把想象带进世界。"; color: Theme.muted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            Row {
                spacing: 7; Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: 3
                Icon { name: "sparkles"; width: 14; height: 14; color: Theme.accent }
                Text { text: "今天，也有新的可能"; color: "#b2c6b6"; font.pixelSize: 11 }
            }
        }
        Item {
            id: hero
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 206
            Layout.preferredHeight: 320
            RoundedImage { anchors.fill: parent; source: "qrc:/assets/forest.png"; radius: 24 }
            Rectangle {
                anchors.fill: parent; radius: 24
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#d30b2019" }
                    GradientStop { position: 0.58; color: "#40102017" }
                    GradientStop { position: 1; color: "#00102017" }
                }
            }
            Rectangle { anchors.fill: parent; radius: 24; color: "transparent"; border.color: "#40d2e9d6"; border.width: 1 }
            Column {
                x: 32; y: page.compact ? 23 : 30; spacing: page.compact ? 11 : 16
                Row {
                    spacing: 8
                    Rectangle { width: 5; height: 5; radius: 2.5; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "A WORLD OF YOUR OWN"; font.pixelSize: 9; font.letterSpacing: 2.2; color: "#d2e5cc" }
                }
                Text { text: "下一场冒险，\n从这里开始。"; font.pixelSize: page.compact ? 31 : 38; font.weight: Font.DemiBold; lineHeight: 1.2; color: "#f3f6e9"; font.letterSpacing: 2 }
                Text { text: "一方天地，无限可能。\n以你喜欢的方式，遇见 Minecraft。"; color: "#c3d3bf"; font.pixelSize: 11; lineHeight: 1.65 }
            }
            Row {
                x: 32; anchors.bottom: parent.bottom; anchors.bottomMargin: page.compact ? 17 : 27; spacing: 8
                Tag { text: "JAVA EDITION"; textColor: "#d7e9d0"; color: "#301b3929"; border.color: "#40deead4" }
                Tag { text: "探索 · 建造 · 自由"; textColor: "#d7e9d0"; color: "#301b3929"; border.color: "#30deead4" }
            }
            Row {
                anchors.right: parent.right; anchors.rightMargin: 23; anchors.bottom: parent.bottom; anchors.bottomMargin: 23; spacing: 6
                Icon { name: "leaf"; width: 13; height: 13; color: "#d7e8c9" }
                Text { text: "静谧之境  /  THE QUIET WILD"; color: "#dfecd4"; font.pixelSize: 8; font.letterSpacing: 1 }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "继续你的旅程"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
            Tag { text: launcher.instances.length + (launcher.currentInstance.id ? " 个本地实例" : " 个示例实例"); Layout.leftMargin: 3; implicitHeight: 22; textColor: Theme.subtle }
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "allInstances"; text: "全部实例"; iconName: "arrow"; compact: true; implicitHeight: 28; onClicked: page.app.currentPage = 1 }
        }
        GlassPanel {
            Layout.fillWidth: true; Layout.preferredHeight: page.compact ? 100 : 113
            backdrop: page.backdrop
            RowLayout {
                anchors.fill: parent; anchors.margins: 21; spacing: 16
                Rectangle {
                    width: 58; height: 58; radius: 15; color: "#294c36"; border.color: "#49734b"
                    Icon { anchors.centerIn: parent; width: 34; height: 34; name: launcher.currentInstance.icon; color: launcher.currentInstance.color }
                }
                ColumnLayout {
                    spacing: 8
                    Row { spacing: 10
                        Text { objectName: "currentInstanceName"; text: launcher.currentInstance.name; color: Theme.text; font.pixelSize: 19; font.weight: Font.DemiBold }
                        Tag { text: launcher.currentInstance.id ? (launcher.currentInstance.installed ? "已安装" : "待安装") : "示例"; implicitHeight: 21; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Row { spacing: 7
                        Text { text: "Minecraft " + launcher.currentInstance.version; color: Theme.muted; font.pixelSize: 11 }
                        Text { text: "·"; color: Theme.subtle; font.pixelSize: 11 }
                        Text { text: launcher.currentInstance.loader; color: Theme.muted; font.pixelSize: 11 }
                    }
                }
                Item { Layout.fillWidth: true }
                Column {
                    visible: page.width > 880
                    spacing: 8; Layout.rightMargin: 12
                    Text { text: "内存分配"; color: Theme.subtle; font.pixelSize: 10 }
                    Text { objectName: "currentInstanceMemory"; text: (launcher.currentInstance.memoryGb !== undefined ? launcher.currentInstance.memoryGb : launcher.memoryGb) + " GB"; color: Theme.muted; font.pixelSize: 15 }
                    ActionButton {
                        objectName: "homeInstanceSettings"; text: "实例设置"; iconName: "settings"; compact: true; implicitHeight: 26
                        visible: !!launcher.currentInstance.id
                        onClicked: page.app.openInstanceSettings(launcher.currentInstance)
                    }
                }
                ActionButton {
                    objectName: "launchButton"
                    text: "开始游戏"; iconName: "play"; primary: true
                    implicitWidth: page.width > 880 ? 202 : 170; implicitHeight: 54
                    onClicked: page.app.requestLaunch()
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 14
            Repeater {
                model: [ {title: "发现更多可能", subtitle: "版本、模组，让世界更丰富", icon: "download", target: 2}, {title: "按你的方式游玩", subtitle: "每一处细节，都可以很合心意", icon: "settings", target: 3} ]
                delegate: GlassPanel {
                    id: quickCard
                    required property var modelData
                    Layout.fillWidth: true; Layout.preferredHeight: page.compact ? 81 : 97
                    backdrop: page.backdrop; radius: 18
                    Button {
                        id: quickButton
                        anchors.fill: parent; hoverEnabled: true; activeFocusOnTab: true
                        Accessible.name: quickCard.modelData.title
                        background: Rectangle { radius: 18; color: quickButton.hovered ? "#0effffff" : "transparent"; border.color: quickButton.activeFocus ? Theme.accent : "transparent" }
                        contentItem: Item {
                            Row {
                                x: 15; anchors.verticalCenter: parent.verticalCenter; spacing: 14
                                Rectangle { width: 40; height: 40; radius: 12; color: "#12d2ead5"; anchors.verticalCenter: parent.verticalCenter
                                    Icon { anchors.centerIn: parent; width: 20; height: 20; name: quickCard.modelData.icon; color: "#a7c7ac" }
                                }
                                Column { spacing: 7
                                    Text { text: quickCard.modelData.title; color: Theme.text; font.pixelSize: 13 }
                                    Text { text: quickCard.modelData.subtitle; color: Theme.subtle; font.pixelSize: 10 }
                                }
                            }
                            Icon { anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter; width: 17; height: 17; name: "arrow"; color: Theme.subtle }
                        }
                        onClicked: page.app.currentPage = quickCard.modelData.target
                    }
                }
            }
        }
    }
}
