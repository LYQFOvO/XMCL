import QtQuick
import QtQuick.Effects
Item {
    id: panel
    property Item backdrop
    property real radius: 22
    property color tint: Qt.rgba(0.055, 0.105, 0.085, 0.56)
    property bool glassEnabled: launcher.effectsEnabled
    ShaderEffectSource {
        id: capture
        visible: false
        sourceItem: panel.backdrop
        sourceRect: {
            if (!panel.backdrop) return Qt.rect(0, 0, panel.width, panel.height)
            // mapToItem alone does not register geometry dependencies. Track ancestors
            // so layout moves and instance filtering also update the sampled region.
            let geometry = panel.backdrop.width + panel.backdrop.height
            let ancestor = panel
            while (ancestor) {
                geometry += ancestor.x + ancestor.y + ancestor.width + ancestor.height
                ancestor = ancestor.parent
            }
            let point = panel.mapToItem(panel.backdrop, 0, 0)
            return Qt.rect(point.x, point.y, panel.width, panel.height)
        }
        textureSize: Qt.size(Math.max(1, panel.width), Math.max(1, panel.height))
        live: panel.visible && panel.glassEnabled
        smooth: true
    }
    MultiEffect {
        id: blur
        width: panel.width; height: panel.height
        source: capture
        blurEnabled: true; blur: 0.8; blurMax: 48
        saturation: -0.15
        autoPaddingEnabled: false
        visible: false
        layer.enabled: panel.glassEnabled
    }
    ShaderEffect {
        anchors.fill: parent
        visible: panel.glassEnabled && panel.backdrop !== null
        property variant source: blur
        property vector2d panelSize: Qt.vector2d(width, height)
        property real cornerRadius: panel.radius
        property color glassTint: panel.tint
        fragmentShader: "qrc:/shaders/glass.frag.qsb"
    }
    Rectangle {
        anchors.fill: parent; radius: panel.radius
        visible: !panel.glassEnabled || panel.backdrop === null
        color: "#182c24"; border.color: "#35483d"; border.width: 1
    }
}
