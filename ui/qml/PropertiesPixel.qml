pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "AdjustSpec.js" as Spec

// The Properties panel's page of a pixel layer or a group ("px", U35): size, place, angle, flips, alignment.
PropertiesPage {
    id: page
    objectName: "page:px"
    implicitHeight: pc.implicitHeight + 18
    ColumnLayout {
        id: pc
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 8
        RowLayout {
            spacing: 8
            PropertiesPage.Hd {
                Layout.fillWidth: true
                Layout.preferredHeight: 18  // Main.dc.html's: the page 162 high, as the dock was measured
                icon: page.active.kind === "group" ? "folder" : "image"
                text: page.active.kind === "group" ? qsTr("Nhóm layer") : qsTr("Layer điểm ảnh")
            }
            PropertiesPage.Cap { text: qsTr("Biến đổi") }
        }
        PropertiesPage.Size { host: page }
        RowLayout {
            spacing: 4
            NumberField {
                Layout.preferredWidth: 104
                label: qsTr("Góc")
                labelColor: Theme.t3
                unit: "°"
                decimals: 2
                min: -180
                max: 180
                value: page.lf("a", 0)
                Accessible.name: qsTr("Góc xoay")
                onMoved: v => page.setLf("a", v)
            }
            ToolToggle {
                icon.name: "flip"
                icon.width: 16
                ToolTip.text: qsTr("Lật ngang · Flip Horizontal")
                Accessible.name: qsTr("Lật ngang")
                onClicked: page.run({ op: "flipLayer", axis: "h" })
            }
            ToolToggle {
                icon.name: "flip"
                icon.width: 16
                contentItem.rotation: 90
                ToolTip.text: qsTr("Lật dọc · Flip Vertical")
                Accessible.name: qsTr("Lật dọc")
                onClicked: page.run({ op: "flipLayer", axis: "v" })
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            spacing: 2
            PropertiesPage.Cap {
                Layout.preferredWidth: 42
                text: qsTr("Căn")
            }
            Repeater {
                model: Spec.ALIGN
                ToolToggle {
                    id: al
                    required property var modelData
                    icon.name: al.modelData[0]
                    icon.width: 16
                    ToolTip.text: al.modelData[1]
                    Accessible.name: al.modelData[1]
                    onClicked: page.run({ op: "step", name: al.modelData[1], icon: al.modelData[0] })
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
}
