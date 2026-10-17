// copied from snapone 2571122 (ui/qml/CurveBox.qml, monotone() of ui/qml/Spec.js): the point curve, without App, Spec
// and the parametric curve
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// Curves' box (the CURVE row of the prototype's adjustments, plan.md's "CurveBox của SnapOne"): one channel's curve
// through `points` ([[input, output]…], 0..255, by input, both ends included) over `histogram` (an SVG path in a
// 284 × 220 box), the quarter grid and the diagonal, the `selected` point filled. Press away from the points to add
// one on the curve, drag a point between its neighbours, double-click one to delete it (not an end). Value protocol of
// the shared controls: started(), moved(points)… while the mouse is down, committed(); picked(index) the point pressed.
Item {
    id: cb
    property var points: [[0, 0], [255, 255]]
    property int selected: -1
    property color ink: Theme.t1
    property string histogram
    signal started()
    signal moved(var points)
    signal committed()
    signal picked(int index)

    implicitWidth: 284
    implicitHeight: Math.round(width * 220 / 284)

    // The monotone cubic through the points (Fritsch–Carlson): what the curve does between them.
    function monotone(pts) {
        const p = pts.slice().sort((a, b) => a[0] - b[0]), n = p.length
        if (n < 2) return x => x
        const xs = p.map(q => q[0]), ys = p.map(q => q[1]), d = [], m = []
        for (let i = 0; i < n - 1; i++) d[i] = (ys[i + 1] - ys[i]) / Math.max(1e-6, xs[i + 1] - xs[i])
        m[0] = d[0]
        m[n - 1] = d[n - 2]
        for (let i = 1; i < n - 1; i++) m[i] = d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2
        for (let i = 0; i < n - 1; i++) {
            if (d[i] === 0) { m[i] = 0; m[i + 1] = 0; continue }
            const a = m[i] / d[i], b = m[i + 1] / d[i], s = a * a + b * b
            if (s > 9) { const t = 3 / Math.sqrt(s); m[i] = t * a * d[i]; m[i + 1] = t * b * d[i] }
        }
        return x => {
            if (x <= xs[0]) return ys[0]
            if (x >= xs[n - 1]) return ys[n - 1]
            let i = 0
            while (i < n - 2 && x > xs[i + 1]) i++
            const h = xs[i + 1] - xs[i], t = (x - xs[i]) / h, t2 = t * t, t3 = t2 * t
            return (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * ys[i + 1] + (t3 - t2) * h * m[i + 1]
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: Theme.bg
    }
    Shape {  // the histogram, scaled from its box
        visible: cb.histogram !== ""
        width: 284
        height: 220
        transform: Scale { xScale: cb.width / 284; yScale: cb.height / 220 }
        ShapePath {
            strokeColor: "transparent"
            fillColor: Theme.faint
            PathSvg { path: cb.histogram }
        }
    }
    Canvas {
        anchors.fill: parent
        readonly property var deps: [cb.points, cb.selected, cb.ink, Theme.dark, width, height]
        onDepsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"), W = width, H = height, f = cb.monotone(cb.points), n = Math.ceil(W / 2)
            ctx.reset()
            ctx.lineWidth = 1
            ctx.strokeStyle = Theme.grid
            ctx.beginPath()
            for (let k = 1; k < 4; k++) {
                const x = Math.round(W * k / 4) + 0.5, y = Math.round(H * k / 4) + 0.5
                ctx.moveTo(x, 0)
                ctx.lineTo(x, H)
                ctx.moveTo(0, y)
                ctx.lineTo(W, y)
            }
            ctx.stroke()
            ctx.strokeStyle = Theme.trk
            ctx.beginPath()
            ctx.moveTo(0, H)
            ctx.lineTo(W, 0)
            ctx.stroke()
            ctx.strokeStyle = cb.ink
            ctx.lineWidth = 1.6
            ctx.beginPath()
            for (let j = 0; j <= n; j++) {
                const x = j / n * W, y = H - Math.max(0, Math.min(255, f(x / W * 255))) / 255 * H
                if (j) ctx.lineTo(x, y)
                else ctx.moveTo(x, y)
            }
            ctx.stroke()
            cb.points.forEach((p, i) => {  // the ends only once moved off their corners, or picked
                const on = i === cb.selected
                if (!on && ((p[0] === 0 && p[1] === 0) || (p[0] === 255 && p[1] === 255)))
                    return
                ctx.beginPath()
                ctx.arc(p[0] / 255 * W, H - p[1] / 255 * H, on ? 4.5 : 4, 0, Math.PI * 2)
                ctx.fillStyle = on ? cb.ink : Theme.bg
                ctx.fill()
                if (on)
                    return
                ctx.lineWidth = 1.5
                ctx.strokeStyle = cb.ink
                ctx.stroke()
            })
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        preventStealing: true
        cursorShape: Qt.CrossCursor
        property var pts: []  // the points while the mouse is down
        property int held: -1  // the one dragged
        property real ox  // where it was grabbed, from its centre
        property real oy
        property bool active  // started() sent
        function at(x, y) { return [x / width * 255, (1 - y / height) * 255] }
        function clamp(v) { return Math.max(0, Math.min(255, Math.round(v))) }
        function send(p) {
            if (!active) {
                active = true
                cb.started()
            }
            pts = p
            cb.moved(p)
        }
        function end() {
            held = -1
            if (!active) return
            active = false
            cb.committed()
        }
        onPressed: e => {
            const q = at(e.x, e.y)
            pts = cb.points
            let i = pts.findIndex(p => Math.hypot((p[0] - q[0]) / 255 * width, (p[1] - q[1]) / 255 * height) <= 8)
            if (i < 0) {  // a new point on the curve, at most 14
                const x = clamp(q[0])
                if (pts.length >= 14 || pts.some(p => p[0] === x)) return
                const np = [x, clamp(cb.monotone(pts)(x))], p = pts.concat([np]).sort((a, b) => a[0] - b[0])
                i = p.indexOf(np)
                send(p)
            }
            held = i
            ox = pts[i][0] - q[0]
            oy = pts[i][1] - q[1]
            cb.picked(i)
        }
        onPositionChanged: e => {
            if (held < 0) return
            const q = at(e.x, e.y), n = pts.length
            const lo = held === 0 ? 0 : pts[held - 1][0] + 1, hi = held === n - 1 ? 255 : pts[held + 1][0] - 1
            const np = [Math.max(lo, Math.min(hi, clamp(q[0] + ox))), clamp(q[1] + oy)]
            if (np[0] === pts[held][0] && np[1] === pts[held][1]) return
            const p = pts.slice()
            p[held] = np
            send(p)
        }
        onReleased: end()
        onCanceled: end()
        onDoubleClicked: {
            const i = held
            held = -1
            if (i <= 0 || i >= pts.length - 1) return
            send(pts.filter((p, k) => k !== i))
            cb.picked(i - 1)
        }
    }
}
