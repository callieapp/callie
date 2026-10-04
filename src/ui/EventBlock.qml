pragma ComponentBehavior: Bound
import Callie.Ui

import QtQuick

/// One timed event in the week/day grid. Drag to move; the block snaps to
/// Theme.snapMinutes and reports the delta rather than mutating state itself.
Rectangle {
    id: root

    required property string summary
    required property string location
    required property url conferenceUrl
    required property color accent
    required property date start
    required property date end

    signal moveRequested(int deltaMinutes, int deltaDays)
    signal activated

    readonly property bool compact: height < 34

    radius: Theme.radiusMd
    color: Theme.tint(accent, Theme.dark ? 0.22 : 0.12)
    border.width: 1
    border.color: Theme.tint(accent, Theme.dark ? 0.45 : 0.28)

    opacity: drag.active ? 0.85 : 1
    scale: drag.active ? 1.02 : 1
    z: drag.active ? 100 : 1

    Behavior on scale {
        NumberAnimation {
            duration: Theme.durFast
            easing.type: Theme.easing
        }
    }
    Behavior on opacity {
        NumberAnimation {
            duration: Theme.durFast
        }
    }

    // Accent spine
    Rectangle {
        width: 3
        radius: 1.5
        color: root.accent
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
            margins: 3
        }
    }

    Column {
        anchors {
            fill: parent
            leftMargin: Theme.space4
            rightMargin: Theme.space3
            topMargin: root.compact ? 2 : Theme.space2
        }
        spacing: 1
        clip: true

        Text {
            width: parent.width
            text: root.summary
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            visible: !root.compact
            text: Qt.formatTime(root.start, "h:mm") + " – " + Qt.formatTime(root.end, "h:mm") + (
                      root.location ? "  ·  " + root.location : "")
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textXs
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
        color: root.accent

        Text {
            id: joinLabel
            anchors.centerIn: parent
            text: qsTr("Join")
            color: Theme.accentText
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
