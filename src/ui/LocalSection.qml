pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick

/// The calendars kept on this computer: each with a way to remove it, then a
/// name box to make another.
Column {
    id: root

    spacing: Theme.space3

    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: Local.available ? qsTr(
                                    "Kept as files that other calendar apps, such as khal, can read too.") :
                                qsTr("Showing sample data, so these calendars cannot be changed here.")
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }

    Repeater {
        model: Local.calendars

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
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
                font.weight: Theme.weightStrong
            }
            // Removing takes a second click; the folder goes to the trash.
            DangerButton {
                id: remove
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                label: qsTr("Remove")
                armedLabel: qsTr("Click again to move to the trash")
                onConfirmed: Local.remove(row.modelData.id)
            }
        }
    }

    Row {
        visible: Local.available
        spacing: Theme.space3

        Field {
            id: name
            width: 260
            placeholderText: qsTr("New calendar")
            Accessible.name: qsTr("New calendar's name")
            onAccepted: add.clicked()
        }
        StickerButton {
            id: add
            anchors.verticalCenter: parent.verticalCenter
            accent: true
            glyph: "plus"
            text: qsTr("Add")
            enabled: name.text.trim() !== ""
            opacity: enabled ? 1 : Theme.fadedOpacity
            onClicked: {
                if (Local.add(name.text) !== "")
                    name.clear()
            }
        }
    }

    Text {
        visible: Local.error !== ""
        width: parent.width
        wrapMode: Text.Wrap
        text: Local.error
        color: Theme.danger
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }
}
