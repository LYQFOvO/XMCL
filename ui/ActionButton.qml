import QtQuick
import QtQuick.Controls
Button {
    id: control
    property string iconName: ""
    property bool primary: false
    property bool compact: false
    property color foreground: primary ? Theme.ink : Theme.text
    implicitHeight: compact ? 38 : 48
    implicitWidth: contentRow.implicitWidth + (compact ? 28 : 36)
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.name: text
    scale: down ? 0.98 : 1
    Behavior on scale { NumberAnimation { duration: launcher.animationsEnabled ? 120 : 0 } }
    background: Rectangle {
        radius: control.compact ? 11 : 14
        color: control.primary ? (control.hovered ? "#cff9d8" : Theme.accent) : (control.hovered ? "#30ffffff" : "#14ffffff")
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : control.primary ? "#e1ffe8" : "#20ffffff"
        Behavior on color { ColorAnimation { duration: launcher.animationsEnabled ? 140 : 0 } }
    }
    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: 9
            Icon { name: control.iconName; color: control.foreground; width: 18; height: 18; visible: control.iconName !== ""; anchors.verticalCenter: parent.verticalCenter }
            Text { text: control.text; color: control.foreground; font.family: Theme.font; font.pixelSize: control.compact ? 12 : 14; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
        }
    }
}
