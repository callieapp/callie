pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// The card that opens when an event is clicked: a header in the event's
/// calendar colors, the user's answer when invited, a join button for calls,
/// where, which calendar and the notes, then writing to guests and deleting.
/// Escape or a click outside closes it.
Popup {
    id: root

    /// The event as one of EventModel's row maps.
    property var event: ({})
    /// Where answers and deletions go.
    property CalendarSource source
    /// The user's answer, kept here so the card follows a change at once.
    property string response
    /// An answer ("accepted", "tentative", "declined") or "delete" waiting for
    /// its this-event-or-all choice, which repeating events ask for.
    property string pending
    readonly property bool repeats: (event.seriesId || "") !== ""

    property string summary
    property string when
    property string location
    property url conferenceUrl
    /// From EventModel.callService: empty when the link should not be opened.
    property string callService
    property string calendarName
    property color calendarColor
    property string description

    readonly property color fill: Theme.calendarColor(calendarColor, Theme.calendar)
    readonly property color ink: Theme.calendarInk(calendarColor, Theme.calendar)
    /// Fills the card from one of EventModel.eventsOn()'s maps and opens it.
    function show(event, service) {
        const day = Qt.formatDate(Settings.times.date(event.start), "dddd, MMMM d")
        summary = event.summary
        when = event.allDay ? day : qsTr("%1, %2").arg(day).arg(qsTr("%1 to %2").arg(Settings.times.time(
                                                                                         event.start)).arg(
                                                                    Settings.times.time(event.end)))
        location = event.location
        conferenceUrl = event.conferenceUrl
        callService = service
        calendarName = event.calendarName
        calendarColor = event.calendarColor
        description = event.description
        root.event = event
        response = event.response || ""
        pending = ""
        deleteButton.armed = false
        actions.clearError()
        open()
    }

    /// Answers or deletes now, or first asks which occurrences when it repeats.
    function act(action) {
        if (repeats && pending !== action) {
            pending = action
            return
        }
        perform(action, false)
    }

    function perform(action, wholeSeries) {
        pending = ""
        if (action === "delete")
            actions.remove(event, wholeSeries)
        else
            actions.respond(event, action, wholeSeries)
    }

    EventActions {
        id: actions
        source: root.source
        onResponded: (eventId, status) => {
            if (eventId === root.event.eventId)
                root.response = status
        }
        onRemoved: eventId => {
            if (eventId === root.event.eventId)
                root.close()
        }
    }

    /// show(), then places the card by `item`: to its right if there is room,
    /// else to its left, else below it, always inside the parent view.
    function showNear(event, service, item) {
        show(event, service)
        const area = parent
        const gap = Theme.space3
        const right = item.mapToItem(area, item.width + gap, 0)
        const left = item.mapToItem(area, -gap - width, 0)
        if (right.x + width <= area.width || left.x >= 0) {
            x = right.x + width <= area.width ? right.x : left.x
            y = Math.min(Math.max(0, right.y), area.height - height - gap)
            return
        }
        const below = item.mapToItem(area, 0, item.height + gap)
        x = Math.min(Math.max(0, below.x + Theme.space7), area.width - width - gap)
        y = below.y + height <= area.height ? below.y : Math.max(0, below.y - item.height - height
                                                                 - 2 * gap)
    }

    readonly property bool hasCall: callService !== ""
    readonly property string callName: callService === "zoom" ? qsTr("Join Zoom call") :
                                                                callService === "meet" ? qsTr(
                                                                                             "Join Google Meet") :
                                                                                         qsTr("Join call")

    width: 320
    // Keeps the whole card inside the window, wherever its event sits.
    margins: Theme.space3
    padding: 0
    modal: false
    // Dims the calendar without blocking it, so a click elsewhere still lands.
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation {
            property: "scale"
            from: 0.94
            to: 1
            duration: Theme.durMed
            easing.type: Theme.easingBounce
            easing.overshoot: Theme.bounce
        }
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.durFast
        }
    }

    Overlay.modeless: Rectangle {
        // The attached Window type is not the QML Window type, so this stays untyped.
        readonly property var appWindow: Window.window

        // Follows the window's rounded corners rather than filling them in.
        radius: appWindow && appWindow.cornerRadius ? appWindow.cornerRadius : 0
        color: Theme.tint(Theme.shadowColor, 0.35)

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.durFast
            }
        }
    }

    background: Rectangle {
        radius: Theme.radiusXl
        color: Theme.surface
        border.color: Theme.border

        SoftShadow {
            anchors.fill: parent
            radius: parent.radius
        }
    }

    contentItem: Column {
        // Header: the event's own colors.
        Rectangle {
            width: root.width
            height: header.implicitHeight + 2 * Theme.space4
            topLeftRadius: Theme.radiusXl
            topRightRadius: Theme.radiusXl
            color: root.fill

            Row {
                id: header
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.space5
                    rightMargin: Theme.space4
                }
                spacing: Theme.space3

                Column {
                    width: header.width - closeButton.width - header.spacing
                    spacing: Theme.space1

                    Text {
                        width: parent.width
                        text: root.summary
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        color: root.ink
                        font.family: Theme.displayFontFamily
                        font.pixelSize: Theme.textXl
                        font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width
                        text: root.when
                        wrapMode: Text.Wrap
                        color: root.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textMd
                        font.weight: Font.Bold
                    }
                }

                AbstractButton {
                    id: closeButton
                    width: 30
                    height: 30
                    Accessible.name: qsTr("Close")
                    onClicked: root.close()

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: closeButton.hovered ? Theme.tint(root.ink, 0.12) : "transparent"
                    }
                    contentItem: Item {
                        Glyph {
                            anchors.centerIn: parent
                            width: 12
                            height: 12
                            stroke: 1.8
                            name: "close"
                            color: root.ink
                        }
                    }
                }
            }
        }

        // The user's answer, right under the header, when they were invited.
        Rectangle {
            visible: root.event.canRespond === true
            width: root.width
            height: answers.implicitHeight + 2 * Theme.space3
            color: Theme.surfaceAlt

            Row {
                id: answers
                anchors {
                    left: parent.left
                    leftMargin: Theme.space5
                    verticalCenter: parent.verticalCenter
                }
                spacing: Theme.space2

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    rightPadding: Theme.space2
                    text: qsTr("Going?")
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.ExtraBold
                }
                Repeater {
                    model: [
                        {
                            "status": "accepted",
                            "label": qsTr("Yes")
                        },
                        {
                            "status": "tentative",
                            "label": qsTr("Maybe")
                        },
                        {
                            "status": "declined",
                            "label": qsTr("No")
                        }
                    ]

                    PillButton {
                        id: answer
                        required property var modelData
                        label: answer.modelData.label
                        selected: root.response === answer.modelData.status
                        enabled: !actions.busy
                        onClicked: root.act(answer.modelData.status)
                    }
                }
            }
        }

        Column {
            width: root.width
            padding: Theme.space5
            spacing: Theme.space4

            StickerButton {
                visible: root.hasCall
                width: parent.width - 2 * parent.padding
                height: 40
                accent: true
                glyph: "video"
                glyphStroke: Theme.fineGlyphStroke
                text: root.callName
                onClicked: Qt.openUrlExternally(root.conferenceUrl)
            }

            Detail {
                width: parent.width - 2 * parent.padding
                label: qsTr("Where")
                value: root.location
            }
            Detail {
                width: parent.width - 2 * parent.padding
                label: qsTr("Calendar")
                value: root.calendarName
            }
            Detail {
                width: parent.width - 2 * parent.padding
                label: qsTr("Notes")
                value: root.description
            }

            // Which occurrences a repeating event's answer or deletion is for.
            Column {
                visible: root.pending !== ""
                width: parent.width - 2 * parent.padding
                spacing: Theme.space2

                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: root.pending === "delete" ? qsTr(
                                                          "Delete this event, or every one in the series?") :
                                                      qsTr("Answer for this event, or every one in the series?")
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.Bold
                }
                Flow {
                    width: parent.width
                    spacing: Theme.space2

                    StickerButton {
                        text: qsTr("This event")
                        onClicked: root.perform(root.pending, false)
                    }
                    StickerButton {
                        text: qsTr("All events")
                        onClicked: root.perform(root.pending, true)
                    }
                    StickerButton {
                        text: qsTr("Cancel")
                        onClicked: root.pending = ""
                    }
                }
            }

            Flow {
                visible: (root.event.attendees || []).length > 0 || root.event.canEdit === true
                width: parent.width - 2 * parent.padding
                spacing: Theme.space2

                StickerButton {
                    visible: (root.event.attendees || []).length > 0
                    text: qsTr("Email guests")
                    onClicked: Qt.openUrlExternally(actions.mailGuests(root.event))
                }
                // A one-off event takes a second click to delete.
                StickerButton {
                    id: deleteButton
                    property bool armed: false
                    visible: root.event.canEdit === true
                    enabled: !actions.busy
                    text: armed ? qsTr("Click again to delete") : qsTr("Delete")
                    onClicked: {
                        if (root.repeats) {
                            root.act("delete")
                        } else if (armed) {
                            armed = false
                            root.perform("delete", false)
                        } else {
                            armed = true
                            disarm.restart()
                        }
                    }

                    Timer {
                        id: disarm
                        interval: 4000
                        onTriggered: deleteButton.armed = false
                    }
                }
            }

            Text {
                visible: actions.error !== "" && actions.errorEventId === root.event.eventId
                width: parent.width - 2 * parent.padding
                text: actions.error
                wrapMode: Text.Wrap
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
            }
        }
    }

    component Detail: Row {
        id: detail

        property string label
        property string value

        visible: value !== ""
        spacing: Theme.space4

        Text {
            width: 64
            text: detail.label
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.ExtraBold
        }
        Text {
            width: detail.width - 64 - detail.spacing
            text: detail.value
            wrapMode: Text.Wrap
            maximumLineCount: 8
            elide: Text.ElideRight
            textFormat: Text.PlainText
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
            font.weight: Font.DemiBold
        }
    }
}
