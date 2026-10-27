import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 社区资源下载：搜索 Modrinth / CurseForge，选择文件后直接写入指定实例。
// 高级依赖/覆盖操作仍在功能面板的社区资源模块中保留。
Popup {
    id: dialog
    objectName: "communityDialog"
    property var app
    property string provider: "modrinth"
    property string kind: "mod"
    property var searchResults: []
    property var versionResults: []
    property string selectedProjectId: ""
    property string selectedProjectTitle: ""
    property string selectedVersionId: ""
    property string gameVersion: ""
    property string loader: ""
    property string status: ""
    property bool busy: false
    property bool error: false
    property string keyStatus: ""
    readonly property string targetDirectory: kind === "resourcepack" ? "resourcepacks" : kind === "shader" ? "shaderpacks" : kind === "datapack" ? "datapacks" : "mods"
    readonly property string chosenInstanceId: {
        if (instanceChoice.currentIndex < 0 || instanceChoice.currentIndex >= features.instances.length) return ""
        return features.instances[instanceChoice.currentIndex].id || ""
    }
    readonly property string chosenInstanceTitle: {
        if (instanceChoice.currentIndex < 0 || instanceChoice.currentIndex >= features.instances.length) return ""
        return features.instances[instanceChoice.currentIndex].title || ""
    }
    readonly property var communityTask: {
        const tasks = features.downloadTasks
        for (let i = 0; i < tasks.length; ++i) if (tasks[i].module === "community") return tasks[i]
        return null
    }
    readonly property bool taskRunning: !!communityTask && communityTask.active
    readonly property bool taskComplete: !!communityTask && communityTask.phase === "complete" && !communityTask.active
    readonly property bool taskFailed: !!communityTask && !!communityTask.error && !communityTask.active
    readonly property real taskProgress: {
        if (taskComplete) return 1
        if (!communityTask || !communityTask.active) return 0
        if (communityTask.phase === "metadata") return 0
        const total = Math.max(1, communityTask.totalFiles || 1)
        return Math.max(0, Math.min(1, ((communityTask.completedFiles || 0) + (communityTask.totalBytes > 0 ? (communityTask.bytes || 0) / communityTask.totalBytes : 0)) / total))
    }
    readonly property var kindModel: [
        {id: "mod", label: "模组 · mods"},
        {id: "resourcepack", label: "资源包 · resourcepacks"},
        {id: "shader", label: "光影 · shaderpacks"},
        {id: "datapack", label: "数据包 · datapacks"}
    ]
    readonly property var providerModel: [
        {id: "modrinth", label: "Modrinth"},
        {id: "curseforge", label: "CurseForge"}
    ]
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 70, 1060)
    height: Math.min(parent.height - 60, 720)
    padding: 26
    modal: true; focus: true
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#b005130e" }
    background: Rectangle { radius: 25; color: "#162a20"; border.color: "#567561" }
    function show() {
        resetStatus()
        searchResults = []; versionResults = []; selectedProjectId = ""; selectedProjectTitle = ""; selectedVersionId = ""
        pickInstance()
        open()
    }
    function resetStatus() {
        busy = false; error = false; status = ""; keyStatus = ""
    }
    function pickInstance() {
        let wanted = launcher.currentInstance.id
        let index = -1
        for (let i = 0; i < features.instances.length; ++i) if (features.instances[i].id === wanted) index = i
        if (index < 0 && features.instances.length > 0) index = 0
        instanceChoice.currentIndex = index
        applyInstance()
    }
    function applyInstance() {
        const list = features.instances
        if (instanceChoice.currentIndex < 0 || instanceChoice.currentIndex >= list.length) {
            gameVersion = ""; loader = ""
        } else {
            gameVersion = list[instanceChoice.currentIndex].version || ""
            loader = String(list[instanceChoice.currentIndex].loader || "vanilla").toLowerCase()
        }
    }
    function search() {
        const query = searchField.text.trim()
        if (!query.length) { error = true; status = "请输入要搜索的资源关键词。"; return }
        resetStatus(); busy = true; status = "正在搜索 " + (provider === "curseforge" ? "CurseForge" : "Modrinth") + "…"
        searchResults = []; versionResults = []; selectedProjectId = ""; selectedProjectTitle = ""; selectedVersionId = ""
        features.execute("community", "community.search", {
            provider: provider, query: query, kind: kind, gameVersion: gameVersion, loader: loader
        })
    }
    function selectProject(row) {
        selectedProjectId = row.projectId || row.id || ""
        selectedProjectTitle = row.title || row.name || selectedProjectId
        selectedVersionId = ""; versionResults = []
        if (!selectedProjectId.length) return
        busy = true; error = false; status = "正在读取「" + selectedProjectTitle + "」的文件列表…"
        features.execute("community", "community.versions", {
            provider: provider, projectId: selectedProjectId, gameVersion: gameVersion, loader: loader
        })
    }
    function selectVersion(row) {
        selectedVersionId = row.versionId || row.id || ""
        status = "已选择文件，点击下方按钮下载并安装。"
        error = false
    }
    function download() {
        if (!features.instances.length) { error = true; status = "请先在游戏实例页创建实例，再下载资源。"; return }
        if (!chosenInstanceId.length) { error = true; status = "请选择要安装资源的游戏实例。"; return }
        if (!selectedProjectId.length || !selectedVersionId.length) { error = true; status = "请先搜索项目并选择一个文件。"; return }
        resetStatus(); busy = true; status = "正在准备下载…"
        features.execute("community", "community.install", {
            provider: provider,
            instanceId: chosenInstanceId,
            projectId: selectedProjectId,
            versionId: selectedVersionId,
            kind: targetDirectory,
            title: selectedProjectTitle
        })
    }
    function saveKey() {
        const key = keyField.text.trim()
        if (!key.length) { error = true; keyStatus = "请输入 CurseForge API Key。"; return }
        features.execute("community", "community.key", {key: key})
    }
    Connections {
        target: features
        function onCompleted(module, action, data) {
            if (module !== "community") return
            if (action === "community.search") {
                dialog.busy = false
                dialog.searchResults = data.items || []
                dialog.error = false
                dialog.status = dialog.searchResults.length ? (data.message || "选择项目查看文件") : "没有找到匹配的资源，换个关键词试试。"
            } else if (action === "community.versions") {
                dialog.busy = false
                dialog.versionResults = data.items || []
                dialog.error = false
                dialog.status = dialog.versionResults.length ? "选择要安装的文件。" : "没有适配当前实例版本与加载器的文件。"
            } else if (action === "community.install") {
                dialog.busy = false
                dialog.error = false
                dialog.status = data.message || "资源已下载并安装。"
            } else if (action === "community.key") {
                dialog.keyStatus = data.message || "密钥已保存。"
            }
        }
        function onFailed(module, action, message) {
            if (module !== "community") return
            if (action === "community.key") { dialog.keyStatus = message; dialog.error = true; return }
            dialog.busy = false
            dialog.error = true
            dialog.status = message
        }
    }
    contentItem: ColumnLayout {
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout { spacing: 6
                Text { text: "下载模组与资源"; color: Theme.text; font.pixelSize: 23; font.weight: Font.DemiBold }
                Text { text: "搜索社区作品，选好文件后直接安装到实例。"; color: Theme.muted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "communityOpenAdvanced"; text: "高级操作"; iconName: "layers"; compact: true; onClicked: { dialog.close(); dialog.app.openFeatures("community", {}) } }
            ActionButton { objectName: "communityClose"; text: "返回"; iconName: "close"; compact: true; onClicked: dialog.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#28ffffff" }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 22
            ColumnLayout {
                Layout.preferredWidth: 440; Layout.maximumWidth: 440; Layout.fillHeight: true; spacing: 11
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    GlassComboBox {
                        id: providerChoice; objectName: "communityProvider"; Layout.preferredWidth: 158
                        textRole: "label"; valueRole: "id"; model: dialog.providerModel
                        currentIndex: 0
                        onActivated: { dialog.provider = currentValue; dialog.searchResults = []; dialog.versionResults = []; dialog.selectedProjectId = ""; dialog.status = ""; dialog.error = false }
                    }
                    GlassComboBox {
                        id: kindChoice; objectName: "communityKind"; Layout.fillWidth: true
                        textRole: "label"; valueRole: "id"; model: dialog.kindModel
                        currentIndex: 0
                        onActivated: { dialog.kind = currentValue; dialog.searchResults = []; dialog.versionResults = []; dialog.selectedProjectId = ""; dialog.status = ""; dialog.error = false }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    TextField {
                        id: searchField; objectName: "communitySearchField"
                        Layout.fillWidth: true; implicitHeight: 42
                        placeholderText: "搜索模组、资源包或光影"; placeholderTextColor: Theme.subtle
                        color: Theme.text; selectionColor: "#46735a"; leftPadding: 12; selectByMouse: true
                        font.pixelSize: 12
                        background: Rectangle { radius: 10; color: "#0e2017"; border.color: searchField.activeFocus ? Theme.accent : Theme.fieldBorder }
                        Keys.onReturnPressed: dialog.search()
                    }
                    ActionButton { objectName: "communitySearchButton"; text: "搜索"; iconName: "search"; compact: true; primary: true; enabled: !dialog.busy; onClicked: dialog.search() }
                }
                Text {
                    Layout.fillWidth: true
                    text: features.instances.length ? ("将安装到：" + (dialog.chosenInstanceId.length ? (dialog.chosenInstanceTitle + " · Minecraft " + dialog.gameVersion) : "请在右侧选择实例")) : "还没有游戏实例，请先在游戏实例页创建实例。"
                    color: features.instances.length ? Theme.muted : "#f3b3a2"
                    font.pixelSize: 10; wrapMode: Text.Wrap
                }
                ListView {
                    id: resultList; objectName: "communityResults"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 7; model: dialog.searchResults
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Button {
                        required property var modelData
                        required property int index
                        objectName: "communityResult" + index
                        width: ListView.view.width; height: 63
                        hoverEnabled: true
                        background: Rectangle {
                            radius: 11
                            color: dialog.selectedProjectId === (modelData.projectId || modelData.id) ? "#31553e" : parent.hovered ? "#284532" : "#1b3325"
                            border.color: dialog.selectedProjectId === (modelData.projectId || modelData.id) ? "#a5dfb4" : "#36513f"
                        }
                        contentItem: Column {
                            leftPadding: 12; rightPadding: 12; topPadding: 9; spacing: 5
                            Text { width: parent.width; text: modelData.title || modelData.name || "项目"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text { width: parent.width; text: modelData.subtitle || ""; color: Theme.muted; font.pixelSize: 10; elide: Text.ElideRight; maximumLineCount: 1 }
                        }
                        onClicked: dialog.selectProject(modelData)
                    }
                    Text {
                        anchors.centerIn: parent; width: parent.width - 30
                        visible: resultList.count === 0
                        text: dialog.busy ? "正在搜索…" : "输入关键词后点击搜索。\n资源下载需要已创建的游戏实例。"
                        color: Theme.subtle; font.pixelSize: 12; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
            Rectangle { Layout.fillHeight: true; width: 1; color: "#28ffffff" }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 11
                Text {
                    Layout.fillWidth: true
                    text: dialog.selectedProjectTitle || "选择左侧项目查看文件"
                    color: dialog.selectedProjectTitle ? Theme.text : Theme.muted
                    font.pixelSize: 17; font.weight: Font.DemiBold; elide: Text.ElideRight
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    ColumnLayout { spacing: 4
                        Text { text: "安装目标实例"; color: Theme.muted; font.pixelSize: 10 }
                        GlassComboBox {
                            id: instanceChoice; objectName: "communityInstance"
                            Layout.preferredWidth: 240
                            textRole: "title"; valueRole: "id"
                            model: features.instances
                            displayText: instanceChoice.currentIndex < 0 ? "请选择实例" : currentText
                            onActivated: dialog.applyInstance()
                        }
                    }
                    ColumnLayout { spacing: 4
                        Text { text: "目标目录"; color: Theme.muted; font.pixelSize: 10 }
                        Rectangle { implicitWidth: 152; implicitHeight: 42; radius: 10; color: Theme.fieldBackground; border.color: Theme.fieldBorder
                            Text { anchors.centerIn: parent; text: dialog.targetDirectory; color: Theme.fieldText; font.pixelSize: 12 }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
                ListView {
                    id: versionList; objectName: "communityVersions"
                    Layout.fillWidth: true; Layout.preferredHeight: 150; Layout.fillHeight: true
                    clip: true; spacing: 6; model: dialog.versionResults
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Button {
                        required property var modelData
                        required property int index
                        objectName: "communityVersion" + index
                        width: ListView.view.width; height: 56
                        hoverEnabled: true
                        background: Rectangle {
                            radius: 10
                            color: dialog.selectedVersionId === (modelData.versionId || modelData.id) ? "#31553e" : parent.hovered ? "#284532" : "#1b3325"
                            border.color: dialog.selectedVersionId === (modelData.versionId || modelData.id) ? "#a5dfb4" : "#36513f"
                        }
                        contentItem: Column {
                            leftPadding: 12; topPadding: 8; spacing: 5
                            Text { width: parent.width - 20; text: modelData.title || modelData.name || modelData.id; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                            Text { width: parent.width - 20; text: modelData.subtitle || ""; color: Theme.muted; font.pixelSize: 10; elide: Text.ElideRight }
                        }
                        onClicked: dialog.selectVersion(modelData)
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    visible: dialog.busy || (dialog.communityTask && (dialog.taskRunning || dialog.taskComplete || dialog.taskFailed))
                    implicitHeight: 52; radius: 11
                    color: dialog.taskFailed ? "#3a2422" : "#1b3325"; border.color: dialog.taskFailed ? "#a46355" : "#36513f"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true; elide: Text.ElideRight
                                text: dialog.taskFailed ? ("下载失败：" + (dialog.communityTask ? dialog.communityTask.error : dialog.status))
                                                        : (dialog.taskRunning || dialog.busy) ? "正在下载社区资源…" : "下载并安装完成"
                                color: dialog.taskFailed ? "#f3b3a2" : Theme.text; font.pixelSize: 11
                            }
                            Text { text: Math.round(dialog.taskProgress * 100) + "%"; color: Theme.accent; font.pixelSize: 11 }
                        }
                        Rectangle {
                            Layout.fillWidth: true; height: 4; radius: 2; color: "#26ffffff"
                            Rectangle { width: parent.width * dialog.taskProgress; height: parent.height; radius: 2; color: Theme.accent }
                        }
                    }
                }
                Text {
                    objectName: "communityStatus"
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: dialog.status
                    color: dialog.error ? "#f3b3a2" : Theme.accent
                    font.pixelSize: 12
                    visible: text.length > 0
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    visible: dialog.provider === "curseforge"
                    TextField {
                        id: keyField; objectName: "communityKeyField"
                        Layout.fillWidth: true; implicitHeight: 38
                        placeholderText: "CurseForge API Key（保存在系统钥匙串）"; placeholderTextColor: Theme.subtle
                        color: Theme.text; echoMode: TextInput.Password; leftPadding: 12; selectByMouse: true
                        background: Rectangle { radius: 10; color: "#0e2017"; border.color: keyField.activeFocus ? Theme.accent : Theme.fieldBorder }
                    }
                    ActionButton { objectName: "communitySaveKey"; text: "保存密钥"; compact: true; onClicked: dialog.saveKey() }
                    Text { text: dialog.keyStatus; color: dialog.error ? "#f3b3a2" : Theme.muted; font.pixelSize: 10 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        objectName: "communitySourceInfo"
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.subtle; font.pixelSize: 10
                        text: "默认只下载所选文件；必需依赖与整合包请用高级操作。社区资源始终使用 Modrinth/CurseForge 官方源"
                              + (features.appearance.downloadSource === "bmclapi" ? "（BMCLAPI 镜像只覆盖游戏与加载器下载）。" : "。")
                    }
                    ActionButton {
                        objectName: "communityDownloadButton"
                        text: dialog.busy ? "正在处理…" : "下载并安装"
                        iconName: "download"; primary: true
                        enabled: !dialog.busy && features.instances.length > 0 && dialog.selectedVersionId.length > 0
                        onClicked: dialog.download()
                    }
                }
            }
        }
    }
}
