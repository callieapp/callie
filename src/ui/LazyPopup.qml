import QtQuick
import QtQuick.Controls

/// A popup made the first time it opens, so the window does not wait for it
/// at startup. Give it the popup as its sourceComponent and call open().
Loader {
    id: root

    readonly property Popup popup: item as Popup
    readonly property bool opened: popup !== null && popup.opened

    function open() {
        active = true
        popup.open()
    }

    function close() {
        if (popup)
            popup.close()
    }

    active: false
}
