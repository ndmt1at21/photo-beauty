import QtQuick
import QtQuick.Shapes
import QtQuick.Controls.Basic

// Check box and its label (<input type=checkbox> with accent-color in the prototype, drawn as Chrome draws it): a 16 px
// row (12 px text × 1.35), `checked` comes from the owner, clicked() asks it to flip. Off, the whole row fades.
// wrapWidth > 0: the label wraps to keep the row (box, gap, label) that wide, lines of 12 × 1.35, the box in the middle.
AbstractButton {
    id: c
    property real wrapWidth: 0
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: wrapWidth > 0 ? Math.max(16, implicitContentHeight) : 16
    padding: 0
    spacing: 6  // the label's gap: 8 in SelectMask, Liquify's View Options, the spaces' foot
    opacity: enabled ? 1 : 0.4
    hoverEnabled: true
    font.pointSize: Theme.pt(12)
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: checked

    contentItem: Row {
        spacing: c.spacing
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 13
            height: 13
            radius: 2
            color: c.checked ? Theme.accent : Theme.checkFill
            border.color: c.checked ? Theme.accent : c.enabled && c.hovered ? Theme.t2 : Theme.checkLine
            Shape {
                anchors.fill: parent
                visible: c.checked
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.checkTick
                    strokeWidth: 1.9
                    fillColor: "transparent"
                    capStyle: ShapePath.FlatCap
                    joinStyle: ShapePath.MiterJoin
                    PathPolyline { path: [Qt.point(3, 6.6), Qt.point(5.4, 9), Qt.point(10, 3.2)] }
                }
            }
        }
        Text {
            readonly property bool wraps: c.wrapWidth > 0
            anchors.verticalCenter: parent.verticalCenter
            width: wraps ? c.wrapWidth - 13 - c.spacing : implicitWidth
            wrapMode: wraps ? Text.Wrap : Text.NoWrap
            lineHeightMode: wraps ? Text.FixedHeight : Text.ProportionalHeight
            lineHeight: wraps ? 12 * 1.35 * Theme.fontScale : 1
            text: c.text
            font: c.font
            color: Theme.t2
        }
    }
}
