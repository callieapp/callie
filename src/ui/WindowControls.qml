pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// Minimize, maximize and close, drawn flat and quiet in the order the desktop
/// uses. `buttons` is one side's list from WindowButtons.
Row {
    id: root

    required property list<string> buttons
    // The attached Window type is not the QML Window type, so this stays untyped.
    readonly property var window: Window.window
    readonly property bool maximized: window !== null && window.visibility === Window.Maximized

    spacing: Theme.space1

    Repeater {
        model: root.buttons

        AbstractButton {
            id: control

            required property string modelData

            width: 30
            height: 30
            focusPolicy: Qt.StrongFocus
            Accessible.name: modelData === "minimize" ? qsTr("Minimize") : modelData === "close" ? qsTr(
                                                                                                       "Close") :
                                                                                                   root.maximized
                                                                                                   ? qsTr("Restore") :
                                                                                                     qsTr("Maximize")
            onClicked: {
                if (modelData === "minimize")
                    root.window.showMinimized()
                else if (modelData === "close")
                    root.window.close()
                else if (root.maximized)
                    root.window.showNormal()
                else
                    root.window.showMaximized()
            }

            background: Rectangle {
                radius: Theme.radiusSm
                color: !control.hovered ? "transparent" : control.modelData === "close" ? Theme.tint(
                                                                                              Theme.danger,
                                                                                              0.25) : Theme.surfaceAlt
                border.width: control.visualFocus ? 2 : 0
                border.color: Theme.text
            }
            contentItem: Item {
                Glyph {
                    anchors.centerIn: parent
                    width: 12
                    height: 12
                    stroke: 1.6
                    name: control.modelData === "maximize" && root.maximized ? "restore" :
                                                                               control.modelData
                    color: control.hovered ? Theme.text : Theme.textFaint
                }
            }
        }
    }
}
