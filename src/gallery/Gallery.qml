pragma ComponentBehavior: Bound

import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Every token and component of the active theme on one page, so a theme can be
/// judged by looking rather than by reading values.
ApplicationWindow {
    id: window

    // Google Calendar's own event colors, as sample input for harmonizing.
    readonly property var sampleCalendars: [
        {
            name: "Tomato",
            color: "#d50000"
        },
        {
            name: "Tangerine",
            color: "#f4511e"
        },
        {
            name: "Banana",
            color: "#f6bf26"
        },
        {
            name: "Basil",
            color: "#0b8043"
        },
        {
            name: "Sage",
            color: "#33b679"
        },
        {
            name: "Peacock",
            color: "#039be5"
        },
        {
            name: "Blueberry",
            color: "#3f51b5"
        },
        {
            name: "Lavender",
            color: "#7986cb"
        },
        {
            name: "Grape",
            color: "#8e24aa"
        },
        {
            name: "Flamingo",
            color: "#e67c73"
        },
        {
            name: "Graphite",
            color: "#616161"
        }
    ]

    width: 1100
    height: 900
    visible: true
    title: qsTr("Callie gallery: %1").arg(Theme.name)
    color: Theme.bg

    component Heading: Text {
        color: Theme.text
        font.family: Theme.displayFontFamily
        font.pixelSize: Theme.textXl
        font.weight: Font.DemiBold
        topPadding: Theme.space6
    }

    component Label: Text {
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }

    component Mono: Text {
        color: Theme.textFaint
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.textXs
        font.features: {
            "tnum": 1
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        Column {
            objectName: "content"
            spacing: Theme.space4
            width: parent.width
            padding: Theme.space7

            Text {
                text: Theme.name
                color: Theme.text
                font.family: Theme.displayFontFamily
                font.pixelSize: Theme.text2xl
                font.weight: Font.DemiBold
            }

            Label {
                text: Theme.dark ? qsTr("Dark theme") : qsTr("Light theme")
            }

            Repeater {
                model: Theme.warnings

                Text {
                    required property string modelData

                    text: qsTr("Warning: %1").arg(modelData)
                    color: Theme.danger
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textMd
                }
            }

            Heading {
                text: qsTr("Colors")
            }

            Flow {
                width: parent.width - 2 * Theme.space7
                spacing: Theme.space4

                Repeater {
                    model: Theme.swatches

                    Column {
                        id: swatch

                        required property var modelData

                        spacing: Theme.space2

                        Rectangle {
                            width: 132
                            height: 64
                            radius: Theme.radiusMd
                            color: swatch.modelData.color
                            border.color: Theme.border
                            border.width: 1
                        }

                        Label {
                            text: swatch.modelData.name
                        }

                        Mono {
                            text: swatch.modelData.color.toString() + "  " + Theme.contrast(
                                      swatch.modelData.color, Theme.bg).toFixed(1) + ":1"
                        }
                    }
                }
            }

            Heading {
                text: qsTr("Type")
            }

            Repeater {
                model: [Theme.text2xl, Theme.textXl, Theme.textLg, Theme.textMd, Theme.textSm,
                    Theme.textXs]

                Text {
                    required property int modelData

                    text: qsTr("%1px: Lunch with Alex at Cafe Sol, 12:00 to 13:00").arg(modelData)
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: modelData
                }
            }

            Heading {
                text: qsTr("Shape")
            }

            Row {
                spacing: Theme.space5

                Repeater {
                    model: [Theme.radiusSm, Theme.radiusMd, Theme.radiusLg, Theme.radiusXl]

                    Rectangle {
                        id: radiusSample

                        required property int modelData

                        width: 96
                        height: 64
                        radius: modelData
                        color: Theme.surfaceAlt
                        border.color: Theme.border
                        border.width: 1

                        Mono {
                            anchors.centerIn: parent
                            text: radiusSample.modelData + "px"
                        }
                    }
                }
            }

            Heading {
                text: qsTr("Controls")
            }

            Row {
                spacing: Theme.space3

                PillButton {
                    label: qsTr("Week")
                    selected: true
                }

                PillButton {
                    label: qsTr("Month")
                }

                StickerButton {
                    glyph: "chevron-left"
                    Accessible.name: qsTr("Previous")
                }

                StickerButton {
                    glyph: "chevron-right"
                    Accessible.name: qsTr("Next")
                }

                StickerButton {
                    text: qsTr("Today")
                }

                StickerButton {
                    text: qsTr("Join call")
                    accent: true
                }

                WindowControls {
                    buttons: ["minimize", "maximize", "close"]
                }
            }

            Heading {
                text: qsTr("Calendar colors")
            }

            Label {
                text: qsTr("Original, then fitted to this theme, then as an event.")
            }

            Repeater {
                model: window.sampleCalendars

                Row {
                    id: calendar

                    required property var modelData
                    readonly property color fitted: Theme.calendarColor(modelData.color,
                                                                        Theme.calendar)

                    spacing: Theme.space4

                    Label {
                        width: 88
                        anchors.verticalCenter: parent.verticalCenter
                        text: calendar.modelData.name
                    }

                    Rectangle {
                        width: 40
                        height: 40
                        radius: Theme.radiusMd
                        color: calendar.modelData.color
                    }

                    Rectangle {
                        width: 40
                        height: 40
                        radius: Theme.radiusMd
                        color: calendar.fitted
                    }

                    EventBlock {
                        width: 240
                        height: 48
                        summary: qsTr("%1 planning").arg(calendar.modelData.name)
                        location: qsTr("Studio")
                        joinUrl: ""
                        calendarName: calendar.modelData.name
                        description: ""
                        calendarColor: calendar.modelData.color
                        start: new Date(2026, 9, 5, 10, 0)
                        end: new Date(2026, 9, 5, 11, 0)
                        declined: false
                        depth: 0
                        canEdit: false
                        response: ""
                    }

                    EventBlock {
                        width: 240
                        height: 48
                        summary: qsTr("%1 maybe").arg(calendar.modelData.name)
                        location: ""
                        joinUrl: ""
                        calendarName: calendar.modelData.name
                        description: ""
                        calendarColor: calendar.modelData.color
                        start: new Date(2026, 9, 5, 10, 0)
                        end: new Date(2026, 9, 5, 11, 0)
                        declined: false
                        response: "tentative"
                        depth: 0
                        canEdit: false
                    }
                }
            }
        }
    }
}
