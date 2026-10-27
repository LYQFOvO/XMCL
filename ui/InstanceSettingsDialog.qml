import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: dialog
    objectName: "instanceSettingsDialog"
    property var app
    property string instanceId: ""
    property string instanceTitle: ""
    property string instanceVersion: ""
    property string instanceLoader: ""
    property string javaPath: ""
    property string jvmArgs: ""
    property string gameArgs: ""
    property int memoryGb: 4
    property string error: ""
    property bool saving: false
    property var javaOptions: []
    property bool scanning: false
    property var javaMatch: ({})
    property string javaStatus: ""
    property string requiredJavaText: ""
    readonly property int memoryLimit: launcher.maxMemoryGb
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 70, 900)
    height: Math.min(parent.height - 60, 640)
    padding: 24
    modal: true; focus: true
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#b005130e" }
    background: Rectangle { radius: 25; color: "#162a20"; border.color: "#567561" }

    function show(instance) {
        instanceId = instance.id || ""
        instanceTitle = instance.name || ""
        instanceVersion = instance.version || ""
        instanceLoader = instance.loader || ""
        javaPath = instance.javaPath || ""
        jvmArgs = instance.jvmArgs || ""
        gameArgs = instance.gameArgs || ""
        const saved = instance.memoryGb !== undefined && instance.memoryGb !== null ? Number(instance.memoryGb) : launcher.suggestedMemoryGb
        memoryGb = Math.max(2, Math.min(saved, memoryLimit))
        memorySlider.value = memoryGb
        windowWidth.text = String(instance.width || 1280)
        windowHeight.text = String(instance.height || 720)
        error = ""; saving = false; javaOptions = []; scanning = false; javaMatch = ({}); javaStatus = ""
        open()
        refreshJavaMatch()
    }
    function refreshJavaMatch() {
        if (!dialog.instanceId) return
        features.execute("java", "java.match", {instanceId: dialog.instanceId})
    }
    function applyMatchedJava() {
        const matched = dialog.javaMatch.matched || ({})
        if (matched.path) { dialog.javaPath = matched.path; javaPathField.text = matched.path }
    }
    function save() {
        saving = true; error = ""
        features.execute("instances", "instance.configure", {
            instanceId: dialog.instanceId,
            javaPath: dialog.javaPath.trim(),
            memoryGb: Math.round(memorySlider.value),
            jvmArgs: dialog.jvmArgs,
            gameArgs: dialog.gameArgs,
            width: parseInt(windowWidth.text, 10) || 1280,
            height: parseInt(windowHeight.text, 10) || 720
        })
    }
    Connections {
        target: features
        function onCompleted(module, action, data) {
            if (module === "instances" && action === "instance.configure" && dialog.saving) {
                dialog.saving = false
                dialog.refreshJavaMatch()
                dialog.close()
            } else if (module === "java" && action === "java.scan" && dialog.scanning) {
                dialog.scanning = false
                dialog.javaOptions = (data.items || []).filter(item => item.valid)
                dialog.refreshJavaMatch()
            } else if (module === "java" && action === "java.match") {
                dialog.javaMatch = data || ({})
                const matched = dialog.javaMatch.matched || ({})
                const required = dialog.javaMatch.requiredMajor
                dialog.requiredJavaText = required > 0 ? ("版本要求 Java " + required) : "无法从版本确定 Java 要求"
                if (matched.path) dialog.javaStatus = "已匹配 " + matched.title + " · " + (dialog.javaMatch.usedManual ? "实例指定" : "自动匹配")
                else dialog.javaStatus = (data.warnings && data.warnings.length) ? data.warnings[0] : "没有匹配的 Java"
            }
        }
        function onFailed(module, action, message) {
            if (module === "instances" && action === "instance.configure" && dialog.saving) {
                dialog.saving = false
                dialog.error = message
            } else if (module === "java" && action === "java.scan" && dialog.scanning) {
                dialog.scanning = false
                dialog.error = "扫描 Java 失败：" + message
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 15
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout { spacing: 6
                Text { text: "实例设置"; color: Theme.text; font.pixelSize: 23; font.weight: Font.DemiBold }
                Text { text: dialog.instanceTitle + " · 该实例独立使用自己的版本、Java 与内存配置"; color: Theme.muted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: "返回"; iconName: "close"; compact: true; onClicked: dialog.close() }
        }
        Row { spacing: 8
            Tag { text: "Minecraft " + dialog.instanceVersion; implicitHeight: 22; textColor: Theme.muted }
            Tag { text: dialog.instanceLoader; implicitHeight: 22; textColor: Theme.accent }
            Tag { text: "版本隔离"; implicitHeight: 22; textColor: Theme.subtle }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#28ffffff" }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 24
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10
                Text { text: "Java 运行时"; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                TextField {
                    id: javaPathField; objectName: "instanceJavaPath"; Layout.fillWidth: true; implicitHeight: 40
                    text: dialog.javaPath
                    placeholderText: "留空自动选择（例如 /usr/bin/java 或 JAVA_HOME 中的 java）"
                    placeholderTextColor: "#a2b4a8"; color: Theme.text; leftPadding: 12; selectByMouse: true
                    onTextEdited: dialog.javaPath = text
                    background: Rectangle { radius: 10; color: "#0e2017"; border.color: javaPathField.activeFocus ? Theme.accent : "#52715d" }
                }
                RowLayout { Layout.fillWidth: true; spacing: 8
                    GlassComboBox {
                        objectName: "instanceJavaOptions"; Layout.fillWidth: true
                        visible: dialog.javaOptions.length > 0
                        model: dialog.javaOptions; textRole: "title"; valueRole: "path"
                        displayText: dialog.javaOptions.length ? "选择已检测的 Java（" + dialog.javaOptions.length + " 个）" : ""
                        onActivated: { dialog.javaPath = currentValue; javaPathField.text = currentValue }
                    }
                    ActionButton {
                        objectName: "instanceJavaScan"; text: dialog.scanning ? "扫描中…" : "扫描本机 Java"; iconName: "search"; compact: true
                        enabled: !dialog.scanning
                        onClicked: { dialog.scanning = true; features.execute("java", "java.scan", {}) }
                    }
                    ActionButton {
                        objectName: "instanceJavaAuto"; text: "自动匹配"; iconName: "check"; compact: true
                        enabled: (dialog.javaMatch.matched || ({})).path !== undefined
                        onClicked: dialog.applyMatchedJava()
                    }
                }
                Text {
                    objectName: "instanceJavaStatus"; Layout.fillWidth: true; wrapMode: Text.Wrap
                    color: (dialog.javaMatch.matched || ({})).path ? Theme.accent : Theme.muted; font.pixelSize: 10; lineHeight: 1.45
                    text: dialog.javaStatus.length ? (dialog.requiredJavaText + " · " + dialog.javaStatus) : (dialog.instanceId.length ? "正在检查该实例的 Java…" : "")
                }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "指定后仅此实例使用；留空则按游戏版本要求自动查找本机 Java。智能匹配优先精确主版本，没有精确版本时才建议较高版本。"; color: Theme.subtle; font.pixelSize: 10 }
                Text { text: "JVM 参数"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.topMargin: 4 }
                TextField {
                    id: jvmField; objectName: "instanceJvmArgs"; Layout.fillWidth: true; implicitHeight: 40
                    text: dialog.jvmArgs
                    placeholderText: "例如 -XX:+UseG1GC -Dfile.encoding=UTF-8"
                    placeholderTextColor: "#a2b4a8"; color: Theme.text; leftPadding: 12; selectByMouse: true
                    onTextEdited: dialog.jvmArgs = text
                    background: Rectangle { radius: 10; color: "#0e2017"; border.color: jvmField.activeFocus ? Theme.accent : "#52715d" }
                }
                Text { text: "游戏参数"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.topMargin: 4 }
                TextField {
                    id: gameArgField; objectName: "instanceGameArgs"; Layout.fillWidth: true; implicitHeight: 40
                    text: dialog.gameArgs
                    placeholderText: "例如 --demo 或额外启动参数"
                    placeholderTextColor: "#a2b4a8"; color: Theme.text; leftPadding: 12; selectByMouse: true
                    onTextEdited: dialog.gameArgs = text
                    background: Rectangle { radius: 10; color: "#0e2017"; border.color: gameArgField.activeFocus ? Theme.accent : "#52715d" }
                }
                Text { text: "窗口尺寸"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.topMargin: 4 }
                RowLayout { spacing: 10
                    TextField {
                        id: windowWidth; objectName: "instanceWindowWidth"; Layout.preferredWidth: 110; implicitHeight: 38
                        text: "1280"; color: Theme.text; leftPadding: 12; selectByMouse: true; validator: IntValidator { bottom: 320; top: 7680 }
                        inputMethodHints: Qt.ImhDigitsOnly
                        background: Rectangle { radius: 10; color: "#0e2017"; border.color: windowWidth.activeFocus ? Theme.accent : "#52715d" }
                    }
                    Text { text: "×"; color: Theme.subtle }
                    TextField {
                        id: windowHeight; objectName: "instanceWindowHeight"; Layout.preferredWidth: 110; implicitHeight: 38
                        text: "720"; color: Theme.text; leftPadding: 12; selectByMouse: true; validator: IntValidator { bottom: 240; top: 4320 }
                        inputMethodHints: Qt.ImhDigitsOnly
                        background: Rectangle { radius: 10; color: "#0e2017"; border.color: windowHeight.activeFocus ? Theme.accent : "#52715d" }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
            Rectangle { Layout.fillHeight: true; width: 1; color: "#28ffffff" }
            Rectangle {
                Layout.preferredWidth: 330; Layout.maximumWidth: 330; Layout.fillHeight: true
                radius: 18; color: "#12251b"; border.color: "#36513f"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 20; spacing: 12
                    RowLayout { Layout.fillWidth: true
                        Icon { name: "cpu"; width: 18; height: 18; color: Theme.accent }
                        Text { text: "内存分配"; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold; Layout.leftMargin: 2 }
                        Item { Layout.fillWidth: true }
                        Text { objectName: "instanceMemoryValue"; text: Math.round(memorySlider.value) + " GB"; color: Theme.accent; font.pixelSize: 22; font.weight: Font.DemiBold }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "仅影响此实例的 Java 堆上限（-Xmx），不会占用其他实例的内存。"; color: Theme.muted; font.pixelSize: 10; lineHeight: 1.5 }
                    Slider {
                        id: memorySlider; objectName: "instanceMemorySlider"
                        Layout.fillWidth: true; from: 2; to: dialog.memoryLimit; stepSize: 1
                        snapMode: Slider.SnapAlways; activeFocusOnTab: true
                        Accessible.name: "实例内存 GB"
                        background: Rectangle {
                            x: memorySlider.leftPadding; y: memorySlider.topPadding + memorySlider.availableHeight / 2 - height / 2
                            width: memorySlider.availableWidth; height: 5; radius: 2.5; color: "#334b3d"
                            Rectangle { width: memorySlider.visualPosition * parent.width; height: parent.height; radius: 2.5; color: Theme.accent }
                        }
                        handle: Rectangle {
                            x: memorySlider.leftPadding + memorySlider.visualPosition * (memorySlider.availableWidth - width)
                            y: memorySlider.topPadding + memorySlider.availableHeight / 2 - height / 2
                            width: 20; height: 20; radius: 10; color: "#def7e1"; border.width: memorySlider.activeFocus ? 3 : 1; border.color: "#779a7c"
                        }
                    }
                    RowLayout { Layout.fillWidth: true
                        Text { text: "2 GB"; color: Theme.subtle; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: dialog.memoryLimit + " GB"; color: Theme.subtle; font.pixelSize: 10 }
                    }
                    Text { objectName: "instanceMemoryAdvice"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.accent; font.pixelSize: 10; lineHeight: 1.45; text: launcher.totalMemoryGb > 0 ? "本机物理内存 " + launcher.totalMemoryGb + " GB · 智能建议 " + launcher.suggestedMemoryGb + " GB" : "无法检测物理内存，请手动选择" }
                    ActionButton {
                        objectName: "instanceSmartMemory"; text: "智能分配"; iconName: "sparkles"; compact: true
                        onClicked: memorySlider.value = launcher.suggestedMemoryGb
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "智能分配会为系统预留约 2 GB，再按本机内存计算游戏堆上限；你可以随时手动调整。"; color: Theme.subtle; font.pixelSize: 10; lineHeight: 1.5 }
                    Item { Layout.fillHeight: true }
                }
            }
        }
        Text { objectName: "instanceSettingsError"; visible: dialog.error.length > 0; text: dialog.error; color: "#ffb6a9"; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11 }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "版本、加载器与游戏目录属于该实例；Java、内存和参数只对本实例生效。"; color: Theme.subtle; font.pixelSize: 10; Layout.fillWidth: true }
            ActionButton { text: "取消"; compact: true; onClicked: dialog.close() }
            ActionButton {
                objectName: "saveInstanceSettings"; text: dialog.saving ? "正在保存…" : "保存设置"; iconName: "check"; primary: true
                enabled: !dialog.saving && dialog.instanceId.length > 0
                onClicked: dialog.save()
            }
        }
    }
}
