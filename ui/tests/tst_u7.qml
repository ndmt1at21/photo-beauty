import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty
import "../qml/View.js" as View
import "../qml/Mock.js" as Mock
import "../qml/Fmt.js" as Fmt

// U7: the overlays on the canvas. Each in place at 12,5 %, 100 %, 800 %, turned or not (known document points against
// View.toCanvas, on the pixels drawn or the handles' items); the marching ants redraw only themselves; Overlays shows
// each as Tools, the document and Thiết lập have it.
Item {
    id: root
    width: 1100
    height: 800
    Component { id: mainC; Main {} }
    Component { id: frameC; Rectangle { width: 1034; height: 750; color: "#808080"; clip: true } }
    Component { id: gridC; GuidesGrid {} }
    Component { id: antsC; Ants {} }
    Component { id: cropC; CropBox {} }
    Component { id: xfC; TransformBox {} }
    Component { id: shapeC; ShapeHandles {} }
    Component { id: typeC; TypeBox {} }
    Component { id: gradC; GradientLine {} }
    Component { id: cloneC; CloneSource {} }
    Component { id: faceC; FaceOverlay {} }
    Component { id: cursorC; BrushCursor {} }
    Component { id: ringC; SampleRing {} }
    Component { id: barC; CanvasBar {} }
    Component { id: canvasC; MockCanvas {} }
    Component { id: overlaysC; Overlays {} }
    Component { id: spyC; SignalSpy {} }

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
        name: "U7"
        when: windowShown
        readonly property var size: ({ w: 1034, h: 750, dpr: 1 })

        function init() {
            Docs.viewport = { w: 1034, h: 750 }
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanupTestCase() { init() }

        // The views each placement is checked in: 12,5 %, 100 %, 800 %, each straight and turned 30°, centred off p so
        // that p is 150, 100 px from the frame's centre.
        function views(p) {
            const out = []
            for (const z of [0.125, 1, 8])
                for (const a of [0, 30])
                    out.push({ x: p.x + 150 / z, y: p.y + 100 / z, zoom: z, angle: a })
            return out
        }
        function at(v, p) { return View.toCanvas(v, size, p) }
        function frame(color) { return createTemporaryObject(frameC, root, color ? { color: color } : {}) }
        function put(c, f, props) { return createTemporaryObject(c, f, Object.assign({ width: f.width, height: f.height, size: size }, props)) }
        function centre(item, f) { return item.mapToItem(f, item.width / 2, item.height / 2) }
        function near(a, b, eps, msg) {
            verify(Math.abs(a.x - b.x) <= eps && Math.abs(a.y - b.y) <= eps, (msg || "") + " " + JSON.stringify(a) + " ≠ " + JSON.stringify(b))
        }
        // Some pixel within 1 px of p passes test (a 1 px line falls on either side of a point between pixels).
        function hit(img, p, test) {
            for (let dy = -1; dy <= 1; dy++)
                for (let dx = -1; dx <= 1; dx++) {
                    const c = img.pixel(Math.round(p.x) + dx, Math.round(p.y) + dy)
                    if (c && test(c))
                        return true
                }
            return false
        }
        function same(c, ink) { return Math.abs(c.r - ink.r) < 0.05 && Math.abs(c.g - ink.g) < 0.05 && Math.abs(c.b - ink.b) < 0.05 }
        function far(c, ink) { return Math.abs(c.r - ink.r) + Math.abs(c.g - ink.g) + Math.abs(c.b - ink.b) > 0.3 }
        function grey(c) { return same(c, Qt.color("#808080")) }

        // ---- each overlay in place -----------------------------------------------------------------------------------
        function test_guides_grid() {  // a guide through its document x, y; grid lines on multiples of the spacing; pixels from 600 %
            const f = frame()
            const g = put(gridC, f, { docW: 4400, docH: 5600, guides: true, guidesX: [1030], guidesY: [730], guideColor: "#00ffff", grid: true, spacing: 100,
                subdivisions: 1, gridColor: "#ff00ff" })
            const cyan = c => c.r < 0.3 && c.g > 0.6 && c.b > 0.6, magenta = c => c.r > 0.6 && c.g < 0.45 && c.b > 0.6
            for (const v of views({ x: 1000, y: 700 })) {
                g.view = v
                const img = grabImage(f), d = 30 / v.zoom, tag = JSON.stringify(v)
                verify(hit(img, at(v, { x: 1030, y: 700 + 2 * d }), cyan), "the vertical guide " + tag)
                verify(hit(img, at(v, { x: 1000 + 2 * d, y: 730 }), cyan), "the horizontal guide " + tag)
                verify(!hit(img, at(v, { x: 1030 + 2 * d, y: 730 + 2 * d }), cyan), "off the guides " + tag)
                verify(hit(img, at(v, { x: 1000, y: 700 + 2 * d }), magenta), "the grid line at x 1.000 " + tag)
                verify(!hit(img, at(v, { x: 1050, y: 750 }), magenta), "between grid lines " + tag)
            }
            g.grid = false
            g.guides = false
            g.pixelGrid = true
            f.color = "#000000"
            for (const v of views({ x: 1000, y: 700 })) {
                g.view = v
                const img = grabImage(f), line = c => c.r > 0.1, tag = JSON.stringify(v)
                compare(hit(img, at(v, { x: 1000, y: 700.5 }), line), v.zoom >= 6, "a pixel's edge from 600 % " + tag)
                verify(!hit(img, at(v, { x: 1000.5, y: 700.5 }), line) || v.zoom < 6, "a pixel's middle " + tag)
            }
        }
        function test_grid_thins() {  // lines closer than 50 px thin out 1-2-5 (U23); the subdivisions from 8 px apart, lighter
            const f = frame()
            const g = put(gridC, f, { docW: 4400, docH: 5600, grid: true, spacing: 100, subdivisions: 4, gridColor: "#ff00ff" })
            const major = c => c.r - c.g > 0.4, minor = c => c.r - c.g > 0.15 && c.r - c.g < 0.35, none = c => !grey(c)
            const line = (v, x, test) => hit(grabImage(f), at(v, { x: x, y: 712.5 }), test)  // y between the lines across
            // 12,5 %: 100 px are 12,5, so a line each 500 (62,5 px, the prototype's ov.grid), no subdivisions
            let v = { x: 1250, y: 700, zoom: 0.125, angle: 0 }
            g.view = v
            verify(line(v, 1000, major) && line(v, 1500, major), "each 500 px at 12,5 %")
            for (const x of [1100, 1200, 1300, 1400, 1250])
                verify(!line(v, x, none), "nothing at " + x + " at 12,5 %")
            // 45 %: 45 px, so each 2nd (90 px); the subdivisions 11,25 px apart, the line left out among them
            v = { x: 1100, y: 700, zoom: 0.45, angle: 0 }
            g.view = v
            verify(line(v, 1000, major) && line(v, 1200, major), "each 200 px at 45 %")
            verify(line(v, 1100, minor) && line(v, 1025, minor) && line(v, 1150, minor), "the subdivisions at 45 %")
            verify(!line(v, 1012.5, none), "between the subdivisions at 45 %")
            // 100 %: each 100 px, the subdivisions each 25
            v = { x: 1050, y: 700, zoom: 1, angle: 0 }
            g.view = v
            verify(line(v, 1000, major) && line(v, 1100, major), "each 100 px at 100 %")
            verify(line(v, 1025, minor) && line(v, 1050, minor) && line(v, 1075, minor), "the subdivisions at 100 %")
            verify(!line(v, 1012, none), "between the subdivisions at 100 %")
        }
        function test_ants_place() {  // the ants on the selection's edge, nothing inside; Quick Mask tints the rest
            const f = frame()
            const a = put(antsC, f, { docW: 4400, docH: 5600, shape: "rect" })  // [0,39; 0,19] + [0,375 × 0,46] of the page
            const left = 0.39 * 4400, mid = (0.19 + 0.23) * 5600
            for (const v of views({ x: left, y: mid })) {
                a.view = v
                const img = grabImage(f), d = 30 / v.zoom, tag = JSON.stringify(v)
                verify(hit(img, at(v, { x: left, y: mid }), c => far(c, Qt.color("#808080"))), "on the edge " + tag)
                verify(grey(img.pixel(Math.round(at(v, { x: left + d, y: mid }).x), Math.round(at(v, { x: left + d, y: mid }).y))), "inside " + tag)
            }
            a.mask = Qt.rgba(1, 0, 0, 0.5)
            a.ants = false
            for (const v of views({ x: left, y: mid })) {
                a.view = v
                const img = grabImage(f), d = 30 / v.zoom, out = at(v, { x: left - d, y: mid }), inn = at(v, { x: left + d, y: mid })
                const o = img.pixel(Math.round(out.x), Math.round(out.y))
                verify(o.r > o.g + 0.2, "Quick Mask outside " + JSON.stringify(v))
                verify(grey(img.pixel(Math.round(inn.x), Math.round(inn.y))), "clear inside " + JSON.stringify(v))
            }
            a.inverse = true  // the inside tinted now
            a.view = views({ x: left, y: mid })[2]
            const img = grabImage(f), inn = at(a.view, { x: left + 30, y: mid })
            verify(img.pixel(Math.round(inn.x), Math.round(inn.y)).r > 0.6, "inverse: the inside tinted")
        }
        function test_crop_place() {  // the shade outside, the edge on the box, the HUD under its bottom right
            const f = frame()
            const c = put(cropC, f, { docW: 4400, docH: 5600, box: { x: 400, y: 600, w: 2000, h: 2500 }, ratio: "4:5" })
            const hud = find(c, o => o instanceof Label)
            for (const v of views({ x: 400, y: 1850 })) {
                c.view = v
                const img = grabImage(f), d = 40 / v.zoom, tag = JSON.stringify(v)
                verify(hit(img, at(v, { x: 400, y: 1850 }), px => px.r > 0.85 && px.g > 0.85), "the left edge " + tag)
                const out = at(v, { x: 400 - d, y: 1850 }), o = img.pixel(Math.round(out.x), Math.round(out.y))
                verify(o.r < 0.3 && Math.abs(o.r - o.b) < 0.02, "shaded outside " + tag)
                const inn = at(v, { x: 400 + d, y: 1850 })
                verify(grey(img.pixel(Math.round(inn.x), Math.round(inn.y))), "clear inside " + tag)
                const br = at(v, { x: 2400, y: 3100 })
                near({ x: hud.x, y: hud.y - 8 }, br, 1, "HUD " + tag)
            }
            compare(hud.text, "4:5 · 2.000 × 2.500 px")
        }
        function test_transform_place() {  // handles and the reference point on the box as scaled and turned about it
            const f = frame()
            const t = put(xfC, f, { docW: 4400, docH: 5600, box: { x: 1000, y: 1000, w: 2000, h: 3000 } })
            const handles = () => all(t, o => o instanceof Rectangle && o.width === 10 && o.visible)
            const pivot = () => find(t, o => o instanceof Rectangle && o.width === 16)
            for (const box of [[1, 1, 0, 4], [0.5, 0.8, 20, 4], [1.2, 1, -45, 0]]) {
                t.scaleX = box[0]
                t.scaleY = box[1]
                t.angle = box[2]
                t.pivot = box[3]
                const p = { x: 1000 + box[3] % 3 / 2 * 2000, y: 1000 + Math.floor(box[3] / 3) / 2 * 3000 }
                // A point of the box (fx, fy across it) transformed as the bar has it.
                const tf = (fx, fy) => {
                    const a = box[2] * Math.PI / 180, dx = (1000 + fx * 2000 - p.x) * box[0], dy = (1000 + fy * 3000 - p.y) * box[1]
                    return { x: p.x + dx * Math.cos(a) - dy * Math.sin(a), y: p.y + dx * Math.sin(a) + dy * Math.cos(a) }
                }
                for (const v of views(tf(0, 0))) {
                    t.view = v
                    const hs = handles(), tag = JSON.stringify(v) + " " + box
                    compare(hs.length, 8)
                    const want = [[0, 0], [0.5, 0], [1, 0], [0, 0.5], [1, 0.5], [0, 1], [0.5, 1], [1, 1]].map(q => at(v, tf(q[0], q[1])))
                    hs.forEach((h, i) => near(centre(h, f), want[i], 1.5, "handle " + i + " " + tag))
                    near(centre(pivot(), f), at(v, p), 1.5, "reference point " + tag)
                }
            }
            t.warp = true
            t.view = views({ x: 1000, y: 1000 })[2]
            compare(handles().length, 4, "Warp: the corners")
            verify(!pivot().visible)
        }
        function test_shape_place() {  // size handles on the box, radius dots on the corners' arcs
            const f = frame()
            const s = put(shapeC, f, { box: { x: 500, y: 700, w: 1200, h: 800 }, radius: 144 })
            for (const v of views({ x: 500, y: 700 })) {
                s.view = v
                const hs = all(s, o => o instanceof Rectangle && o.width === 10), dots = all(s, o => o instanceof Rectangle && o.width === 9), tag = JSON.stringify(v)
                const want = [[0, 0], [0.5, 0], [1, 0], [0, 0.5], [1, 0.5], [0, 1], [0.5, 1], [1, 1]].map(q => at(v, { x: 500 + q[0] * 1200, y: 700 + q[1] * 800 }))
                hs.forEach((h, i) => near(centre(h, f), want[i], 1.5, "handle " + i + " " + tag))
                const r = Math.max(144, 6 / v.zoom)  // kept 6 px off the corner
                const wantDots = [[0, 0], [1, 0], [0, 1], [1, 1]].map(q => at(v, { x: q[0] ? 1700 - r : 500 + r, y: q[1] ? 1500 - r : 700 + r }))
                compare(dots.length, 4)
                dots.forEach((d, i) => near(centre(d, f), wantDots[i], 1.5, "radius dot " + i + " " + tag))
            }
        }
        function test_type_place() {  // the box's handles and the text's top left on the box
            const f = frame()
            const t = put(typeC, f, { box: { x: 600, y: 900, w: 1840, h: 400 }, text: "Chân dung", fontSize: 200 })
            for (const v of views({ x: 600, y: 900 })) {
                t.view = v
                const hs = all(t, o => o instanceof Rectangle && o.width === 8), tag = JSON.stringify(v)
                const want = [[0, 0], [0.5, 0], [1, 0], [0, 0.5], [1, 0.5], [0, 1], [0.5, 1], [1, 1]].map(q => at(v, { x: 600 + q[0] * 1840, y: 900 + q[1] * 400 }))
                hs.forEach((h, i) => near(centre(h, f), want[i], 1.5, "handle " + i + " " + tag))
                near(t.editor.mapToItem(f, 0, 0), at(v, { x: 600, y: 900 }), 1.5, "the text " + tag)
                compare(t.editor.font.pixelSize, Math.max(1, Math.round(200 * v.zoom)))
            }
        }
        function test_gradient_place() {  // stops along the line at their places, the picked one's HUD beside it
            const f = frame()
            const g = put(gradC, f, { from: { x: 500, y: 4000 }, to: { x: 3500, y: 1000 }, current: 1,
                stops: [{ pos: 0, color: "#2b1d4e" }, { pos: 0.48, color: "#c0587e" }, { pos: 1, color: "transparent" }] })
            const hud = find(g, o => o instanceof Label)
            for (const v of views({ x: 500 + 0.48 * 3000, y: 4000 - 0.48 * 3000 })) {
                g.view = v
                const stops = all(g, o => o instanceof Rectangle && o.border.width >= 2), tag = JSON.stringify(v)
                compare(stops.length, 3)
                stops.forEach((s, i) => {
                    const t = [0, 0.48, 1][i]
                    near(centre(s, f), at(v, { x: 500 + t * 3000, y: 4000 - t * 3000 }), 1, "stop " + i + " " + tag)
                })
                near({ x: hud.x - 20, y: hud.y - 5 }, at(v, { x: 500 + 0.48 * 3000, y: 4000 - 0.48 * 3000 }), 1, "HUD " + tag)
            }
            compare(hud.text, "48% · #C0587E")
        }
        function test_clone_place() {  // the crosshair on the source, then the pointer's offset away
            const f = frame()
            const c = put(cloneC, f, { docW: 4400, docH: 5600, source: { x: 3000, y: 1000 }, diameter: 120, photo: Mock.photoUrl(0), photoSize: Qt.size(1300, 1625) })
            const cross = () => all(c, o => o instanceof Rectangle && o.width === 17)[0]
            for (const v of views({ x: 3000, y: 1000 })) {
                c.view = v
                near(centre(cross(), f), at(v, { x: 3000, y: 1000 }), 1, "on the source " + JSON.stringify(v))
            }
            c.offset = { x: 500, y: -200 }
            for (const v of views({ x: 2000, y: 1500 })) {
                c.view = v
                c.point = at(v, { x: 2000, y: 1500 })
                near(centre(cross(), f), at(v, { x: 2500, y: 1300 }), 1, "the pointer's offset away " + JSON.stringify(v))
            }
        }
        function test_face_place() {  // the face's box and the skin where the prototype's portrait has them
            const f = frame()
            const o = put(faceC, f, { docW: 4400, docH: 5600, active: true, retouch: { chips: { skin: true }, overlay: true, feather: 4 } })
            const box = find(o, x => x instanceof Rectangle && Qt.colorEqual(x.border.color, Qt.rgba(1, 1, 1, 0.9)))
            const corner = { x: 214 / 550 * 4400, y: 132 / 700 * 5600 }, cheek = { x: 322 / 550 * 4400, y: 330 / 700 * 5600 }
            for (const v of views(corner)) {
                o.view = v
                const img = grabImage(f), tag = JSON.stringify(v)
                near(box.mapToItem(f, 0, 0), at(v, corner), 1.5, "the face's box " + tag)
                const out = at(v, { x: corner.x - 40 / v.zoom, y: corner.y + 40 / v.zoom })
                verify(grey(img.pixel(Math.round(out.x), Math.round(out.y))), "nothing outside the face " + tag)
            }
            for (const v of views(cheek)) {
                o.view = v
                const img = grabImage(f), sk = at(v, cheek), c = img.pixel(Math.round(sk.x), Math.round(sk.y))
                verify(c.r > c.g + 0.15, "skin tinted " + JSON.stringify(v))
            }
            compare(o.caption, "Xem trước Mask: Da · Feather 4 px")
        }
        function test_brush_place() {  // the tip around the pointer at the brush's size on the canvas; a crosshair in it
            const f = frame()
            const b = put(cursorC, f, { diameter: 300, hardness: 50 })
            for (const v of views({ x: 2000, y: 2000 })) {
                b.view = v
                b.diameter = v.zoom > 1 ? 40 : 300  // in the frame
                mouseMove(f, 400, 300)
                compare(b.point, { x: 400, y: 300 })
                const img = grabImage(f), r = b.diameter * v.zoom / 2, tag = JSON.stringify(v)
                verify(hit(img, { x: 400 + r, y: 300 }, c => c.r > 0.85), "the tip's edge " + tag)
                verify(hit(img, { x: 400, y: 300 }, c => c.r > 0.85), "the crosshair " + tag)
                verify(grey(img.pixel(400 + Math.round(r / 2), 300)), "inside the tip " + tag)
            }
        }

        // ---- the marching ants: 10 steps a second, nothing else redrawn ----------------------------------------------
        function test_ants_only_overlay_redraws() {
            const d = Docs.current, f = frame()
            const view = { x: 2200, y: 2800, zoom: 0.125, angle: 0 }
            const mc = put(canvasC, f, { doc: d, view: view })
            const ov = put(overlaysC, f, { doc: d, view: view })
            const ants = find(ov, o => o instanceof Ants), tick = ants.resources.find(o => o instanceof Timer)
            let frames = 0, steps = 0, canvas = 0
            const w = root.Window.window, counted = () => frames++, stepped = () => steps++, touched = () => canvas++
            w.frameSwapped.connect(counted)
            ants.phaseChanged.connect(stepped)
            for (const s of [mc.viewChanged, mc.sizeChanged, mc.docChanged, mc.sChanged, mc.centreChanged, mc.seenChanged, mc.showPhotoChanged, mc.transparentChanged])
                s.connect(touched)
            verify(!tick.running, "no selection: no timer")
            wait(400)  // the photo loaded, drawn
            frames = 0
            wait(400)
            compare(frames, 0, "no selection: nothing drawn")
            d.run({ op: "select", shape: "rect" })
            verify(tick.running)
            const rev = d.rev
            wait(150)
            frames = 0
            steps = 0
            wait(1000)
            verify(steps >= 8 && steps <= 12, "10 steps a second: " + steps)
            verify(frames >= steps && frames <= 2 * steps + 2, steps + " steps drew " + frames + " frames (a Shape's change takes two)")
            compare(canvas, 0, "the canvas untouched")
            compare(d.rev, rev)
            d.run({ op: "deselect" })
            verify(!tick.running, "no selection: the timer stops")
            wait(150)
            frames = 0
            wait(400)
            compare(frames, 0)
            w.frameSwapped.disconnect(counted)
        }

        // ---- Overlays: each as Tools, the document and Thiết lập have it -----------------------------------------------
        function open(state) {
            const w = createTemporaryObject(mainC, root, { startState: state || "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function parts(w) {
            const ov = find(w.contentItem, o => o instanceof Overlays), of = type => find(ov, o => o instanceof type)
            return { ov: ov, frame: ov.parent, input: ov.pointerArea, grid: of(GuidesGrid), face: of(FaceOverlay), ants: of(Ants), crop: of(CropBox), xf: of(TransformBox),
                shape: of(ShapeHandles), type: of(TypeBox), grad: of(GradientLine), clone: of(CloneSource), ring: of(SampleRing), cursor: of(BrushCursor), bar: of(CanvasBar) }
        }
        function label(item) { return find(item, o => o instanceof Label && o.visible) }

        function test_crop_transform() {  // the Crop tool's box from its ratio; Free Transform from its bar; Warp
            const w = open(), c = parts(w)
            verify(c.cursor.visible && !c.crop.visible && !c.xf.visible, "Brush at the start")
            Tools.setTool("crop", "crop")
            verify(c.crop.visible && !c.cursor.visible)
            compare(c.crop.box, { x: 0, y: 50, w: 4400, h: 5500 })
            compare(label(c.crop).text, "4:5 · 4.400 × 5.500 px")
            Tools.setValue("crop:2", "16:9")
            compare(c.crop.box, Tools.cropBox(4400, 5600))
            compare(label(c.crop).text, "16:9 · 4.400 × 2.475 px")
            Tools.xf = true
            verify(!c.crop.visible && c.xf.visible)
            compare(label(c.xf).text, "W 100,0% H 100,0%\n∠ 0,0°")
            compare(c.xf.box, { x: 0, y: 0, w: 4400, h: 5600 })
            Tools.setValues({ "xf:7": 50, "xf:9": 80, "xf:11": -6.5, "xf:2": 0 })
            compare([c.xf.scaleX, c.xf.scaleY, c.xf.angle, c.xf.pivot], [0.5, 0.8, -6.5, 0])
            compare(label(c.xf).text, "W 50,0% H 80,0%\n∠ −6,5°")
            Tools.warp = true
            verify(c.xf.warp)
            Tools.setTool("move", "move")
            verify(!c.xf.visible && !c.crop.visible, "a tool picked ends Free Transform")
        }
        function test_selection() {  // ants on the document's selection; Quick Mask; View › Extras, Viền vùng chọn
            const w = open(), c = parts(w), d = Docs.current
            const qm = find(c.ov, o => o instanceof Label && o.text === "Quick Mask · Q để thoát")
            compare(c.ants.shape, null)
            compare(c.ants.antsPath, "")
            d.run({ op: "select", shape: "ellip" })
            compare(c.ants.shape, "ellip")
            verify(c.ants.antsPath !== "")
            verify(!qm.visible)
            Docs.exec("@qm")
            verify(c.ants.mask.a > 0.4 && c.ants.mask.r > 0.9 && c.ants.mask.g < 0.1)
            compare(c.ants.antsPath, "", "Quick Mask: no ants")
            verify(qm.visible)
            Docs.exec("@qm")
            verify(!qm.visible && c.ants.antsPath !== "")
            Docs.exec("@tog:selEdges")
            compare(c.ants.antsPath, "")
            Docs.exec("@tog:selEdges")
            Docs.exec("@tog:extras")
            compare(c.ants.antsPath, "")
            Docs.exec("@tog:extras")
            d.run({ op: "inverse" })
            verify(c.ants.inverse)
            verify(c.ants.antsPath.startsWith(c.ants.border), "inverse: the document's edge too")
        }
        function test_grid_guides() {  // View's switches, Thiết lập › Guide và lưới
            const w = open(), c = parts(w), d = Docs.current
            verify(!c.grid.grid && !c.grid.guides)
            Docs.exec("@tog:grid")
            Docs.exec("@newGuide")
            verify(c.grid.grid && c.grid.guides)
            compare([c.grid.spacing, c.grid.subdivisions, c.grid.guidesX, c.grid.guidesY], [100, 4, [1467], [2240]])
            Prefs.set("gridSpacing", 0)
            Prefs.set("gridSubdivisions", 1)
            Prefs.set("gridColor", "#ff0000")
            Prefs.set("gridStyle", 2)
            Prefs.set("guideColor", "#00ff00")
            Prefs.set("guideStyle", 1)
            compare([c.grid.spacing, c.grid.subdivisions, c.grid.gridStyle, c.grid.guideStyle], [25, 2, 2, 1])
            verify(Qt.colorEqual(c.grid.gridColor, "#ff0000") && Qt.colorEqual(c.grid.guideColor, "#00ff00"))
            Docs.exec("@tog:extras")
            verify(!c.grid.grid && !c.grid.guides && !c.grid.pixelGrid)
            Docs.exec("@tog:extras")
            verify(c.grid.pixelGrid && !c.grid.pixels, "the pixel grid waits for 600 %")
            d.zoomTo(600)
            wait(0)
            verify(c.grid.pixels && c.grid.pixelPath !== "")
            Prefs.set("pixelGrid", false)
            verify(!c.grid.pixels)
            Prefs.set("pixelGrid", true)
            Docs.exec("@tog:pixGrid")
            verify(!c.grid.pixels)
        }
        function test_retouch() {  // the face and the chips' masks while the Retouch panel shows a portrait
            const w = open("Retouch"), c = parts(w)
            verify(c.face.active)
            compare(c.face.parts, { skin: true, eyes: false, lips: false, teeth: false })
            compare(c.face.caption, "Xem trước Mask: Da · Feather 4 px")
            Tools.setRetouch({ chips: { skin: true, eyes: true, lips: false, teeth: true }, feather: 12 })
            compare(c.face.parts, { skin: true, eyes: true, lips: false, teeth: true })
            compare(c.face.caption, "Xem trước Mask: Da, Mắt, Răng · Feather 12 px")
            Tools.setRetouch({ overlay: false })
            compare(c.face.parts, { skin: false, eyes: false, lips: false, teeth: false })
            compare(c.face.caption, "")
            verify(c.face.active, "the face's box stays")
            Tools.setRetouch({ overlay: true })
            Tools.showPanel("Properties")
            verify(!c.face.active)
            compare(c.face.caption, "")
            Tools.showPanel("Retouch")
            verify(c.face.active)
            Docs.open("san-pham.webp")
            verify(!c.face.active, "no face")
        }
        function test_brush_cursor() {  // the tool's brush; the rulers keep the pointer; Alt right drag; Caps Lock; Thiết lập
            const w = open(), c = parts(w)
            mouseMove(c.frame, 400, 300)
            compare(c.cursor.point, { x: 400, y: 300 })
            verify(c.input.containsMouse, "the canvas keeps its hover: the rulers mark the pointer")
            compare([c.cursor.diameter, c.cursor.hardness, c.cursor.tip, c.cursor.crosshair], [400, 0, 3, true])
            Tools.setValues({ "brush:size": 120, "brush:hard": 80 })
            compare([c.cursor.diameter, c.cursor.hardness], [120, 80])
            Tools.setTool("burn", "dodge")
            compare([c.cursor.diameter, c.cursor.hardness], [300, 0], "Burn: Dodge's bar")
            Tools.setValue("dodge:tip", 3)
            compare([c.cursor.roundness, c.cursor.tipAngle, c.cursor.square], [0.45, -30, false])
            Tools.setValues({ "dodge:angle": 60, "dodge:roundness": 80 })  // Brush Settings' (the bar's angle too, U23), anticlockwise
            compare([c.cursor.roundness, c.cursor.tipAngle], [0.8, -60])
            Tools.setValue("dodge:flipX", true)  // mirrored, as Brush Settings' stroke
            compare(c.cursor.tipAngle, 60)
            Tools.setValue("dodge:flipY", true)
            compare(c.cursor.tipAngle, -60)
            Tools.setValue("dodge:tip", 4)
            verify(c.cursor.square)
            Tools.setTool("brush", "brush")
            const s = View.scale(c.ov.view, c.ov.size)
            mousePress(c.frame, 400, 300, Qt.RightButton, Qt.AltModifier)
            mouseMove(c.frame, 450, 280, -1, Qt.RightButton, Qt.AltModifier)
            verify(c.cursor.adjusting)
            compare([Tools.value("brush:size"), Tools.value("brush:hard")], [Math.round(120 + 100 / s), 60])
            const hud = label(c.cursor)
            compare(hud.text, "Cỡ " + Fmt.vn(Math.round(120 + 100 / s)) + " px · Độ cứng 60%")
            mouseMove(c.frame, 300, 500, -1, Qt.RightButton, Qt.AltModifier)
            compare([Tools.value("brush:size"), Tools.value("brush:hard")], [Math.max(1, Math.round(120 - 200 / s)), 100], "kept in 1 px…, 0–100 %")
            mouseRelease(c.frame, 300, 500, Qt.RightButton, Qt.AltModifier)
            verify(!c.cursor.adjusting && !label(c.cursor))
            // Alt held turns Brush into the Eyedropper for a moment (U5's Toolbar): its own pointer, and Alt with the right
            // button still resizes the brush.
            keyPress(Qt.Key_Alt)
            tryCompare(Tools, "tool", "eyedrop")
            verify(c.cursor.visible && !c.cursor.drawn)
            const size0 = Tools.value("brush:size")
            mousePress(c.frame, 400, 300, Qt.RightButton, Qt.AltModifier)
            mouseMove(c.frame, 420, 300, -1, Qt.RightButton, Qt.AltModifier)
            verify(c.cursor.adjusting)
            compare(Tools.value("brush:size"), Math.round(size0 + 40 / s))
            mouseRelease(c.frame, 420, 300, Qt.RightButton, Qt.AltModifier)
            keyRelease(Qt.Key_Alt)
            compare(Tools.tool, "brush")
            verify(c.cursor.drawn)
            const menu = c.frame.parent.resources.find(o => o instanceof PopupMenu)  // Viewport's
            mouseClick(c.frame, 400, 300, Qt.RightButton)
            tryVerify(() => menu.opened, 1000, "the right button without Alt: the canvas's menu")
            keyClick(Qt.Key_Escape)
            tryVerify(() => !menu.visible)
            mouseMove(c.frame, 410, 300)
            keyClick(Qt.Key_CapsLock)
            verify(c.cursor.precise && c.cursor.cross, "Caps Lock: the crosshair alone")
            keyClick(Qt.Key_CapsLock)
            verify(!c.cursor.precise)
            Prefs.set("paintCursor", 0)
            verify(!c.cursor.drawn, "Standard: the system's pointer")
            Prefs.set("paintCursor", 2)
            compare(c.cursor.d, 2 * Math.max(2, Math.round(Tools.value("brush:size") * s * (1 + Tools.value("brush:hard") / 100) / 4)), "Normal Brush Tip")
        }
        function test_eyedropper() {  // held down: the ring, the colour under it new above, the one before below; let go: picked
            const w = open(), c = parts(w)
            Tools.setTool("eyedrop", "eye")
            Tools.setFg("#123456")
            verify(!c.ring.visible)
            mousePress(c.frame, 300, 200)
            verify(c.ring.visible)
            compare(c.ring.point, { x: 300, y: 200 })
            verify(Qt.colorEqual(c.ring.oldColor, "#123456") && Qt.colorEqual(c.ring.newColor, "#d9a68e"))
            mouseRelease(c.frame, 300, 200)
            verify(!c.ring.visible)
            compare(Tools.fg, "#d9a68e")
            Tools.setValue("eyedrop:6", false)
            mousePress(c.frame, 300, 200)
            verify(!c.ring.visible, "Show Sampling Ring off")
            mouseRelease(c.frame, 300, 200)
        }
        function test_clone_source() {  // Alt click sets it; the first stroke fixes the offset; then it goes along
            const w = open(), c = parts(w)
            Tools.setTool("clone", "stamp")
            verify(c.clone.visible)
            compare(c.clone.cross, null)
            compare(label(c.clone).text, "Alt nhấp để đặt nguồn · Aligned")
            mouseClick(c.frame, 300, 200, Qt.LeftButton, Qt.AltModifier)
            near(c.ov.cloneSource, View.toDoc(c.ov.view, c.ov.size, { x: 300, y: 200 }), 1e-6)
            near(c.clone.cross, { x: 300, y: 200 }, 1e-6, "on the source")
            mouseClick(c.frame, 500, 400)
            mouseMove(c.frame, 600, 450)
            near(c.clone.cross, { x: 400, y: 250 }, 1e-6, "the pointer's offset away")
            verify(c.clone.previewing)
            Tools.setValue("clone:13", false)
            compare(label(c.clone).text, "Alt nhấp để đặt nguồn")
            Tools.setTool("heal", "heal")
            verify(c.clone.visible)
            Tools.setTool("spot", "heal")
            verify(!c.clone.visible, "Spot Healing takes no source")
        }
        function test_shape_text_gradient() {  // a live shape's, a text's, a gradient fill's handles with their tools
            const w = open(), c = parts(w), d = Docs.current
            d.run({ op: "newShapeLayer", shape: "rectS" })
            verify(!c.shape.visible)
            Tools.setTool("rectS", "shape")
            verify(c.shape.visible)
            compare([c.shape.box, c.shape.radius, c.shape.ellipse], [{ x: 1600, y: 2400, w: 1200, h: 800 }, 144, false])
            compare(label(c.shape).text, "W 1.200 px H 800 px Bo góc 144 px")
            d.zoomTo(100)
            wait(0)
            waitForRendering(w.contentItem)
            const dot = all(c.shape, o => o instanceof Rectangle && o.width === 9)[0]
            mousePress(dot, 4, 4)
            mouseMove(dot, 60, 60, -1, Qt.LeftButton)
            mouseRelease(dot, 60, 60)
            verify(Tools.value("rectS:17") > 144, "the radius dragged: " + Tools.value("rectS:17"))
            d.run({ op: "newShapeLayer", shape: "ellipS" })
            compare([c.shape.radius, c.shape.ellipse], [-1, true])
            d.zoomTo(12.5)
            wait(0)
            waitForRendering(w.contentItem)
            d.run({ op: "newTypeLayer" })
            verify(!c.shape.visible && !c.type.visible)
            Tools.setTool("type", "type")
            verify(c.type.visible)
            compare([c.type.text, c.type.fontSize], ["Chữ mẫu", 200])
            Tools.setValues({ "char:size": 24, "char:color": "#ff0000" })  // Character's, which the Type bar shows (U23)
            compare(c.type.fontSize, 100)
            verify(Qt.colorEqual(c.type.color, "#ff0000"))
            Tools.setValues({ "char:size": null, "char:color": null })
            mouseClick(c.type.editor, 5, 5)
            verify(c.type.editor.activeFocus)
            keyClick(Qt.Key_End)
            keyClick(Qt.Key_A)
            compare(c.type.editor.text, "Chữ mẫua")
            keyClick(Qt.Key_Escape)
            verify(!c.type.editor.activeFocus, "Esc: done typing")
            d.run({ op: "newFillLayer", fill: "Gradient", color: "linear-gradient(135deg, #2b1d4e, #c0587e, #f7c873)" })
            verify(!c.grad.visible)
            Tools.setTool("gradient", "grad")
            verify(c.grad.visible)
            compare(c.grad.stops, [{ pos: 0, color: "#2b1d4e" }, { pos: 0.5, color: "#c0587e" }, { pos: 1, color: "#f7c873" }])
            waitForRendering(w.contentItem)
            const stops = () => all(c.grad, o => o instanceof Rectangle && o.border.width >= 2)
            const mid = stops()[1], a = c.grad.a, b = c.grad.b
            mousePress(mid, mid.width / 2, mid.height / 2)
            const q = c.grad.along(0.75), p = mid.mapFromItem(c.grad, q.x, q.y)
            mouseMove(mid, p.x, p.y, -1, Qt.LeftButton)
            mouseRelease(mid, p.x, p.y)
            compare(c.grad.current, 1)
            verify(Math.abs(c.grad.stops[1].pos - 0.75) < 0.01, "dragged along: " + c.grad.stops[1].pos)
            const m2 = stops()[1]
            mousePress(m2, m2.width / 2, m2.height / 2)
            mouseMove(m2, m2.width / 2 + 120, m2.height / 2 + 120, -1, Qt.LeftButton)
            mouseRelease(m2, m2.width / 2 + 120, m2.height / 2 + 120)
            compare(c.grad.stops.length, 2, "dragged away: gone")
            Tools.setTool("move", "move")
            verify(!c.grad.visible)
            Tools.setTool("gradient", "grad")
            compare(c.grad.stops.length, 2, "kept while the layer is")
            d.run({ op: "newFillLayer", fill: "Gradient", color: "linear-gradient(90deg, transparent, #ffffff, transparent)" })
            compare(c.grad.stops.map(x => x.color), ["transparent", "#ffffff", "transparent"])
        }
        function test_canvas_bar() {  // the faint selection's notice; Bỏ qua; the job's progress and its cancel
            const w = open(), c = parts(w), d = Docs.current
            c.ants.faint = true
            compare(c.bar.notice, "")
            d.run({ op: "select", shape: "eyes" })
            compare(c.bar.notice, "Không có điểm ảnh nào được chọn quá 50%, nên viền vùng chọn không hiện.")
            compare(c.ants.antsPath, "")
            waitForRendering(w.contentItem)
            const skip = find(c.bar, o => o instanceof AbstractButton && o.text === "Bỏ qua")
            verify(skip.visible)
            mouseClick(skip)
            compare(c.bar.notice, "")
            d.run({ op: "select", shape: "lips" })
            verify(c.bar.notice !== "", "another selection: said again")
            const f = frame()
            const bar = createTemporaryObject(barC, f, { width: f.width, height: f.height, task: "Gaussian Blur · cả ảnh", progress: 0.42, notice: "PSD có 2 phần chưa hiện như bản gốc",
                details: true })
            const spy = createTemporaryObject(spyC, root, { target: bar, signalName: "canceled" })
            const more = createTemporaryObject(spyC, root, { target: bar, signalName: "detailsClicked" })
            waitForRendering(f)
            verify(!!find(bar, o => o instanceof Text && o.text === "42%"))
            const line = find(bar, o => o instanceof Rectangle && o.height === 2)
            compare(line.width, Math.round(1034 * 0.42))
            mouseClick(find(bar, o => o instanceof AbstractButton && o.Accessible.name === "Hủy"))
            compare(spy.count, 1)
            mouseClick(find(bar, o => o instanceof AbstractButton && o.text === "Xem chi tiết"))
            compare(more.count, 1)
        }
    }
}