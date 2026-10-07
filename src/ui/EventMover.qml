import Callie.Ui
import QtQuick

/// Saves an event dragged to new times. A repeating event first asks whether
/// the change is for the whole series; a failure puts the dragged item back
/// and says why beside it. Fills the view whose items it moves.
Item {
    id: root

    required property var source
    /// Places a popup beside an item, as the view lays them out.
    required property var placeBeside

    /// The item whose change is being saved, with a settle() that puts it back.
    property var moving: null

    function move(item, event, from, to) {
        if (actions.busy) {
            item.settle()
            return
        }
        moving = item
        if (event.seriesId !== "") {
            placeBeside(item, choice)
            choice.ask(event, from, to)
        } else {
            actions.move(event, from, to, false)
        }
    }

    function failed(message) {
        if (!moving)
            return
        if (message !== "") {
            error.text = message
            placeBeside(moving, error)
            error.open()
        }
        moving.settle()
        moving = null
    }

    EventActions {
        id: actions
        source: root.source
        onErrorChanged: {
            if (error !== "")
                root.failed(error)
        }
        // The reloaded model draws the event in its new place.
        onMoved: root.moving = null
    }

    MoveChoice {
        id: choice
        onChosen: (event, from, to, wholeSeries) => actions.move(event, from, to, wholeSeries)
        onCancelled: root.failed("")
    }

    Tip {
        id: error
        timeout: 5000
    }
}
