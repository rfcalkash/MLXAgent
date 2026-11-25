import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

Rectangle {
    id: root
    property alias text: inputField.text

    implicitHeight: scrollView.contentHeight+pads
    color: "transparent"
    border.color: inputField.activeFocus ? Material.accentColor : Material.dividerColor
    radius: 12
    clip: true
    property int pads: inputField.topPadding + inputField.bottomPadding + scrollView.anchors.margins*2
    
    
    // Контейнер для скролла
    ScrollView {
        id: scrollView
        anchors.fill: parent
        anchors.margins: 4 // Небольшой отступ от рамки
        clip: true
        
        // Настройка скроллбара (скрыт, пока не нужен)
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        
        TextArea {
            id: inputField
            placeholderText: "Напишите сообщение..."
            placeholderTextColor: "red"
            wrapMode: TextArea.Wrap
            font.pixelSize: 14
            background: null
            verticalAlignment: Text.AlignVCenter
        }
    }
}
