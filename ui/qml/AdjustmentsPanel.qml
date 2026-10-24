pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "AdjustSpec.js" as Spec

// The Adjustments panel (docs/ui-tasks.md, U10; PanelsA, the adjGrid part of Main.dc.html), a tab of the dock's group
// Properties, Adjustments, Retouch: the 16 adjustments in four columns; a click adds that adjustment layer above the
// active layer (one History step, "Layer Curves mới") and shows it in Properties, as the prototype's addAdj().
Item {
    id: panel
    property var menu: []  // the items of the … of its tab strip (PopupMenu's): none, as the prototype's
    readonly property Doc doc: Docs.current
    implicitWidth: 308
    implicitHeight: grid.implicitHeight + 16

    Grid {
        id: grid
        x: 8
        y: 8
        columns: 4
        spacing: 2
        Repeater {
            model: Spec.ORDER
            AbstractButton {
                id: b
                required property string modelData
                width: (panel.width - 22) / 4
                height: 58
                enabled: !!panel.doc && panel.doc.supports("newAdjustmentLayer")  // greyed on a real document (decision 4)
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Thêm adjustment layer %1").arg(b.modelData)
                Accessible.name: ToolTip.text
                onClicked: {
                    if (panel.doc.run({ op: "newAdjustmentLayer", adjustment: b.modelData }))
                        Tools.showPanel("Properties")
                }
                background: Rectangle {
                    radius: 6
                    color: b.hovered || b.down ? Theme.sel : "transparent"
                }
                contentItem: Item {
                    Column {  // gap 5 in CSS's line box of 1.35: 2 between Qt's tighter one
                        anchors.centerIn: parent
                        spacing: 2
                        Icon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            name: Spec.ADJ[b.modelData].ic
                            size: 20
                            color: b.enabled ? Theme.t2 : Theme.t4
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.min(implicitWidth, 68)
                            text: Spec.ADJ[b.modelData].sh
                            color: b.enabled ? Theme.t2 : Theme.t4
                            elide: Text.ElideRight
                            font.pointSize: Theme.pt(10.5)
                        }
                    }
                }
            }
        }
    }
}
