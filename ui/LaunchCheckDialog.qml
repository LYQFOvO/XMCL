import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: dialog
    objectName: "launchCheckDialog"
    property var app
    property var instance: ({})
    property bool checking: false
    property bool repairing: false
    property bool starting: false
    property bool analyzed: false
    property var check: ({})
    property var findings: []
    property string error: ""
    readonly property var matchedJava: (check.java && check.java.matched) ? check.java.matched : ({})
    readonly property bool javaReady: !!matchedJava.path || !!check.java && !!check.java.usedManual
    readonly property int missingFiles: check.files ? check.files.missing : 0
    readonly property int mismatchedFiles: check.files ? check.files.mismatched : 0
    readonly property var fileIssues: check.fileIssues || []
    readonly property var issues: check.issues || []
    readonly property bool fixable: !!check.fixable
    readonly property var repairState: features.downloadState || ({})
    readonly property bool repairingActive: repairing && (repairState.active || repairState.phase === "repair" || repairState.phase === "downloading")
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 70, 940)
    height: Math.min(parent.height - 50, 700)
    padding: 24
    modal: true; focus: true
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#b005130e" }
    background: Rectangle { radius: 25; color: "#162a20"; border.color: "#567561" }

    function showFor(target) {
        instance = target || ({})
        error = ""; findings = []; check = ({}); analyzed = false
        checking = false; repairing = false; starting = false
        open()
        recheck()
    }
    function recheck() {
        if (!instance.id) return
        checking = true; error = ""
        features.execute("game", "launch.check", {instanceId: instance.id})
    }
    function repair() {
        if (!instance.id) return
        repairing = true; error = ""
        features.execute("game", "launch.repair", {instanceId: instance.id})
    }
    function start() {
        if (!instance.id) return
        starting = true; error = ""; findings = []; analyzed = false
        features.execute("game", "game.launch", {instanceId: instance.id})
    }
    function analyze() {
        if (!instance.id) return
        analyzed = true
        features.execute("game", "diagnostic.analyze", {instanceId: instance.id})
    }
    Connections {
        target: features
        function onCompleted(module, action, data) {
            if (module !== "game") return
            if (action === "launch.check") { dialog.checking = false; dialog.check = data || ({}); dialog.error = "" }
            else if (action === "launch.repair") { dialog.repairing = false; dialog.recheck() }
            else if (action === "game.launch") { dialog.starting = false; dialog.close() }
            else if (action === "diagnostic.analyze") { dialog.findings = data.findings || [] }
        }
        function onFailed(module, action, message) {
            if (module !== "game") return
            if (action === "launch.check") { dialog.checking = false; dialog.error = message }
            else if (action === "launch.repair") { dialog.repairing = false; dialog.error = message }
            else if (action === "game.launch") { dialog.starting = false; dialog.error = message; dialog.analyze() }
        }
    }
    contentItem: ColumnLayout {
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout { spacing: 5
                Text { text: dialog.repairing ? "正在修复启动文件" : "启动检查"; color: Theme.text; font.pixelSize: 23; font.weight: Font.DemiBold }
                Text { text: (dialog.instance.name || "实例") + " · Minecraft " + (dialog.instance.version || "") + " · " + (dialog.instance.loader || ""); color: Theme.muted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "launchCheckClose"; text: "返回"; iconName: "close"; compact: true; onClicked: dialog.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#28ffffff" }
        Text {
            objectName: "launchCheckSummary"; Layout.fillWidth: true; wrapMode: Text.Wrap
            color: dialog.error.length ? "#ffb6a9" : (dialog.fixable ? "#f2d9a6" : Theme.accent); font.pixelSize: 12; lineHeight: 1.4
            text: dialog.error.length ? dialog.error
                : dialog.checking ? "正在检查 Java、客户端、库与资源文件…"
                : (dialog.check.message || "")
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 16
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 132; radius: 16; color: "#12251b"; border.color: "#36513f"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 7
                    RowLayout { Layout.fillWidth: true
                        Icon { name: "cpu"; width: 17; height: 17; color: Theme.accent }
                        Text { text: "Java 运行时"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.leftMargin: 2 }
                        Item { Layout.fillWidth: true }
                        Tag { objectName: "launchCheckJavaTag"; text: dialog.javaReady ? "可用" : (dialog.checking ? "检查中" : "未匹配"); implicitHeight: 20; textColor: dialog.javaReady ? Theme.accent : "#f2b9a6" }
                    }
                    Text {
                        objectName: "launchCheckJava"; Layout.fillWidth: true; wrapMode: Text.Wrap; elide: Text.ElideMiddle
                        color: dialog.javaReady ? Theme.text : Theme.muted; font.pixelSize: 11; lineHeight: 1.4
                        text: dialog.matchedJava.path ? (dialog.matchedJava.title + " · " + (dialog.check.java && dialog.check.java.usedManual ? "实例指定" : "自动匹配") + "\n" + dialog.matchedJava.path)
                            : (dialog.check.java && dialog.check.java.warnings && dialog.check.java.warnings.length ? dialog.check.java.warnings[0] : "尚未找到匹配的 Java")
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.subtle; font.pixelSize: 10
                        text: dialog.check.java && dialog.check.java.requiredMajor > 0 ? "该版本要求 Java " + dialog.check.java.requiredMajor + "；可在实例设置中手动指定。" : "无法从版本确定 Java 要求。"
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 132; radius: 16; color: "#12251b"; border.color: "#36513f"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 7
                    RowLayout { Layout.fillWidth: true
                        Icon { name: "folder"; width: 17; height: 17; color: Theme.accent }
                        Text { text: "文件完整性"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.leftMargin: 2 }
                        Item { Layout.fillWidth: true }
                        Tag { objectName: "launchCheckFilesTag"; text: (dialog.missingFiles + dialog.mismatchedFiles) > 0 ? "需修复" : (dialog.checking ? "检查中" : "通过"); implicitHeight: 20; textColor: (dialog.missingFiles + dialog.mismatchedFiles) > 0 ? "#f2b9a6" : Theme.accent }
                    }
                    Text {
                        objectName: "launchCheckFiles"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.muted; font.pixelSize: 11; lineHeight: 1.4
                        text: dialog.check.files ? ("已检查 " + dialog.check.files.checked + " 个文件 · 缺失 " + dialog.check.files.missing + " · 大小不符 " + dialog.check.files.mismatched + (dialog.check.files.assetObjects ? " · 资源 " + dialog.check.files.assetObjects : "")) : "正在读取实例文件…"
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.subtle; font.pixelSize: 10; maximumLineCount: 2; elide: Text.ElideRight
                        text: dialog.fileIssues.length ? dialog.fileIssues.map(item => item.title + "：" + item.reason).join("\n") : "只下载缺失或大小不符的文件，不删除未知模组或用户文件。"
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.subtle; font.pixelSize: 10; text: dialog.repairingActive ? ("正在修复 " + (dialog.repairState.bytes || 0) + " / " + (dialog.repairState.totalBytes || 0) + " 字节") : (dialog.issues.length ? "请先处理检查结果中的问题。" : "") }
            ActionButton { objectName: "launchCheckInstanceSettings"; text: "实例设置"; iconName: "settings"; compact: true; onClicked: { dialog.close(); dialog.app.openInstanceSettings(dialog.instance) } }
            ActionButton { objectName: "launchCheckRecheck"; text: "重新检查"; iconName: "search"; compact: true; enabled: !dialog.checking && !dialog.repairingActive; onClicked: dialog.recheck() }
            ActionButton {
                objectName: "launchCheckRepair"; text: dialog.repairingActive ? "正在修复…" : (dialog.fixable ? "一键修复缺失文件" : "无需修复"); iconName: "download"; compact: true
                enabled: dialog.fixable && !dialog.repairingActive && !dialog.checking
                onClicked: dialog.repair()
            }
        }
        ListView {
            id: issueList; objectName: "launchCheckIssues"
            Layout.fillWidth: true; Layout.preferredHeight: Math.min(190, Math.max(54, dialog.issues.length * 44 + (dialog.findings.length ? dialog.findings.length * 66 : 0)))
            clip: true; spacing: 6; model: dialog.issues
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width; height: 40; radius: 10
                color: modelData.severity === "error" ? "#2a1d1b" : "#1b2c22"; border.color: modelData.severity === "error" ? "#6b3d33" : "#36513f"
                RowLayout { anchors.fill: parent; anchors.margins: 10; spacing: 8
                    Text { text: modelData.severity === "error" ? "!" : "·"; color: modelData.severity === "error" ? "#ffb6a9" : Theme.accent; font.pixelSize: 13; font.weight: Font.Bold }
                    Text { text: modelData.title; color: Theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; text: modelData.detail; color: Theme.muted; font.pixelSize: 10; elide: Text.ElideRight }
                }
            }
        }
        ColumnLayout {
            visible: dialog.findings.length > 0
            Layout.fillWidth: true; spacing: 6
            RowLayout { Layout.fillWidth: true
                Text { text: "启动诊断"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Text { text: "以下为基于日志的推断，不会自动修改你的文件"; color: Theme.subtle; font.pixelSize: 9 }
            }
            Repeater {
                model: dialog.findings
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true; spacing: 8
                    Text { text: "•"; color: modelData.severity === "error" ? "#ffb6a9" : Theme.accent; font.pixelSize: 11 }
                    ColumnLayout { Layout.fillWidth: true; spacing: 2
                        Text { text: modelData.title; color: Theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                        Text { Layout.fillWidth: true; text: modelData.advice; color: Theme.muted; font.pixelSize: 10; wrapMode: Text.Wrap }
                    }
                }
            }
        }
        Item { Layout.fillHeight: true }
        Text {
            objectName: "launchCheckError"; visible: dialog.error.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap
            text: dialog.error; color: "#ffb6a9"; font.pixelSize: 11; maximumLineCount: 3; elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.subtle; font.pixelSize: 10; text: dialog.check.logHint || "检查只读取文件与日志；修复只下载缺失或大小不符的文件。" }
            ActionButton { text: "取消"; compact: true; onClicked: dialog.close() }
            ActionButton {
                objectName: "launchCheckStart"; text: dialog.starting ? "正在启动…" : "继续启动"; iconName: "play"; primary: true
                enabled: !dialog.checking && !dialog.repairingActive && !dialog.starting && !dialog.fixable && dialog.javaReady && dialog.error.length === 0
                opacity: enabled ? 1 : 0.45
                onClicked: dialog.start()
            }
        }
    }
}
