import QtQuick
import QtQuick.Effects
Item {
    id: root
    property string name: "cube"
    property color color: Theme.text
    implicitWidth: 22
    implicitHeight: 22
    Image {
        id: glyph
        anchors.fill: parent
        source: root.name.length ? "qrc:/assets/icons/" + root.name + ".svg" : ""
        sourceSize: Qt.size(width * 2, height * 2)
        visible: false
    }
    MultiEffect { anchors.fill: glyph; source: glyph; colorization: 1; colorizationColor: root.color }
}
