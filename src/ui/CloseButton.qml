import Callie.Ui
import QtQuick

/// The X that closes a popup, as Escape does.
StickerButton {
    glyph: "close"
    tip: qsTr("Close (Esc)")
    Accessible.name: qsTr("Close")
}
