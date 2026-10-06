pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// What is coming up, day by day: only the days that hold something, each
/// with its events in order.
Item {
    id: root

    required property EventModel model
    required property date rangeStart
    required property int dayCount

    /// {date, events} for each day that has events.
    readonly property var days: {
        const list = []
        for (let i = 0; i < dayCount; ++i) {
            const events = model.eventsOn(i, model.revision)
            if (events.length > 0) {
                list.push({
                              "date": Views.dayAt(rangeStart, i),
                              "events": events
                          })
            }
        }
        return list
    }

    function showEvent(event, item) {
        details.showNear(event, root.model.callService(event.conferenceUrl), item)
    }

    Text {
        anchors.centerIn: parent
        visible: root.days.length === 0
        text: qsTr("Nothing planned in the next %n days.", "", root.dayCount)
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textMd
        font.weight: Font.DemiBold
    }

    ListView {
        anchors {
            fill: parent
            leftMargin: Theme.space7
            rightMargin: Theme.space7
        }
        topMargin: Theme.space6
        bottomMargin: Theme.space6
        clip: true
        spacing: Theme.space6
        boundsBehavior: Flickable.StopAtBounds
        model: root.days
        ScrollBar.vertical: ScrollBar {}

        delegate: Column {
            id: day

            required property var modelData

            width: ListView.view.width
            spacing: Theme.space2

            Text {
                text: Views.heading(day.modelData.date, Settings.times.date(Clock.now))
                color: Theme.text
                font.family: Theme.displayFontFamily
                font.pixelSize: Theme.textLg
                font.weight: Font.Bold
            }

            Repeater {
                model: day.modelData.events

                AbstractButton {
                    id: row

                    required property var modelData
                    readonly property var event: modelData
                    readonly property bool faded: event.declined || (Settings.dimPast && event.end
                                                                     < Clock.now)

                    width: day.width
                    height: Theme.listRowHeight + Theme.space3
                    opacity: faded ? Theme.fadedOpacity : 1
                    Accessible.name: event.summary
                    onClicked: root.showEvent(row.event, row)

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    background: Rectangle {
                        radius: Theme.radiusMd
                        color: row.hovered ? Theme.surfaceAlt : "transparent"
                    }

                    contentItem: Row {
                        leftPadding: Theme.space3
                        spacing: Theme.space4

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 150
                            text: row.event.allDay ? qsTr("All day") : qsTr("%1 to %2").arg(Settings.times.time(
                                                                                                row.event.start)).arg(
                                                         Settings.times.time(row.event.end))
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSm
                            font.weight: Font.Bold
                            font.features: {
                                "tnum": 1
                            }
                        }
                        // The calendar's sticker color as a small bar.
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 6
                            height: row.height - Theme.space4
                            radius: width / 2
                            color: Theme.calendarColor(row.event.calendarColor, Theme.calendar)
                        }
                        Text {
                            id: title
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, row.width - 150 - 6 - 3 * Theme.space4
                                            - Theme.space3)
                            text: row.event.summary
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textMd
                            font.weight: Font.Bold
                            font.strikeout: row.event.declined
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.event.location !== ""
                            width: Math.max(0, row.width - title.x - title.width - 2 * Theme.space4)
                            text: row.event.location
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSm
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }
    }

    EventDetails {
        id: details
    }
}
