import Callie.Ui
import QtQuick

/// A small dot or bar in a calendar's color that marks an event in lists:
/// hollow while the invitation waits for an answer, and hollow around a
/// smaller one for "maybe".
Rectangle {
    id: root

    property color calendarColor
    property string response

    readonly property color fill: Theme.calendarColor(calendarColor, Theme.calendar)
    readonly property bool maybe: response === "tentative"
    readonly property bool pending: response === "needsAction"

    radius: width / 2
    color: maybe || pending ? "transparent" : fill
    border.width: maybe || pending ? Theme.markRing : 0
    border.color: fill

    Rectangle {
        visible: root.maybe
        anchors.centerIn: parent
        width: Math.max(1, parent.width - 4 * Theme.markRing)
        height: Math.max(1, parent.height - 4 * Theme.markRing)
        radius: width / 2
        color: root.fill
    }
}
