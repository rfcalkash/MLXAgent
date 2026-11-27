import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MlxClient
import QtQuick.Controls.Material
import QtQuick.Controls.impl

Column{
    id: thumbnailRoot

    property var attachmentData: null
    signal removeRequested()

    width: Material.delegateHeight*3
    height: Material.delegateHeight*4
    Rectangle {
        width: parent.width
        height: width
        // color: "#F0F0F0"
        radius: 8
        clip: true
        
        Item{
            anchors.fill: parent
            Image {
                id: imgId
                visible: attachmentData && attachmentData.type === "image" && status!==Image.Error
                anchors.fill: parent
                source: attachmentData.localUrl
                fillMode: Image.PreserveAspectCrop
            }
            IconImage{
                visible: !imgId.visible
                anchors.fill: parent
                sourceSize: Qt.size(width, height)
                source: "icons/file.svg"
            }
        }
        
        // Кнопка удаления (появляется при наведении или тапе)
        RoundButton {
            id: deleteBtn
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 2
            width: Material.delegateHeight
            height: width
            padding: 0
            icon.source: "icons/trash.svg"
            
            // Логика видимости: на ПК - hover, на мобилках - нажатие
            visible: hoverHandler.hovered || Qt.platform.os === "android" || Qt.platform.os === "ios"
            
            onClicked: thumbnailRoot.removeRequested()
        }

        RoundProgressBar{
            anchors.centerIn: parent
            width: Material.delegateHeight
            height: width
            visible: attachmentData.status === "uploading"
            value: attachmentData.progress
        }
        
        HoverHandler { id: hoverHandler }
        // TapHandler { id: tapHandler }
    }
    Label{
        text: attachmentData.localUrl
        fontSizeMode: Text.HorizontalFit
        font.pixelSize: 12
        minimumPixelSize: 6
        width: parent.width
        height: Material.delegateHeight
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideLeft
    }
}
