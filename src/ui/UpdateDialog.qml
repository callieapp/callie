import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Says a newer release is out, and how to install it: Callie comes from the
/// system's package manager, which needs a terminal for its password.
Popup {
    id: root

    anchors.centerIn: Overlay.overlay
    width: 420
    padding: Theme.space6
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Scrim {}

    enter: Transition {
        NumberAnimation {
            property: "scale"
            from: 0.94
            to: 1
            duration: Theme.durMed
            easing.type: Theme.easingBounce
            easing.overshoot: Theme.bounce
        }
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.durFast
        }
    }

    background: Rectangle {
        radius: Theme.radiusXl
        color: Theme.surface
        border.color: Theme.border

        SoftShadow {
            anchors.fill: parent
            radius: parent.radius
        }
    }

    contentItem: Column {
        spacing: Theme.space4

        Item {
            width: parent.width
            height: heading.height

            Column {
                id: heading
                width: parent.width - close.width - Theme.space3
                spacing: Theme.space1

                Text {
                    width: parent.width
                    text: qsTr("Callie %1 is out").arg(Release.update)
                    wrapMode: Text.Wrap
                    color: Theme.text
                    font.family: Theme.displayFontFamily
                    font.pixelSize: Theme.textXl
                    font.weight: Theme.weightBold
                }
                Text {
                    text: qsTr("You have %1.").arg(Release.version)
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Theme.weightBold
                }
            }
            StickerButton {
                id: close
                anchors.right: parent.right
                glyph: "close"
                Accessible.name: qsTr("Close")
                onClicked: root.close()
            }
        }

        Text {
            width: parent.width
            text: qsTr("Update it from a terminal, then open Callie again:")
            wrapMode: Text.Wrap
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
        }

        Rectangle {
            width: parent.width
            height: command.implicitHeight + 2 * Theme.space3
            radius: Theme.radiusMd
            color: Theme.surfaceAlt

            Text {
                id: command
                anchors {
                    left: parent.left
                    leftMargin: Theme.space4
                    verticalCenter: parent.verticalCenter
                }
                text: "callie upgrade"
                color: Theme.text
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.textMd
            }
            StickerButton {
                anchors {
                    right: parent.right
                    rightMargin: Theme.space2
                    verticalCenter: parent.verticalCenter
                }
                text: copied.running ? qsTr("Copied") : qsTr("Copy")
                onClicked: {
                    Support.copyText(command.text)
                    copied.restart()
                }

                Timer {
                    id: copied
                    interval: 2000
                }
            }
        }

        StickerButton {
            text: qsTr("What's new in %1").arg(Release.update)
            onClicked: Qt.openUrlExternally(Release.updatePage)
        }

        Text {
            width: parent.width
            text: qsTr("Callie looks for new releases once a day. Settings can turn that off.")
            wrapMode: Text.Wrap
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
        }
    }
}
