import QtCore
import QtQuick
import QtTest
import PhotoBeauty
import "../qml/View.js" as View

// V2b: DocCanvas showing a document through the Presenter, here the CPU's (offscreen): a test file (a mock photo over
// see-through bands, saved as PNG, then as .pbd) against the CPU's reference composite (DocCanvas.composite()): sRGB on
// an sRGB screen to the bit, at fit, 50 %, 100 %, 800 %, turned; a .pbd's preview, then the tiles it lacks from a
// coarser level, then sharp; no frame while nothing changes. With PB_CANVAS=<file> (D1's .pbd, a 45 MP JPEG) also that
// file at 1920 x 1080 device pixels: its first frames, its pixels, pans (a view each frame of the window), Viewport
// showing it; with QT_QPA_PLATFORM=windows on the Iris Xe through the GPU.
Item {
    id: root
    width: 400
    height: 300
    Component {
        id: dcC
        DocCanvas {  // what came up: each frame's `shown`, its time and frameMs
            property var seen: []
            onFramed: seen.push(Object.assign({ t: Date.now(), ms: frameMs, gpu: gpu }, shown))
        }
    }
    Component { id: vpC; Viewport {} }
    FrameAnimation {  // test_file_pan's hand: the view `step` device pixels further each frame of the window (its vsync)
        id: hand
        property var canvas: null
        property var from: ({})
        property int step: 20  // 1,200 device pixels a second at 60 Hz, as V2a's bench
        property int leg: 0    // frames before it turns back; 0: on and on
        property int views: 0
        onTriggered: {
            views++
            const n = leg > 0 ? leg - Math.abs(views % (2 * leg) - leg) : views
            canvas.view = { x: from.x + n * step / from.zoom, y: from.y, zoom: from.zoom, angle: 0 }
        }
    }
    property int swaps: 0  // the window's frames
    Connections {
        target: root.Window.window
        function onFrameSwapped() { root.swaps++ }
    }
    Canvas {  // the test file: a mock photo over the top 1000 rows, a red band at half alpha, then nothing (a 2D
        // canvas: grabToImage() gives no alpha on the software renderer)
        id: art
        readonly property string photo: "qrc:/qt/qml/PhotoBeauty/mock/photos/halong.jpg"
        property bool painted: false
        x: root.width  // out of the window
        width: 1800 / Screen.devicePixelRatio  // it saves device pixels: 1800 x 1200 of them
        height: 1200 / Screen.devicePixelRatio
        Component.onCompleted: loadImage(photo)
        onImageLoaded: requestPaint()
        onPaint: {
            if (!isImageLoaded(photo))
                return
            const ctx = getContext("2d"), k = width / 1800
            ctx.clearRect(0, 0, width, height)
            ctx.drawImage(photo, 0, 0, 1800, 1000, 0, 0, width, 1000 * k)
            ctx.fillStyle = Qt.rgba(1, 0, 0, 0.5)
            ctx.fillRect(0, 1000 * k, width, 100 * k)
            painted = true
        }
    }

    TestCase {
        name: "V2b"
        when: windowShown
        readonly property string dir: local(StandardPaths.writableLocation(StandardPaths.TempLocation))
        readonly property string png: dir + "/pb-v2b.png"
        readonly property string pbd: dir + "/pb-v2b.pbd"
        readonly property var board: [40, 40, 40]  // past the canvas: DocCanvas's pasteboard

        function local(url) { return decodeURIComponent(String(url).replace(/^file:\/\/(\/(?=[A-Za-z]:))?/, "")) }
        function initTestCase() {
            tryVerify(() => art.painted, 20000, "the test file drawn")
            // Canvas.save() takes a path from this file's folder (an absolute one fails on Windows)
            const from = local(Qt.resolvedUrl(".")).split("/").filter(x => x), to = png.split("/").filter(x => x)
            let k = 0
            while (k < from.length && k < to.length && from[k].toLowerCase() === to[k].toLowerCase())
                k++
            verify(art.save("../".repeat(from.length - k) + to.slice(k).join("/")), "saved as " + png)
            art.visible = false
        }

        function open(source, props) {
            const c = createTemporaryObject(dcC, root, Object.assign({ width: root.width, height: root.height, source: source }, props || {}))
            tryVerify(() => c.docSize.width > 0 || c.error !== "", 60000, "open " + source)
            compare(c.error, "", "opened " + source)
            return c
        }
        function shows(c, v) {
            const s = c.shown
            return s.x === v.x && s.y === v.y && s.zoom === v.zoom && s.angle === (v.angle || 0) && !s.preview
        }
        function show(c, v) {  // view v, sharp: its frame up, no tiles on their way
            c.view = v
            tryVerify(() => shows(c, v) && !c.shown.pending, 60000, "a sharp frame of " + JSON.stringify(v))
        }
        function byte(v) { return Math.round(v * 255 / 65535) }
        // Logical pixels a device pixel of c's frame (View::pixels()): not the grab's, which the window crops when c
        // reaches past it (a 1920 x 1080 frame on a screen whose work area is 1055 high).
        function sizes(c, img) {
            const dpr = Screen.devicePixelRatio
            return { sx: c.width / Math.round(c.width * dpr), sy: c.height / Math.round(c.height * dpr) }
        }
        function under(c, s, i, j) { return c.toDoc(Qt.point((i + 0.5) * s.sx, (j + 0.5) * s.sy)) }  // device pixel (i, j)'s centre

        // c's frame against the CPU's reference composite at its view, every `step`-th device pixel: the pasteboard past
        // the canvas; nearest from 100 % up, a texel's 8 bits (either texel on an edge, as float may round it the other
        // way); below, bilinear at the view's level: within the range of the 2 x 2 texels around, ±1. Points on the
        // canvas's edge and see-through texels left out; at least `least` checked.
        function check(c, step, least) {
            const img = grabImage(c), s = sizes(c, img), v = c.view, d = c.docSize, eps = 1e-3
            const L = Math.max(0, Math.floor(-Math.log2(v.zoom))), k = Math.pow(2, L)
            const tw = Math.ceil(d.width / k) - 1, th = Math.ceil(d.height / k) - 1
            let checked = 0, wrong = ""
            for (let j = step >> 1; j < img.height && !wrong; j += step)
                for (let i = step >> 1; i < img.width && !wrong; i += step) {
                    const p = under(c, s, i, j), got = [img.red(i, j), img.green(i, j), img.blue(i, j)]
                    let lo = [255, 255, 255], hi = [0, 0, 0]
                    if (p.x < -eps || p.y < -eps || p.x > d.width + eps || p.y > d.height + eps) {
                        lo = hi = board
                    } else if (p.x < eps || p.y < eps || p.x > d.width - eps || p.y > d.height - eps) {
                        continue
                    } else if (L === 0 && v.zoom >= 1) {
                        let opaque = true
                        for (const q of [[p.x - eps, p.y - eps], [p.x + eps, p.y - eps], [p.x - eps, p.y + eps], [p.x + eps, p.y + eps]]) {
                            const t = c.composite(0, Math.floor(q[0]), Math.floor(q[1]))
                            opaque = opaque && t[3] === 65535
                            for (let n = 0; n < 3; n++) {
                                lo[n] = Math.min(lo[n], byte(t[n]))
                                hi[n] = Math.max(hi[n], byte(t[n]))
                            }
                        }
                        if (!opaque)
                            continue
                    } else {
                        const x0 = Math.floor(p.x / k - 0.5), y0 = Math.floor(p.y / k - 0.5)
                        let opaque = true
                        for (const q of [[x0, y0], [x0 + 1, y0], [x0, y0 + 1], [x0 + 1, y0 + 1]]) {
                            const t = c.composite(L, Math.min(Math.max(q[0], 0), tw), Math.min(Math.max(q[1], 0), th))
                            opaque = opaque && t[3] === 65535
                            for (let n = 0; n < 3; n++) {
                                lo[n] = Math.min(lo[n], byte(t[n]) - 1)
                                hi[n] = Math.max(hi[n], byte(t[n]) + 1)
                            }
                        }
                        if (!opaque)
                            continue
                    }
                    if ([0, 1, 2].some(n => got[n] < lo[n] || got[n] > hi[n]))
                        wrong = i + ", " + j + " (document " + p.x.toFixed(2) + ", " + p.y.toFixed(2) + "): " + got + " not in " + lo + " … " + hi
                    checked++
                }
            verify(!wrong, JSON.stringify(v) + " at " + wrong)
            verify(checked >= least, checked + " pixels checked of " + JSON.stringify(v))
        }

        // sRGB on an sRGB screen, 100 %: the composite's 8 bits to the bit, the checkerboard (8 device pixels, from the
        // document's top left on screen) where it is see-through, both mixed where it is half.
        function test_srgb() {
            const c = open(png)
            compare(c.docSize, Qt.size(1800, 1200))
            show(c, { x: 900, y: 1000, zoom: 1, angle: 0 })
            const img = grabImage(c), s = sizes(c, img), cell = 8 * Screen.devicePixelRatio
            const o = c.toCanvas(Qt.point(0, 0)), ox = o.x / s.sx - 0.5, oy = o.y / s.sy - 0.5
            const n = [0, 0, 0]  // opaque, see-through, half
            let wrong = ""
            for (let j = 0; j < img.height && !wrong; j += 2)
                for (let i = 0; i < img.width && !wrong; i += 2) {
                    const p = under(c, s, i, j), t = c.composite(0, Math.floor(p.x), Math.floor(p.y)), a = t[3] / 65535
                    const sq = (Math.floor((i - ox) / cell) + Math.floor((j - oy) / cell)) & 1 ? 204 : 255
                    const want = [0, 1, 2].map(m => (t[m] / 65535 * 255) * a + sq * (1 - a))
                    const got = [img.red(i, j), img.green(i, j), img.blue(i, j)], kind = a === 1 ? 0 : a === 0 ? 1 : 2
                    if ([0, 1, 2].some(m => kind < 2 ? got[m] !== Math.round(want[m]) : Math.abs(got[m] - want[m]) > 1))
                        wrong = i + ", " + j + ": " + got + " ≠ " + want.map(w => w.toFixed(2)) + " of " + t
                    n[kind]++
                }
            verify(!wrong, wrong)
            verify(n[0] > 5000 && n[1] > 1000 && n[2] > 5000, "pixels checked: " + n)
        }

        function test_views() {
            const c = open(png)
            const fit = View.fit(1800, 1200, { w: c.width, h: c.height, dpr: Screen.devicePixelRatio }, 0)
            for (const v of [{ x: 900, y: 600, zoom: fit, angle: 0 }, { x: 640.3, y: 455.7, zoom: 0.5, angle: 0 },
                             { x: 1700.25, y: 333.3, zoom: 1, angle: 0 }, { x: 1795.1, y: 20.6, zoom: 8, angle: 0 },
                             { x: 900, y: 600, zoom: 0.5, angle: 30 }, { x: 300.4, y: 800.2, zoom: 1, angle: -30 },
                             { x: 10.5, y: 990.5, zoom: 8, angle: 90 }, { x: 900, y: 600, zoom: 0.125, angle: 45 }]) {
                show(c, v)
                check(c, 3, 1000)
                grabImage(c).save(dir + "/pb-v2b-" + Math.round(v.zoom * 1000) + "-" + v.angle + ".png")  // to look at
            }
        }

        // A .pbd: its preview first, then the tiles of the view's level it lacks (it keeps mips from level 3 on) from level
        // 3 while a job makes them, then sharp, through on_ready(): a frame no view asked for.
        function test_coarser() {
            compare(open(png).save(pbd), "")
            const v = { x: 900, y: 600, zoom: 0.25, angle: 0 }  // level 2
            const c = open(pbd, { view: v })
            tryVerify(() => shows(c, v) && !c.shown.pending, 20000, "sharp")
            const seen = c.seen, said = JSON.stringify(seen)
            verify(seen[0].preview, "the preview first: " + said)
            const coarse = seen.findIndex(s => s.coarser > 0)
            verify(coarse > 0 && seen[coarse].pending && seen[coarse].missing === 0, "tiles from level 3: " + said)
            compare([c.shown.coarser, c.shown.missing], [0, 0], said)
            check(c, 3, 2000)
        }

        function test_still() {
            const v = { x: 900, y: 600, zoom: 0.1, angle: 10 }  // level 3: the mips made in the frame, none to come
            const c = open(png, { view: v })
            tryVerify(() => shows(c, v) && !c.shown.pending)
            verify(c.shown.built > 0, "its mips made in the frame: " + JSON.stringify(c.seen))
            wait(200)  // the last frame on screen
            const n = c.frames, s = root.swaps
            wait(500)
            compare([c.frames, root.swaps], [n, s], "still: no frame, of the canvas or the window")
            c.view = { x: 900, y: 600, zoom: 0.1, angle: 10, more: 1 }  // another map, the same view
            c.pasteboard = c.pasteboard
            wait(300)
            compare(c.frames, n, "the same")
            c.pasteboard = "#102030"
            tryCompare(c, "frames", n + 1)
            const img = grabImage(c)
            compare([img.red(1, 1), img.green(1, 1), img.blue(1, 1)], [16, 32, 48], "the pasteboard")
            wait(300)
            compare(c.frames, n + 1)
        }

        // Another source on the same canvas: the other document once it opens (a .pbd: its preview first); none: V1's
        // test card.
        function test_source() {
            const v = { x: 900, y: 600, zoom: 0.1, angle: 0 }
            compare(open(png).save(pbd), "")
            const c = open(png, { view: v })
            tryVerify(() => shows(c, v) && !c.shown.pending)
            let n = c.seen.length
            c.source = pbd
            tryVerify(() => c.seen.length > n && shows(c, v) && !c.shown.pending, 20000, "the .pbd up")
            verify(c.seen[n].preview, JSON.stringify(c.seen.slice(n)))
            n = c.seen.length
            c.source = ""
            tryVerify(() => c.seen.length > n, 5000, "the test card")
            compare([c.docSize.width, c.shown.tiles, c.shown.pending], [0, 0, false])
            c.source = png
            c.source = ""  // before it opened
            wait(1000)
            compare(c.docSize.width, 0, "an open a later source overtook comes to nothing")
        }

        function test_errors() {  // a file it cannot open: why, nothing up, no crash
            for (const f of [dir + "/pb-v2b-none.png", dir + "/pb-v2b-none.pbd", png.slice(0, -4) + ".pbd.png"]) {
                const c = createTemporaryObject(dcC, root, { source: f })
                tryVerify(() => c.error !== "", 10000, f)
                wait(100)
                compare([c.frames, c.docSize.width], [0, 0], f)
            }
        }

        // ---- PB_CANVAS=<file> ---------------------------------------------------------------------------------------
        function sharpAt(c, from, v) {  // when the first sharp frame of v after seen[from] came up
            const s = c.seen.slice(from).find(r => r.x === v.x && r.y === v.y && r.zoom === v.zoom && !r.pending && !r.preview)
            return s ? s.t : NaN
        }
        function median(a) { return a.length ? a.slice().sort((x, y) => x - y)[Math.floor(a.length / 2)] : NaN }
        function p95(a) { return a.length ? a.slice().sort((x, y) => x - y)[Math.min(a.length - 1, Math.floor(a.length * 0.95))] : NaN }
        function big() {  // 1920 x 1080 device pixels, PB_CANVAS's file fit
            root.width = Math.round(1920 / Screen.devicePixelRatio)
            root.height = Math.round(1080 / Screen.devicePixelRatio)
            const c = createTemporaryObject(dcC, root, { width: root.width, height: root.height })
            c.documentChanged.connect(() => c.view = fitted(c))
            const t0 = Date.now()
            c.source = CanvasEnv.document
            tryVerify(() => c.docSize.width > 0 || c.error !== "", 60000, "open " + CanvasEnv.document)
            compare(c.error, "")
            return { c: c, t0: t0, opened: Date.now() - t0 }
        }
        function fitted(c) {
            const d = c.docSize
            return { x: d.width / 2, y: d.height / 2, zoom: View.fit(d.width, d.height, { w: c.width, h: c.height, dpr: Screen.devicePixelRatio }, 0), angle: 0 }
        }
        function cleanup() {
            root.width = 400
            root.height = 300
        }

        // Its first frames (plan.md: a .pbd's within 1 s, a 45 MP JPEG's within 300 ms), its pixels at fit, 50 %, 100 %,
        // 800 %, turned.
        function test_file_open() {
            if (CanvasEnv.document === "")
                skip("PB_CANVAS=<file> runs this")
            const b = big(), c = b.c
            tryVerify(() => shows(c, c.view) && !c.shown.pending, 120000, "sharp")
            const seen = c.seen, at = s => s ? s.t - b.t0 : NaN
            const first = seen[0], doc = seen.find(s => !s.preview), sharp = seen[seen.length - 1]
            console.log("V2b " + CanvasEnv.document + " " + c.docSize.width + " x " + c.docSize.height + " through the " + (doc.gpu ? "GPU" : "CPU") +
                        ": opened in " + b.opened + " ms; first frame (" + (first.preview ? "the preview" : "the document") + ") at " + at(first) +
                        " ms, the document's at " + at(doc) + " ms (" + doc.ms.toFixed(1) + " ms, " + doc.tiles + " tiles, " + doc.coarser + " coarser, " +
                        doc.missing + " missing, " + doc.built + " built), sharp at " + at(sharp) + " ms; frames " + JSON.stringify(seen))
            if (CanvasEnv.document.toLowerCase().endsWith(".pbd"))
                verify(at(first) <= 1000, "a .pbd's first frame within 1 s: " + at(first) + " ms")
            check(c, 7, 10000)
            const x = c.docSize.width / 2 + 0.25, y = c.docSize.height / 2 + 0.25  // device pixels' centres inside texels
            for (const v of [{ x: x, y: y, zoom: 0.5, angle: 0 }, { x: x, y: y, zoom: 1, angle: 0 }, { x: x, y: y, zoom: 8, angle: 0 },
                             { x: x, y: y, zoom: 0.5, angle: 30 }, { x: x + 100.25, y: y - 50.5, zoom: 8, angle: -60 }]) {
                show(c, v)
                check(c, 7, 10000)
            }
            wait(200)
            const n = c.frames, s = root.swaps
            wait(1000)
            compare([c.frames, root.swaps], [n, s], "still for 1 s: no frame")
        }

        // Pans, the view 20 device pixels further each frame of the window for 2 s: at 50 % (first: a level not made yet, a
        // .pbd keeping mips from level 3 on) and 100 % on and on, at fit back and forth (600 device pixels). Frames a
        // second of the canvas and the window (plan.md: 60 at fit and 100 %), frame times, frames with tiles from a
        // coarser level (none missing: new ones show at once), the time until sharp once it stops (plan.md: within
        // 150 ms); at 50 % then a frame's height down, below what the pan showed, 5 jumps of 256 device pixels, each a
        // column of tiles never shown, and the time until sharp.
        function test_file_pan() {
            if (CanvasEnv.document === "")
                skip("PB_CANVAS=<file> runs this")
            const c = big().c, d = c.docSize, fit = fitted(c).zoom
            for (const z of [0.5, 1, fit]) {
                const v = { x: (z === fit ? d.width / 2 : d.width / 4) + 0.5, y: d.height / 2 + 0.5, zoom: z, angle: 0 }
                show(c, v)
                const k0 = c.seen.length, s0 = root.swaps, t0 = Date.now()
                hand.canvas = c
                hand.from = v
                hand.leg = z === fit ? 30 : 0
                hand.views = 0
                hand.running = true
                wait(2000)
                hand.running = false
                const t1 = Date.now(), f = c.seen.slice(k0), k1 = c.seen.length, s = (t1 - t0) / 1000, swaps = root.swaps - s0
                tryVerify(() => shows(c, c.view) && !c.shown.pending, 20000, "sharp")
                const sharp = sharpAt(c, k1, c.view) - t1, gaps = f.slice(1).map((r, i) => r.t - f[i].t), ms = f.map(r => r.ms)
                console.log("V2b pan at " + (z * 100).toFixed(1) + " % through the " + (c.gpu ? "GPU" : "CPU") + ": " + hand.views + " views, " +
                            f.length + " frames of the canvas, " + swaps + " of the window in " + (t1 - t0) + " ms: " + (f.length / s).toFixed(1) +
                            " a second (the window " + (swaps / s).toFixed(1) + "); frame median " + median(ms).toFixed(2) + " ms, p95 " +
                            p95(ms).toFixed(2) + " ms, max " + Math.max.apply(null, ms).toFixed(1) + " ms; between frames median " + median(gaps) +
                            " ms, p95 " + p95(gaps) + " ms; " + f.filter(r => r.coarser > 0).length + " frames with coarser tiles, missing at most " +
                            Math.max.apply(null, f.map(r => r.missing)) + "; sharp " + sharp + " ms after it stopped")
                compare(Math.max.apply(null, f.map(r => r.missing)), 0, "new tiles show at once")
                if (z !== 0.5)
                    continue
                const jumps = [], y = v.y + Math.round(c.height * Screen.devicePixelRatio) / z
                show(c, { x: v.x, y: y, zoom: z, angle: 0 })
                for (let j = 1; j <= 5; j++) {
                    const w = { x: v.x + j * 256 / z, y: y, zoom: z, angle: 0 }, t = Date.now(), n = c.seen.length
                    show(c, w)
                    jumps.push((sharpAt(c, n, w) - t) + " ms (" + c.seen.slice(n).map(r => r.coarser + "/" + r.tiles).join(", ") + ")")
                }
                console.log("V2b jumps of 256 device pixels at 50 %, until sharp (frames: coarser/tiles): " + jumps.join("; "))
            }
        }

        // Viewport with PB_CANVAS=<file>, opened for real as the command line opens it (Session, N1): DocCanvas in the mock
        // canvas's place on its document, the file fit to the frame.
        function test_file_viewport() {
            if (CanvasEnv.document === "")
                skip("PB_CANVAS=<file> runs this")
            Docs.reset()
            const vp = createTemporaryObject(vpC, root, { width: root.width, height: root.height })
            tryVerify(() => vp.sized && Docs.viewport.w === Math.round(vp.size.w * vp.dpr) && Docs.viewport.h === Math.round(vp.size.h * vp.dpr),
                      5000, "the frame a file opened fits")
            const d = Docs.openFile(CanvasEnv.document)
            verify(d && d.real, "opened for real: " + CanvasEnv.document)
            const find = item => {
                for (const ch of item.children) {
                    const f = ch instanceof DocCanvas ? ch : find(ch)
                    if (f)
                        return f
                }
                return null
            }
            const dc = find(vp)
            verify(dc && dc.visible && dc.model === d.model)
            tryVerify(() => dc.docSize.width > 0 && dc.frames > 0 && !dc.shown.pending && !dc.shown.preview, 60000)
            compare(vp.docSize, dc.docSize)
            compare(vp.view.zoom, View.fit(dc.docSize.width, dc.docSize.height, vp.size, 0))
            compare(dc.view, vp.view)
        }
    }
}
