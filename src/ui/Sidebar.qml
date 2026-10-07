pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// The left column: a greeting, the next event today, a mini month to jump
/// around in, and the calendars on show.
Rectangle {
    id: root

    required property TodayModel today
    required property MonthModel month
    required property EventModel events

    /// A day was picked in the mini month.
    signal dayPicked(date day)

    // Week numbers take a column of their own when shown.
    readonly property real dayCellWidth: (width - 2 * Theme.space5) / (Settings.weekNumbers ? 8 : 7)

    color: Theme.surface

    Column {
        id: upper
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            leftMargin: Theme.space5
            rightMargin: Theme.space5
            topMargin: Theme.space6
        }
        spacing: Theme.space6

        Column {
            width: parent.width
            spacing: Theme.space1

            Text {
                text: root.today.greeting
                color: Theme.text
                font.family: Theme.displayFontFamily
                font.pixelSize: Theme.textDisplay
                font.weight: Font.Bold
            }
            Text {
                text: root.today.dateLabel
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
            }
        }

        // Up next: a sticker in the event's calendar colors.
        Item {
            id: upNext

            readonly property color fill: Theme.calendarColor(root.today.nextColor, Theme.calendar)
            readonly property color ink: Theme.calendarInk(root.today.nextColor, Theme.calendar)

            visible: root.today.hasNext
            width: parent.width
            height: nextColumn.implicitHeight + 2 * Theme.space4

            Rectangle {
                anchors {
                    fill: parent
                    topMargin: Theme.stickerEdge
                    bottomMargin: -Theme.stickerEdge
                }
                radius: Theme.radiusLg
                color: Theme.calendarEdge(root.today.nextColor, Theme.calendar)
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusLg
                color: upNext.fill
            }

            Column {
                id: nextColumn
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.space4
                    rightMargin: Theme.space4
                }
                spacing: Theme.space2

                Text {
                    width: parent.width
                    text: root.today.nextLabel.toUpperCase()
                    color: upNext.ink
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textXs
                    font.weight: Font.ExtraBold
                    font.letterSpacing: 0.6
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: root.today.nextTitle
                    textFormat: Text.PlainText
                    color: upNext.ink
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textLg
                    font.weight: Font.ExtraBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: root.today.nextDetail
                    textFormat: Text.PlainText
                    color: upNext.ink
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
        }

        // Mini month: the shown week is shaded, today is an accent sticker.
        Column {
            width: parent.width
            spacing: Theme.space3

            Row {
                Item {
                    visible: Settings.weekNumbers
                    width: root.dayCellWidth
                    height: 1
                }
                Repeater {
                    // From the chosen first day, as the week view is.
                    model: 7

                    Text {
                        required property int index
                        width: root.dayCellWidth
                        horizontalAlignment: Text.AlignHCenter
                        text: Qt.locale().dayName((Settings.firstDayOfWeek + index) % 7,
                                                  Locale.NarrowFormat)
                        color: Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textXs
                        font.weight: Font.ExtraBold
                    }
                }
            }

            Row {
                Column {
                    visible: Settings.weekNumbers
                    spacing: Theme.space1

                    Repeater {
                        model: root.month.weekNumbers

                        Text {
                            required property int modelData
                            width: root.dayCellWidth
                            height: Theme.miniDaySize
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: modelData
                            color: Theme.textFaint
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textXs
                            font.weight: Font.Bold
                        }
                    }
                }

                Grid {
                    columns: 7
                    rowSpacing: Theme.space1

                    Repeater {
                        model: root.month

                        AbstractButton {
                            id: dayCell

                            required property date date
                            required property int day
                            required property bool inMonth
                            required property bool inWeek
                            required property bool isToday

                            width: root.dayCellWidth
                            height: Theme.miniDaySize
                            // A sliver between neighbours, so the shown week reads as days.
                            leftInset: Theme.space1
                            rightInset: Theme.space1
                            focusPolicy: Qt.TabFocus
                            Accessible.name: Qt.formatDate(date, Qt.locale().dateFormat(
                                                               Locale.LongFormat))
                            onClicked: root.dayPicked(date)

                            background: Item {
                                Rectangle {
                                    visible: dayCell.isToday
                                    anchors {
                                        fill: parent
                                        topMargin: Theme.stickerEdge
                                        bottomMargin: -Theme.stickerEdge
                                    }
                                    radius: Theme.radiusSm
                                    color: Theme.accentEdge
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.radiusSm
                                    color: dayCell.isToday ? Theme.accent : dayCell.inWeek
                                                             ? Theme.surfaceAlt : dayCell.hovered
                                                               ? Theme.tint(Theme.surfaceAlt, 0.5) :
                                                                 "transparent"
                                    border.width: dayCell.visualFocus ? 2 : 0
                                    border.color: Theme.text
                                }
                            }
                            contentItem: Text {
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                // Neighbouring months' days stay visible and clickable, since
                                // today and the shown week can fall among them.
                                text: dayCell.day
                                // Other months' days recede, even inside the shown week.
                                opacity: dayCell.inMonth || dayCell.isToday ? 1 : Theme.fadedOpacity
                                color: dayCell.isToday ? Theme.accentText : dayCell.inWeek
                                                         ? Theme.text : dayCell.inMonth
                                                           ? Theme.textMuted : Theme.textFaint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSm
                                font.weight: dayCell.isToday ? Font.ExtraBold : dayCell.inWeek
                                                               ? Font.Bold : Font.Normal
                                font.features: {
                                    "tnum": 1
                                }
                            }

                            HoverHandler {
                                cursorShape: Qt.PointingHandCursor
                            }
                        }
                    }
                }
            }
        }
    }

    // Calendars: deliberately quiet, so the event stickers draw the eye. They
    // fill the rest of the column and scroll, grouped by account.
    Item {
        id: calendars

        // Accounts in the order their first calendar arrives.
        readonly property var accounts: {
            const seen = []
            for (const calendar of root.events.calendars) {
                if (seen.indexOf(calendar.account) < 0)
                    seen.push(calendar.account)
            }
            return seen
        }

        anchors {
            top: upper.bottom
            topMargin: Theme.space6
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: Theme.space5
            rightMargin: Theme.space5
        }

        Text {
            id: heading
            text: qsTr("CALENDARS")
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textXs
            font.weight: Font.ExtraBold
            font.letterSpacing: 0.6
        }

        Flickable {
            anchors {
                top: heading.bottom
                topMargin: Theme.space3
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            contentHeight: groups.height + Theme.space5
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: groups
                width: parent.width
                spacing: Theme.space3

                Repeater {
                    model: calendars.accounts

                    Column {
                        id: group

                        required property string modelData
                        readonly property bool collapsed: Settings.collapsedAccounts.indexOf(
                                                              modelData) >= 0
                        // One account needs no heading of its own.
                        readonly property bool titled: calendars.accounts.length > 1

                        width: parent.width
                        spacing: Theme.space1

                        AbstractButton {
                            id: groupHeader
                            visible: group.titled
                            width: parent.width
                            height: Theme.listRowHeight
                            focusPolicy: Qt.TabFocus
                            Accessible.name: group.modelData
                            Accessible.role: Accessible.Button
                            onClicked: Settings.setAccountCollapsed(group.modelData,
                                                                    !group.collapsed)

                            HoverHandler {
                                id: headerHover
                                cursorShape: Qt.PointingHandCursor
                            }

                            MoreButton {
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                shown: headerHover.hovered || groupHeader.visualFocus
                                Accessible.name: qsTr("Rename %1").arg(groupHeader.title)
                                onClicked: root.editLook(this, true, group.modelData, "")
                            }

                            readonly property string title: Settings.accountNames[group.modelData]
                                                            || group.modelData

                            background: Rectangle {
                                radius: Theme.radiusSm
                                color: groupHeader.hovered ? Theme.surfaceAlt : "transparent"
                                border.width: groupHeader.visualFocus ? 2 : 0
                                border.color: Theme.text
                            }
                            contentItem: Row {
                                spacing: Theme.space2

                                Glyph {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Theme.smallGlyphSize
                                    height: Theme.smallGlyphSize
                                    stroke: Theme.smallGlyphStroke
                                    name: "chevron-right"
                                    color: Theme.textFaint
                                    rotation: group.collapsed ? 0 : 90

                                    Behavior on rotation {
                                        NumberAnimation {
                                            duration: Theme.durFast
                                        }
                                    }
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: groupHeader.width - Theme.smallGlyphSize - Theme.space2
                                           - Theme.listRowHeight
                                    text: groupHeader.title
                                    textFormat: Text.PlainText
                                    elide: Text.ElideMiddle
                                    color: Theme.textFaint
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textSm
                                    font.weight: Font.Bold
                                }
                            }
                        }

                        Repeater {
                            model: group.collapsed && group.titled ? [] : root.events.calendars.filter(c
                                                                                                       => c.account
                                                                                                          === group.modelData)

                            CalendarRow {}
                        }
                    }
                }
            }
        }
    }

    /// Opens the editor for a calendar's or an account's own look, beside `item`.
    function editLook(item, account, id, name) {
        const at = item.mapToItem(root, item.width + Theme.space3, 0)
        lookEditor.x = at.x
        lookEditor.y = Math.min(at.y, root.height - lookEditor.height - Theme.space3)
        if (account)
            lookEditor.editAccount(id)
        else
            lookEditor.editCalendar(id, name)
    }

    LookEditor {
        id: lookEditor
    }

    /// A small "..." button that shows on hover, for a row's own settings.
    component MoreButton: AbstractButton {
        id: more

        property bool shown

        width: Theme.listRowHeight
        height: Theme.listRowHeight
        opacity: shown || hovered || visualFocus ? 1 : 0
        focusPolicy: Qt.TabFocus

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        background: Rectangle {
            radius: Theme.radiusSm
            color: more.hovered ? Theme.border : "transparent"
        }
        contentItem: Item {
            Glyph {
                anchors.centerIn: parent
                width: Theme.smallGlyphSize + 2
                height: Theme.smallGlyphSize + 2
                stroke: Theme.smallGlyphStroke
                name: "more"
                color: Theme.textMuted
            }
        }
    }

    /// One calendar: its color as a dot that fills when shown, and a click to
    /// show or hide it.
    component CalendarRow: AbstractButton {
        id: row

        required property var modelData
        readonly property bool shown: Settings.hiddenCalendars.indexOf(modelData.id) < 0

        width: parent ? parent.width : 0
        height: Theme.listRowHeight
        focusPolicy: Qt.TabFocus
        Accessible.role: Accessible.CheckBox
        Accessible.checked: shown
        Accessible.name: modelData.name
        onClicked: Settings.setCalendarVisible(modelData.id, !shown)

        HoverHandler {
            id: rowHover
            cursorShape: Qt.PointingHandCursor
        }

        MoreButton {
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            shown: rowHover.hovered || row.visualFocus
            Accessible.name: qsTr("Rename or recolor %1").arg(row.modelData.name)
            onClicked: root.editLook(this, false, row.modelData.id, row.modelData.name)
        }

        background: Rectangle {
            radius: Theme.radiusSm
            color: row.hovered ? Theme.surfaceAlt : "transparent"
            border.width: row.visualFocus ? 2 : 0
            border.color: Theme.text
        }
        contentItem: Row {
            leftPadding: Theme.space1
            spacing: Theme.space3

            Rectangle {
                readonly property color tone: Theme.calendarColor(row.modelData.color,
                                                                  Theme.calendar)

                anchors.verticalCenter: parent.verticalCenter
                width: Theme.textBase
                height: Theme.textBase
                radius: Theme.radiusSm
                color: row.shown ? tone : "transparent"
                border.width: 2
                border.color: tone

                Glyph {
                    anchors.centerIn: parent
                    visible: row.shown
                    width: Theme.smallGlyphSize - 1
                    height: Theme.smallGlyphSize - 1
                    stroke: Theme.smallGlyphStroke
                    name: "check"
                    color: Theme.calendarInk(row.modelData.color, Theme.calendar)
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: row.width - Theme.textBase - Theme.space3 - Theme.space1
                       - Theme.listRowHeight

                text: row.modelData.name
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: row.shown ? Theme.textMuted : Theme.textFaint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textBase
                font.weight: Font.DemiBold
            }
        }
    }
}
