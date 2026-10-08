import Callie.Ui
import QtQuick

/// The dim laid over the calendar behind a popup, fading in and out with it.
Rectangle {
    // The attached Window type is not the QML Window type, so this stays untyped.
    readonly property var appWindow: Window.window

    // Follows the window's rounded corners rather than filling them in.
    radius: appWindow && appWindow.cornerRadius ? appWindow.cornerRadius : 0
    color: Theme.tint(Theme.shadowColor, 0.35)

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.durFast
        }
    }
}
