import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Asks before subscribing to a calendar link a browser handed over.
Popup {
    id: root

    property string link

    anchors.centerIn: Overlay.overlay
    width: 420
    padding: Theme.space6
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Scrim {}

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
            width: parent.width
            text: qsTr("Subscribe to this calendar?")
            color: Theme.text
            font.family: Theme.displayFontFamily
            font.pixelSize: Theme.textXl
            font.weight: Theme.weightBold
        }
        Text {
            width: parent.width
            elide: Text.ElideMiddle
            text: root.link
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr(
                      "Callie keeps a copy that you can see but not change, and fetches it again every hour.")
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
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
        Row {
            anchors.right: parent.right
            spacing: Theme.space3

            StickerButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            StickerButton {
                accent: true
                glyph: "plus"
                text: qsTr("Subscribe")
                onClicked: {
                    if (Subscribed.add(root.link) !== "")
                        root.close()
                }
            }
        }
    }
}
