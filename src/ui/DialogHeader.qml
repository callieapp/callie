import Callie.Ui
import QtQuick

/// A popup's title, with an optional line under it, and the X that closes it.
Item {
    id: root

    property string title
    property string subtitle
    property int titleSize: Theme.textXl
    /// The display face for a dialog's title; small popups ask in the text face.
    property bool display: true

    signal closeRequested

    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(texts.implicitHeight, close.implicitHeight)

    Column {
        id: texts
        width: parent.width - close.width - Theme.space3
        spacing: Theme.space1

        Text {
            width: parent.width
            text: root.title
            wrapMode: Text.Wrap
            color: Theme.text
            font.family: root.display ? Theme.displayFontFamily : Theme.fontFamily
            font.pixelSize: root.titleSize
            font.weight: Theme.weightBold
        }
        Text {
            visible: root.subtitle !== ""
            width: parent.width
            elide: Text.ElideMiddle
            text: root.subtitle
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
        }
    }
    CloseButton {
        id: close
        anchors.right: parent.right
        onClicked: root.closeRequested()
    }
}
