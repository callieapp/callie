pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick

/// The theme's soft shadow under a raised surface: menus, popovers, tooltips.
/// Stacked translucent rounded rectangles stand in for a blur, so it needs no
/// graphics effects module. Fill the surface it belongs to, behind it.
Item {
    id: root

    property real radius: Theme.radiusLg
    // Enough layers that their steps do not show as bands over light content.
    readonly property int layers: 14

    z: -1

    Repeater {
        model: root.layers

        Rectangle {
            required property int index
            readonly property real step: (index + 1) / root.layers

            x: -Theme.shadowBlur * step / 2
            y: -Theme.shadowBlur * step / 2 + Theme.shadowOffset * step
            width: root.width + Theme.shadowBlur * step
            height: root.height + Theme.shadowBlur * step
            radius: root.radius + Theme.shadowBlur * step / 2
            // Inner layers darkest, easing out so the edge has no visible rim.
            color: Theme.tint(Theme.shadowColor, Theme.shadowColor.a * (1 - step) * (1 - step) * 3
                              / root.layers)
        }
    }
}
