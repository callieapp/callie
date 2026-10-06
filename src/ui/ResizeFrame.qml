pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick

/// The resize borders a frameless window loses: thin strips along each edge and
/// corner that hand the drag to the window system.
Item {
    id: root

    // The attached Window type is not the QML Window type, so this stays untyped.
    readonly property var window: Window.window
    property int thickness: 6

    visible: window !== null && window.visibility !== Window.Maximized && window.visibility
             !== Window.FullScreen

    component Edge: Item {
        id: edge

        required property int edges
        required property int cursor

        HoverHandler {
            cursorShape: edge.cursor
        }
        DragHandler {
            target: null
            dragThreshold: 0
            onActiveChanged: {
                if (active)
                    root.window.startSystemResize(edge.edges)
            }
        }
    }

    Edge {
        edges: Qt.TopEdge
        cursor: Qt.SizeVerCursor
        x: root.thickness
        width: root.width - 2 * root.thickness
        height: root.thickness
    }
    Edge {
        edges: Qt.BottomEdge
        cursor: Qt.SizeVerCursor
        x: root.thickness
        y: root.height - root.thickness
        width: root.width - 2 * root.thickness
        height: root.thickness
    }
    Edge {
        edges: Qt.LeftEdge
        cursor: Qt.SizeHorCursor
        y: root.thickness
        width: root.thickness
        height: root.height - 2 * root.thickness
    }
    Edge {
        edges: Qt.RightEdge
        cursor: Qt.SizeHorCursor
        x: root.width - root.thickness
        y: root.thickness
        width: root.thickness
        height: root.height - 2 * root.thickness
    }
    Edge {
        edges: Qt.TopEdge | Qt.LeftEdge
        cursor: Qt.SizeFDiagCursor
        width: root.thickness * 2
        height: root.thickness * 2
    }
    Edge {
        edges: Qt.TopEdge | Qt.RightEdge
        cursor: Qt.SizeBDiagCursor
        x: root.width - width
        width: root.thickness * 2
        height: root.thickness * 2
    }
    Edge {
        edges: Qt.BottomEdge | Qt.LeftEdge
        cursor: Qt.SizeBDiagCursor
        y: root.height - height
        width: root.thickness * 2
        height: root.thickness * 2
    }
    Edge {
        edges: Qt.BottomEdge | Qt.RightEdge
        cursor: Qt.SizeFDiagCursor
        x: root.width - width
        y: root.height - height
        width: root.thickness * 2
        height: root.thickness * 2
    }
}
