import Callie.Ui
import QtQuick
import QtQuick.Controls

/// The newest release's notes from CHANGELOG.md, with a link to all of them.
Popup {
    id: root

    anchors.centerIn: Overlay.overlay
    width: 460
    height: Math.min(implicitHeight, (Overlay.overlay ? Overlay.overlay.height : 600) - 2
                     * Theme.space6)
    padding: Theme.space6
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle {
        // The attached Window type is not the QML Window type, so this stays untyped.
        readonly property var appWindow: Window.window

        radius: appWindow && appWindow.cornerRadius ? appWindow.cornerRadius : 0
        color: Theme.tint(Theme.shadowColor, 0.35)
    }

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
                spacing: Theme.space1

                Text {
                    text: qsTr("What's new in %1").arg(Release.notesVersion)
                    color: Theme.text
                    font.family: Theme.displayFontFamily
                    font.pixelSize: Theme.textXl
                    font.weight: Font.Bold
                }
                Text {
                    text: qsTr("Released %1").arg(Qt.formatDate(Release.notesDate, "MMMM d, yyyy"))
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.Bold
                }
            }
            StickerButton {
                anchors.right: parent.right
                glyph: "close"
                Accessible.name: qsTr("Close")
                onClicked: root.close()
            }
        }

        ScrollView {
            id: scroller
            width: parent.width
            height: Math.min(notes.implicitHeight, 360)
            clip: true
            contentWidth: availableWidth

            Text {
                id: notes
                width: scroller.availableWidth
                text: Release.notes
                textFormat: Text.MarkdownText
                wrapMode: Text.Wrap
                color: Theme.text
                linkColor: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
        }

        StickerButton {
            text: qsTr("Every version's notes")
            onClicked: Qt.openUrlExternally(Release.history)
        }
    }
}
