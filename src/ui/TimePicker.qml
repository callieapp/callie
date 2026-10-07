pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// A time of day as a button that opens a list of quarter hours to pick from.
AbstractButton {
    id: root

    /// Minutes past midnight.
    property int minutes
    /// What the list starts from, such as the start time for an end time.
    property int from: 0
    /// Shows how long after `from` each choice is, for an end time.
    property bool showLength: false

    signal picked(int minutes)

    /// How a time reads, such as "2:30 PM"; the day only carries the clock.
    function label(m) {
        return Settings.times.time(Settings.times.at(Settings.times.date(Clock.now), m))
    }

    implicitHeight: Theme.listRowHeight + Theme.space2
    implicitWidth: caption.implicitWidth + 2 * Theme.space4
    Accessible.name: label(minutes)
    onClicked: list.open()

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: root.hovered || list.opened ? Theme.border : Theme.surfaceAlt
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.text
    }
    contentItem: Text {
        id: caption
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: root.label(root.minutes)
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textMd
        font.weight: Font.Bold
        font.features: {
            "tnum": 1
        }
    }

    Popup {
        id: list
        y: root.height + Theme.space1
        width: Math.max(root.width, Theme.space7 * 5)
        height: Math.min(times.contentHeight + 2 * padding, Theme.space7 * 8)
        padding: Theme.space1
        onOpened: times.positionViewAtIndex(Math.max(0, times.currentIndex - 2), ListView.Beginning)

        background: Rectangle {
            radius: Theme.radiusLg
            color: Theme.surface
            border.color: Theme.border

            SoftShadow {
                anchors.fill: parent
                radius: parent.radius
            }
        }

        contentItem: ListView {
            id: times
            clip: true
            // Every quarter hour from the start of the list to the end of the day.
            model: Math.max(1, Math.floor((24 * 60 - root.from) / Theme.snapMinutes))
            currentIndex: Math.max(0, Math.round((root.minutes - root.from) / Theme.snapMinutes))
            boundsBehavior: Flickable.StopAtBounds

            delegate: MenuEntry {
                required property int index
                readonly property int at: root.from + index * Theme.snapMinutes

                width: times.width
                highlighted: ListView.isCurrentItem || hovered
                text: {
                    if (!root.showLength)
                        return root.label(at)
                    const length = at - root.from
                    const span = length < 60 ? qsTr("%1 min").arg(length) : length % 60 === 0 ? qsTr(
                                                                                                    "%1 h").arg(
                                                                                                    length / 60) :
                                                                                                qsTr("%1 h %2 min").arg(
                                                                                                    Math.floor(
                                                                                                        length / 60)).arg(
                                                                                                    length % 60)
                    return qsTr("%1 (%2)").arg(root.label(at)).arg(span)
                }
                onClicked: {
                    list.close()
                    root.picked(at)
                }
            }
        }
    }
}
