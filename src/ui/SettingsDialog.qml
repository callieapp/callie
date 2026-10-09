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
    // Wide enough for every tab on one row.
    width: 600
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// The user wants to edit the theme's colors, which needs the calendar in view.
    signal editColorsRequested

    /// Which group of settings shows: "general", "appearance" or "accounts".
    property string tab: "general"
    /// The source's syncReport, for the accounts tab.
    property var syncReport: []
    /// Where the calendars new events can go come from.
    property CalendarSource source

    // Lists the calendars that take new events, as quick add offers them.
    Composer {
        id: newEvents
        source: root.source
    }

    Overlay.modal: Scrim {}

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

        // Wraps onto a second line rather than running off a narrow dialog.
        Flow {
            width: root.width - 2 * Theme.space6
            spacing: Theme.space2

            Repeater {
                model: [
                    {
                        "id": "general",
                        "label": qsTr("General")
                    },
                    {
                        "id": "week",
                        "label": qsTr("Week")
                    },
                    {
                        "id": "reminders",
                        "label": qsTr("Reminders")
                    },
                    {
                        "id": "appearance",
                        "label": qsTr("Appearance")
                    },
                    {
                        "id": "keyboard",
                        "label": qsTr("Keyboard")
                    },
                    {
                        "id": "accounts",
                        "label": qsTr("Accounts")
                    },
                    {
                        "id": "developer",
                        "label": qsTr("Developer")
                    }
                ]

                PillButton {
                    id: tabButton
                    required property var modelData
                    label: tabButton.modelData.label
                    selected: root.tab === tabButton.modelData.id
                    onClicked: root.tab = tabButton.modelData.id
                }
            }
        }

        AccountsSection {
            visible: root.tab === "accounts"
            width: root.width - 2 * Theme.space6
            report: root.syncReport
        }

        Section {
            title: qsTr("Appearance")
            visible: root.tab === "appearance"

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
                DangerButton {
                    visible: Themes.editable
                    label: qsTr("Delete theme")
                    armedLabel: qsTr("Click again to delete")
                    onConfirmed: Themes.removeCurrent()
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
            visible: root.tab === "general"

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
            visible: root.tab === "general"

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
            title: qsTr("New events")
            visible: root.tab === "general" && newEvents.calendars.length > 1

            Text {
                text: qsTr("Start them in")
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
                font.weight: Font.DemiBold
            }
            Flow {
                width: parent.width
                spacing: Theme.space2
                Accessible.role: Accessible.RadioButton

                // Also lit when the chosen calendar is gone or hidden, since new
                // events then go to the last one used.
                PillButton {
                    label: qsTr("The last one used")
                    selected: !newEvents.calendars.some(c => c.id === Settings.defaultCalendar)
                    onClicked: Settings.defaultCalendar = ""
                }
                Repeater {
                    model: newEvents.calendars

                    PillButton {
                        required property var modelData
                        label: modelData.name
                        selected: Settings.defaultCalendar === modelData.id
                        onClicked: Settings.defaultCalendar = modelData.id
                    }
                }
            }
        }

        Section {
            title: qsTr("Week")
            visible: root.tab === "week"

            Text {
                text: qsTr("Starts on")
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
                font.weight: Font.DemiBold
            }
            Flow {
                width: parent.width
                spacing: Theme.space2
                Accessible.role: Accessible.RadioButton

                Repeater {
                    model: [
                        {
                            "label": qsTr("Like my region"),
                            "day": 0
                        },
                        {
                            "label": Qt.locale().dayName(1),
                            "day": 1
                        },
                        {
                            "label": Qt.locale().dayName(0),
                            "day": 7
                        },
                        {
                            "label": Qt.locale().dayName(6),
                            "day": 6
                        }
                    ]

                    PillButton {
                        required property var modelData
                        label: modelData.label
                        selected: Settings.weekStart === modelData.day
                        onClicked: Settings.weekStart = modelData.day
                    }
                }
            }
            Toggle {
                width: parent.width
                text: qsTr("Hide weekends")
                checked: Settings.hideWeekends
                onToggled: Settings.hideWeekends = checked
            }
            Toggle {
                width: parent.width
                text: qsTr("Show week numbers")
                checked: Settings.weekNumbers
                onToggled: Settings.weekNumbers = checked
            }
            Toggle {
                width: parent.width
                text: qsTr("Give today more room")
                checked: Settings.widenToday
                onToggled: Settings.widenToday = checked
            }
        }

        Section {
            title: qsTr("Developer")
            visible: root.tab === "developer"

            Toggle {
                width: parent.width
                text: qsTr("Developer mode")
                checked: Settings.developerMode
                onToggled: Settings.developerMode = checked
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr(
                          "Shows ids on event cards and in the calendar menu, a Copy as JSON button on each event, and the raw repeat rule in the editor.")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
            }
            Toggle {
                width: parent.width
                text: qsTr("Log debug details")
                checked: Settings.verboseLogging
                onToggled: Settings.verboseLogging = checked
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr(
                          "Writes Callie's debug lines to the log. They can include event titles, so turn it off again before sharing logs widely.")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
            }
        }

        Section {
            title: qsTr("Folders")
            visible: root.tab === "developer"

            Flow {
                width: parent.width
                spacing: Theme.space2

                StickerButton {
                    text: qsTr("Settings")
                    onClicked: Support.openFolder("config")
                }
                StickerButton {
                    text: qsTr("Waiting changes")
                    onClicked: Support.openFolder("data")
                }
                StickerButton {
                    text: qsTr("Cache")
                    onClicked: Support.openFolder("cache")
                }
                StickerButton {
                    text: qsTr("Logs")
                    onClicked: Support.openLogs()
                }
            }
        }

        Section {
            title: qsTr("Cached events")
            visible: root.tab === "developer" && Accounts.accounts.length > 0

            Repeater {
                model: Accounts.accounts

                Item {
                    id: cached
                    required property var modelData
                    width: parent.width
                    height: resync.height

                    Text {
                        anchors {
                            left: parent.left
                            right: resync.left
                            rightMargin: Theme.space3
                            verticalCenter: parent.verticalCenter
                        }
                        text: cached.modelData.id
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textMd
                    }
                    // Forgets every cached event of the account, so it asks twice.
                    DangerButton {
                        id: resync
                        anchors.right: parent.right
                        enabled: !Accounts.busy
                        label: qsTr("Forget and resync")
                        armedLabel: qsTr("Click again to resync")
                        onConfirmed: Accounts.resync(cached.modelData.id)
                    }
                }
            }
        }

        Section {
            title: qsTr("Vi mode")
            visible: root.tab === "keyboard"

            Toggle {
                width: parent.width
                text: qsTr("Move around and act with single keys")
                checked: Settings.viMode
                onToggled: Settings.viMode = checked
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr(
                          "j and k scroll, h and l step back and forward, t goes to today, / searches. The leader key starts a command; ? lists them all.")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
            }
            Row {
                enabled: Settings.viMode
                opacity: enabled ? 1 : Theme.fadedOpacity
                spacing: Theme.space2

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Leader")
                    color: Theme.textFaint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.ExtraBold
                }
                Repeater {
                    model: [
                        {
                            "key": ",",
                            "label": ","
                        },
                        {
                            "key": ":",
                            "label": ":"
                        },
                        {
                            "key": " ",
                            "label": qsTr("Space")
                        }
                    ]

                    PillButton {
                        required property var modelData
                        label: modelData.label
                        selected: Settings.leaderKey === modelData.key
                        onClicked: Settings.leaderKey = modelData.key
                    }
                }
            }
            Row {
                enabled: Settings.viMode
                opacity: enabled ? 1 : Theme.fadedOpacity
                spacing: Theme.space2

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Wait for the next key")
                    color: Theme.textFaint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSm
                    font.weight: Font.ExtraBold
                }
                StickerButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: "minimize"
                    Accessible.name: qsTr("Wait half a second less")
                    enabled: Settings.leaderTimeout > 0
                    onClicked: Settings.leaderTimeout = Math.max(0, Settings.leaderTimeout - 500)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 90
                    horizontalAlignment: Text.AlignHCenter
                    text: Settings.leaderTimeout === 0 ? qsTr("until Esc") : qsTr("%1 s").arg(Number(
                                                                                                  Settings.leaderTimeout
                                                                                                  / 1000).toLocaleString(
                                                                                                  Qt.locale(
                                                                                                      ), "f", 1))
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textMd
                    font.weight: Font.Bold
                }
                StickerButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: "plus"
                    Accessible.name: qsTr("Wait half a second more")
                    onClicked: Settings.leaderTimeout += 500
                }
            }
        }

        Section {
            title: qsTr("Working hours")
            visible: root.tab === "week"

            Toggle {
                width: parent.width
                text: qsTr("Shade the hours outside them")
                checked: Settings.showWorkHours
                onToggled: Settings.showWorkHours = checked
            }
            Row {
                spacing: Theme.space4

                TimeStepper {
                    minutes: Settings.workStart
                    label: qsTr("Start of working hours")
                    onStepped: minutes => Settings.workStart = minutes
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("to")
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textMd
                }
                TimeStepper {
                    minutes: Settings.workEnd
                    label: qsTr("End of working hours")
                    onStepped: minutes => Settings.workEnd = minutes
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("The day and week views open at the start of working hours.")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
            }
        }

        Section {
            title: qsTr("Reminders")
            visible: root.tab === "reminders"

            Toggle {
                width: parent.width
                text: qsTr("Remind me before events")
                checked: Settings.notify
                onToggled: Settings.notify = checked
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("For events without reminders of their own:")
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
                font.weight: Font.DemiBold
                opacity: Settings.notify ? 1 : Theme.fadedOpacity
            }
            Flow {
                width: parent.width
                spacing: Theme.space2
                enabled: Settings.notify
                opacity: enabled ? 1 : Theme.fadedOpacity
                Accessible.role: Accessible.RadioButton

                Repeater {
                    model: [
                        {
                            "label": qsTr("None"),
                            "minutes": -1
                        },
                        {
                            "label": qsTr("At start"),
                            "minutes": 0
                        },
                        {
                            "label": qsTr("5 min"),
                            "minutes": 5
                        },
                        {
                            "label": qsTr("10 min"),
                            "minutes": 10
                        },
                        {
                            "label": qsTr("30 min"),
                            "minutes": 30
                        },
                        {
                            "label": qsTr("1 hour"),
                            "minutes": 60
                        }
                    ]

                    PillButton {
                        required property var modelData
                        label: modelData.label
                        selected: Settings.reminderMinutes === modelData.minutes
                        onClicked: Settings.reminderMinutes = modelData.minutes
                    }
                }
            }
        }

        Section {
            title: qsTr("When the window closes")
            visible: root.tab === "reminders"

            Toggle {
                width: parent.width
                text: qsTr("Keep running so reminders still come")
                checked: Settings.keepRunning
                onToggled: Settings.keepRunning = checked
            }
            Toggle {
                width: parent.width
                visible: StartAtLogin.available
                enabled: Settings.keepRunning
                opacity: enabled ? 1 : Theme.fadedOpacity
                text: qsTr("Start when I log in")
                checked: StartAtLogin.enabled
                onToggled: StartAtLogin.enabled = checked
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                visible: StartAtLogin.error !== ""
                text: StartAtLogin.error
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Quit Callie from the ? menu, or with Ctrl+Q.")
                visible: Settings.keepRunning
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
            }
        }

        DangerButton {
            visible: root.tab === "general"
            label: qsTr("Reset to defaults")
            armedLabel: qsTr("Click again to reset")
            // Settings forget the theme too, so the default one comes back.
            onConfirmed: {
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

    /// A time of day with buttons for half an hour earlier and later.
    component TimeStepper: Row {
        id: stepper

        required property int minutes
        required property string label

        signal stepped(int minutes)

        spacing: Theme.space2

        StickerButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: "minimize"
            Accessible.name: qsTr("%1, half an hour earlier").arg(stepper.label)
            onClicked: stepper.stepped(stepper.minutes - 30)
        }
        // As wide as the widest time it can show, so the buttons stay put.
        TextMetrics {
            id: widest
            font: shown.font
            text: Settings.times.time(Settings.times.at(Settings.times.date(Clock.now), 12 * 60
                                                        + 30))
        }
        Text {
            id: shown
            anchors.verticalCenter: parent.verticalCenter
            width: widest.advanceWidth + Theme.space3
            horizontalAlignment: Text.AlignHCenter
            text: Settings.times.time(Settings.times.at(Settings.times.date(Clock.now),
                                                        stepper.minutes))
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
            font.weight: Font.Bold
            font.features: {
                "tnum": 1
            }
        }
        StickerButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: "plus"
            Accessible.name: qsTr("%1, half an hour later").arg(stepper.label)
            onClicked: stepper.stepped(stepper.minutes + 30)
        }
    }

    /// A small heading over a group of related settings.
    component Section: Column {
        id: section

        property string title
        default property alias content: rows.data

        width: root.width - 2 * Theme.space6
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
