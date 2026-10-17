pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// 3 × 3 anchor grid: Canvas Size's (22 px cells, the anchor in t1, arrows in the cells next to it
// pointing where the canvas grows) or, compact, the options bar's reference point of Free Transform
// (5 px squares in a frame). `anchor` is 0–8 row by row from the top left and comes from the owner;
// picked(index) asks it to move.
Rectangle {
    id: g
    property int anchor: 4
    property bool compact: false
    property int cell: 22
    signal picked(int index)
    readonly property var names: [qsTr("Neo trên trái"), qsTr("Neo trên giữa"), qsTr("Neo trên phải"), qsTr("Neo giữa trái"), qsTr("Neo giữa"),
        qsTr("Neo giữa phải"), qsTr("Neo dưới trái"), qsTr("Neo dưới giữa"), qsTr("Neo dưới phải")]

    implicitWidth: grid.implicitWidth
    implicitHeight: grid.implicitHeight
    radius: 3
    color: "transparent"
    border.color: compact ? Theme.t4 : "transparent"

    Grid {
        id: grid
        columns: 3
        spacing: g.compact ? 2 : 3
        padding: g.compact ? 3 : 0
        Repeater {
            model: 9
            AbstractButton {
                id: c
                required property int index
                readonly property int dr: Math.floor(index / 3) - Math.floor(g.anchor / 3)
                readonly property int dc: index % 3 - g.anchor % 3
                readonly property bool near: Math.max(Math.abs(dr), Math.abs(dc)) === 1
                width: g.compact ? 5 : g.cell
                height: width
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: g.names[index]
                Accessible.role: Accessible.RadioButton
                Accessible.checked: index === g.anchor
                onClicked: g.picked(index)
                background: Rectangle {
                    radius: g.compact ? 0 : 4
                    color: c.index === g.anchor ? Theme.t1 : g.compact ? Theme.t4 : Theme.p2
                }
                contentItem: Item {
                    Icon {  // the arrow points down, turned toward (dc, dr)
                        visible: !g.compact && c.near
                        anchors.centerIn: parent
                        name: "arrow"
                        size: g.cell / 2 + 3
                        rotation: Math.atan2(-c.dc, c.dr) * 180 / Math.PI
                        color: Theme.t2
                    }
                }
            }
        }
    }
}
