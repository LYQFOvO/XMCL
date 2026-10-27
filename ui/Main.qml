import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    width: 1320; height: 860
    minimumWidth: 1060; minimumHeight: 720
    visible: true
    title: "XMCL · 让热爱，即刻启程"
    color: "#0c1c16"
    font.family: Theme.font
    property int currentPage: 0
    readonly property var pageNames: ["首页", "游戏实例", "下载管理", "设置"]
    function showMessage(heading, message) {
        information.heading = heading
        information.message = message
        information.open()
    }
    function requestLaunch() {
        if (launcher.currentInstance.id) {
            launchCheckDialog.showFor(launcher.currentInstance)
            return
        }
        showMessage("准备好下一场冒险", "当前选择：" + launcher.currentInstance.name + " · Minecraft " + launcher.currentInstance.version
                    + "\n\n当前展示的是示例实例，不会启动真实游戏。请在游戏实例页创建实例、安装版本并选择账号。")
    }
    function openFeatures(module, values) { featurePanel.showModule(module, values || ({})) }
    readonly property bool accountLoggedIn: !!features.accountName
    Shortcut { sequence: features.appearance.shortcuts.features; onActivated: window.openFeatures("instances", {}) }
    function openInstaller() { installDialog.showInstaller() }
    function openCommunity() { communityDialog.show() }
    function openInstanceSettings(instance) {
        if (!instance || !instance.id) {
            showMessage("示例实例", "示例配置没有真实版本和启动配置。请先创建实例并安装游戏版本，再为它单独设置 Java 与内存。")
            return
        }
        instanceSettingsDialog.show(instance)
    }
    InstallDialog { id: installDialog; app: window }
    CommunityDialog { id: communityDialog; app: window }
    InstanceSettingsDialog { id: instanceSettingsDialog; app: window }
    LaunchCheckDialog { id: launchCheckDialog; app: window }
    FeaturePanel { id: featurePanel; objectName: "featurePanel" }
    Connections {
        target: features
        function onFailed(module, action, message) {
            if (!featurePanel.opened && !installDialog.opened && !communityDialog.opened && !instanceSettingsDialog.opened && !launchCheckDialog.opened && !(module === "game" && action === "game.install") && !(module === "community" && action === "community.install")) window.showMessage("操作未完成", message)
        }
        function onCompleted(module, action, data) {
            if (!featurePanel.opened && action === "game.launch") window.showMessage("游戏启动", data.message)
        }
    }
    Shortcut { sequence: features.appearance.shortcuts.home; onActivated: window.currentPage = 0 }
    Shortcut { sequence: features.appearance.shortcuts.library; onActivated: window.currentPage = 1 }
    Shortcut { sequence: features.appearance.shortcuts.downloads; onActivated: window.currentPage = 2 }
    Shortcut { sequence: features.appearance.shortcuts.settings; onActivated: window.currentPage = 3 }

    Item {
        id: scene
        anchors.fill: parent
        Image { anchors.fill: parent; source: features.appearance.backgroundUrl || "qrc:/assets/forest.png"; fillMode: Image.PreserveAspectCrop }
        Rectangle { anchors.fill: parent; color: "#b6091b15" }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#a60b1b15" }
                GradientStop { position: 1; color: "#3410201a" }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: "#00101b14" }
                GradientStop { position: 1; color: "#d90a1712" }
            }
        }
    }

    GlassPanel {
        id: sidebar
        x: 18; y: 18; width: 198; height: parent.height - 36
        radius: 24; backdrop: scene
        tint: Qt.rgba(0.07, 0.12, 0.09, 0.42)
        Row {
            x: 23; y: 31; spacing: 12
            Rectangle {
                width: 37; height: 39; color: "transparent"
                Icon { anchors.fill: parent; name: "grass"; color: Theme.accent }
            }
            Column {
                spacing: 1
                Text { text: "XMCL"; color: Theme.text; font.pixelSize: 25; font.weight: Font.Bold; font.letterSpacing: 3 }
                Text { text: "让热爱，即刻启程"; color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 1 }
            }
        }
        Text { x: 25; y: 118; text: "工作台"; color: Theme.subtle; font.pixelSize: 11; font.letterSpacing: 2 }
        Column {
            x: 12; y: 146; width: parent.width - 24; spacing: 8
            Repeater {
                model: [{name: "首页", icon: "home"}, {name: "游戏实例", icon: "grid"}, {name: "下载管理", icon: "download"}, {name: "设置", icon: "settings"}]
                delegate: Button {
                    id: nav
                    required property var modelData
                    required property int index
                    objectName: "nav" + index
                    width: parent.width; height: 48
                    hoverEnabled: true
                    activeFocusOnTab: true
                    property bool selected: window.currentPage === index
                    Accessible.name: modelData.name
                    background: Rectangle {
                        radius: 13
                        color: nav.selected ? "#29b7efc5" : nav.hovered ? "#13ffffff" : "transparent"
                        border.width: 1
                        border.color: nav.activeFocus ? Theme.accent : nav.selected ? "#33c4f6d1" : "transparent"
                        Behavior on color { ColorAnimation { duration: launcher.animationsEnabled ? 160 : 0 } }
                        Rectangle { x: 1; width: 3; height: 17; radius: 2; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent; visible: nav.selected }
                    }
                    contentItem: Row {
                        leftPadding: 17; spacing: 13
                        Icon { anchors.verticalCenter: parent.verticalCenter; width: 19; height: 19; name: nav.modelData.icon; color: nav.selected ? Theme.accent : Theme.muted }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: nav.modelData.name; color: nav.selected ? Theme.text : Theme.muted; font.pixelSize: 13; font.weight: nav.selected ? Font.DemiBold : Font.Normal }
                    }
                    onClicked: window.currentPage = index
                }
            }
        }
        Rectangle {
            x: 16; width: parent.width - 32; height: 91; anchors.bottom: about.top; anchors.bottomMargin: 20
            radius: 15; color: "#08ffffff"; border.color: "#14ffffff"
            Icon { x: 15; y: 16; width: 17; height: 17; name: "leaf"; color: Theme.accent }
            Text { x: 42; y: 16; text: "慢一点，也没关系"; color: "#c8d8ca"; font.pixelSize: 11 }
            Text { x: 15; y: 43; text: "你的世界，一直在等你。"; color: Theme.subtle; font.pixelSize: 10 }
            Text { x: 15; y: 62; text: "MAKE ROOM FOR PLAY."; color: "#6c8c79"; font.pixelSize: 8; font.letterSpacing: 1.2 }
        }
        Button {
            id: about
            x: 16; width: parent.width - 32; height: 40; anchors.bottom: parent.bottom; anchors.bottomMargin: 15
            hoverEnabled: true; activeFocusOnTab: true; Accessible.name: "关于 XMCL"
            background: Rectangle { radius: 10; color: about.hovered ? "#10ffffff" : "transparent"; border.color: about.activeFocus ? Theme.accent : "transparent" }
            contentItem: Row {
                spacing: 10; leftPadding: 10
                Icon { name: "info"; width: 16; height: 16; color: Theme.subtle; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "关于 XMCL"; color: Theme.muted; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
            }
            onClicked: window.showMessage("XMCL · 让热爱，即刻启程", "一个更有人情味的 Minecraft 启动器。\n\n版本 0.2.0 · Native Preview\nC++ / Qt Quick\n\n通过原生功能面板管理实例、账号、资源与联机；部分协议仍在移植中。XMCL 为独立项目，与 Mojang 或 Microsoft 无隶属关系。")
        }
    }

    Row {
        x: 245; y: 32; height: 28; spacing: 12
        Icon { name: "home"; width: 15; height: 15; color: Theme.subtle; anchors.verticalCenter: parent.verticalCenter }
        Text { text: "工作台"; color: Theme.subtle; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
        Text { text: "/"; color: "#52675a"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
        Text { text: window.pageNames[window.currentPage]; color: Theme.muted; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 29; y: 23; spacing: 14
        Tag { text: "NATIVE PREVIEW"; textColor: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
        Button {
            id: account
            objectName: "accountButton"
            width: 152; height: 43; hoverEnabled: true; activeFocusOnTab: true
            Accessible.name: features.accountName || "账号，未登录"
            background: Rectangle { radius: 14; color: account.hovered ? "#24ffffff" : "#10ffffff"; border.color: account.activeFocus ? Theme.accent : "#18ffffff" }
            contentItem: Row {
                spacing: 9; leftPadding: 5
                Rectangle {
                    width: 30; height: 30; radius: 9; color: "#344e37"; anchors.verticalCenter: parent.verticalCenter
                    Icon { anchors.centerIn: parent; width: 18; height: 18; name: "user"; color: "#b9d9a4" }
                }
                Column {
                    spacing: 1; anchors.verticalCenter: parent.verticalCenter
                    Text { text: features.accountName || "探险家"; color: Theme.text; font.pixelSize: 11 }
                    Text { text: features.accountName ? "当前游戏账号" : "点击登录正版账号"; color: Theme.subtle; font.pixelSize: 8 }
                }
                Icon { width: 13; height: 13; name: "chevron-down"; color: Theme.muted; anchors.verticalCenter: parent.verticalCenter }
            }
            onClicked: window.openFeatures("accounts", window.accountLoggedIn ? {actionId: "account.list"} : {actionId: "microsoft.login"})
        }
    }
    StackLayout {
        id: pages
        x: 245; y: 96; width: parent.width - x - 29; height: parent.height - y - 47
        currentIndex: window.currentPage
        HomePage { app: window; backdrop: scene }
        LibraryPage { app: window; backdrop: scene }
        DownloadsPage { app: window; backdrop: scene }
        SettingsPage { app: window; backdrop: scene }
    }
    Row {
        x: 247; anchors.bottom: parent.bottom; anchors.bottomMargin: 19; spacing: 7
        Rectangle { width: 5; height: 5; radius: 2.5; color: "#759f80"; anchors.verticalCenter: parent.verticalCenter }
        Text { text: "为每一种玩法，留一片天地。"; color: "#6e8c79"; font.pixelSize: 10 }
    }
    Text { anchors.right: parent.right; anchors.rightMargin: 30; anchors.bottom: parent.bottom; anchors.bottomMargin: 19; text: "XMCL  0.2.0   /   原生预览"; color: "#6e8c79"; font.pixelSize: 9; font.letterSpacing: 0.4 }

    Popup {
        id: information
        objectName: "informationDialog"
        property string heading: ""
        property string message: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 470; padding: 29
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Overlay.modal: Rectangle { color: "#ab06100c" }
        background: Rectangle { radius: 24; color: "#1c3026"; border.color: "#58715e" }
        contentItem: Column {
            spacing: 20
            Icon { name: "leaf"; color: Theme.accent; width: 29; height: 29 }
            Text { text: information.heading; color: Theme.text; font.pixelSize: 22; font.weight: Font.DemiBold; width: parent.width; wrapMode: Text.WordWrap }
            Text { text: information.message; color: Theme.muted; font.pixelSize: 13; lineHeight: 1.55; width: parent.width; wrapMode: Text.WordWrap }
            ActionButton { text: "知道了"; primary: true; anchors.right: parent.right; onClicked: information.close() }
        }
    }
}
