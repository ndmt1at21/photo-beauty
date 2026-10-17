// copied from snapone 6bdb5c8 (ui/qml/Histogram.qml), without App, the clipping triangles, the grid and the shooting data
import QtQuick

// Compact RGB histogram (the Histogram panel): counts is [r, g, b], 256 bins each, as the core's
// histogram; the channels fill in h1–h3 over one another and over bg, mixed by Theme.hmix (screen on
// the dark theme, multiply on the light one, as the prototype's --hmix).
Rectangle {
    id: h
    property var counts: null

    implicitWidth: 256
    implicitHeight: 96
    radius: 3
    color: Theme.bg

    Canvas {
        anchors.fill: parent
        readonly property var deps: [h.counts, Theme.hmix, Theme.bg, width, height]
        onDepsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"), W = width, H = height, a = h.counts
            ctx.reset()
            if (!a || !W || !H) return
            const sm = ch => {  // ±2 bins
                const o = []
                for (let i = 0; i < 256; i++) {
                    let s = 0, k = 0
                    for (let j = Math.max(0, i - 2); j <= Math.min(255, i + 2); j++) { s += ch[j]; k++ }
                    o.push(s / k)
                }
                return o
            }
            const C = a.map(sm)
            let mx = 1
            for (let i = 2; i < 254; i++) mx = Math.max(mx, C[0][i], C[1][i], C[2][i])
            ctx.fillStyle = Theme.bg  // the channels mix with what is under them too
            ctx.fillRect(0, 0, W, H)
            ctx.globalCompositeOperation = Theme.hmix
            C.forEach((ch, k) => {
                ctx.fillStyle = Theme.hist[k]
                ctx.beginPath()
                ctx.moveTo(0, H)
                for (let i = 0; i < 256; i++) ctx.lineTo(i / 255 * W, H - Math.min(1, Math.sqrt(ch[i] / mx)) * (H - 3))
                ctx.lineTo(W, H)
                ctx.closePath()
                ctx.fill()
            })
        }
    }
}
