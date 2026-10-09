pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick

/// A small line icon drawn from rounded bars, so icons need no image files and
/// take the text color: chevron-left, chevron-right, minimize, maximize,
/// restore, close, check, plus, refresh, bell, search, settings, menu, video or more.
Item {
    id: root

    property string name
    property color color: Theme.text
    property real stroke: 2

    implicitWidth: 14
    implicitHeight: 14

    // Each bar is [length, angle, x, y], with sizes as fractions of the icon.
    readonly property var bars: {
        switch (name) {
        case "chevron-left":
            return [[0.5, -45, 0.47, 0.36], [0.5, 45, 0.47, 0.64]]
        case "chevron-right":
            return [[0.5, 45, 0.53, 0.36], [0.5, -45, 0.53, 0.64]]
        case "minimize":
            return [[0.62, 0, 0.5, 0.5]]
        case "close":
            return [[0.74, 45, 0.5, 0.5], [0.74, -45, 0.5, 0.5]]
        case "check":
            return [[0.34, 45, 0.33, 0.6], [0.62, -50, 0.6, 0.47]]
        case "video":
            // The lens: a wedge opening to the right of the body.
            return [[0.3, -38, 0.78, 0.4], [0.3, 38, 0.78, 0.6], [0.34, 90, 0.92, 0.5]]
        case "plus":
            return [[0.7, 0, 0.5, 0.5], [0.7, 90, 0.5, 0.5]]
        case "menu":
            return [[0.8, 0, 0.5, 0.25], [0.8, 0, 0.5, 0.5], [0.8, 0, 0.5, 0.75]]
        case "settings":
            // Three sliders, each with its knob at a different setting.
            return [[0.84, 0, 0.5, 0.22], [0.84, 0, 0.5, 0.5], [0.84, 0, 0.5, 0.78], [0.3, 90, 0.32,
                                                                                      0.22], [0.3,
                                                                                              90, 0.68, 0.5],
                    [0.3, 90, 0.42, 0.78]]
        default:
            return []
        }
    }

    Repeater {
        model: root.bars

        Rectangle {
            required property var modelData

            width: root.width * modelData[0]
            height: root.stroke
            radius: root.stroke / 2
            color: root.color
            antialiasing: true
            x: root.width * modelData[2] - width / 2
            y: root.height * modelData[3] - height / 2
            rotation: modelData[1]
        }
    }

    // A bell: a dome flaring to a rim, with the clapper below.
    Canvas {
        id: bell
        visible: root.name === "bell"
        anchors.fill: parent
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, h = height, s = root.stroke
            ctx.strokeStyle = root.color
            ctx.fillStyle = root.color
            ctx.lineWidth = s
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.beginPath()
            ctx.moveTo(w * 0.14, h * 0.74)
            ctx.quadraticCurveTo(w * 0.26, h * 0.62, w * 0.26, h * 0.44)
            ctx.arc(w * 0.5, h * 0.44, w * 0.24, Math.PI, 0)
            ctx.quadraticCurveTo(w * 0.74, h * 0.62, w * 0.86, h * 0.74)
            ctx.closePath()
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(w * 0.5, h * 0.86, s * 0.9, 0, 2 * Math.PI)
            ctx.fill()
        }

        Connections {
            target: root
            function onColorChanged() {
                bell.requestPaint()
            }
        }
    }

    // Most of a circle with an arrowhead at its open end, for refresh.
    Canvas {
        id: arc
        visible: root.name === "refresh"
        anchors.fill: parent
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            // On a 24-unit grid inset by half the stroke, so nothing pokes out.
            const unit = (width - root.stroke) / 24
            const at = v => root.stroke / 2 + v * unit
            ctx.strokeStyle = root.color
            ctx.lineWidth = root.stroke
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            // Round from the right, open at the upper right.
            ctx.beginPath()
            ctx.arc(at(12), at(12), 9 * unit, 0, Math.PI * 1.78)
            ctx.stroke()
            // A corner for the head, inside the circle's box.
            ctx.beginPath()
            ctx.moveTo(at(21), at(3))
            ctx.lineTo(at(21), at(9))
            ctx.lineTo(at(15), at(9))
            ctx.stroke()
        }

        Connections {
            target: root
            function onColorChanged() {
                arc.requestPaint()
            }
            function onStrokeChanged() {
                arc.requestPaint()
            }
        }
    }

    // More: three dots in a row.
    Repeater {
        model: root.name === "more" ? [0.2, 0.5, 0.8] : []

        Rectangle {
            required property real modelData

            width: root.stroke * 1.5
            height: width
            radius: width / 2
            color: root.color
            antialiasing: true
            x: root.width * modelData - width / 2
            y: root.height / 2 - height / 2
        }
    }

    // Search: a lens with a handle.
    Rectangle {
        visible: root.name === "search"
        x: root.width * 0.08
        y: root.height * 0.08
        width: root.width * 0.6
        height: width
        radius: width / 2
        color: "transparent"
        border.width: root.stroke
        border.color: root.color
        antialiasing: true
    }
    Rectangle {
        visible: root.name === "search"
        width: root.width * 0.34
        height: root.stroke
        radius: root.stroke / 2
        color: root.color
        antialiasing: true
        rotation: 45
        x: root.width * 0.76 - width / 2
        y: root.height * 0.76 - height / 2
    }

    // The video camera's body.
    Rectangle {
        visible: root.name === "video"
        x: 0
        y: root.height * 0.24
        width: root.width * 0.62
        height: root.height * 0.52
        radius: root.stroke * 1.2
        color: "transparent"
        border.width: root.stroke * 0.85
        border.color: root.color
        antialiasing: true
    }

    // Maximize is a rounded square; restore adds a second one behind it.
    Rectangle {
        visible: root.name === "maximize" || root.name === "restore"
        anchors.centerIn: parent
        width: root.width * (root.name === "restore" ? 0.48 : 0.6)
        height: width
        radius: root.stroke
        color: "transparent"
        border.width: root.stroke * 0.85
        border.color: root.color
        antialiasing: true
    }
    Rectangle {
        visible: root.name === "restore"
        x: root.width * 0.38
        y: root.height * 0.14
        width: root.width * 0.48
        height: width
        radius: root.stroke
        color: "transparent"
        border.width: root.stroke * 0.85
        border.color: root.color
        opacity: 0.6
        antialiasing: true
    }
}
