import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A labelled on/off switch: a pill track whose knob slides to the accent.
AbstractButton {
    id: root

    checkable: true
    implicitHeight: 32
    implicitWidth: caption.implicitWidth + track.width + Theme.space4
    padding: 0
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.CheckBox
    Accessible.checked: checked
    Accessible.name: text

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        Text {
            id: caption
            anchors {
                left: parent.left
                right: track.left
                rightMargin: Theme.space4
                verticalCenter: parent.verticalCenter
            }
            text: root.text
            wrapMode: Text.Wrap
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
            font.weight: Font.DemiBold
        }

        Rectangle {
            id: track
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            width: 38
            height: 22
            radius: height / 2
            color: root.checked ? Theme.accent : Theme.surfaceAlt
            border.width: root.visualFocus ? 2 : 1
            border.color: root.visualFocus ? Theme.text : root.checked ? Theme.accentEdge :
                                                                         Theme.border

            Behavior on color {
                ColorAnimation {
                    duration: Theme.durFast
                }
            }

            Rectangle {
                width: parent.height - 6
                height: width
                radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                x: root.checked ? parent.width - width - 3 : 3
                color: root.checked ? Theme.accentText : Theme.textMuted

                Behavior on x {
                    NumberAnimation {
                        duration: Theme.durMed
                        easing.type: Theme.easingBounce
                        easing.overshoot: Theme.bounce
                    }
                }
            }
        }
    }
}
