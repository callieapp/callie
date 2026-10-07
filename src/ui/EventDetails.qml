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
    /// The card has turned into a form to change the event.
    property bool editing: false

    function show(event, service) {
        editing = false
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
        onDuplicated: root.close()
    }

    /// Copies the event to the same time, in a calendar that can take it.
    function duplicate() {
        actions.duplicate(event, new Date(NaN), Settings.defaultCalendar
                          || Settings.newEventCalendar)
    }

    /// The event was copied, to be pasted elsewhere.
    signal copied(var event)

    // In the card, since an open card keeps the window's shortcuts from working.
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: root.opened && !root.editing
        onActivated: root.copied(root.event)
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: root.opened && !root.editing && !actions.busy
        onActivated: root.duplicate()
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

    // Wider while it is a form, which has more to hold.
    width: editing ? 400 : 320
    // Keeps the whole card inside the window, wherever its event sits.
    margins: Theme.space3
    padding: 0
    modal: false
    // Dims the calendar without blocking it, so a click elsewhere still lands.
    dim: true
    focus: true
    // A click elsewhere would lose what is being typed, so the form needs Cancel or Escape.
    closePolicy: editing ? Popup.CloseOnEscape : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onClosed: editing = false

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
        // The form, in place of everything else while editing.
        Column {
            visible: root.editing
            width: root.width
            padding: Theme.space5

            EventEditor {
                id: editor
                width: parent.width - 2 * parent.padding
                actions: actions
                onFinished: saved => {
                    root.editing = false
                    if (saved)
                        root.close()
                }
            }
        }

        // Header: the event's own colors.
        Rectangle {
            visible: !root.editing
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
            visible: root.event.canRespond === true && !root.editing
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
            visible: !root.editing
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
            Guests {
                width: parent.width - 2 * parent.padding
                guests: root.event.guests || []
                ownResponse: root.response
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
                width: parent.width - 2 * parent.padding
                spacing: Theme.space2

                StickerButton {
                    visible: root.event.canEdit === true
                    text: qsTr("Edit")
                    onClicked: {
                        editor.load(root.event)
                        root.editing = true
                    }
                }
                StickerButton {
                    text: qsTr("Duplicate")
                    enabled: !actions.busy
                    onClicked: root.duplicate()
                }
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

    /// Who is invited and how each answered, organizer first. Long lists show
    /// the first few until asked for the rest.
    component Guests: Row {
        id: list

        property var guests: []
        /// The user's own answer as just given, which the list may not have yet.
        property string ownResponse
        property bool expanded: false
        readonly property int shownCount: expanded ? guests.length : Math.min(guests.length, 6)

        function answerOf(guest) {
            return guest.self && ownResponse !== "" ? ownResponse : guest.response
        }
        function answerLabel(answer) {
            switch (answer) {
            case "accepted":
                return qsTr("going")
            case "tentative":
                return qsTr("maybe")
            case "declined":
                return qsTr("not going")
            default:
                return qsTr("no answer yet")
            }
        }
        function count(response) {
            return guests.filter(g => answerOf(g) === response).length
        }

        visible: guests.length > 0
        spacing: Theme.space4
        onGuestsChanged: expanded = false

        Text {
            width: 64
            text: qsTr("Guests")
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.ExtraBold
        }
        Column {
            width: list.width - 64 - list.spacing
            spacing: Theme.space2

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: {
                    const parts = [list.guests.length === 1 ? qsTr("1 guest") : qsTr("%1 guests").arg(
                                                                  list.guests.length)]
                    const yes = list.count("accepted"), maybe = list.count("tentative")
                    const no = list.count("declined"), waiting = list.count("needsAction")
                    if (yes)
                        parts.push(qsTr("%1 yes").arg(yes))
                    if (maybe)
                        parts.push(qsTr("%1 maybe").arg(maybe))
                    if (no)
                        parts.push(qsTr("%1 no").arg(no))
                    if (waiting)
                        parts.push(qsTr("%1 waiting").arg(waiting))
                    return parts.join(", ")
                }
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
                font.weight: Font.Bold
            }

            Repeater {
                model: list.guests.slice(0, list.shownCount)

                Row {
                    id: guestRow

                    required property var modelData
                    readonly property string answer: list.answerOf(modelData)

                    width: parent.width
                    spacing: Theme.space2

                    AnswerMark {
                        anchors.verticalCenter: parent.verticalCenter
                        answer: guestRow.answer
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.textBase - parent.spacing
                        text: {
                            let name = guestRow.modelData.name
                            if (guestRow.modelData.self)
                                name = qsTr("%1 (you)").arg(name)
                            return guestRow.modelData.organizer ? qsTr("%1, organizer").arg(name) :
                                                                  name
                        }
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: guestRow.answer === "declined" ? Theme.textFaint : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textMd
                        font.weight: Font.DemiBold
                        font.strikeout: guestRow.answer === "declined"
                        Accessible.name: qsTr("%1: %2").arg(text).arg(list.answerLabel(
                                                                          guestRow.answer))
                    }
                }
            }

            Text {
                visible: list.guests.length > list.shownCount
                text: qsTr("Show all %1").arg(list.guests.length)
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
                font.weight: Font.ExtraBold

                TapHandler {
                    onTapped: list.expanded = true
                }
                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }

    /// A guest's answer as a small sign: a tick for yes, a cross for no, a
    /// question mark for maybe, and an empty ring while they have not answered.
    component AnswerMark: Item {
        id: mark

        property string answer

        width: Theme.textBase
        height: Theme.textBase

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: mark.answer === "accepted" ? Theme.accent : "transparent"
            border.width: mark.answer === "accepted" ? 0 : Theme.markRing
            border.color: mark.answer === "declined" ? Theme.textFaint : Theme.textMuted
        }
        Glyph {
            anchors.centerIn: parent
            visible: mark.answer === "accepted" || mark.answer === "declined"
            width: parent.width - 6
            height: width
            stroke: Theme.smallGlyphStroke
            name: mark.answer === "accepted" ? "check" : "close"
            color: mark.answer === "accepted" ? Theme.accentText : Theme.textFaint
        }
        Text {
            anchors.centerIn: parent
            visible: mark.answer === "tentative"
            text: "?"
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textXs
            font.weight: Font.ExtraBold
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
