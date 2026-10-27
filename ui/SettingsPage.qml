import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    property var app
    property Item backdrop
    readonly property string downloadSource: features.appearance.downloadSource || "official"
    function chooseDownloadSource(source) {
        features.execute("personalization", "personalization.downloadSource", {source: source})
    }
    Flickable {
        id: settingsFlickable
        objectName: "settingsFlickable"
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: settingsContent.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            id: settingsScrollBar
            objectName: "settingsScrollBar"
            parent: settingsFlickable
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            policy: ScrollBar.AsNeeded
            contentItem: Rectangle {
                implicitWidth: 6; implicitHeight: 36; radius: 3
                color: settingsScrollBar.pressed ? Theme.accent : Theme.popupScrollHandle
            }
            background: Rectangle { implicitWidth: 6; radius: 3; color: Theme.popupScrollTrack }
        }
        ColumnLayout {
            id: settingsContent
            width: settingsFlickable.width
            spacing: 20
            RowLayout {
                Layout.fillWidth: true
                Column { spacing: 8
                    Text { text: "舒服一点，尽兴一点。"; color: Theme.text; font.pixelSize: 29; font.weight: Font.DemiBold }
                    Text { text: "让启动器，成为你喜欢的样子。"; color: Theme.muted; font.pixelSize: 12 }
                }
                Item { Layout.fillWidth: true }
                Row { spacing: 7
                    Icon { name: "check"; width: 15; height: 15; color: Theme.accent }
                    Text { text: "偏好自动保存"; color: Theme.accent; font.pixelSize: 11 }
                }
            }
            GlassPanel {
                Layout.fillWidth: true; Layout.preferredHeight: 225; backdrop: page.backdrop
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 25; spacing: 19
                    Row { spacing: 11
                        Icon { name: "sparkles"; width: 20; height: 20; color: Theme.accent }
                        Text { text: "外观与体验";
                            TapHandler { onTapped: page.app.openFeatures("personalization", {}) } color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#18ffffff" }
                    RowLayout { Layout.fillWidth: true
                        Column { spacing: 7
                            Text { text: "液态玻璃"; color: Theme.text; font.pixelSize: 13 }
                            Text { text: "背景模糊、柔和折射与边缘高光；关闭后使用纯色面板。"; color: Theme.muted; font.pixelSize: 11 }
                        }
                        Item { Layout.fillWidth: true }
                        Toggle { objectName: "effectsToggle"; checked: launcher.effectsEnabled; Accessible.name: "液态玻璃"; onToggled: launcher.effectsEnabled = checked }
                    }
                    RowLayout { Layout.fillWidth: true
                        Column { spacing: 7
                            Text { text: "界面动画"; color: Theme.text; font.pixelSize: 13 }
                            Text { text: "按钮与控件的轻柔过渡，让每一次操作更自然。"; color: Theme.muted; font.pixelSize: 11 }
                        }
                        Item { Layout.fillWidth: true }
                        Toggle { objectName: "animationsToggle"; checked: launcher.animationsEnabled; Accessible.name: "界面动画"; onToggled: launcher.animationsEnabled = checked }
                    }
                }
            }
            GlassPanel {
                Layout.fillWidth: true; Layout.preferredHeight: 398; backdrop: page.backdrop
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 25; spacing: 14
                    RowLayout { Layout.fillWidth: true
                        Icon { name: "cpu"; width: 20; height: 20; color: Theme.accent }
                        Text { text: "游戏与下载"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.leftMargin: 4 }
                        Text { text: "默认设置"; color: Theme.subtle; font.pixelSize: 10; Layout.leftMargin: 8 }
                        Item { Layout.fillWidth: true }
                        Text { objectName: "memoryValue"; text: launcher.memoryGb + " GB"; color: Theme.accent; font.pixelSize: 21; font.weight: Font.DemiBold }
                    }
                    Text { text: "未单独设置的实例使用此默认内存；每个实例都可以在自己的「实例设置」里单独覆盖 Java、内存与参数。"; color: Theme.muted; font.pixelSize: 11 }
                    Slider {
                        id: memory; objectName: "memorySlider"
                        Layout.fillWidth: true; from: 2; to: launcher.maxMemoryGb; stepSize: 1; value: launcher.memoryGb
                        snapMode: Slider.SnapAlways; activeFocusOnTab: true
                        Accessible.name: "游戏内存 GB"
                        onMoved: launcher.memoryGb = Math.round(value)
                        background: Rectangle {
                            x: memory.leftPadding; y: memory.topPadding + memory.availableHeight / 2 - height / 2
                            width: memory.availableWidth; height: 5; radius: 2.5; color: "#334b3d"
                            Rectangle { width: memory.visualPosition * parent.width; height: parent.height; radius: 2.5; color: Theme.accent }
                        }
                        handle: Rectangle {
                            x: memory.leftPadding + memory.visualPosition * (memory.availableWidth - width)
                            y: memory.topPadding + memory.availableHeight / 2 - height / 2
                            width: 20; height: 20; radius: 10; color: "#def7e1"; border.width: memory.activeFocus ? 3 : 1; border.color: "#779a7c"
                        }
                    }
                    RowLayout { Layout.fillWidth: true
                        Text { text: "2 GB"; color: Theme.subtle; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: "请根据实际设备与模组需求设置"; color: Theme.subtle; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: launcher.maxMemoryGb + " GB"; color: Theme.subtle; font.pixelSize: 10 }
                    }
                    RowLayout { Layout.fillWidth: true
                        Text {
                            objectName: "globalMemoryAdvice"; Layout.fillWidth: true; color: Theme.subtle; font.pixelSize: 10
                            text: launcher.totalMemoryGb > 0 ? "本机物理内存 " + launcher.totalMemoryGb + " GB · 智能建议 " + launcher.suggestedMemoryGb + " GB" : "无法检测物理内存，请手动选择"
                        }
                        ActionButton { objectName: "smartMemoryGlobal"; text: "智能分配"; iconName: "sparkles"; compact: true; implicitHeight: 26; onClicked: launcher.memoryGb = launcher.suggestedMemoryGb }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#18ffffff"; Layout.topMargin: 4 }
                    RowLayout { Layout.fillWidth: true
                        Column { spacing: 6
                            Row { spacing: 9
                                Icon { name: "download"; width: 16; height: 16; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "下载源"; color: Theme.text; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                            }
                            Text { text: "官方源直连上游；网络受限时可切换到 BMCLAPI 镜像加速，失败自动回退官方源。"; color: Theme.muted; font.pixelSize: 11 }
                        }
                        Item { Layout.fillWidth: true }
                        Row { spacing: 8
                            ActionButton {
                                objectName: "downloadSourceOfficial"
                                text: "官方源"; compact: true
                                primary: page.downloadSource === "official"
                                onClicked: page.chooseDownloadSource("official")
                            }
                            ActionButton {
                                objectName: "downloadSourceBmclapi"
                                text: "BMCLAPI 镜像"; compact: true
                                primary: page.downloadSource === "bmclapi"
                                onClicked: page.chooseDownloadSource("bmclapi")
                            }
                        }
                    }
                    Text {
                        objectName: "downloadSourceInfo"
                        Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10
                        color: page.downloadSource === "bmclapi" ? Theme.accent : Theme.subtle
                        text: page.downloadSource === "bmclapi"
                              ? "当前使用 BMCLAPI 镜像（由 bangbang93 提供，https://bmclapidoc.bangbang93.com/）：Mojang/Fabric/Forge/NeoForge 等文件经镜像下载，镜像不可用时会自动回退官方源；Quilt、社区资源与整合包仍使用官方源，所有文件仍按官方摘要校验。下载界面会标明来源。"
                              : "当前使用官方源：直接连接 Mojang、Fabric、Quilt、Forge、NeoForge 与 authlib-injector 官方地址。"
                    }
                }
            }
            RowLayout { Layout.fillWidth: true; Layout.leftMargin: 4
                Icon { name: "info"; width: 15; height: 15; color: Theme.subtle }
                Text { text: "XMCL 0.2.0  ·  简体中文  ·  深色森林"; color: Theme.subtle; font.pixelSize: 10; Layout.leftMargin: 4 }
                Item { Layout.fillWidth: true }
                ActionButton { text: "关于 XMCL"; compact: true; onClicked: page.app.showMessage("给每一位探险家的启动器", "XMCL 0.2.0 · Native Preview\n\n以 C++ 和 Qt Quick 构建。\n你的视觉、内存与下载源偏好保存在本机。\n\n原生功能面板：Ctrl+Shift+M。\n微软登录使用内置公共客户端；联机需要配置自己的服务。") }
            }
            Item { Layout.preferredHeight: 6 }
        }
    }
}
