import Callie.Ui
import QtQuick

/// A button for what cannot be undone: a red label, then on the first click
/// it fills red and asks for a second, which it reports as `confirmed`.
StickerButton {
    id: root

    property string label
    property string armedLabel: qsTr("Click again")
    /// False when the action asks in its own way, such as a repeating event's
    /// choice of occurrences, so one click is enough.
    property bool confirms: true

    signal confirmed

    destructive: true
    text: armed ? armedLabel : label
    onClicked: {
        if (!confirms || armed) {
            armed = false
            confirmed()
        } else {
            armed = true
            disarm.restart()
        }
    }

    Timer {
        id: disarm
        interval: 4000
        onTriggered: root.armed = false
    }
}
