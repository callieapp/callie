pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Picks the time zone Callie shows times in: the system's, or any IANA zone,
/// found by typing part of its name.
AbstractButton {
    id: root

    /// An IANA id, or empty for the system zone.
    property string zoneId
    signal picked(string zoneId)

    implicitHeight: 32
    implicitWidth: 260
    padding: 0
    focusPolicy: Qt.StrongFocus
    Accessible.name: qsTr("Time zone: %1").arg(label.text)
    onClicked: list.open()

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: root.hovered ? Theme.border : Theme.surfaceAlt
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.text
    }

    contentItem: Text {
        id: label
        leftPadding: Theme.space4
        rightPadding: Theme.space4
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: root.zoneId === "" ? qsTr("Same as the system") : root.zoneId.replace(/_/g, " ")
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textMd
        font.weight: Font.DemiBold
    }

    Popup {
        id: list

        readonly property var zones: Settings.availableTimeZones()
        readonly property var matches: {
            const needle = search.text.trim().toLowerCase().replace(/ /g, "_")
            const found = [""]
            for (const zone of zones) {
                if (needle === "" || zone.toLowerCase().indexOf(needle) >= 0)
                    found.push(zone)
            }
            return needle === "" ? found : found.slice(1)
        }

        y: root.height + Theme.space2
        width: root.width
        height: 300
        padding: Theme.space2
        focus: true
        onOpened: {
            search.text = ""
            search.forceActiveFocus()
        }

        background: Rectangle {
            radius: Theme.radiusLg
            color: Theme.surface
            border.color: Theme.border

            SoftShadow {
                anchors.fill: parent
                radius: parent.radius
            }
        }

        contentItem: Column {
            spacing: Theme.space2

            TextField {
                id: search
                width: parent.width
                placeholderText: qsTr("Search, e.g. Tokyo")
                placeholderTextColor: Theme.textFaint
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
                Keys.onReturnPressed: {
                    if (list.matches.length > 0) {
                        root.picked(list.matches[0])
                        list.close()
                    }
                }
                background: Rectangle {
                    radius: Theme.radiusMd
                    color: Theme.surfaceAlt
                    border.color: search.activeFocus ? Theme.accent : Theme.border
                }
            }

            ListView {
                width: parent.width
                height: parent.height - search.height - parent.spacing
                clip: true
                model: list.matches
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    id: row
                    required property string modelData

                    width: ListView.view.width
                    height: 28
                    onClicked: {
                        root.picked(modelData)
                        list.close()
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: row.hovered ? Theme.surfaceAlt : "transparent"
                    }
                    contentItem: Text {
                        leftPadding: Theme.space2
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        text: row.modelData === "" ? qsTr("Same as the system") :
                                                     row.modelData.replace(/_/g, " ")
                        color: row.modelData === root.zoneId ? Theme.accent : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                        font.weight: row.modelData === root.zoneId ? Font.ExtraBold : Font.Normal
                    }
                }
            }
        }
    }
}
