pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import "View.js" as View
import "Fmt.js" as Fmt

// Free Transform and Warp's box (docs/ui-tasks.md, U7, U34; artboard Overlays, the prototype's ov.xf, ov.warp): the
// layer's box as Free Transform's bar has it, 8 handles, the reference point, W, H and the angle beside it, the turning
// arrow as the pointer outside it; Warp: its grid of Bezier patches, their points and handles. A left drag on it does
// what Photoshop 2025's does (U34): a handle scales (a corner keeping proportions as `proportional`, Shift the other
// way; Alt about the reference point), Ctrl distorts, Ctrl Shift on a side skews, Ctrl Alt Shift on a corner makes
// perspective; outside it turns (Shift by 15°), inside it moves, the reference point goes where it is dragged; Warp's
// points and Bezier handles move. It works out the transform that makes and gives it to the owner (transformed(),
// warped()), which keeps it and gives it back through the properties: the box keeps nothing but Warp's picked point.
// The owner lays it over the frame; other buttons, the wheel fall through to the canvas.
//
// The transform takes a point q of the box (document px) to P + L · (q − r): r is the reference point on the box, P
// where it is now (the bar's X, Y), L = R(angle) · [[1, tan skewX], [tan skewY, 1]] · diag(scaleX, scaleY),
// R(a) = [[cos a, −sin a], [sin a, cos a]] (y down: a > 0 clockwise). Distort and Perspective move the box's corners
// before it (`corners`, the box's own px): the frame is the 4 corners so transformed, its side handles their middles.
// Warp's control net is in the box's own px too, before the transform: a Bezier patch is 4 × 4 of it.
Item {
    id: tb
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property real docW: 0
    property real docH: 0
    property var box: ({ x: 0, y: 0, w: docW, h: docH })  // the layer's bounds, document px
    property real scaleX: 1  // 1 is 100 %; below 0 flipped
    property real scaleY: 1
    property real angle: 0  // °, clockwise, about the reference point
    property real skewX: 0  // °, H: the sides lean
    property real skewY: 0  // °, V: the top and bottom lean
    property int pivot: 4  // the reference point, 0–8 as AnchorGrid's: rows from the top left; −1: `ref`
    property var ref: null  // the reference point off those 9: { x, y }, fractions across the box
    property real refX: NaN  // where the reference point is now, document px (the bar's X, Y); NaN: its place on the box
    property real refY: NaN
    property var corners: null  // [TL, TR, BR, BL] as Distort, Perspective moved them, the box's own px; null: the box's
    property bool warp: false
    property int cells: 3  // Warp's grid, n × n patches
    property var mesh: null  // Warp's control net: (3 cells + 1)² points row by row, the box's own px; null: flat
    property bool proportional: true  // a corner keeps the proportions (Thiết lập, the bar's link); Shift the other way
    property bool interactive: true  // takes the left button
    property Item pointerArea: null  // the canvas's MouseArea under it (Viewport's): the hover handler goes on it, else on this

    // A drag's transform: { scaleX, scaleY, angle, skewX, skewY, pivot, ref, refX, refY, corners } as the properties take
    // them, refX and refY numbers. Warp's: the whole net (the box's own px).
    signal transformed(var t)
    signal warped(var mesh)

    readonly property bool ok: !!view && !!size && docW > 0 && docH > 0 && !!box && box.w > 0 && box.h > 0
    readonly property real s: ok ? View.scale(view, size) : 1
    readonly property var p: ok ? posOf({ pivot: pivot, ref: ref, refX: refX, refY: refY }) : ({ x: 0, y: 0 })  // the reference point, document px
    readonly property var quad: ok ? quadOf(current()) : []  // the frame's corners [TL, TR, BR, BL], document px
    // Canvas points: the frame's 9 places (0–8 as pivot's; 4 its middle), its corners, the reference point.
    readonly property var pts: ok ? [0, 1, 2, 3, 4, 5, 6, 7, 8].map(k => View.toCanvas(view, size, handleOf(current(), k))) : []
    readonly property var corner4: ok ? quad.map(q => View.toCanvas(view, size, q)) : []
    readonly property var ring: ok ? View.toCanvas(view, size, p) : ({ x: 0, y: 0 })
    readonly property var topRight: ok ? corner4[1] : ({ x: 0, y: 0 })
    readonly property real turn: ok ? Math.atan2(corner4[1].y - corner4[0].y, corner4[1].x - corner4[0].x) * 180 / Math.PI : 0  // the top edge on screen, °

    // ---- the transform ----------------------------------------------------------------------------------------------
    function current() {  // the transform as the properties have it
        return { scaleX: scaleX, scaleY: scaleY, angle: angle, skewX: skewX, skewY: skewY, pivot: pivot, ref: ref, refX: p.x, refY: p.y, corners: corners }
    }
    function put(t, patch) { return Object.assign({}, t, patch) }
    function place(k) { return { x: k % 3 / 2, y: Math.floor(k / 3) / 2 } }  // fractions across the box of the 9 places
    function refOf(t) {  // the reference point on the box, box px
        const f = t.pivot >= 0 && t.pivot <= 8 ? place(t.pivot) : (t.ref || { x: 0.5, y: 0.5 })
        return { x: box.x + f.x * box.w, y: box.y + f.y * box.h }
    }
    function posOf(t) { return isFinite(t.refX) && isFinite(t.refY) ? { x: t.refX, y: t.refY } : refOf(t) }
    function linOf(t) {  // L as [a, b, c, d]: x' = a x + b y, y' = c x + d y
        const a = t.angle * Math.PI / 180, cs = Math.cos(a), sn = Math.sin(a)
        const kx = Math.tan(t.skewX * Math.PI / 180), ky = Math.tan(t.skewY * Math.PI / 180)
        const m00 = t.scaleX, m01 = kx * t.scaleY, m10 = ky * t.scaleX, m11 = t.scaleY
        return [cs * m00 - sn * m10, cs * m01 - sn * m11, sn * m00 + cs * m10, sn * m01 + cs * m11]
    }
    function fwd(t, q) {  // box px → document px
        const r = refOf(t), P = posOf(t), L = linOf(t), dx = q.x - r.x, dy = q.y - r.y
        return { x: P.x + L[0] * dx + L[1] * dy, y: P.y + L[2] * dx + L[3] * dy }
    }
    function lin(t, dx, dy) {  // a document px move as box px
        const L = linOf(t), det = L[0] * L[3] - L[1] * L[2]
        return { x: (L[3] * dx - L[1] * dy) / det, y: (L[0] * dy - L[2] * dx) / det }
    }
    function back(t, q) {  // document px → box px
        const r = refOf(t), P = posOf(t), m = lin(t, q.x - P.x, q.y - P.y)
        return { x: r.x + m.x, y: r.y + m.y }
    }
    function boxCorners() { return [{ x: box.x, y: box.y }, { x: box.x + box.w, y: box.y }, { x: box.x + box.w, y: box.y + box.h }, { x: box.x, y: box.y + box.h }] }
    function cornersOf(t) { return t.corners && t.corners.length === 4 ? t.corners : boxCorners() }
    function bil(c, u, v) {  // bilinear over [TL, TR, BR, BL]
        return {
            x: (1 - u) * (1 - v) * c[0].x + u * (1 - v) * c[1].x + u * v * c[2].x + (1 - u) * v * c[3].x,
            y: (1 - u) * (1 - v) * c[0].y + u * (1 - v) * c[1].y + u * v * c[2].y + (1 - u) * v * c[3].y
        }
    }
    function quadOf(t) { return cornersOf(t).map(q => fwd(t, q)) }
    function handleOf(t, k) { const f = place(k); return fwd(t, bil(cornersOf(t), f.x, f.y)) }  // document px
    // A point of the frame (fx, fy: 0 to 1 across it), document px.
    function at(fx, fy) { const t = current(); return fwd(t, bil(cornersOf(t), fx, fy)) }
    function inside(q) {  // q, a document point, in the frame
        const c = quad
        let n = 0
        for (let i = 0, j = 3; i < 4; j = i++)
            if ((c[i].y > q.y) !== (c[j].y > q.y) && q.x < (c[j].x - c[i].x) * (q.y - c[i].y) / (c[j].y - c[i].y) + c[i].x)
                n++
        return n % 2 === 1
    }
    function bit(key) { return key === Qt.Key_Shift ? Qt.ShiftModifier : key === Qt.Key_Alt ? Qt.AltModifier : key === Qt.Key_Control ? Qt.ControlModifier : 0 }  // a modifier key's flag
    function norm(a) {  // ° in (−180, 180]
        a = a % 360
        return a > 180 ? a - 360 : a <= -180 ? a + 360 : a
    }
    function part(n, d) {  // a scale's factor, never 0
        if (Math.abs(d) < 1e-9)
            return 1
        const f = n / d
        return Math.abs(f) < 1e-4 ? (f < 0 ? -1e-4 : 1e-4) : f
    }
    // The same transform with the reference point at place k (0–8) of the box.
    function withPivot(t, k) {
        const q = fwd(t, refOf({ pivot: k }))
        return put(t, { pivot: k, ref: null, refX: q.x, refY: q.y })
    }

    // What a press at canvas point q takes: a handle 0–8 (not 4), "ref", "in" or "out".
    function hitAt(q) {
        if (!ok)
            return null
        for (const k of [0, 2, 8, 6, 1, 5, 7, 3])
            if (Math.hypot(pts[k].x - q.x, pts[k].y - q.y) <= 8)
                return k
        if (Math.hypot(ring.x - q.x, ring.y - q.y) <= 8)
            return "ref"
        return inside(View.toDoc(view, size, q)) ? "in" : "out"
    }
    // The transform t0 at the press (document point d0) makes as the drag of `what` reaches d with modifiers mods.
    function dragged(t0, what, d0, d, mods) {
        const shift = !!(mods & Qt.ShiftModifier), alt = !!(mods & Qt.AltModifier), ctrl = !!(mods & Qt.ControlModifier)
        const dx = d.x - d0.x, dy = d.y - d0.y
        if (what === "in")
            return moved(t0, dx, dy, shift)
        if (what === "out")
            return turned(t0, d0, d, shift)
        if (what === "ref")
            return refMoved(t0, dx, dy)
        const corner = what === 0 || what === 2 || what === 6 || what === 8
        if (ctrl && alt && shift && corner)
            return perspective(t0, what, dx, dy)
        if (ctrl && shift && !corner)
            return skewed(t0, what, dx, dy, alt)
        if (ctrl)
            return distorted(t0, what, dx, dy)
        return scaled(t0, what, dx, dy, alt, corner && proportional !== shift)
    }
    function moved(t0, dx, dy, shift) {  // inside: P moves; Shift: along 0°, 45°, 90°…
        if (shift) {
            const a = Math.round(Math.atan2(dy, dx) / (Math.PI / 4)) * Math.PI / 4, l = dx * Math.cos(a) + dy * Math.sin(a)
            dx = l * Math.cos(a)
            dy = l * Math.sin(a)
        }
        const P = posOf(t0)
        return put(t0, { refX: P.x + dx, refY: P.y + dy })
    }
    function turned(t0, d0, d, shift) {  // outside: about the reference point, by the angle the pointer went round it
        const P = posOf(t0)
        let a = t0.angle + (Math.atan2(d.y - P.y, d.x - P.x) - Math.atan2(d0.y - P.y, d0.x - P.x)) * 180 / Math.PI
        if (shift)
            a = Math.round(a / 15) * 15
        return put(t0, { angle: norm(a) })
    }
    function refMoved(t0, dx, dy) {  // the reference point: the transform stays; at one of the 9 places within 8 px, there
        const P = posOf(t0), q = { x: P.x + dx, y: P.y + dy }, c = View.toCanvas(view, size, q)
        for (let k = 0; k < 9; k++) {
            const h = View.toCanvas(view, size, fwd(t0, refOf({ pivot: k })))
            if (Math.hypot(h.x - c.x, h.y - c.y) <= 8)
                return withPivot(t0, k)
        }
        const b = back(t0, q)
        return put(t0, { pivot: -1, ref: { x: (b.x - box.x) / box.w, y: (b.y - box.y) / box.h }, refX: q.x, refY: q.y })
    }
    // A handle k scales the box about the one across it, or the reference point with Alt: the handle follows the
    // pointer along the box's axes it moves on (a corner both, a side one); prop: both by the factor of the axis that
    // went further.
    function scaled(t0, k, dx, dy, alt, prop) {
        const c = cornersOf(t0), f = place(k), g = place(8 - k), h = bil(c, f.x, f.y)
        const a = alt ? refOf(t0) : bil(c, g.x, g.y), hd = fwd(t0, h), q = back(t0, { x: hd.x + dx, y: hd.y + dy })
        let fx = k % 3 !== 1 ? part(q.x - a.x, h.x - a.x) : 1, fy = Math.floor(k / 3) !== 1 ? part(q.y - a.y, h.y - a.y) : 1
        if (prop)
            fx = fy = Math.abs(fx) >= Math.abs(fy) ? fx : fy
        // q → t0(a + diag(fx, fy) (q − a)): the same with the scales times fx, fy and P where a stays.
        const r = refOf(t0), P = posOf(t0), L = linOf(t0), ux = (1 - fx) * (a.x - r.x), uy = (1 - fy) * (a.y - r.y)
        return put(t0, { scaleX: t0.scaleX * fx, scaleY: t0.scaleY * fy, refX: P.x + L[0] * ux + L[1] * uy, refY: P.y + L[2] * ux + L[3] * uy })
    }
    // Ctrl Shift on a side: it slides along the box's turned axis (top, bottom: H, tan skewX; left, right: V, tan skewY)
    // as far as the pointer goes along it, the side across staying, or both about the reference point with Alt.
    function skewed(t0, k, dx, dy, alt) {
        const a = t0.angle * Math.PI / 180, cs = Math.cos(a), sn = Math.sin(a), r = refOf(t0), P = posOf(t0), h = k === 1 || k === 7
        const far = h ? (k === 1 ? box.y : box.y + box.h) : (k === 3 ? box.x : box.x + box.w)
        const near = h ? (k === 1 ? box.y + box.h : box.y) : (k === 3 ? box.x + box.w : box.x)
        const zs = h ? t0.scaleY : t0.scaleX, z = zs * (far - (h ? r.y : r.x)), zo = alt ? 0 : zs * (near - (h ? r.y : r.x))
        if (Math.abs(z - zo) < 1e-9)
            return t0
        const ex = h ? cs : -sn, ey = h ? sn : cs  // the axis it slides along: R(a) (1, 0) or R(a) (0, 1)
        const dt = (dx * ex + dy * ey) / (z - zo), was = (h ? t0.skewX : t0.skewY) * Math.PI / 180
        const k2 = Math.atan(Math.tan(was) + dt) * 180 / Math.PI
        return put(t0, h ? { skewX: k2, refX: P.x - dt * zo * ex, refY: P.y - dt * zo * ey } : { skewY: k2, refX: P.x - dt * zo * ex, refY: P.y - dt * zo * ey })
    }
    // Ctrl: a corner goes where the pointer takes it, a side's two corners with it.
    function distorted(t0, k, dx, dy) {
        const c = cornersOf(t0).slice(), ids = { 0: [0], 2: [1], 8: [2], 6: [3], 1: [0, 1], 5: [1, 2], 7: [2, 3], 3: [3, 0] }[k]
        for (const i of ids) {
            const q = fwd(t0, c[i])
            c[i] = back(t0, { x: q.x + dx, y: q.y + dy })
        }
        return put(t0, { corners: c })
    }
    // Ctrl Alt Shift on a corner: it moves along the side the drag goes more along, the corner across that side as far the
    // other way.
    function perspective(t0, k, dx, dy) {
        const c = cornersOf(t0).slice(), i = { 0: 0, 2: 1, 8: 2, 6: 3 }[k], b = lin(t0, dx, dy), L = linOf(t0)
        if (Math.hypot(L[0] * b.x, L[2] * b.x) >= Math.hypot(L[1] * b.y, L[3] * b.y)) {
            const j = [1, 0, 3, 2][i]
            c[i] = { x: c[i].x + b.x, y: c[i].y }
            c[j] = { x: c[j].x - b.x, y: c[j].y }
        } else {
            const j = [3, 2, 1, 0][i]
            c[i] = { x: c[i].x, y: c[i].y + b.y }
            c[j] = { x: c[j].x, y: c[j].y - b.y }
        }
        return put(t0, { corners: c })
    }
    // The arrows: the box moved dx, dy document px (Warp: its picked point).
    function nudge(dx, dy) {
        if (!ok)
            return
        if (!warp) {
            transformed(put(current(), { refX: p.x + dx, refY: p.y + dy }))
        } else if (pick >= 0) {
            const t = current()
            warped(meshDragged(t, netOf(t), { anchor: pick }, { x: 0, y: 0 }, { x: dx, y: dy }))
        }
    }

    // ---- Warp -------------------------------------------------------------------------------------------------------
    readonly property int side: 3 * cells + 1  // points along a row of the net
    property int pick: -1  // the point picked (its index in the net), −1 none
    onWarpChanged: pick = -1
    onCellsChanged: pick = -1
    function netOf(t) {  // the net, box px: `mesh`, or flat over the frame's corners
        const n = side
        if (mesh && mesh.length === n * n)
            return mesh
        const c = cornersOf(t), out = []
        for (let i = 0; i < n; i++)
            for (let j = 0; j < n; j++)
                out.push(bil(c, j / (n - 1), i / (n - 1)))
        return out
    }
    readonly property var net: { const t = ok && warp ? current() : null; return t ? netOf(t).map(q => View.toCanvas(view, size, fwd(t, q))) : [] }  // canvas
    readonly property var gridPts: {  // the grid's points, row by row: indices of the net
        const out = [], n = side
        for (let i = 0; i < n; i += 3)
            for (let j = 0; j < n; j += 3)
                out.push(i * n + j)
        return out
    }
    // The Bezier handles shown: the picked point's, else the corners' along the top and bottom edges (as the artboard).
    readonly property var bezier: {
        const n = side
        if (!warp)
            return []
        if (pick < 0)
            return [1, n - 2, (n - 1) * n + 1, n * n - 2]
        const i = Math.floor(pick / n), j = pick % n, out = []
        for (const d of [[0, -1], [0, 1], [-1, 0], [1, 0]])
            if (i + d[0] >= 0 && i + d[0] < n && j + d[1] >= 0 && j + d[1] < n)
                out.push((i + d[0]) * n + j + d[1])
        return out
    }
    function anchorOf(k) {  // a handle's point
        const n = side, i = Math.floor(k / n), j = k % n
        return i % 3 === 0 ? i * n + (j % 3 === 1 ? j - 1 : j + 1) : (i % 3 === 1 ? i - 1 : i + 1) * n + j
    }
    function meshHit(q) {  // { handle: k } or { anchor: k } a press at canvas point q takes, else {}
        for (const k of bezier)
            if (Math.hypot(net[k].x - q.x, net[k].y - q.y) <= 6)
                return { handle: k }
        for (const k of gridPts)
            if (Math.hypot(net[k].x - q.x, net[k].y - q.y) <= 8)
                return { anchor: k }
        return {}
    }
    // The net net0 (box px) as the drag of `what` goes from d0 to d (document px): a handle alone; a point with its
    // handles and the patches' inner points about it.
    function meshDragged(t0, net0, what, d0, d) {
        const m = lin(t0, d.x - d0.x, d.y - d0.y), n = side, out = net0.slice()
        const go = k => { out[k] = { x: net0[k].x + m.x, y: net0[k].y + m.y } }
        if (what.handle !== undefined) {
            go(what.handle)
        } else if (what.anchor !== undefined) {
            const i = Math.floor(what.anchor / n), j = what.anchor % n
            for (let a = Math.max(0, i - 1); a <= Math.min(n - 1, i + 1); a++)
                for (let b = Math.max(0, j - 1); b <= Math.min(n - 1, j + 1); b++)
                    go(a * n + b)
        }
        return out
    }
    readonly property string meshPath: {
        const c = net, n = side
        if (c.length !== n * n)
            return ""
        const pt = k => c[k].x.toFixed(2) + " " + c[k].y.toFixed(2)
        let d = ""
        for (let i = 0; i < n; i += 3) {
            d += "M" + pt(i * n)
            for (let j = 0; j + 3 < n; j += 3)
                d += "C" + pt(i * n + j + 1) + " " + pt(i * n + j + 2) + " " + pt(i * n + j + 3)
        }
        for (let j = 0; j < n; j += 3) {
            d += "M" + pt(j)
            for (let i = 0; i + 3 < n; i += 3)
                d += "C" + pt((i + 1) * n + j) + " " + pt((i + 2) * n + j) + " " + pt((i + 3) * n + j)
        }
        return d
    }

    // ---- the pointer ------------------------------------------------------------------------------------------------
    HoverHandler {
        id: hover
        parent: tb.pointerArea ?? tb
        enabled: tb.ok && tb.visible && tb.interactive
        cursorShape: tb.cursorWanted
    }
    readonly property point pointer: drag.pressed ? Qt.point(drag.mouseX, drag.mouseY) : hover.hovered ? mapFromItem(hover.parent, hover.point.position) : Qt.point(-1e6, -1e6)
    readonly property var under: drag.pressed ? drag.what : ok && hover.hovered ? (warp ? null : hitAt({ x: pointer.x, y: pointer.y })) : null
    // Outside the frame: it turns there, and shows so.
    readonly property bool turning: ok && !warp && under === "out"
    readonly property var cursorWanted: {
        if (turning)
            return Qt.BlankCursor
        if (typeof under !== "number")
            return under === "ref" ? Qt.CrossCursor : undefined  // else the canvas's
        const c = pts[under], m = pts[4], a = ((Math.atan2(c.y - m.y, c.x - m.x) * 180 / Math.PI) % 180 + 180) % 180
        return a < 22.5 || a >= 157.5 ? Qt.SizeHorCursor : a < 67.5 ? Qt.SizeFDiagCursor : a < 112.5 ? Qt.SizeVerCursor : Qt.SizeBDiagCursor
    }
    MouseArea {  // the left button's drags; it keeps the transform at the press and makes each step from it
        id: drag
        property var t0: null
        property var net0: null
        property var d0: null  // the press, document px
        property var what: null  // hitAt()'s, or meshHit()'s
        anchors.fill: parent
        enabled: tb.ok && tb.interactive
        acceptedButtons: Qt.LeftButton
        onPressed: m => {
            forceActiveFocus()  // off a field of the bar, as a press on the canvas
            const q = { x: m.x, y: m.y }
            here = Qt.point(m.x, m.y)
            t0 = tb.current()
            d0 = View.toDoc(tb.view, tb.size, q)
            if (tb.warp) {
                net0 = tb.netOf(t0)
                what = tb.meshHit(q)
                if (what.anchor !== undefined)
                    tb.pick = what.anchor
                else if (what.handle === undefined)
                    tb.pick = -1
            } else {
                what = tb.hitAt(q)
            }
        }
        property point here  // the pointer, as the last move left it
        onPositionChanged: m => {
            here = Qt.point(m.x, m.y)
            go(m.modifiers)
        }
        function go(mods) {
            if (!t0 || what === null || !tb.ok)
                return
            const d = View.toDoc(tb.view, tb.size, { x: here.x, y: here.y })
            if (tb.warp) {
                if (what.anchor !== undefined || what.handle !== undefined)
                    tb.warped(tb.meshDragged(t0, net0, what, d0, d))
            } else {
                tb.transformed(tb.dragged(t0, what, d0, d, mods))
            }
        }
        // Shift, Alt, Ctrl pressed or let go while the pointer stays: the drag as they say now (the key goes on).
        Keys.onPressed: e => {
            if (pressed && (e.key === Qt.Key_Shift || e.key === Qt.Key_Alt || e.key === Qt.Key_Control))
                go(e.modifiers | tb.bit(e.key))
            e.accepted = false
        }
        Keys.onReleased: e => {  // a modifier's own release still has it (X11, offscreen): without it
            if (pressed && (e.key === Qt.Key_Shift || e.key === Qt.Key_Alt || e.key === Qt.Key_Control))
                go(e.modifiers & ~tb.bit(e.key))
            e.accepted = false
        }
        onReleased: what = null
        onCanceled: what = null
    }

    // ---- drawn ------------------------------------------------------------------------------------------------------
    // The frame's corners on the canvas, whole px; its edge as a path d px outside them (mitred).
    readonly property var whole: corner4.map(q => ({ x: Math.round(q.x), y: Math.round(q.y) }))
    function n2(v) { return String(Math.round(v * 100) / 100) }  // a path's number
    function outline(c, d) {
        if (c.length !== 4)
            return ""
        let area = 0
        for (let i = 0; i < 4; i++)
            area += c[i].x * c[(i + 1) % 4].y - c[(i + 1) % 4].x * c[i].y
        const sg = area >= 0 ? 1 : -1, nrm = []
        for (let i = 0; i < 4; i++) {
            const e = { x: c[(i + 1) % 4].x - c[i].x, y: c[(i + 1) % 4].y - c[i].y }, l = Math.hypot(e.x, e.y) || 1
            nrm.push({ x: sg * e.y / l, y: -sg * e.x / l })
        }
        let s = ""
        for (let i = 0; i < 4; i++) {
            const a = nrm[(i + 3) % 4], b = nrm[i], k = d / Math.max(0.125, 1 + a.x * b.x + a.y * b.y)  // d from both sides
            s += (i ? "L" : "M") + n2(c[i].x + (a.x + b.x) * k) + " " + n2(c[i].y + (a.y + b.y) * k)
        }
        return s + "Z"
    }
    Shape {  // the frame's edge (not Warp): white just outside it, then dark, as the artboard's
        anchors.fill: parent
        visible: tb.ok && !tb.warp
        ShapePath {
            strokeColor: Qt.rgba(0, 0, 0, 0.45)
            strokeWidth: 1
            fillColor: "transparent"
            PathSvg { path: tb.outline(tb.whole, 1.5) }
        }
        ShapePath {
            strokeColor: "#ffffff"
            strokeWidth: 1
            fillColor: "transparent"
            PathSvg { path: tb.outline(tb.whole, 0.5) }
        }
    }
    Shape {  // Warp: its grid, white on dark, and the Bezier handles' arms
        id: grid
        anchors.fill: parent
        visible: tb.ok && tb.warp
        readonly property string arms: {
            let d = ""
            for (const k of tb.bezier) {
                const a = tb.net[tb.anchorOf(k)], h = tb.net[k]
                if (a && h)
                    d += "M" + a.x.toFixed(2) + " " + a.y.toFixed(2) + "L" + h.x.toFixed(2) + " " + h.y.toFixed(2)
            }
            return d
        }
        ShapePath {
            strokeColor: Qt.rgba(0, 0, 0, 0.55)
            strokeWidth: 3
            fillColor: "transparent"
            PathSvg { path: tb.meshPath }
        }
        ShapePath {
            strokeColor: "#ffffff"
            strokeWidth: 1
            fillColor: "transparent"
            PathSvg { path: tb.meshPath + grid.arms }
        }
    }
    Repeater {  // Warp's Bezier handles shown
        model: tb.ok && tb.warp ? tb.bezier : []
        Rectangle {
            required property int modelData
            readonly property var c: tb.net[modelData] ?? { x: 0, y: 0 }
            x: c.x - 3.5
            y: c.y - 3.5
            width: 7
            height: 7
            radius: 3.5
            color: "#ffffff"
            border.color: "#1c1c1e"
        }
    }
    Repeater {  // Warp's grid points: the corners as Free Transform's handles, the rest smaller; the picked one dark
        model: tb.ok && tb.warp ? tb.gridPts : []
        Rectangle {
            id: pt
            required property int modelData
            readonly property var c: tb.net[modelData] ?? { x: 0, y: 0 }
            readonly property int n: tb.side
            readonly property bool corner: (modelData === 0 || modelData === n - 1 || modelData === n * (n - 1) || modelData === n * n - 1)
            width: corner ? 10 : 8
            height: width
            x: Math.round(c.x) - width / 2
            y: Math.round(c.y) - width / 2
            rotation: tb.turn
            color: modelData === tb.pick ? "#ffffff" : "#1c1c1e"
            Rectangle { x: 1; y: 1; width: pt.width - 2; height: pt.width - 2; color: pt.modelData === tb.pick ? "#1c1c1e" : "#ffffff" }
        }
    }
    Repeater {  // the 8 handles (not Warp), turned with the frame's top edge
        model: tb.ok && !tb.warp ? [0, 1, 2, 3, 5, 6, 7, 8] : []
        Rectangle {
            required property int modelData
            readonly property var c: tb.pts[modelData] ?? { x: 0, y: 0 }
            x: Math.round(c.x) - 5
            y: Math.round(c.y) - 5
            width: 10
            height: 10
            rotation: tb.turn
            color: "#1c1c1e"
            Rectangle { x: 1; y: 1; width: 8; height: 8; color: "#ffffff" }
        }
    }
    Rectangle {  // the reference point
        visible: tb.ok && !tb.warp
        x: Math.round(tb.ring.x) - 8
        y: Math.round(tb.ring.y) - 8
        width: 16
        height: 16
        radius: 8
        color: "transparent"
        border.color: Qt.rgba(0, 0, 0, 0.5)
        Rectangle {
            x: 1
            y: 1
            width: 14
            height: 14
            radius: 7
            color: "transparent"
            border.color: "#ffffff"
        }
    }
    CanvasBar.Hud {
        visible: tb.ok && !tb.warp
        x: Math.round(tb.topRight.x) + 36
        y: Math.round(tb.topRight.y) - 21
        text: "W " + Fmt.vn(tb.scaleX * 100, 1) + "% H " + Fmt.vn(tb.scaleY * 100, 1) + "%\n∠ " + Fmt.vn(tb.angle, 1) + "°"
    }
    Loader {  // the pointer outside the box (made when shown: Viewport's test finds the canvas's own Icon first)
        active: tb.turning
        x: tb.pointer.x - 11
        y: tb.pointer.y - 11
        sourceComponent: Icon {
            name: "rotate_right"
            size: 22
            color: "#ffffff"
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.9)
        }
    }
}
