import Callie.Ui
import QtQuick

/// Callie's face. Hovering or clicking it makes it hop and wiggle, once each
/// time the pointer arrives.
Item {
    id: root

    property int size: 28

    function play() {
        if (!wiggle.running)
            wiggle.start()
    }

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    Image {
        id: face
        anchors.centerIn: parent
        width: root.size
        height: root.size
        source: "qrc:/callie/assets/logo.png"
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        smooth: true
        mipmap: true
        transformOrigin: Item.Bottom
    }

    HoverHandler {
        onHoveredChanged: {
            if (hovered)
                root.play()
        }
    }
    TapHandler {
        onTapped: root.play()
    }

    SequentialAnimation {
        id: wiggle

        // A hop up, landing with a little squash.
        ParallelAnimation {
            NumberAnimation {
                target: face
                property: "anchors.verticalCenterOffset"
                to: -root.size * 0.18
                duration: Theme.durFast
                easing.type: Easing.OutQuad
            }
            NumberAnimation {
                target: face
                property: "scale"
                to: 1.08
                duration: Theme.durFast
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: face
                property: "anchors.verticalCenterOffset"
                to: 0
                duration: Theme.durMed
                easing.type: Easing.OutBounce
            }
            NumberAnimation {
                target: face
                property: "scale"
                to: 1
                duration: Theme.durMed
            }
        }
        // Then a happy side-to-side wiggle that settles.
        NumberAnimation {
            target: face
            property: "rotation"
            to: -12
            duration: Theme.durFast / 2
        }
        NumberAnimation {
            target: face
            property: "rotation"
            to: 10
            duration: Theme.durFast
        }
        NumberAnimation {
            target: face
            property: "rotation"
            to: -5
            duration: Theme.durFast
        }
        NumberAnimation {
            target: face
            property: "rotation"
            to: 0
            duration: Theme.durMed
            easing.type: Theme.easingBounce
            easing.overshoot: Theme.bounce
        }
    }
}
