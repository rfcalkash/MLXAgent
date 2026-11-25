import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

// Основной компонент: ChatInput.qml
Item {
    id: root

    // --- Входящий контракт ---
    required property string serverStatus
    required property var capabilities
    required property var draftAttachments
    property int thumbHeight: 50
    property int maxHeight: 200
    property color inactiveColor: "grey"
    property color accentColor: "blue"
    property alias textColor: inputField.color

    // implicitHeight: childrenRect.height

    // --- Исходящие сигналы ---
    signal attachClicked()
    signal retryAttachmentClicked(string localId)
    signal removeAttachmentClicked(string localId)
    signal sendClicked(string text)

    function hasCapability(cap) {
        return capabilities && capabilities.indexOf(cap) !== -1
    }

    Component.onCompleted: console.log("root "+height)
    onHeightChanged: console.log("root "+height)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10

        Component.onCompleted: console.log("col "+height)
        onHeightChanged: console.log("col "+height)

        ScrollableTextArea {
            Layout.fillHeight: true
            Layout.fillWidth: true
        }

        ChatButtons {
            Layout.fillWidth: true
        }
    }

    // --- Вспомогательный компонент: Миниатюра ---
    component AttachmentThumbnail: Item {
        id: thumbnailRoot
        property var attachmentData: null
        signal removeRequested()

        Rectangle {
            anchors.fill: parent
            color: "#F0F0F0"
            radius: 8
            clip: true

            Image {
                anchors.fill: parent
                source: attachmentData ? attachmentData.url : ""
                fillMode: Image.PreserveAspectCrop
            }

            // Кнопка удаления (появляется при наведении или тапе)
            Button {
                id: deleteBtn
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 2
                width: 24
                height: 24
                padding: 0
                icon.name: "edit-clear"
                icon.color: "white"

                // Логика видимости: на ПК - hover, на мобилках - нажатие
                visible: hoverHandler.hovered || tapHandler.pressed

                onClicked: thumbnailRoot.removeRequested()
            }

            HoverHandler { id: hoverHandler }
            TapHandler { id: tapHandler }
        }
    }
}