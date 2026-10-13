pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick

/// The calendars subscribed to by link: each with how its last fetch went and
/// a way to stop it, then a box for another link.
Column {
    id: root

    spacing: Theme.space3

    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: Subscribed.available ? qsTr(
                                         "Calendars others publish as a link, such as holidays or a team's schedule. Callie keeps a copy and fetches it again every hour.") :
                                     qsTr("Showing sample data, so subscriptions cannot be changed here.")
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }

    Repeater {
        model: Subscribed.calendars

        Item {
            id: row

            required property var modelData

            width: root.width
            height: Math.max(remove.implicitHeight, details.implicitHeight)

            Rectangle {
                id: dot
                anchors.top: parent.top
                anchors.topMargin: Theme.space2
                width: Theme.space3
                height: width
                radius: width / 2
                color: row.modelData.color
            }
            Column {
                id: details
                anchors {
                    left: dot.right
                    leftMargin: Theme.space3
                    right: remove.left
                    rightMargin: Theme.space3
                    verticalCenter: parent.verticalCenter
                }

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: row.modelData.name
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textMd
                    font.weight: Theme.weightStrong
                }
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: row.modelData.error !== "" ? row.modelData.error : row.modelData.fetched
                                                       ? qsTr("Updated %1").arg(Settings.times.time(
                                                                                    row.modelData.fetched)) :
                                                         qsTr("Fetching...")
                    color: row.modelData.error !== "" ? Theme.danger : Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                }
            }
            DangerButton {
                id: remove
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                label: qsTr("Unsubscribe")
                armedLabel: qsTr("Click again to unsubscribe")
                onConfirmed: Subscribed.remove(row.modelData.id)
            }
        }
    }

    Row {
        visible: Subscribed.available
        spacing: Theme.space3

        Field {
            id: link
            width: 320
            placeholderText: qsTr("webcal:// or https:// link")
            Accessible.name: qsTr("Link to a calendar")
            onAccepted: subscribe.clicked()
        }
        StickerButton {
            id: subscribe
            anchors.verticalCenter: parent.verticalCenter
            accent: true
            glyph: "plus"
            text: qsTr("Subscribe")
            enabled: link.text.trim() !== ""
            onClicked: {
                if (Subscribed.add(link.text) !== "")
                    link.clear()
            }
        }
    }

    Text {
        visible: Subscribed.error !== ""
        width: parent.width
        wrapMode: Text.Wrap
        text: Subscribed.error
        color: Theme.danger
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }
}
