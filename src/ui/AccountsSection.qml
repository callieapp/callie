pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// The connected Google accounts: each with how its sync is going, a way to
/// sign in again, and removal; then connecting another.
Column {
    id: root

    /// The source's syncReport, for each account's status line.
    property var report: []

    function entryFor(id) {
        for (const entry of report)
            if (entry.account === id)
                return entry
        return null
    }

    width: parent ? parent.width : 0
    spacing: Theme.space4

    Text {
        visible: Accounts.accounts.length === 0
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("No accounts yet. Connect a Google account to see its calendars here.")
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textMd
    }

    Repeater {
        model: Accounts.accounts

        Item {
            id: row

            required property var modelData
            readonly property var entry: root.entryFor(modelData.id)
            readonly property string problem: entry ? (entry.error || (entry.problems.length > 0
                                                                       ? entry.problems[0] : "")) :
                                                      ""

            width: root.width
            height: Math.max(avatar.height, details.implicitHeight)

            Rectangle {
                id: avatar
                width: 36
                height: 36
                radius: width / 2
                color: Theme.tint(Theme.accent, 0.25)

                Text {
                    anchors.centerIn: parent
                    text: row.modelData.id.charAt(0).toUpperCase()
                    color: Theme.accent
                    font.family: Theme.displayFontFamily
                    font.pixelSize: Theme.textLg
                    font.weight: Font.Bold
                }
            }

            Column {
                id: details
                anchors {
                    left: avatar.right
                    leftMargin: Theme.space3
                    right: actions.left
                    rightMargin: Theme.space3
                    verticalCenter: parent.verticalCenter
                }
                spacing: Theme.space1

                Text {
                    width: parent.width
                    text: row.modelData.id
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textMd
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    text: row.problem !== "" ? row.problem : !row.entry || isNaN(
                                                   row.entry.lastSynced.getTime()) ? qsTr(
                                                                                         "Not synced yet") :
                                                                                     qsTr("Synced %1").arg(
                                                                                         Settings.times.time(
                                                                                             row.entry.lastSynced))
                    color: row.problem !== "" ? Theme.danger : Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                }
            }

            Row {
                id: actions
                anchors {
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                }
                spacing: Theme.space2

                // Signs in again: mends a broken sign-in, and grants what Callie
                // has asked for since, such as reading contacts.
                StickerButton {
                    visible: Accounts.unavailable === ""
                    enabled: !Accounts.busy
                    accent: row.problem !== ""
                    text: qsTr("Reconnect")
                    onClicked: Accounts.reconnect(row.modelData.id)
                }
                // Removing takes a second click, since the account's sign-in goes too.
                DangerButton {
                    visible: Accounts.unavailable === ""
                    enabled: !Accounts.busy
                    label: qsTr("Remove")
                    armedLabel: qsTr("Click again to remove")
                    onConfirmed: Accounts.remove(row.modelData.id)
                }
            }
        }
    }

    Row {
        spacing: Theme.space3

        StickerButton {
            accent: true
            enabled: Accounts.unavailable === "" && !Accounts.busy
            opacity: enabled ? 1 : Theme.fadedOpacity
            glyph: "plus"
            text: qsTr("Connect a Google account")
            onClicked: Accounts.connectGoogle()
        }
        StickerButton {
            visible: Accounts.signingIn
            text: qsTr("Cancel")
            onClicked: Accounts.cancel()
        }
    }

    Text {
        visible: text !== ""
        width: parent.width
        wrapMode: Text.Wrap
        text: Accounts.unavailable !== "" ? Accounts.unavailable : Accounts.error !== ""
                                            ? Accounts.error : Accounts.status
        color: Accounts.error !== "" && Accounts.unavailable === "" ? Theme.danger : Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }
}
