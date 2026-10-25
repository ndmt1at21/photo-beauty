pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "AdjustSpec.js" as Spec

// The Properties panel's page of an adjustment layer ("adj", U35): its rows (AdjustRows); at the foot clip, view
// the previous state, reset, eye, delete.
PropertiesPage {
    id: page
    objectName: "page:adj"
    implicitHeight: ac.implicitHeight + 20 + 32
    readonly property string adj: page.active.props.adj || ""
    property bool peeking  // Giữ để xem trạng thái trước held (the panel's `peeking`): the mock shows the same image
    ColumnLayout {
        id: ac
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 9
        RowLayout {
            spacing: 8
            Layout.preferredHeight: 24
            Icon {
                name: Spec.ADJ[page.adj] ? Spec.ADJ[page.adj].ic : "tune"
                size: 17
                color: Theme.t2
            }
            Text {  // the kind, as the artboard's and Photoshop's: the layer's name is in Layers
                Layout.fillWidth: true
                text: page.adj
                color: Theme.t1
                elide: Text.ElideRight
                font.weight: Font.DemiBold
                font.pointSize: Theme.pt(12.5)
            }
            ToolToggle {
                icon.name: Spec.ADJ[page.adj] ? Spec.ADJ[page.adj].ic : "tune"
                icon.width: 15
                checked: true
                ToolTip.text: qsTr("Thông số adjustment")
                Accessible.name: qsTr("Thông số adjustment")
            }
            ToolToggle {
                enabled: !!page.active.props.mask
                icon.name: "mask"
                icon.width: 15
                ToolTip.text: qsTr("Layer Mask")
                Accessible.name: qsTr("Layer Mask")
                onClicked: page.doc.setEditMask(true)
            }
        }
        AdjustRows {
            Layout.fillWidth: true
            name: page.adj
            values: Spec.full(page.adj, page.active.props.vals)
            onStarted: page.doc.beginGesture()
            onMoved: v => page.run({ op: "setProps", layer: page.active.id, props: { vals: Spec.changed(page.adj, v) } })
            onCommitted: page.doc.endGesture()
        }
    }
    PropertiesPage.Foot {
        ToolToggle {
            icon.name: "subdirectory_arrow_right"
            checked: !!page.active.props.clip
            ToolTip.text: qsTr("Clip vào layer dưới · Clip to Layer (Ctrl Alt G)")
            Accessible.name: qsTr("Clip vào layer dưới")
            onClicked: page.run({ op: "clippingMask" })
        }
        ToolToggle {
            icon.name: "preview"
            ToolTip.text: qsTr("Giữ để xem trạng thái trước · View Previous State") + " (\\)"  // lupdate drops a "\\" in qsTr
            Accessible.name: qsTr("Giữ để xem trạng thái trước")
            onPressedChanged: page.peeking = pressed
        }
        ToolToggle {
            icon.name: "restart_alt"
            ToolTip.text: qsTr("Đặt lại mặc định · Reset to Adjustment Defaults")
            Accessible.name: qsTr("Đặt lại mặc định")
            onClicked: page.run({ op: "resetAdjustment" })
        }
        ToolToggle {
            icon.name: page.active.props.visible ? "visibility" : "visibility_off"
            ToolTip.text: qsTr("Ẩn hiện layer · Toggle Layer Visibility")
            Accessible.name: qsTr("Ẩn hiện layer")
            onClicked: page.run({ op: "setProps", props: { visible: !page.active.props.visible } })
        }
        ToolToggle {
            icon.name: "delete"
            ToolTip.text: qsTr("Xóa · Delete This Layer")
            Accessible.name: qsTr("Xóa adjustment layer")
            onClicked: page.run({ op: "deleteLayers", layers: [page.active.id] })
        }
    }
}
