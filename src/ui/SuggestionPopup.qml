pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Suggestions for a field, in a card under it rather than in the form, so
/// the form does not move as they come and go. The field keeps the focus and
/// the keys; this shows what it is offering and takes clicks.
Popup {
    id: root

    /// What to offer: {text, detail, strong}, detail and strong optional.
    property var items: []
    property int highlighted: -1
    /// Shown while more may come, such as while asking a server.
    property bool busy: false
    property string busyText
    /// A line under the suggestions, such as where they came from.
    property string footer
    /// Whether the field wants suggestions shown, such as while it has focus.
    property bool wanted: true

    signal picked(int index)

    x: 0
    y: parent ? parent.height + Theme.space1 : 0
    width: parent ? parent.width : 0
    padding: Theme.space1
    margins: Theme.space3
    // The field keeps the focus, and with it Escape and the arrows.
    focus: false
    closePolicy: Popup.NoAutoClose
    visible: wanted && (items.length > 0 || busy)

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
        radius: Theme.radiusLg

        SoftShadow {
            anchors.fill: parent
            radius: parent.radius
        }
    }

    contentItem: Column {
        Repeater {
            model: root.items

            Rectangle {
                id: row

                required property var modelData
                required property int index

                width: parent.width
                height: Theme.listRowHeight + Theme.space2
                radius: Theme.radiusMd
                color: index === root.highlighted || pointer.hovered ? Theme.surfaceAlt :
                                                                       "transparent"
                Accessible.role: Accessible.Button
                Accessible.name: modelData.detail ? qsTr("%1, %2").arg(modelData.text).arg(
                                                        modelData.detail) : modelData.text

                HoverHandler {
                    id: pointer
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    // Holds the press, so it cannot reach the view under the card.
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: root.picked(row.index)
                }

                Row {
                    anchors {
                        left: parent.left
                        right: parent.right
                        leftMargin: Theme.space3
                        rightMargin: Theme.space3
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: Theme.space2

                    Text {
                        width: Math.min(implicitWidth, parent.width)
                        elide: Text.ElideRight
                        text: row.modelData.text
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                        font.weight: row.modelData.strong ? Font.Bold : Font.Normal
                    }
                    Text {
                        visible: text !== ""
                        width: parent.width - x
                        elide: Text.ElideRight
                        text: row.modelData.detail || ""
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                    }
                }
            }
        }

        // Still looking, or where the suggestions came from.
        Row {
            visible: root.busy || root.footer !== ""
            height: Theme.listRowHeight
            leftPadding: Theme.space3
            spacing: Theme.space2

            Glyph {
                visible: root.busy
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.smallGlyphSize
                height: width
                name: "refresh"
                stroke: Theme.smallGlyphStroke
                color: Theme.textFaint

                // A turn every three slow beats; still when the theme turns motion off.
                RotationAnimator on rotation {
                    running: root.busy && root.visible && Theme.durSlow > 0
                    from: 0
                    to: 360
                    duration: Theme.durSlow * 3
                    loops: Animation.Infinite
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.busy ? root.busyText : root.footer
                color: Theme.textFaint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textXs
            }
        }
    }
}
