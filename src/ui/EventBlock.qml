pragma ComponentBehavior: Bound
import Callie.Ui

import QtQuick

/// One timed event in the week/day grid, drawn as a sticker in its calendar's
/// colors. Click for details; drag to move, which snaps to Theme.snapMinutes and
/// reports the delta rather than mutating state itself.
Rectangle {
    id: root

    required property string summary
    required property string location
    required property url conferenceUrl
    required property string calendarName
    required property string description
    /// The calendar's own color; the theme turns it into fill, ink and edge.
    required property color calendarColor
    required property date start
    required property date end
    /// The user said no; shown struck through when declined events are shown.
    required property bool declined

    signal moveRequested(int deltaMinutes, int deltaDays)
    signal activated

    readonly property bool compact: height < 34
    readonly property bool hovered: hover.hovered
    readonly property color ink: Theme.calendarInk(calendarColor, Theme.calendar)
    readonly property bool past: Settings.dimPast && end < Clock.now
    readonly property bool faded: declined || past

    radius: Theme.radiusMd
    color: Theme.calendarColor(calendarColor, Theme.calendar)

    opacity: drag.active ? 0.85 : faded ? Theme.fadedOpacity : 1
    // Fades the sticker and its edge as one, so the edge does not show through.
    layer.enabled: faded
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
            textFormat: Text.PlainText
            color: root.ink
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.ExtraBold
            font.strikeout: root.declined
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            visible: !root.compact
            textFormat: Text.PlainText
            text: {
                const span = qsTr("%1 to %2").arg(Settings.times.time(root.start)).arg(
                          Settings.times.time(root.end))
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

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: root.activated()
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
