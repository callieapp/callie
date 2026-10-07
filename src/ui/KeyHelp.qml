pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Every keyboard shortcut, from the same table the keys run on: those that
/// always work, then vi mode's keys and the commands after its leader.
Popup {
    id: root

    required property var actions

    anchors.centerIn: Overlay.overlay
    width: 620
    height: Math.min(implicitHeight, (parent ? parent.height : 800) - 2 * Theme.space6)
    padding: Theme.space6
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle {
        // The attached Window type is not the QML Window type, so this stays untyped.
        readonly property var appWindow: Window.window

        radius: appWindow && appWindow.cornerRadius ? appWindow.cornerRadius : 0
        color: Theme.tint(Theme.shadowColor, 0.35)
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

    contentItem: Flickable {
        implicitHeight: content.implicitHeight
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            width: parent.width
            spacing: Theme.space4

            Text {
                text: qsTr("Keyboard shortcuts")
                color: Theme.text
                font.family: Theme.displayFontFamily
                font.pixelSize: Theme.textXl
                font.weight: Font.Bold
            }

            Table {
                title: qsTr("Always")
                rows: root.actions.filter(a => a.keys.length > 0).map(a => ({
                    "keys": a.keys,
                    "label": a.inCard ? qsTr("%1 (in its card)").arg(a.label) : a.label
                }))
            }

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: Settings.viMode ? qsTr(
                                            "Vi mode is on. Keys typed into a text field are left alone.") :
                                        qsTr("Vi mode is off; turn it on under Settings, Keyboard.")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
            }

            Table {
                title: qsTr("Vi mode")
                rows: root.actions.filter(a => a.vi !== "").map(a => ({
                    "keys": [a.vi],
                    "label": a.label
                })).concat([
                {
                    "keys": ["Esc"],
                    "label": qsTr("Close, or stop a command")
                }
                    ])
                }

                    Table {
                        title: qsTr("After the leader (%1)").arg(Settings.leaderKey === " " ? qsTr(
                                                                                                  "Space") :
                                                                                              Settings.leaderKey)
                        rows: root.actions.filter(a => a.leader !== "").map(a => ({
                            "keys": [a.leader],
                            "label": a.label
                        }))
                    }
                }
            }

            /// A titled list of keys and what they do, in two columns.
            component Table: Column {
                id: table

                required property string title
                required property var rows

                width: parent.width
                spacing: Theme.space2

                Text {
                    text: table.title
                    color: Theme.textFaint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.ExtraBold
                }
                Grid {
                    columns: 2
                    columnSpacing: Theme.space5
                    rowSpacing: Theme.space2

                    Repeater {
                        model: table.rows

                        Row {
                            id: row
                            required property var modelData
                            width: (table.width - Theme.space5) / 2
                            spacing: Theme.space3

                            Row {
                                id: caps
                                spacing: Theme.space1

                                Repeater {
                                    model: row.modelData.keys

                                    KeyCap {
                                        required property string modelData
                                        keys: modelData
                                    }
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: row.width - caps.width - row.spacing
                                elide: Text.ElideRight
                                text: row.modelData.label
                                color: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSm
                            }
                        }
                    }
                }
            }
        }
