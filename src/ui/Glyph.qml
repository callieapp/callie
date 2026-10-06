pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick

/// A small line icon drawn from rounded bars, so icons need no image files and
/// take the text color: chevron-left, chevron-right, minimize, maximize,
/// restore, close or check.
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
