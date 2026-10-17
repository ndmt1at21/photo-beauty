pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import "Fmt.js" as Fmt

// Hue/Saturation's colour bars (the prototype's RANGE row, Controls' HueRange): the hues in over the hues out (turned by
// `shift` °, within the range and its falloff when there is one), both centred on `centre` °, and for a colour range
// its four handles between them: `span` [a, b, c, d] °, the falloff a–b, the range b–c, the falloff c–d (Reds' 315°/345°
// 15°\45°, the numbers under the bars), null for Master (no handles). Drag a triangle (a, d), an inner edge (b, c) or
// the middle (all four): started(), moved(span)…, committed().
Item {
    id: hr
    property var span: null
    property real centre: 180
    property real shift: 0
    signal started()
    signal moved(var span)
    signal committed()
    readonly property var r: span ? span.map(rel) : []  // the span as offsets from the centre, a ≤ b ≤ c ≤ d

    implicitWidth: 284
    implicitHeight: span ? 62 : 46

    function rel(h) { return ((h - centre) % 360 + 540) % 360 - 180 }  // °, −180..180 from the centre
    function xAt(o) { return (o + 180) / 360 * width }
    function weight(o) {  // how much of the shift the hue at offset o takes
        if (!span) return 1
        if (o < r[0] || o > r[3]) return 0
        if (o < r[1]) return (o - r[0]) / Math.max(1, r[1] - r[0])
        if (o > r[2]) return (r[3] - o) / Math.max(1, r[3] - r[2])
        return 1
    }
    function hue(o, out) {
        const h = centre + o + (out ? shift * weight(o) : 0)
        return Qt.hsva((h % 360 + 360) % 360 / 360, 1, 1, 1)
    }

    Canvas {  // in, out: a stop every 10°
        width: parent.width
        height: 46
        readonly property var deps: [hr.span, hr.centre, hr.shift, width]
        onDepsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            for (let out = 0; out < 2; out++) {
                const g = ctx.createLinearGradient(0, 0, width, 0)
                for (let i = 0; i <= 36; i++)
                    g.addColorStop(i / 36, String(hr.hue(i * 10 - 180, out)))
                ctx.globalAlpha = out ? 0.75 : 1
                ctx.fillStyle = g
                ctx.beginPath()
                ctx.roundedRect(0, out ? 36 : 0, width, 10, 2, 2)
                ctx.fill()
            }
        }
    }
    Rectangle {  // Master: the band alone
        visible: !hr.span
        y: 15
        width: parent.width
        height: 16
        color: Theme.p3
        opacity: 0.5
    }
    Item {
        visible: !!hr.span
        y: 15
        width: parent.width
        height: 16
        Rectangle {
            x: hr.span ? hr.xAt(hr.r[0]) : 0
            width: hr.span ? hr.xAt(hr.r[3]) - x : 0
            height: 16
            color: Theme.p3
        }
        Rectangle {
            x: hr.span ? hr.xAt(hr.r[1]) : 0
            width: hr.span ? hr.xAt(hr.r[2]) - x : 0
            height: 16
            color: Theme.t3
        }
        Shape {  // ◀ at a, ▶ at d
            anchors.fill: parent
            ShapePath {
                strokeColor: "transparent"
                fillColor: Theme.t2
                startX: hr.span ? hr.xAt(hr.r[0]) + 1 : 0
                startY: 0
                PathLine { relativeX: -6; relativeY: 8 }
                PathLine { relativeX: 6; relativeY: 8 }
                PathLine { relativeX: 0; relativeY: -16 }
            }
            ShapePath {
                strokeColor: "transparent"
                fillColor: Theme.t2
                startX: hr.span ? hr.xAt(hr.r[3]) : 0
                startY: 0
                PathLine { relativeX: 6; relativeY: 8 }
                PathLine { relativeX: -6; relativeY: 8 }
                PathLine { relativeX: 0; relativeY: -16 }
            }
        }
        MouseArea {
            x: -7
            width: parent.width + 14
            height: parent.height
            preventStealing: true
            property int part: -1  // 0–3 a handle, 4 the middle
            property real x0
            property var r0: []
            property bool active  // started() sent
            function end() {
                part = -1
                if (!active) return
                active = false
                hr.committed()
            }
            onPressed: e => {
                const x = e.x - 7, xs = hr.r.map(o => hr.xAt(o))
                part = x >= xs[0] - 7 && x <= xs[0] + 1 ? 0 : x >= xs[3] - 1 && x <= xs[3] + 7 ? 3 : Math.abs(x - xs[1]) <= 3 ? 1
                     : Math.abs(x - xs[2]) <= 3 ? 2 : x > xs[1] && x < xs[2] ? 4 : -1
                x0 = x
                r0 = hr.r.slice()
            }
            onPositionChanged: e => {
                if (part < 0) return
                const d = Math.round((e.x - 7 - x0) / hr.width * 360), r = r0.slice()
                if (part === 4) {
                    const k = Math.max(-180 - r[0], Math.min(180 - r[3], d))
                    for (let i = 0; i < 4; i++) r[i] += k
                } else {
                    r[part] = Math.max(part ? r[part - 1] : -180, Math.min(part < 3 ? r[part + 1] : 180, r0[part] + d))
                }
                const s = r.map(o => Math.round(((o + hr.centre) % 360 + 360) % 360))
                if (JSON.stringify(s) === JSON.stringify(hr.span)) return
                if (!active) {
                    active = true
                    hr.started()
                }
                hr.moved(s)
            }
            onReleased: end()
            onCanceled: end()
        }
    }
    Repeater {  // 315° / 345°   15° \ 45°
        model: hr.span ? 2 : 0
        Text {
            required property int index
            x: index ? hr.width - width : 0
            y: 49
            text: !hr.span ? "" : index ? Fmt.vn(hr.span[2]) + "° \\ " + Fmt.vn(hr.span[3]) + "°" : Fmt.vn(hr.span[0]) + "° / " + Fmt.vn(hr.span[1]) + "°"
            color: Theme.t2
            font.family: Theme.mono
            font.pointSize: Theme.pt(11)
        }
    }
}
