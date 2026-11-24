import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MlxClient
import QtQuick.Controls.Material

ToolBar {
    id: headerBar
    property bool expanded: false
    property bool showFull: expanded || (app.models.serverStatus !== "LOADED" && app.models.serverStatus !== "GENERATING")
    clip: true
    height: (optionsPane.visible ? optionsPane.implicitHeight : statusPane.implicitHeight) + 12
    Behavior on height { NumberAnimation { duration: 200 } }
    ColumnLayout {
        id: optionsPane
        anchors.fill: parent
        anchors.margins: 6            
        
        RowLayout {
            Layout.fillWidth: true
            // Layout.alignment: Qt.AlignTop
            Layout.preferredHeight: 60
            RoundButton {
                text: "☰"
                onClicked: chatsDrawer.open()
                flat: true
            }
            RoundButton {
                text: headerBar.expanded ? "▲" : "▼"
                onClicked: headerBar.expanded = !headerBar.expanded
                flat: true
            }
            TextField {
                id: urlField
                Layout.fillWidth: true
                visible: headerBar.showFull
                text: app.transport.serverUrl
                placeholderText: "http://192.168.1.X:8000"
            }
            Button {
                text: "Подключить"
                visible: headerBar.showFull
                onClicked: app.transport.serverUrl = urlField.text
            }
            ColumnLayout {
                visible: !headerBar.showFull
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
            Label {
                text: app.models.serverStatus + (app.models.textOnlyMode && app.models.serverStatus === "LOADED" ? " [TXT]" : "")
                font.bold: true
                color: app.models.serverStatus === "LOADED" ? Material.color(Material.Green) :
                                                              (app.models.serverStatus === "OFFLINE" ? Material.color(Material.Red) : Material.color(Material.Orange))
            }
        }
        
        RowLayout {
            Layout.fillWidth: true
            visible: headerBar.showFull
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
