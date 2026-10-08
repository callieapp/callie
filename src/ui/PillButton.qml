import Callie.Ui
import QtQuick
import QtQuick.Controls

/// One choice in a group such as the view switcher: plain text until selected,
/// then a pink sticker.
AbstractButton {
    id: root

    property string label: ""
    property bool selected: false
    /// Behind the label on hover, for places already on surfaceAlt.
    property color hoverColor: Theme.surfaceAlt

    text: label
    implicitHeight: 32
    implicitWidth: text.length > 0 ? caption.implicitWidth + 26 : 32
    padding: 0
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.RadioButton
    Accessible.checked: selected

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Item {
        Rectangle {
            visible: root.selected
            anchors {
                fill: parent
                topMargin: Theme.stickerEdge
                bottomMargin: -Theme.stickerEdge
            }
            radius: Theme.radiusMd
            color: Theme.accentEdge
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            // Clear is the hover color with no alpha: "transparent" is clear black,
            // which the fade would pass through as a dark flash.
            color: root.selected ? Theme.accent : Theme.tint(root.hoverColor, root.hovered ? 1 : 0)
            border.width: root.visualFocus ? 2 : 0
            border.color: Theme.text

            Behavior on color {
                ColorAnimation {
                    duration: Theme.durFast
                }
            }
        }
    }

    contentItem: Text {
        id: caption
        text: root.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: root.selected ? Theme.accentText : root.hovered ? Theme.text : Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textMd
        font.weight: root.selected ? Font.ExtraBold : Font.Bold
    }
}
