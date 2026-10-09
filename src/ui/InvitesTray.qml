pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Invitations waiting for an answer: answer each here, or open it on its
/// day. A repeating invitation is answered for the whole series.
Popup {
    id: root

    required property InvitesModel model
    property CalendarSource source

    /// Asks the window to show this invitation on its day.
    signal jumpRequested(date day, string uid, date start)

    width: 340
    padding: Theme.space5
    // A click elsewhere only closes the tray.
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Scrim {}

    EventActions {
        id: actions
        source: root.source
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

        Text {
            text: qsTr("Invitations")
            color: Theme.text
            font.family: Theme.displayFontFamily
            font.pixelSize: Theme.textLg
            font.weight: Font.Bold
        }

        Text {
            visible: root.model.count === 0
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Nothing is waiting for an answer.")
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
        }

        Text {
            visible: actions.error !== ""
            width: parent.width
            wrapMode: Text.Wrap
            text: actions.error
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
        }

        ListView {
            id: list
            visible: root.model.count > 0
            width: parent.width
            // Up to half the window's height before the list scrolls.
            height: Math.min(contentHeight, Window.height / 2)
            clip: true
            spacing: Theme.space3
            model: root.model
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: invite

                required property int index
                required property string summary
                required property date start
                required property date end
                required property bool allDay
                required property color calendarColor
                required property bool repeats
                /// An answer waiting for its this-event-or-all choice.
                property string pending

                function answer(status, wholeSeries) {
                    pending = ""
                    actions.respond(root.model.inviteAt(index), status, wholeSeries)
                }

                width: list.width
                height: body.implicitHeight + 2 * Theme.space3
                radius: Theme.radiusLg
                color: rowHover.hovered ? Theme.surfaceAlt : "transparent"
                border.color: Theme.border

                HoverHandler {
                    id: rowHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: {
                        root.jumpRequested(Settings.times.date(invite.start), root.model.inviteAt(
                                               invite.index).uid, invite.start)
                        root.close()
                    }
                }

                Column {
                    id: body
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: Theme.space4
                        rightMargin: Theme.space4
                    }
                    spacing: Theme.space2

                    Row {
                        width: parent.width
                        spacing: Theme.space2

                        CalendarMark {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.space3
                            height: Theme.space3
                            calendarColor: invite.calendarColor
                            response: "needsAction"
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.space3 - parent.spacing
                            text: invite.summary
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textMd
                            font.weight: Font.ExtraBold
                        }
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: {
                            const day = Qt.formatDate(Settings.times.date(invite.start),
                                                      "ddd, MMM d")
                            const when = invite.allDay ? day : qsTr("%1, %2").arg(day).arg(
                                                             Settings.times.time(invite.start))
                            return invite.repeats ? qsTr("%1, repeats").arg(when) : when
                        }
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                        font.weight: Font.Bold
                    }
                    Row {
                        visible: invite.pending === ""
                        spacing: Theme.space2

                        Repeater {
                            model: [
                                {
                                    "status": "accepted",
                                    "label": qsTr("Yes")
                                },
                                {
                                    "status": "tentative",
                                    "label": qsTr("Maybe")
                                },
                                {
                                    "status": "declined",
                                    "label": qsTr("No")
                                }
                            ]

                            PillButton {
                                required property var modelData
                                label: modelData.label
                                hoverColor: Theme.answerHover
                                enabled: !actions.busy
                                Accessible.name: qsTr("%1 to %2").arg(modelData.label).arg(
                                                     invite.summary)
                                // A repeating invitation asks which occurrences first, as the card does.
                                onClicked: {
                                    if (invite.repeats)
                                        invite.pending = modelData.status
                                    else
                                        invite.answer(modelData.status, false)
                                }
                            }
                        }
                    }
                    Flow {
                        visible: invite.pending !== ""
                        width: parent.width
                        spacing: Theme.space2

                        PillButton {
                            label: qsTr("This event")
                            hoverColor: Theme.answerHover
                            enabled: !actions.busy
                            onClicked: invite.answer(invite.pending, false)
                        }
                        PillButton {
                            label: qsTr("All events")
                            hoverColor: Theme.answerHover
                            enabled: !actions.busy
                            onClicked: invite.answer(invite.pending, true)
                        }
                        PillButton {
                            label: qsTr("Cancel")
                            hoverColor: Theme.answerHover
                            onClicked: invite.pending = ""
                        }
                    }
                }
            }
        }
    }
}
