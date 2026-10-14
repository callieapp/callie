pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick

/// A CalDAV account's calendars, each with a way to delete it on the server,
/// then a name box to make another there.
Column {
    id: root

    property string accountId
    readonly property var calendars: ServerCalendars.calendars[accountId] || []

    spacing: Theme.space2

    Repeater {
        model: root.calendars

        Item {
            id: row

            required property var modelData

            width: root.width
            height: remove.implicitHeight

            Rectangle {
                id: dot
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.space3
                height: width
                radius: width / 2
                color: row.modelData.color
            }
            Text {
                anchors {
                    left: dot.right
                    leftMargin: Theme.space3
                    right: remove.left
                    rightMargin: Theme.space3
                    verticalCenter: parent.verticalCenter
                }
                elide: Text.ElideRight
                text: row.modelData.name
                textFormat: Text.PlainText
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
            }
            // Deleting takes a second click, since the server keeps no copy.
            DangerButton {
                id: remove
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                enabled: !ServerCalendars.busy
                label: qsTr("Delete")
                armedLabel: qsTr("Click again to delete it on the server")
                onConfirmed: ServerCalendars.remove(row.modelData.id)
            }
        }
    }

    Row {
        spacing: Theme.space3

        Field {
            id: name
            width: 240
            placeholderText: qsTr("New calendar")
            Accessible.name: qsTr("New calendar's name")
            onAccepted: add.clicked()
        }
        StickerButton {
            id: add
            anchors.verticalCenter: parent.verticalCenter
            glyph: "plus"
            text: ServerCalendars.busy ? qsTr("Adding...") : qsTr("Add")
            enabled: name.text.trim() !== "" && !ServerCalendars.busy
            onClicked: ServerCalendars.add(root.accountId, name.text)
        }
    }

    Connections {
        target: ServerCalendars
        function onMade(accountId) {
            if (accountId === root.accountId)
                name.clear()
        }
    }

    Text {
        visible: ServerCalendars.error !== "" && ServerCalendars.errorAccount === root.accountId
        width: parent.width
        wrapMode: Text.Wrap
        text: ServerCalendars.error
        color: Theme.danger
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }
}
