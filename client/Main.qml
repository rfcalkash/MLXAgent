import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MlxClient
import QtQuick.Controls.Material

ApplicationWindow {
    id: window
    width: 900
    height: 650
    visible: true
    title: "MLX Local Client"

    readonly property bool isDark: Material.theme === Material.Dark

    Material.theme: Material.System
    Material.background: isDark ? "#121413" : "#F7F6F2"
    Material.foreground: isDark ? "#E6E4DF" : "#1C1E1D"
    Material.primary: isDark ? "#4DB6AC" : "#1B5E55"
    Material.accent: isDark ? "#E6A15C" : "#C86D2D"

    // Системная кнопка "Назад" на Android закрывает Drawer или выходит из настроек
    onClosing: (close) => {
                   if (chatsDrawer.opened) {
                       chatsDrawer.close()
                       close.accepted = false
                   } else if (stackView.depth > 1) {
                       stackView.pop()
                       close.accepted = false
                   }
               }

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
            Label { text: "Repo ID (например, mlx-community/gemma-4-E4B-it-4bit):" }
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
        // closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        visible: !app.auth.isLoggedIn
        width: authLayout.width + 20
        ColumnLayout {
            spacing: 10
            id: authLayout
            anchors.centerIn: parent
            width: Math.min(window.width * 0.8, authButtonsRow.width+20)
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
                id: authButtonsRow
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

    // Выдвижная панель чатов со своей кнопкой "назад"
    Drawer {
        id: chatsDrawer
        width: Math.min(window.width * 0.8, 300)
        height: window.height

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                RoundButton {
                    text: "←"
                    flat: true
                    onClicked: chatsDrawer.close()
                }
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
                highlighted: true
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

    // Полноэкранный StackView для навигации между чатом и настройками
    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: chatPageComponent
    }

    // Экран настроек (пушится поверх всего окна)
    Component {
        id: settingsPageComponent
        Settings {
            Material.background: window.Material.background

            serverUrl: app.transport.serverUrl
            serverStatus: app.models.serverStatus
            activeModel: app.models.activeModel
            textOnlyMode: app.models.textOnlyMode
            isBusy: app.models.isBusy
            models: app.models.models
            activeModelIndex: app.models.activeModelIndex
            toolCtrl: app.tools

            onBackClicked: stackView.pop()
            onConnectClicked: (url) => app.transport.serverUrl = url
            onLoadModelClicked: (modelId, textOnly) => app.models.loadModel(modelId, textOnly)
            onUnloadModelClicked: app.models.unloadModel()
            onDownloadModelClicked: downloadDialog.open()
            onDeleteModelClicked: (modelId) => app.models.deleteModel(modelId)
        }
    }

    // Главный экран чата (верхняя полоска + список сообщений + плашка Ask + футер)
    Component {
        id: chatPageComponent
        Page {
            Material.background: window.Material.background

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Лаконичный верхний бар чата
                ToolBar {
                    Layout.fillWidth: true
                    Material.background: window.Material.background

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 4

                        RoundButton {
                            text: "☰"
                            flat: true
                            font.pixelSize: 18
                            onClicked: chatsDrawer.open()
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Label {
                                text: app.chat.activeChatTitle
                                font.bold: true
                                font.pixelSize: 15
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 6

                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: app.models.serverStatus === "LOADED" ? Material.color(Material.Green) :
                                                                                  (app.models.serverStatus === "OFFLINE" ? Material.color(Material.Red) : Material.color(Material.Orange))
                                }

                                Label {
                                    text: app.models.activeModel
                                          ? (app.models.activeModel + (app.models.textOnlyMode ? " [TXT]" : ""))
                                          : app.models.serverStatus
                                    font.pixelSize: 11
                                    opacity: 0.75
                                    elide: Text.ElideMiddle
                                    Layout.maximumWidth: 260
                                }
                            }
                        }

                        RoundButton {
                            text: "⚙"
                            flat: true
                            font.pixelSize: 18
                            onClicked: stackView.push(settingsPageComponent)
                        }
                    }
                }
                // Список сообщений чата
                ListView {
                    id: chatList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 10
                    spacing: 10
                    clip: true
                    model: app.chat.model
                    onCountChanged: positionViewAtEnd()
                    // Автодоскролл при появлении виртуальной клавиатуры на Android
                    onHeightChanged: if (atYEnd) positionViewAtEnd()
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
                        required property var toolCalls
                        property bool thinkExpanded: false
                        property bool toolResultExpanded: false
                        property bool isUser: role === "user"
                        property bool isToolResult: role === "tool"
                        property bool hasThink: think.length > 0
                        property bool hasWarning: warning.length > 0
                        property bool hasToolCalls: toolCalls && toolCalls.length > 0
                        property bool isStreaming: !isUser && !isToolResult
                                                   && index === chatList.count - 1
                                                   && app.models.serverStatus === "GENERATING"
                        Rectangle {
                            id: bubble
                            width: Math.min(parent.width * 0.88,
                                            Math.max(contentCol.implicitWidth + 24,
                                                     parent.thinkExpanded ? thinkText.implicitWidth + 24 : 140,
                                                     parent.toolResultExpanded ? toolOutText.implicitWidth + 24 : 140))
                            height: contentCol.implicitHeight + 20
                            radius: 12
                            color: isUser ? "#2b5278" : (isToolResult ? "#222624" : "#2d2d2d")
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
                                ToolButton {
                                    visible: !isUser && !isToolResult && hasThink
                                    text: thinkExpanded ? "▼ Скрыть рассуждения" : "▶ Ход мыслей..."
                                    flat: true
                                    font.pixelSize: 12
                                    Layout.preferredHeight: 24
                                    onClicked: thinkExpanded = !thinkExpanded
                                }
                                Rectangle {
                                    visible: !isUser && !isToolResult && hasThink && thinkExpanded
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
                                ColumnLayout {
                                    visible: !isUser && !isToolResult && hasToolCalls
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Repeater {
                                        model: toolCalls
                                        Rectangle {
                                            Layout.fillWidth: true
                                            implicitHeight: callLabel.implicitHeight + 8
                                            color: "#1e2422"
                                            border.color: "#4DB6AC"
                                            border.width: 1
                                            radius: 6
                                            Label {
                                                id: callLabel
                                                anchors.fill: parent
                                                anchors.margins: 4
                                                font.pixelSize: 12
                                                color: "#80CBC4"
                                                wrapMode: Text.WordWrap
                                                text: "🔧 " + modelData.function.name + "(" + JSON.stringify(modelData.function.arguments) + ")"
                                            }
                                        }
                                    }
                                }
                                ToolButton {
                                    visible: isToolResult
                                    text: toolResultExpanded ? "▼ Скрыть вывод инструмента" : "▶ Результат инструмента (" + content.length + " симв.)"
                                    font.pixelSize: 12
                                    Material.foreground: "#80CBC4"
                                    Layout.preferredHeight: 24
                                    onClicked: toolResultExpanded = !toolResultExpanded
                                }
                                Rectangle {
                                    visible: isToolResult && toolResultExpanded
                                    Layout.fillWidth: true
                                    implicitHeight: Math.min(toolOutText.implicitHeight + 12, 240)
                                    color: "#181a19"
                                    radius: 6
                                    TextArea {
                                        id: toolOutText
                                        anchors.fill: parent
                                        text: content
                                        color: "#9e9e9e"
                                        font.family: "Menlo"
                                        font.pixelSize: 11
                                        wrapMode: Text.WrapAnywhere
                                        readOnly: true
                                        selectByMouse: true
                                    }
                                }
                                TextArea {
                                    id: contentText
                                    Layout.fillWidth: true
                                    text: {
                                        if (content.length > 0) return app.chat.formatContent(content, color, font.pixelSize)
                                        if (isStreaming) return hasThink ? "Формулирую ответ..." : "Думаю..."
                                        return ""
                                    }
                                    visible: !isToolResult && text.length > 0
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
                // Плашка запроса разрешения (Ask)
                Rectangle {
                    visible: app.tools.pendingApprovals.length > 0
                    Layout.fillWidth: true
                    implicitHeight: approvalRow.implicitHeight + 16
                    color: "#2b2518"
                    border.color: "#E6A15C"
                    border.width: 1
                    RowLayout {
                        id: approvalRow
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 10
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            textFormat: Text.StyledText
                            text: app.tools.pendingApprovals.length > 0
                                  ? "🔧 Запрос разрешения: " + app.tools.pendingApprovals[0].name + "" + app.tools.pendingApprovals[0].argsJson + ""
                                  : ""
                        }
                        Button {
                            text: "Разрешить"
                            highlighted: true
                            onClicked: Qt.callLater(() => app.tools.approveCall(app.tools.pendingApprovals[0].callId))
                        }
                        Button {
                            text: "Отклонить"
                            onClicked: Qt.callLater(() => app.tools.rejectCall(app.tools.pendingApprovals[0].callId))
                        }
                    }
                }
                // Нижняя панель ввода внутри ColumnLayout
                MyFooterBar {
                    id: footerBar
                    Layout.fillWidth: true
                    // Layout.alignment: Qt.AlignBottom
                    // Material.background: window.Material.background
                    serverStatus: app.models.serverStatus
                    capabilities: app.models.capabilities
                    draftAttachments: app.chat.activeDraftAttachments
                    onAttachClicked: imagePicker.open()
                    onRetryAttachmentClicked: (localId) => app.chat.retryAttachment(localId)
                    onRemoveAttachmentClicked: (localId) => app.chat.removeDraftAttachment(localId)
                    onSendClicked: (text) => app.chat.sendMessage(text)
                    // maxHeight: window.height * 0.45
                    Layout.maximumHeight: window.height * 0.45
                    Layout.minimumHeight: Material.delegateHeight
                }
            }
        }
    }
}