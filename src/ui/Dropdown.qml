pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A choice of one from a list, shown as a button with the current choice that
/// opens a menu of the others. `model` is a list of objects with `textRole`
/// and `valueRole` fields.
PickerButton {
    id: root

    property var model: []
    property string textRole: "name"
    property string valueRole: "id"
    /// The value of the current choice.
    property var currentValue
    /// The narrowest the button gets, whatever its choices.
    property real minimumWidth: 220
    /// The widest it gets; a longer choice is cut short.
    property real maximumWidth: Infinity

    signal activated(var value)

    readonly property var current: {
        for (const choice of root.model) {
            if (choice[root.valueRole] === root.currentValue)
                return choice
        }
        return null
    }

    text: root.current ? root.current[root.textRole] : ""
    open: menu.opened
    implicitWidth: Math.min(root.maximumWidth, Math.max(root.minimumWidth, root.fullWidth))
    onClicked: menu.popup(root, 0, root.height + Theme.space1)

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
