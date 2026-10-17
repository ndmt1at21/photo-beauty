pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "ToolSpec.js" as ToolSpec
import "Fmt.js" as Fmt

// The options bar, 36 high (docs/ui-tasks.md, U5; artboard OptionsBars, the options bar of Main.dc.html): the cells of
// the tool's row of ToolSpec.BARS (Free Transform's or Warp's while on), each kept in Tools.values by
// "<bar>:<index>" and, not set yet, at its default (ToolSpec.def()). The brush of a painting bar opens BrushPicker;
// Select and Mask… opens its space, the colour cells the Color Picker, Brush Settings and Character their panels, the
// gradient Gradients; the rest of the buttons run the prototype's actions (Docs.exec()). Rotate View's angle is the
// view's (Doc.view). Popups close at a press outside and keep it from the canvas.
Rectangle {
    id: ob
    readonly property string bar: ToolSpec.barOf(Tools.tool, Tools.xf, Tools.warp)
    readonly property var win: Window.window  // Main: openDialog(), openSpace(), hasSpace()
    readonly property Doc doc: Docs.current

    implicitWidth: 1440
    implicitHeight: 36
    color: Theme.p1
    clip: true
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }

    // Cells the prototype dims by state: Elliptical Marquee's Anti-alias is on, W, H and their swap wait for a Style.
    function dimmed(i, c) {
        if (bar === "marquee" && i === 5)
            return Tools.tool !== "ellip"
        if (bar === "marquee" && i >= 9 && i <= 11)
            return Tools.value("marquee:8", ToolSpec.def("marquee", 8)) === "Normal"
        return c.dis
    }
    // An action of a cell: "=Name" a space or dialog by name, "@side:Panel" a panel, "@…" the prototype's (Docs.exec()).
    function run(act) {
        const w = win
        if (!act)
            return
        if (act[0] === "=") {
            const name = act.slice(1)
            if (w && w.hasSpace && w.hasSpace(name))
                w.openSpace(name, {})
            else if (w && w.hasDialog && w.hasDialog(name))
                w.openDialog(name, {})
        } else if (act.indexOf("@side:") === 0) {
            Tools.showPanel(act.slice(6))
        } else {
            Docs.exec(act)
        }
    }
    function can(act) {
        const w = win
        return act[0] !== "=" || !!(w && w.hasSpace && (w.hasSpace(act.slice(1)) || w.hasDialog(act.slice(1))))
    }
    function pickColor(key, color, title) {
        const w = win
        if (w && w.hasDialog && w.hasDialog("ColorPicker"))
            w.openDialog("ColorPicker", { color: color, title: title, picked: c => Tools.setValue(key, String(c)) })
    }
    // A colour "#rrggbb" with no alpha, as CSS fades to "transparent" in a gradient.
    function noAlpha(hex) { return "rgba(" + [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16)).join(", ") + ", 0)" }
    // A colour of a cell as QML takes it: "none" none, CSS rgba() as #aarrggbb.
    function cssColor(css) {
        const m = /^rgba\((\d+),\s*(\d+),\s*(\d+),\s*([\d.]+)\)$/.exec(css)
        return m ? Qt.rgba(+m[1] / 255, +m[2] / 255, +m[3] / 255, +m[4]) : css === "none" ? "transparent" : css
    }

    RowLayout {
        id: row
        x: 10
        width: ToolSpec.BARS[ob.bar].indexOf("SP") >= 0 ? Math.max(implicitWidth, ob.width - 20) : implicitWidth  // no spacer: packed left
        height: 35
        spacing: 10
        Repeater {
            model: ToolSpec.cells(ob.bar)
            delegate: Slot {}
        }
    }

    // A cell: its kind's component, with the cell (modelData), its index in the row and its key in Tools.values.
    component Slot: Loader {
        required property var modelData
        required property int index
        readonly property string key: ob.bar + ":" + index
        readonly property var value: Tools.value(key, modelData.def)
        readonly property bool off: ob.dimmed(index, modelData)
        Layout.fillWidth: modelData.kind === "SP"
        Layout.minimumWidth: modelData.kind === "SP" ? 0 : implicitWidth
        sourceComponent: ({ ic: icC, "|": ruleC, L: labelC, D: dropC, F: fieldC, FC: fieldC, IF: fieldC, T: toggleC, B: toggleC, C: checkC, BT: btnC,
            BI: btnC, M: segC, S: segC, S2: segC, GT: segC, BP: brushC, SW: swatchC, G: gradC, AN: anchorC, SP: null })[modelData.kind] || null
    }

    Component {
        id: icC
        Item {
            implicitWidth: 28
            implicitHeight: 24
            ToolTip.visible: hh.hovered
            ToolTip.delay: 600
            ToolTip.text: Tools.xf ? (Tools.warp ? qsTr("Warp") : qsTr("Free Transform")) : ToolSpec.tip(Tools.tool, Prefs.value("tooltipKeys") !== false)
            HoverHandler { id: hh }
            Icon {
                anchors.centerIn: parent
                name: Tools.xf ? (Tools.warp ? "grid_on" : "crop_free") : ToolSpec.tool(Tools.tool).icn
                color: Theme.t1
            }
        }
    }
    Component {
        id: ruleC
        Rectangle { implicitWidth: 1; implicitHeight: 20; color: Theme.ln }
    }
    Component {
        id: labelC
        Text {
            readonly property Slot slot: parent as Slot
            text: slot.modelData.text
            color: slot.off ? Theme.t4 : Theme.t2
            font.pointSize: Theme.pt(12)
        }
    }
    Component {
        id: dropC
        Dropdown {
            readonly property Slot slot: parent as Slot
            readonly property bool aa: slot.modelData.listName === "aa"  // Type's anti-aliasing is Tools.antiAlias
            implicitWidth: slot.modelData.w
            enabled: !slot.off
            focusPolicy: Qt.TabFocus
            options: slot.modelData.list
            value: aa ? "aA " + Tools.antiAlias : slot.value
            Accessible.name: slot.modelData.tip
            popup.modal: true
            popup.dim: false
            onPicked: v => {
                if (aa)
                    Tools.antiAlias = String(v).replace("aA ", "")
                else
                    Tools.setValue(slot.key, v)
            }
        }
    }
    Component {
        id: fieldC
        Item {
            id: fc
            readonly property Slot slot: parent as Slot
            readonly property var c: slot.modelData
            readonly property bool view: ob.bar === "rot"  // Rotate View's angle: the view's
            readonly property real v: {
                const x = view ? (ob.doc ? ob.doc.view.angle : 0) : slot.value
                return typeof x === "number" ? x : Fmt.pnum(String(x ?? "")) ?? NaN
            }
            implicitWidth: (c.kind === "IF" ? 20 : 0) + nf.implicitWidth - 64 + c.w
            implicitHeight: 24
            enabled: !slot.off
            Icon {
                visible: fc.c.kind === "IF"
                anchors.verticalCenter: parent.verticalCenter
                name: fc.c.icon || ""
                size: 16
                color: Theme.t3
            }
            NumberField {
                id: nf
                x: fc.c.kind === "IF" ? 20 : 0
                width: parent.width - x
                anchors.verticalCenter: parent.verticalCenter
                label: fc.c.kind === "IF" ? "" : fc.c.text
                labelInside: false
                value: isNaN(fc.v) ? 0 : fc.v
                unit: fc.c.unit
                decimals: fc.c.decimals
                min: fc.c.min
                max: fc.c.max
                defaultValue: isNaN(fc.c.def) ? NaN : fc.c.def
                slider: fc.c.kind === "FC"
                sliderMin: fc.c.min
                sliderMax: fc.c.max
                Accessible.name: fc.c.tip
                onMoved: x => {
                    if (fc.view)
                        ob.doc?.setView({ angle: x })
                    else
                        Tools.setValue(fc.slot.key, x)
                }
            }
            Rectangle {  // an empty cell ("" as Xóa W, H leaves Crop's): no number until one is typed
                objectName: "empty"
                visible: isNaN(fc.v) && !nf.editing
                x: parent.width - fc.c.w + 2
                y: (parent.height - height) / 2
                width: fc.c.w - 4 - (nf.slider ? 20 : 0)
                height: 20
                color: Theme.p2
            }
        }
    }
    Component {
        id: toggleC
        ToolToggle {
            readonly property Slot slot: parent as Slot
            readonly property var c: slot.modelData
            enabled: !slot.off
            focusPolicy: Qt.TabFocus
            icon.name: c.icon
            checked: c.kind === "T" && (c.act === "@warp" ? Tools.warp : !!slot.value)
            ToolTip.text: c.tip
            Accessible.name: c.tip
            onClicked: {
                if (c.kind === "T" && !c.act)
                    Tools.setValue(slot.key, !checked)
                else
                    ob.run(c.act)
            }
        }
    }
    Component {
        id: checkC
        CheckRow {
            readonly property Slot slot: parent as Slot
            enabled: !slot.off
            focusPolicy: Qt.TabFocus
            text: slot.modelData.text
            checked: !!slot.value
            onClicked: Tools.setValue(slot.key, !checked)
        }
    }
    Component {
        id: btnC
        Btn {
            readonly property Slot slot: parent as Slot
            readonly property var c: slot.modelData
            implicitHeight: 24
            leftPadding: c.kind === "BI" ? 6 : 10
            rightPadding: 10
            enabled: !slot.off && ob.can(c.act)
            focusPolicy: Qt.TabFocus
            text: c.text
            icon.name: c.icon || ""
            onClicked: {
                if (c.act)
                    ob.run(c.act)
                else if (ob.bar === "rot")  // Đặt lại view, with no action in the prototype: the view back to 0°
                    ob.doc?.setView({ angle: 0 })
            }
        }
    }
    Component {
        id: segC
        Segmented {
            readonly property Slot slot: parent as Slot
            enabled: !slot.off
            options: slot.modelData.options
            current: slot.value
            Accessible.name: slot.modelData.tip
            onPicked: i => Tools.setValue(slot.key, i)
        }
    }
    Component {
        id: brushC
        BrushPicker { bar: ToolSpec.brushBar(Tools.tool, Tools.xf) }
    }
    Component {
        id: swatchC
        Swatch {
            readonly property Slot slot: parent as Slot
            readonly property string css: String(slot.value)
            implicitWidth: 30
            implicitHeight: 22
            radius: 4
            ring: Theme.t4
            focusPolicy: Qt.TabFocus
            none: css === "none"
            color: ob.cssColor(css)
            ToolTip.text: slot.modelData.tip
            Accessible.name: qsTr("%1: mở Color Picker").arg(slot.modelData.tip)
            onClicked: {
                const t = slot.modelData.tip
                ob.pickColor(slot.key, css === "none" ? "#ffffff" : css, t.charAt(0).toLowerCase() + t.slice(1))
            }
        }
    }
    Component {
        id: gradC
        AbstractButton {  // the gradient in use; Gradients holds the presets
            implicitWidth: 120
            implicitHeight: 24
            leftPadding: 3
            rightPadding: 2
            focusPolicy: Qt.TabFocus
            hoverEnabled: true
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Bảng preset gradient")
            Accessible.name: qsTr("Gradient đang dùng: mở panel Gradients")
            onClicked: Tools.showPanel("Gradients")
            background: Rectangle { radius: 4; color: Theme.p2 }
            contentItem: RowLayout {
                spacing: 6
                Canvas {  // the colours of the gradient's CSS, left to right
                    readonly property var stops: {
                        const c = Tools.gradientCss().match(/#[0-9a-fA-F]{6}|transparent/g) || ["#ffffff", "#000000"]
                        return c.map((x, i) => x !== "transparent" ? x : ob.noAlpha(c[i ? i - 1 : i + 1] || "#ffffff"))
                    }
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    onStopsChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onAvailableChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d"), g = ctx.createLinearGradient(0, 0, width, 0)
                        ctx.reset()
                        stops.forEach((x, i) => g.addColorStop(stops.length > 1 ? i / (stops.length - 1) : 0, x))
                        ctx.fillStyle = g
                        ctx.beginPath()
                        ctx.roundedRect(0, 0, width, height, 3, 3)
                        ctx.fill()
                    }
                }
                Icon { name: "expand_more"; size: 16; color: Theme.t3 }
            }
        }
    }
    Component {
        id: anchorC
        AnchorGrid {
            readonly property Slot slot: parent as Slot
            compact: true
            anchor: slot.value
            onPicked: i => Tools.setValue(slot.key, i)
        }
    }
}
