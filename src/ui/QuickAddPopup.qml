pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Make an event by describing it in one line. The card below the field
/// shows what Callie understood, live, in the chosen calendar's colors.
Popup {
    id: root

    required property CalendarSource source

    width: 420
    margins: Theme.space3
    padding: Theme.space5
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    /// A time drawn on the grid, handed to the composer when the popup opens.
    property var pickedStart: null
    property var pickedEnd: null
    /// Opened for a drawn time, so only a title is needed.
    property bool fromGrid: false

    /// Opens beside `item` with the drawn time filled in.
    function openFor(start, end, item) {
        pickedStart = start
        pickedEnd = end
        // Beside the drawn block, on whichever side the window has room for.
        const window = Overlay.overlay
        const right = item.mapToItem(window, item.width + Theme.space3, 0)
        const left = item.mapToItem(window, -Theme.space3 - width, 0)
        const spot = right.x + width <= window.width ? right : left
        const local = window.mapToItem(parent, Math.max(0, spot.x), spot.y)
        x = local.x
        y = local.y
        open()
    }

    onOpened: {
        composer.reset()
        fromGrid = pickedStart !== null
        if (fromGrid)
            composer.pickTimes(pickedStart, pickedEnd)
        pickedStart = null
        pickedEnd = null
        field.text = ""
        field.forceActiveFocus()
    }

    Composer {
        id: composer
        source: root.source
        onCreated: root.close()
    }

    readonly property var calendar: {
        for (const c of composer.calendars)
            if (c.id === composer.calendarId)
                return c
        return null
    }
    readonly property color tone: calendar ? calendar.color : Theme.accent

    function whenText() {
        const day = Qt.formatDate(Settings.times.date(composer.start), "dddd, MMMM d")
        if (composer.allDay)
            return qsTr("%1, all day").arg(day)
        return qsTr("%1, %2 to %3").arg(day).arg(Settings.times.time(composer.start)).arg(
                    Settings.times.time(composer.end))
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.durFast
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
        spacing: Theme.space4

        TextField {
            id: field
            width: parent.width
            placeholderText: root.fromGrid ? qsTr(
                                                 "What is it? Add \"at\" and a place, if you like") :
                                             qsTr("Lunch with Alex tomorrow 12-1pm at Cafe Sol")
            placeholderTextColor: Theme.textFaint
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textBase
            font.weight: Font.DemiBold
            leftPadding: Theme.space4
            rightPadding: Theme.space4
            topPadding: Theme.space3
            bottomPadding: Theme.space3
            Accessible.name: qsTr("Describe the event")
            onTextEdited: composer.text = text
            Keys.onReturnPressed: composer.submit()
            Keys.onEnterPressed: composer.submit()
            background: Rectangle {
                radius: Theme.radiusLg
                color: Theme.surfaceAlt
                border.width: field.activeFocus ? 2 : 1
                border.color: field.activeFocus ? Theme.accent : Theme.border
            }
        }

        // What Callie understood, as the sticker it will become.
        Rectangle {
            width: parent.width
            height: preview.implicitHeight + 2 * Theme.space4
            radius: Theme.radiusLg
            color: Theme.calendarColor(root.tone, Theme.calendar)

            Rectangle {
                z: -1
                anchors {
                    fill: parent
                    topMargin: Theme.stickerEdge
                    bottomMargin: -Theme.stickerEdge
                }
                radius: parent.radius
                color: Theme.calendarEdge(root.tone, Theme.calendar)
            }

            Column {
                id: preview
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.space4
                    rightMargin: Theme.space4
                }
                spacing: Theme.space1

                Text {
                    width: parent.width
                    text: composer.summary !== "" ? composer.summary : qsTr("Add a title")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.calendarInk(root.tone, Theme.calendar)
                    opacity: composer.summary !== "" ? 1 : Theme.fadedOpacity
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textBase
                    font.weight: Font.ExtraBold
                }
                Text {
                    width: parent.width
                    text: root.whenText()
                    color: Theme.calendarInk(root.tone, Theme.calendar)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: composer.location !== ""
                    width: parent.width
                    text: composer.location
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.calendarInk(root.tone, Theme.calendar)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.DemiBold
                }
            }
        }

        Flow {
            width: parent.width
            spacing: Theme.space2
            visible: composer.calendars.length > 1

            Repeater {
                model: composer.calendars

                PillButton {
                    required property var modelData
                    label: modelData.name
                    selected: composer.calendarId === modelData.id
                    onClicked: composer.calendarId = modelData.id
                }
            }
        }

        Text {
            visible: composer.calendars.length === 0
            width: parent.width
            text: qsTr("None of your calendars take new events.")
            wrapMode: Text.Wrap
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
        }

        Text {
            visible: composer.error !== ""
            width: parent.width
            text: composer.error
            wrapMode: Text.Wrap
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
        }

        Item {
            width: parent.width
            height: create.height

            Text {
                anchors {
                    left: parent.left
                    right: create.left
                    rightMargin: Theme.space3
                    verticalCenter: parent.verticalCenter
                }
                text: qsTr("Days, times, lengths and a place after \"at\" are understood.")
                wrapMode: Text.Wrap
                color: Theme.textFaint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textXs
            }
            StickerButton {
                id: create
                anchors.right: parent.right
                accent: true
                enabled: composer.ready && !composer.busy
                opacity: enabled ? 1 : Theme.fadedOpacity
                text: composer.busy ? qsTr("Adding...") : qsTr("Add event")
                onClicked: composer.submit()
            }
        }
    }
}
