import QtQuick
import QtQuick.Effects
Item {
    id: root
    property alias source: image.source
    property real radius: 22
    Rectangle {
        id: roundedMask
        anchors.fill: parent
        radius: root.radius
        color: "white"
        layer.enabled: true
        visible: false
    }
    Image {
        id: image
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: roundedMask
        }
    }
}
