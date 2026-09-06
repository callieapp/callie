import QtQuick
import Callie.Ui

Rectangle {
    id: root

    property string glyph: ""
    signal clicked()

    width: 28
    height: 28
    radius: Theme.radiusMd
    color: hover.hovered ? Theme.surfaceAlt : "transparent"

    Behavior on color { ColorAnimation { duration: Theme.durFast } }

    Text {
        anchors.centerIn: parent
        text: root.glyph
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textXl
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.clicked() }
}
