pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A month as whole weeks: each day lists what it holds, as many as fit, and
/// says how many more there are. Click a day to see it on its own.
Item {
    id: root

    required property EventModel model
    /// Monday of the first week shown, and the days shown, whole weeks.
    required property date rangeStart
    required property int dayCount
    /// The month being shown (0 to 11); days outside it are quieter.
    required property int month

    signal dayClicked(date day)

    readonly property int weeks: Math.max(1, Math.ceil(dayCount / 7))
    readonly property real cellWidth: width / 7
    readonly property real cellHeight: (height - weekdays.height) / weeks

    function dateFor(i) {
        return Views.dayAt(rangeStart, i)
    }

    function isToday(d) {
        const now = Settings.times.date(Clock.now)
        return d.getFullYear() === now.getFullYear() && d.getMonth() === now.getMonth() && d.getDate(
                    ) === now.getDate()
    }

    function showEvent(event, item) {
        details.showNear(event, root.model.callService(event.conferenceUrl), item)
    }

    Row {
        id: weekdays
        width: parent.width
        height: 36

        Repeater {
            model: 7

            Text {
                required property int index
                width: root.cellWidth
                height: weekdays.height
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                // Monday first, as the grid is.
                text: Qt.locale().dayName((index + 1) % 7, Locale.ShortFormat).toUpperCase()
                color: Theme.textFaint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textXs
                font.weight: Font.ExtraBold
                font.letterSpacing: 0.8
            }
        }
    }

    Grid {
        anchors.top: weekdays.bottom
        columns: 7

        Repeater {
            model: root.dayCount

            Rectangle {
                id: cell

                required property int index
                readonly property date date: root.dateFor(index)
                readonly property bool inMonth: date.getMonth() === root.month
                readonly property bool today: root.isToday(date)
                readonly property var events: root.model.eventsOn(index, root.model.revision)
                readonly property int fits: Math.max(0, Math.floor((height - dayLabel.height
                                                                    - Theme.space3) / (
                                                                       Theme.monthChipHeight
                                                                       + Theme.space1)))
                readonly property int shown: events.length > fits ? Math.max(0, fits - 1) :
                                                                    events.length

                width: root.cellWidth
                height: root.cellHeight
                color: today ? Theme.todayWash : root.model.isDayOff(date) ? Theme.dayOffWash :
                                                                             "transparent"

                border.width: 0

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.hairline
                }
                Rectangle {
                    width: 1
                    height: parent.height
                    color: Theme.hairline
                }

                TapHandler {
                    onTapped: root.dayClicked(cell.date)
                }

                // The date, as an accent sticker on today.
                Item {
                    id: dayLabel
                    x: Theme.space2
                    y: Theme.space2
                    width: 26
                    height: 26

                    Rectangle {
                        visible: cell.today
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
                        color: cell.today ? Theme.accent : "transparent"
                    }
                    Text {
                        anchors.centerIn: parent
                        text: cell.date.getDate()
                        color: cell.today ? Theme.accentText : cell.inMonth ? Theme.text :
                                                                              Theme.textFaint

                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                        font.weight: cell.today ? Font.ExtraBold : Font.Bold
                        font.features: {
                            "tnum": 1
                        }
                    }
                }

                Column {
                    anchors {
                        top: dayLabel.bottom
                        topMargin: Theme.space1
                        left: parent.left
                        right: parent.right
                        leftMargin: Theme.space2
                        rightMargin: Theme.space2
                    }
                    spacing: Theme.space1

                    Repeater {
                        model: cell.events.slice(0, cell.shown)

                        MonthChip {
                            id: chipItem
                            width: cell.width - 2 * Theme.space2
                            onClicked: root.showEvent(chipItem.event, chipItem)
                        }
                    }

                    Text {
                        visible: cell.events.length > cell.shown
                        text: qsTr("%n more", "", cell.events.length - cell.shown)
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textXs
                        font.weight: Font.Bold
                    }
                }
            }
        }
    }

    EventDetails {
        id: details
    }

    /// One event in a day cell: all-day events as stickers, timed ones as a dot,
    /// the start time and the title.
    component MonthChip: AbstractButton {
        id: chip

        required property var modelData
        readonly property var event: modelData
        readonly property bool faded: event.declined || (Settings.dimPast && event.end < Clock.now)

        height: Theme.monthChipHeight
        opacity: faded ? Theme.fadedOpacity : 1
        Accessible.name: event.summary

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        background: Rectangle {
            radius: Theme.radiusSm
            color: chip.event.allDay ? Theme.calendarColor(chip.event.calendarColor,
                                                           Theme.calendar) : chip.hovered
                                       ? Theme.surfaceAlt : "transparent"
        }

        contentItem: Row {
            leftPadding: Theme.space2
            spacing: Theme.space2

            Rectangle {
                visible: !chip.event.allDay
                anchors.verticalCenter: parent.verticalCenter
                width: 7
                height: 7
                radius: width / 2
                color: Theme.calendarColor(chip.event.calendarColor, Theme.calendar)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: chip.width - x - Theme.space2
                text: chip.event.allDay ? chip.event.summary : qsTr("%1 %2").arg(Settings.times.time(
                                                                                     chip.event.start)).arg(
                                              chip.event.summary)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: chip.event.allDay ? Theme.calendarInk(chip.event.calendarColor,
                                                             Theme.calendar) : Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textXs
                font.weight: Font.Bold
                font.strikeout: chip.event.declined
            }
        }
    }
}
