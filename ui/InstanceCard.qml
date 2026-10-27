import QtQuick
import QtQuick.Controls

GlassPanel {
    id: card
    property var instance
    property bool selected: false
    signal chosen()
    signal manageRequested()
    signal settingsRequested()
    TapHandler { acceptedButtons: Qt.RightButton; onTapped: card.manageRequested() }
    radius: 21
    RoundedImage { x: 1; y: 1; width: parent.width - 2; height: 133; radius: 20; source: features.appearance.thumbnails[card.instance.id || ""] || "qrc:/assets/forest.png"; opacity: 0.73 }
    Rectangle {
        x: 1; y: 1; width: parent.width - 2; height: 134; radius: 20
        gradient: Gradient {
            GradientStop { position: 0; color: "#10234126" }
            GradientStop { position: 1; color: "#ea142a20" }
        }
    }
    Tag { x: 16; y: 16; text: card.instance.loader; color: "#b31b3023"; textColor: card.instance.color }
    ActionButton {
        objectName: card.objectName + "Settings"
        anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 12
        text: "设置"; iconName: "settings"; compact: true; implicitHeight: 28
        visible: !!card.instance.id
        onClicked: card.settingsRequested()
    }
    Rectangle {
        x: 20; y: 90; width: 57; height: 57; radius: 17
        color: "#263e2d"; border.color: card.instance.color
        Icon { anchors.centerIn: parent; width: 32; height: 32; name: card.instance.icon; color: card.instance.color }
    }
    Column {
        x: 20; y: 164; width: parent.width - 40; spacing: 10
        Text { width: parent.width; elide: Text.ElideRight; text: card.instance.name; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
        Text { text: card.instance.category; color: card.instance.color; font.pixelSize: 10 }
        Text { width: parent.width; text: card.instance.description; color: Theme.muted; font.pixelSize: 11; wrapMode: Text.Wrap; lineHeight: 1.5; height: 39 }
        Rectangle { width: parent.width; height: 1; color: "#18ffffff" }
        Row {
            width: parent.width; spacing: 6
            Text { text: "Minecraft " + card.instance.version; color: Theme.muted; font.pixelSize: 10 }
            Text { text: card.instance.id ? (card.instance.installed ? "· 已安装" : "· 待安装") : "· 示例配置"; color: Theme.subtle; font.pixelSize: 10 }
        }
    }
    ActionButton {
        objectName: card.objectName + "Select"
        x: 20; anchors.bottom: parent.bottom; anchors.bottomMargin: 19; width: parent.width - 40
        text: card.selected ? "当前实例" : "选为当前"; iconName: card.selected ? "check" : "play"
        primary: card.selected; implicitHeight: 41
        onClicked: card.chosen()
    }
    Rectangle { anchors.fill: parent; radius: card.radius; color: "transparent"; border.color: card.selected ? "#9ebbd9a6" : "transparent"; border.width: 1 }
}
