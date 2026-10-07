pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick

/// After the leader key, if the next key does not come at once, shows every
/// key that can follow it and what each does.
Rectangle {
    id: root

    required property KeyRouter router
    readonly property var commands: router.actions.filter(a => a.leader !== "")

    anchors {
        horizontalCenter: parent.horizontalCenter
        bottom: parent.bottom
        bottomMargin: Theme.space6
    }
    z: 900
    width: grid.implicitWidth + 2 * Theme.space5
    height: column.implicitHeight + 2 * Theme.space4
    radius: Theme.radiusXl
    color: Theme.surface
    border.color: Theme.border
    visible: shown
    opacity: shown ? 1 : 0

    property bool shown: false

    // A quick leader and key never flash the panel.
    Timer {
        id: delay
        interval: Theme.whichKeyDelay
        running: root.router.pending === "leader"
        onTriggered: root.shown = true
    }
    Connections {
        target: root.router
        function onPendingChanged() {
            if (root.router.pending !== "leader")
                root.shown = false
        }
    }

    SoftShadow {
        anchors.fill: parent
        radius: parent.radius
    }

    Column {
        id: column
        anchors.centerIn: parent
        spacing: Theme.space3

        Row {
            spacing: Theme.space2

            KeyCap {
                keys: root.router.leaderKey
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.router.leaderTimeout > 0 ? qsTr("then") : qsTr("then, or Esc")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
                font.weight: Font.Bold
            }
        }

        Grid {
            id: grid
            columns: 3
            columnSpacing: Theme.space5
            rowSpacing: Theme.space2

            Repeater {
                model: root.commands

                Row {
                    id: command
                    required property var modelData
                    spacing: Theme.space2

                    KeyCap {
                        keys: command.modelData.leader
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: command.modelData.label
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                    }
                }
            }
        }
    }
}
