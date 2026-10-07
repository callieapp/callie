import Callie.Ui
import QtQuick

/// An all-day event in the week's strip: a sticker across its days that can be
/// dragged to other days, or stretched and shortened from its right end.
Rectangle {
    id: chip

    required property int index
    required property string summary
    required property bool allDay
    required property int firstDay
    required property int daySpan
    required property int lane
    required property color calendarColor
    required property bool declined
    required property string response
    required property date end
    required property url conferenceUrl
    required property bool canEdit
    /// The week view, for its column edges (columnX) and the shown column under
    /// an x in the strip (columnAtX).
    required property var view

    /// Days a drag moves the event, and adds to its end when stretching.
    property int dayShift: 0
    property int spanChange: 0
    /// Waiting for the change to be saved, drawn where it was dropped.
    property bool settling: false
    property int grabColumn: -1
    readonly property bool pending: response === "needsAction"

    signal activated
    signal moveRequested(int days, int endDays)

    /// Back to where the model has it.
    function settle() {
        settling = false
        dayShift = 0
        spanChange = 0
        grabColumn = -1
    }

    function columnUnder(scenePosition) {
        return view.columnAtX(parent.mapFromItem(null, scenePosition.x, scenePosition.y).x)
    }

    // The days the pointer crossed since the drag began.
    function daysDragged(handler) {
        if (grabColumn < 0)
            grabColumn = columnUnder(handler.centroid.scenePressPosition)
        return columnUnder(handler.centroid.scenePosition) - grabColumn
    }

    visible: allDay && width > 0
    opacity: declined || (Settings.dimPast && end < Clock.now) ? Theme.fadedOpacity : 1
    layer.enabled: opacity < 1
    x: view.columnX(firstDay + dayShift) + 3
    y: Theme.space1 + lane * Theme.allDayRowHeight
    width: view.columnX(firstDay + dayShift + daySpan + spanChange) - x - 3
    height: Theme.allDayRowHeight - 3 - Theme.stickerEdge
    z: drag.active || stretch.active ? 2 : 1
    radius: height / 2
    // Hollow, outlined in the calendar's color, until answered.
    color: pending ? Theme.surface : Theme.calendarColor(calendarColor, Theme.calendar)
    border.width: pending ? Theme.inviteOutline : 0
    border.color: Theme.calendarColor(calendarColor, Theme.calendar)
    Accessible.role: Accessible.Button
    Accessible.name: summary

    Rectangle {
        visible: !chip.pending
        z: -1
        anchors {
            fill: parent
            topMargin: Theme.stickerEdge
            bottomMargin: -Theme.stickerEdge
        }
        radius: parent.radius
        color: Theme.calendarEdge(chip.calendarColor, Theme.calendar)
    }

    Stripes {
        anchors.fill: parent
        radius: chip.radius
        calendarColor: chip.calendarColor
        response: chip.response
    }

    Text {
        anchors {
            fill: parent
            leftMargin: Theme.space3
            rightMargin: Theme.space2
        }
        verticalAlignment: Text.AlignVCenter
        text: chip.summary
        textFormat: Text.PlainText
        elide: Text.ElideRight
        color: chip.pending ? Theme.text : Theme.calendarInk(chip.calendarColor, Theme.calendar)
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
        font.weight: Font.ExtraBold
        font.strikeout: chip.declined
    }

    HoverHandler {
        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: chip.activated()
    }

    DragHandler {
        id: drag

        target: null
        yAxis.enabled: false
        enabled: chip.canEdit && !chip.settling
        onCentroidChanged: {
            if (active)
                chip.dayShift = chip.daysDragged(drag)
        }
        onActiveChanged: {
            if (active)
                return
            if (chip.dayShift !== 0) {
                chip.settling = true
                chip.moveRequested(chip.dayShift, 0)
            } else {
                chip.settle()
            }
        }
    }

    // The right end, to add days or take them off.
    Item {
        anchors {
            top: parent.top
            bottom: parent.bottom
            right: parent.right
        }
        width: Theme.space3
        visible: chip.canEdit

        HoverHandler {
            cursorShape: Qt.SizeHorCursor
        }
        DragHandler {
            id: stretch

            target: null
            yAxis.enabled: false
            // The end is inside the chip, whose own drag would move it instead.
            grabPermissions: PointerHandler.CanTakeOverFromAnything
            enabled: !chip.settling
            onCentroidChanged: {
                // Never shorter than a day.
                if (active)
                    chip.spanChange = Math.max(1 - chip.daySpan, chip.daysDragged(stretch))
            }
            onActiveChanged: {
                if (active)
                    return
                if (chip.spanChange !== 0) {
                    chip.settling = true
                    chip.moveRequested(0, chip.spanChange)
                } else {
                    chip.settle()
                }
            }
        }
    }
}
