pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A choice of one from a list, shown as a button with the current choice that
/// opens a menu of the others. `model` is a list of objects with `textRole`
/// and `valueRole` fields.
AbstractButton {
    id: root

    property var model: []
    property string textRole: "name"
    property string valueRole: "id"
    /// The value of the current choice.
    property var currentValue
    /// The narrowest the button gets, whatever its choices.
    property real minimumWidth: 220

    signal activated(var value)

    readonly property var current: {
        for (const choice of root.model) {
            if (choice[root.valueRole] === root.currentValue)
                return choice
        }
        return null
    }

    implicitHeight: Theme.listRowHeight + Theme.space2
    implicitWidth: Math.max(root.minimumWidth, caption.implicitWidth + chevron.width + Theme.space2
                            + 2 * Theme.space3)
    leftPadding: Theme.space3
    rightPadding: Theme.space3
    Accessible.role: Accessible.ComboBox
    Accessible.name: caption.text
    onClicked: menu.popup(root, 0, root.height + Theme.space1)

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: root.hovered || menu.opened ? Theme.border : Theme.surfaceAlt
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
            text: root.current ? root.current[root.textRole] : ""
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

    MenuCard {
        id: menu
        minimumWidth: root.width

        Repeater {
            model: root.model

            MenuEntry {
                required property var modelData
                text: modelData[root.textRole]
                marks: true
                current: modelData[root.valueRole] === root.currentValue
                onTriggered: root.activated(modelData[root.valueRole])
            }
        }
    }
}
