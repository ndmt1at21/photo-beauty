import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import QtTest
import PhotoBeauty
import "../qml/View.js" as View
import "../qml/Fmt.js" as Fmt

// U34: dragging Free Transform's, Warp's, Crop's and Perspective Crop's handles on the mock canvas, as Photoshop 2025's.
// Each drag with each modifier against the frame worked out here in document space from the box's corners (not in the
// transform's terms), ±0,5 px on the canvas at 12,5 %, 100 %, 800 % and 100 % with the view turned 30°; then through
// Main: the bar's boxes follow both ways, Enter makes one step, Esc none, the arrows move the box (and not Main's keys),
// a real document's stay locked; shots mid-drag in build/shots/u34.
Item {
    id: root
    width: 1100
    height: 800
    Component { id: mainC; Main { width: 1440; height: 900 } }
    Component { id: frameC; Rectangle { width: 1034; height: 750; color: "#808080"; clip: true } }
    Component {
        id: xfC
        TransformBox {  // with its owner's part: what a drag makes, back in
            id: t
            onTransformed: s => {
                t.scaleX = s.scaleX
                t.scaleY = s.scaleY
                t.angle = s.angle
                t.skewX = s.skewX
                t.skewY = s.skewY
                t.pivot = s.pivot
                t.ref = s.ref
                t.refX = s.refX
                t.refY = s.refY
                t.corners = s.corners
            }
            onWarped: m => t.mesh = m
        }
    }
    Component {
        id: cropC
        CropBox {
            id: c
            onBoxEdited: (b, a) => {
                c.box = b
                c.angle = a
            }
            onQuadEdited: q => c.quad = q
        }
    }
    Component { id: shortcutC; Shortcut { property int hits: 0; onActivated: hits++ } }

    function find(item, pred) {
        for (const c of item.children) {
            if (pred(c))
                return c
            const f = find(c, pred)
            if (f)
                return f
        }
        return null
    }
    function all(item, pred, out) {
        out = out || []
        for (const c of item.children) {
            if (pred(c))
                out.push(c)
            all(c, pred, out)
        }
        return out
    }

    TestCase {
        id: tc
        name: "U34"
        when: windowShown
        readonly property var size: ({ w: 1034, h: 750, dpr: 1 })
        readonly property string shots: "/home/ndmt1at21/photo-beauty/build/shots/u34/"

        function init() {
            Docs.viewport = { w: 1034, h: 750 }
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanupTestCase() { init() }

        // ---- geometry, document px ---------------------------------------------------------------------------------
        function add(a, b) { return { x: a.x + b.x, y: a.y + b.y } }
        function sub(a, b) { return { x: a.x - b.x, y: a.y - b.y } }
        function mul(a, k) { return { x: a.x * k, y: a.y * k } }
        function dot(a, b) { return a.x * b.x + a.y * b.y }
        function unit(a) { const l = Math.hypot(a.x, a.y); return { x: a.x / l, y: a.y / l } }
        function mid(a, b) { return { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 } }
        function rotd(p, c, deg) {  // p turned deg° clockwise about c
            const r = deg * Math.PI / 180, d = sub(p, c)
            return { x: c.x + d.x * Math.cos(r) - d.y * Math.sin(r), y: c.y + d.x * Math.sin(r) + d.y * Math.cos(r) }
        }
        function ang(p, c) { return Math.atan2(p.y - c.y, p.x - c.x) * 180 / Math.PI }
        // A frame [TL, TR, BR, BL]'s 9 places, 0–8 row by row: corners, sides' middles, its middle.
        function places(c) { return [c[0], mid(c[0], c[1]), c[1], mid(c[3], c[0]), mid(c[0], c[2]), mid(c[1], c[2]), c[3], mid(c[3], c[2]), c[2]] }
        function rect(x, y, w, h) { return [{ x: x, y: y }, { x: x + w, y: y }, { x: x + w, y: y + h }, { x: x, y: y + h }] }
        function turned(c, w, h, deg) {  // a w × h box about c turned deg°: [TL, TR, BR, BL]
            return rect(c.x - w / 2, c.y - h / 2, w, h).map(p => rotd(p, c, deg))
        }

        // ---- the canvas -----------------------------------------------------------------------------------------------
        // The views: 12,5 %, 100 %, 800 %, and 100 % turned 30°, each centred 40, 30 px off the box's middle c.
        function views(c) {
            return [[0.125, 0], [1, 0], [8, 0], [1, 30]].map(v => ({ x: c.x + 40 / v[0], y: c.y + 30 / v[0], zoom: v[0], angle: v[1] }))
        }
        function at(v, p, sz) { return View.toCanvas(v, sz || size, p) }
        function doc(v, q, sz) { return View.toDoc(v, sz || size, q) }
        function whole(q) { return { x: Math.round(q.x), y: Math.round(q.y) } }
        function centre(item, f) { return item.mapToItem(f, item.width / 2, item.height / 2) }
        function near(a, b, eps, msg) {
            verify(Math.abs(a.x - b.x) <= eps && Math.abs(a.y - b.y) <= eps, (msg || "") + " " + JSON.stringify(a) + " ≠ " + JSON.stringify(b))
        }
        // A left drag on item from canvas point a to b (whole px) with modifiers, in steps; shot(), if given, mid-way.
        function drag(item, a, b, mods, shot) {
            mods = mods || Qt.NoModifier
            mousePress(item, a.x, a.y, Qt.LeftButton, mods)
            mouseMove(item, Math.round((a.x + b.x) / 2), Math.round((a.y + b.y) / 2), -1, Qt.LeftButton, mods)
            mouseMove(item, b.x, b.y, -1, Qt.LeftButton, mods)
            if (shot)
                shot()
            mouseRelease(item, b.x, b.y, Qt.LeftButton, mods)
        }
        function frame() { return createTemporaryObject(frameC, root) }
        // Free Transform's handles drawn (8, in places 0, 1, 2, 3, 5, 6, 7, 8) and its reference point.
        function handles(t) { return all(t, o => o instanceof Rectangle && o.width === 10 && o.visible) }
        function ringOf(t) { return find(t, o => o instanceof Rectangle && o.width === 16) }
        // The frame drawn as it should be: c (document px) and the reference point p, ±0,5 px.
        function frameIs(t, f, v, c, p, msg) {
            const want = places(c), hs = handles(t), ks = [0, 1, 2, 3, 5, 6, 7, 8]
            compare(hs.length, 8, msg)
            ks.forEach((k, i) => {
                near(t.pts[k], at(v, want[k]), 0.5, msg + " place " + k)
                near(centre(hs[i], f), at(v, want[k]), 0.5, msg + " handle " + k)
            })
            t.corner4.forEach((q, i) => near(q, at(v, c[i]), 0.5, msg + " corner " + i))
            if (p) {
                near(t.ring, at(v, p), 0.5, msg + " reference point")
                near(centre(ringOf(t), f), at(v, p), 0.5, msg + " reference point drawn")
            }
        }

        // ---- Free Transform: each drag with each modifier ------------------------------------------------------------
        // The frame c scaled by handle k going to t (document px) about a, along the box's own axes (it is not skewed);
        // prop: both axes by the factor of the one that went further. The reference point p goes along.
        function scaleTo(c, p, k, t, a, prop) {
            const u = unit(sub(c[1], c[0])), v = unit(sub(c[3], c[0])), h = places(c)[k]
            let fx = k % 3 === 1 ? 1 : dot(sub(t, a), u) / dot(sub(h, a), u), fy = Math.floor(k / 3) === 1 ? 1 : dot(sub(t, a), v) / dot(sub(h, a), v)
            if (prop)
                fx = fy = Math.abs(fx) >= Math.abs(fy) ? fx : fy
            const go = x => add(a, add(mul(u, fx * dot(sub(x, a), u)), mul(v, fy * dot(sub(x, a), v))))
            return { c: c.map(go), p: go(p), fx: fx, fy: fy }
        }
        // Each row: what is pressed (a place 0–8, "in", "out", "ref"), the modifiers, the drag (canvas px), the
        // Thiết lập's proportions, and the frame and reference point it makes from c, p as the pointer goes d0 → d.
        function test_xf_data() {
            const S = Qt.ShiftModifier, A = Qt.AltModifier, C = Qt.ControlModifier
            const across = k => 8 - k
            const sc = (k, alt, prop) => (c, p, d0, d) => scaleTo(c, p, k, add(places(c)[k], sub(d, d0)), alt ? p : places(c)[across(k)], prop)
            return [
                { tag: "corner: proportions kept", k: 8, mods: 0, by: [60, 25], prop: true, want: sc(8, false, true) },
                { tag: "corner Shift: free", k: 8, mods: S, by: [60, 25], prop: true, want: sc(8, false, false) },
                { tag: "corner Alt: about the reference point", k: 8, mods: A, by: [40, 50], prop: true, want: sc(8, true, true) },
                { tag: "corner Alt Shift", k: 0, mods: A | S, by: [-30, 20], prop: true, want: sc(0, true, false) },
                { tag: "corner, proportions off in Thiết lập: free", k: 2, mods: 0, by: [45, -20], prop: false, want: sc(2, false, false) },
                { tag: "corner Shift, proportions off: kept", k: 6, mods: S, by: [-25, 40], prop: false, want: sc(6, false, true) },
                { tag: "side: one way", k: 5, mods: 0, by: [50, 20], prop: true, want: sc(5, false, false) },
                { tag: "side Shift: still one way", k: 7, mods: S, by: [15, -40], prop: true, want: sc(7, false, false) },
                { tag: "side Alt", k: 3, mods: A, by: [-35, 10], prop: true, want: sc(3, true, false) },
                { tag: "outside: turns", k: "out", mods: 0, turn: 23, prop: true, want: (c, p, d0, d) => {
                    const a = ang(d, p) - ang(d0, p)
                    return { c: c.map(x => rotd(x, p, a)), p: p, angle: a }
                } },
                { tag: "outside Shift: by 15°", k: "out", mods: S, turn: 23, prop: true, want: (c, p, d0, d) => {
                    const a = Math.round((ang(d, p) - ang(d0, p)) / 15) * 15
                    return { c: c.map(x => rotd(x, p, a)), p: p, angle: a }
                } },
                { tag: "inside: moves", k: "in", mods: 0, by: [40, -30], prop: true, want: (c, p, d0, d) => ({ c: c.map(x => add(x, sub(d, d0))), p: add(p, sub(d, d0)) }) },
                { tag: "inside Shift: along 45°", k: "in", mods: S, by: [50, 12], prop: true, want: (c, p, d0, d) => {
                    const m = sub(d, d0), a = Math.round(Math.atan2(m.y, m.x) / (Math.PI / 4)) * Math.PI / 4, e = { x: Math.cos(a), y: Math.sin(a) }, g = mul(e, dot(m, e))
                    return { c: c.map(x => add(x, g)), p: add(p, g) }
                } },
                { tag: "the reference point", k: "ref", mods: 0, by: [70, 45], prop: true, want: (c, p, d0, d) => ({ c: c, p: add(p, sub(d, d0)) }) },
                { tag: "Ctrl corner: Distort", k: 0, mods: C, by: [-30, 20], prop: true, want: (c, p, d0, d) => ({ c: [add(c[0], sub(d, d0)), c[1], c[2], c[3]], p: p }) },
                { tag: "Ctrl side: Distort", k: 5, mods: C, by: [25, 35], prop: true, want: (c, p, d0, d) => ({ c: [c[0], add(c[1], sub(d, d0)), add(c[2], sub(d, d0)), c[3]], p: p }) },
                { tag: "Ctrl Shift top: Skew", k: 1, mods: C | S, by: [40, 15], prop: true, want: (c, p, d0, d) => {
                    const u = unit(sub(c[1], c[0])), g = mul(u, dot(sub(d, d0), u))
                    return { c: [add(c[0], g), add(c[1], g), c[2], c[3]], p: add(p, mul(g, 0.5)) }
                } },
                { tag: "Ctrl Shift left: Skew", k: 3, mods: C | S, by: [10, 30], prop: true, want: (c, p, d0, d) => {
                    const v = unit(sub(c[3], c[0])), g = mul(v, dot(sub(d, d0), v))
                    return { c: [add(c[0], g), c[1], c[2], add(c[3], g)], p: add(p, mul(g, 0.5)) }
                } },
                { tag: "Ctrl Alt Shift bottom: Skew about the reference point", k: 7, mods: C | A | S, by: [-35, 12], prop: true, want: (c, p, d0, d) => {
                    const u = unit(sub(c[1], c[0])), g = mul(u, dot(sub(d, d0), u))
                    return { c: [sub(c[0], g), sub(c[1], g), add(c[2], g), add(c[3], g)], p: p }
                } },
                { tag: "Ctrl Alt Shift corner, across: Perspective", k: 0, mods: C | A | S, by: [30, 8], prop: true, want: (c, p, d0, d) => {
                    const u = unit(sub(c[1], c[0])), g = mul(u, dot(sub(d, d0), u))
                    return { c: [add(c[0], g), sub(c[1], g), c[2], c[3]], p: p }
                } },
                { tag: "Ctrl Alt Shift corner, down: Perspective", k: 8, mods: C | A | S, by: [6, -35], prop: true, want: (c, p, d0, d) => {
                    const v = unit(sub(c[3], c[0])), g = mul(v, dot(sub(d, d0), v))
                    return { c: [c[0], sub(c[1], g), add(c[2], g), c[3]], p: p }
                } }
            ]
        }
        function test_xf(data) {
            const mid0 = { x: 1500, y: 2100 }
            for (const v of views(mid0)) {
                const f = frame(), w = 360 / v.zoom, h = 270 / v.zoom, tag = data.tag + " @ " + JSON.stringify(v)
                const t = createTemporaryObject(xfC, f, { width: f.width, height: f.height, size: size, view: v, docW: 4400, docH: 5600, proportional: data.prop,
                    box: { x: mid0.x - w / 2, y: mid0.y - h / 2, w: w, h: h } })
                const c = rect(mid0.x - w / 2, mid0.y - h / 2, w, h), p = mid0
                frameIs(t, f, v, c, p, tag + " before")
                let a, b
                if (data.k === "out") {  // from outside the top right corner, round the reference point
                    a = whole(add(at(v, c[1]), { x: 30, y: -30 }))
                    b = whole(at(v, rotd(doc(v, a), p, data.turn)))
                } else {
                    a = whole(data.k === "in" ? at(v, add(p, mul(sub(c[2], p), 0.4))) : data.k === "ref" ? at(v, p) : t.pts[data.k])
                    b = { x: a.x + data.by[0], y: a.y + data.by[1] }
                }
                drag(t, a, b, data.mods)
                const want = data.want(c, p, doc(v, a), doc(v, b))
                frameIs(t, f, v, want.c, want.p, tag)
                if (want.angle !== undefined)
                    verify(Math.abs(t.angle - want.angle) < 1e-6, tag + " angle " + t.angle + " ≠ " + want.angle)
                if (data.k === 8 && !(data.mods & Qt.AltModifier))  // the bar's W, H: the factors
                    verify(Math.abs(t.scaleX - want.fx) < 1e-9 && Math.abs(t.scaleY - want.fy) < 1e-9, tag + " W, H " + [t.scaleX, t.scaleY, want.fx, want.fy])
            }
        }
        // Drags after one another, each from what the last made: a turned box scales along its own axes; the reference
        // point moved, Alt scales and the turn go about it; a distorted box still scales, turns and moves.
        function test_xf_chain() {
            const mid0 = { x: 1500, y: 2100 }
            for (const v of views(mid0)) {
                const f = frame(), w = 360 / v.zoom, h = 270 / v.zoom, tag = JSON.stringify(v)
                const t = createTemporaryObject(xfC, f, { width: f.width, height: f.height, size: size, view: v, docW: 4400, docH: 5600, box: { x: mid0.x - w / 2, y: mid0.y - h / 2, w: w, h: h } })
                let c = rect(mid0.x - w / 2, mid0.y - h / 2, w, h), p = mid0
                const step = (what, by, mods, want, msg) => {
                    const a = whole(what === "ref" ? at(v, p) : what === "out" ? add(at(v, c[1]), { x: 28, y: -28 }) : t.pts[what])
                    const b = what === "out" ? whole(at(v, rotd(doc(v, a), p, by))) : { x: a.x + by[0], y: a.y + by[1] }
                    drag(t, a, b, mods)
                    const r = want(doc(v, a), doc(v, b))
                    c = r.c
                    p = r.p
                    frameIs(t, f, v, c, p, tag + " " + msg)
                }
                step("out", 35, 0, (d0, d) => ({ c: c.map(x => rotd(x, p, ang(d, p) - ang(d0, p))), p: p }), "turned 35°")
                step(8, [30, 40], Qt.ShiftModifier, (d0, d) => scaleTo(c, p, 8, add(c[2], sub(d, d0)), c[0], false), "then scaled along its axes")
                step(5, [-20, 15], 0, (d0, d) => scaleTo(c, p, 5, add(places(c)[5], sub(d, d0)), places(c)[3], false), "then its side")
                step("ref", [-60, 50], 0, (d0, d) => ({ c: c, p: add(p, sub(d, d0)) }), "the reference point moved")
                step(2, [25, -30], Qt.AltModifier, (d0, d) => scaleTo(c, p, 2, add(c[1], sub(d, d0)), p, true), "Alt about it")
                step("out", -40, 0, (d0, d) => ({ c: c.map(x => rotd(x, p, ang(d, p) - ang(d0, p))), p: p }), "turned about it")
                step(6, [-20, 25], Qt.ControlModifier, (d0, d) => ({ c: [c[0], c[1], c[2], add(c[3], sub(d, d0))], p: p }), "distorted")
                step("out", 20, 0, (d0, d) => ({ c: c.map(x => rotd(x, p, ang(d, p) - ang(d0, p))), p: p }), "distorted, turned")
            }
        }
        // Shift, Alt pressed and let go while the pointer stays: the drag as they say at once (Photoshop's).
        function test_live_modifiers() {
            const v = { x: 1500, y: 2100, zoom: 1, angle: 0 }, f = frame(), c = rect(1320, 1965, 360, 270), p = { x: 1500, y: 2100 }
            const t = createTemporaryObject(xfC, f, { width: f.width, height: f.height, size: size, view: v, docW: 4400, docH: 5600, box: { x: 1320, y: 1965, w: 360, h: 270 } })
            const a = whole(t.pts[8]), b = { x: a.x + 60, y: a.y + 20 }
            mousePress(t, a.x, a.y)
            mouseMove(t, b.x, b.y, -1, Qt.LeftButton)
            const t8 = add(c[2], sub(doc(v, b), doc(v, a)))
            frameIs(t, f, v, scaleTo(c, p, 8, t8, c[0], true).c, null, "kept")
            keyPress(Qt.Key_Shift)
            frameIs(t, f, v, scaleTo(c, p, 8, t8, c[0], false).c, null, "Shift down: free")
            keyRelease(Qt.Key_Shift)
            frameIs(t, f, v, scaleTo(c, p, 8, t8, c[0], true).c, null, "Shift up: kept")
            keyPress(Qt.Key_Alt)
            frameIs(t, f, v, scaleTo(c, p, 8, t8, p, true).c, null, "Alt down: about the reference point")
            keyRelease(Qt.Key_Alt)
            frameIs(t, f, v, scaleTo(c, p, 8, t8, c[0], true).c, null, "Alt up")
            mouseRelease(t, b.x, b.y)
            const cb = createTemporaryObject(cropC, f, { width: f.width, height: f.height, size: size, view: v, docW: 4400, docH: 5600, box: { x: 1320, y: 1965, w: 360, h: 270 } })
            const ca = whole(cb.pts[8]), cbb = { x: ca.x + 30, y: ca.y + 45 }, tc = add(c[2], sub(doc(v, cbb), doc(v, ca)))
            mousePress(cb, ca.x, ca.y)
            mouseMove(cb, cbb.x, cbb.y, -1, Qt.LeftButton)
            cropIs(cb, f, v, cropTo(c, 8, tc, false, 0), "crop: free")
            keyPress(Qt.Key_Shift)
            cropIs(cb, f, v, cropTo(c, 8, tc, false, 360 / 270), "crop: Shift down")
            keyRelease(Qt.Key_Shift)
            cropIs(cb, f, v, cropTo(c, 8, tc, false, 0), "crop: Shift up")
            mouseRelease(cb, cbb.x, cbb.y)
        }
        // The reference point dragged near one of the 9 places takes it (AnchorGrid's index); off them it is free.
        function test_xf_ref_snaps() {
            const v = views({ x: 1500, y: 2100 })[1], f = frame()
            const t = createTemporaryObject(xfC, f, { width: f.width, height: f.height, size: size, view: v, docW: 4400, docH: 5600, box: { x: 1320, y: 1965, w: 360, h: 270 } })
            drag(t, whole(t.ring), { x: Math.round(t.pts[2].x) - 90, y: Math.round(t.pts[2].y) + 60 })
            compare(t.pivot, -1, "off the 9 places: its own")
            verify(Math.abs(t.ref.x - (360 - 90) / 360) < 0.01 && Math.abs(t.ref.y - 60 / 270) < 0.01, JSON.stringify(t.ref))
            drag(t, whole(t.ring), { x: Math.round(t.pts[2].x) - 5, y: Math.round(t.pts[2].y) + 4 })
            compare([t.pivot, t.ref], [2, null], "near the top right corner: that place")
            near(t.ring, t.pts[2], 1e-6, "on the top right corner")
            const c0 = t.quad
            drag(t, whole(t.pts[6]), { x: Math.round(t.pts[6].x) - 30, y: Math.round(t.pts[6].y) + 20 }, Qt.AltModifier)
            near(t.quad[1], c0[1], 1e-6, "Alt: about it")
        }

        // The pointer over the box: resize arrows on the handles as they lie on the screen, a cross on the reference point,
        // the turning arrow outside (the canvas's own pointer hidden), the canvas's inside.
        function test_xf_pointer() {
            for (const v of [{ x: 1500, y: 2100, zoom: 1, angle: 0 }, { x: 1500, y: 2100, zoom: 1, angle: 90 }]) {
                const f = frame(), turned90 = v.angle === 90
                const t = createTemporaryObject(xfC, f, { width: f.width, height: f.height, size: size, view: v, docW: 4400, docH: 5600, box: { x: 1320, y: 1965, w: 360, h: 270 } })
                const over = (q, want, msg) => {
                    mouseMove(t, q.x, q.y)
                    compare(t.cursorWanted, want, msg + " @ " + v.angle)
                }
                over(whole(t.pts[8]), turned90 ? Qt.SizeBDiagCursor : Qt.SizeFDiagCursor, "bottom right")
                over(whole(t.pts[2]), turned90 ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor, "top right")
                over(whole(t.pts[5]), turned90 ? Qt.SizeVerCursor : Qt.SizeHorCursor, "right")
                over(whole(t.pts[1]), turned90 ? Qt.SizeHorCursor : Qt.SizeVerCursor, "top")
                over(whole(t.ring), Qt.CrossCursor, "the reference point")
                over(whole(add(t.ring, { x: 40, y: 30 })), undefined, "inside")
                verify(!t.turning)
                over(whole(add(t.pts[5], turned90 ? { x: 0, y: 40 } : { x: 40, y: 0 })), Qt.BlankCursor, "outside")
                verify(t.turning)
                const icon = find(t, o => o instanceof Icon && o.visible)
                verify(!!icon && icon.name === "rotate_right", "the turning arrow drawn")
            }
        }

        // ---- Warp: its points and Bezier handles -------------------------------------------------------------------
        function test_warp_data() { return [{ tag: "3 × 3", n: 3 }, { tag: "4 × 4", n: 4 }, { tag: "5 × 5", n: 5 }] }
        function test_warp(data) {
            const mid0 = { x: 1500, y: 2100 }, n = 3 * data.n + 1
            for (const v of views(mid0)) {
                const f = frame(), w = 360 / v.zoom, h = 270 / v.zoom, x0 = mid0.x - w / 2, y0 = mid0.y - h / 2, tag = data.tag + " @ " + JSON.stringify(v)
                const t = createTemporaryObject(xfC, f, { width: f.width, height: f.height, size: size, view: v, docW: 4400, docH: 5600, warp: true, cells: data.n,
                    box: { x: x0, y: y0, w: w, h: h } })
                let net = []
                for (let i = 0; i < n; i++)
                    for (let j = 0; j < n; j++)
                        net.push({ x: x0 + j / (n - 1) * w, y: y0 + i / (n - 1) * h })
                const points = () => all(t, o => o instanceof Rectangle && (o.width === 8 || o.width === 10) && o.visible && o.parent === t)
                const dots = () => all(t, o => o instanceof Rectangle && o.width === 7 && o.visible)
                const check = (msg) => {
                    const ps = points()
                    compare(ps.length, (data.n + 1) * (data.n + 1), msg + " points")
                    ps.forEach((r, i) => near(centre(r, f), at(v, net[t.gridPts[i]]), 0.5, msg + " point " + t.gridPts[i]))
                    dots().forEach((r, i) => near(centre(r, f), at(v, net[t.bezier[i]]), 0.5, msg + " handle " + t.bezier[i]))
                }
                check(tag + " flat")
                compare(handles(t).length, 4, "the corners as Free Transform's handles")
                const k = 3 * n + 3, a = whole(t.net[k]), b = { x: a.x + 25, y: a.y + 18 }  // the point at (1, 1) of the grid
                drag(t, a, b)
                compare(t.pick, k)
                const m = sub(doc(v, b), doc(v, a))
                for (let i = 2; i <= 4; i++)
                    for (let j = 2; j <= 4; j++)
                        net[i * n + j] = add(net[i * n + j], m)
                check(tag + " a point and its handles moved")
                compare(t.bezier.slice().sort((x, y) => x - y), [2 * n + 3, 3 * n + 2, 3 * n + 4, 4 * n + 3], "its 4 handles shown")
                const hk = 3 * n + 4, ha = whole(t.net[hk]), hb = { x: ha.x - 12, y: ha.y + 20 }
                drag(t, ha, hb)
                compare(t.pick, k, "a handle keeps the point picked")
                net[hk] = add(net[hk], sub(doc(v, hb), doc(v, ha)))
                check(tag + " one handle moved")
                verify(t.meshPath.indexOf("C") > 0)
            }
        }

        // ---- Crop: sides, corners, outside, inside; Perspective Crop ------------------------------------------------
        // A crop box (middle m, w × h, turned deg°) as a frame [TL, TR, BR, BL].
        function cropFrame(b, deg) { return turned({ x: b.x + b.w / 2, y: b.y + b.h / 2 }, b.w, b.h, deg) }
        // The box's corners c with handle k going to t, about the side across or (Alt) the middle; ar, the ratio kept (0 none).
        function cropTo(c, k, t, alt, ar) {
            const u = unit(sub(c[1], c[0])), v = unit(sub(c[3], c[0])), m = mid(c[0], c[2]), w = dot(sub(c[1], c[0]), u), h = dot(sub(c[3], c[0]), v)
            const sx = k % 3 - 1, sy = Math.floor(k / 3) - 1, lt = { x: dot(sub(t, m), u), y: dot(sub(t, m), v) }
            const span = (sd, half, tv) => alt ? [-Math.abs(tv), Math.abs(tv)] : tv >= -sd * half ? [-sd * half, tv] : [tv, -sd * half]
            let xs = sx ? span(sx, w / 2, lt.x) : [-w / 2, w / 2], ys = sy ? span(sy, h / 2, lt.y) : [-h / 2, h / 2]
            if (ar > 0) {
                let nw = xs[1] - xs[0], nh = ys[1] - ys[0]
                if (sx && sy) {
                    if (nw >= nh * ar)
                        nh = nw / ar
                    else
                        nw = nh * ar
                } else if (sx) {
                    nh = nw / ar
                } else {
                    nw = nh * ar
                }
                const fit = (sd, len, tv, half) => !sd || alt ? [-len / 2, len / 2] : tv >= -sd * half ? [-sd * half, -sd * half + len] : [-sd * half - len, -sd * half]
                xs = fit(sx, nw, lt.x, w / 2)
                ys = fit(sy, nh, lt.y, h / 2)
            }
            const lm = { x: (xs[0] + xs[1]) / 2, y: (ys[0] + ys[1]) / 2 }, nm = add(m, add(mul(u, lm.x), mul(v, lm.y))), hw = (xs[1] - xs[0]) / 2, hh = (ys[1] - ys[0]) / 2
            return [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(s => add(nm, add(mul(u, s[0] * hw), mul(v, s[1] * hh))))
        }
        function cropIs(cb, f, v, c, msg) {
            const want = places(c), ks = [0, 1, 2, 3, 5, 6, 7, 8]
            ks.forEach(k => near(cb.pts[k], at(v, want[k]), 0.5, msg + " place " + k))
        }
        function test_crop_data() {
            const S = Qt.ShiftModifier, A = Qt.AltModifier
            const by = (k, alt, ar) => (c, d0, d) => cropTo(c, k, add(places(c)[k], sub(d, d0)), alt, ar)
            return [
                { tag: "side", k: 5, mods: 0, by: [40, 10], aspect: 0, want: by(5, false, 0) },
                { tag: "side, ratio set", k: 5, mods: 0, by: [40, 0], aspect: 1.25, want: by(5, false, 1.25) },
                { tag: "top, ratio set", k: 1, mods: 0, by: [10, -30], aspect: 0.8, want: by(1, false, 0.8) },
                { tag: "side Alt", k: 3, mods: A, by: [-30, 5], aspect: 0, want: by(3, true, 0) },
                { tag: "corner", k: 8, mods: 0, by: [30, 45], aspect: 0, want: by(8, false, 0) },
                { tag: "corner Shift: its own ratio", k: 8, mods: S, by: [30, 45], aspect: 0, want: by(8, false, 360 / 270) },
                { tag: "corner, ratio set", k: 0, mods: 0, by: [-20, 35], aspect: 1, want: by(0, false, 1) },
                { tag: "corner Alt", k: 2, mods: A, by: [25, -15], aspect: 0, want: by(2, true, 0) },
                { tag: "turned box, side", k: 7, mods: 0, by: [12, 30], aspect: 0, deg: 20, want: by(7, false, 0) },
                { tag: "turned box, corner, ratio set", k: 6, mods: 0, by: [-25, 30], aspect: 1.5, deg: -35, want: by(6, false, 1.5) },
                { tag: "outside: turns", k: "out", mods: 0, turn: 18, aspect: 0, want: (c, d0, d) => {
                    const m = mid(c[0], c[2])
                    return c.map(x => rotd(x, m, ang(d, m) - ang(d0, m)))
                } },
                { tag: "inside: moves the box (Classic)", k: "in", mods: 0, by: [-35, 22], aspect: 0, want: (c, d0, d) => c.map(x => add(x, sub(d, d0))) }
            ]
        }
        function test_crop(data) {
            const mid0 = { x: 1500, y: 2100 }
            for (const v of views(mid0)) {
                const f = frame(), w = 360 / v.zoom, h = 270 / v.zoom, deg = data.deg || 0, tag = data.tag + " @ " + JSON.stringify(v)
                const cb = createTemporaryObject(cropC, f, { width: f.width, height: f.height, size: size, view: v, docW: 4400, docH: 5600, aspect: data.aspect,
                    box: { x: mid0.x - w / 2, y: mid0.y - h / 2, w: w, h: h }, angle: deg })
                const c = cropFrame(cb.box, deg)
                cropIs(cb, f, v, c, tag + " before")
                let a, b
                if (data.k === "out") {
                    a = whole(add(at(v, c[1]), { x: 30, y: -30 }))
                    b = whole(at(v, rotd(doc(v, a), mid0, data.turn)))
                } else {
                    a = whole(data.k === "in" ? at(v, add(mid0, mul(sub(c[2], mid0), 0.4))) : cb.pts[data.k])
                    b = { x: a.x + data.by[0], y: a.y + data.by[1] }
                }
                drag(cb, a, b, data.mods)
                cropIs(cb, f, v, data.want(c, doc(v, a), doc(v, b)), tag)
                if (data.aspect > 0)
                    verify(Math.abs(cb.box.w / cb.box.h - data.aspect) < 1e-9, tag + " ratio " + cb.box.w / cb.box.h)
            }
        }
        // The crop's drawn parts follow the box: the shade's hole, its edge, the HUD under its bottom right.
        function test_crop_drawn() {
            const v = { x: 1500, y: 2100, zoom: 1, angle: 30 }, f = frame()
            const cb = createTemporaryObject(cropC, f, { width: f.width, height: f.height, size: size, view: v, docW: 4400, docH: 5600, box: { x: 1320, y: 1965, w: 360, h: 270 }, angle: 15 })
            const img = grabImage(f), m = { x: 1500, y: 2100 }, inside = at(v, rotd({ x: 1650, y: 2050 }, m, 15)), outside = at(v, rotd({ x: 1700, y: 2050 }, m, 15))
            const pi = img.pixel(Math.round(inside.x), Math.round(inside.y)), po = img.pixel(Math.round(outside.x), Math.round(outside.y))
            verify(Math.abs(pi.r - 0.5) < 0.03, "clear inside the turned box " + pi)
            verify(po.r < 0.3, "shaded outside it " + po)
            const hud = find(cb, o => o instanceof Label && o.visible)
            compare(hud.text, "360 × 270 px\n∠ 15,0°")
            near({ x: hud.x, y: hud.y - 8 }, whole(at(v, rotd({ x: 1680, y: 2235 }, m, 15))), 1, "HUD")
        }
        function test_pcrop() {
            for (const v of views({ x: 1500, y: 2100 })) {
                const f = frame(), tag = JSON.stringify(v)
                const cb = createTemporaryObject(cropC, f, { width: f.width, height: f.height, size: size, view: v, docW: 4400, docH: 5600, perspective: true })
                verify(!cb.shown, "nothing before a drag")
                mouseClick(cb, 400, 300)
                compare(cb.quad, null, "a click draws nothing")
                drag(cb, { x: 330, y: 220 }, { x: 650, y: 470 })  // draws the corners: the dragged rectangle on the screen
                let q = [{ x: 330, y: 220 }, { x: 650, y: 220 }, { x: 650, y: 470 }, { x: 330, y: 470 }].map(p => doc(v, p))
                cropIs(cb, f, v, q, tag + " drawn")
                const step = (k, by, ids, msg) => {
                    const a = whole(k === "in" ? at(v, mid(q[0], q[2])) : cb.pts[k]), b = { x: a.x + by[0], y: a.y + by[1] }, m = sub(doc(v, b), doc(v, a))
                    drag(cb, a, b)
                    q = q.map((p, i) => ids.indexOf(i) >= 0 ? add(p, m) : p)
                    cropIs(cb, f, v, q, tag + " " + msg)
                    const drawn = all(cb, o => o instanceof Rectangle && o.width === 8 && o.visible), ks = [0, 1, 2, 3, 5, 6, 7, 8]
                    ks.forEach((k2, i) => near(centre(drawn[i], f), at(v, places(q)[k2]), 0.5, tag + " " + msg + " handle " + k2))
                }
                step(2, [30, -20], [1], "a corner, free")
                step(6, [-15, 25], [3], "another")
                step(7, [8, 20], [2, 3], "a side: its two corners")
                step("in", [-15, 10], [0, 1, 2, 3], "inside: all four")
                const grid = find(cb, o => o instanceof Shape && o.lines !== undefined)
                compare(grid.lines.split("M").length - 1, 5, "the edge and 4 lines of the grid")
                cb.grid = false
                compare(grid.lines.split("M").length - 1, 1, "Show Grid off")
            }
        }

        // ---- through Main ------------------------------------------------------------------------------------------
        property var w: null
        function main() {
            if (!w) {
                w = mainC.createObject(root, { startState: "Main" })
                w.requestActivate()
                tryVerify(() => w.active)
            }
            Docs.viewport = { w: 1034, h: 750 }
            Docs.reset()
            Tools.reset()
            Prefs.reset()
            waitForRendering(w.contentItem)
            return w
        }
        function overlays() { return find(main().contentItem, o => o instanceof Overlays) }
        function parts() {
            const ov = overlays()
            return { ov: ov, xf: find(ov, o => o instanceof TransformBox), crop: find(ov, o => o instanceof CropBox), bar: find(w.contentItem, o => o instanceof OptionsBar) }
        }
        function cells(bar) { return find(bar, o => o instanceof RowLayout).children.filter(c => c instanceof Loader) }
        function field(bar, i) { return find(cells(bar)[i].item, o => o instanceof NumberField) }
        function type(nf, text) {
            const tf = find(nf, o => o instanceof TextField)
            mouseClick(tf)
            tryVerify(() => tf.activeFocus)
            keySequence("Ctrl+A")
            for (const ch of text)
                keyClick(ch)
            keyClick(Qt.Key_Return)
        }
        function steps(d) { return d.history.length }
        function cleanup() {
            if (w)
                w.contentItem.forceActiveFocus()
        }

        // The bar's boxes as a drag leaves them: W, H, the angle, H skew, X, Y; Enter makes one step, Esc none.
        function test_main_bar() {
            const c = parts(), d = Docs.current, ov = c.ov
            Tools.setTool("move", "move")
            verify(Docs.exec("@xf") && Tools.xf)
            waitForRendering(w.contentItem)
            const v = ov.view, sz = ov.size, t = c.xf, n0 = steps(d)
            const box = rect(0, 0, 4400, 5600), p = { x: 2200, y: 2800 }
            compare(field(c.bar, 3).shown, "2.200 px")
            const a = whole(t.pts[8]), b = { x: a.x - 60, y: a.y - 40 }
            drag(ov, a, b)
            const want = scaleTo(box, p, 8, add(box[2], sub(doc(v, b, sz), doc(v, a, sz))), box[0], true)
            verify(Math.abs(Tools.value("xf:7") - want.fx * 100) < 1e-6 && Math.abs(Tools.value("xf:9") - want.fy * 100) < 1e-6, [Tools.value("xf:7"), want.fx])
            compare(field(c.bar, 7).shown, Fmt.unit(want.fx * 100, "%", 0))
            compare(field(c.bar, 9).shown, Fmt.unit(want.fy * 100, "%", 0))
            compare([field(c.bar, 3).shown, field(c.bar, 5).shown], [Fmt.unit(want.p.x, "px", 0), Fmt.unit(want.p.y, "px", 0)], "X, Y: the reference point went along")
            compare(find(t, o => o instanceof Label && o.visible).text, "W " + Fmt.vn(want.fx * 100, 1) + "% H " + Fmt.vn(want.fy * 100, 1) + "%\n∠ 0,0°")
            const o = whole(add(t.pts[5], { x: 40, y: 0 })), o2 = whole(at(v, rotd(doc(v, o, sz), want.p, 30), sz))  // right of it
            drag(ov, o, o2)
            const turn = ang(doc(v, o2, sz), want.p) - ang(doc(v, o, sz), want.p)
            verify(Math.abs(Tools.value("xf:11") - turn) < 1e-6, Tools.value("xf:11") + " " + turn)
            compare(field(c.bar, 11).shown, Fmt.unit(turn, "°", 2))
            const s0 = whole(t.pts[1])
            drag(ov, s0, { x: s0.x + 30, y: s0.y }, Qt.ControlModifier | Qt.ShiftModifier)
            verify(Math.abs(Tools.value("xf:13")) > 1, "H skew " + Tools.value("xf:13"))
            compare(field(c.bar, 13).shown, Fmt.unit(Tools.value("xf:13"), "°", 1))
            const s1 = whole(t.pts[5])
            drag(ov, s1, { x: s1.x, y: s1.y + 30 }, Qt.ControlModifier | Qt.ShiftModifier)
            verify(Math.abs(Tools.value("xf:14")) > 1, "V skew " + Tools.value("xf:14"))
            compare(field(c.bar, 14).shown, Fmt.unit(Tools.value("xf:14"), "°", 1))
            compare(steps(d), n0, "no step while dragging")
            keyClick(Qt.Key_Return)
            compare(steps(d), n0 + 1, "Enter: one step")
            compare(d.history[d.index].name, "Free Transform")
            verify(!Tools.xf)
            verify(Docs.exec("@xf"))
            waitForRendering(w.contentItem)
            compare([Tools.value("xf:7"), Tools.value("xf:11"), Tools.value("xf:13")], [100, 0, 0], "a new Free Transform starts from the box")
            drag(ov, whole(t.pts[8]), { x: Math.round(t.pts[8].x) + 40, y: Math.round(t.pts[8].y) + 10 })
            keyClick(Qt.Key_Escape)
            compare(steps(d), n0 + 1, "Esc: no step")
            verify(!Tools.xf)
            verify(Docs.exec("@warpOn"))
            waitForRendering(w.contentItem)
            drag(ov, whole(t.net[0]), { x: Math.round(t.net[0].x) + 20, y: Math.round(t.net[0].y) + 30 })
            verify(!!Tools.value("warp:mesh"), "Warp's net in Tools")
            keyClick(Qt.Key_Enter)
            compare([steps(d), d.history[d.index].name], [n0 + 2, "Warp"])
        }
        // Boxes typed in the bar move the box; the link takes H along W; a place picked in the bar leaves the box where it
        // is; Relative counts X, Y from where the reference point was.
        function test_main_bar_in() {
            const c = parts(), ov = c.ov, t = c.xf
            Tools.setTool("move", "move")
            Docs.exec("@xf")
            waitForRendering(w.contentItem)
            const v = ov.view, sz = ov.size
            type(field(c.bar, 7), "50")
            compare([Tools.value("xf:7"), Tools.value("xf:9")], [50, 50], "W typed, linked: H too")
            t.quad.forEach((q, i) => near(q, rect(1100, 1400, 2200, 2800)[i], 1e-6, "scaled about the middle " + i))
            type(field(c.bar, 9), "80")
            compare([Tools.value("xf:7"), Tools.value("xf:9")], [80, 80], "H typed, linked: W too")
            Tools.setValue("xf:8", false)
            type(field(c.bar, 9), "40")
            compare([Tools.value("xf:7"), Tools.value("xf:9")], [80, 40], "unlinked")
            const was = t.quad
            mouseClick(find(cells(c.bar)[2].item, o => o instanceof AbstractButton))  // AnchorGrid's top left
            compare(Tools.value("xf:2"), 0)
            t.quad.forEach((q, i) => near(q, was[i], 1e-6, "the box stays " + i))
            near(t.p, was[0], 1e-6, "the reference point on its top left")
            compare(field(c.bar, 3).shown, Fmt.unit(was[0].x, "px", 0))
            type(field(c.bar, 3), "1000")
            near(t.quad[0], { x: 1000, y: was[0].y }, 1e-6, "X typed moves it")
            Tools.setValue("xf:4", true)
            compare([Tools.value("xf:3"), Tools.value("xf:5")], [0, 0], "Relative: from here")
            type(field(c.bar, 5), "-200")
            near(t.quad[0], { x: 1000, y: was[0].y - 200 }, 1e-6, "Y typed, relative")
            Tools.setValue("xf:4", false)
            compare(Tools.value("xf:5"), was[0].y - 200)
            keyClick(Qt.Key_Escape)
        }
        // The arrows: 1 px, Shift 10; taken before Main's keys (none of a key of the same arrow runs) while the box is up.
        function test_main_keys() {
            const c = parts(), ov = c.ov, t = c.xf, d = Docs.current, n0 = steps(d)
            const left = createTemporaryObject(shortcutC, w, { sequence: "Left" }), shiftDown = createTemporaryObject(shortcutC, w, { sequence: "Shift+Down" })
            Tools.setTool("move", "move")  // Move: the tool whose own arrows (U31) the box's take over
            Docs.exec("@xf")
            waitForRendering(w.contentItem)
            mouseClick(ov, ov.width / 2, ov.height / 2)  // the focus on the canvas
            const p0 = t.p
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Right, Qt.ShiftModifier)
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            keyClick(Qt.Key_Up)
            near(t.p, { x: p0.x - 1 + 10, y: p0.y + 10 - 1 }, 1e-9, "1 px, Shift 10")
            compare([left.hits, shiftDown.hits], [0, 0], "the arrows are the box's")
            keyClick(Qt.Key_Escape)
            compare(steps(d), n0, "Esc: no step")
            Tools.setTool("brush", "brush")  // a tool no key of Main's has the arrows for
            keyClick(Qt.Key_Left)
            compare(left.hits, 1, "without the box the key is Main's again")
            Tools.setTool("crop", "crop")
            waitForRendering(w.contentItem)
            const b0 = c.crop.box
            keyClick(Qt.Key_Right, Qt.ShiftModifier)
            keyClick(Qt.Key_Up)
            compare([c.crop.box.x, c.crop.box.y, c.crop.box.w], [b0.x + 10, b0.y - 1, b0.w], "the crop box too")
            compare(left.hits, 1)
            keyClick(Qt.Key_Escape)
            compare(c.crop.box, Tools.cropBox(d.w, d.h), "Esc: the box back")
            compare([steps(d), Tools.value("crop:box", null)], [n0, null])
        }
        // Space held during Free Transform: the canvas pans (the Hand for a moment, U5), the box takes nothing.
        function test_main_space() {
            const c = parts(), ov = c.ov, t = c.xf, d = Docs.current
            d.setView({ zoom: 1 })
            Tools.setTool("move", "move")
            Docs.exec("@xf")
            waitForRendering(w.contentItem)
            mouseClick(ov, ov.width / 2, ov.height / 2)
            const v0 = ov.view
            keyPress(Qt.Key_Space)
            tryCompare(Tools, "tool", "hand")
            verify(!t.interactive)
            drag(ov, { x: 500, y: 400 }, { x: 560, y: 360 })
            keyRelease(Qt.Key_Space)
            tryCompare(Tools, "tool", "move")
            verify(Math.abs(d.view.x - v0.x) > 10, "panned " + JSON.stringify([d.view, v0]))
            compare([Tools.value("xf:7", 100), Tools.value("xf:3", 2200)], [100, 2200], "the box untouched")
            verify(Tools.xf && t.interactive)
            keyClick(Qt.Key_Escape)
        }
        // Thiết lập › Free Transform giữ tỉ lệ off: a corner scales freely, Shift keeps the proportions; the link shows it.
        function test_main_prefs() {
            const c = parts(), ov = c.ov, t = c.xf
            Prefs.set("transformProportional", false)
            Tools.setTool("move", "move")
            Docs.exec("@xf")
            waitForRendering(w.contentItem)
            compare(Tools.value("xf:8"), false)
            verify(!cells(c.bar)[8].item.checked, "the link off")
            const v = ov.view, sz = ov.size, box = rect(0, 0, 4400, 5600)
            let a = whole(t.pts[8]), b = { x: a.x - 50, y: a.y - 20 }
            drag(ov, a, b)
            const free = scaleTo(box, { x: 2200, y: 2800 }, 8, add(box[2], sub(doc(v, b, sz), doc(v, a, sz))), box[0], false)
            verify(Math.abs(Tools.value("xf:7") - free.fx * 100) < 1e-6 && Math.abs(Tools.value("xf:9") - free.fy * 100) < 1e-6 && Math.abs(free.fx - free.fy) > 0.01)
            a = whole(t.pts[8])
            b = { x: a.x - 30, y: a.y + 10 }
            const c0 = t.quad, p0 = t.p
            drag(ov, a, b, Qt.ShiftModifier)
            const kept = scaleTo(c0, p0, 8, add(c0[2], sub(doc(v, b, sz), doc(v, a, sz))), c0[0], true)
            t.quad.forEach((q, i) => near(at(v, q, sz), at(v, kept.c[i], sz), 0.5, "Shift: kept " + i))
            keyClick(Qt.Key_Escape)
        }
        // Crop: a corner keeps the bar's ratio; Enter crops to the box as dragged (the bar's ✓ too), Esc leaves it; a turn
        // outside. Perspective Crop: drawn, a corner moved, Enter one step, Esc none.
        function test_main_crop() {
            const c = parts(), ov = c.ov, d = Docs.current, n0 = steps(d)
            Tools.setTool("crop", "crop")
            waitForRendering(w.contentItem)
            const cb = c.crop, v = ov.view, sz = ov.size
            compare(cb.box, Tools.cropBox(4400, 5600))
            drag(ov, whole(cb.pts[8]), { x: Math.round(cb.pts[8].x) - 80, y: Math.round(cb.pts[8].y) - 10 })
            verify(Math.abs(cb.box.w / cb.box.h - 0.8) < 1e-9, "4:5 kept")
            compare(Tools.cropBox(4400, 5600), Tools.value("crop:box"), "Tools.cropBox() gives it")
            const want = [Math.round(cb.box.w), Math.round(cb.box.h)]
            compare(find(cb, o => o instanceof Label && o.visible).text, "4:5 · " + Fmt.vn(want[0]) + " × " + Fmt.vn(want[1]) + " px")
            keyClick(Qt.Key_Return)
            compare([steps(d), d.history[d.index].name, d.w, d.h], [n0 + 1, "Crop", want[0], want[1]], "Enter: one step, the box's size")
            compare(Tools.value("crop:box", null), null, "a new box on the cropped document")
            drag(ov, whole(cb.pts[5]), { x: Math.round(cb.pts[5].x) - 50, y: Math.round(cb.pts[5].y) })
            const w2 = [Math.round(cb.box.w), Math.round(cb.box.h)]
            mouseClick(cells(c.bar)[15].item)  // ✓ Áp dụng
            compare([steps(d), d.w, d.h], [n0 + 2, w2[0], w2[1]], "the bar's ✓: the same")
            const o = whole(add(cb.pts[5], { x: 40, y: 0 })), v2 = ov.view  // the cropped document's view
            drag(ov, o, whole(at(v2, rotd(doc(v2, o, sz), { x: d.w / 2, y: d.h / 2 }, 12), sz)))
            verify(Math.abs(cb.angle - 12) < 0.6, "turned " + cb.angle)
            keyClick(Qt.Key_Escape)
            compare([steps(d), cb.angle], [n0 + 2, 0], "Esc: no step, not turned")
            Tools.setTool("pcrop", "crop")
            waitForRendering(w.contentItem)
            drag(ov, { x: 400, y: 200 }, { x: 640, y: 520 })
            verify(cb.shown && !!Tools.value("pcrop:quad"))
            drag(ov, whole(cb.pts[0]), { x: Math.round(cb.pts[0].x) + 30, y: Math.round(cb.pts[0].y) + 15 })
            keyClick(Qt.Key_Escape)
            compare([steps(d), Tools.value("pcrop:quad", null)], [n0 + 2, null], "Esc: no step, no corners")
            drag(ov, { x: 400, y: 200 }, { x: 640, y: 520 })
            keyClick(Qt.Key_Return)
            compare([steps(d), d.history[d.index].name, Tools.value("pcrop:quad", null)], [n0 + 3, "Perspective Crop", null])
        }
        // The Perspective Crop bar's ✓ and ✗ (cells 12, 11) as Enter and Esc: one step "Perspective Crop", none.
        function test_main_pcrop_bar() {
            const c = parts(), ov = c.ov, d = Docs.current, n0 = steps(d)
            Tools.setTool("pcrop", "crop")
            waitForRendering(w.contentItem)
            const cb = c.crop, bar = cells(c.bar)
            mouseClick(bar[12].item)
            compare([steps(d), cb.shown], [n0, false], "✓ with no corners: no step")
            drag(ov, { x: 400, y: 200 }, { x: 640, y: 520 })
            verify(cb.shown && !!Tools.value("pcrop:quad"))
            mouseClick(bar[11].item)
            compare([steps(d), Tools.value("pcrop:quad", null), cb.shown, Tools.tool], [n0, null, false, "pcrop"], "✗: no step, no corners, the tool kept")
            drag(ov, { x: 400, y: 200 }, { x: 640, y: 520 })
            drag(ov, whole(cb.pts[8]), { x: Math.round(cb.pts[8].x) + 25, y: Math.round(cb.pts[8].y) + 10 })
            mouseClick(bar[12].item)
            compare([steps(d), d.history[d.index].name, Tools.value("pcrop:quad", null), cb.shown], [n0 + 1, "Perspective Crop", null, false], "✓: one step")
            compare([d.w, d.h], [4400, 5600], "the mock keeps its size")
            d.undo()
            compare(d.index, n0 - 1, "undone")
        }
        // A real document: the core's to come (N49): the box takes no drag.
        function test_main_real() {
            const c = parts(), ov = c.ov
            Tools.real = true
            Docs.exec("@xf")
            waitForRendering(w.contentItem)
            verify(c.xf.visible && !c.xf.interactive)
            const a = whole(c.xf.pts[8])
            drag(ov, a, { x: a.x - 50, y: a.y - 50 })
            compare(Tools.value("xf:7", 100), 100)
            Tools.real = false
            Tools.reset()
        }
        // Shots mid-drag, each from a fresh box, the view turned and not (build/shots/u34).
        function test_shots() {
            const c = parts(), ov = c.ov, t = c.xf, cb = c.crop, d = Docs.current
            const shot = name => () => {
                waitForRendering(w.contentItem)
                grabImage(w.contentItem).save(shots + name + ".png")
            }
            const go = (cmd) => {
                if (Tools.xf)
                    keyClick(Qt.Key_Escape)
                Docs.exec(cmd)
                waitForRendering(w.contentItem)
            }
            // A drag of place k of item it (its pts) by dx, dy, shot mid-way.
            const by = (it, k, dx, dy, mods, name) => {
                const a = whole(it.pts[k])
                drag(ov, a, { x: a.x + dx, y: a.y + dy }, mods, shot(name))
            }
            d.setView({ zoom: 0.09 })
            Tools.setTool("move", "move")
            go("@xf")
            by(t, 2, -70, 40, 0, "xf-scale")
            go("@xf")
            const o = whole(add(t.pts[5], { x: 40, y: 0 }))
            drag(ov, o, whole(at(ov.view, rotd(doc(ov.view, o, ov.size), t.p, 25), ov.size)), 0, shot("xf-turn"))
            go("@xf")
            by(t, 0, 60, 30, Qt.ControlModifier, "xf-distort")
            go("@xf")
            by(t, 8, 50, 0, Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier, "xf-perspective")
            go("@xf")
            by(t, 5, 0, -50, Qt.ControlModifier | Qt.ShiftModifier, "xf-skew")
            go("@warpOn")
            Tools.setValue("warp:3", "4 × 4")
            waitForRendering(w.contentItem)
            const k = 3 * 13 + 3  // the point (1, 1) of the 4 × 4 grid, in its net of 13 × 13
            drag(ov, whole(t.net[k]), { x: Math.round(t.net[k].x) + 50, y: Math.round(t.net[k].y) + 35 }, 0, shot("warp-point"))
            drag(ov, whole(t.net[k + 1]), { x: Math.round(t.net[k + 1].x) + 10, y: Math.round(t.net[k + 1].y) - 40 }, 0, shot("warp-handle"))
            d.setView({ angle: 30 })
            go("@xf")
            by(t, 8, -50, -30, Qt.AltModifier, "xf-alt-view30")
            keyClick(Qt.Key_Escape)
            d.setView({ angle: 0 })
            Tools.setTool("crop", "crop")
            waitForRendering(w.contentItem)
            by(cb, 8, -90, -60, 0, "crop-corner")
            const co = whole(add(cb.pts[5], { x: 40, y: 0 }))
            drag(ov, co, whole(at(ov.view, rotd(doc(ov.view, co, ov.size), { x: cb.box.x + cb.box.w / 2, y: cb.box.y + cb.box.h / 2 }, 15), ov.size)), 0, shot("crop-turn"))
            keyClick(Qt.Key_Escape)
            Tools.setTool("pcrop", "crop")
            waitForRendering(w.contentItem)
            drag(ov, { x: 380, y: 180 }, { x: 660, y: 560 })
            by(cb, 2, -60, 30, 0, "pcrop-corner")
            keyClick(Qt.Key_Escape)
        }
    }
}
