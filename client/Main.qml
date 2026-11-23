import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MlxClient

ApplicationWindow {
    id: window
    width: 900
    height: 650
    visible: true
    title: "MLX Local Client"

    AppContext {
        id: app
        Component.onCompleted: initialize()
        onErrorOccurred: (msg) => {
                             errorDialog.text = msg
                             errorDialog.open()
                         }
    }

    FileDialog {
        id: imagePicker
        title: "Выберите изображение"
        nameFilters: ["Изображения (*.png *.jpg *.jpeg *.webp)"]
        onAccepted: app.chat.attachFile(selectedFile)
    }

    Dialog {
        id: errorDialog
        property alias text: errorLabel.text
        title: "Ошибка"
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label { id: errorLabel; wrapMode: Text.WordWrap }
    }

    Dialog {
        id: downloadDialog
        title: "Скачать модель (HuggingFace)"
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: app.models.downloadModel(repoInput.text)
        ColumnLayout {
            width: 300
            Label { text: "Repo ID (например, mlx-community/Qwen2.5-VL-3B-Instruct-4bit):" }
            TextField {
                id: repoInput
                Layout.fillWidth: true
                placeholderText: "mlx-community/..."
            }
        }
    }

    Dialog {
        id: authDialog
        title: "Вход в аккаунт"
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        visible: !app.auth.isLoggedIn

        ColumnLayout {
            spacing: 10
            width: 280
            TextField {
                id: loginField
                Layout.fillWidth: true
                placeholderText: "Логин"
            }
            TextField {
                id: passField
                Layout.fillWidth: true
                placeholderText: "Пароль"
                echoMode: TextInput.Password
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: "Войти"
                    Layout.fillWidth: true
                    enabled: loginField.text.length > 0 && passField.text.length > 0
                    onClicked: app.auth.loginUser(loginField.text, passField.text)
                }
                Button {
                    text: "Регистрация"
                    Layout.fillWidth: true
                    enabled: loginField.text.length > 0 && passField.text.length > 0
                    onClicked: app.auth.registerUser(loginField.text, passField.text)
                }
            }
        }
    }

    Drawer {
        id: chatsDrawer
        width: Math.min(window.width * 0.75, 280)
        height: window.height

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "👤 " + app.auth.userLogin
                    font.bold: true
                    Layout.fillWidth: true
                }
                Button {
                    text: "Выйти"
                    flat: true
                    onClicked: {
                        chatsDrawer.close()
                        app.auth.logout()
                    }
                }
            }

            Button {
                text: "+ Новый чат"
                Layout.fillWidth: true
                onClicked: {
                    app.chat.createNewChat()
                    chatsDrawer.close()
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: app.chat.chats

                delegate: ItemDelegate {
                    width: parent ? parent.width : 0
                    highlighted: modelData.id === app.chat.activeChatId
                    onClicked: {
                        app.chat.selectChat(modelData.id)
                        chatsDrawer.close()
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        Label {
                            text: modelData.title
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        ToolButton {
                            text: "✕"
                            onClicked: app.chat.deleteChat(modelData.id)
                        }
                    }
                }
            }
        }
    }

    Component {
        id: statusLabel
        Label {
            text: app.models.serverStatus + (app.models.textOnlyMode && app.models.serverStatus === "LOADED" ? " [TXT]" : "")
            font.bold: true
            color: app.models.serverStatus === "LOADED" ? "green" :
                                                          app.models.serverStatus === "OFFLINE" ? "red" : "orange"
        }
    }

    header: ToolBar {
        id: headerBar
        clip: true
        property bool expanded: false
        height: (optionsPane.visible ? optionsPane.implicitHeight : statusPane.implicitHeight) + 12
        Behavior on height { NumberAnimation { duration: 200 } }

        RowLayout {
            id: statusPane
            anchors.fill: parent
            anchors.margins: 6
            visible: !optionsPane.visible

            RoundButton {
                text: "☰"
                onClicked: chatsDrawer.open()
            }
            RoundButton {
                text: headerBar.expanded ? "▲" : "▼"
                onClicked: headerBar.expanded = !headerBar.expanded
            }
            ColumnLayout {
                Label {
                    textFormat: Text.StyledText
                    text: "<b>Сервер: </b><i>" + app.transport.serverUrl + "</i>"
                    Layout.fillWidth: true
                }
                Label {
                    text: "<b>Модель: </b><i>" + app.models.activeModel + "</i>"
                    Layout.fillWidth: true
                }
            }
            Loader { sourceComponent: statusLabel }
        }

        ColumnLayout {
            id: optionsPane
            anchors.fill: parent
            anchors.margins: 6
            visible: headerBar.expanded || (app.models.serverStatus !== "LOADED" && app.models.serverStatus !== "GENERATING")

            RowLayout {
                Layout.fillWidth: true
                RoundButton {
                    text: "☰"
                    onClicked: chatsDrawer.open()
                }
                RoundButton {
                    text: headerBar.expanded ? "▲" : "▼"
                    onClicked: headerBar.expanded = !headerBar.expanded
                }
                TextField {
                    id: urlField
                    Layout.fillWidth: true
                    text: app.transport.serverUrl
                    placeholderText: "http://192.168.1.X:8000"
                }
                Button {
                    text: "Подключить"
                    onClicked: app.transport.serverUrl = urlField.text
                }
                Loader { sourceComponent: statusLabel }
            }

            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: modelSelector
                    Layout.fillWidth: true
                    model: app.models.models
                    textRole: "id"
                    enabled: !app.models.isBusy && app.models.serverStatus !== "LOADED"
                    Connections {
                        target: app.models
                        function onActiveModelIndexChanged() {
                            modelSelector.currentIndex = app.models.activeModelIndex
                        }
                    }
                }
                CheckBox {
                    id: textOnlyCheck
                    text: "Text-only"
                    checked: false
                    enabled: !app.models.isBusy && app.models.serverStatus !== "LOADED"
                }
                Button {
                    text: app.models.serverStatus === "LOADED" ? "Выгрузить" : "Загрузить"
                    enabled: !app.models.isBusy && (app.models.serverStatus === "LOADED" || modelSelector.currentIndex >= 0)
                    onClicked: {
                        if (app.models.serverStatus === "LOADED") {
                            app.models.unloadModel()
                        } else {
                            var selectedId = app.models.models[modelSelector.currentIndex].id
                            app.models.loadModel(selectedId, textOnlyCheck.checked)
                        }
                    }
                }
                Button {
                    text: "+ HF"
                    enabled: !app.models.isBusy
                    onClicked: downloadDialog.open()
                }
                Button {
                    text: "Удалить"
                    enabled: !app.models.isBusy && app.models.serverStatus !== "LOADED" && modelSelector.currentIndex >= 0
                    onClicked: {
                        var selectedId = app.models.models[modelSelector.currentIndex].id
                        app.models.deleteModel(selectedId)
                    }
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: "#444444"
        }
    }

    ListView {
        id: chatList
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10
        clip: true
        model: app.chat.model
        onCountChanged: positionViewAtEnd()
        ScrollBar.vertical: ScrollBar {
            id: listScrollBar
            policy: ScrollBar.AsNeeded
        }
        delegate: Item {
            width: chatList.width
            height: bubble.height
            required property int index
            required property string role
            required property string content
            required property string think
            required property string warning
            required property var attachments
            property bool thinkExpanded: false
            property bool isUser: role === "user"
            property bool hasThink: think.length > 0
            property bool hasWarning: warning.length > 0
            property bool isStreaming: !isUser
                                       && index === chatList.count - 1
                                       && app.models.serverStatus === "GENERATING"
            Rectangle {
                id: bubble
                width: Math.min(parent.width * 0.85, Math.max(contentCol.implicitWidth + 24, 140))
                height: contentCol.implicitHeight + 20
                radius: 12
                color: isUser ? "#2b5278" : "#2d2d2d"
                anchors.right: isUser ? parent.right : undefined
                anchors.left: isUser ? undefined : parent.left
                anchors.rightMargin: isUser ? listScrollBar.width : undefined
                ColumnLayout {
                    id: contentCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    Flow {
                        visible: attachments && attachments.length > 0
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: attachments
                            Image {
                                width: 140
                                height: 140
                                fillMode: Image.PreserveAspectCrop
                                source: modelData.url || ""
                                asynchronous: true
                            }
                        }
                    }
                    Button {
                        visible: !isUser && hasThink
                        text: thinkExpanded ? "▼ Скрыть рассуждения" : "▶ Ход мыслей..."
                        flat: true
                        font.pixelSize: 12
                        Layout.preferredHeight: 24
                        onClicked: thinkExpanded = !thinkExpanded
                    }
                    Rectangle {
                        visible: !isUser && hasThink && thinkExpanded
                        Layout.fillWidth: true
                        implicitHeight: thinkText.implicitHeight + 12
                        color: "#1e1e1e"
                        radius: 6
                        TextArea {
                            id: thinkText
                            anchors.fill: parent
                            anchors.margins: 6
                            text: app.chat.formatContent(think, color, font.pixelSize)
                            color: "#aaaaaa"
                            wrapMode: Text.WordWrap
                            textFormat: Text.RichText
                            readOnly: true
                            selectByMouse: true
                            selectByKeyboard: true
                        }
                    }
                    TextArea {
                        Layout.fillWidth: true
                        text: {
                            if (content.length > 0) return app.chat.formatContent(content, color, font.pixelSize)
                            if (isStreaming) return hasThink ? "Формулирую ответ..." : "Думаю..."
                            return ""
                        }
                        visible: text.length > 0
                        color: content.length > 0 ? "white" : "#888888"
                        wrapMode: Text.WordWrap
                        textFormat: Text.RichText
                        readOnly: true
                        selectByMouse: true
                        selectByKeyboard: true
                    }
                    Label {
                        visible: hasWarning
                        text: "⚠ " + warning
                        color: "#ff9800"
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
            }
        }
    }
    footer: ToolBar {
        id: footerBar
        height: Math.min(footerCol.implicitHeight + 12, parent.height * 0.45)
        property bool hasUploadingFiles: app.chat.activeDraftAttachments.some(item => item.status === "uploading")
        ColumnLayout {
            id: footerCol
            anchors.fill: parent
            anchors.margins: 6
            spacing: 6
            Row {
                visible: app.chat.activeDraftAttachments.length > 0
                spacing: 8
                Repeater {
                    model: app.chat.activeDraftAttachments
                    Rectangle {
                        width: 72
                        height: 72
                        radius: 6
                        color: "#1e1e1e"
                        border.color: modelData.status === "error" ? "#f4
        4336" : "transparent"
                        border.width: 2
                        clip: true
                        Image {
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectCrop
                            source: modelData.localUrl || ""
                            opacity: modelData.status === "ready" ? 1.0 : 0.6
                        }
                        ProgressBar {
                            visible: modelData.status === "uploading"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            value: modelData.progress
                        }
                        RoundButton {
                            visible: modelData.status === "error"
                            anchors.centerIn: parent
                            width: 30
                            height: 30
                            text: "↻"
                            onClicked: app.chat.retryAttachment(modelData.localId)
                        }
                        RoundButton {
                            width: 22
                            height: 22
                            anchors.top: parent.top
                            anchors.right: parent.right
                            text: "✕"
                            font.pixelSize: 10
                            onClicked: app.chat.removeDraftAttachment(modelData.localId)
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: "📎"
                    visible: app.models.capabilities.includes("image")
                    enabled: app.models.serverStatus === "LOADED"
                    onClicked: imagePicker.open()
                    Layout.alignment: Qt.AlignBottom
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    TextArea {
                        id: promptInput
                        width: parent.width
                        placeholderText: app.models.serverStatus === "LOADED" || app.models.serverStatus === "GENERATING"
                                         ? "Введите сообщение..."
                                         : "Сначала загрузите модель..."
                        enabled: app.models.serverStatus === "LOADED"
                        wrapMode: TextArea.Wrap
                    }
                }
                Button {
                    text: "Отправить"
                    enabled: app.models.serverStatus === "LOADED"
                             && !footerBar.hasUploadingFiles
                             && (promptInput.text.trim().length > 0 || app.chat.activeDraftAttachments.length > 0)
                    onClicked: {
                        app.chat.sendMessage(promptInput.text)
                        promptInput.clear()
                    }
                    Layout.alignment: Qt.AlignBottom
                }
            }
        }
    }
}