pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Edits the colors of one of the user's themes. Each change saves to the
/// theme file and shows at once, so the window beside it is the preview.
Popup {
    id: root

    /// Done was pressed.
    signal finished

    parent: Overlay.overlay
    x: parent ? parent.width - width - Theme.space6 : 0
    y: parent ? (parent.height - height) / 2 : 0
    width: 400
    height: Math.min(640, (parent ? parent.height : 640) - 2 * Theme.space6)
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Item {}

    background: Rectangle {
        radius: Theme.radiusXl
        color: Theme.surface
        border.color: Theme.border

        SoftShadow {
            anchors.fill: parent
            radius: parent.radius
        }
    }

    contentItem: Item {
        Item {
            id: header
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                margins: Theme.space6
            }
            height: title.implicitHeight

            Text {
                id: title
                text: qsTr("Theme colors")
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
                accent: true
                text: qsTr("Done")
                onClicked: {
                    root.close()
                    root.finished()
                }
            }
        }

        Text {
            id: hint
            anchors {
                top: header.bottom
                topMargin: Theme.space3
                left: parent.left
                right: parent.right
                leftMargin: Theme.space6
                rightMargin: Theme.space6
            }
            text: qsTr("Changes save as you go and show in the calendar beside.")
            wrapMode: Text.Wrap
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
        }

        ListView {
            anchors {
                top: hint.bottom
                topMargin: Theme.space4
                left: parent.left
                right: parent.right
                bottom: error.top
                leftMargin: Theme.space6
                rightMargin: Theme.space4
                bottomMargin: Theme.space3
            }
            clip: true
            spacing: Theme.space2
            boundsBehavior: Flickable.StopAtBounds
            model: Themes.colorKeys
            ScrollBar.vertical: ScrollBar {}

            delegate: Row {
                id: row

                required property string modelData
                readonly property color value: Themes.colors[modelData]

                spacing: Theme.space3

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: 28
                    radius: Theme.radiusSm
                    color: row.value
                    border.color: Theme.border
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 150
                    text: row.modelData.replace(/-/g, " ")
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textMd
                    font.weight: Font.DemiBold
                }
                TextField {
                    id: hex
                    anchors.verticalCenter: parent.verticalCenter
                    width: 120
                    text: String(row.value)
                    selectByMouse: true
                    color: Theme.text
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.textMd
                    Accessible.name: qsTr("%1 color").arg(row.modelData)
                    validator: RegularExpressionValidator {
                        regularExpression: /#?[0-9a-fA-F]{0,6}/
                    }
                    onEditingFinished: {
                        const value = text.startsWith("#") ? text : "#" + text
                        if (/^#[0-9a-fA-F]{6}$/.test(value))
                            Themes.setColor(row.modelData, value)
                        else
                            // Rebound, so it keeps following the theme afterwards.
                            text = Qt.binding(() => String(row.value))
                    }
                    background: Rectangle {
                        radius: Theme.radiusMd
                        color: Theme.surfaceAlt
                        border.color: hex.activeFocus ? Theme.accent : Theme.border
                    }
                }
            }
        }

        Text {
            id: error
            anchors {
                bottom: parent.bottom
                left: parent.left
                right: parent.right
                margins: Theme.space6
            }
            visible: Themes.error !== ""
            height: visible ? implicitHeight : 0
            text: Themes.error
            wrapMode: Text.Wrap
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
        }
    }
}
