import QtQuick
Rectangle {
    id: root
    property alias text: label.text
    property color textColor: Theme.muted
    implicitWidth: label.implicitWidth + 16
    implicitHeight: 25
    radius: 7
    color: "#10ffffff"
    border.color: "#15ffffff"
    Text { id: label; anchors.centerIn: parent; color: root.textColor; font.family: Theme.font; font.pixelSize: 11 }
}
