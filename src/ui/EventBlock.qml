pragma ComponentBehavior: Bound
import Callie.Ui

import QtQuick

/// One timed event in the week/day grid, drawn as a sticker in its calendar's
/// colors. Drag to move; the block snaps to Theme.snapMinutes and reports the
/// delta rather than mutating state itself.
Rectangle {
    id: root

    required property string summary
    required property string location
    required property url conferenceUrl
    /// The calendar's own color; the theme turns it into fill, ink and edge.
    required property color calendarColor
    required property date start
    required property date end

    signal moveRequested(int deltaMinutes, int deltaDays)
    signal activated

    readonly property bool compact: height < 34
    readonly property color ink: Theme.calendarInk(calendarColor, Theme.calendar)

    radius: Theme.radiusMd
    color: Theme.calendarColor(calendarColor, Theme.calendar)

    opacity: drag.active ? 0.85 : 1
    scale: drag.active ? 1.02 : 1
    z: drag.active ? 100 : 1

    Behavior on scale {
        NumberAnimation {
            duration: Theme.durFast
            easing.type: Theme.easingBounce
            easing.overshoot: Theme.bounce
        }
    }
    Behavior on opacity {
        NumberAnimation {
            duration: Theme.durFast
        }
    }

    // The sticker's edge, showing below it.
    Rectangle {
        z: -1
        anchors {
            fill: parent
            topMargin: Theme.stickerEdge
            bottomMargin: -Theme.stickerEdge
        }
        radius: root.radius
        color: Theme.calendarEdge(root.calendarColor, Theme.calendar)
    }

    Column {
        anchors {
            fill: parent
            leftMargin: Theme.space3 + 1
            rightMargin: Theme.space3
            topMargin: root.compact ? 2 : Theme.space2
        }
        spacing: 1
        clip: true

        Text {
            width: parent.width
            text: root.summary
            color: root.ink
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.ExtraBold
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            visible: !root.compact
            text: {
                const span = qsTr("%1 to %2").arg(Qt.formatTime(root.start, "h:mm")).arg(Qt.formatTime(
                                                                                             root.end, "h:mm"))
                return root.location ? qsTr("%1, %2").arg(span).arg(root.location) : span
            }
            // Full-strength ink, lighter weight: the theme's contrast check covers it.
            color: root.ink
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textXs
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }

    // Join affordance for events that carry a conference link
    Rectangle {
        visible: root.conferenceUrl.toString() !== "" && !root.compact && hover.hovered
        anchors {
            right: parent.right
            bottom: parent.bottom
            margins: Theme.space2
        }
        width: joinLabel.implicitWidth + Theme.space4
        height: 20
        radius: Theme.radiusSm
        color: root.ink

        Text {
            id: joinLabel
            anchors.centerIn: parent
            text: qsTr("Join")
            color: root.color
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textXs
            font.weight: Font.DemiBold
        }

        TapHandler {
            onTapped: Qt.openUrlExternally(root.conferenceUrl)
        }
    }

    HoverHandler {
        id: hover
    }
    TapHandler {
        onDoubleTapped: root.activated()
    }

    DragHandler {
        id: drag
        target: null
        onActiveChanged: {
            if (active) {
                startY = centroid.position.y
                startX = centroid.position.x
            } else {
                const dyMinutes = Math.round(((centroid.position.y - startY) / Theme.hourHeight
                                              * 60) / Theme.snapMinutes) * Theme.snapMinutes
                const dxDays = Math.round((centroid.position.x - startX) / root.width)
                if (dyMinutes !== 0 || dxDays !== 0)
                    root.moveRequested(dyMinutes, dxDays)
            }
        }
        property real startY: 0
        property real startX: 0
    }
}
