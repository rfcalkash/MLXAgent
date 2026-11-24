import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: footerBar

    // Входящий контракт
    required property string serverStatus
    required property var capabilities
    required property var draftAttachments
    property int maxHeight

    // Исходящие сигналы
    signal attachClicked()
    signal retryAttachmentClicked(string localId)
    signal removeAttachmentClicked(string localId)
    signal sendClicked(string text)

    height: Math.min(footerCol.implicitHeight + 12, maxHeight)
    property bool hasUploadingFiles: draftAttachments.some(item => item.status === "uploading")

    ColumnLayout {
        id: footerCol
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        Row {
            visible: footerBar.draftAttachments.length > 0
            spacing: 8

            Repeater {
                model: footerBar.draftAttachments
                Rectangle {
                    width: 72
                    height: 72
                    radius: 6
                    color: "#1e1e1e"
                    border.color: modelData.status === "error" ? "#f44336" : "transparent"
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
                        onClicked: footerBar.retryAttachmentClicked(modelData.localId)
                    }
                    RoundButton {
                        width: 22
                        height: 22
                        anchors.top: parent.top
                        anchors.right: parent.right
                        text: "✕"
                        font.pixelSize: 10
                        onClicked: footerBar.removeAttachmentClicked(modelData.localId)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Button {
                text: "📎"
                visible: footerBar.capabilities.includes("image")
                enabled: footerBar.serverStatus === "LOADED"
                onClicked: footerBar.attachClicked()
                Layout.alignment: Qt.AlignBottom
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                TextArea {
                    id: promptInput
                    width: parent.width
                    placeholderText: footerBar.serverStatus === "LOADED" || footerBar.serverStatus === "GENERATING"
                                     ? "Введите сообщение..."
                                     : "Сначала загрузите модель..."
                    enabled: footerBar.serverStatus === "LOADED"
                    wrapMode: TextArea.Wrap
                }
            }
            Button {
                text: "Отправить"
                enabled: footerBar.serverStatus === "LOADED"
                         && !footerBar.hasUploadingFiles
                         && (promptInput.text.trim().length > 0 || footerBar.draftAttachments.length > 0)
                onClicked: {
                    footerBar.sendClicked(promptInput.text)
                    promptInput.clear()
                }
                Layout.alignment: Qt.AlignBottom
            }
        }
    }
}
