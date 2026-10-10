pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Who Callie is: the face, the version, a few words, and where to learn more.
Popup {
    id: root

    /// "What's new?" was asked for from here.
    signal whatsNewRequested

    anchors.centerIn: Overlay.overlay
    width: 380
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
    onOpened: logo.play()

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

        Logo {
            id: logo
            anchors.horizontalCenter: parent.horizontalCenter
            size: 80
        }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space1

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Release.devel ? qsTr("Callie Devel") : qsTr("Callie")
                color: Theme.text
                font.family: Theme.displayFontFamily
                font.pixelSize: Theme.text2xl
                font.weight: Font.Bold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Version %1").arg(Release.version)
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
                font.weight: Font.Bold
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("A cozy, open source calendar for Linux that keeps up with your Google "
                       + "calendars. Made with care, and free to use, share and change under "
                       + "the MIT license.")
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
        }

        StickerButton {
            anchors.horizontalCenter: parent.horizontalCenter
            accent: true
            text: qsTr("What's new in %1").arg(Release.notesVersion)
            visible: Release.notes !== ""
            onClicked: {
                root.close()
                root.whatsNewRequested()
            }
        }

        Flow {
            width: parent.width
            spacing: Theme.space2

            Repeater {
                model: [
                    {
                        "label": qsTr("Website"),
                        "url": Release.website
                    },
                    {
                        "label": qsTr("Privacy"),
                        "url": Release.privacy
                    },
                    {
                        "label": qsTr("Terms"),
                        "url": Release.terms
                    },
                    {
                        "label": qsTr("Source code"),
                        "url": Release.source
                    }
                ]

                PillButton {
                    id: link
                    required property var modelData
                    label: link.modelData.label
                    onClicked: Qt.openUrlExternally(link.modelData.url)
                }
            }
        }
    }
}
