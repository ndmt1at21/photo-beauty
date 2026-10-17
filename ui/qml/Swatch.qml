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
        Rectangle {  // top left to bottom right, as linear-gradient(to top right…) draws it
            visible: s.none
            anchors.centerIn: parent
            width: Math.hypot(s.width, s.height)
            height: 2
            rotation: Math.atan2(s.height, s.width) * 180 / Math.PI
            color: Theme.danger
        }
    }
}
