import Callie.Ui
import QtQuick

/// Carries an event dragged to a view's edge onto the days before or after:
/// held at the edge, the view pages, and the drag goes on over the new days
/// until the button is let go. Paging rebuilds the dragged item, so from then
/// on the view draws a stand-in that follows `point`.
Item {
    id: root

    /// What the view noted about the carried event, or null when not carrying.
    property var carry: null
    /// The pointer in the scene, while dragging.
    property point point
    /// Dropped, and waiting for the move to be saved.
    property bool dropped: false

    /// Held at an edge long enough: the view should note what `item` drags
    /// in `carry`, or leave it null to stay put.
    signal handOff(var item, point scenePosition)
    /// The view should show the days before (-1) or after (1).
    signal pageRequested(int direction)
    signal moved(point scenePosition)
    signal released(point scenePosition)

    /// Reports where `item` is dragged to, and the edge the pointer is at:
    /// -1, 1, or 0 for neither. `item` is null once carrying.
    function track(item, scenePosition, edge) {
        point = scenePosition
        if (item)
            hold.item = item
        if (edge === hold.edge)
            return
        hold.edge = edge
        if (edge !== 0)
            hold.restart()
        else
            hold.stop()
    }

    function clear() {
        hold.stop()
        hold.edge = 0
        hold.item = null
        watch.active = false
        carry = null
        dropped = false
    }

    Timer {
        id: hold

        property var item: null
        property int edge: 0

        interval: 500
        repeat: true
        onTriggered: {
            if (!root.carry) {
                // Let go before the wait was over.
                if (!hold.item || !hold.item.dragging) {
                    root.clear()
                    return
                }
                root.handOff(hold.item, root.point)
                if (!root.carry) {
                    root.clear()
                    return
                }
                watch.active = true
            }
            root.pageRequested(hold.edge)
        }
    }

    PointerWatch {
        id: watch
        window: root.Window.window
        onMoved: scenePosition => {
            root.point = scenePosition
            root.moved(scenePosition)
        }
        onReleased: scenePosition => {
            hold.stop()
            hold.edge = 0
            root.point = scenePosition
            root.released(scenePosition)
        }
        onCancelled: root.clear()
    }
}
