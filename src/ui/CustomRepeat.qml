pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// The editor's custom repeat: every so many days, weeks, months or years, on
/// which weekdays or which day of the month, and when it ends. With developer
/// mode on, the rule itself can be edited too.
Column {
    id: root

    /// The EventEditor, whose custom rule this edits.
    required property var editor
    readonly property var custom: editor.custom
    readonly property string frequency: custom.frequency || ""
    readonly property int interval: custom.interval || 1
    readonly property var weekdays: custom.weekdays || []
    readonly property bool hasUntil: !!custom.until && !isNaN(custom.until.getTime())
    readonly property int count: custom.count || 0

    spacing: Theme.space3

    Text {
        visible: !root.editor.customReadable
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr("This event repeats in a way this form cannot change, so it stays as it is.")
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }

    Column {
        visible: root.editor.customReadable
        width: parent.width
        spacing: Theme.space3

        // Every N days, weeks, months or years.
        Flow {
            width: parent.width
            spacing: Theme.space2

            Label {
                text: qsTr("Every")
            }
            Stepper {
                value: root.interval
                label: qsTr("Interval")
                onStepped: value => root.editor.setCustom({
                                                              "interval": Math.max(1, value)
                                                          })
            }
            Repeater {
                model: [
                    {
                        "id": "daily",
                        "label": qsTr("days", "", root.interval)
                    },
                    {
                        "id": "weekly",
                        "label": qsTr("weeks", "", root.interval)
                    },
                    {
                        "id": "monthly",
                        "label": qsTr("months", "", root.interval)
                    },
                    {
                        "id": "yearly",
                        "label": qsTr("years", "", root.interval)
                    }
                ]

                PillButton {
                    required property var modelData
                    label: modelData.label
                    selected: root.frequency === modelData.id
                    onClicked: root.editor.setCustom({
                                                         "frequency": modelData.id
                                                     })
                }
            }
        }

        // Which weekdays, in the order the week starts in.
        Flow {
            visible: root.frequency === "weekly"
            width: parent.width
            spacing: Theme.space1

            Repeater {
                model: 7

                PillButton {
                    required property int index
                    // Qt's day numbers, Monday 1 to Sunday 7.
                    readonly property int day: (Settings.firstDayOfWeek - 1 + index) % 7 + 1
                    readonly property bool on: root.weekdays.indexOf(day) >= 0
                    label: Qt.locale().dayName(day % 7, Locale.ShortFormat)
                    selected: on
                    Accessible.name: Qt.locale().dayName(day % 7, Locale.LongFormat)
                    // One day at least stays on.
                    onClicked: {
                        if (on && root.weekdays.length === 1)
                            return
                        const days = on ? root.weekdays.filter(d => d !== day) : root.weekdays.concat(
                                              [day])
                        root.editor.setCustom({
                                                  "weekdays": days
                                              })
                    }
                }
            }
        }

        // On the day of the month, or on its weekday of the month.
        Flow {
            visible: root.frequency === "monthly"
            width: parent.width
            spacing: Theme.space2

            Repeater {
                model: root.editor.actions.repeatChoices(root.editor.startDay).filter(c => c.id
                                                                                           === "monthly"
                                                                                           || c.id
                                                                                           === "monthlyWeekday")

                PillButton {
                    required property var modelData
                    readonly property bool onWeekday: modelData.id === "monthlyWeekday"
                    label: modelData.label
                    selected: !!root.custom.onWeekday === onWeekday
                    onClicked: root.editor.setCustom({
                                                         "onWeekday": onWeekday
                                                     })
                }
            }
        }

        // Never, on a date, or after so many times.
        Flow {
            width: parent.width
            spacing: Theme.space2

            Label {
                text: qsTr("Ends")
            }
            PillButton {
                label: qsTr("Never")
                selected: !root.hasUntil && root.count === 0
                onClicked: root.editor.setCustom({
                                                     "until": new Date(NaN),
                                                     "count": 0
                                                 })
            }
            PillButton {
                label: qsTr("On a day")
                selected: root.hasUntil
                // Three months on, to change from there.
                onClicked: root.editor.setCustom({
                                                     "until": root.editor.actions.addDays(
                                                                  root.editor.startDay, 90),
                                                     "count": 0
                                                 })
            }
            PillButton {
                label: qsTr("After")
                selected: root.count > 0
                onClicked: root.editor.setCustom({
                                                     "until": new Date(NaN),
                                                     "count": 10
                                                 })
            }
            DatePicker {
                visible: root.hasUntil
                day: root.hasUntil ? root.custom.until : root.editor.startDay
                onPicked: day => root.editor.setCustom({
                                                           "until": day
                                                       })
            }
            Stepper {
                visible: root.count > 0
                value: root.count
                label: qsTr("Times")
                onStepped: value => root.editor.setCustom({
                                                              "count": Math.max(1, value)
                                                          })
            }
            Label {
                visible: root.count > 0
                text: qsTr("times", "", root.count)
            }
        }
    }

    // The rule itself, for developers: one line each, as RFC 5545 writes them.
    Field {
        visible: Settings.developerMode
        width: parent.width
        text: root.editor.customLines.join(" ")
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.textXs
        placeholderText: "RRULE:FREQ=WEEKLY;BYDAY=MO"
        Accessible.name: qsTr("Repeat rule")
        onEditingFinished: {
            const lines = text.trim().split(/\s+/).filter(l => l !== "")
            if (JSON.stringify(lines) === JSON.stringify(root.editor.customLines))
                return
            root.editor.customLines = lines
            root.editor.custom = root.editor.actions.customRepeat(lines, root.editor.startDay,
                                                                  root.editor.zone)
            root.editor.customTouched = true
        }
    }

    component Label: Text {
        height: Theme.listRowHeight + Theme.space2
        verticalAlignment: Text.AlignVCenter
        color: Theme.textFaint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
        font.weight: Font.ExtraBold
    }

    /// A number with a button either side.
    component Stepper: Row {
        id: stepper

        required property int value
        required property string label

        signal stepped(int value)

        spacing: Theme.space2

        StickerButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: "minimize"
            Accessible.name: qsTr("%1, one less").arg(stepper.label)
            onClicked: stepper.stepped(stepper.value - 1)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 28
            horizontalAlignment: Text.AlignHCenter
            text: stepper.value
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
            Accessible.name: qsTr("%1, one more").arg(stepper.label)
            onClicked: stepper.stepped(stepper.value + 1)
        }
    }
}
