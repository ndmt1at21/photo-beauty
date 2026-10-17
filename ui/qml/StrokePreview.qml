import QtQuick

// The brush's stroke (docs/ui-tasks.md, U13; "Nét xem trước" of Controls.dc.html: the foot of Brush Settings, the rows of
// Brushes): dabs of the tip along the prototype's S curve. A dab is as wide as `size` (px, 1–5.000) on a log scale up to
// a quarter of the height, as soft as `hardness` (%), turned to `angle` (°, anticlockwise) and flattened to `roundness` (%),
// square when `square`, `spacing` % of its width after the last. A faked pen pressure rises and falls along the curve: at
// the ends a dab shrinks to `minSize` of its width and fades to `minOpacity` (0–1; 1 leaves it alone). Canvas, no shader,
// so it draws offscreen too.
Canvas {
    id: sp
    property real size: 400
    property real hardness: 0
    property real spacing: 25
    property real angle
    property real roundness: 100
    property bool square: false
    property real minSize: 1
    property real minOpacity: 1
    property color color: Theme.t1
    readonly property var look: [size, hardness, spacing, angle, roundness, square, minSize, minOpacity, color, width, height]
    property int dabs  // drawn the last time, for tests

    implicitWidth: 260
    implicitHeight: 40
    onLookChanged: requestPaint()

    // The centre line of the prototype's stroke, two cubics in its 420 × 64 box: halfway between the path's two edges.
    readonly property var curve: [[8, 44], [72, 12], [132, 10], [197, 34], [251, 54], [325, 58], [412, 20]]

    onPaint: {
        const c = getContext("2d"), w = width, h = height
        c.reset()
        if (w < 4 || h < 4)
            return
        const pts = []
        for (let s = 0; s < 2; s++) {
            const p0 = curve[3 * s], p1 = curve[3 * s + 1], p2 = curve[3 * s + 2], p3 = curve[3 * s + 3]
            for (let i = s; i <= 64; i++) {
                const t = i / 64, u = 1 - t
                const a = u * u * u, b = 3 * u * u * t, q = 3 * u * t * t, e = t * t * t
                pts.push([a * p0[0] + b * p1[0] + q * p2[0] + e * p3[0], a * p0[1] + b * p1[1] + q * p2[1] + e * p3[1]])
            }
        }
        const xs = pts.map(p => p[0]), ys = pts.map(p => p[1])
        const x0 = Math.min.apply(null, xs), y0 = Math.min.apply(null, ys), cw = Math.max.apply(null, xs) - x0, ch = Math.max.apply(null, ys) - y0
        const big = h / 4, pad = big / 2 + 1  // the widest dab, 5.000 px, stays inside
        const k = Math.min((w - 2 * pad) / cw, (h - 2 * pad) / ch)  // as the artboard's SVG: one scale, centred
        const ox = (w - cw * k) / 2, oy = (h - ch * k) / 2
        const xy = pts.map(p => [ox + (p[0] - x0) * k, oy + (p[1] - y0) * k])
        const len = [0]
        for (let i = 1; i < xy.length; i++)
            len.push(len[i - 1] + Math.hypot(xy[i][0] - xy[i - 1][0], xy[i][1] - xy[i - 1][1]))
        const L = len[len.length - 1]

        const d0 = Math.max(1, big * Math.log(1 + Math.max(1, size)) / Math.log(5001))
        const hard = Math.max(0, Math.min(1, hardness / 100)), flat = Math.max(0.02, roundness / 100), turn = -angle * Math.PI / 180
        const ink = color, clear = Qt.rgba(color.r, color.g, color.b, 0)
        const least = L / 1500  // ponytail: at most 1.500 dabs, whatever the spacing
        let at = 0, j = 1, n = 0
        while (at <= L) {
            while (j < len.length - 1 && len[j] < at)
                j++
            const f = (at - len[j - 1]) / Math.max(1e-6, len[j] - len[j - 1])
            const x = xy[j - 1][0] + (xy[j][0] - xy[j - 1][0]) * f, y = xy[j - 1][1] + (xy[j][1] - xy[j - 1][1]) * f
            const p = Math.sin(Math.PI * at / L)
            const d = d0 * (minSize + (1 - minSize) * p), o = minOpacity + (1 - minOpacity) * p
            if (d > 0.2 && o > 0) {
                const r = d / 2
                c.save()
                c.translate(x, y)
                c.rotate(turn)
                c.scale(1, flat)
                c.globalAlpha = o
                if (hard >= 1) {
                    c.fillStyle = ink
                } else {
                    const g = c.createRadialGradient(0, 0, 0, 0, 0, r)
                    g.addColorStop(0, ink)
                    g.addColorStop(hard, ink)
                    g.addColorStop(1, clear)
                    c.fillStyle = g
                }
                c.beginPath()
                if (square)
                    c.rect(-r, -r, d, d)
                else
                    c.arc(0, 0, r, 0, 2 * Math.PI)
                c.fill()
                c.restore()
                n++
            }
            at += Math.max(least, 0.5, d * spacing / 100)
        }
        dabs = n
    }
}
