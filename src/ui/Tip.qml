import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A tooltip in the theme's colors.
ToolTip {
    id: root

    contentItem: Text {
        text: root.text
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }

    background: Rectangle {
        color: Theme.surfaceAlt
        border.color: Theme.border
        radius: Theme.radiusMd
    }
}
