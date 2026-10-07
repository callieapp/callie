import Callie.Ui
import QtQuick

/// A key, or a run of keys, drawn as a little sticker.
Rectangle {
    id: root

    property string keys

    implicitWidth: Math.max(implicitHeight, label.implicitWidth + 2 * Theme.space2)
    implicitHeight: label.implicitHeight + Theme.space1 * 2
    radius: Theme.radiusSm
    color: Theme.surfaceAlt
    border.color: Theme.border

    Text {
        id: label
        anchors.centerIn: parent
        // A space is a key too, so it gets a name.
        text: root.keys === " " ? qsTr("Space") : root.keys
        textFormat: Text.PlainText
        color: Theme.text
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.textSm
        font.weight: Font.Bold
    }
}
