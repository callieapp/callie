import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Says what was just done, with a button to take it back, at the bottom of
/// the window. It goes away before the change is sent, while undo still works.
Popup {
    id: root

    property CalendarSource source
    property string changeId
    property string what

    /// Takes back the change shown, if it can still be.
    function undo() {
        if (changeId === "" || !source)
            return
        const undone = source.undoChange(changeId)
        changeId = ""
        if (undone) {
            close()
        } else {
            what = qsTr("Too late to undo")
            hide.restart()
        }
    }

    function show(id, text) {
        changeId = id
        what = text
        open()
        hide.restart()
    }

    x: (parent ? parent.width - width : 0) / 2
    y: parent ? parent.height - height - Theme.space6 : 0
    padding: Theme.space3
    leftPadding: Theme.space5
    modal: false
    focus: false
    closePolicy: Popup.NoAutoClose

    Timer {
        id: hide
        // Shorter than the time a change is held, so Undo always works while shown.
        interval: 6000
        onTriggered: root.close()
    }

    background: Rectangle {
        radius: height / 2
        color: Theme.surfaceAlt
        border.color: Theme.border

        SoftShadow {
            anchors.fill: parent
            radius: parent.radius
        }
    }

    contentItem: Row {
        spacing: Theme.space4

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.what
            textFormat: Text.PlainText
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textMd
            font.weight: Font.DemiBold
        }
        PillButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.changeId !== ""
            label: qsTr("Undo")
            onClicked: root.undo()
        }
    }
}
