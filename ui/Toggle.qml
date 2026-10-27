import QtQuick
import QtQuick.Controls
Switch {
    id: control
    implicitWidth: 46; implicitHeight: 28
    padding: 0
    activeFocusOnTab: true
    indicator: Rectangle {
        width: 46; height: 28; radius: 14
        color: control.checked ? Theme.accent : "#41544a"
        border.color: control.activeFocus ? "white" : "transparent"
        Rectangle {
            x: control.checked ? 22 : 4; y: 4
            width: 20; height: 20; radius: 10
            color: control.checked ? Theme.ink : "#d5ded7"
            Behavior on x { NumberAnimation { duration: launcher.animationsEnabled ? 160 : 0; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {}
}
