import Callie.Ui
import QtQuick

/// Diagonal stripes inside a rounded rectangle, laid over an event in its
/// calendar's edge color when the user answered "maybe". Shows nothing for
/// any other answer.
Canvas {
    id: root

    property color calendarColor
    property string response
    property real radius

    readonly property color color: Theme.tint(Theme.calendarEdge(calendarColor, Theme.calendar),
                                              0.5)

    visible: response === "tentative"

    onColorChanged: requestPaint()
    onRadiusChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const r = Math.min(radius, width / 2, height / 2)
        ctx.beginPath()
        ctx.roundedRect(0, 0, width, height, r, r)
        ctx.clip()
        ctx.strokeStyle = color
        ctx.lineWidth = Theme.stripeWidth
        ctx.beginPath()
        for (let x = -height; x < width; x += Theme.stripeStep) {
            ctx.moveTo(x, height)
            ctx.lineTo(x + height, 0)
        }
        ctx.stroke()
    }
}
