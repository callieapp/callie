pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// What is in an .ics file being opened, and which calendar to add it to.
Popup {
    id: root

    /// The calendar picked; the first that can take events until one is.
    property string calendarId

    /// When a previewed event starts, with its year only when not this one.
    function when(event) {
        // A timed start is put on its day in the zone Callie shows times in.
        const start = event.allDay ? event.start : Settings.times.date(event.start)
        const thisYear = start.getFullYear() === new Date().getFullYear()
        const day = Qt.formatDate(start, thisYear ? "ddd d MMM" : "ddd d MMM yyyy")
        return event.allDay ? day : day + ", " + Settings.times.time(event.start)
    }

    anchors.centerIn: Overlay.overlay
    width: 420
    padding: Theme.space6
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    onAboutToShow: calendarId = Importer.calendars.length > 0 ? Importer.calendars[0].id : ""
    onClosed: Importer.cancel()

    Overlay.modal: Scrim {}

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
        spacing: Theme.space4

        DialogHeader {
            title: Importer.count === 1 ? qsTr("Add 1 event") : qsTr("Add %1 events").arg(
                                              Importer.count)

            subtitle: qsTr("From %1").arg(Importer.fileName)
            onCloseRequested: root.close()
        }

        Column {
            width: parent.width
            spacing: Theme.space2

            Repeater {
                model: Importer.preview

                Row {
                    id: line

                    required property var modelData

                    width: parent.width
                    spacing: Theme.space3

                    Text {
                        width: 150
                        text: root.when(line.modelData)
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width - 150 - parent.spacing
                        text: line.modelData.summary || qsTr("(No title)")
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                        font.weight: Theme.weightStrong
                        elide: Text.ElideRight
                    }
                }
            }
            Text {
                visible: Importer.count > Importer.preview.length
                text: qsTr("and %n more", "", Importer.count - Importer.preview.length)
                color: Theme.textFaint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
            }
        }

        Row {
            width: parent.width
            spacing: Theme.space4

            Text {
                id: into
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Add to")
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
                font.weight: Theme.weightStrong
            }
            Dropdown {
                model: Importer.calendars
                currentValue: root.calendarId
                minimumWidth: 160
                maximumWidth: parent.width - into.width - parent.spacing
                Accessible.name: qsTr("Calendar to add them to")
                onActivated: value => root.calendarId = value
            }
        }

        Text {
            visible: Importer.error !== ""
            width: parent.width
            wrapMode: Text.Wrap
            text: Importer.error
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
        }

        Row {
            anchors.right: parent.right
            spacing: Theme.space3

            StickerButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            StickerButton {
                accent: true
                glyph: "plus"
                text: Importer.busy ? qsTr("Adding...") : qsTr("Add")
                enabled: root.calendarId !== "" && !Importer.busy
                onClicked: Importer.add(root.calendarId)
            }
        }
    }
}
