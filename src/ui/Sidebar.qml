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

    color: Theme.surface

    Column {
        anchors {
            fill: parent
            leftMargin: 18
            rightMargin: 18
            topMargin: 22
        }
        spacing: 20

        Column {
            width: parent.width
            spacing: Theme.space1

            Text {
                text: root.today.greeting
                color: Theme.text
                font.family: Theme.displayFontFamily
                font.pixelSize: 21
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
            height: nextColumn.implicitHeight + 24

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
                    leftMargin: 14
                    rightMargin: 14
                }
                spacing: Theme.space2

                Text {
                    width: parent.width
                    text: root.today.nextLabel.toUpperCase()
                    color: upNext.ink
                    opacity: 0.75
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textXs
                    font.weight: Font.ExtraBold
                    font.letterSpacing: 0.6
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: root.today.nextTitle
                    color: upNext.ink
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textLg
                    font.weight: Font.ExtraBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: root.today.nextDetail
                    color: upNext.ink
                    opacity: 0.8
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
        }

        // Mini month: the shown week is shaded, today is a pink sticker.
        Column {
            width: parent.width
            spacing: Theme.space3

            Row {
                Repeater {
                    // Monday first, as the week view is.
                    model: [1, 2, 3, 4, 5, 6, 0]

                    Text {
                        required property int modelData
                        width: (root.width - 36) / 7
                        horizontalAlignment: Text.AlignHCenter
                        text: Qt.locale().dayName(modelData, Locale.NarrowFormat)
                        color: Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        font.weight: Font.ExtraBold
                    }
                }
            }

            Grid {
                columns: 7
                rowSpacing: 2

                Repeater {
                    model: root.month

                    AbstractButton {
                        id: dayCell

                        required property date date
                        required property int day
                        required property bool inMonth
                        required property bool inWeek
                        required property bool isToday

                        width: (root.width - 36) / 7
                        height: 26
                        enabled: inMonth
                        focusPolicy: Qt.TabFocus
                        Accessible.name: Qt.formatDate(date, Qt.locale().dateFormat(
                                                           Locale.LongFormat))
                        onClicked: root.dayPicked(date)

                        background: Item {
                            Rectangle {
                                visible: dayCell.isToday
                                anchors {
                                    fill: parent
                                    topMargin: 2
                                    bottomMargin: -2
                                }
                                radius: 9
                                color: Theme.accentEdge
                            }
                            Rectangle {
                                anchors.fill: parent
                                radius: 9
                                color: dayCell.isToday ? Theme.accent : dayCell.inWeek
                                                         ? Theme.surfaceAlt : dayCell.hovered
                                                           && dayCell.inMonth ? Theme.tint(
                                                                                    Theme.surfaceAlt,
                                                                                    0.5) : "transparent"
                                border.width: dayCell.visualFocus ? 2 : 0
                                border.color: Theme.text
                            }
                        }
                        contentItem: Text {
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: dayCell.inMonth ? dayCell.day : ""
                            color: dayCell.isToday ? Theme.accentText : dayCell.inWeek ? Theme.text :
                                                                                         Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSm
                            font.weight: dayCell.isToday ? Font.ExtraBold : dayCell.inWeek
                                                           ? Font.Bold : Font.Normal
                            font.features: {
                                "tnum": 1
                            }
                        }

                        HoverHandler {
                            cursorShape: dayCell.inMonth ? Qt.PointingHandCursor : Qt.ArrowCursor
                        }
                    }
                }
            }
        }

        // Calendars: deliberately quiet, so the event stickers draw the eye.
        Column {
            width: parent.width
            spacing: 10

            Text {
                text: qsTr("CALENDARS")
                color: Theme.textFaint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textXs
                font.weight: Font.ExtraBold
                font.letterSpacing: 0.6
            }

            Repeater {
                model: root.events.calendars

                Row {
                    id: calendarRow
                    required property var modelData
                    width: parent.width
                    spacing: 10

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14
                        height: 14
                        radius: 5
                        color: Theme.calendarColor(calendarRow.modelData.color, Theme.calendar)
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: calendarRow.width - 14 - 10
                        text: calendarRow.modelData.name
                        elide: Text.ElideRight
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }
}
