import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    property var app
    property Item backdrop
    readonly property bool hasTask: features.downloadTasks.length > 0
    ColumnLayout {
        anchors.fill: parent; spacing: 23
        RowLayout {
            Layout.fillWidth: true
            Column { spacing: 8
                Text { text: "好玩的，正在路上。"; color: Theme.text; font.pixelSize: 29; font.weight: Font.DemiBold }
                Text { text: "在这里，照看每一份期待。"; color: Theme.muted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                objectName: "openGameDownloads"
                text: "下载游戏"
                iconName: "download"; primary: true
                opacity: enabled ? 1 : 0.5
                onClicked: page.app.openInstaller()
            }
        }
        GlassPanel {
            Layout.fillWidth: true; Layout.preferredHeight: 69; backdrop: page.backdrop; radius: 17
            RowLayout { anchors.fill: parent; anchors.margins: 20; spacing: 12
                Icon { name: "info"; color: Theme.accent; width: 19; height: 19 }
                Column { spacing: 5
                    Text { text: "下载与安装任务"; color: Theme.text; font.pixelSize: 12 }
                    Text { text: "支持断点续传和文件校验；关闭窗口前可暂停下载。"; color: Theme.muted; font.pixelSize: 10 }
                }
                Item { Layout.fillWidth: true }
                Tag { objectName: "activeDownloadSource"; text: features.appearance.downloadSource === "bmclapi" ? "下载源：BMCLAPI 镜像" : "下载源：官方源"; textColor: features.appearance.downloadSource === "bmclapi" ? Theme.accent : Theme.subtle; implicitHeight: 22 }
                ActionButton { objectName: "openCommunityDownloads"; text: "模组与资源"; compact: true; onClicked: page.app.openCommunity() }
            }
        }
        GlassPanel {
            visible: !page.hasTask
            Layout.fillWidth: true; Layout.fillHeight: true; backdrop: page.backdrop
            Column {
                anchors.centerIn: parent; spacing: 19
                Rectangle {
                    width: 79; height: 79; radius: 25; color: "#123f6349"; border.color: "#387b9b68"; anchors.horizontalCenter: parent.horizontalCenter
                    Icon { anchors.centerIn: parent; name: "download"; width: 34; height: 34; color: Theme.accent }
                }
                Text { text: "轻装上阵，随时出发"; color: Theme.text; font.pixelSize: 23; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "还没有下载任务。\n下一段旅程所需的一切，将会出现在这里。"; color: Theme.muted; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; lineHeight: 1.7; anchors.horizontalCenter: parent.horizontalCenter }
                ActionButton { objectName: "emptyGameDownload"; text: "选择游戏版本"; iconName: "play"; compact: true; anchors.horizontalCenter: parent.horizontalCenter; onClicked: page.app.openInstaller() }
            }
        }
        ListView {
            visible: page.hasTask
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 16; model: features.downloadTasks
            ScrollBar.vertical: ScrollBar {}
            delegate: GlassPanel {
            id: taskCard
            required property var modelData
            readonly property bool paused: !!modelData.paused
            readonly property bool complete: modelData.phase ? modelData.phase === "complete" : !modelData.active && !modelData.error && modelData.completedFiles === modelData.totalFiles
            readonly property real progress: complete ? 1 : (modelData.completedFiles + (modelData.totalBytes > 0 ? modelData.bytes / modelData.totalBytes : 0)) / Math.max(1, modelData.totalFiles)
            readonly property string stageText: {
                if (modelData.error) return modelData.error
                if (complete) return modelData.module === "game" ? "安装完成 · 可在首页选择并启动" : "文件下载完成"
                if (paused) return "已暂停 · 点击继续恢复下载"
                if (modelData.phase === "metadata") return "正在获取版本信息与资源索引…"
                if (modelData.phase === "loader") return "正在准备加载器…"
                if (modelData.phase === "installing") return "正在运行加载器安装器，请稍候…"
                return "正在下载 · " + (modelData.name || "准备文件")
            }
            width: ListView.view.width; height: 185; backdrop: page.backdrop
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 25; spacing: 18
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle { width: 47; height: 47; radius: 14; color: "#294c36"
                        Icon { anchors.centerIn: parent; name: taskCard.complete ? "check" : "cube"; color: Theme.accent; width: 26; height: 26 }
                    }
                    ColumnLayout { spacing: 7; Layout.leftMargin: 6; Layout.fillWidth: true
                        Text { Layout.fillWidth: true; elide: Text.ElideRight; text: taskCard.modelData.title; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                        Text { objectName: taskCard.modelData.module === "game" ? "downloadStatus" : taskCard.modelData.module + "DownloadStatus"; Layout.fillWidth: true; elide: Text.ElideMiddle; text: taskCard.stageText; color: Theme.muted; font.pixelSize: 11 }
                    }
                    Tag { text: taskCard.modelData.module === "game" ? "游戏" : "资源"; textColor: Theme.accent }
                    ActionButton { objectName: taskCard.modelData.module === "game" ? "pauseDownload" : taskCard.modelData.module + "PauseDownload"; text: taskCard.paused ? "继续" : "暂停"; iconName: taskCard.paused ? "play" : "pause"; compact: true; visible: taskCard.modelData.canPause === undefined ? taskCard.modelData.active : taskCard.modelData.canPause; onClicked: features.execute(taskCard.modelData.module, taskCard.modelData.prefix + (taskCard.paused ? ".resume" : ".pause"), {}) }
                    ActionButton { objectName: taskCard.modelData.module === "game" ? "retryDownload" : taskCard.modelData.module + "RetryDownload"; text: "重试"; iconName: "download"; compact: true; visible: !!taskCard.modelData.canRetry; onClicked: features.execute(taskCard.modelData.module, taskCard.modelData.prefix + ".retry", {}) }
                    ActionButton { objectName: taskCard.modelData.module === "game" ? "cancelDownload" : taskCard.modelData.module + "CancelDownload"; text: "取消"; iconName: "close"; compact: true; visible: taskCard.modelData.active; onClicked: features.execute(taskCard.modelData.module, taskCard.modelData.prefix + ".cancel", {}) }
                }
                Rectangle {
                    Layout.fillWidth: true; height: 5; radius: 2.5; color: "#26ffffff"
                    Rectangle { width: parent.width * Math.max(0, Math.min(1, taskCard.progress)); height: parent.height; radius: 2.5; color: Theme.accent
                        Behavior on width { NumberAnimation { duration: launcher.animationsEnabled ? 175 : 0 } }
                    }
                }
                RowLayout { Layout.fillWidth: true
                    Text { text: taskCard.modelData.completedFiles + " / " + taskCard.modelData.totalFiles + " 个文件"; color: Theme.subtle; font.pixelSize: 10 }
                    Item { Layout.fillWidth: true }
                    Text { text: taskCard.modelData.active && !taskCard.modelData.canPause && taskCard.modelData.module === "game" ? "准备中" : Math.round(Math.max(0,Math.min(1,taskCard.progress)) * 100) + "%"; color: Theme.accent; font.pixelSize: 12 }
                }
            }
        }
        }
    }
}
