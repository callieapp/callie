import Callie.Ui
import QtQuick

/// A labelled row of a form: the label in a column of `labelWidth`, so the
/// rows' controls line up, and the controls flowing on in the rest.
Row {
    id: root

    property string label
    property real labelWidth
    default property alias content: controls.data

    spacing: Theme.space3

    Text {
        width: root.labelWidth
        height: Theme.listRowHeight + Theme.space1
        verticalAlignment: Text.AlignVCenter
        text: root.label
        color: Theme.textFaint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
        font.weight: Font.ExtraBold
    }
    Flow {
        id: controls
        width: root.width - root.labelWidth - root.spacing
        spacing: Theme.space2
    }
}
