pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Finds events by any words in their title, place, notes or guests. Up and
/// Down pick a result and Enter opens it on its day, as a click does.
Popup {
    id: root

    property CalendarSource source

    /// Asks the window to show this event on its day.
    signal jumpRequested(date day, string uid, date start)

    function choose(row) {
        if (row < 0 || row >= results.count)
            return
        const start = search.data(search.index(row, 0), SearchModel.StartRole)
        const uid = search.data(search.index(row, 0), SearchModel.UidRole)
        close()
        jumpRequested(Settings.times.date(start), uid, start)
    }

    anchors.centerIn: undefined
    x: (parent ? parent.width - width : 0) / 2
    y: Theme.space7 * 2
    width: Math.min(560, parent ? parent.width - 2 * Theme.space6 : 560)
    padding: Theme.space4
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        field.selectAll()
        field.forceActiveFocus()
    }

    SearchModel {
        id: search
        source: root.source
        now: Clock.now
        hiddenCalendars: Settings.hiddenCalendars
        query: root.opened ? field.text : ""
    }

    Overlay.modal: Rectangle {
        // The attached Window type is not the QML Window type, so this stays untyped.
        readonly property var appWindow: Window.window

        radius: appWindow && appWindow.cornerRadius ? appWindow.cornerRadius : 0
        color: Theme.tint(Theme.shadowColor, 0.35)
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
        spacing: Theme.space3

        Field {
            id: field
            width: parent.width
            placeholderText: qsTr("Search events")
            Accessible.name: qsTr("Search events")
            onTextEdited: results.currentIndex = 0
            Keys.onDownPressed: results.incrementCurrentIndex()
            Keys.onUpPressed: results.decrementCurrentIndex()
            Keys.onReturnPressed: root.choose(results.currentIndex)
            Keys.onEnterPressed: root.choose(results.currentIndex)
        }

        Text {
            visible: field.text.trim() !== "" && search.count === 0
            width: parent.width
            leftPadding: Theme.space3
            text: search.busy ? qsTr("Looking...") : qsTr("Nothing matches.")
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
        }

        ListView {
            id: results
            visible: search.count > 0
            width: parent.width
            height: Math.min(contentHeight, Window.height / 2)
            clip: true
            model: search
            currentIndex: 0
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            section.property: "upcoming"
            section.delegate: Text {
                required property string section
                width: results.width
                topPadding: Theme.space2
                bottomPadding: Theme.space1
                leftPadding: Theme.space3
                text: section === "true" ? qsTr("UPCOMING") : qsTr("PAST")
                color: Theme.textFaint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textXs
                font.weight: Font.ExtraBold
                font.letterSpacing: 0.6
            }

            delegate: AbstractButton {
                id: result

                required property int index
                required property string summary
                required property date start
                required property bool allDay
                required property string location
                required property color calendarColor
                required property bool upcoming

                width: results.width
                height: Theme.listRowHeight + Theme.space3
                Accessible.name: summary
                onClicked: root.choose(index)

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                background: Rectangle {
                    radius: Theme.radiusMd
                    color: result.ListView.isCurrentItem || result.hovered ? Theme.surfaceAlt :
                                                                             "transparent"
                }
                contentItem: Row {
                    leftPadding: Theme.space3
                    spacing: Theme.space3

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 150
                        text: {
                            const date = Settings.times.date(result.start)
                            // The year only when it is not this one.
                            const day = Qt.formatDate(date, date.getFullYear()
                                                      === Clock.now.getFullYear() ? "ddd, MMM d" :
                                                                                    "ddd, MMM d, yyyy")
                            return result.allDay ? day : qsTr("%1, %2").arg(day).arg(
                                                       Settings.times.time(result.start))
                        }
                        elide: Text.ElideRight
                        color: result.upcoming ? Theme.textMuted : Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                        font.weight: Font.Bold
                        font.features: {
                            "tnum": 1
                        }
                    }
                    CalendarMark {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.space3
                        height: Theme.space3
                        calendarColor: result.calendarColor
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: result.width - 150 - Theme.space3 - 3 * Theme.space3 - Theme.space3
                        text: result.location ? qsTr("%1, %2").arg(result.summary).arg(
                                                    result.location) : result.summary
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: result.upcoming ? Theme.text : Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textMd
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }
}
