import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// A page of the Properties panel (U35: each page a file, PropertiesDocument, PropertiesPixel…, so that the tasks wiring
// them to the core work side by side; PropertiesPanel shows the one `page` names). What the panel sets on it: `doc`
// (Docs.current), `active` (the active layer) and `cur` (the document), stand-ins while there are none, as a page's
// bindings may run once more as it goes. What the pages share: the helpers below and the parts used as
// PropertiesPage.Cap, .Dim, .Hd, .Foot, .Size. Shared by every page: a task changing this file asks the UI lead first
// (build/orch/api/u35.md). No `pragma ComponentBehavior: Bound` here: a bound file's inline components cannot be made
// in another file.
Item {
    id: page
    required property Doc doc
    required property var active  // { id, kind, props }; without a layer { id: -1, kind: "", props: Mock.PROPS }
    required property var cur  // the document; without one { w, h, ppi, mode, bits, profile } of a 1 × 1 at 72 ppi
    readonly property real half: (width - 60) / 2  // one field of a pair (.g2: 12 in, 6 + 24 + 6 between)
    readonly property Main win: Window.window as Main  // openDialog(), openSpace(); null outside Main

    function run(cmd) { return !!doc && doc.run(cmd) }
    // A field of the layer the prototype keeps out of the document (its v: "L:<id>:w"…), in Tools.values: no step.
    function lf(k, def) { return Tools.value("L:" + active.id + ":" + k, def) }
    function setLf(k, v) { Tools.setValue("L:" + active.id + ":" + k, v) }
    // The Color Picker (U11) on a colour; nothing while it is not in the module.
    function pickColor(color, title, picked) {
        if (win && win.hasDialog("ColorPicker"))
            win.openDialog("ColorPicker", { color: String(color), title: title, picked: picked })
    }

    component Cap: Text {  // .cap
        color: Theme.t3
        font.pointSize: Theme.pt(10.5)
        font.weight: Font.Medium
        font.letterSpacing: 0.63
        font.capitalization: Font.AllUppercase
        // CSS's line box (line-height 1.35), whole as layouts round heights up: Qt's 15 pushed the rows under each one
        Layout.preferredHeight: Math.round(10.5 * 1.35 * Theme.fontScale)
    }
    component Dim: Text {
        color: Theme.t3
        font.pointSize: Theme.pt(12)
    }
    component Hd: RowLayout {  // .hd: an icon, what is shown
        property alias icon: hdIcon.name
        property alias text: hdText.text
        spacing: 8
        Layout.preferredHeight: 20
        Icon {
            id: hdIcon
            size: 16
            color: Theme.t3
        }
        Text {
            id: hdText
            Layout.fillWidth: true
            color: Theme.t2
            elide: Text.ElideRight
            font.pointSize: Theme.pt(12)
        }
    }
    component Foot: Item {  // .foot: 32 high, a rule over its buttons at the right
        default property alias buttons: footRow.data
        anchors.bottom: parent.bottom
        width: parent.width
        height: 32
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.ln
        }
        Row {
            id: footRow
            anchors.right: parent.right
            anchors.rightMargin: 8
            y: 4
            spacing: 2
        }
    }
    component Size: Column {  // W (link) H over X Y, the prototype's .g2 of a layer: fields kept out of the document
        id: sf
        required property PropertiesPage host  // the page it is on
        // W, H, X, Y as the prototype starts a layer's (PanelsA; a pixel layer, a group: the document's size at 0, 0)
        readonly property var def: ({ so: [2100, 1312, 190, 2040], text: [1840, 236, 320, 410], shape: [1200, 800, 640, 1900] })[sf.host.active.kind]
                                   || [sf.host.cur.w, sf.host.cur.h, 0, 0]
        spacing: 6
        Row {
            spacing: 6
            NumberField {
                width: sf.host.half
                label: "W"
                labelColor: Theme.t3
                unit: "px"
                min: 1
                value: sf.host.lf("w", sf.def[0])
                Accessible.name: qsTr("Chiều rộng")
                onMoved: v => sf.host.setLf("w", v)
            }
            Item {
                width: 24
                height: 24
                ToolToggle {
                    visible: sf.host.active.kind !== "text"
                    width: 24
                    icon.name: "link"
                    icon.width: 16
                    checked: sf.host.lf("link", true)
                    ToolTip.text: qsTr("Giữ tỉ lệ · Link width and height")
                    Accessible.name: qsTr("Giữ tỉ lệ W và H")
                    onClicked: sf.host.setLf("link", !checked)
                }
            }
            NumberField {
                width: sf.host.half
                label: "H"
                labelColor: Theme.t3
                unit: "px"
                min: 1
                value: sf.host.lf("h", sf.def[1])
                Accessible.name: qsTr("Chiều cao")
                onMoved: v => sf.host.setLf("h", v)
            }
        }
        Row {
            spacing: 6
            NumberField {
                width: sf.host.half
                label: "X"
                labelColor: Theme.t3
                unit: "px"
                value: sf.host.lf("x", sf.def[2])
                Accessible.name: qsTr("Vị trí X")
                onMoved: v => sf.host.setLf("x", v)
            }
            Item {
                width: 24
                height: 24
            }
            NumberField {
                width: sf.host.half
                label: "Y"
                labelColor: Theme.t3
                unit: "px"
                value: sf.host.lf("y", sf.def[3])
                Accessible.name: qsTr("Vị trí Y")
                onMoved: v => sf.host.setLf("y", v)
            }
        }
    }
}
