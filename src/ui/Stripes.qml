pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick

/// Diagonal stripes inside a rounded rectangle, laid over an event in its
/// calendar's edge color when the user answered "maybe". Shows nothing for
/// any other answer, and only then makes the canvas that draws them.
Item {
    id: root

    property color calendarColor
    property string response
    property real radius

    readonly property color color: Theme.tint(Theme.calendarEdge(calendarColor, Theme.calendar),
                                              0.5)

    Loader {
        anchors.fill: parent
        active: root.response === "tentative"

        sourceComponent: Canvas {
            id: canvas

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const r = Math.min(root.radius, width / 2, height / 2)
                ctx.beginPath()
                ctx.roundedRect(0, 0, width, height, r, r)
                ctx.clip()
                ctx.strokeStyle = root.color
                ctx.lineWidth = Theme.stripeWidth
                ctx.beginPath()
                for (let x = -height; x < width; x += Theme.stripeStep) {
                    ctx.moveTo(x, height)
                    ctx.lineTo(x + height, 0)
                }
                ctx.stroke()
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            Connections {
                target: root
                function onColorChanged() {
                    canvas.requestPaint()
                }
                function onRadiusChanged() {
                    canvas.requestPaint()
                }
            }
        }
    }
}
