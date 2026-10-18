import QtQuick
import QtQuick.Controls.Basic

// Colour cell of the prototype (.sw, 18 px): `color`, or with `none` white struck through in red (no
// colour, NONE_SW); `ring` the 1 px edge inside it. clicked() is the owner's to answer.
AbstractButton {
    id: s
    property color color: "transparent"
    property bool none: false
    property color ring: Qt.rgba(1, 1, 1, 0.14)
    property real radius: 3

    implicitWidth: 18
    implicitHeight: 18
    hoverEnabled: true
    ToolTip.visible: hovered && ToolTip.text !== ""
    ToolTip.delay: 600

    background: Rectangle {
        radius: s.radius
        color: s.none ? "#ffffff" : s.color
        border.width: 1
        border.color: s.ring
        clip: true
        Rectangle {  // top left to bottom right, as linear-gradient(to top right…) draws it, stopping at the round corners
            // where the diagonal meets a corner's arc, + 1 for the stroke's own width: clip only cuts square
            readonly property real u: (s.width + s.height) / Math.hypot(s.width, s.height)
            readonly property real cut: s.radius * (u - Math.sqrt(u * u - 1)) + 1
            visible: s.none
            anchors.centerIn: parent
            width: Math.hypot(s.width, s.height) - 2 * cut
            height: 2
            rotation: Math.atan2(s.height, s.width) * 180 / Math.PI
            antialiasing: true
            color: Theme.danger
        }
    }
}
