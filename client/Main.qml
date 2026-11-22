import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MlxClient

ApplicationWindow {
    id: window
    width: 800
    height: 600
    visible: true
    title: "MLX Local Client"

    ApiClient {
        id: api
        Component.onCompleted: refreshAll()
        onErrorOccurred: (msg) => {
            errorDialog.text = msg
            errorDialog.open()
        }
    }

    // Диалог ошибок
    Dialog {
        id: errorDialog
        property alias text: errorLabel.text
        title: "Ошибка"
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        Label { id: errorLabel; wrapMode: Text.WordWrap }
    }

    // Диалог скачивания новой модели с HF
    Dialog {
        id: downloadDialog
        title: "Скачать модель (HuggingFace)"
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: api.downloadModel(repoInput.text)
        ColumnLayout {
            width: 300
            Label { text: "Repo ID (например, mlx-community/Qwen2.5-7B-Instruct-4bit):" }
            TextField {
                id: repoInput
                Layout.fillWidth: true
                placeholderText: "mlx-community/..."
            }
        }
    }

    // Верхняя панель управления
    header: ToolBar {
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6

            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: urlField
                    Layout.fillWidth: true
                    text: api.serverUrl
                    placeholderText: "http://192.168.1.X:8000"
                }
                Button {
                    text: "Подключить"
                    onClicked: api.serverUrl = urlField.text
                }
                Label {
                    text: api.serverStatus
                    font.bold: true
                    color: api.serverStatus === "LOADED" ? "green" :
                           api.serverStatus === "OFFLINE" ? "red" : "orange"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: modelSelector
                    Layout.fillWidth: true
                    model: api.models
                    textRole: "id"
                    enabled: !api.isBusy
                }
                Button {
                    text: api.serverStatus === "LOADED" ? "Выгрузить" : "Загрузить"
                    enabled: !api.isBusy && (api.serverStatus === "LOADED" || modelSelector.currentIndex >= 0)
                    onClicked: {
                        if (api.serverStatus === "LOADED") {
                            api.unloadModel()
                        } else {
                            var selectedId = api.models[modelSelector.currentIndex].id
                            api.loadModel(selectedId)
                        }
                    }
                }
                Button {
                    text: "+ HF"
                    enabled: !api.isBusy
                    onClicked: downloadDialog.open()
                }
                Button {
                    text: "Удалить"
                    enabled: !api.isBusy && modelSelector.currentIndex >= 0
                    onClicked: {
                        var selectedId = api.models[modelSelector.currentIndex].id
                        api.deleteModel(selectedId)
                    }
                }
            }
        }
    }

    // Область чата
    ListView {
        id: chatList
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10
        clip: true
        model: api.chatModel
        onCountChanged: positionViewAtEnd()

        delegate: Item {
            width: chatList.width
            height: bubble.height

            required property int index
            required property string role
            required property string content
            required property string think
            required property string warning

            // Локальное UI-состояние карточки
            property bool thinkExpanded: false

            // Вычисляемые свойства
            property bool isUser: role === "user"
            property bool hasThink: think.length > 0
            property bool hasWarning: warning.length > 0
            property bool isStreaming: !isUser
                                       && index === chatList.count - 1
                                       && api.serverStatus === "GENERATING"

            Rectangle {
                id: bubble
                width: Math.min(parent.width * 0.85, Math.max(contentCol.implicitWidth + 24, 120))
                height: contentCol.implicitHeight + 20
                radius: 12
                color: isUser ? "#2b5278" : "#2d2d2d"
                anchors.right: isUser ? parent.right : undefined
                anchors.left: isUser ? undefined : parent.left

                ColumnLayout {
                    id: contentCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

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

                        Text {
                            id: thinkText
                            anchors.fill: parent
                            anchors.margins: 6
                            text: think
                            color: "#aaaaaa"
                            font.pixelSize: 12
                            font.italic: true
                            wrapMode: Text.WordWrap
                            textFormat: Text.MarkdownText
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: {
                            if (content.length > 0) return content
                            if (isStreaming) return hasThink ? "Формулирую ответ..." : "Думаю..."
                            return ""
                        }
                        visible: text.length > 0
                        color: content.length > 0 ? "white" : "#888888"
                        wrapMode: Text.WordWrap
                        textFormat: Text.MarkdownText
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

    // Нижняя панель ввода
    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 6

            Button {
                text: "Очистить"
                onClicked: api.clearChat()
            }
            TextArea {
                id: promptInput
                Layout.fillWidth: true
                placeholderText: api.serverStatus === "LOADED" ? "Введите сообщение..." : "Сначала загрузите модель..."
                enabled: api.serverStatus === "LOADED"
                wrapMode: TextArea.Wrap
            }
            Button {
                text: "Отправить"
                enabled: api.serverStatus === "LOADED" && promptInput.text.trim().length > 0
                onClicked: {
                    api.sendMessage(promptInput.text)
                    promptInput.clear()
                }
            }
        }
    }
}
