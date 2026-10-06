pragma ComponentBehavior: Bound
import Callie.Ui

import QtQuick
import QtQuick.Controls

Item {
    id: root

    required property EventModel model

    /// A day's heading was clicked, to look at that day on its own.
    signal dayClicked(date day)
    /// A time was drawn on the grid for a new event: minutes into `day`, with
    /// the drawn block to open the composer beside.
    signal rangeDrawn(date day, int fromMinutes, int toMinutes, Item block)

    /// Forgets the drawn block, once its composer has closed.
    function clearDraft() {
        drawer.column = -1
    }
    property date anchorDate: Clock.now
    property int dayCount: 7

    readonly property int todayColumn: {
        for (let i = 0; i < dayCount; ++i)
            if (isToday(dateForColumn(i)))
                return i
        return -1
    }
    // Today's column can take a larger share of the width.
    readonly property real todayShare: Settings.widenToday && todayColumn >= 0 ? Theme.todayShare :
                                                                                 1
    /// The width of an ordinary day column.
    readonly property real dayWidth: (width - Theme.gutterWidth) / (dayCount - 1 + todayShare)

    function columnX(i) {
        const wider = todayColumn >= 0 && i > todayColumn ? (todayShare - 1) * dayWidth : 0
        return Theme.gutterWidth + i * dayWidth + wider
    }

    function columnWidth(i) {
        return i === todayColumn ? todayShare * dayWidth : dayWidth
    }

    function dateForColumn(i) {
        const d = new Date(root.anchorDate)
        d.setDate(d.getDate() + i)
        return d
    }

    function isToday(d) {
        // Today in the chosen zone, which can differ from the system's.
        const now = Settings.times.date(Clock.now)
        if (d.getFullYear() !== now.getFullYear())
            return false
        if (d.getMonth() !== now.getMonth())
            return false
        return d.getDate() === now.getDate()
    }

    // ---- Day header --------------------------------------------------------
    Item {
        id: header
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 64

        Row {
            anchors.fill: parent
            leftPadding: Theme.gutterWidth

            Repeater {
                model: root.dayCount

                Item {
                    id: dayHeader
                    required property int index
                    readonly property date date: root.dateForColumn(index)
                    readonly property bool today: root.isToday(date)

                    width: root.columnWidth(index)
                    height: header.height

                    TapHandler {
                        onTapped: root.dayClicked(dayHeader.date)
                    }
                    HoverHandler {
                        cursorShape: root.dayCount > 1 ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.space1

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Qt.formatDate(dayHeader.date, "ddd").toUpperCase()
                            color: dayHeader.today ? Theme.accent : Theme.textFaint
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textXs
                            font.weight: Font.ExtraBold
                            font.letterSpacing: 0.8
                        }

                        // Today's date is an accent sticker; the rest are plain.
                        Item {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 33
                            height: 33

                            Rectangle {
                                visible: dayHeader.today
                                anchors {
                                    fill: parent
                                    topMargin: Theme.stickerEdge
                                    bottomMargin: -Theme.stickerEdge
                                }
                                radius: Theme.radiusMd
                                color: Theme.accentEdge
                            }
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusMd
                                color: dayHeader.today ? Theme.accent : "transparent"
                            }
                            Text {
                                anchors.centerIn: parent
                                text: dayHeader.date.getDate()
                                color: dayHeader.today ? Theme.accentText : Theme.text
                                font.family: Theme.displayFontFamily
                                font.pixelSize: Theme.textDate
                                font.weight: Font.Bold
                                font.features: {
                                    "tnum": 1
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- All-day events ----------------------------------------------------
    Item {
        id: allDayStrip

        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
        }
        // A thin strip when empty, so the grid still starts below a hairline.
        height: root.model.allDayRows > 0 ? root.model.allDayRows * Theme.allDayRowHeight + Theme.space2 :
                                            Theme.space4
        clip: true

        Repeater {
            model: root.model

            Rectangle {
                id: chip
                required property string summary
                required property bool allDay
                required property int firstDay
                required property int daySpan
                required property int lane
                required property color calendarColor
                required property bool declined
                required property date end

                visible: allDay
                opacity: declined || (Settings.dimPast && end < Clock.now) ? Theme.fadedOpacity : 1
                layer.enabled: opacity < 1
                x: root.columnX(firstDay) + 3
                y: Theme.space1 + lane * Theme.allDayRowHeight
                width: root.columnX(firstDay + daySpan) - root.columnX(firstDay) - 6
                height: Theme.allDayRowHeight - 3 - Theme.stickerEdge
                radius: height / 2
                color: Theme.calendarColor(calendarColor, Theme.calendar)

                Rectangle {
                    z: -1
                    anchors {
                        fill: parent
                        topMargin: Theme.stickerEdge
                        bottomMargin: -Theme.stickerEdge
                    }
                    radius: parent.radius
                    color: Theme.calendarEdge(chip.calendarColor, Theme.calendar)
                }

                Text {
                    anchors {
                        fill: parent
                        leftMargin: Theme.space3
                        rightMargin: Theme.space2
                    }
                    verticalAlignment: Text.AlignVCenter
                    text: chip.summary
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.calendarInk(chip.calendarColor, Theme.calendar)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.ExtraBold
                    font.strikeout: chip.declined
                }
            }
        }

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            color: Theme.hairline
        }
    }

    // ---- Scrollable time grid ---------------------------------------------
    Flickable {
        id: grid
        anchors {
            top: allDayStrip.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        contentHeight: 24 * Theme.hourHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // Open on the working day, a little above 8:00 so its label shows.
        Component.onCompleted: contentY = 8 * Theme.hourHeight - Theme.space4

        // Inset so the window's resize strip does not cover the handle.
        ScrollBar.vertical: ScrollBar {
            rightPadding: Theme.resizeBorder + Theme.space1
        }

        Item {
            width: grid.width
            height: grid.contentHeight

            // Hour lines + gutter labels
            Repeater {
                model: 24

                Item {
                    id: hourRow
                    required property int index
                    y: index * Theme.hourHeight
                    width: grid.width
                    height: Theme.hourHeight

                    Rectangle {
                        anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                        }
                        anchors.leftMargin: Theme.gutterWidth
                        height: 1
                        color: Theme.hairline
                    }

                    Text {
                        anchors {
                            right: parent.left
                            top: parent.top
                        }
                        anchors.rightMargin: -Theme.gutterWidth + Theme.space3
                        anchors.topMargin: -7
                        visible: hourRow.index > 0
                        text: Settings.times.hour(hourRow.index)
                        color: Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textXs
                        font.weight: Font.Bold
                        font.features: {
                            "tnum": 1
                        }
                    }
                }
            }

            // Day columns: a hairline on the left, a wash on today and on days off.
            Repeater {
                model: root.dayCount

                Rectangle {
                    id: column
                    required property int index
                    readonly property date date: root.dateForColumn(index)
                    readonly property bool dayOff: root.model.isDayOff(date)

                    x: root.columnX(index)
                    width: root.columnWidth(index)
                    height: grid.contentHeight
                    color: root.isToday(date) ? Theme.todayWash : dayOff ? Theme.dayOffWash :
                                                                           "transparent"

                    Rectangle {
                        width: 1
                        height: parent.height
                        color: Theme.hairline
                    }
                }
            }

            // Dragging across empty time draws a new event, in snapped steps.
            MouseArea {
                id: drawer

                property int column: -1
                /// The slot first pressed, which the drawn range always keeps.
                property int pressedMinute: 0
                property int fromMinutes: 0
                property int toMinutes: 0
                /// Only a real drag draws; a plain click does nothing.
                property bool dragged: false
                readonly property int firstMinute: Math.min(fromMinutes, toMinutes)
                readonly property int lastMinute: Math.max(fromMinutes, toMinutes)

                function minutesAt(y) {
                    const snapped = Math.round(y / Theme.hourHeight * 60 / Theme.snapMinutes)
                          * Theme.snapMinutes
                    return Math.max(0, Math.min(24 * 60, snapped))
                }

                function columnAt(x) {
                    for (let i = root.dayCount - 1; i >= 0; --i)
                        if (x >= root.columnX(i))
                            return i
                    return 0
                }

                x: Theme.gutterWidth
                width: root.columnX(root.dayCount) - Theme.gutterWidth
                height: grid.contentHeight
                // Keeps the drag from scrolling the grid instead; the wheel still scrolls.
                preventStealing: true
                cursorShape: Qt.CrossCursor

                onPressed: mouse => {
                    dragged = false
                    pressedMinute = Math.floor(mouse.y / Theme.hourHeight * 60 / Theme.snapMinutes)
                            * Theme.snapMinutes
                    fromMinutes = pressedMinute
                    toMinutes = pressedMinute + Theme.snapMinutes
                    column = -1
                    pressColumn = columnAt(mouse.x + x)
                    pressY = mouse.y
                }
                onPositionChanged: mouse => {
                    if (!dragged && Math.abs(mouse.y - pressY) < Theme.hourHeight / 8)
                        return
                    dragged = true
                    column = pressColumn
                    const at = minutesAt(mouse.y)
                    // Downward from the pressed slot, or upward to include it.
                    if (at > pressedMinute) {
                        fromMinutes = pressedMinute
                        toMinutes = Math.max(at, pressedMinute + Theme.snapMinutes)
                    } else {
                        fromMinutes = at
                        toMinutes = pressedMinute + Theme.snapMinutes
                    }
                }
                onReleased: {
                    if (dragged && column >= 0)
                        root.rangeDrawn(root.dateForColumn(column), firstMinute, lastMinute, draft)
                    else
                        column = -1
                }

                property int pressColumn: -1
                property real pressY: 0
            }

            // The new event being drawn, until its composer closes.
            Rectangle {
                id: draft

                readonly property date day: root.dateForColumn(Math.max(0, drawer.column))

                visible: drawer.column >= 0
                z: 60
                x: root.columnX(Math.max(0, drawer.column)) + 3
                y: drawer.firstMinute / 60 * Theme.hourHeight
                width: root.columnWidth(Math.max(0, drawer.column)) - 6
                height: (drawer.lastMinute - drawer.firstMinute) / 60 * Theme.hourHeight - 2
                radius: Theme.radiusMd
                color: Theme.tint(Theme.accent, 0.35)
                border.width: 2
                border.color: Theme.accent

                Text {
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: Theme.space2
                    }
                    text: qsTr("%1 to %2").arg(Settings.times.time(Settings.times.at(draft.day,
                                                                                     drawer.firstMinute))).arg(
                              Settings.times.time(Settings.times.at(draft.day, drawer.lastMinute)))
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textXs
                    font.weight: Font.ExtraBold
                }
            }

            // Events
            Repeater {
                model: root.model

                EventBlock {
                    id: block

                    required property int index
                    required property string uid
                    required property int dayIndex
                    required property int startMinutes
                    required property int durationMinutes
                    required property int lane
                    required property int laneCount
                    required property bool allDay

                    readonly property real laneWidth: (root.columnWidth(dayIndex) - 6) / laneCount

                    visible: !allDay && dayIndex >= 0 && dayIndex < root.dayCount

                    x: root.columnX(dayIndex) + 3 + lane * laneWidth
                    width: laneWidth - (laneCount > 1 ? 3 : 0)
                    y: startMinutes / 60 * Theme.hourHeight
                    height: Math.max(Theme.minEventHeight, durationMinutes / 60 * Theme.hourHeight
                                     - 2 - Theme.stickerEdge)

                    onActivated: root.showDetails(block)
                    onHoveredChanged: root.blockHovered(block, hovered)

                    onMoveRequested: (deltaMinutes, deltaDays) => {
                        // TODO(core): commit the move once the model can write.
                        console.log("move", uid, deltaMinutes, "min", deltaDays, "days")
                    }
                }
            }

            // The current time across the whole week, faint beside today's line.
            Rectangle {
                visible: now.visible
                x: Theme.gutterWidth
                y: now.y + now.height / 2 - height / 2
                width: root.columnX(root.dayCount) - Theme.gutterWidth
                height: 1
                z: 49
                color: Theme.tint(Theme.accent, 0.45)
            }

            // Now indicator: an accent line across today, with the time in the gutter.
            Item {
                id: now
                readonly property date current: Clock.now
                readonly property int columnIndex: root.todayColumn

                visible: columnIndex >= 0
                y: Settings.times.minutesIntoDay(current) / 60 * Theme.hourHeight
                x: root.columnX(columnIndex) + 2
                width: root.columnWidth(columnIndex) - 4
                height: 3
                z: 50

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: Theme.accent
                }

                Rectangle {
                    anchors {
                        verticalCenter: parent.verticalCenter
                        horizontalCenter: parent.left
                    }
                    width: 13
                    height: 13
                    radius: width / 2
                    color: Theme.accent
                    border.width: 3
                    border.color: Theme.bg
                }
            }

            Rectangle {
                visible: now.visible
                z: 50
                x: Theme.space1
                y: now.y + now.height / 2 - height / 2
                width: nowLabel.implicitWidth + 12
                height: nowLabel.implicitHeight + 2
                radius: height / 2
                color: Theme.accent

                Text {
                    id: nowLabel
                    anchors.centerIn: parent
                    text: Settings.times.time(now.current)
                    color: Theme.accentText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textXs
                    font.weight: Font.ExtraBold
                    font.features: {
                        "tnum": 1
                    }
                }
            }
        }
    }

    // ---- Event tooltip and details -----------------------------------------
    function whenText(block) {
        return qsTr("%1 to %2").arg(Settings.times.time(block.start)).arg(Settings.times.time(
                                                                              block.end))
    }

    // Beside the event, on whichever side has room, kept inside the view.
    function placeBeside(block, popup) {
        const gap = Theme.space3
        const right = block.mapToItem(root, block.width + gap, 0)
        const left = block.mapToItem(root, -gap - popup.width, 0)
        popup.x = right.x + popup.width <= root.width ? right.x : Math.max(0, left.x)
        popup.y = Math.min(Math.max(0, right.y), root.height - popup.height - gap)
    }

    property EventBlock hoveredBlock: null

    // A sync rebuilds the blocks, which clears this without a hover change.
    onHoveredBlockChanged: {
        if (!hoveredBlock) {
            tipDelay.stop()
            tip.visible = false
        }
    }

    function blockHovered(block, hovered) {
        if (hovered) {
            hoveredBlock = block
            tipDelay.restart()
        } else if (hoveredBlock === block) {
            hoveredBlock = null
            tipDelay.stop()
            tip.visible = false
        }
    }

    function showDetails(block) {
        tipDelay.stop()
        tip.visible = false
        details.showNear(root.model.eventAt(block.index), root.model.callService(
                             block.conferenceUrl), block)
    }

    /// Opens the details of the event in `row`, centered, for when there is no
    /// block under the pointer to place them by.
    function showRow(row) {
        const event = root.model.eventAt(row)
        details.show(event, root.model.callService(event.conferenceUrl))
        details.x = (root.width - details.width) / 2
        details.y = Theme.space7
    }

    Timer {
        id: tipDelay
        interval: 450
        onTriggered: {
            const block = root.hoveredBlock
            if (!block || details.opened)
                return
            tip.summary = block.summary
            tip.when = block.calendarName ? qsTr("%1, %2").arg(root.whenText(block)).arg(
                                                block.calendarName) : root.whenText(block)
            tip.timing = root.model.timing(block.start, block.end, Clock.now)
            tip.hasCall = root.model.callService(block.conferenceUrl) !== ""
            root.placeBeside(block, tip)
            tip.visible = true
        }
    }

    EventTip {
        id: tip
        visible: false
        z: 200
    }

    EventDetails {
        id: details
        source: root.model.source
    }
}
