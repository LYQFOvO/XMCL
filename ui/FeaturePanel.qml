import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Popup {
    id: panel
    property string moduleId: "instances"
    property var contextValues: ({})
    property var values: ({})
    property var availableActions: []
    property var outputItems: []
    property string message: ""
    property string details: ""
    property string resultUrl: ""
    property string userCode: ""
    property string verificationUri: ""
    property string requestedActionId: ""
    property string previewPath: ""
    property bool slimPreview: false
    property bool isError: false
    readonly property var activeAction: availableActions[actionChoice.currentIndex] || ({})
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 70, 1110)
    height: Math.min(parent.height - 60, 770)
    padding: 25
    modal: true; focus: true
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#b005130e" }
    background: Rectangle { radius: 25; color: "#162a20"; border.color: "#567561" }
    function showModule(id, initial) {
        moduleId = id
        contextValues = Object.assign({}, initial || ({}))
        requestedActionId = contextValues.actionId || ""
        if (contextValues.actionId) { let next = Object.assign({}, contextValues); delete next.actionId; contextValues = next }
        outputItems = []; message = ""; details = ""; isError = false
        userCode = ""; verificationUri = ""; resultUrl = ""; previewPath = ""
        reloadActions(); open()
    }
    function reloadActions() {
        availableActions = features.actions(moduleId)
        let index = 0
        if (requestedActionId) for (let i = 0; i < availableActions.length; ++i) if (availableActions[i].id === requestedActionId) index = i
        requestedActionId = ""
        actionChoice.currentIndex = index
        resetFields()
    }
    function resetFields() {
        let next = Object.assign({}, contextValues)
        let fields = activeAction.fields || []
        for (let f of fields) if (next[f.key] === undefined) next[f.key] = f.default === undefined ? "" : f.default
        values = next
    }
    function setField(key, value) {
        let next = Object.assign({}, values); next[key] = value; values = next
    }
    function chooseResult(row) {
        let next = Object.assign({}, contextValues, row)
        if (moduleId === "instances") next.instanceId = row.id
        if (moduleId === "accounts" && row.type) next.id = row.id
        if (row.url && moduleId === "accounts") next.providerId = row.id
        if (row.previewPath) previewPath = row.previewPath
        if (row.variant) slimPreview = row.variant === "slim"
        contextValues = next
        resetFields()
        message = "已选中：" + (row.title || row.name || row.id)
    }
    function perform() {
        let args = Object.assign({}, values)
        if (activeAction.confirm) { confirmation.open(); return }
        run(args)
    }
    function run(args) {
        isError = false; message = "正在处理…"; details = ""; resultUrl = ""
        userCode = ""; verificationUri = ""
        features.execute(moduleId, activeAction.id, args)
        for (let f of activeAction.fields || []) if (f.type === "password") setField(f.key, "")
    }
    Connections {
        target: features
        function onCompleted(module, action, data) {
            if (module !== panel.moduleId) return
            panel.isError = false
            panel.message = data.message || "操作完成"
            panel.details = data.details || ""
            panel.resultUrl = data.url || ""
            panel.userCode = data.userCode || ""
            panel.verificationUri = data.verificationUri || ""
            if (data.server) { let next=Object.assign({},panel.contextValues);next.server=data.server;panel.contextValues=next;panel.resetFields() }
            if (data.items !== undefined) panel.outputItems = data.items
            if (data.previewPath) panel.previewPath = data.previewPath
            if (data.variant) panel.slimPreview = data.variant === "slim"
            if (data.instanceId || (data.id && module === "instances")) {
                let next = Object.assign({}, panel.contextValues, data); next.instanceId = data.instanceId || data.id; panel.contextValues = next
                panel.resetFields()
            }
        }
        function onFailed(module, action, text) {
            if (module !== panel.moduleId) return
            panel.isError = true; panel.message = text
            panel.userCode = ""; panel.verificationUri = ""
        }
        function onActivity(text) { if (panel.opened) panel.message = text }
    }
    contentItem: ColumnLayout {
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            Icon { name: "layers"; color: Theme.accent; width: 24; height: 24 }
            Text { text: "你的冒险工作室"; color: Theme.text; font.pixelSize: 22; font.weight: Font.DemiBold; Layout.leftMargin: 5 }
            Item { Layout.fillWidth: true }
            ActionButton { text: "返回"; iconName: "close"; compact: true; onClicked: panel.close() }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.preferredHeight: 48
            clip: true
            contentWidth: moduleButtons.implicitWidth; contentHeight: 39
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
            Row {
                id: moduleButtons; spacing: 8
                Repeater {
                    model: features.modules
                    ActionButton {
                        required property var modelData
                        text: modelData.title; iconName: modelData.icon; compact: true; primary: panel.moduleId === modelData.id
                        onClicked: { panel.moduleId = modelData.id; panel.outputItems = []; panel.message = ""; panel.details = ""; panel.resultUrl = ""; panel.userCode = ""; panel.verificationUri = ""; panel.reloadActions() }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#28ffffff" }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 20
            ColumnLayout {
                Layout.preferredWidth: 350; Layout.maximumWidth: 350; Layout.fillHeight: true
                spacing: 12
                GlassComboBox {
                    id: actionChoice
                    objectName: "featureActionChoice"
                    Layout.fillWidth: true
                    model: panel.availableActions
                    textRole: "label"
                    onCurrentIndexChanged: panel.resetFields()
                }
                Text { text: panel.activeAction.description || ""; visible: text.length > 0; color: Theme.muted; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 14
                        Repeater {
                            model: panel.activeAction.fields || []
                            ColumnLayout {
                                id: formRow
                                required property var modelData
                                Layout.fillWidth: true
                                Text { text: formRow.modelData.label + (formRow.modelData.required ? " *" : ""); color: Theme.muted; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                GlassComboBox {
                                    visible: formRow.modelData.key === "instanceId" && features.instances.length > 0
                                    Layout.fillWidth: true
                                    model: features.instances; textRole: "title"; valueRole: "id"
                                    currentIndex: indexOfValue(panel.values[formRow.modelData.key])
                                    displayText: currentIndex < 0 ? "请选择实例" : currentText
                                    onActivated: { panel.setField(formRow.modelData.key, currentValue); let next=Object.assign({},panel.contextValues);next.instanceId=currentValue;panel.contextValues=next }
                                }
                                RowLayout {
                                    visible: !["bool", "choice"].includes(formRow.modelData.type) && !(formRow.modelData.key === "instanceId" && features.instances.length > 0)
                                    Layout.fillWidth: true
                                    TextField {
                                        objectName: "featureField_" + formRow.modelData.key
                                        Layout.fillWidth: true
                                        text: panel.values[formRow.modelData.key] === undefined ? "" : String(panel.values[formRow.modelData.key])
                                        echoMode: formRow.modelData.type === "password" ? TextInput.Password : TextInput.Normal
                                        color: Theme.text; selectionColor: "#557d61"; font.pixelSize: 12
                                        background: Rectangle { color: "#0e2017"; radius: 9; border.color: parent.activeFocus ? Theme.accent : "#3d5845" }
                                        onTextEdited: panel.setField(formRow.modelData.key, formRow.modelData.type === "number" ? Number(text) : text)
                                    }
                                    ActionButton {
                                        visible: formRow.modelData.type === "path" || formRow.modelData.type === "directory"
                                        text: "选择"; compact: true
                                        onClicked: {
                                            if (formRow.modelData.type === "directory") { directoryPicker.fieldKey = formRow.modelData.key; directoryPicker.open() }
                                            else { filePicker.fieldKey = formRow.modelData.key; filePicker.open() }
                                        }
                                    }
                                }
                                GlassComboBox {
                                    visible: formRow.modelData.type === "choice"
                                    Layout.fillWidth: true; model: formRow.modelData.options || []
                                    currentIndex: Math.max(0, model.indexOf(panel.values[formRow.modelData.key]))
                                    onActivated: panel.setField(formRow.modelData.key, currentText)
                                }
                                Toggle { visible: formRow.modelData.type === "bool"; checked: !!panel.values[formRow.modelData.key]; onToggled: panel.setField(formRow.modelData.key, checked) }
                            }
                        }
                    }
                }
                ActionButton { objectName: "executeFeatureAction"; Layout.fillWidth: true; text: panel.activeAction.label || "执行"; primary: true; enabled: !!panel.activeAction.id; onClicked: panel.perform() }
            }
            Rectangle { Layout.fillHeight: true; width: 1; color: "#1cffffff" }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                Text { text: panel.message || "选择一项操作，开始管理你的世界。"; color: panel.isError ? "#f3b3a2" : Theme.accent; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Rectangle {
                    objectName: "deviceCodeBox"
                    Layout.fillWidth: true
                    visible: panel.userCode.length > 0
                    implicitHeight: deviceCodeColumn.implicitHeight + 32
                    radius: 14; color: "#1f3a2a"; border.color: "#7fb894"; border.width: 1
                    ColumnLayout {
                        id: deviceCodeColumn
                        anchors.fill: parent; anchors.margins: 16; spacing: 7
                        Text { text: "微软设备码 · 在浏览器登录页输入"; color: Theme.muted; font.pixelSize: 11; font.letterSpacing: 1 }
                        TextEdit {
                            objectName: "deviceCodeValue"
                            text: panel.userCode; readOnly: true; selectByMouse: true
                            color: Theme.accent; selectionColor: "#46735a"; selectedTextColor: "#ffffff"
                            font.family: Theme.font; font.pixelSize: 28; font.weight: Font.DemiBold; font.letterSpacing: 5
                            Layout.fillWidth: true; Layout.preferredHeight: contentHeight
                        }
                        Text { text: "代码可选中复制。登录完成后本面板会自动保存账号。"; color: Theme.muted; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Text {
                            objectName: "deviceCodeSafety"
                            Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#f2d9a6"; font.pixelSize: 10; lineHeight: 1.4
                            text: "安全提示：只输入本窗口刚为你生成的代码，不要把代码告诉任何人。他人索要设备码可能是诱导授权；微软授权页会显示应用名称，请确认后再批准。"
                        }
                    }
                }
                ScrollView {
                    Layout.fillWidth: true; Layout.preferredHeight: Math.min(130, detailsText.implicitHeight)
                    visible: panel.details.length > 0; clip: true; contentWidth: availableWidth
                    Text { id: detailsText; width: parent.width; text: panel.details; color: Theme.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
                }
                ActionButton { text: "打开登录页面"; iconName: "link"; compact: true; visible: panel.resultUrl.length > 0; onClicked: Qt.openUrlExternally(panel.resultUrl) }
                Loader {
                    Layout.fillWidth: true; Layout.preferredHeight: active ? 210 : 0
                    active: panel.moduleId === "accounts" && panel.previewPath.length > 0
                    sourceComponent: SkinPreview { source: "file://" + panel.previewPath; slim: panel.slimPreview }
                }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 7; model: panel.outputItems
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Button {
                        required property var modelData
                        width: ListView.view.width; height: 65
                        hoverEnabled: true
                        background: Rectangle { radius: 11; color: parent.hovered ? "#2a4333" : "#1c3326"; border.color: "#36553e" }
                        contentItem: Column {
                            leftPadding: 8; topPadding: 7; spacing: 6
                            Text { width: parent.width - 16; text: modelData.title || modelData.name || modelData.id || "项目"; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                            Text { width: parent.width - 16; text: modelData.subtitle || modelData.path || ""; color: Theme.muted; font.pixelSize: 10; elide: Text.ElideMiddle }
                        }
                        onClicked: panel.chooseResult(modelData)
                    }
                }
                Text { text: "点击结果可将选中项目带入后续操作。"; color: Theme.subtle; font.pixelSize: 10 }
            }
        }
    }
    FileDialog { id: filePicker; property string fieldKey; title: "选择文件"; onAccepted: panel.setField(fieldKey, selectedFile.toString()) }
    FolderDialog { id: directoryPicker; property string fieldKey; title: "选择目录"; onAccepted: panel.setField(fieldKey, selectedFolder.toString()) }
    Popup {
        id: confirmation
        parent: Overlay.overlay; anchors.centerIn: parent; width: 400; padding: 24; modal: true; focus: true
        background: Rectangle { radius: 20; color: "#213c2a"; border.color: "#80aa86" }
        contentItem: Column { spacing: 18
            Text { text: "确认" + (panel.activeAction.label || "操作") + "？"; color: Theme.text; font.pixelSize: 20 }
            Text { width: parent.width; text: panel.activeAction.description || "此操作将修改本地文件。"; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap }
            Row { spacing: 12
                ActionButton { text: "取消"; onClicked: confirmation.close() }
                ActionButton { text: "确认执行"; primary: true; onClicked: { let args=Object.assign({},panel.values);args.confirm=true;confirmation.close();panel.run(args) } }
            }
        }
    }
}
