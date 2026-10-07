pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A date as a button that opens a month to pick a day from.
AbstractButton {
    id: root

    /// The day, as a local midnight.
    property date day

    signal picked(date day)

    implicitHeight: Theme.listRowHeight + Theme.space2
    implicitWidth: caption.implicitWidth + 2 * Theme.space4
    Accessible.name: caption.text
    onClicked: {
        month.month = root.day
        calendar.open()
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: root.hovered || calendar.opened ? Theme.border : Theme.surfaceAlt
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.text
    }
    contentItem: Text {
        id: caption
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: Qt.formatDate(root.day, "ddd, MMM d, yyyy")
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textMd
        font.weight: Font.Bold
    }

    MonthModel {
        id: month
        firstDay: Settings.firstDayOfWeek
        today: Settings.times.date(Clock.now)
    }

    Popup {
        id: calendar
        y: root.height + Theme.space1
        padding: Theme.space3

        background: Rectangle {
            radius: Theme.radiusLg
            color: Theme.surface
            border.color: Theme.border

            SoftShadow {
                anchors.fill: parent
                radius: parent.radius
            }
        }

        contentItem: Column {
            spacing: Theme.space2

            Row {
                spacing: Theme.space2

                StickerButton {
                    glyph: "chevron-left"
                    Accessible.name: qsTr("Previous month")
                    onClicked: {
                        const d = month.month
                        month.month = new Date(d.getFullYear(), d.getMonth() - 1, 1)
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7 * Theme.space7 - 2 * Theme.space7 - 2 * parent.spacing
                    horizontalAlignment: Text.AlignHCenter
                    text: Qt.formatDate(month.month, "MMMM yyyy")
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textMd
                    font.weight: Font.ExtraBold
                }
                StickerButton {
                    glyph: "chevron-right"
                    Accessible.name: qsTr("Next month")
                    onClicked: {
                        const d = month.month
                        month.month = new Date(d.getFullYear(), d.getMonth() + 1, 1)
                    }
                }
            }

            Grid {
                columns: 7

                Repeater {
                    model: month

                    AbstractButton {
                        id: cell

                        required property date date
                        required property int day
                        required property bool inMonth
                        required property bool isToday
                        readonly property bool chosen: date.getTime() === root.day.getTime()

                        width: Theme.space7
                        height: Theme.space7
                        Accessible.name: Qt.formatDate(date, Qt.locale().dateFormat(
                                                           Locale.LongFormat))
                        onClicked: {
                            calendar.close()
                            root.picked(date)
                        }

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        background: Rectangle {
                            radius: Theme.radiusSm
                            color: cell.chosen ? Theme.accent : cell.hovered ? Theme.surfaceAlt :
                                                                               "transparent"

                            border.width: cell.isToday && !cell.chosen ? 1 : 0
                            border.color: Theme.accent
                        }
                        contentItem: Text {
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: cell.day
                            color: cell.chosen ? Theme.accentText : cell.inMonth ? Theme.text :
                                                                                   Theme.textFaint

                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSm
                            font.weight: Font.Bold
                        }
                    }
                }
            }
        }
    }
}
