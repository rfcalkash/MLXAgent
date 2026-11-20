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
        model: api.messages
        onCountChanged: positionViewAtEnd()

        delegate: Item {
            width: chatList.width
            height: bubble.height

            property bool isUser: modelData.role === "user"

            Rectangle {
                id: bubble
                width: Math.min(parent.width * 0.8, msgText.implicitWidth + 24)
                height: msgText.implicitHeight + 20
                radius: 12
                color: isUser ? "#2b5278" : "#333333"
                anchors.right: isUser ? parent.right : undefined
                anchors.left: isUser ? undefined : parent.left

                Text {
                    id: msgText
                    anchors.fill: parent
                    anchors.margins: 10
                    text: modelData.content
                    color: "white"
                    wrapMode: Text.WordWrap
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
