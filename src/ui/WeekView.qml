pragma ComponentBehavior: Bound
import Callie.Ui

import QtQuick
import QtQuick.Controls

Item {
    id: root

    required property EventModel model
    property date anchorDate: new Date()
    property int dayCount: 7

    readonly property real dayWidth: (width - Theme.gutterWidth) / dayCount

    function dateForColumn(i) {
        const d = new Date(root.anchorDate)
        d.setDate(d.getDate() + i)
        return d
    }

    function isToday(d) {
        const now = new Date()
        if (d.getFullYear() !== now.getFullYear())
            return false
        if (d.getMonth() !== now.getMonth())
            return false
        return d.getDate() === now.getDate()
    }

    // ---- Day header --------------------------------------------------------
    Item {
        id: header
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 62

        Row {
            anchors.fill: parent
            leftPadding: Theme.gutterWidth

            Repeater {
                model: root.dayCount

                Item {
                    id: dayHeader
                    required property int index
                    readonly property date date: root.dateForColumn(index)
                    readonly property bool today: root.isToday(date)

                    width: root.dayWidth
                    height: header.height

                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.space1

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Qt.formatDate(dayHeader.date, "ddd").toUpperCase()
                            color: dayHeader.today ? Theme.accent : Theme.textFaint
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textXs
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.6
                        }

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 30
                            height: 30
                            radius: 15
                            color: dayHeader.today ? Theme.accent : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: dayHeader.date.getDate()
                                color: dayHeader.today ? Theme.accentText : Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textLg
                                font.weight: dayHeader.today ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            color: Theme.border
        }
    }

    // ---- All-day events ----------------------------------------------------
    Item {
        id: allDayStrip

        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
        }
        height: root.model.allDayRows > 0 ? root.model.allDayRows * Theme.allDayRowHeight + Theme.space2
                                            * 2 : 0
        visible: height > 0
        clip: true

        Repeater {
            model: root.model

            Rectangle {
                id: chip
                required property string summary
                required property bool allDay
                required property int firstDay
                required property int daySpan
                required property int lane
                required property color calendarColor
                readonly property color accent: Theme.calendarColor(calendarColor, Theme.calendar)

                visible: allDay
                x: Theme.gutterWidth + firstDay * root.dayWidth + 3
                y: Theme.space2 + lane * Theme.allDayRowHeight
                width: daySpan * root.dayWidth - 6
                height: Theme.allDayRowHeight - 3
                radius: Theme.radiusMd
                color: Theme.tint(accent, Theme.dark ? 0.32 : 0.18)

                Text {
                    anchors {
                        fill: parent
                        leftMargin: Theme.space3
                        rightMargin: Theme.space2
                    }
                    verticalAlignment: Text.AlignVCenter
                    text: chip.summary
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.DemiBold
                }
            }
        }

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            color: Theme.border
        }
    }

    // ---- Scrollable time grid ---------------------------------------------
    Flickable {
        id: grid
        anchors {
            top: allDayStrip.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        contentHeight: 24 * Theme.hourHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // Open on the working day rather than midnight.
        Component.onCompleted: contentY = 7 * Theme.hourHeight

        ScrollBar.vertical: ScrollBar {}

        Item {
            width: grid.width
            height: grid.contentHeight

            // Hour lines + gutter labels
            Repeater {
                model: 24

                Item {
                    id: hourRow
                    required property int index
                    y: index * Theme.hourHeight
                    width: grid.width
                    height: Theme.hourHeight

                    Rectangle {
                        anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                        }
                        anchors.leftMargin: Theme.gutterWidth
                        height: 1
                        color: Theme.hairline
                    }

                    Text {
                        anchors {
                            right: parent.left
                            top: parent.top
                        }
                        anchors.rightMargin: -Theme.gutterWidth + Theme.space4
                        anchors.topMargin: -6
                        visible: hourRow.index > 0
                        text: Qt.formatTime(new Date(2000, 0, 1, hourRow.index, 0), "h AP")
                        color: Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textXs
                        font.features: {
                            "tnum": 1
                        }
                    }
                }
            }

            // Day column separators
            Repeater {
                model: root.dayCount - 1

                Rectangle {
                    required property int index
                    x: Theme.gutterWidth + (index + 1) * root.dayWidth
                    width: 1
                    height: grid.contentHeight
                    color: Theme.hairline
                }
            }

            // Events
            Repeater {
                model: root.model

                EventBlock {
                    required property string uid
                    required property int dayIndex
                    required property int startMinutes
                    required property int durationMinutes
                    required property int lane
                    required property int laneCount
                    required property color calendarColor
                    required property bool allDay

                    readonly property real laneWidth: (root.dayWidth - 6) / laneCount

                    visible: !allDay && dayIndex >= 0 && dayIndex < root.dayCount

                    x: Theme.gutterWidth + dayIndex * root.dayWidth + 3 + lane * laneWidth
                    width: laneWidth - (laneCount > 1 ? 3 : 0)
                    y: startMinutes / 60 * Theme.hourHeight
                    height: Math.max(20, durationMinutes / 60 * Theme.hourHeight - 2)

                    accent: Theme.calendarColor(calendarColor, Theme.calendar)

                    onMoveRequested: (deltaMinutes, deltaDays) => {
                        // TODO(core): commit the move once the model can write.
                        console.log("move", uid, deltaMinutes, "min", deltaDays, "days")
                    }
                }
            }

            // Now indicator
            Item {
                id: now
                property date current: new Date()
                readonly property int columnIndex: {
                    for (let i = 0; i < root.dayCount; ++i)
                        if (root.isToday(root.dateForColumn(i)))
                            return i
                    return -1
                }

                visible: columnIndex >= 0
                y: (current.getHours() * 60 + current.getMinutes()) / 60 * Theme.hourHeight
                x: Theme.gutterWidth + columnIndex * root.dayWidth
                width: root.dayWidth
                height: 1
                z: 50

                Rectangle {
                    anchors.fill: parent
                    color: Theme.accent
                }

                Rectangle {
                    anchors {
                        verticalCenter: parent.top
                        left: parent.left
                    }
                    width: 7
                    height: 7
                    radius: 3.5
                    color: Theme.accent
                }

                Timer {
                    interval: 30000
                    running: true
                    repeat: true
                    onTriggered: now.current = new Date()
                }
            }
        }
    }
}
