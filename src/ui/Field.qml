import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A one-line text box in the theme's colors.
TextField {
    id: root

    placeholderTextColor: Theme.textFaint
    color: Theme.text
    selectionColor: Theme.tint(Theme.accent, 0.4)
    selectedTextColor: Theme.text
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textBase
    font.weight: Font.DemiBold
    leftPadding: Theme.space4
    rightPadding: Theme.space4
    topPadding: Theme.space3
    bottomPadding: Theme.space3

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.surfaceAlt
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.accent : Theme.border
    }
}
