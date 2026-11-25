import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

// Основной компонент: ChatInput.qml
RowLayout {
    id: root

    signal attachClicked()
    signal sendClicked()

    property alias showAttachments: attachButton.visible
    property alias canSend: sendButton.enabled
    property int attachmentsCount: 0
    spacing: 4
    
    // Кнопка "Добавить"
    RoundButton {
        id: attachButton
        visible: root.showAttachments
        // flat: true
        icon.name: "image-add"
        // icon.color: root.accentColor
        onClicked: root.attachClicked()
        Rectangle{
            color: Material.primaryColor
            height: parent.height*0.3
            width: height
            radius: height/2
            visible: root.attachmentsCount>0
            anchors{bottom: parent.bottom; right: parent.right}
            Label{
                height: parent.height*0.7
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                width: height
                anchors.centerIn: parent
                text: root.attachmentsCount
                color: Material.primaryTextColor
                visible: root.attachmentsCount<10
                fontSizeMode: Text.Fit
            }
        }
    }
    
    Item { Layout.fillWidth: true } // Пружина
    
    // Кнопка "Отправить"
    RoundButton {
        id: sendButton
        // flat: true
        icon.name: "media-send"
        // icon.color: enabled ? root.accentColor : root.inactiveColor
        // enabled: inputField.text.length > 0 || draftAttachments.length > 0
        onClicked: {
            root.sendClicked()
            // inputField.clear()
        }
    }
}
