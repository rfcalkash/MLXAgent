import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MlxClient
import QtQuick.Controls.Material

ToolBar {
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
