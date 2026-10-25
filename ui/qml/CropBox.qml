pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import "View.js" as View
import "Fmt.js" as Fmt

// The Crop tool's box (docs/ui-tasks.md, U7, U34; artboard Overlays, the prototype's ov.crop): the part cut away
// shaded, the rule of thirds, corner and side handles, the ratio and size below it, the turning arrow as the pointer
// outside it; Perspective Crop's four corners (`perspective`), shaded outside, with its grid. A left drag on it (U34) does
// what Photoshop 2025's Classic Mode does: a side or a corner moves (keeping `aspect` when set, else the box's own with
// Shift; Alt about the middle), outside it turns about its middle, inside it moves; Perspective Crop: a drag draws the
// four corners, then each goes where it is dragged, a side's two together, inside all four. It gives the box it makes to
// the owner (boxEdited, quadEdited), which keeps it and gives it back: the box keeps nothing of its own. The owner lays
// it over the frame; other buttons, the wheel fall through to the canvas.
Item {
    id: cr
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property real docW: 0
    property real docH: 0
    property var box: null  // { x, y, w, h }, document px: the crop before it turns
    property real angle: 0  // °, clockwise, about the box's middle
    property string ratio  // the HUD's first part, "4:5"
    property real aspect: 0  // the width over the height a drag keeps (the bar's ratio); 0 none: Shift keeps the box's own
    property bool perspective: false  // Perspective Crop: `quad`, not `box`
    property var quad: null  // Perspective Crop's [TL, TR, BR, BL], document px; null until a drag draws one
    property bool grid: true  // Perspective Crop's grid (its Show Grid)
    property bool interactive: true  // takes the left button
    property Item pointerArea: null  // the canvas's MouseArea under it (Viewport's): the hover handler goes on it, else on this

    signal boxEdited(var box, real angle)
    signal quadEdited(var quad)

    readonly property bool ok: !!view && !!size && docW > 0 && docH > 0 && (perspective || !!box)
    readonly property real s: ok ? View.scale(view, size) : 1
    readonly property var origin: ok ? View.toCanvas(view, size, { x: 0, y: 0 }) : ({ x: 0, y: 0 })
    // The box on the canvas, whole px of the document's box as the prototype's cr.
    readonly property rect r: ok && box ? Qt.rect(Math.round(box.x * s), Math.round(box.y * s), Math.round(box.w * s), Math.round(box.h * s)) : Qt.rect(0, 0, 0, 0)
    // The box's corners [TL, TR, BR, BL], document px; none before Perspective Crop's are drawn.
    readonly property var corners: !ok ? [] : perspective ? (quad && quad.length === 4 ? quad : []) : box ? [0, 2, 8, 6].map(k => placeOf(box, angle, k)) : []
    readonly property bool shown: corners.length === 4  // a box to draw
    // Canvas points of the box's 9 places (0–8 rows from the top left; 4 its middle).
    readonly property var pts: { const c = corners; return c.length === 4 ? [0, 1, 2, 3, 4, 5, 6, 7, 8].map(k => View.toCanvas(view, size, bil(c, k % 3 / 2, Math.floor(k / 3) / 2))) : [] }
    readonly property var corner: pts.length ? pts[8] : ({ x: 0, y: 0 })  // bottom right
    readonly property string hud: {
        if (!ok)
            return ""
        if (perspective) {  // the mean of the sides across, as its empty W, H take them
            const c = quad, d = (a, b) => Math.hypot(b.x - a.x, b.y - a.y)
            return c && c.length === 4 ? Fmt.vn(Math.round((d(c[0], c[1]) + d(c[3], c[2])) / 2)) + " × " + Fmt.vn(Math.round((d(c[0], c[3]) + d(c[1], c[2])) / 2)) + " px" : ""
        }
        return box ? (ratio ? ratio + " · " : "") + Fmt.vn(Math.round(box.w)) + " × " + Fmt.vn(Math.round(box.h)) + " px" + (angle ? "\n∠ " + Fmt.vn(angle, 1) + "°" : "") : ""
    }

    // ---- geometry ---------------------------------------------------------------------------------------------------
    function mid(b) { return { x: b.x + b.w / 2, y: b.y + b.h / 2 } }
    function rot(v, a) { const t = a * Math.PI / 180; return { x: v.x * Math.cos(t) - v.y * Math.sin(t), y: v.x * Math.sin(t) + v.y * Math.cos(t) } }
    function placeOf(b, a, k) {  // place k (0–8) of box b turned a° about its middle, document px
        const c = mid(b), v = rot({ x: (k % 3 / 2 - 0.5) * b.w, y: (Math.floor(k / 3) / 2 - 0.5) * b.h }, a)
        return { x: c.x + v.x, y: c.y + v.y }
    }
    function bil(c, u, v) {  // bilinear over [TL, TR, BR, BL]
        return {
            x: (1 - u) * (1 - v) * c[0].x + u * (1 - v) * c[1].x + u * v * c[2].x + (1 - u) * v * c[3].x,
            y: (1 - u) * (1 - v) * c[0].y + u * (1 - v) * c[1].y + u * v * c[2].y + (1 - u) * v * c[3].y
        }
    }
    function inside(p) {  // p, a document point, in the box
        if (perspective) {
            const c = quad || []
            let n = 0
            for (let i = 0, j = c.length - 1; i < c.length; j = i++)
                if ((c[i].y > p.y) !== (c[j].y > p.y) && p.x < (c[j].x - c[i].x) * (p.y - c[i].y) / (c[j].y - c[i].y) + c[i].x)
                    n++
            return n % 2 === 1
        }
        const l = rot({ x: p.x - mid(box).x, y: p.y - mid(box).y }, -angle)
        return Math.abs(l.x) <= box.w / 2 && Math.abs(l.y) <= box.h / 2
    }
    function bit(key) { return key === Qt.Key_Shift ? Qt.ShiftModifier : key === Qt.Key_Alt ? Qt.AltModifier : key === Qt.Key_Control ? Qt.ControlModifier : 0 }  // a modifier key's flag
    function norm(a) {  // ° in (−180, 180]
        a = a % 360
        return a > 180 ? a - 360 : a <= -180 ? a + 360 : a
    }
    // The square's (u, v) on the quad as a perspective plane: the projective map of the unit square onto it (Heckbert).
    function plane(c) {
        const sx = c[0].x - c[1].x + c[2].x - c[3].x, sy = c[0].y - c[1].y + c[2].y - c[3].y
        let g = 0, h = 0
        if (Math.abs(sx) > 1e-9 || Math.abs(sy) > 1e-9) {
            const dx1 = c[1].x - c[2].x, dx2 = c[3].x - c[2].x, dy1 = c[1].y - c[2].y, dy2 = c[3].y - c[2].y, det = dx1 * dy2 - dx2 * dy1
            if (Math.abs(det) > 1e-12) {
                g = (sx * dy2 - dx2 * sy) / det
                h = (dx1 * sy - sx * dy1) / det
            }
        }
        const a = c[1].x - c[0].x + g * c[1].x, b = c[3].x - c[0].x + h * c[3].x, d = c[1].y - c[0].y + g * c[1].y, e = c[3].y - c[0].y + h * c[3].y
        return (u, v) => { const w = g * u + h * v + 1; return { x: (a * u + b * v + c[0].x) / w, y: (d * u + e * v + c[0].y) / w } }
    }

    // What a press at canvas point q takes: a handle 0–8 (not 4; a side anywhere along it), "in", "out", or "new"
    // (Perspective Crop without its corners).
    function hitAt(q) {
        if (!ok)
            return null
        if (!shown)
            return perspective ? "new" : null
        for (const k of [0, 2, 8, 6])
            if (Math.hypot(pts[k].x - q.x, pts[k].y - q.y) <= 10)
                return k
        for (const k of [1, 5, 7, 3]) {
            if (perspective) {
                if (Math.hypot(pts[k].x - q.x, pts[k].y - q.y) <= 8)
                    return k
                continue
            }
            const a = pts[{ 1: 0, 5: 2, 7: 8, 3: 6 }[k]], b = pts[{ 1: 2, 5: 8, 7: 6, 3: 0 }[k]], ex = b.x - a.x, ey = b.y - a.y, l2 = ex * ex + ey * ey
            const t = l2 > 0 ? ((q.x - a.x) * ex + (q.y - a.y) * ey) / l2 : 0
            if (t > 0 && t < 1 && Math.hypot(a.x + t * ex - q.x, a.y + t * ey - q.y) <= 6)
                return k
        }
        return inside(View.toDoc(view, size, q)) ? "in" : "out"
    }
    // The box b0 turned a0 at the press (document point d0) as the drag of `what` reaches d: { box, angle }.
    function cropDragged(b0, a0, what, d0, d, mods) {
        const dx = d.x - d0.x, dy = d.y - d0.y, c0 = mid(b0)
        if (what === "in")
            return { box: { x: b0.x + dx, y: b0.y + dy, w: b0.w, h: b0.h }, angle: a0 }
        if (what === "out")
            return { box: b0, angle: norm(a0 + (Math.atan2(d.y - c0.y, d.x - c0.x) - Math.atan2(d0.y - c0.y, d0.x - c0.x)) * 180 / Math.PI) }
        const alt = !!(mods & Qt.AltModifier), ar = aspect > 0 ? aspect : mods & Qt.ShiftModifier ? b0.w / b0.h : 0
        const sx = what % 3 - 1, sy = Math.floor(what / 3) - 1, hw = b0.w / 2, hh = b0.h / 2  // the sides it is on: −1, 0, 1
        const m = rot({ x: dx, y: dy }, -a0), t = { x: sx * hw + m.x, y: sy * hh + m.y }  // where it goes, the box's own frame
        // An axis the handle moves on: from the side across (the middle with Alt) to t, at least 1 px.
        const span = (sd, half, tv) => {
            if (alt) {
                const e = Math.max(0.5, Math.abs(tv))
                return [-e, e]
            }
            const a = -sd * half
            return tv >= a ? [a, Math.max(a + 1, tv)] : [Math.min(a - 1, tv), a]
        }
        let xs = sx ? span(sx, hw, t.x) : [-hw, hw], ys = sy ? span(sy, hh, t.y) : [-hh, hh]
        if (ar > 0) {  // the ratio kept: a corner by the axis that went further, a side's other axis about the middle
            let w = xs[1] - xs[0], h = ys[1] - ys[0]
            if (sx && sy) {
                if (w >= h * ar)
                    h = w / ar
                else
                    w = h * ar
            } else if (sx) {
                h = w / ar
            } else {
                w = h * ar
            }
            const fit = (sd, v, tv, half) => {
                if (!sd)
                    return [-v / 2, v / 2]
                if (alt)
                    return [-v / 2, v / 2]
                const a = -sd * half
                return tv >= a ? [a, a + v] : [a - v, a]
            }
            xs = fit(sx, w, t.x, hw)
            ys = fit(sy, h, t.y, hh)
        }
        const v = rot({ x: (xs[0] + xs[1]) / 2, y: (ys[0] + ys[1]) / 2 }, a0), w = xs[1] - xs[0], h = ys[1] - ys[0]
        return { box: { x: c0.x + v.x - w / 2, y: c0.y + v.y - h / 2, w: w, h: h }, angle: a0 }
    }
    // Perspective Crop's corners q0 at the press as the drag of `what` goes from canvas point c0 (document d0) to c (d).
    function quadDragged(q0, what, c0, c, d0, d) {
        if (what === "new") {
            if (Math.abs(c.x - c0.x) < 3 || Math.abs(c.y - c0.y) < 3)
                return null
            const x0 = Math.min(c0.x, c.x), x1 = Math.max(c0.x, c.x), y0 = Math.min(c0.y, c.y), y1 = Math.max(c0.y, c.y)
            return [{ x: x0, y: y0 }, { x: x1, y: y0 }, { x: x1, y: y1 }, { x: x0, y: y1 }].map(p => View.toDoc(view, size, p))
        }
        if (what === "out" || !q0)
            return null
        const dx = d.x - d0.x, dy = d.y - d0.y, ids = what === "in" ? [0, 1, 2, 3] : { 0: [0], 2: [1], 8: [2], 6: [3], 1: [0, 1], 5: [1, 2], 7: [2, 3], 3: [3, 0] }[what]
        const out = q0.slice()
        for (const i of ids)
            out[i] = { x: q0[i].x + dx, y: q0[i].y + dy }
        return out
    }
    // The arrows: the box (Perspective Crop's corners) moved dx, dy document px.
    function nudge(dx, dy) {
        if (!shown)
            return
        if (perspective)
            quadEdited(quad.map(p => ({ x: p.x + dx, y: p.y + dy })))
        else
            boxEdited({ x: box.x + dx, y: box.y + dy, w: box.w, h: box.h }, angle)
    }

    // ---- the pointer ------------------------------------------------------------------------------------------------
    HoverHandler {
        id: hover
        parent: cr.pointerArea ?? cr
        enabled: cr.ok && cr.visible && cr.interactive
        cursorShape: cr.cursorWanted
    }
    readonly property point pointer: drag.pressed ? Qt.point(drag.mouseX, drag.mouseY) : hover.hovered ? mapFromItem(hover.parent, hover.point.position) : Qt.point(-1e6, -1e6)
    readonly property var under: drag.pressed ? drag.what : ok && hover.hovered ? hitAt({ x: pointer.x, y: pointer.y }) : null
    // Outside the box: it turns there, and shows so (not Perspective Crop's).
    readonly property bool turning: ok && !perspective && shown && under === "out"
    readonly property var cursorWanted: {
        if (turning)
            return Qt.BlankCursor
        if (under === "in")
            return Qt.SizeAllCursor
        if (typeof under !== "number")
            return undefined  // the canvas's
        const c = pts[under], m = pts[4], a = ((Math.atan2(c.y - m.y, c.x - m.x) * 180 / Math.PI) % 180 + 180) % 180
        return a < 22.5 || a >= 157.5 ? Qt.SizeHorCursor : a < 67.5 ? Qt.SizeFDiagCursor : a < 112.5 ? Qt.SizeVerCursor : Qt.SizeBDiagCursor
    }
    MouseArea {  // the left button's drags; it keeps the box at the press and makes each step from it
        id: drag
        property var b0: null
        property real a0: 0
        property var q0: null
        property var c0: null  // the press, canvas px
        property var d0: null  // and document px
        property var what: null  // hitAt()'s
        anchors.fill: parent
        enabled: cr.ok && cr.interactive
        acceptedButtons: Qt.LeftButton
        onPressed: m => {
            forceActiveFocus()  // off a field of the bar, as a press on the canvas
            c0 = { x: m.x, y: m.y }
            here = Qt.point(m.x, m.y)
            d0 = View.toDoc(cr.view, cr.size, c0)
            b0 = cr.box
            a0 = cr.angle
            q0 = cr.quad
            what = cr.hitAt(c0)
        }
        property point here  // the pointer, as the last move left it
        onPositionChanged: m => {
            here = Qt.point(m.x, m.y)
            go(m.modifiers)
        }
        function go(mods) {
            if (what === null || !cr.ok)
                return
            const c = { x: here.x, y: here.y }, d = View.toDoc(cr.view, cr.size, c)
            if (cr.perspective) {
                const q = cr.quadDragged(q0, what, c0, c, d0, d)
                if (q)
                    cr.quadEdited(q)
            } else if (b0) {
                const e = cr.cropDragged(b0, a0, what, d0, d, mods)
                cr.boxEdited(e.box, e.angle)
            }
        }
        // Shift, Alt pressed or let go while the pointer stays: the drag as they say now (the key goes on).
        Keys.onPressed: e => {
            if (pressed && (e.key === Qt.Key_Shift || e.key === Qt.Key_Alt))
                go(e.modifiers | cr.bit(e.key))
            e.accepted = false
        }
        Keys.onReleased: e => {  // a modifier's own release still has it (X11, offscreen): without it
            if (pressed && (e.key === Qt.Key_Shift || e.key === Qt.Key_Alt))
                go(e.modifiers & ~cr.bit(e.key))
            e.accepted = false
        }
        onReleased: what = null
        onCanceled: what = null
    }

    // ---- drawn ------------------------------------------------------------------------------------------------------
    // A polygon cut to [0, W] × [0, H] (Sutherland–Hodgman): the shade's hole stays on the page.
    function cut(poly, W, H) {
        let out = poly
        const cuts = [["x", 0, 1], ["x", W, -1], ["y", 0, 1], ["y", H, -1]]
        for (const sd of cuts) {
            const k = sd[0], v = sd[1], g = sd[2], src = out
            out = []
            for (let i = 0; i < src.length; i++) {
                const a = src[i], b = src[(i + 1) % src.length], ia = (a[k] - v) * g >= 0, ib = (b[k] - v) * g >= 0
                if (ia)
                    out.push(a)
                if (ia !== ib) {
                    const f = (v - a[k]) / (b[k] - a[k])
                    out.push({ x: a.x + f * (b.x - a.x), y: a.y + f * (b.y - a.y) })
                }
            }
            if (!out.length)
                break
        }
        return out
    }
    function path(poly) { return poly.length < 3 ? "" : poly.map((p, i) => (i ? "L" : "M") + p.x.toFixed(2) + " " + p.y.toFixed(2)).join("") + "Z" }
    Item {  // the document's box, turned with the view
        id: page
        x: Math.round(cr.origin.x)
        y: Math.round(cr.origin.y)
        width: Math.round(cr.docW * cr.s)
        height: Math.round(cr.docH * cr.s)
        rotation: cr.ok ? cr.view.angle : 0
        transformOrigin: Item.TopLeft
        visible: cr.shown
        // The box's corners on the page: the crop item's as drawn (turned about its middle), Perspective Crop's own.
        readonly property var hole: {
            if (!cr.shown)
                return []
            if (cr.perspective)
                return cr.quad && cr.quad.length === 4 ? cr.quad.map(p => ({ x: p.x * cr.s, y: p.y * cr.s })) : []
            const m = { x: cr.r.x + cr.r.width / 2, y: cr.r.y + cr.r.height / 2 }
            return [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(f => {
                const v = cr.rot({ x: f[0] * cr.r.width / 2, y: f[1] * cr.r.height / 2 }, cr.angle)
                return { x: m.x + v.x, y: m.y + v.y }
            })
        }
        Shape {  // the shade: the page but the box
            anchors.fill: parent
            ShapePath {
                fillColor: Qt.rgba(0, 0, 0, 0.62)
                fillRule: ShapePath.OddEvenFill
                strokeColor: "transparent"
                PathSvg { path: "M0 0H" + page.width + "V" + page.height + "H0Z" + cr.path(cr.cut(page.hole, page.width, page.height)) }
            }
        }
        Item {
            id: crop
            visible: !cr.perspective
            x: cr.r.x
            y: cr.r.y
            width: cr.r.width
            height: cr.r.height
            rotation: cr.angle
            transformOrigin: Item.Center
            Rectangle {  // its edge, just outside it
                x: -1
                y: -1
                width: parent.width + 2
                height: parent.height + 2
                color: "transparent"
                border.color: Qt.rgba(1, 1, 1, 0.9)
            }
            Repeater {  // thirds
                model: [[1, 0], [2, 0], [0, 1], [0, 2]]
                Rectangle {
                    required property var modelData
                    x: modelData[0] ? Math.round(crop.width * modelData[0] / 3) : 0
                    y: modelData[1] ? Math.round(crop.height * modelData[1] / 3) : 0
                    width: modelData[0] ? 1 : crop.width
                    height: modelData[0] ? crop.height : 1
                    color: Qt.rgba(1, 1, 1, 0.5)
                }
            }
            // Handles, 3 px thick, 3 px outside the box: the corners' arms, then the sides' middles.
            Repeater {
                model: {
                    const w = cr.r.width, h = cr.r.height, mx = Math.round(w / 2) - 9, my = Math.round(h / 2) - 9
                    return [[-3, -3, 18, 3], [-3, -3, 3, 18], [w - 15, -3, 18, 3], [w, -3, 3, 18], [-3, h, 18, 3], [-3, h - 15, 3, 18], [w - 15, h, 18, 3], [w, h - 15, 3, 18],
                        [mx, -3, 18, 3], [mx, h, 18, 3], [-3, my, 3, 18], [w, my, 3, 18]]
                }
                Rectangle {
                    required property var modelData
                    x: modelData[0] - 1
                    y: modelData[1] - 1
                    width: modelData[2] + 2
                    height: modelData[3] + 2
                    color: Qt.rgba(0, 0, 0, 0.6)
                    Rectangle { x: 1; y: 1; width: parent.width - 2; height: parent.height - 2; color: "#ffffff" }
                }
            }
        }
        Shape {  // Perspective Crop: its edge and grid (thirds of the plane it stands for)
            id: plane
            anchors.fill: parent
            visible: cr.perspective
            readonly property string lines: {
                const q = page.hole
                if (!cr.perspective || q.length !== 4)
                    return ""
                const f = cr.plane(q), pt = p => p.x.toFixed(2) + " " + p.y.toFixed(2)
                let d = cr.path(q)
                if (cr.grid)
                    for (const t of [1 / 3, 2 / 3])
                        d += "M" + pt(f(t, 0)) + "L" + pt(f(t, 1)) + "M" + pt(f(0, t)) + "L" + pt(f(1, t))
                return d
            }
            ShapePath {
                strokeColor: Qt.rgba(0, 0, 0, 0.5)
                strokeWidth: 3
                fillColor: "transparent"
                PathSvg { path: plane.lines }
            }
            ShapePath {
                strokeColor: Qt.rgba(1, 1, 1, 0.9)
                strokeWidth: 1
                fillColor: "transparent"
                PathSvg { path: plane.lines }
            }
        }
        Repeater {  // Perspective Crop's handles: corners, sides' middles
            model: cr.perspective && cr.shown ? [0, 1, 2, 3, 5, 6, 7, 8] : []
            Rectangle {
                required property int modelData
                readonly property var c: page.hole.length === 4 ? cr.bil(page.hole, modelData % 3 / 2, Math.floor(modelData / 3) / 2) : { x: 0, y: 0 }
                x: c.x - 4
                y: c.y - 4
                width: 8
                height: 8
                color: "#1c1c1e"
                Rectangle { x: 1; y: 1; width: 6; height: 6; color: "#ffffff" }
            }
        }
    }
    CanvasBar.Hud {
        visible: cr.shown
        x: Math.round(cr.corner.x)
        y: Math.round(cr.corner.y) + 8
        text: cr.hud
    }
    Loader {  // the pointer outside the box (made when shown: Viewport's test finds the canvas's own Icon first)
        active: cr.turning
        x: cr.pointer.x - 11
        y: cr.pointer.y - 11
        sourceComponent: Icon {
            name: "rotate_right"
            size: 22
            color: "#ffffff"
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.9)
        }
    }
}
