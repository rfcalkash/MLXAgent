import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Controls.impl

ToolButton {
    id: control
    property real iconAngle: 0


    contentItem: Row {
        // implicitWidth: control.width
        // implicitHeight: control.height

        IconImage {
            id: iconImg
            // anchors.fill: parent
            source: control.icon.source
            rotation: control.iconAngle
            visible: control.icon.source
            anchors.verticalCenter: parent.verticalCenter
            color: controlText.color

            Behavior on rotation {
                NumberAnimation { duration: 150 }
            }

        }
        Text {
            id: controlText
            text: control.text
            font: control.font
            opacity: enabled ? 1.0 : 0.3
            color: control.down ?  Material.accentColor : (control.hovered? Material.primaryHighlightedTextColor:Material.primaryTextColor)
            Behavior on color {
                ColorAnimation { duration: 150 }
            }
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }

    }

    background: Item{}
}
