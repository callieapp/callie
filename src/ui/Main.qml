pragma ComponentBehavior: Bound
import Callie.Ui

import QtQuick
import QtQuick.Controls
import QtQuick.Effects

ApplicationWindow {
    id: window

    width: 1280
    height: 840
    minimumWidth: 880
    minimumHeight: 560
    visible: true
    title: qsTr("Callie")
    // Transparent where the rounded frame leaves its corners.
    color: frame.rounded ? "transparent" : Theme.bg
    // Callie draws its own title bar; see the title bar below and ResizeFrame.
    flags: Qt.Window | Qt.FramelessWindowHint

    /// How round the window's corners are, for overlays drawn outside the frame.
    readonly property int cornerRadius: frame.rounded ? frameMask.radius : 0

    /// Where events come from; set by main.cpp.
    required property CalendarSource source

    /// The day the views are on; each shows the range around it. Set once
    /// rather than bound to the clock, which would pull it back on every tick.
    property date focusDate
    readonly property string view: Settings.view

    /// The first day the current view shows, and how many days it shows.
    readonly property date rangeStart: Views.start(view, focusDate, Settings.firstDayOfWeek)
    readonly property int rangeDays: Views.days(view, focusDate, Settings.firstDayOfWeek)

    function today() {
        return Settings.times.date(Clock.now)
    }

    Component.onCompleted: focusDate = today()

    /// Moves a view's worth of days: a day, a week, a month or the agenda's span.
    function step(n) {
        focusDate = Views.step(view, focusDate, n)
    }

    function showDay(day) {
        focusDate = day
        Settings.view = "day"
    }

    // The event a reminder asked to see, shown once its day has loaded.
    property string revealUid
    property date revealStart

    function reveal() {
        if (revealUid === "" || !weekLoader.item)
            return
        const row = events.rowOf(revealUid, revealStart)
        if (row < 0)
            return
        const view = weekLoader.item as WeekView
        revealUid = ""
        view.showRow(row)
    }

    /// Shows the event with this uid starting at `start` on its day, with its details.
    function revealEvent(day, uid, start) {
        window.show()
        window.raise()
        window.requestActivate()
        window.revealUid = uid
        window.revealStart = start
        window.showDay(day)
        window.reveal()
    }

    Connections {
        target: Reminders
        function onOpenRequested(day, uid, start) {
            window.revealEvent(day, uid, start)
        }
    }

    InvitesModel {
        id: invites
        source: window.source
        now: Clock.now
        hiddenCalendars: Settings.hiddenCalendars
    }

    Connections {
        target: events
        function onRevisionChanged() {
            window.reveal()
        }
    }

    EventModel {
        id: events
        source: window.source
        rangeStart: window.rangeStart
        dayCount: window.rangeDays
        timeZoneId: Settings.timeZoneId
        use24Hour: Settings.use24Hour
        showDeclined: Settings.showDeclined
        hiddenCalendars: Settings.hiddenCalendars
        minimumMinutes: Theme.minEventMinutes
    }

    TodayModel {
        id: todayModel
        source: window.source
        now: Clock.now
        timeZoneId: Settings.timeZoneId
        use24Hour: Settings.use24Hour
        hiddenCalendars: Settings.hiddenCalendars
    }

    MonthModel {
        id: monthModel
        month: window.focusDate
        firstDay: Settings.firstDayOfWeek
        // Only the week view is a run of seven days to mark.
        weekStart: window.view === "week" ? window.rangeStart : new Date(NaN)
        today: Settings.times.date(Clock.now)
    }

    // Everything the window shows, with its corners rounded unless it fills
    // the screen.
    Item {
        id: frame

        // The software renderer has no shader effects, so it keeps square corners.
        readonly property bool rounded: window.visibility === Window.Windowed && GraphicsInfo.api
                                        !== GraphicsInfo.Software

        anchors.fill: parent
        layer.enabled: rounded
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: frameMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.bg
        }

        // ---- Title bar, which is also the toolbar ----------------------------
        Rectangle {
            id: titleBar

            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
            }
            height: 58
            color: Theme.surface

            // Behind the controls: dragging the bar moves the window, double-clicking
            // maximizes it, as a desktop title bar would.
            DragHandler {
                target: null
                onActiveChanged: {
                    if (active)
                        window.startSystemMove()
                }
            }
            TapHandler {
                onDoubleTapped: window.visibility === Window.Maximized ? window.showNormal() :
                                                                         window.showMaximized()
            }

            Rectangle {
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                height: 1
                color: Theme.border
            }

            Row {
                id: leading
                anchors {
                    left: parent.left
                    verticalCenter: parent.verticalCenter
                    leftMargin: WindowButtons.left.length > 0 ? Theme.space4 : Theme.space5
                }
                spacing: Theme.space4

                WindowControls {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: buttons.length > 0
                    buttons: WindowButtons.left
                }

                Logo {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -Theme.logoLift
                }

                // As wide as the longest month name in the user's language, so the
                // arrows stay put while paging.
                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: monthNames.implicitWidth + Theme.space3 + yearWidth.advanceWidth
                    height: month.implicitHeight

                    Column {
                        id: monthNames
                        visible: false

                        // Both forms, since some languages inflect a month inside a date.
                        Repeater {
                            model: 24

                            Text {
                                required property int index
                                text: index < 12 ? Qt.locale().standaloneMonthName(index,
                                                                                   Locale.LongFormat) :
                                                   Qt.locale().monthName(index - 12,
                                                                         Locale.LongFormat)
                                font: month.font
                            }
                        }
                    }
                    TextMetrics {
                        id: yearWidth
                        font: year.font
                        text: "0000"
                    }

                    Text {
                        id: month
                        text: Qt.formatDate(window.view === "week" ? window.rangeStart :
                                                                     window.focusDate, "MMMM")
                        color: Theme.text
                        font.family: Theme.displayFontFamily
                        font.pixelSize: Theme.textDisplay
                        font.weight: Font.Bold
                    }
                    Text {
                        id: year
                        anchors {
                            left: month.right
                            leftMargin: Theme.space3
                            baseline: month.baseline
                        }
                        text: Qt.formatDate(window.view === "week" ? window.rangeStart :
                                                                     window.focusDate, "yyyy")
                        color: Theme.textMuted
                        font.family: Theme.displayFontFamily
                        font.pixelSize: Theme.textDisplay
                        font.weight: Font.DemiBold
                    }
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space2

                    StickerButton {
                        glyph: "chevron-left"
                        Accessible.name: ({
                                              "day": qsTr("Previous day"),
                                              "week": qsTr("Previous week"),
                                              "month": qsTr("Previous month"),
                                              "agenda": qsTr("Previous %n days", "",
                                                             window.rangeDays)
                                          })[window.view]
                        onClicked: window.step(-1)
                    }
                    StickerButton {
                        glyph: "chevron-right"
                        Accessible.name: ({
                                              "day": qsTr("Next day"),
                                              "week": qsTr("Next week"),
                                              "month": qsTr("Next month"),
                                              "agenda": qsTr("Next %n days", "", window.rangeDays)
                                          })[window.view]
                        onClicked: window.step(1)
                    }
                }

                StickerButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Today")
                    onClicked: window.focusDate = window.today()
                }

                // The one primary action in view, so it is the pink one.
                StickerButton {
                    id: newButton
                    anchors.verticalCenter: parent.verticalCenter
                    accent: true
                    glyph: "plus"
                    text: qsTr("New")
                    Accessible.name: qsTr("New event")
                    onClicked: quickAdd.open()

                    QuickAddPopup {
                        id: quickAdd
                        y: newButton.height + Theme.space3
                        source: window.source
                        onClosed: {
                            // The New button opens it below itself; a drawn time opens it beside.
                            x = 0
                            y = newButton.height + Theme.space3
                            const view = weekLoader.item as WeekView
                            if (view)
                                view.clearDraft()
                        }
                    }
                }
            }

            Row {
                id: trailing
                anchors {
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    rightMargin: Theme.space4
                }
                spacing: Theme.space4

                // Invitations waiting for an answer, with their count.
                AbstractButton {
                    id: invitesButton
                    anchors.verticalCenter: parent.verticalCenter
                    // As tall as the sync chip beside it.
                    width: syncStatus.height
                    height: syncStatus.height
                    Accessible.name: invites.count > 0 ? qsTr("%1 invitations waiting").arg(
                                                             invites.count) : qsTr("Invitations")
                    onClicked: invitesTray.opened ? invitesTray.close() : invitesTray.open()

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    background: Rectangle {
                        radius: height / 2
                        color: invitesButton.hovered ? Theme.surfaceAlt : "transparent"
                        border.width: invitesButton.visualFocus ? 2 : 0
                        border.color: Theme.text
                    }
                    contentItem: Item {
                        Glyph {
                            anchors.centerIn: parent
                            width: 15
                            height: 15
                            stroke: Theme.fineGlyphStroke
                            name: "bell"
                            color: invites.count > 0 ? Theme.text : Theme.textFaint
                        }
                        Rectangle {
                            visible: invites.count > 0
                            x: parent.width - width + 2
                            y: -2
                            width: Math.max(height, badgeText.implicitWidth + Theme.space2)
                            height: Theme.textXs + Theme.space1 * 2
                            radius: height / 2
                            color: Theme.accent

                            Text {
                                id: badgeText
                                anchors.centerIn: parent
                                text: invites.count > 99 ? "99+" : invites.count
                                color: Theme.accentText
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textXs
                                font.weight: Font.ExtraBold
                            }
                        }
                    }

                    InvitesTray {
                        id: invitesTray
                        y: invitesButton.height + Theme.space3
                        x: invitesButton.width - width
                        model: invites
                        source: window.source
                        onJumpRequested: (day, uid, start) => window.revealEvent(day, uid, start)
                    }
                }

                // Sync now; it turns while a sync runs.
                AbstractButton {
                    id: refreshButton
                    anchors.verticalCenter: parent.verticalCenter
                    visible: syncStatus.visible
                    width: syncStatus.height
                    height: syncStatus.height
                    enabled: !window.source.syncing
                    Accessible.name: qsTr("Sync now")
                    onClicked: window.source.refresh()

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    background: Rectangle {
                        radius: height / 2
                        color: refreshButton.hovered ? Theme.tint(syncStatus.ink, 0.28) : Theme.tint(
                                                           syncStatus.ink, 0.16)
                        border.width: refreshButton.visualFocus ? 2 : 0
                        border.color: Theme.text
                    }
                    contentItem: Item {
                        Glyph {
                            id: refreshGlyph
                            anchors.centerIn: parent
                            width: 13
                            height: 13
                            stroke: Theme.fineGlyphStroke
                            name: "refresh"
                            color: syncStatus.ink

                            RotationAnimator on rotation {
                                running: window.source.syncing
                                from: 0
                                to: 360
                                duration: Theme.durSlow * 4
                                loops: Animation.Infinite
                                onRunningChanged: {
                                    if (!running)
                                        refreshGlyph.rotation = 0
                                }
                            }
                        }
                    }
                }

                // Sync status. When the bar is too narrow for the words it shrinks to
                // its icon, and below that it hides. Hovering it tells more.
                Rectangle {
                    id: syncStatus

                    readonly property bool failed: window.source.lastError !== "" &&
                                                   !window.source.syncing

                    readonly property bool known: window.source.syncing || failed || waiting > 0 ||
                                                  !isNaN(window.source.lastSynced.getTime())
                    // Pink is kept for today and actions, so a healthy sync stays neutral.
                    readonly property color ink: failed ? Theme.danger : window.source.syncing
                                                          ? Theme.textFaint : Theme.textMuted
                    readonly property int waiting: window.source.waitingChanges.length
                    readonly property string label: {
                        if (window.source.syncing)
                            return qsTr("Syncing...")
                        if (failed)
                            return qsTr("Sync failed")
                        if (waiting > 0)
                            return waiting === 1 ? qsTr("1 change to send") : qsTr(
                                                       "%1 changes to send").arg(waiting)
                        return qsTr("Updated %1").arg(Settings.times.time(window.source.lastSynced))
                    }
                    // Room between the left side and the rest of the right side.
                    readonly property real spare: titleBar.width - leading.x - leading.width
                                                  - trailingFixed.width - height - 4 * Theme.space4
                    readonly property real fullWidth: labelMetrics.advanceWidth + 2 * Theme.space4
                                                      + (check.visible ? check.width + Theme.space3 :
                                                                         0)
                    readonly property bool compact: spare < fullWidth + Theme.space4

                    /// The hover text: the status, then a line or more per account.
                    function details() {
                        const lines = [label]
                        let explained = false
                        for (const entry of window.source.syncReport) {
                            const when = isNaN(entry.lastSynced.getTime()) ? qsTr("not synced yet") :
                                                                             qsTr("synced %1").arg(
                                                                                 Settings.times.time(
                                                                                     entry.lastSynced))
                            lines.push(qsTr("%1: %2").arg(entry.account).arg(when))
                            if (entry.error)
                                lines.push("    " + entry.error)
                            for (const problem of entry.problems)
                                lines.push("    " + problem)
                            explained = explained || entry.error !== "" || entry.problems.length > 0
                        }
                        // The cache may not hold the reason, if writing it failed too.
                        if (failed && !explained)
                            lines.push(window.source.lastError)
                        if (waiting > 0) {
                            lines.push(qsTr("Waiting to be sent:"))
                            for (const change of window.source.waitingChanges)
                                lines.push("    " + change)
                        }
                        return lines.join("\n")
                    }

                    anchors.verticalCenter: parent.verticalCenter
                    visible: known && spare >= height + Theme.space4
                    Accessible.role: Accessible.StaticText
                    Accessible.name: failed ? label + ": " + window.source.lastError : label
                    height: 28
                    width: compact ? height : fullWidth
                    radius: height / 2
                    color: Theme.tint(ink, 0.16)

                    TextMetrics {
                        id: labelMetrics
                        font: syncText.font
                        text: syncStatus.label
                    }

                    Row {
                        anchors.centerIn: parent
                        spacing: Theme.space3

                        Glyph {
                            id: check
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !window.source.syncing && !syncStatus.failed
                            name: "check"
                            width: 12
                            height: 12
                            stroke: 1.8
                            color: syncStatus.ink
                        }
                        Text {
                            id: syncText
                            anchors.verticalCenter: parent.verticalCenter
                            // Compact keeps a mark for the states without a check.
                            visible: !syncStatus.compact || !check.visible
                            text: !syncStatus.compact ? syncStatus.label : syncStatus.failed ? "!" :
                                                                                               "..."

                            color: syncStatus.ink
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSm
                            font.weight: Font.ExtraBold
                        }
                    }

                    HoverHandler {
                        id: syncHover
                    }
                    Tip {
                        visible: syncHover.hovered
                        text: syncStatus.details()
                    }
                }

                Row {
                    id: trailingFixed
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space4

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.space2

                        Repeater {
                            model: [
                                {
                                    "id": "day",
                                    "label": qsTr("Day")
                                },
                                {
                                    "id": "week",
                                    "label": qsTr("Week")
                                },
                                {
                                    "id": "month",
                                    "label": qsTr("Month")
                                },
                                {
                                    "id": "agenda",
                                    "label": qsTr("Agenda")
                                }
                            ]

                            PillButton {
                                required property var modelData
                                label: modelData.label
                                selected: window.view === modelData.id
                                onClicked: Settings.view = modelData.id
                            }
                        }
                    }

                    StickerButton {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: "search"
                        glyphStroke: Theme.fineGlyphStroke
                        Accessible.name: qsTr("Search events")
                        onClicked: searchPopup.open()
                    }

                    StickerButton {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: "settings"
                        glyphStroke: Theme.fineGlyphStroke
                        Accessible.name: qsTr("Settings")
                        onClicked: settingsDialog.open()
                    }

                    // Help: debug info, logs and bug reports.
                    StickerButton {
                        id: helpButton
                        anchors.verticalCenter: parent.verticalCenter
                        text: "?"
                        Accessible.name: qsTr("Help")
                        onClicked: helpMenu.popup(helpButton, 0, helpButton.height + Theme.space3)

                        Tip {
                            visible: copiedTip.running
                            text: qsTr("Debug info copied")
                        }

                        Timer {
                            id: copiedTip
                            interval: 2000
                        }

                        Connections {
                            target: Support
                            function onCopied() {
                                copiedTip.restart()
                            }
                        }

                        Menu {
                            id: helpMenu
                            padding: Theme.space2

                            background: Rectangle {
                                implicitWidth: 200
                                color: Theme.surface
                                border.color: Theme.border
                                radius: Theme.radiusLg

                                SoftShadow {
                                    anchors.fill: parent
                                    radius: parent.radius
                                }
                            }

                            MenuEntry {
                                text: qsTr("Copy debug info")
                                onTriggered: Support.copyDebugInfo()
                            }
                            MenuEntry {
                                text: qsTr("Open logs folder")
                                onTriggered: Support.openLogs()
                            }
                            MenuEntry {
                                text: qsTr("Report a bug...")
                                onTriggered: Support.reportBug()
                            }
                            MenuSeparator {
                                contentItem: Rectangle {
                                    implicitHeight: 1
                                    color: Theme.border
                                }
                            }
                            MenuEntry {
                                text: qsTr("What's new")
                                onTriggered: whatsNew.open()
                            }
                            MenuEntry {
                                text: qsTr("About Callie")
                                onTriggered: about.open()
                            }
                            // Closing the window leaves Callie running then, so
                            // this is the way out.
                            MenuEntry {
                                text: qsTr("Quit Callie")
                                visible: Settings.keepRunning
                                height: visible ? implicitHeight : 0
                                onTriggered: Qt.quit()
                            }
                        }
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: WindowButtons.right.length > 0
                        width: 1
                        height: 26
                        color: Theme.border
                    }

                    WindowControls {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: buttons.length > 0
                        buttons: WindowButtons.right
                    }
                }
            }
        }

        // ---- Sidebar + grid ----------------------------------------------------
        Row {
            anchors {
                left: parent.left
                right: parent.right
                top: titleBar.bottom
                bottom: parent.bottom
            }
            spacing: 0

            Sidebar {
                width: Theme.sidebarWidth
                height: parent.height
                today: todayModel
                month: monthModel
                events: events
                onDayPicked: day => window.focusDate = day
            }

            Item {
                width: parent.width - Theme.sidebarWidth
                height: parent.height

                // Only the view on screen exists, so hidden ones cost nothing.
                Loader {
                    id: weekLoader
                    anchors.fill: parent
                    active: window.view === "day" || window.view === "week"
                    onLoaded: window.reveal()
                    sourceComponent: Component {
                        WeekView {
                            model: events
                            anchorDate: window.rangeStart
                            dayCount: window.view === "day" ? 1 : 7
                            onDayClicked: day => window.showDay(day)
                            onRangeDrawn: (day, from, to, block) => quickAdd.openFor(
                                                                        Settings.times.at(day, from),
                                                                        Settings.times.at(day, to),
                                                                        block)
                        }
                    }
                }
                Loader {
                    anchors.fill: parent
                    active: window.view === "month"
                    sourceComponent: Component {
                        MonthView {
                            model: events
                            rangeStart: window.rangeStart
                            dayCount: window.rangeDays
                            month: window.focusDate.getMonth()
                            onDayClicked: day => window.showDay(day)
                        }
                    }
                }
                Loader {
                    anchors.fill: parent
                    active: window.view === "agenda"
                    sourceComponent: Component {
                        AgendaView {
                            model: events
                            rangeStart: window.rangeStart
                            dayCount: window.rangeDays
                        }
                    }
                }
            }
        }
    }

    AboutDialog {
        id: about
        onWhatsNewRequested: whatsNew.open()
    }

    WhatsNewDialog {
        id: whatsNew
    }

    // The first start of a new version says what changed, once.
    Timer {
        running: true
        interval: 600
        onTriggered: {
            if (Release.takeUpdateNotice())
                whatsNew.open()
        }
    }

    SettingsDialog {
        id: settingsDialog
        syncReport: window.source.syncReport
        source: window.source
        onEditColorsRequested: {
            close()
            themeEditor.open()
        }
    }

    ThemeEditor {
        id: themeEditor
        onFinished: settingsDialog.open()
    }

    Shortcut {
        sequences: [StandardKey.New]
        onActivated: quickAdd.open()
    }

    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: searchPopup.open()
    }

    SearchPopup {
        id: searchPopup
        source: window.source
        onJumpRequested: (day, uid, start) => window.revealEvent(day, uid, start)
    }

    Shortcut {
        sequences: [StandardKey.Quit]
        onActivated: Qt.quit()
    }

    Shortcut {
        sequences: [StandardKey.Preferences, "Ctrl+,"]
        onActivated: settingsDialog.open()
    }

    Rectangle {
        id: frameMask
        anchors.fill: frame
        radius: Theme.radiusMd
        visible: false
        layer.enabled: true
    }

    ResizeFrame {
        anchors.fill: parent
        z: 1000
    }
}
