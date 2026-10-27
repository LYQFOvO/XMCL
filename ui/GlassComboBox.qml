import QtQuick
import QtQuick.Controls

// 深色森林主题下的下拉框：折叠态与弹层共用一套明确的高对比配色。
// Basic 样式默认会把弹层背景画成 palette.window、把项文字画成 palette.text；
// 这里全部显式覆盖，避免文字与背景变成同一种深绿色。
ComboBox {
    id: control
    implicitHeight: 42
    implicitWidth: 220
    leftPadding: 13
    rightPadding: 35
    hoverEnabled: true
    activeFocusOnTab: true
    font.pixelSize: 13
    font.family: Theme.font
    palette.window: Theme.popupBackground
    palette.base: Theme.popupBackground
    palette.button: Theme.fieldBackground
    palette.buttonText: Theme.fieldText
    palette.text: Theme.fieldText
    palette.highlightedText: Theme.popupTextHighlighted
    palette.highlight: Theme.popupHighlight
    palette.mid: Theme.popupBorder
    palette.dark: Theme.fieldTextDisabled
    background: Rectangle {
        objectName: control.objectName + "Background"
        radius: 10
        color: !control.enabled ? Theme.fieldBackgroundDisabled : control.down ? Theme.fieldBackgroundDown : Theme.fieldBackground
        border.color: control.activeFocus || control.popup.visible ? Theme.fieldFocusRing : Theme.fieldBorder
        border.width: control.activeFocus ? 2 : 1
    }
    contentItem: Text {
        objectName: control.objectName + "CurrentText"
        text: control.displayText
        color: control.enabled ? Theme.fieldText : Theme.fieldTextDisabled
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        name: "chevron-down"; color: control.enabled ? Theme.fieldText : Theme.fieldTextDisabled
        width: 15; height: 15
        x: control.width - width - 12; y: (control.height-height)/2
        rotation: control.popup.visible ? 180 : 0
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        width: ListView.view.width; height: 40
        highlighted: control.highlightedIndex === index
        hoverEnabled: true
        contentItem: Text {
            objectName: control.objectName + "OptionText" + option.index
            text: control.textAt(option.index)
            color: option.highlighted ? Theme.popupTextHighlighted : Theme.popupText
            font.family: control.font.family
            font.pixelSize: control.font.pixelSize
            font.weight: control.currentIndex === option.index ? Font.DemiBold : Font.Normal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: 8
        }
        background: Rectangle {
            objectName: control.objectName + "OptionBackground" + option.index
            radius: 7
            color: option.highlighted ? Theme.popupHighlight : option.hovered ? Theme.popupHover : Theme.popupBackground
        }
    }
    popup: Popup {
        id: popupRoot
        objectName: control.objectName + "Popup"
        readonly property real popupHeight: Math.min(contentItem.implicitHeight + 12, 420)
        // 下方空间不足时向上展开，避免小窗口里弹层被窗口底边裁切。
        y: {
            const margin = 5
            const sceneBottom = control.mapToItem(null, 0, control.height).y
            const availableBelow = (control.Window ? control.Window.height : 720) - sceneBottom
            return availableBelow >= popupHeight + margin + 8 ? control.height + margin
                                                              : control.height - popupHeight - margin
        }
        width: control.width
        height: popupHeight
        padding: 6
        margins: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle {
            objectName: control.objectName + "PopupBackground"
            radius: 12; color: Theme.popupBackground; border.color: Theme.popupBorder; border.width: 1
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                id: popupScroll
                policy: ScrollBar.AsNeeded
                contentItem: Rectangle {
                    implicitWidth: 6; implicitHeight: 36; radius: 3
                    color: popupScroll.pressed ? Theme.accent : Theme.popupScrollHandle
                }
                background: Rectangle { implicitWidth: 6; radius: 3; color: Theme.popupScrollTrack }
            }
        }
    }
}
