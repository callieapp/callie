pragma ComponentBehavior: Bound
import Callie.Ui

import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window

    width: 1280
    height: 840
    minimumWidth: 880
    minimumHeight: 560
    visible: true
    title: qsTr("Callie")
    color: Theme.bg

    /// Where events come from; set by main.cpp.
    required property CalendarSource source

    /// Monday of the displayed week.
    property date weekStart: {
        const d = new Date()
        // JS Sunday=0 -> Monday-based
        const offset = (d.getDay() + 6) % 7
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
        source: window.source
        rangeStart: window.weekStart
        dayCount: 7
    }

    // ---- Header ------------------------------------------------------------
    header: Rectangle {
        height: 56
        color: Theme.surface

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            color: Theme.border
        }

        Row {
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
                leftMargin: Theme.space5
            }
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

                NavButton {
                    glyph: "‹"
                    onClicked: window.shiftWeeks(-1)
                }
                NavButton {
                    glyph: "›"
                    onClicked: window.shiftWeeks(1)
                }
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

        // Sync status, with the error on hover.
        Text {
            id: syncStatus
            readonly property bool failed: window.source.lastError !== ""

            anchors {
                right: viewSwitcher.left
                verticalCenter: parent.verticalCenter
                rightMargin: Theme.space5
            }
            text: window.source.syncing ? qsTr("Syncing...") : failed ? qsTr("Sync failed") : isNaN(
                                                                            window.source.lastSynced.getTime(
                                                                                )) ? "" : qsTr(
                                                                                         "Updated %1").arg(
                                                                                         Qt.formatTime(
                                                                                             window.source.lastSynced,
                                                                                             "HH:mm"))
            color: failed && !window.source.syncing ? Theme.danger : Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm

            HoverHandler {
                id: syncHover
            }
            ToolTip.visible: syncStatus.failed && syncHover.hovered
            ToolTip.text: window.source.lastError
        }

        // View switcher. Only Week is implemented so far.
        Row {
            id: viewSwitcher
            anchors {
                right: helpButton.left
                verticalCenter: parent.verticalCenter
                rightMargin: Theme.space3
            }
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

        // Help: debug info, logs and bug reports.
        NavButton {
            id: helpButton
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
                rightMargin: Theme.space5
            }
            glyph: "?"
            onClicked: helpMenu.popup(helpButton, 0, helpButton.height)

            Tip {
                visible: copiedTip.running
                text: qsTr("Debug info copied")
            }

            Timer {
                id: copiedTip
                interval: 2000
            }

            Connections {
                target: Support
                function onCopied() {
                    copiedTip.restart()
                }
            }

            Menu {
                id: helpMenu
                padding: Theme.space1

                // TODO(ui): DESIGN.md gives menus a soft shadow; add one once the theme
                // has a shadow token, which the styling pass decides.
                background: Rectangle {
                    implicitWidth: 200
                    color: Theme.surface
                    border.color: Theme.border
                    radius: Theme.radiusLg
                }

                MenuEntry {
                    text: qsTr("Copy debug info")
                    onTriggered: Support.copyDebugInfo()
                }
                MenuEntry {
                    text: qsTr("Open logs folder")
                    onTriggered: Support.openLogs()
                }
                MenuEntry {
                    text: qsTr("Report a bug...")
                    onTriggered: Support.reportBug()
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
                anchors {
                    right: parent.right
                    top: parent.top
                    bottom: parent.bottom
                }
                width: 1
                color: Theme.border
            }

            Column {
                anchors {
                    fill: parent
                    margins: Theme.space5
                }
                spacing: Theme.space4

                Text {
                    text: qsTr("Calendars")
                    color: Theme.textFaint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textXs
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.6
                }

                Repeater {
                    model: events.calendars

                    Row {
                        id: calendarRow
                        required property var modelData
                        width: parent.width
                        spacing: Theme.space3

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 9
                            height: 9
                            radius: 2.5
                            color: Theme.calendarColor(calendarRow.modelData.color, Theme.calendar)
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: calendarRow.width - 9 - Theme.space3
                            text: calendarRow.modelData.name
                            elide: Text.ElideRight
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
