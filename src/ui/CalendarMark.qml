import Callie.Ui
import QtQuick

/// A small dot or bar in a calendar's color that marks an event in lists,
/// left hollow when the user answered "maybe".
Rectangle {
    id: root

    property color calendarColor
    property string response

    readonly property color fill: Theme.calendarColor(calendarColor, Theme.calendar)
    readonly property bool maybe: response === "tentative"

    radius: width / 2
    color: maybe ? "transparent" : fill
    border.width: maybe ? Theme.markRing : 0
    border.color: fill
}
