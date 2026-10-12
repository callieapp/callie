import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A menu item in the theme's colors, for menus like the title bar's ? menu.
MenuItem {
    id: root

    /// The menu's padding around its entries. The highlight's corners follow
    /// the menu's rounded ones at that distance, so they never cross its border.
    readonly property real inset: menu ? menu.padding : 0

    implicitHeight: 30
    leftPadding: Theme.space4
    // A checkable entry keeps room for its check on the right.
    rightPadding: Theme.space4 + (root.checkable ? 14 + Theme.space2 : 0)

    contentItem: Text {
        text: root.text
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
        font.weight: root.checked ? Theme.weightBold : Font.Normal
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    indicator: Glyph {
        x: root.width - width - Theme.space4
        anchors.verticalCenter: parent.verticalCenter
        visible: root.checkable && root.checked
        name: "check"
        color: Theme.accent
    }

    background: Rectangle {
        radius: Math.max(Theme.radiusSm, Theme.radiusLg - root.inset)
        color: root.highlighted ? Theme.surfaceAlt : "transparent"
    }
}
