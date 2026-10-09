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
            /// Callie asks for more than this account granted when it signed in.
            readonly property bool missing: !!entry && (entry.missingScopes || []).length > 0
            readonly property url photo: Settings.accountPhotos[modelData.id] || ""
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

                // The account's photo, round, over its letter until it loads.
                Canvas {
                    id: photo

                    readonly property url source: row.photo
                    property bool ready: false

                    function load() {
                        ready = false
                        if (source.toString() !== "")
                            loadImage(source)
                    }

                    anchors.fill: parent
                    visible: ready
                    Component.onCompleted: load()
                    onSourceChanged: load()
                    onImageLoaded: {
                        ready = isImageLoaded(source)
                        requestPaint()
                    }
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.beginPath()
                        ctx.arc(width / 2, height / 2, width / 2, 0, 2 * Math.PI)
                        ctx.clip()
                        ctx.drawImage(source, 0, 0, width, height)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: !photo.ready
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
                    text: row.problem !== "" ? row.problem : row.missing ? qsTr(
                                                                               "Reconnect to grant new permissions") :
                                                                           !row.entry || isNaN(
                                                                               row.entry.lastSynced.getTime(
                                                                                   )) ? qsTr(
                                                                                            "Not synced yet") :
                                                                                        qsTr("Synced %1").arg(
                                                                                            Settings.times.time(
                                                                                                row.entry.lastSynced))
                    color: row.problem !== "" ? Theme.danger : row.missing ? Theme.accent :
                                                                             Theme.textMuted

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
                    id: reconnect
                    visible: Accounts.unavailable === ""
                    enabled: !Accounts.busy
                    accent: row.problem !== "" || row.missing
                    text: qsTr("Reconnect")
                    onClicked: Accounts.reconnect(row.modelData.id)

                    // A dot when the account has not granted all Callie asks for.
                    Rectangle {
                        visible: row.missing
                        anchors {
                            right: parent.right
                            top: parent.top
                            rightMargin: -width / 3
                            topMargin: -height / 3
                        }
                        width: Theme.space3
                        height: width
                        radius: width / 2
                        color: Theme.danger
                        border.width: 2
                        border.color: Theme.surface
                    }
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
