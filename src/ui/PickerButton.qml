import Callie.Ui
import QtQuick
import QtQuick.Controls

/// The face of a control that opens a list to choose from: the current
/// choice, cut short when it does not fit, and a caret.
AbstractButton {
    id: root

    /// Whether its list is open, which keeps the button shaded.
    property bool open
    /// The width that shows all of `text`.
    readonly property real fullWidth: caption.implicitWidth + chevron.width + Theme.space2
                                      + leftPadding + rightPadding

    implicitHeight: Theme.listRowHeight + Theme.space2
    leftPadding: Theme.space3
    rightPadding: Theme.space3
    Accessible.role: Accessible.ComboBox
    Accessible.name: text

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: root.hovered || root.open ? Theme.border : Theme.surfaceAlt
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.text
    }
    contentItem: Item {
        Text {
            id: caption
            anchors.left: parent.left
            anchors.right: chevron.left
            anchors.rightMargin: Theme.space2
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: root.text
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
            font.weight: Theme.weightBold
        }
        Glyph {
            id: chevron
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-down"
            color: Theme.textMuted
            width: 12
            height: 12
        }
    }
}
