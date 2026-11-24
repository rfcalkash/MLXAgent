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

    // База с едва уловимым тёплым подтоном (слоновая кость днём, глубокий уголь ночью)
    Material.background: isDark ? "#121413" : "#F7F6F2"
    Material.foreground: isDark ? "#E6E4DF" : "#1C1E1D"

    // Primary: Благородный изумрудно-хвойный
    Material.primary: isDark ? "#4DB6AC" : "#1B5E55"

    // Accent: Дорогой янтарно-золотой / терракотовый (противоположен на цветовом круге)
    Material.accent: isDark ? "#E6A15C" : "#C86D2D"

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

    header: HeaderBar {
        id: headerBar
        Material.background: window.Material.background
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

            function fixWidth(){
                thinkText.wrapMode = Text.NoWrap
                contentText.wrapMode = Text.NoWrap
                Qt.callLater(() => {
                    thinkText.wrapMode = Text.WordWrap
                    contentText.wrapMode = Text.WordWrap
                })
            }
            onIsStreamingChanged: {
                if (isStreaming) chatList.positionViewAtEnd()
                else {
                    fixWidth()
                }
            }
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
                        id: contentText
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
    footer: MyFooterBar {
        id: footerBar
        Material.background: window.Material.background
    }
}