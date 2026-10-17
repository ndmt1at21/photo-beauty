pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import "Color.js" as Color

// Gradient bar with stops (Controls: GradientBar; docs/ui-tasks.md, U11): opacity stops over the bar (black is 100%),
// colour stops under it, a diamond for each midpoint beside the stop picked. A click under the bar adds a colour stop,
// over it an opacity stop, as the gradient is there; drag a stop along, or away from the bar to delete it (two of a kind
// stay), a diamond to move its midpoint; double-click a colour stop, or click its Màu cell, for the Color Picker (the
// window's openDialog, Main's). Under the bar: Vị trí, the colour or Độ mờ of the stop picked, Xóa điểm; ← → move that
// stop (Shift 10%), Delete deletes it. value: Color.gradient()'s { colors: [{ p, c: [r, g, b], m }], alphas: [{ p, a, m }] },
// each list sorted; value protocol of the shared controls: started(), moved(v)…, committed(v).
Item {
    id: gb
    property var value: Color.gradient("linear-gradient(90deg, #000000, #ffffff)")
    property string kind: "c"  // the stop picked: "c" colour, "a" opacity, and its index
    property int index: 0
    readonly property string list: kind === "c" ? "colors" : "alphas"
    readonly property var stops: value[list]
    readonly property int at: Math.max(0, Math.min(index, stops.length - 1))
    readonly property var stop: stops[Math.max(0, Math.min(index, stops.length - 1))]  // not through `at`: it may lag behind stops
    readonly property real w: width - 16  // the bar, 8 px in from each end
    signal started()
    signal moved(var v)
    signal committed(var v)

    implicitWidth: 300
    implicitHeight: 110

    function px(p) { return 8 + p * w }
    function pos(x) { return Color.clamp((x - 8) / w, 0, 1) }
    function mid(l, j) { return l[j].p + (l[j + 1].p - l[j].p) * l[j].m }
    // [v0 with stop i of list k (i -1: none) taken out and s (null: none) put in its place, where s or the stop after went]
    function place(v0, k, i, s) {
        const l = v0[k].filter((x, j) => j !== i)
        if (s)
            l.push(s)
        const sl = Color.sorted(l), v = Object.assign({}, v0)
        v[k] = sl
        return [v, s ? sl.indexOf(s) : Math.max(0, Math.min(i, sl.length - 1))]
    }
    function change(k, i, patch) {  // the value with stop i of list k changed, which stays picked
        const r = place(value, k, i, Object.assign({}, value[k][i], patch))
        kind = k === "colors" ? "c" : "a"
        index = r[1]
        return r[0]
    }
    function removed() {
        const r = place(value, list, at, null)
        index = r[1]
        return r[0]
    }

    QtObject {
        id: st
        property string mode  // "stop", "mid" while the mouse drags one, "field" while a field moves
        property var v0  // the value as the drag began
        property string k
        property int i
        property real dx  // from the stop to the mouse
        property bool on  // the value changed
        property var last
    }
    function go(v) {
        if (!st.on) {
            st.on = true
            started()
        }
        st.last = v
        moved(v)
    }
    function end() {
        st.mode = ""
        if (!st.on) return
        st.on = false
        committed(st.last)
    }
    function once(v) {
        started()
        moved(v)
        committed(v)
    }
    // under the mouse: { k, i } a stop, { k, i, mid } the diamond after stop i, { k, i: -1 } room for one; null the bar
    function hit(x, y) {
        const k = y < 18 ? "alphas" : y > 44 ? "colors" : ""
        if (!k) return null
        const l = value[k]
        let best = -1, d = 8
        l.forEach((s, j) => {
            const e = Math.abs(px(s.p) - x)
            if (e < d) {
                d = e
                best = j
            }
        })
        if (best >= 0) return { k: k, i: best }
        if (k === list && (k === "alphas" || y < 58))  // a diamond, over the colour stops' row
            for (const j of [at - 1, at])
                if (j >= 0 && j < l.length - 1 && Math.abs(px(mid(l, j)) - x) < 6) return { k: k, i: j, mid: true }
        return { k: k, i: -1 }
    }
    function pickColor(i) {  // the Color Picker on colour stop i
        const win = gb.Window.window
        if (win && win.openDialog)
            win.openDialog("ColorPicker", { color: Color.css(value.colors[i].c), title: qsTr("điểm màu"), picked: c => gb.once(gb.change("colors", i, { c: Color.parseHex(c) })) })
    }

    // The bar's transparency checker, .ck: 4 px squares, white then grey, rounded 4
    component Checker: Canvas {
        onWidthChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.beginPath()
            ctx.roundedRect(0, 0, width, height, 4, 4)
            ctx.clip()
            ctx.fillStyle = "#ffffff"
            ctx.fillRect(0, 0, width, height)
            ctx.fillStyle = "#c9c9c9"
            for (let y = 0; y < height; y += 4)
                for (let x = y % 8 ? 0 : 4; x < width; x += 8)
                    ctx.fillRect(x, y, 4, 4)
        }
    }
    component Pent: Shape {  // a colour stop, the prototype's clip-path polygon(50% 0, 100% 35%, 100% 100%, 0 100%, 0 35%)
        id: pt
        property color fill
        property color edge: "transparent"
        ShapePath {
            fillColor: pt.fill
            strokeColor: pt.edge
            strokeWidth: 1
            PathPolyline {
                path: [Qt.point(pt.width / 2, 0), Qt.point(pt.width, pt.height * 0.35), Qt.point(pt.width, pt.height), Qt.point(0, pt.height),
                    Qt.point(0, pt.height * 0.35), Qt.point(pt.width / 2, 0)]
            }
        }
    }

    T.Control {  // a Control, for FocusRing
        id: area
        width: parent.width
        height: 76
        focusPolicy: Qt.TabFocus
        Accessible.role: Accessible.Slider
        Accessible.name: (gb.kind === "c" ? qsTr("Điểm màu %1 ở %2%") : qsTr("Điểm độ mờ %1 ở %2%")).arg(gb.at + 1).arg(Math.round(gb.stop.p * 100))
        Keys.onLeftPressed: e => gb.once(gb.change(gb.list, gb.at, { p: Math.max(0, gb.stop.p - (e.modifiers & Qt.ShiftModifier ? 0.1 : 0.01)) }))
        Keys.onRightPressed: e => gb.once(gb.change(gb.list, gb.at, { p: Math.min(1, gb.stop.p + (e.modifiers & Qt.ShiftModifier ? 0.1 : 0.01)) }))
        Keys.onDeletePressed: if (gb.stops.length > 2) gb.once(gb.removed())

        Item {  // the bar
            x: 8
            y: 18
            width: gb.w
            height: 26
            Checker {
                visible: Color.seeThrough(gb.value)
                anchors.fill: parent
            }
            Row {
                Repeater {
                    model: Color.pieces(gb.value)
                    Rectangle {
                        id: piece
                        required property var modelData
                        required property int index
                        readonly property bool first: index === 0
                        readonly property bool last: modelData.x + modelData.w >= 1
                        width: modelData.w * gb.w
                        height: 26
                        topLeftRadius: first ? 4 : 0
                        bottomLeftRadius: first ? 4 : 0
                        topRightRadius: last ? 4 : 0
                        bottomRightRadius: last ? 4 : 0
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: piece.modelData.a }
                            GradientStop { position: 1; color: piece.modelData.b }
                        }
                    }
                }
            }
        }
        Repeater {  // the midpoints beside the stop picked
            model: 2
            Rectangle {
                id: dia
                required property int index
                readonly property int j: gb.at - 1 + index
                visible: j >= 0 && j < gb.stops.length - 1
                x: visible ? gb.px(gb.mid(gb.stops, j)) - 4 : 0
                y: gb.kind === "c" ? 48 : 2
                width: 8
                height: 8
                rotation: 45
                color: Theme.t3
            }
        }
        Repeater {  // opacity stops: black 100%, white 0%
            model: gb.value.alphas.length
            Item {
                id: os
                required property int index
                readonly property var s: gb.value.alphas[index] || { p: 0, a: 1 }
                readonly property bool on: gb.kind === "a" && gb.at === index
                readonly property color grey: Qt.rgba(1 - s.a, 1 - s.a, 1 - s.a, 1)
                x: gb.px(s.p) - 8
                y: -2
                width: 16
                height: 16
                Rectangle {
                    visible: !os.on
                    x: 2
                    y: 2
                    width: 12
                    height: 12
                    radius: 2
                    color: os.grey
                    border.color: Theme.t3
                }
                Rectangle {  // picked: white in a dark edge, as a colour stop
                    visible: os.on
                    anchors.fill: parent
                    radius: 3
                    color: Theme.p1
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: 3
                        color: "#ffffff"
                        Rectangle { anchors.fill: parent; anchors.margins: 2; radius: 2; color: os.grey }
                    }
                }
            }
        }
        Repeater {  // colour stops
            model: gb.value.colors.length
            Item {
                id: cs
                required property int index
                readonly property var s: gb.value.colors[index] || { p: 0, c: [0, 0, 0] }
                readonly property bool on: gb.kind === "c" && gb.at === index
                readonly property color fill: Color.css(s.c)
                x: gb.px(s.p) - 10
                y: 49
                width: 20
                height: 24
                Pent {
                    visible: !cs.on
                    x: 3
                    y: 3
                    width: 14
                    height: 18
                    fill: cs.fill
                    edge: Theme.t3
                }
                Pent {
                    visible: cs.on
                    anchors.fill: parent
                    fill: Theme.p1
                }
                Pent {
                    visible: cs.on
                    x: 1
                    y: 1
                    width: 18
                    height: 22
                    fill: "#ffffff"
                }
                Pent {
                    visible: cs.on
                    x: 3
                    y: 4
                    width: 14
                    height: 17
                    fill: cs.fill
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            onPressed: e => {
                area.forceActiveFocus(Qt.MouseFocusReason)
                const t = gb.hit(e.x, e.y)
                if (!t) return
                st.k = t.k
                if (t.mid) {
                    st.mode = "mid"
                    st.i = t.i
                    st.v0 = gb.value
                    return
                }
                if (t.i < 0) {  // a new stop, of the colour or the opacity there, then dragged as any
                    const q = gb.pos(e.x), r = gb.place(gb.value, t.k, -1, t.k === "colors" ? { p: q, c: Color.colorAt(gb.value, q), m: 0.5 } : { p: q, a: Color.alphaAt(gb.value, q), m: 0.5 })
                    gb.kind = t.k === "colors" ? "c" : "a"
                    gb.index = r[1]
                    st.v0 = r[0]
                    st.i = r[1]
                    st.dx = 0
                    gb.go(r[0])
                } else {
                    gb.kind = t.k === "colors" ? "c" : "a"
                    gb.index = t.i
                    st.v0 = gb.value
                    st.i = t.i
                    st.dx = e.x - gb.px(gb.value[t.k][t.i].p)
                }
                st.mode = "stop"
            }
            onPositionChanged: e => {
                const l = st.v0 ? st.v0[st.k] : []
                if (st.mode === "stop") {  // along, or 32 px off the stops' row: gone, while two of the kind are left
                    const out = l.length > 2 && Math.abs(e.y - (st.k === "colors" ? 61 : 6)) > 32
                    const r = gb.place(st.v0, st.k, st.i, out ? null : Object.assign({}, l[st.i], { p: gb.pos(e.x - st.dx) }))
                    gb.index = r[1]
                    gb.go(r[0])
                } else if (st.mode === "mid" && l[st.i + 1].p > l[st.i].p) {
                    const v = Object.assign({}, st.v0), m = l.slice()
                    m[st.i] = Object.assign({}, l[st.i], { m: Color.clamp((gb.pos(e.x) - l[st.i].p) / (l[st.i + 1].p - l[st.i].p), 0.05, 0.95) })
                    v[st.k] = m
                    gb.go(v)
                }
            }
            onReleased: gb.end()
            onCanceled: gb.end()
            onDoubleClicked: e => {
                const t = gb.hit(e.x, e.y)
                if (t && t.k === "colors" && t.i >= 0 && !t.mid)
                    gb.pickColor(t.i)
            }
        }
    }

    // Under the bar: where the stop picked is, its colour or opacity; Xóa điểm at the right
    Row {
        y: 86
        spacing: 10
        NumberField {
            width: 110
            label: qsTr("Vị trí")
            value: Math.round(gb.stop.p * 100)
            unit: "%"
            min: 0
            max: 100
            onMoved: x => gb.go(gb.change(gb.list, gb.at, { p: x / 100 }))
            onCommitted: gb.end()
        }
        Row {
            visible: gb.kind === "c"
            height: 24
            spacing: 6
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Màu")
                color: Theme.t2
                font.pointSize: Theme.pt(12)
            }
            Swatch {
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                height: 22
                radius: 4
                ring: Theme.ln
                color: Color.css(gb.kind === "c" ? gb.stop.c : [0, 0, 0])
                ToolTip.text: qsTr("Màu của điểm: #%1").arg(Color.hex(gb.kind === "c" ? gb.stop.c : [0, 0, 0]))
                Accessible.name: ToolTip.text
                onClicked: gb.pickColor(gb.at)
            }
        }
        NumberField {
            visible: gb.kind === "a"
            width: 110
            label: qsTr("Độ mờ")
            value: Math.round((gb.kind === "a" ? gb.stop.a : 1) * 100)
            unit: "%"
            min: 0
            max: 100
            onMoved: x => gb.go(gb.change("alphas", gb.at, { a: x / 100 }))
            onCommitted: gb.end()
        }
    }
    Btn {
        anchors.right: parent.right
        y: 86
        implicitHeight: 24
        leftPadding: 10
        rightPadding: 10
        enabled: gb.stops.length > 2
        text: qsTr("Xóa điểm")
        onClicked: gb.once(gb.removed())
    }
}
