import QtQuick

/// Diagonal stripes inside a rounded rectangle, laid over an event to mark
/// one the user answered "maybe".
Canvas {
    id: root

    property color color
    property real radius

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
        ctx.lineWidth = 3
        const step = 8
        ctx.beginPath()
        for (let x = -height; x < width; x += step) {
            ctx.moveTo(x, height)
            ctx.lineTo(x + height, 0)
        }
        ctx.stroke()
    }
}
