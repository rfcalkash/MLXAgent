import QtQuick
import QtQuick.Controls.Material
import QtQuick.Shapes

Item {
    id: root

    // Основные свойства
    property real value: 0.0          // Значение от 0.0 до 1.0
    property real lineWidth: 4        // Толщина кольца (в Material обычно 3-4px)
    property color progressColor: Material.accent // Синий акцентный цвет темы
    property color trackColor: Qt.alpha(progressColor, 0.2) // Полупрозрачный фон (или "#E0E0E0")
    property bool showText: false     // Показывать ли проценты в центре

    // Стандартный размер Material BusyIndicator
    implicitWidth: 48
    implicitHeight: 48

    // Ограничиваем значение диапазоном [0, 1] и плавно анимируем
    readonly property real clampedValue: Math.max(0.0, Math.min(1.0, value))
    property real animatedValue: clampedValue

    Behavior on animatedValue {
        NumberAnimation {
            duration: 250
            easing.type: Easing.OutCubic
        }
    }

    Shape {
        anchors.fill: parent
        // Включаем мультисэмплинг для идеально гладких краев
        layer.enabled: true
        layer.samples: 4

        // 1. Фоновое серое кольцо (трек)
        ShapePath {
            strokeWidth: root.lineWidth
            strokeColor: root.trackColor
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: (Math.min(root.width, root.height) - root.lineWidth) / 2
                radiusY: radiusX
                startAngle: 0
                sweepAngle: 360
            }
        }

        // 2. Заполняющийся синий сегмент
        ShapePath {
            strokeWidth: root.lineWidth
            strokeColor: root.progressColor
            fillColor: "transparent"
            // Закругленные концы дуги, как в Material Design 3
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: (Math.min(root.width, root.height) - root.lineWidth) / 2
                radiusY: radiusX
                startAngle: -90 // Начинаем строго сверху (на 12 часах)
                sweepAngle: 360 * root.animatedValue
            }
        }
    }

    // Опциональный текст с процентами по центру
    Text {
        visible: root.showText
        anchors.centerIn: parent
        text: Math.round(root.animatedValue * 100) + "%"
        color: Material.foreground
        font.pixelSize: Math.min(root.width, root.height) * 0.26
        font.bold: true
    }
}
