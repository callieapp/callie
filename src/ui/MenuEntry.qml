import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A menu item in the theme's colors, for menus like the title bar's ? menu.
MenuItem {
    id: root

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
        radius: Theme.radiusSm
        color: root.highlighted ? Theme.surfaceAlt : "transparent"
    }
}
