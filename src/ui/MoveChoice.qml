import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Asks whether a change dragged onto a repeating event is for that one
/// occurrence or the whole series.
Popup {
    id: root

    /// The change waiting for an answer, handed back with it.
    property var event
    property date from
    property date to

    signal chosen(var event, date from, date to, bool wholeSeries)
    signal cancelled

    function ask(event, from, to) {
        root.event = event
        root.from = from
        root.to = to
        open()
    }

    width: 280
    padding: Theme.space5
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // Closing any way but a choice keeps the event where it was.
    onClosed: if (event)
                  cancelled()

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
        spacing: Theme.space3

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Move this event, or every one in the series?")
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.Bold
        }
        Flow {
            width: parent.width
            spacing: Theme.space2

            StickerButton {
                text: qsTr("This event")
                onClicked: root.choose(false)
            }
            StickerButton {
                text: qsTr("All events")
                onClicked: root.choose(true)
            }
            StickerButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
        }
    }

    function choose(wholeSeries) {
        const answered = event
        event = null
        close()
        if (answered)
            chosen(answered, from, to, wholeSeries)
    }
}
