import Callie.Ui
import QtQuick
import QtQuick.Controls

/// The card that opens when an event is clicked: a header in the event's
/// calendar colors, a join button for calls, then where, which calendar and
/// the notes. Escape or a click outside closes it.
Popup {
    id: root

    property string summary
    property string when
    property string location
    property url conferenceUrl
    /// From EventModel.callService: empty when the link should not be opened.
    property string callService
    property string calendarName
    property color calendarColor
    property string description

    readonly property color fill: Theme.calendarColor(calendarColor, Theme.calendar)
    readonly property color ink: Theme.calendarInk(calendarColor, Theme.calendar)
    /// Fills the card from one of EventModel.eventsOn()'s maps and opens it.
    function show(event, service) {
        const day = Qt.formatDate(Settings.times.date(event.start), "dddd, MMMM d")
        summary = event.summary
        when = event.allDay ? day : qsTr("%1, %2").arg(day).arg(qsTr("%1 to %2").arg(Settings.times.time(
                                                                                         event.start)).arg(
                                                                    Settings.times.time(event.end)))
        location = event.location
        conferenceUrl = event.conferenceUrl
        callService = service
        calendarName = event.calendarName
        calendarColor = event.calendarColor
        description = event.description
        open()
    }

    /// show(), then places the card by `item`: to its right if there is room,
    /// else to its left, else below it, always inside the parent view.
    function showNear(event, service, item) {
        show(event, service)
        const area = parent
        const gap = Theme.space3
        const right = item.mapToItem(area, item.width + gap, 0)
        const left = item.mapToItem(area, -gap - width, 0)
        if (right.x + width <= area.width || left.x >= 0) {
            x = right.x + width <= area.width ? right.x : left.x
            y = Math.min(Math.max(0, right.y), area.height - height - gap)
            return
        }
        const below = item.mapToItem(area, 0, item.height + gap)
        x = Math.min(Math.max(0, below.x + Theme.space7), area.width - width - gap)
        y = below.y + height <= area.height ? below.y : Math.max(0, below.y - item.height - height
                                                                 - 2 * gap)
    }

    readonly property bool hasCall: callService !== ""
    readonly property string callName: callService === "zoom" ? qsTr("Join Zoom call") :
                                                                callService === "meet" ? qsTr(
                                                                                             "Join Google Meet") :
                                                                                         qsTr("Join call")

    width: 320
    // Keeps the whole card inside the window, wherever its event sits.
    margins: Theme.space3
    padding: 0
    modal: false
    // Dims the calendar without blocking it, so a click elsewhere still lands.
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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

    Overlay.modeless: Rectangle {
        // The attached Window type is not the QML Window type, so this stays untyped.
        readonly property var appWindow: Window.window

        // Follows the window's rounded corners rather than filling them in.
        radius: appWindow && appWindow.cornerRadius ? appWindow.cornerRadius : 0
        color: Theme.tint(Theme.shadowColor, 0.35)

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.durFast
            }
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
        // Header: the event's own colors.
        Rectangle {
            width: root.width
            height: header.implicitHeight + 2 * Theme.space4
            topLeftRadius: Theme.radiusXl
            topRightRadius: Theme.radiusXl
            color: root.fill

            Row {
                id: header
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.space5
                    rightMargin: Theme.space4
                }
                spacing: Theme.space3

                Column {
                    width: header.width - closeButton.width - header.spacing
                    spacing: Theme.space1

                    Text {
                        width: parent.width
                        text: root.summary
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        color: root.ink
                        font.family: Theme.displayFontFamily
                        font.pixelSize: Theme.textXl
                        font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width
                        text: root.when
                        wrapMode: Text.Wrap
                        color: root.ink
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textMd
                        font.weight: Font.Bold
                    }
                }

                AbstractButton {
                    id: closeButton
                    width: 30
                    height: 30
                    Accessible.name: qsTr("Close")
                    onClicked: root.close()

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: closeButton.hovered ? Theme.tint(root.ink, 0.12) : "transparent"
                    }
                    contentItem: Item {
                        Glyph {
                            anchors.centerIn: parent
                            width: 12
                            height: 12
                            stroke: 1.8
                            name: "close"
                            color: root.ink
                        }
                    }
                }
            }
        }

        Column {
            width: root.width
            padding: Theme.space5
            spacing: Theme.space4

            StickerButton {
                visible: root.hasCall
                width: parent.width - 2 * parent.padding
                height: 40
                accent: true
                glyph: "video"
                glyphStroke: 1.8
                text: root.callName
                onClicked: Qt.openUrlExternally(root.conferenceUrl)
            }

            Detail {
                width: parent.width - 2 * parent.padding
                label: qsTr("Where")
                value: root.location
            }
            Detail {
                width: parent.width - 2 * parent.padding
                label: qsTr("Calendar")
                value: root.calendarName
            }
            Detail {
                width: parent.width - 2 * parent.padding
                label: qsTr("Notes")
                value: root.description
            }
        }
    }

    component Detail: Row {
        id: detail

        property string label
        property string value

        visible: value !== ""
        spacing: Theme.space4

        Text {
            width: 64
            text: detail.label
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
            font.weight: Font.ExtraBold
        }
        Text {
            width: detail.width - 64 - detail.spacing
            text: detail.value
            wrapMode: Text.Wrap
            maximumLineCount: 8
            elide: Text.ElideRight
            textFormat: Text.PlainText
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
            font.weight: Font.DemiBold
        }
    }
}
