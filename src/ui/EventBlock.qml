pragma ComponentBehavior: Bound
import Callie.Ui

import QtQuick

/// One timed event in the week/day grid, drawn as a sticker in its calendar's
/// colors. Click for details. An event the user may change can be dragged to
/// another time or day, or stretched from its bottom edge; both snap to
/// Theme.snapMinutes, show where the event will land, and report the change
/// rather than making it.
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
    /// How many earlier events this one steps in over; deeper ones draw on top,
    /// outlined so they stand apart from what they cover.
    required property int depth
    /// The user's answer; "tentative" (maybe) shows striped.
    required property string response
    required property bool canEdit

    /// Days the drag has carried the event, and the x distance to get there;
    /// the view sets both from draggedTo, since only it knows its columns.
    property int dayShift: 0
    property real dayShiftX: 0
    /// Minutes the drag or stretch moves the start, and adds to the length.
    readonly property int minuteShift: drag.active || settling ? drag.minutes : 0
    readonly property int lengthChange: stretch.active || settling ? stretch.minutes : 0
    /// The change was dropped and waits to be saved; the event stays where it
    /// landed until the view reloads it or calls settle().
    property bool settling: false
    /// How much taller the block is drawn while being stretched.
    readonly property real stretchHeight: lengthChange / 60 * Theme.hourHeight

    signal moveRequested(int deltaMinutes, int deltaDays)
    signal resizeRequested(int deltaMinutes)
    signal draggedTo(point scenePosition)
    signal activated

    /// Puts the block back where its event is, after a cancelled or failed change.
    function settle() {
        settling = false
        dayShift = 0
        dayShiftX = 0
        drag.minutes = 0
        stretch.minutes = 0
    }

    function snapped(pixels) {
        return Math.round(pixels / Theme.hourHeight * 60 / Theme.snapMinutes) * Theme.snapMinutes
    }

    readonly property bool compact: height < 34
    readonly property bool hovered: hover.hovered
    readonly property color ink: shade(Theme.calendarInk(calendarColor, Theme.calendar))
    readonly property bool past: Settings.dimPast && end < Clock.now
    readonly property bool faded: declined || past

    radius: Theme.radiusMd
    color: shade(Theme.calendarColor(calendarColor, Theme.calendar))

    opacity: drag.active ? 0.85 : 1

    // Faded by mixing toward the background rather than by opacity, so a
    // faded event still hides what it covers.
    function shade(color) {
        return faded ? Qt.tint(color, Theme.tint(Theme.bg, 1 - Theme.fadedOpacity)) : color
    }
    scale: drag.active ? 1.02 : 1
    z: drag.active ? 100 : 1 + depth
    border.width: depth > 0 ? 1 : 0
    border.color: Theme.bg
    transform: Translate {
        x: root.dayShiftX
        y: root.minuteShift / 60 * Theme.hourHeight
    }

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
        color: root.shade(Theme.calendarEdge(root.calendarColor, Theme.calendar))
    }

    Stripes {
        anchors.fill: parent
        radius: root.radius
        calendarColor: root.calendarColor
        response: root.response
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
                const from = Settings.times.shifted(root.start, root.dayShift, root.minuteShift)
                const to = Settings.times.shifted(root.end, root.dayShift, root.minuteShift
                                                  + root.lengthChange)
                const span = qsTr("%1 to %2").arg(Settings.times.time(from)).arg(Settings.times.time(
                                                                                     to))
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
        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: root.activated()
    }

    DragHandler {
        id: drag

        property int minutes: 0

        target: null
        enabled: root.canEdit && !root.settling
        onCentroidChanged: {
            if (!active)
                return
            minutes = root.snapped(centroid.scenePosition.y - centroid.scenePressPosition.y)
            root.draggedTo(centroid.scenePosition)
        }
        onActiveChanged: {
            if (active)
                return
            if (minutes !== 0 || root.dayShift !== 0) {
                root.settling = true
                root.moveRequested(minutes, root.dayShift)
            } else {
                root.settle()
            }
        }
    }

    // The bottom edge, to stretch or shorten the event.
    Item {
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: 6
        visible: root.canEdit

        HoverHandler {
            cursorShape: Qt.SizeVerCursor
        }
        DragHandler {
            id: stretch

            property int minutes: 0

            target: null
            // The edge is inside the block, whose own drag would move it instead.
            grabPermissions: PointerHandler.CanTakeOverFromAnything
            enabled: !root.settling
            onCentroidChanged: {
                if (!active)
                    return
                const length = (root.end.getTime() - root.start.getTime()) / 60000
                // Never shorter than one snap.
                minutes = Math.max(Theme.snapMinutes - length, root.snapped(centroid.scenePosition.y
                                                                            - centroid.scenePressPosition.y))
            }
            onActiveChanged: {
                if (active)
                    return
                if (minutes !== 0) {
                    root.settling = true
                    root.resizeRequested(minutes)
                } else {
                    root.settle()
                }
            }
        }
    }
}
