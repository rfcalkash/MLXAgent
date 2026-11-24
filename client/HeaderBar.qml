import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

ToolBar {
    id: headerBar

    // Входящий контракт (required гарантирует ошибку на этапе запуска, если забыть пробросить)
    required property string serverUrl
    required property string serverStatus
    required property string activeModel
    required property bool textOnlyMode
    required property bool isBusy
    required property var models
    required property int activeModelIndex

    // Исходящие сигналы
    signal menuClicked()
    signal connectClicked(string url)
    signal loadModelClicked(string modelId, bool textOnly)
    signal unloadModelClicked()
    signal downloadModelClicked()
    signal deleteModelClicked(string modelId)

    property bool expanded: false
    property bool showFull: expanded || (serverStatus !== "LOADED" && serverStatus !== "GENERATING")

    // Защита от разрушения декларативных биндингов при ручном вводе/выборе пользователем
    onServerUrlChanged: urlField.text = serverUrl
    onActiveModelIndexChanged: modelSelector.currentIndex = activeModelIndex
    Component.onCompleted: {
        urlField.text = serverUrl
        modelSelector.currentIndex = activeModelIndex
    }

    clip: true
    height: optionsPane.implicitHeight + 12
    Behavior on height { NumberAnimation { duration: 200 } }

    ColumnLayout {
        id: optionsPane
        anchors.fill: parent
        anchors.margins: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 60

            RoundButton {
                text: "☰"
                flat: true
                onClicked: headerBar.menuClicked()
            }
            RoundButton {
                text: headerBar.expanded ? "▲" : "▼"
                flat: true
                onClicked: headerBar.expanded = !headerBar.expanded
            }
            TextField {
                id: urlField
                Layout.fillWidth: true
                visible: headerBar.showFull
                placeholderText: "http://192.168.1.X:8000"
            }
            Button {
                text: "Подключить"
                visible: headerBar.showFull
                onClicked: headerBar.connectClicked(urlField.text)
            }
            ColumnLayout {
                visible: !headerBar.showFull
                Label {
                    textFormat: Text.StyledText
                    text: "<b>Сервер: </b><i>" + headerBar.serverUrl + "</i>"
                    Layout.fillWidth: true
                }
                Label {
                    text: "<b>Модель: </b><i>" + headerBar.activeModel + "</i>"
                    Layout.fillWidth: true
                }
            }
            Label {
                text: headerBar.serverStatus + (headerBar.textOnlyMode && headerBar.serverStatus === "LOADED" ? " [TXT]" : "")
                font.bold: true
                color: headerBar.serverStatus === "LOADED" ? Material.color(Material.Green) :
                       (headerBar.serverStatus === "OFFLINE" ? Material.color(Material.Red) : Material.color(Material.Orange))
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: headerBar.showFull

            ComboBox {
                id: modelSelector
                Layout.fillWidth: true
                model: headerBar.models
                textRole: "id"
                enabled: !headerBar.isBusy && headerBar.serverStatus !== "LOADED"
            }
            CheckBox {
                id: textOnlyCheck
                text: "Text-only"
                checked: false
                enabled: !headerBar.isBusy && headerBar.serverStatus !== "LOADED"
            }
            Button {
                text: headerBar.serverStatus === "LOADED" ? "Выгрузить" : "Загрузить"
                enabled: !headerBar.isBusy && (headerBar.serverStatus === "LOADED" || modelSelector.currentIndex >= 0)
                onClicked: {
                    if (headerBar.serverStatus === "LOADED") {
                        headerBar.unloadModelClicked()
                    } else {
                        var selectedId = headerBar.models[modelSelector.currentIndex].id
                        headerBar.loadModelClicked(selectedId, textOnlyCheck.checked)
                    }
                }
            }
            Button {
                text: "+ HF"
                enabled: !headerBar.isBusy
                onClicked: headerBar.downloadModelClicked()
            }
            Button {
                text: "Удалить"
                enabled: !headerBar.isBusy && headerBar.serverStatus !== "LOADED" && modelSelector.currentIndex >= 0
                onClicked: {
                    var selectedId = headerBar.models[modelSelector.currentIndex].id
                    headerBar.deleteModelClicked(selectedId)
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
