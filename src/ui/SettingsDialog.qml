pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

/// Callie's preferences. Every change applies and saves at once, so there is
/// nothing to confirm; reset puts everything back.
Popup {
    id: root

    anchors.centerIn: Overlay.overlay
    width: 480
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// The user wants to edit the theme's colors, which needs the calendar in view.
    signal editColorsRequested

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
        padding: Theme.space6
        spacing: Theme.space5

        Item {
            width: parent.width - 2 * parent.padding
            height: title.implicitHeight

            Text {
                id: title
                text: qsTr("Settings")
                color: Theme.text
                font.family: Theme.displayFontFamily
                font.pixelSize: Theme.textXl
                font.weight: Font.Bold
            }

            StickerButton {
                anchors {
                    right: parent.right
                    verticalCenter: title.verticalCenter
                }
                glyph: "close"
                Accessible.name: qsTr("Close")
                onClicked: root.close()
            }
        }

        Section {
            title: qsTr("Appearance")

            Flow {
                width: parent.width
                spacing: Theme.space2

                Repeater {
                    model: Themes.available

                    PillButton {
                        required property var modelData
                        label: modelData.name
                        selected: Themes.current === modelData.id
                        onClicked: Themes.use(modelData.id)
                    }
                }
            }

            Flow {
                width: parent.width
                spacing: Theme.space2

                StickerButton {
                    text: Themes.editable ? qsTr("Edit colors") : qsTr("Customize colors")
                    onClicked: {
                        if (Themes.customize())
                            root.editColorsRequested()
                    }
                }
                StickerButton {
                    text: qsTr("Import...")
                    onClicked: importDialog.open()
                }
                StickerButton {
                    text: qsTr("Export...")
                    onClicked: exportDialog.open()
                }
                // Deleting takes a second click, since the file is gone for good.
                StickerButton {
                    id: deleteTheme
                    property bool armed: false
                    visible: Themes.editable
                    text: armed ? qsTr("Click again to delete") : qsTr("Delete theme")
                    onClicked: {
                        if (armed) {
                            armed = false
                            Themes.removeCurrent()
                        } else {
                            armed = true
                            disarm.restart()
                        }
                    }

                    Timer {
                        id: disarm
                        interval: 4000
                        onTriggered: deleteTheme.armed = false
                    }
                }
            }

            Text {
                visible: Themes.error !== ""
                width: parent.width
                text: Themes.error
                wrapMode: Text.Wrap
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
            }
        }

        Section {
            title: qsTr("Time")

            Row {
                spacing: Theme.space2
                Accessible.role: Accessible.RadioButton

                Repeater {
                    model: [
                        {
                            "label": qsTr("Like my region"),
                            "format": Settings.Locale
                        },
                        {
                            "label": qsTr("24-hour"),
                            "format": Settings.TwentyFourHour
                        },
                        {
                            "label": qsTr("AM/PM"),
                            "format": Settings.TwelveHour
                        }
                    ]

                    PillButton {
                        required property var modelData
                        label: modelData.label
                        selected: Settings.timeFormat === modelData.format
                        onClicked: Settings.timeFormat = modelData.format
                    }
                }
            }

            Row {
                // Clears the raised edge of the selected clock button above.
                topPadding: Theme.space3
                spacing: Theme.space4

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Time zone")
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textMd
                    font.weight: Font.DemiBold
                }
                ZonePicker {
                    zoneId: Settings.timeZoneId
                    onPicked: id => Settings.timeZoneId = id
                }
            }
        }

        Section {
            title: qsTr("Events")

            Toggle {
                width: parent.width
                text: qsTr("Show events I declined")
                checked: Settings.showDeclined
                onToggled: Settings.showDeclined = checked
            }
            Toggle {
                width: parent.width
                text: qsTr("Fade events that have ended")
                checked: Settings.dimPast
                onToggled: Settings.dimPast = checked
            }
        }

        Section {
            title: qsTr("Week")

            Toggle {
                width: parent.width
                text: qsTr("Give today more room")
                checked: Settings.widenToday
                onToggled: Settings.widenToday = checked
            }
        }

        StickerButton {
            text: qsTr("Reset to defaults")
            // Settings forget the theme too, so the default one comes back.
            onClicked: {
                Settings.reset()
                Themes.useDefault()
            }
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import a theme")
        nameFilters: [qsTr("Callie themes (*.toml)")]
        onAccepted: Themes.importFrom(selectedFile)
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export this theme")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "toml"
        nameFilters: [qsTr("Callie themes (*.toml)")]
        onAccepted: Themes.exportTo(selectedFile)
    }

    /// A small heading over a group of related settings.
    component Section: Column {
        id: section

        property string title
        default property alias content: rows.data

        width: 480 - 2 * Theme.space6
        spacing: Theme.space3

        Text {
            text: section.title.toUpperCase()
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textXs
            font.weight: Font.ExtraBold
            font.letterSpacing: 0.6
        }
        Column {
            id: rows
            width: parent.width
            spacing: Theme.space3
        }
    }
}
