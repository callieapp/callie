pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Renames a calendar or an account in Callie, and recolors a calendar. The
/// changes stay on this computer; Google keeps its own names and colors.
Popup {
    id: root

    /// A calendar's id, or an account's when `account` is set.
    property string targetId
    property bool account: false
    property string name

    readonly property string chosenColor: Settings.calendarLooks[targetId] ? (
                                                                                 Settings.calendarLooks[targetId].color
                                                                                 || "") : ""

    function editCalendar(id, name) {
        account = false
        targetId = id
        root.name = name
        open()
    }

    function editAccount(id) {
        account = true
        targetId = id
        root.name = Settings.accountName(id)
        open()
    }

    // Only a name the user typed is kept, so an unchanged one keeps following
    // the calendar's own if that is renamed later.
    function saveName() {
        if (field.text.trim() === name.trim())
            return
        if (account)
            Settings.setAccountName(targetId, field.text)
        else
            Settings.setCalendarName(targetId, field.text)
    }

    width: 300
    padding: Theme.space5
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        field.text = name
        field.selectAll()
        field.forceActiveFocus()
    }
    // Leaving the box any way keeps what was typed.
    onClosed: saveName()

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

        Text {
            text: root.account ? qsTr("Account name") : qsTr("Calendar name")
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textXs
            font.weight: Font.ExtraBold
            font.letterSpacing: 0.6
        }
        Field {
            id: field
            width: parent.width
            placeholderText: root.account ? root.targetId : qsTr("Its own name")
            Accessible.name: root.account ? qsTr("Account name") : qsTr("Calendar name")
            onAccepted: root.close()
        }

        Grid {
            visible: !root.account
            columns: 6
            spacing: Theme.space2

            Repeater {
                model: Theme.calendarPalette

                AbstractButton {
                    id: swatch

                    required property string modelData
                    readonly property bool chosen: root.chosenColor === modelData

                    width: Theme.space7
                    height: Theme.space7
                    Accessible.name: qsTr("Color %1").arg(modelData)
                    Accessible.checked: chosen
                    onClicked: Settings.setCalendarColor(root.targetId, modelData)

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    background: Rectangle {
                        radius: Theme.radiusMd
                        color: Theme.calendarColor(swatch.modelData, Theme.calendar)
                        border.width: swatch.chosen || swatch.visualFocus ? 2 : 0
                        border.color: Theme.text
                    }
                    contentItem: Item {
                        Glyph {
                            anchors.centerIn: parent
                            visible: swatch.chosen
                            name: "check"
                            color: Theme.calendarInk(swatch.modelData, Theme.calendar)
                        }
                    }
                }
            }
        }

        StickerButton {
            text: root.account ? qsTr("Use the account's own name") : qsTr(
                                     "Use the calendar's own name and color")
            onClicked: {
                field.text = ""
                if (root.account)
                    Settings.setAccountName(root.targetId, "")
                else
                    Settings.resetCalendarLook(root.targetId)
                root.close()
            }
        }
    }
}
