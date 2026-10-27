import QtQuick
import QtQuick3D
import XMCL.Native 1.0
Item {
    id: preview
    property url source
    property bool slim: false
    property real angle: -25
    Image { id: sourceInfo; source: preview.source; visible: false }
    View3D {
        anchors.fill: parent
        environment: SceneEnvironment { backgroundMode: SceneEnvironment.Transparent; antialiasingMode: SceneEnvironment.MSAA }
        PerspectiveCamera { id: camera; position: Qt.vector3d(0, 18, 67); clipNear: 1; clipFar: 200 }
        DirectionalLight { eulerRotation: Qt.vector3d(-30,-30,0); brightness: 1.5; ambientColor: "#9fa99c" }
        Node {
            eulerRotation.y: preview.angle
            Model { y: 28; geometry: SkinGeometry { part: 0; legacy: sourceInfo.implicitHeight === 32 } materials: skin }
            Model { y: 28; scale: Qt.vector3d(1.125,1.125,1.125); geometry: SkinGeometry { part: 6; legacy: sourceInfo.implicitHeight === 32 } materials: skin }
            Model { y: 18; geometry: SkinGeometry { part: 1; legacy: sourceInfo.implicitHeight === 32 } materials: skin }
            Model { x: preview.slim ? -5.5 : -6; y: 18; geometry: SkinGeometry { part: 2; slim: preview.slim; legacy: sourceInfo.implicitHeight === 32 } materials: skin }
            Model { x: preview.slim ? 5.5 : 6; y: 18; geometry: SkinGeometry { part: 3; slim: preview.slim; legacy: sourceInfo.implicitHeight === 32 } materials: skin }
            Model { x: -2; y: 6; geometry: SkinGeometry { part: 4; legacy: sourceInfo.implicitHeight === 32 } materials: skin }
            Model { x: 2; y: 6; geometry: SkinGeometry { part: 5; legacy: sourceInfo.implicitHeight === 32 } materials: skin }
            Node {
                visible: sourceInfo.implicitHeight === 64
                Model { y: 18; scale: Qt.vector3d(1.0625,1.0417,1.125); geometry: SkinGeometry { part: 7 } materials: skin }
                Model { x: preview.slim ? -5.5 : -6; y: 18; scale: Qt.vector3d(1.125,1.0417,1.125); geometry: SkinGeometry { part: 8; slim: preview.slim } materials: skin }
                Model { x: preview.slim ? 5.5 : 6; y: 18; scale: Qt.vector3d(1.125,1.0417,1.125); geometry: SkinGeometry { part: 9; slim: preview.slim } materials: skin }
                Model { x: -2; y: 6; scale: Qt.vector3d(1.125,1.0417,1.125); geometry: SkinGeometry { part: 10 } materials: skin }
                Model { x: 2; y: 6; scale: Qt.vector3d(1.125,1.0417,1.125); geometry: SkinGeometry { part: 11 } materials: skin }
            }
        }
        PrincipledMaterial {
            id: skin
            baseColorMap: Texture { source: preview.source; magFilter: Texture.Nearest; minFilter: Texture.Nearest; mipFilter: Texture.None; generateMipmaps: false }
            roughness: 1
            alphaMode: PrincipledMaterial.Mask
            alphaCutoff: 0.1
            cullMode: Material.NoCulling
        }
    }
    MouseArea {
        anchors.fill: parent
        property real lastX: 0
        onPressed: mouse => { lastX = mouse.x }
        onPositionChanged: mouse => { if (pressed) { preview.angle += (mouse.x-lastX)*0.7; lastX=mouse.x } }
        onWheel: wheel => { camera.z=Math.max(40,Math.min(110,camera.z-wheel.angleDelta.y/30)) }
    }
}
