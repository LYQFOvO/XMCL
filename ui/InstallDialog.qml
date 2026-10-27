import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: dialog
    objectName: "installDialog"
    property var app
    property var versions: []
    property var loaderVersions: []
    property string selectedVersion: ""
    property string loaderId: "vanilla"
    property bool versionsLoading: false
    property bool loadersLoading: false
    property bool submitting: false
    property bool nameEdited: false
    property string error: ""
    readonly property var visibleVersions: versions.filter(v =>
        (releaseType.currentValue === "all" || v.type === releaseType.currentValue)
        && v.id.toLowerCase().includes(search.text.trim().toLowerCase()))
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 70, 980)
    height: Math.min(parent.height - 60, 680)
    padding: 26
    modal: true; focus: true
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#b005130e" }
    background: Rectangle { radius: 25; color: "#162a20"; border.color: "#567561" }
    function showInstaller() {
        error = ""; submitting = false
        open()
        if (!versions.length && !versionsLoading) refreshVersions()
    }
    function refreshVersions() {
        versionsLoading = true; error = ""
        features.execute("game", "version.list", {type: "all"})
    }
    function selectVersion(value) {
        selectedVersion = value
        updateName()
        refreshLoaders()
    }
    function updateName() {
        if (!nameEdited) instanceName.text = selectedVersion + (loaderId === "vanilla" ? "" : " · " + loaderChoice.currentText)
    }
    function refreshLoaders() {
        loaderVersions = []; error = ""
        loadersLoading = loaderId !== "vanilla" && selectedVersion.length > 0
        if (loadersLoading) features.execute("game", "loader.list", {version: selectedVersion, loader: loaderId})
    }
    function install() {
        submitting = true; error = ""
        features.execute("game", "game.install", {
            createInstance: true, name: instanceName.text.trim(),
            version: selectedVersion, loader: loaderId,
            loaderVersion: loaderId === "vanilla" ? "" : loaderVersionChoice.currentValue
        })
    }
    Connections {
        target: features
        function onCompleted(module, action, data) {
            if (module !== "game") return
            if (action === "version.list" && dialog.versionsLoading) {
                dialog.versionsLoading = false; dialog.versions = data.items || []
                if (!dialog.selectedVersion && dialog.versions.length) {
                    const first = dialog.versions.find(v => v.type === "release") || dialog.versions[0]
                    dialog.selectVersion(first.id)
                }
            } else if (action === "loader.list" && dialog.loadersLoading && data.version === dialog.selectedVersion && data.loader === dialog.loaderId) {
                dialog.loadersLoading = false; dialog.loaderVersions = data.items || []
                loaderVersionChoice.currentIndex = dialog.loaderVersions.length ? 0 : -1
                if (!dialog.loaderVersions.length) dialog.error = "这个 Minecraft 版本没有可用的加载器，请选择其他版本。"
            } else if (action === "game.queued" && dialog.submitting) {
                dialog.submitting = false; dialog.close(); dialog.app.currentPage = 2
            }
        }
        function onFailed(module, action, message) {
            if (module !== "game") return
            if (action === "version.list" && dialog.versionsLoading) { dialog.versionsLoading = false; dialog.error = "获取版本失败：" + message }
            else if (action === "loader.list" && dialog.loadersLoading) { dialog.loadersLoading = false; dialog.error = "获取加载器失败：" + message }
            else if (action === "game.install" && dialog.submitting) { dialog.submitting = false; dialog.error = message }
        }
    }
    contentItem: ColumnLayout {
        spacing: 18
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout { spacing: 6
                Text { text: "安装你的下一场冒险"; color: Theme.text; font.pixelSize: 23; font.weight: Font.DemiBold }
                Text { text: "选择版本和玩法，其余交给 XMCL。"; color: Theme.muted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: "返回"; iconName: "close"; compact: true; onClicked: dialog.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#28ffffff" }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 24
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    TextField {
                        id: search; objectName: "installVersionSearch"; Layout.fillWidth: true; implicitHeight: 42
                        placeholderText: "搜索版本，如 1.21.1"; placeholderTextColor: "#a2b4a8"; color: Theme.text
                        leftPadding: 12; selectByMouse: true
                        background: Rectangle { radius: 10; color: "#0e2017"; border.color: search.activeFocus ? Theme.accent : "#52715d" }
                    }
                    GlassComboBox {
                        id: releaseType; objectName: "installReleaseType"; Layout.preferredWidth: 115
                        textRole: "label"; valueRole: "id"
                        model: [{id:"release",label:"正式版"},{id:"snapshot",label:"快照版"},{id:"all",label:"全部版本"}]
                    }
                }
                ListView {
                    id: versionList; objectName: "installVersionList"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 6; model: dialog.visibleVersions
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Button {
                        required property var modelData
                        required property int index
                        objectName: "installVersionRow" + index
                        width: ListView.view.width; height: 58; hoverEnabled: true
                        background: Rectangle { radius: 11; color: dialog.selectedVersion === modelData.id ? "#31553e" : parent.hovered ? "#284532" : "#1b3325"; border.color: dialog.selectedVersion === modelData.id ? "#a5dfb4" : "#36513f" }
                        contentItem: RowLayout {
                            ColumnLayout { Layout.fillWidth: true; spacing: 5
                                Text { text: modelData.id; color: "#f1f7f2"; font.pixelSize: 14; font.weight: Font.DemiBold }
                                Text { text: modelData.subtitle; color: "#b2c5b8"; font.pixelSize: 10 }
                            }
                            Icon { name: "check"; color: Theme.accent; width: 18; height: 18; visible: dialog.selectedVersion === modelData.id }
                        }
                        onClicked: dialog.selectVersion(modelData.id)
                    }
                    Text {
                        anchors.centerIn: parent; width: parent.width - 24
                        visible: dialog.versionsLoading || versionList.count === 0
                        text: dialog.versionsLoading ? "正在获取官方版本列表…" : "没有匹配的版本"
                        color: Theme.muted; font.pixelSize: 13; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                    }
                }
                ActionButton { objectName: "refreshInstallVersions"; text: "刷新版本列表"; iconName: "download"; compact: true; enabled: !dialog.versionsLoading; onClicked: dialog.refreshVersions() }
            }
            Rectangle { Layout.fillHeight: true; width: 1; color: "#28ffffff" }
            ColumnLayout {
                Layout.preferredWidth: 310; Layout.maximumWidth: 310; Layout.fillHeight: true; spacing: 11
                Text { text: "Minecraft " + (dialog.selectedVersion || "请选择版本"); color: Theme.accent; font.pixelSize: 18; font.weight: Font.DemiBold }
                Text { text: "实例名称"; color: Theme.muted; font.pixelSize: 12; Layout.topMargin: 6 }
                TextField {
                    id: instanceName; objectName: "installInstanceName"; Layout.fillWidth: true; implicitHeight: 42
                    color: Theme.text; leftPadding: 12; maximumLength: 100; selectByMouse: true
                    onTextEdited: dialog.nameEdited = true
                    background: Rectangle { radius: 10; color: "#0e2017"; border.color: instanceName.activeFocus ? Theme.accent : "#52715d" }
                }
                Text { text: "玩法 / 加载器"; color: Theme.muted; font.pixelSize: 12; Layout.topMargin: 5 }
                GlassComboBox {
                    id: loaderChoice; objectName: "installLoader"; Layout.fillWidth: true
                    textRole: "label"; valueRole: "id"
                    model: [{id:"vanilla",label:"原版 · 不安装模组加载器"},{id:"fabric",label:"Fabric"},{id:"forge",label:"Forge"},{id:"neoforge",label:"NeoForge"},{id:"quilt",label:"Quilt"}]
                    onActivated: { dialog.loaderId = currentValue; dialog.updateName(); dialog.refreshLoaders() }
                }
                Text { visible: dialog.loaderId !== "vanilla"; text: "加载器版本"; color: Theme.muted; font.pixelSize: 12; Layout.topMargin: 5 }
                GlassComboBox {
                    id: loaderVersionChoice; objectName: "installLoaderVersion"; Layout.fillWidth: true
                    visible: dialog.loaderId !== "vanilla"; enabled: !dialog.loadersLoading && count > 0
                    model: dialog.loaderVersions; textRole: "title"; valueRole: "loaderVersion"
                    displayText: dialog.loadersLoading ? "正在查询兼容版本…" : count ? currentText : "暂无兼容版本"
                }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "将创建独立实例，自动下载客户端、运行库和游戏资源。完成后可在首页选择并启动。"; color: Theme.muted; font.pixelSize: 12; lineHeight: 1.45; Layout.topMargin: 6 }
                Text { visible: dialog.loaderId === "forge" || dialog.loaderId === "neoforge"; Layout.fillWidth: true; wrapMode: Text.Wrap; text: "此加载器安装需要本机 Java，可在实例启动配置中指定。"; color: Theme.muted; font.pixelSize: 11 }
                Item { Layout.fillHeight: true }
            }
        }
        Text { objectName: "installError"; visible: dialog.error.length > 0; text: dialog.error; color: "#ffb6a9"; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12; maximumLineCount: 3; elide: Text.ElideRight }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "下载与校验进度会显示在下载页。"; color: Theme.muted; font.pixelSize: 11; Layout.fillWidth: true }
            Tag { objectName: "installDownloadSource"; text: features.appearance.downloadSource === "bmclapi" ? "下载源：BMCLAPI 镜像（bangbang93）" : "下载源：官方源"; textColor: features.appearance.downloadSource === "bmclapi" ? Theme.accent : Theme.subtle; implicitHeight: 22 }
            ActionButton {
                objectName: "confirmGameInstall"; text: dialog.submitting ? "正在创建…" : "开始下载"; iconName: "download"; primary: true
                enabled: !dialog.submitting && !dialog.versionsLoading && !dialog.loadersLoading && dialog.selectedVersion.length > 0 && instanceName.text.trim().length > 0 && (dialog.loaderId === "vanilla" || loaderVersionChoice.currentIndex >= 0)
                opacity: enabled ? 1 : 0.45
                onClicked: dialog.install()
            }
        }
    }
}
