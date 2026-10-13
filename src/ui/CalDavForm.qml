pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick

/// Signs in to a CalDAV account: a service set up for, or any server, with a
/// user name and an app password.
Column {
    id: root

    /// Fills the form in for signing in again to an account already listed.
    function prefill(account) {
        preset = "other"
        server.text = account.server || ""
        user.text = account.user || account.id
        password.clear()
        password.forceActiveFocus()
    }
    function clear() {
        server.clear()
        user.clear()
        password.clear()
    }

    signal cancelled
    signal submitted

    property string preset: "fastmail"
    readonly property var chosen: Accounts.calDavPresets().find(p => p.id === root.preset) || ({})

    spacing: Theme.space3

    Row {
        width: parent.width
        spacing: Theme.space4

        Text {
            id: serviceLabel
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Service")
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
            font.weight: Theme.weightStrong
        }
        Dropdown {
            model: Accounts.calDavPresets()
            currentValue: root.preset
            maximumWidth: parent.width - serviceLabel.width - parent.spacing
            Accessible.name: qsTr("CalDAV service")
            onActivated: value => root.preset = value
        }
    }
    Field {
        id: server
        visible: !root.chosen.server
        width: parent.width
        placeholderText: qsTr("Server, such as cloud.example.com")
        Accessible.name: qsTr("Server")
        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
    }
    Field {
        id: user
        width: parent.width
        placeholderText: root.preset === "icloud" ? qsTr("Apple ID") : qsTr("Email or user name")
        Accessible.name: qsTr("User name")
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
    }
    Field {
        id: password
        width: parent.width
        placeholderText: qsTr("App password")
        Accessible.name: qsTr("App password")
        echoMode: TextInput.Password
        onAccepted: connect.clicked()
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        text: root.chosen.help || ""
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }
    Row {
        spacing: Theme.space3

        StickerButton {
            id: connect
            accent: true
            text: qsTr("Connect")
            enabled: !Accounts.busy && user.text.trim() !== "" && password.text !== "" && (!!root.chosen.server
                                                                                           || server.text.trim(
                                                                                               ) !== "")
            opacity: enabled ? 1 : Theme.fadedOpacity
            onClicked: {
                root.submitted()
                Accounts.connectCalDav(root.chosen.server || server.text, user.text, password.text)
            }
        }
        // Not while the server is asked: the account would be listed anyway.
        StickerButton {
            text: qsTr("Cancel")
            enabled: !Accounts.busy
            opacity: enabled ? 1 : Theme.fadedOpacity
            onClicked: root.cancelled()
        }
    }
}
