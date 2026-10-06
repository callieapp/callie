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
    // Callie draws its own title bar; see the title bar below and ResizeFrame.
    flags: Qt.Window | Qt.FramelessWindowHint

    /// Where events come from; set by main.cpp.
    required property CalendarSource source

    /// Monday of the displayed week. Set once rather than bound to the clock,
    /// which would pull the view back to this week on every tick.
    property date weekStart

    function mondayOf(day) {
        const d = new Date(day)
        // JS Sunday=0 -> Monday-based
        d.setDate(d.getDate() - (d.getDay() + 6) % 7)
        d.setHours(0, 0, 0, 0)
        return d
    }

    Component.onCompleted: weekStart = mondayOf(Clock.now)

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

    // ---- Title bar, which is also the toolbar ----------------------------
    Rectangle {
        id: titleBar

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: 58
        color: Theme.surface

        // Behind the controls: dragging the bar moves the window, double-clicking
        // maximizes it, as a desktop title bar would.
        DragHandler {
            target: null
            onActiveChanged: {
                if (active)
                    window.startSystemMove()
            }
        }
        TapHandler {
            onDoubleTapped: window.visibility === Window.Maximized ? window.showNormal() :
                                                                     window.showMaximized()
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

        Row {
            id: leading
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
                leftMargin: WindowButtons.left.length > 0 ? Theme.space4 : Theme.space5
            }
            spacing: Theme.space4

            WindowControls {
                anchors.verticalCenter: parent.verticalCenter
                visible: buttons.length > 0
                buttons: WindowButtons.left
            }

            Image {
                anchors.verticalCenter: parent.verticalCenter
                source: "qrc:/callie/assets/logo.png"
                sourceSize: Qt.size(56, 56)
                width: 28
                height: 28
                smooth: true
                mipmap: true
                Accessible.ignored: true
            }

            // As wide as the longest month, so the arrows stay put while paging.
            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: widestMonth.advanceWidth + yearWidth.advanceWidth + 7
                height: month.implicitHeight

                TextMetrics {
                    id: widestMonth
                    font: month.font
                    text: "September"
                }
                TextMetrics {
                    id: yearWidth
                    font: year.font
                    text: "0000"
                }

                Text {
                    id: month
                    text: Qt.formatDate(window.weekStart, "MMMM")
                    color: Theme.text
                    font.family: Theme.displayFontFamily
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }
                Text {
                    id: year
                    anchors {
                        left: month.right
                        leftMargin: 7
                        baseline: month.baseline
                    }
                    text: Qt.formatDate(window.weekStart, "yyyy")
                    color: Theme.textMuted
                    font.family: Theme.displayFontFamily
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space2

                StickerButton {
                    glyph: "chevron-left"
                    Accessible.name: qsTr("Previous week")
                    onClicked: window.shiftWeeks(-1)
                }
                StickerButton {
                    glyph: "chevron-right"
                    Accessible.name: qsTr("Next week")
                    onClicked: window.shiftWeeks(1)
                }
            }

            StickerButton {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Today")
                onClicked: window.weekStart = window.mondayOf(Clock.now)
            }
        }

        Row {
            id: trailing
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
                rightMargin: Theme.space4
            }
            spacing: Theme.space4

            // Sync status, with the error on hover.
            Rectangle {
                id: syncStatus

                readonly property bool failed: window.source.lastError !== "" &&
                                               !window.source.syncing

                readonly property bool known: window.source.syncing || failed || !isNaN(
                                                  window.source.lastSynced.getTime())
                readonly property color ink: failed ? Theme.danger : window.source.syncing
                                                      ? Theme.textMuted : Theme.accent

                anchors.verticalCenter: parent.verticalCenter
                visible: known
                height: 28
                width: syncRow.implicitWidth + 22
                radius: height / 2
                color: Theme.tint(ink, 0.16)

                Row {
                    id: syncRow
                    anchors.centerIn: parent
                    spacing: Theme.space3

                    Glyph {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !window.source.syncing && !syncStatus.failed
                        name: "check"
                        width: 12
                        height: 12
                        stroke: 1.8
                        color: syncStatus.ink
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: window.source.syncing ? qsTr("Syncing...") : syncStatus.failed ? qsTr(
                                                                                                   "Sync failed") :
                                                                                               qsTr("Updated %1").arg(
                                                                                                   Qt.formatTime(
                                                                                                       window.source.lastSynced,
                                                                                                       "HH:mm"))
                        color: syncStatus.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                        font.weight: Font.ExtraBold
                    }
                }

                HoverHandler {
                    id: syncHover
                }
                Tip {
                    visible: syncStatus.failed && syncHover.hovered
                    text: window.source.lastError
                }
            }

            // View switcher. Only Week is implemented so far.
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space2

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
            StickerButton {
                id: helpButton
                anchors.verticalCenter: parent.verticalCenter
                text: "?"
                Accessible.name: qsTr("Help")
                onClicked: helpMenu.popup(helpButton, 0, helpButton.height + Theme.space3)

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

                    // TODO(ui): DESIGN.md gives menus a soft shadow; the theme has the token,
                    // and the menus pass draws it.
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

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: WindowButtons.right.length > 0
                width: 1
                height: 26
                color: Theme.border
            }

            WindowControls {
                anchors.verticalCenter: parent.verticalCenter
                visible: buttons.length > 0
                buttons: WindowButtons.right
            }
        }
    }

    // ---- Sidebar + grid ----------------------------------------------------
    Row {
        anchors {
            left: parent.left
            right: parent.right
            top: titleBar.bottom
            bottom: parent.bottom
        }
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

    ResizeFrame {
        anchors.fill: parent
        z: 1000
    }
}
