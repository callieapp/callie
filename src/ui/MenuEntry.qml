import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A menu item in the theme's colors, for menus like the title bar's ? menu.
MenuItem {
    id: root

    /// The menu's padding around its entries. The highlight's corners follow
    /// the menu's rounded ones at that distance, so they never cross its border.
    readonly property real inset: menu ? menu.padding : 0

    /// Marks this entry as the current choice, with a check on the right. It
    /// is not `checked`, which a click would toggle out from under its binding.
    property bool current: false
    /// Keeps room for that check, so entries line up whichever is current.
    property bool marks: false

    implicitHeight: 30
    leftPadding: Theme.space4
    rightPadding: Theme.space4 + (root.marks ? 14 + Theme.space2 : 0)

    contentItem: Text {
        text: root.text
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
        font.weight: root.current ? Theme.weightBold : Font.Normal
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    indicator: Glyph {
        x: root.width - width - Theme.space4
        anchors.verticalCenter: parent.verticalCenter
        visible: root.current
        name: "check"
        color: Theme.accent
    }

    background: Rectangle {
        radius: Math.max(Theme.radiusSm, Theme.radiusLg - root.inset)
        color: root.highlighted ? Theme.surfaceAlt : "transparent"
    }
}
