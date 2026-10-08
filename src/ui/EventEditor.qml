pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// The event card turned into a form: title, day and times, all day, repeat,
/// place, video call, guests and notes, with the time zone under More options.
/// Only what changes is saved; a repeating event asks which occurrences.
Column {
    id: root

    /// The event as EventModel::eventAt gives it.
    property var event: ({})
    required property EventActions actions

    /// Leaves the form, after saving or not.
    signal finished(bool saved)

    // The form's state, loaded from the event by load().
    property string zone
    property string title
    property string place
    property string notes
    property bool allDay
    property date startDay
    property int startMinutes
    property date endDay
    property int endMinutes
    property string repeat: "none"
    property var guests: []
    property bool videoCall
    property bool moreOptions: false
    /// Waiting to ask which occurrences a repeating event's change is for.
    property bool asking: false
    readonly property bool repeating: (event.seriesId || "") !== ""

    function load(e) {
        event = e
        zone = e.zone || Settings.timeZoneId
        title = e.summary || ""
        place = e.location || ""
        notes = e.description || ""
        allDay = e.allDay === true
        startDay = root.actions.dayOf(e.start, zone)
        startMinutes = root.actions.minutesOf(e.start, zone)
        // An all-day event ends at the start of the day after its last.
        const last = allDay ? new Date(e.end.getTime() - 1) : e.end
        endDay = root.actions.dayOf(last, zone)
        endMinutes = root.actions.minutesOf(e.end, zone)
        repeat = root.actions.repeatChoice(e.recurrence || [], startDay)
        guests = (e.attendees || []).slice()
        videoCall = (e.conferenceUrl || "").toString() !== ""
        moreOptions = false
        asking = false
        root.actions.clearError()
    }

    /// Only the fields that differ from the event, for the occurrences `scope`
    /// names: "this" or "all".
    function changes(scope) {
        const c = {}
        if (title.trim() !== (event.summary || ""))
            c.summary = title
        if (place.trim() !== (event.location || ""))
            c.location = place
        if (notes !== (event.description || ""))
            c.description = notes
        const start = allDay ? root.actions.at(startDay, 0, zone) : root.actions.at(startDay,
                                                                                    startMinutes,
                                                                                    zone)
        const end = allDay ? root.actions.at(endDay, 24 * 60, zone) : root.actions.at(endDay,
                                                                                      endMinutes,
                                                                                      zone)
        if (allDay !== (event.allDay === true) || start.getTime() !== event.start.getTime() || end.getTime(
                    ) !== event.end.getTime() || zone !== (event.zone || Settings.timeZoneId)) {
            c.start = start
            c.end = end
            c.allDay = allDay
            c.zone = zone
        }
        const day = root.actions.dayOf(event.start, zone)
        const was = root.actions.repeatChoice(event.recurrence || [], day)
        // A rule names its day, so moving the series to another day rewrites it.
        const moved = scope !== "this" && root.actions.daysBetween(day, startDay) !== 0
        const rule = root.actions.repeatRule(repeat, startDay)
        const same = JSON.stringify(rule) === JSON.stringify(event.recurrence || [])
        if (repeat !== "custom" && (repeat !== was || moved) && !same)
            c.recurrence = rule
        if (JSON.stringify(guests) !== JSON.stringify(event.attendees || []))
            c.guests = guests
        if (videoCall !== ((event.conferenceUrl || "").toString() !== ""))
            c.videoCall = videoCall
        return c
    }

    /// A new start time, with the end keeping its distance on the clock, into
    /// the next day if it has to.
    function moveStart(minutes) {
        const day = 24 * 60
        const length = Math.max(Theme.snapMinutes, root.actions.daysBetween(startDay, endDay) * day
                                + endMinutes - startMinutes)
        const end = minutes + length
        startMinutes = minutes
        endDay = root.actions.addDays(startDay, Math.floor(end / day))
        endMinutes = end % day
    }

    /// The end comes after the start.
    function valid() {
        const days = root.actions.daysBetween(startDay, endDay)
        return days > 0 || (days === 0 && (allDay || endMinutes > startMinutes))
    }

    function save() {
        if (title.trim() === "" || !valid())
            return
        // A repeating event saves through saveFor(), once asked which occurrences.
        if (repeating) {
            asking = true
            return
        }
        actions.update(event, changes("this"), "this")
    }

    function saveFor(scope) {
        asking = false
        actions.update(event, changes(scope), scope)
    }

    function addGuest(text) {
        const address = text.trim()
        if (address === "" || guests.indexOf(address) >= 0)
            return
        guests = guests.concat([address])
    }

    Connections {
        target: root.actions
        function onUpdated(eventId) {
            if (eventId === root.event.eventId)
                root.finished(true)
        }
    }

    spacing: Theme.space3

    Field {
        width: parent.width
        text: root.title
        placeholderText: qsTr("Title")
        font.pixelSize: Theme.textLg
        font.weight: Font.Bold
        Accessible.name: qsTr("Title")
        onTextEdited: root.title = text
        onAccepted: root.save()
    }

    // When: the day, then the times or the last day.
    Flow {
        width: parent.width
        spacing: Theme.space2

        DatePicker {
            day: root.startDay
            onPicked: day => {
                // The end keeps its distance from the start.
                const shift = root.actions.daysBetween(root.startDay, day)
                root.startDay = day
                root.endDay = root.actions.addDays(root.endDay, shift)
            }
        }
        TimePicker {
            visible: !root.allDay
            minutes: root.startMinutes
            onPicked: minutes => root.moveStart(minutes)
        }
        Text {
            height: Theme.listRowHeight + Theme.space2
            verticalAlignment: Text.AlignVCenter
            text: qsTr("to")
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
        }
        TimePicker {
            visible: !root.allDay
            minutes: root.endMinutes
            from: root.startDay.getTime() === root.endDay.getTime() ? root.startMinutes + Theme.snapMinutes :
                                                                      0
            showLength: root.startDay.getTime() === root.endDay.getTime()
            onPicked: minutes => root.endMinutes = minutes
        }
        DatePicker {
            visible: root.allDay || root.endDay.getTime() !== root.startDay.getTime()
            day: root.endDay
            onPicked: day => root.endDay = day < root.startDay ? root.startDay : day
        }
    }

    Toggle {
        width: parent.width
        text: qsTr("All day")
        checked: root.allDay
        onToggled: {
            root.allDay = checked
            // An all-day event's times are both midnight; give it an hour.
            if (!checked && !root.valid()) {
                root.startMinutes = 9 * 60
                root.endMinutes = 10 * 60
            }
        }
    }

    // Repeat, from the choices for the start day.
    Row {
        spacing: Theme.space3

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Repeats")
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.ExtraBold
        }
        StickerButton {
            text: root.repeat === "custom" ? qsTr("Custom") : root.actions.repeatChoices(
                                                 root.startDay).find(c => c.id
                                                                          === root.repeat).label
            onClicked: repeatMenu.open()

            MenuCard {
                id: repeatMenu
                y: parent.height + Theme.space1
                minimumWidth: 240

                Repeater {
                    model: root.actions.repeatChoices(root.startDay)

                    MenuEntry {
                        required property var modelData
                        text: modelData.label
                        onTriggered: root.repeat = modelData.id
                    }
                }
            }
        }
    }

    Field {
        width: parent.width
        text: root.place
        placeholderText: qsTr("Where")
        Accessible.name: qsTr("Where")
        onTextEdited: root.place = text
    }

    Toggle {
        width: parent.width
        text: qsTr("Video call")
        checked: root.videoCall
        onToggled: root.videoCall = checked
    }

    // Guests as chips, each with a way to take them off, then a box to add more.
    Flow {
        visible: root.guests.length > 0
        width: parent.width
        spacing: Theme.space2

        Repeater {
            model: root.guests

            Rectangle {
                id: chip
                required property string modelData
                width: chipRow.implicitWidth + 2 * Theme.space3
                height: Theme.listRowHeight
                radius: height / 2
                color: Theme.surfaceAlt

                Row {
                    id: chipRow
                    anchors.centerIn: parent
                    spacing: Theme.space2

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: chip.modelData
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                        font.weight: Font.DemiBold
                    }
                    AbstractButton {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.smallGlyphSize + Theme.space1
                        height: width
                        Accessible.name: qsTr("Remove %1").arg(chip.modelData)
                        onClicked: root.guests = root.guests.filter(g => g !== chip.modelData)

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }
                        contentItem: Glyph {
                            name: "close"
                            stroke: Theme.smallGlyphStroke
                            color: Theme.textMuted
                        }
                    }
                }
            }
        }
    }
    Field {
        id: guestField

        /// People matching what is typed, as Contacts.suggest gives them.
        property var suggestions: []
        property int highlighted: 0
        /// A suggestion was picked with the arrows, so Enter takes it over a typed address.
        property bool picking: false

        function take(email) {
            root.addGuest(email)
            text = ""
            suggestions = []
        }
        // A typed address goes in as typed unless a suggestion was picked.
        function accept() {
            const typed = text.trim()
            if (suggestions.length > 0 && (picking || typed.indexOf("@") < 0))
                take(suggestions[highlighted].email)
            else if (typed !== "")
                take(typed)
        }

        width: parent.width
        placeholderText: qsTr("Add guests by name or email")
        Accessible.name: qsTr("Add a guest")
        onTextEdited: {
            suggestions = Contacts.suggest(text, root.guests)
            highlighted = 0
            picking = false
        }
        onAccepted: accept()
        Keys.onDownPressed: {
            highlighted = Math.min(suggestions.length - 1, highlighted + 1)
            picking = true
        }
        Keys.onUpPressed: {
            highlighted = Math.max(0, highlighted - 1)
            picking = true
        }
        Keys.onTabPressed: event => {
            event.accepted = suggestions.length > 0
            if (event.accepted)
                take(suggestions[highlighted].email)
        }
        Keys.onEscapePressed: event => {
            event.accepted = suggestions.length > 0
            suggestions = []
        }
    }

    // The suggestions, under the box, best first.
    Column {
        visible: guestField.suggestions.length > 0
        width: parent.width

        Repeater {
            model: guestField.suggestions

            Rectangle {
                id: suggestion

                required property var modelData
                required property int index

                width: parent.width
                height: Theme.listRowHeight + Theme.space2
                radius: Theme.radiusMd
                color: index === guestField.highlighted || pointer.hovered ? Theme.surfaceAlt :
                                                                             "transparent"
                Accessible.role: Accessible.Button
                Accessible.name: modelData.name ? qsTr("%1, %2").arg(modelData.name).arg(
                                                      modelData.email) : modelData.email

                HoverHandler {
                    id: pointer
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: guestField.take(suggestion.modelData.email)
                }

                Row {
                    anchors {
                        left: parent.left
                        right: parent.right
                        leftMargin: Theme.space3
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: Theme.space2

                    Text {
                        visible: text !== ""
                        text: suggestion.modelData.name
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                        font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width - x
                        elide: Text.ElideRight
                        text: suggestion.modelData.email
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                    }
                }
            }
        }
    }

    TextArea {
        width: parent.width
        text: root.notes
        placeholderText: qsTr("Notes")
        placeholderTextColor: Theme.textFaint
        wrapMode: TextArea.Wrap
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textMd
        leftPadding: Theme.space4
        rightPadding: Theme.space4
        topPadding: Theme.space3
        bottomPadding: Theme.space3
        Accessible.name: qsTr("Notes")
        onTextChanged: root.notes = text

        background: Rectangle {
            radius: Theme.radiusLg
            color: Theme.surfaceAlt
            border.width: parent.activeFocus ? 2 : 1
            border.color: parent.activeFocus ? Theme.accent : Theme.border
        }
    }

    Text {
        text: root.moreOptions ? qsTr("Fewer options") : qsTr("More options")
        color: Theme.accent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
        font.weight: Font.ExtraBold

        TapHandler {
            onTapped: root.moreOptions = !root.moreOptions
        }
        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
    }
    Row {
        visible: root.moreOptions
        spacing: Theme.space3

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Time zone")
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.ExtraBold
        }
        ZonePicker {
            zoneId: root.zone
            onPicked: id => root.zone = id
        }
    }

    Text {
        visible: root.actions.error !== "" && root.actions.errorEventId === root.event.eventId
        width: parent.width
        wrapMode: Text.Wrap
        text: root.actions.error
        color: Theme.danger
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }

    // Which occurrences of a repeating event the change is for.
    Column {
        visible: root.asking
        width: parent.width
        spacing: Theme.space2

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Change this event, the ones after it too, or every one in the series?")
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.Bold
        }
        Flow {
            width: parent.width
            spacing: Theme.space2

            StickerButton {
                // A rule belongs to the series, not one occurrence.
                visible: root.asking && root.changes("this").recurrence === undefined
                text: qsTr("This event")
                onClicked: root.saveFor("this")
            }
            StickerButton {
                text: qsTr("This and following")
                onClicked: root.saveFor("following")
            }
            StickerButton {
                text: qsTr("All events")
                onClicked: root.saveFor("all")
            }
            StickerButton {
                text: qsTr("Cancel")
                onClicked: root.asking = false
            }
        }
    }

    Row {
        visible: !root.asking
        spacing: Theme.space2

        StickerButton {
            accent: true
            text: qsTr("Save")
            enabled: root.title.trim() !== "" && root.valid() && !root.actions.busy
            onClicked: root.save()
        }
        StickerButton {
            text: qsTr("Cancel")
            onClicked: root.finished(false)
        }
    }
}
