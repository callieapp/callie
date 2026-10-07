import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A menu item in the theme's colors, for menus like the title bar's ? menu.
MenuItem {
    id: root

    /// The menu's padding around its entries. The highlight's corners follow
    /// the menu's rounded ones at that distance, so they never cross its border.
    property int inset: Theme.space2

    implicitHeight: 30
    leftPadding: Theme.space4
    rightPadding: Theme.space4

    contentItem: Text {
        text: root.text
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: Math.max(Theme.radiusSm, Theme.radiusLg - root.inset)
        color: root.highlighted ? Theme.surfaceAlt : "transparent"
    }
}
