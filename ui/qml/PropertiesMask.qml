pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// The Properties panel's page of the layer's mask while it takes the brush ("mask", Doc.editMask, U35): Density,
// Feather, Select and Mask…, Color Range…, Invert; at the foot load as a selection, apply, disable, delete.
PropertiesPage {
    id: page
    objectName: "page:mask"
    implicitHeight: mc.implicitHeight + 18 + 32
    ColumnLayout {
        id: mc
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 8
        RowLayout {  // .hd, the layer's name at its right
            spacing: 8
            Layout.preferredHeight: 20
            Icon {
                name: "mask"
                size: 16
                color: Theme.t3
            }
            Text {
                text: qsTr("Layer Mask")
                color: Theme.t2
                font.pointSize: Theme.pt(12)
            }
            Item { Layout.fillWidth: true }
            Text {
                Layout.maximumWidth: 140
                text: page.active.props.name
                color: Theme.t3
                elide: Text.ElideRight
                font.family: Theme.mono
                font.pointSize: Theme.pt(11)
            }
        }
        SliderField {
            Layout.fillWidth: true
            label: qsTr("Density")
            unit: "%"
            value: Math.round(page.active.props.maskDensity * 100)
            onStarted: page.doc.beginGesture()
            onMoved: v => page.run({ op: "setProps", props: { maskDensity: v / 100 } })
            onCommitted: page.doc.endGesture()
        }
        SliderField {
            Layout.fillWidth: true
            Layout.topMargin: 2
            label: qsTr("Feather")
            unit: "px"
            max: 250
            decimals: 1
            step: 0.1
            value: page.active.props.maskFeather
            onStarted: page.doc.beginGesture()
            onMoved: v => page.run({ op: "setProps", props: { maskFeather: v } })
            onCommitted: page.doc.endGesture()
        }
        PropertiesPage.Cap {
            Layout.topMargin: 4
            text: qsTr("Tinh chỉnh")
        }
        Grid {
            columns: 2
            spacing: 6
            Btn {
                width: (mc.width - 6) / 2
                implicitHeight: 26
                text: qsTr("Select and Mask…")
                onClicked: if (page.win && page.win.hasSpace("SelectMask")) page.win.openSpace("SelectMask")
            }
            Btn {
                width: (mc.width - 6) / 2
                implicitHeight: 26
                enabled: !!page.win && page.win.hasDialog("ColorRange")
                text: qsTr("Color Range…")
                onClicked: page.win.openDialog("ColorRange")
            }
            Btn {
                width: (mc.width - 6) / 2
                implicitHeight: 26
                text: qsTr("Invert")
                onClicked: page.run({ op: "mask", action: "inv" })
            }
        }
    }
    PropertiesPage.Foot {
        ToolToggle {
            icon.name: "load_sel"
            ToolTip.text: qsTr("Nạp làm vùng chọn · Load Selection from Mask")
            Accessible.name: qsTr("Nạp Mask làm vùng chọn")
            onClicked: page.run({ op: "select", shape: "skin", name: qsTr("Nạp vùng chọn từ Mask") })
        }
        ToolToggle {
            icon.name: "check"
            ToolTip.text: qsTr("Áp Mask vào layer · Apply Mask")
            Accessible.name: qsTr("Áp Mask vào layer")
            onClicked: page.run({ op: "mask", action: "apply" })
        }
        ToolToggle {
            icon.name: "visibility"
            checked: !!page.active.props.maskOff
            ToolTip.text: qsTr("Tắt Mask · Disable/Enable Mask")
            Accessible.name: qsTr("Tắt Mask")
            onClicked: page.run({ op: "mask", action: "off" })
        }
        ToolToggle {
            icon.name: "delete"
            ToolTip.text: qsTr("Xóa Mask · Delete Mask")
            Accessible.name: qsTr("Xóa Mask")
            onClicked: page.run({ op: "mask", action: "del" })
        }
    }
}
