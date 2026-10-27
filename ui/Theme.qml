pragma Singleton
import QtQuick
QtObject {
    readonly property color text: "#f1f5ef"
    readonly property color muted: "#a6b5ac"
    readonly property color subtle: "#788e82"
    readonly property color accent: features.appearance.accentColor || "#b7efc5"
    readonly property color ink: "#123b2b"
    readonly property string font: Qt.platform.os === "osx" ? "PingFang SC" : "Microsoft YaHei"

    // 输入控件（TextField / ComboBox）与下拉弹层。深色森林主题里必须显式区分
    // 文字、底色与高亮，否则 Qt Basic 样式会退化成深色文字叠深色背景。
    readonly property color fieldBackground: "#203d2c"
    readonly property color fieldBackgroundDown: "#2e513b"
    readonly property color fieldBackgroundDisabled: "#1b2e23"
    readonly property color fieldBorder: "#52715d"
    readonly property color fieldFocusRing: "#b7efc5"
    readonly property color fieldText: "#f1f7f2"
    readonly property color fieldTextDisabled: "#a2b4a8"
    readonly property color popupBackground: "#14291e"
    readonly property color popupBorder: "#65856e"
    readonly property color popupHighlight: "#3a654b"
    readonly property color popupHover: "#2b4937"
    readonly property color popupText: "#f1f7f2"
    readonly property color popupTextHighlighted: "#ffffff"
    readonly property color popupScrollHandle: "#7fb894"
    readonly property color popupScrollTrack: "#1a3a28"
}
