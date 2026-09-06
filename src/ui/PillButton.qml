import QtQuick
import Callie.Ui

Rectangle {
    id: root

    property string label: ""
    property bool selected: false
    signal clicked()

    implicitWidth: text.implicitWidth + Theme.space6
    implicitHeight: 30
    radius: Theme.radiusMd
    color: selected ? Theme.tint(Theme.accent, Theme.dark ? 0.24 : 0.12)
                    : hover.hovered ? Theme.surfaceAlt : "transparent"

    Behavior on color { ColorAnimation { duration: Theme.durFast } }

    Text {
        id: text
        anchors.centerIn: parent
        text: root.label
        color: root.selected ? Theme.accent : Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textMd
        font.weight: root.selected ? Font.DemiBold : Font.Normal
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.clicked() }
}
