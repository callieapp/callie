import Callie.Ui
import QtQuick

/// The card shown while hovering an event: when it is, which calendar, where it
/// stands now, and a hint that clicking shows more.
Rectangle {
    id: root

    property string summary
    property string when
    property string timing
    property bool hasCall: false

    width: 240
    height: column.implicitHeight + 2 * Theme.space4
    radius: Theme.radiusMd
    color: Theme.surfaceAlt
    border.color: Theme.border

    SoftShadow {
        anchors.fill: parent
        radius: root.radius
    }

    Column {
        id: column
        anchors {
            fill: parent
            margins: Theme.space4
        }
        spacing: Theme.space2

        Text {
            width: parent.width
            text: root.summary
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
            font.weight: Font.ExtraBold
        }
        Text {
            width: parent.width
            text: root.when
            elide: Text.ElideRight
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.DemiBold
        }
        Row {
            visible: root.timing !== ""
            spacing: Theme.space3

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.space3
                height: width
                radius: width / 2
                color: Theme.accent
            }
            Text {
                text: root.timing
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
                font.weight: Font.ExtraBold
            }
        }
        Text {
            width: parent.width
            text: root.hasCall ? qsTr("Click for details and to join.") : qsTr("Click for details.")
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.DemiBold
        }
    }
}
