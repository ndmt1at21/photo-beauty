pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "AdjustSpec.js" as Spec
import "Fmt.js" as Fmt

// The rows of one adjustment (docs/ui-tasks.md, U10), built from its DSL in AdjustSpec.js: one builder for Properties
// (an adjustment layer) and the Adjust dialog (Image › Adjustments). `name` is the adjustment's ("Curves"), `values` all
// of them (Spec.full()). A change comes as the shared controls' started(), moved(values)… (the values after it),
// committed(): a drag (a slider, a curve point, a triangle, a handle, a field's label) sends many moved(), anything else
// the three at once, so the owner makes one History step of each. The eyedroppers and the on-image tool (TOOLS) switch
// `tool` only: the mock has no pixels to pick from. Swatches open the Color Picker (U11) through the window (Main), the
// Gradient Map's gradient the Gradients panel.
ColumnLayout {
    id: rows
    property string name
    property var values: Spec.defaults(name)
    property string tool  // the TOOLS button down ("b": the black point's eyedropper), "" for none
    property int point: -1  // the curve point picked
    readonly property var spec: Spec.rows(name)
    readonly property var curve: Spec.curvePoints(values, values.ch || "RGB")
    // The point Input and Output show: the one picked, else the first between the ends, else the black end.
    readonly property int shownPoint: point >= 0 && point < curve.length ? point : curve.length > 2 ? 1 : 0
    signal started()
    signal moved(var values)
    signal committed()

    spacing: 9

    function set(patch) { moved(Object.assign({}, values, patch)) }
    // A value, its row's default while `values` are another adjustment's (Properties going from one layer to the next).
    function val(k, def) { return values[k] === undefined ? def : values[k] }
    function change(patch) {
        started()
        set(patch)
        committed()
    }
    // A number field of FLDS and LF: its value, or the picked curve point's input or output (kept between its neighbours).
    function field(k) { return Spec.POINT[k] === undefined ? values[k] : curve[shownPoint][Spec.POINT[k]] }
    function setField(k, v) {
        if (Spec.POINT[k] === undefined) {
            set({ [k]: v })
            return
        }
        const c = curve, i = shownPoint, q = c[i].slice(), p = c.slice()
        if (k === "cin") q[0] = Math.max(i ? c[i - 1][0] + 1 : 0, Math.min(i < c.length - 1 ? c[i + 1][0] - 1 : 255, v))
        else q[1] = v
        p[i] = q
        setCurve(p)
    }
    function setCurve(p) { set({ pts: Object.assign({}, values.pts, { [values.ch || "RGB"]: p }) }) }
    function pick(k, title) {
        const w = rows.Window.window as Main
        if (w && w.hasDialog("ColorPicker"))
            w.openDialog("ColorPicker", { color: values[k], title: title, picked: c => rows.change({ [k]: String(c) }) })
    }

    component Dim: Text {
        color: Theme.t3
        font.pointSize: Theme.pt(12)
    }

    Repeater {
        model: rows.spec
        Loader {
            id: row
            required property var modelData
            readonly property var r: modelData
            objectName: "row:" + r.k
            Layout.fillWidth: true
            sourceComponent: ({ PRE: pre, DDL: ddl, BTN: btn, S: slider, CHK: chk, SEG: seg, TOOLS: tools, NOTE: note, HIST: hist, OUT: out, FLDS: flds,
                                LF: lf, CURVE: curveBox, RANGE: range, GRAD: grad, RADIO: radio, TINT: tint, TOT: tot })[r.k] || null

            Component {
                id: pre
                Dropdown {
                    prefix: qsTr("Preset: ")
                    options: row.r.list
                    value: rows.values.pre
                    Accessible.name: qsTr("Preset")
                    onPicked: v => {
                        rows.point = -1
                        rows.change(Spec.presetValues(rows.name, v))
                    }
                }
            }
            Component {
                id: ddl
                RowLayout {
                    spacing: 8
                    Dim {
                        visible: row.r.label !== ""
                        Layout.minimumWidth: 64
                        text: row.r.label
                    }
                    Rectangle {  // Selective Color's colour
                        visible: row.r.key === "col"
                        implicitWidth: 8
                        implicitHeight: 8
                        radius: 4
                        color: Spec.SC_DOT[rows.values.col] || "#ef5350"
                    }
                    Dropdown {
                        Layout.fillWidth: true
                        options: row.r.list
                        value: rows.values[row.r.key]
                        Accessible.name: row.r.label || qsTr("Kênh")
                        onPicked: v => {
                            rows.point = -1
                            rows.change(row.r.key === "rng" ? { rng: v, span: null } : { [row.r.key]: v })
                        }
                    }
                    Btn {
                        visible: row.r.auto
                        implicitHeight: 24
                        leftPadding: 12
                        rightPadding: 12
                        text: qsTr("Auto")
                        onClicked: rows.change(Spec.AUTO[rows.name] || {})
                    }
                    ToolToggle {  // Auto Color Correction Options: no such dialog yet
                        visible: row.r.options
                        enabled: false
                        icon.name: "settings"
                        icon.width: 16
                        ToolTip.text: qsTr("Tùy chọn Auto · Auto Options")
                        Accessible.name: qsTr("Tùy chọn Auto")
                    }
                }
            }
            Component {
                id: btn
                Item {
                    implicitHeight: 24
                    Btn {
                        implicitHeight: 24
                        leftPadding: 12
                        rightPadding: 12
                        text: row.r.label
                        onClicked: rows.change(Spec.AUTO[rows.name] || {})
                    }
                }
            }
            Component {
                id: slider
                SliderField {
                    label: row.r.label
                    value: rows.val(row.r.key, row.r.def)
                    min: row.r.min
                    max: row.r.max
                    decimals: row.r.dec
                    step: Math.pow(10, -row.r.dec)
                    unit: row.r.unit
                    track: row.r.track
                    valueText: (min < 0 && !Spec.PLAIN[rows.name] ? Fmt.sg(value, decimals) : Fmt.vn(value, decimals)) + Fmt.suffix(unit)
                    Accessible.name: row.r.label
                    onStarted: rows.started()
                    onMoved: v => rows.set({ [row.r.key]: v })
                    onCommitted: rows.committed()
                }
            }
            Component {
                id: chk
                CheckRow {
                    implicitHeight: 16  // the prototype's label, a line of text
                    text: row.r.label
                    checked: !!rows.values[row.r.key]
                    onClicked: rows.change({ [row.r.key]: !rows.values[row.r.key] })
                }
            }
            Component {
                id: seg
                Segmented {
                    fill: true
                    options: row.r.list
                    current: rows.val(row.r.key, row.r.def)
                    onPicked: i => rows.change({ [row.r.key]: i })
                }
            }
            Component {
                id: tools
                Row {
                    spacing: 2
                    Repeater {
                        model: row.r.list
                        ToolToggle {
                            id: tt
                            required property string modelData
                            readonly property var a: Spec.ATOOLS[tt.modelData]
                            icon.name: tt.a[0]
                            icon.width: 16
                            checked: rows.tool === tt.modelData
                            ToolTip.text: tt.a[1]
                            Accessible.name: tt.a[1].split(" · ")[0]
                            onClicked: rows.tool = tt.checked ? "" : tt.modelData
                            Rectangle {  // the colour the eyedropper sets
                                visible: tt.a[2].charAt(0) === "#"
                                x: 16
                                y: 14
                                width: 8
                                height: 8
                                radius: 2
                                color: visible ? tt.a[2] : "transparent"
                                border.color: Theme.t3
                            }
                            Text {  // + −: adds to the range, takes from it
                                visible: tt.a[2] === "+" || tt.a[2] === "−"
                                anchors.right: parent.right
                                anchors.rightMargin: 2
                                anchors.bottom: parent.bottom
                                text: tt.a[2]
                                color: Theme.t1
                                font.family: Theme.mono
                                font.pointSize: Theme.pt(10)
                            }
                        }
                    }
                }
            }
            Component {
                id: note
                Text {
                    text: row.r.label
                    color: Theme.t3
                    wrapMode: Text.Wrap
                    font.pointSize: Theme.pt(12)
                }
            }
            Component {
                id: hist
                LevelsBar {
                    id: lv
                    readonly property bool levels: row.r.mode === "levels"
                    histogram: Spec.HIST_D
                    stops: lv.levels ? [rows.val("in0", 0), Spec.midOf(rows.val("in0", 0), rows.val("in1", 255), rows.val("gam", 1)), rows.val("in1", 255)] : [rows.val("t", 128)]
                    fills: lv.levels ? ["#000000", "#808080", "#ffffff"] : ["#808080"]
                    onStarted: rows.started()
                    onMoved: (i, x) => rows.set(lv.levels ? Spec.levelsAt(rows.values, i, x) : { t: Math.max(1, x) })
                    onCommitted: rows.committed()
                }
            }
            Component {
                id: out
                LevelsBar {
                    stops: [rows.val("out0", 0), rows.val("out1", 255)]
                    fills: ["#000000", "#ffffff"]
                    onStarted: rows.started()
                    onMoved: (i, x) => rows.set(i ? { out1: x } : { out0: x })
                    onCommitted: rows.committed()
                }
            }
            Component {
                id: flds
                Item {  // 64 px fields, the first at the left, the last at the right
                    id: fl
                    implicitHeight: 24
                    Repeater {
                        model: row.r.fields
                        NumberField {
                            id: nf
                            required property var modelData
                            required property int index
                            readonly property var lim: Spec.limits(nf.modelData.key, rows.values)
                            x: nf.index * (fl.width - 64) / Math.max(1, row.r.fields.length - 1)
                            width: 64
                            value: rows.field(nf.modelData.key) ?? nf.modelData.def
                            decimals: Spec.FIELDS[nf.modelData.key][2]
                            step: Math.pow(10, -nf.decimals)
                            min: nf.lim[0]
                            max: nf.lim[1]
                            Accessible.name: Spec.FIELDS[nf.modelData.key][3]
                            onStarted: rows.started()
                            onMoved: v => rows.setField(nf.modelData.key, v)
                            onCommitted: rows.committed()
                        }
                    }
                }
            }
            Component {
                id: lf
                RowLayout {
                    spacing: 8
                    Repeater {
                        model: row.r.fields
                        NumberField {
                            id: lfn
                            required property var modelData
                            readonly property var lim: Spec.limits(lfn.modelData.key, rows.values)
                            Layout.fillWidth: true
                            Layout.preferredWidth: 100  // equal shares
                            label: lfn.modelData.label
                            labelColor: Theme.t3
                            value: rows.field(lfn.modelData.key) ?? lfn.modelData.def
                            min: lfn.lim[0]
                            max: lfn.lim[1]
                            Accessible.name: lfn.modelData.label
                            onStarted: rows.started()
                            onMoved: v => rows.setField(lfn.modelData.key, v)
                            onCommitted: rows.committed()
                        }
                    }
                }
            }
            Component {
                id: curveBox
                CurveBox {
                    points: rows.curve
                    selected: rows.shownPoint
                    histogram: Spec.CURVE_HIST_D
                    ink: ["Red", "Green", "Blue"].indexOf(rows.values.ch) < 0 ? Theme.t1 : Theme.hist[["Red", "Green", "Blue"].indexOf(rows.values.ch)]
                    Accessible.name: qsTr("Đường cong %1, preset %2").arg(rows.values.ch).arg(rows.values.pre)
                    onPicked: i => rows.point = i
                    onStarted: rows.started()
                    onMoved: p => rows.setCurve(p)
                    onCommitted: rows.committed()
                }
            }
            Component {
                id: range
                HueRange {
                    span: rows.values.rng === "Master" ? null : rows.values.span || Spec.SPANS[rows.values.rng] || null
                    centre: Spec.HUES[rows.values.rng] ?? 180
                    shift: rows.val("h", 0)
                    onStarted: rows.started()
                    onMoved: s => rows.set({ span: s })
                    onCommitted: rows.committed()
                }
            }
            Component {
                id: grad
                AbstractButton {  // .dd 30 high: the gradient, then the chevron
                    id: gb
                    implicitHeight: 30
                    padding: 0
                    hoverEnabled: true
                    Accessible.name: qsTr("Gradient của Gradient Map, mở panel Gradients")
                    onClicked: Tools.showPanel("Gradients")
                    background: Rectangle {
                        radius: 4
                        color: Theme.p2
                    }
                    contentItem: Item {
                        Row {  // a two-colour piece between each pair of its stops
                            x: 4
                            y: 4
                            Repeater {
                                model: Math.max(0, row.r.colors.length - 1)
                                Rectangle {
                                    id: gp
                                    required property int index
                                    width: (gb.width - 26) / (row.r.colors.length - 1)
                                    height: 22
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: row.r.colors[gp.index] }
                                        GradientStop { position: 1; color: row.r.colors[gp.index + 1] }
                                    }
                                }
                            }
                        }
                        Icon {
                            x: parent.width - 16
                            anchors.verticalCenter: parent.verticalCenter
                            name: "expand_more"
                            size: 14
                            color: gb.hovered ? Theme.t1 : Theme.t3
                        }
                    }
                }
            }
            Component {
                id: radio
                RowLayout {
                    id: ra
                    readonly property bool on: rows.values[row.r.key] === row.r.index
                    readonly property string k: row.r.key + row.r.index  // its colour or its pick
                    spacing: 8
                    Radio {
                        Layout.minimumWidth: 96
                        text: row.r.label
                        checked: ra.on
                        onClicked: rows.change({ [row.r.key]: row.r.index })
                    }
                    Swatch {
                        visible: !row.r.list
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 22
                        radius: 4
                        color: rows.val(ra.k, row.r.def)
                        ring: Theme.t4
                        ToolTip.text: qsTr("Mở Color Picker")
                        Accessible.name: qsTr("Chọn màu %1").arg(row.r.label)
                        onClicked: rows.pick(ra.k, row.r.label)
                    }
                    Dropdown {
                        visible: !!row.r.list
                        Layout.fillWidth: true
                        opacity: ra.on ? 1 : 0.5
                        options: row.r.list || []
                        value: rows.values[ra.k]
                        Accessible.name: row.r.label
                        onPicked: v => rows.change({ [ra.k]: v })
                    }
                    Item {
                        visible: !row.r.list
                        Layout.fillWidth: true
                    }
                }
            }
            Component {
                id: tint
                Row {
                    spacing: 10
                    CheckRow {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Tint")
                        checked: !!rows.values.tint
                        onClicked: rows.change({ tint: !rows.values.tint })
                    }
                    Swatch {
                        width: 34
                        height: 22
                        radius: 4
                        color: rows.val("tintColor", "#c8a27c")
                        ring: Theme.t4
                        enabled: !!rows.values.tint
                        opacity: enabled ? 1 : 0.5
                        ToolTip.text: qsTr("Mở Color Picker")
                        Accessible.name: qsTr("Chọn màu Tint")
                        onClicked: rows.pick("tintColor", qsTr("Tint"))
                    }
                }
            }
            Component {
                id: tot
                RowLayout {
                    Dim { text: row.r.label }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: Fmt.sg((rows.values.r || 0) + (rows.values.g || 0) + (rows.values.b || 0), 0) + "%"
                        color: Theme.t1
                        font.family: Theme.mono
                        font.pointSize: Theme.pt(12)
                    }
                }
            }
        }
    }
}
