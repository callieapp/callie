import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A raised button that sits on its edge and presses down into it. `accent`
/// makes it pink, for the one primary action in view.
AbstractButton {
    id: root

    property string glyph
    /// Line weight of the glyph; busy glyphs such as settings read better thinner.
    property real glyphStroke: 2.2
    property bool accent: false

    readonly property color face: accent ? Theme.accent : hovered ? Theme.border : Theme.surfaceAlt
    readonly property color ink: accent ? Theme.accentText : Theme.text
    readonly property real sink: down ? Theme.stickerEdge : 0

    implicitHeight: 32
    implicitWidth: text ? label.implicitWidth + 30 + (glyph ? 20 : 0) : implicitHeight
    padding: 0
    focusPolicy: Qt.StrongFocus
    Accessible.name: text || glyph

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Item {
        Rectangle {
            anchors {
                fill: parent
                topMargin: Theme.stickerEdge
                bottomMargin: -Theme.stickerEdge
            }
            radius: Theme.radiusMd
            color: root.accent ? Theme.accentEdge : Theme.edge
        }
        Rectangle {
            width: parent.width
            height: parent.height
            y: root.sink
            radius: Theme.radiusMd
            color: root.face
            border.width: root.visualFocus ? 2 : 0
            border.color: Theme.text

            Behavior on color {
                ColorAnimation {
                    duration: Theme.durFast
                }
            }
        }
    }

    contentItem: Item {
        Row {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: root.sink
            spacing: Theme.space3

            Glyph {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.glyph !== ""
                name: root.glyph
                color: root.ink
                stroke: root.glyphStroke
            }
            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                visible: root.text !== ""
                text: root.text
                color: root.ink
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
                font.weight: Font.ExtraBold
            }
        }
    }
}
