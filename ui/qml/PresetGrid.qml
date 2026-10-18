pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Preset grid (brushes, gradients, swatches; patterns, shapes, styles at P2): groups of tiles under
// a heading that folds them, the tile on ringed in the accent. groups: [[name, [item…]]] as
// BR_GROUPS, GR_GROUPS, SW_GROUPS (a group named "" shows its tiles without a heading); open: one
// flag a group; current: [group, index]; tileWidth, tileHeight, radius: the tiles; delegate draws a
// tile's inside from its `required property var modelData` (the item), filling its parent. All of it
// comes from the owner; picked(group, index) and toggled(group) ask it.
Column {
    id: g
    property var groups: []
    property var open: []
    property var current: [-1, -1]
    property int tileWidth: 38
    property int tileHeight: 38
    property int radius: 4
    property int indent: 20  // tiles under a heading
    property Component delegate
    signal picked(int group, int index)
    signal toggled(int group)
    spacing: 6

    Repeater {
        model: g.groups
        Column {
            id: grp
            required property var modelData
            required property int index
            readonly property bool named: !!modelData[0]
            readonly property bool shown: !named || !!g.open[index]
            width: g.width
            spacing: 6
            AbstractButton {
                id: head
                visible: grp.named
                width: grp.width
                implicitHeight: 26  // .grp
                text: grp.modelData[0]
                font.pointSize: Theme.pt(12)
                Accessible.role: Accessible.Button
                onClicked: g.toggled(grp.index)
                contentItem: Row {
                    spacing: 6
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: grp.shown ? "expand_more" : "chevron_right"; size: 14; color: Theme.t3 }
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "folder"; size: 16; color: Theme.t3 }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: head.text; font: head.font; color: Theme.t2 }
                }
            }
            Flow {
                visible: grp.shown
                x: grp.named ? g.indent : 0
                width: grp.width - x
                spacing: 5
                Repeater {
                    model: grp.shown ? grp.modelData[1] : []
                    AbstractButton {
                        id: tile
                        required property var modelData
                        required property int index
                        readonly property bool on: g.current[0] === grp.index && g.current[1] === index
                        width: g.tileWidth
                        height: g.tileHeight
                        Accessible.checkable: true
                        Accessible.checked: on
                        onClicked: g.picked(grp.index, index)
                        Component.onCompleted: if (g.delegate) g.delegate.createObject(tile, { modelData: modelData })
                        Rectangle {  // the ring of the tile on, over what the delegate drew
                            z: 1
                            visible: tile.on
                            anchors.fill: parent
                            anchors.margins: -2
                            radius: g.radius + 2
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.accent
                        }
                    }
                }
            }
        }
    }
}
