pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Callie.Ui

ApplicationWindow {
    id: window

    width: 1280
    height: 840
    minimumWidth: 880
    minimumHeight: 560
    visible: true
    title: qsTr("Callie")
    color: Theme.bg

    /// Monday of the displayed week.
    property date weekStart: {
        const d = new Date()
        const offset = (d.getDay() + 6) % 7   // JS Sunday=0 -> Monday-based
        d.setDate(d.getDate() - offset)
        d.setHours(0, 0, 0, 0)
        return d
    }

    function shiftWeeks(n) {
        const d = new Date(weekStart)
        d.setDate(d.getDate() + n * 7)
        weekStart = d
    }

    EventModel {
        id: events
        rangeStart: window.weekStart
        dayCount: 7
    }

    // ---- Header ------------------------------------------------------------
    header: Rectangle {
        height: 56
        color: Theme.surface

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.border
        }

        Row {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: Theme.space5 }
            spacing: Theme.space4

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Qt.formatDate(window.weekStart, "MMMM yyyy")
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textXl
                font.weight: Font.DemiBold
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space1

                NavButton { glyph: "‹"; onClicked: window.shiftWeeks(-1) }
                NavButton { glyph: "›"; onClicked: window.shiftWeeks(1) }
            }

            PillButton {
                anchors.verticalCenter: parent.verticalCenter
                label: qsTr("Today")
                onClicked: {
                    const d = new Date()
                    d.setDate(d.getDate() - ((d.getDay() + 6) % 7))
                    d.setHours(0, 0, 0, 0)
                    window.weekStart = d
                }
            }
        }

        // View switcher. Only Week is implemented so far.
        Row {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: Theme.space5 }
            spacing: 0

            Repeater {
                model: [qsTr("Day"), qsTr("Week"), qsTr("Month"), qsTr("Agenda")]

                PillButton {
                    required property string modelData
                    required property int index
                    label: modelData
                    selected: index === 1
                }
            }
        }
    }

    // ---- Sidebar + grid ----------------------------------------------------
    Row {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            width: 220
            height: parent.height
            color: Theme.surfaceAlt

            Rectangle {
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: 1
                color: Theme.border
            }

            Column {
                anchors { fill: parent; margins: Theme.space5 }
                spacing: Theme.space4

                Text {
                    text: qsTr("Calendars")
                    color: Theme.textFaint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textXs
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.6
                }

                // TODO(core): replace once CalendarInfo is exposed to QML.
                Repeater {
                    model: ListModel {
                        ListElement { name: "Work";     dot: "#5B8DEF" }
                        ListElement { name: "Focus";    dot: "#7C6BD6" }
                        ListElement { name: "Personal"; dot: "#2FA98C" }
                    }

                    Row {
                        required property string name
                        required property string dot
                        spacing: Theme.space3

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 9; height: 9; radius: 2.5
                            color: parent.dot
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.name
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textMd
                        }
                    }
                }
            }
        }

        WeekView {
            width: parent.width - 220
            height: parent.height
            model: events
            anchorDate: window.weekStart
        }
    }
}
