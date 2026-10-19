import QtCore
import QtQuick
import QtTest
import PhotoBeauty
import "../qml/View.js" as View

// C3b: strokes on DocCanvas, here through the CPU (offscreen), on tst_v2b's test file (a mock photo over a band at
// half alpha, then nothing): the mouse's, and the pen's as Qt hands a platform's tablet events over (its pressure on
// the size); every event in a frame, none dropped or merged, even all at once; the frame after pen up the CPU's
// composite of the document after the stroke, to the bit at 100 %; Eraser; undo, redo; a view changed mid-stroke; no
// frame while still. With PB_CANVAS=<file> (D1) and PB_STROKES=<folder of pb-bench make-strokes' logs> also the D1
// strokes played at their pace through the pen's way in at 1920 x 1080 device pixels (QT_QPA_PLATFORM=windows on the
// Iris Xe: through the GPU, the latencies), and pb-ui's Viewport in Main: Brush, Eraser, Ctrl Z, Ctrl Shift Z, Hand.
Item {
    id: root
    width: 400
    height: 300
    Component {
        id: dcC
        DocCanvas {
            property var steps: []  // painted()'s
            onPainted: step => steps.push(step)
        }
    }
    Component { id: mainC; Main {} }
    Canvas {  // the test file, as tst_v2b's: a mock photo over the top 1000 rows, a red band at half alpha, then nothing
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
        name: "C3b"
        when: windowShown
        readonly property string dir: local(StandardPaths.writableLocation(StandardPaths.TempLocation))
        readonly property string png: dir + "/pb-c3b.png"
        readonly property var board: [40, 40, 40]  // past the canvas: DocCanvas's pasteboard
        readonly property var v: ({ x: 900, y: 1000, zoom: 1, angle: 0 })  // 100 %: rows 850 to 1150, the photo, the band, nothing
        readonly property var blue: [0, 0, 65535, 65535]

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

        function shows(c, w) {
            const s = c.shown
            return s.x === w.x && s.y === w.y && s.zoom === w.zoom && s.angle === (w.angle || 0) && !s.preview
        }
        function show(c, w) {  // view w, sharp
            c.view = w
            tryVerify(() => shows(c, w) && !c.shown.pending, 60000, "a sharp frame of " + JSON.stringify(w))
        }
        // The test file at 100 %, Brush: 60 px, hard, blue (the pen's pressure on its size, BrushParams').
        function open(props) {
            const c = createTemporaryObject(dcC, root, Object.assign({ width: root.width, height: root.height, source: png, view: v,
                                                                       tool: "brush", brush: { size: 60, hardness: 1, color: "#0000ff" } }, props || {}))
            tryVerify(() => c.docSize.width > 0 || c.error !== "", 20000, "open")
            compare(c.error, "")
            tryVerify(() => shows(c, c.view) && !c.shown.pending, 20000, "sharp")
            return c
        }
        function line(y, x0, x1) {  // document points every 5 px
            const p = []
            for (let x = x0 || 750; x <= (x1 || 1050); x += 5)
                p.push([x, y])
            return p
        }
        // A stroke through document points, as they are on the canvas now: the mouse's, or the pen's at `pressure`;
        // then until its step is made and a frame with it is up. The pen's: whether Qt made the mouse events (pen()).
        function stroke(c, pts, pressure) {
            let mice = false
            pts.forEach((p, i) => {
                const q = c.toCanvas(Qt.point(p[0], p[1]))
                if (pressure === undefined) {
                    if (i === 0)
                        mousePress(c, q.x, q.y)
                    else
                        mouseMove(c, q.x, q.y)
                } else {
                    mice = c.pen(i === 0 ? "press" : "move", q, pressure)
                }
            })
            const end = pts[pts.length - 1], q = c.toCanvas(Qt.point(end[0], end[1]))
            if (pressure === undefined)
                mouseRelease(c, q.x, q.y)
            else
                c.pen("release", q, 0)
            c.wait()
            return mice
        }
        function counts(c, n) {  // every event in, applied, in a frame (and play()'s, handed over)
            const s = c.stats()
            compare([s.added, s.applied, s.shown, s.sent === undefined ? n : s.sent], [n, n, n, n], "events in, applied, shown, handed over")
            return s
        }

        // Logical pixels a device pixel of c's frame (View::pixels()).
        function sizes(c) {
            const dpr = Screen.devicePixelRatio
            return { sx: c.width / Math.round(c.width * dpr), sy: c.height / Math.round(c.height * dpr) }
        }
        function under(c, s, i, j) { return c.toDoc(Qt.point((i + 0.5) * s.sx, (j + 0.5) * s.sy)) }  // device pixel (i, j)'s centre
        function byte(v) { return Math.round(v * 255 / 65535) }
        // c's frame at 100 % against the CPU's composite of its document (tst_v2b's test_srgb), every `step`-th device
        // pixel: its 8 bits to the bit, over the checkerboard (8 device pixels from the document's top left on screen)
        // where it is see-through, ±1 where half; the pasteboard past the canvas. Opaque, see-through, half checked.
        function exact(c, step) {
            const img = grabImage(c), s = sizes(c), cell = 8 * Screen.devicePixelRatio, d = c.docSize
            const o = c.toCanvas(Qt.point(0, 0)), ox = o.x / s.sx - 0.5, oy = o.y / s.sy - 0.5
            const n = [0, 0, 0]
            let wrong = ""
            for (let j = step >> 1; j < img.height && !wrong; j += step)
                for (let i = step >> 1; i < img.width && !wrong; i += step) {
                    const p = under(c, s, i, j), got = [img.red(i, j), img.green(i, j), img.blue(i, j)]
                    if (p.x < 0 || p.y < 0 || p.x >= d.width || p.y >= d.height) {
                        if (got.some((g, m) => g !== board[m]))
                            wrong = i + ", " + j + ": " + got + " past the canvas"
                        continue
                    }
                    const t = c.composite(0, Math.floor(p.x), Math.floor(p.y)), a = t[3] / 65535
                    const sq = (Math.floor((i - ox) / cell) + Math.floor((j - oy) / cell)) & 1 ? 204 : 255
                    const want = [0, 1, 2].map(m => (t[m] / 65535 * 255) * a + sq * (1 - a))
                    const kind = a === 1 ? 0 : a === 0 ? 1 : 2
                    if ([0, 1, 2].some(m => kind < 2 ? got[m] !== Math.round(want[m]) : Math.abs(got[m] - want[m]) > 1))
                        wrong = i + ", " + j + " (document " + p.x.toFixed(2) + ", " + p.y.toFixed(2) + "): " + got + " ≠ " +
                                want.map(w => w.toFixed(2)) + " of " + t
                    n[kind]++
                }
            verify(!wrong, JSON.stringify(c.view) + " at " + wrong)
            return n
        }
        function pixel(c, x, y) {  // the frame's [R, G, B] at a document point
            const img = grabImage(c), s = sizes(c), q = c.toCanvas(Qt.point(x, y))
            const i = Math.floor(q.x / s.sx), j = Math.floor(q.y / s.sy)
            return [img.red(i, j), img.green(i, j), img.blue(i, j)]
        }

        // The mouse's stroke: its every event, its step, its pixels in the document and the frame.
        function test_mouse() {
            const c = open(), pts = line(950)
            const was = c.composite(0, 900, 990), frames = c.frames
            pts.forEach((p, i) => {
                const q = c.toCanvas(Qt.point(p[0], p[1]))
                if (i === 0)
                    mousePress(c, q.x, q.y)
                else
                    mouseMove(c, q.x, q.y)
            })
            mouseRelease(c, c.toCanvas(Qt.point(1050, 950)).x, c.toCanvas(Qt.point(1050, 950)).y)
            tryVerify(() => c.steps.length === 1, 5000, "pen up makes the step by itself")
            c.wait()
            compare(c.steps, ["Brush"])
            const s = counts(c, pts.length)
            verify(s.frames > 0 && c.frames > frames && s.latency.length === pts.length, JSON.stringify(s))
            compare(c.composite(0, 900, 950), blue, "blue where it went")
            compare(c.composite(0, 760, 975), blue, "its whole size, 60 px")
            compare(c.composite(0, 900, 990), was, "nothing past it")
            verify(shows(c, v))
            compare(pixel(c, 900, 950), [0, 0, 255])
            const n = exact(c, 2)
            verify(n[0] > 5000 && n[1] > 500 && n[2] > 2000, "pixels checked: " + n)
        }

        // The pen's: a quarter of the size at a quarter of the pressure, the whole at full; Qt's way in for tablet events.
        function test_pen() {
            const c = open(), pts = line(900), was = c.composite(0, 900, 912)
            const mice = stroke(c, pts, 0.25)
            console.log("C3b: Qt " + (mice ? "makes" : "does not make") + " mouse events of the tablet events no handler takes, here (" +
                        Qt.platform.pluginName + ")")
            compare(c.steps, ["Brush"])
            counts(c, pts.length)
            compare(c.composite(0, 900, 900), blue, "its middle")
            compare(c.composite(0, 900, 912), was, "a quarter of 60 px at a quarter of the pressure")
            exact(c, 2)
            verify(c.undo())
            stroke(c, pts, 1)
            compare(c.steps, ["Brush", "Brush"])
            counts(c, pts.length)
            compare(c.composite(0, 900, 912), blue, "the whole 60 px at full pressure")
            exact(c, 2)
        }

        function test_eraser() {
            const c = open({ tool: "eraser" }), pts = line(1050), was = c.composite(0, 900, 1050)
            verify(was[3] > 30000 && was[3] < 35000, "the band, at half alpha: " + was)
            stroke(c, pts)
            compare(c.steps, ["Eraser"])
            counts(c, pts.length)
            compare(c.composite(0, 900, 1050)[3], 0, "erased")
            const n = exact(c, 2)
            verify(n[1] > 1500, "see-through: " + n)
            verify(c.undo())
            c.wait()
            compare(c.composite(0, 900, 1050), was, "undone")
            exact(c, 2)
            verify(c.redo())
            c.wait()
            compare(c.composite(0, 900, 1050)[3], 0, "again")
            exact(c, 2)
        }

        // Undo gives the pixels back, redo the stroke again, frames and all; past the ends, nothing.
        function test_undo_redo() {
            const c = open(), pts = line(950), was = [c.composite(0, 900, 950), c.composite(0, 760, 975)]
            verify(!c.undo(), "nothing to undo yet")
            stroke(c, pts)
            verify(!c.redo())
            let n = c.frames
            verify(c.undo())
            tryVerify(() => c.frames > n, 5000, "a frame of the state undone, by itself")
            c.wait()
            compare([c.composite(0, 900, 950), c.composite(0, 760, 975)], was, "the pixels before")
            compare(pixel(c, 900, 950), was[0].slice(0, 3).map(byte))
            exact(c, 2)
            verify(!c.undo(), "the first step")
            n = c.frames
            verify(c.redo())
            tryVerify(() => c.frames > n, 5000, "a frame of the state redone, by itself")
            c.wait()
            compare([c.composite(0, 900, 950), c.composite(0, 760, 975)], [blue, blue], "the stroke again")
            compare(pixel(c, 900, 950), [0, 0, 255])
            exact(c, 2)
            // undo in the middle of a stroke: it ends, becomes a step, and goes; redo brings it back
            const row = c.composite(0, 820, 880), a = c.toCanvas(Qt.point(800, 880)), b = c.toCanvas(Qt.point(850, 880))
            mousePress(c, a.x, a.y)
            mouseMove(c, b.x, b.y)
            verify(c.undo())
            mouseRelease(c, b.x, b.y)
            c.wait()
            compare(c.steps, ["Brush", "Brush"])
            compare([c.composite(0, 900, 950), c.composite(0, 820, 880)], [blue, row], "back to the first stroke")
            exact(c, 2)
            verify(c.redo(), "the stroke undone comes back")
            c.wait()
            compare([c.composite(0, 900, 950), c.composite(0, 820, 880)], [blue, blue], "both strokes")
            exact(c, 2)
        }

        // The view moved mid-stroke: its frames follow (the view each one shows is its own), the stroke goes on where
        // the pointer is in the document.
        function test_view() {
            const c = open(), w = { x: 950, y: 1000, zoom: 1, angle: 0 }, past = c.composite(0, 1150, 950)
            mousePress(c, 50, 100)  // document 750, 950
            for (let x = 55; x <= 200; x += 5)
                mouseMove(c, x, 100)
            c.view = w  // the pointer stays: the document point under it is 50 px further
            for (let x = 200; x <= 300; x += 5)
                mouseMove(c, x, 100)
            mouseRelease(c, 300, 100)
            c.wait()
            verify(shows(c, w), JSON.stringify(c.shown))
            counts(c, 1 + 30 + 21)
            compare([c.composite(0, 900, 950), c.composite(0, 950, 950), c.composite(0, 1040, 950)], [blue, blue, blue])
            compare(c.composite(0, 1150, 950), past, "nothing past its end, document 1050")
            exact(c, 2)
        }

        // Nothing while still: after pen up, nor without a tool, nor past the document.
        function test_still() {
            const c = open()
            stroke(c, line(950))
            wait(300)
            const n = c.frames
            wait(500)
            compare(c.frames, n, "still: no frame")
            mousePress(c, 100, 200)  // a stroke, its step not waited for: the tool changes right after pen up
            mouseMove(c, 120, 200)
            mouseRelease(c, 120, 200)
            for (const t of ["", "zoom", "hand"]) {
                c.tool = t
                mousePress(c, 100, 100)
                mouseMove(c, 150, 120)
                mouseRelease(c, 150, 120)
                c.wait()
                compare(c.steps, ["Brush", "Brush"], "no stroke with tool '" + t + "'")
            }
            c.tool = "brush"
            stroke(c, line(1000, 700, 750))
            compare(c.steps, ["Brush", "Brush", "Brush"])
            const e = createTemporaryObject(dcC, root, { width: 100, height: 100, tool: "brush" })  // no document
            mousePress(e, 50, 50)
            mouseRelease(e, 50, 50)
            compare(e.steps, [])
        }

        // C4's test strokes through Qt's queue as a platform hands input over, at their pace and all at once, the
        // mouse's and the pen's: every event comes, none merged.
        function test_queue() {
            let log = local(Qt.resolvedUrl("../../tests/data/strokes/rgb8-paint.jsonl")), L = null
            const c = open()
            L = c.strokeLog(log)
            if (!L.events) {  // Windows: beside this file
                log = local(Qt.resolvedUrl("rgb8-paint.jsonl"))
                L = c.strokeLog(log)
            }
            verify(L.events > 50, log)
            show(c, { x: L.cx, y: L.cy, zoom: 0.5, angle: 0 })
            for (const kind of ["mouse", "pen"])
                for (const pace of [0, 1]) {
                    const n = c.play(log, kind, pace)
                    compare(n, L.events)
                    c.wait()
                    const s = counts(c, n)
                    console.log("C3b: " + n + " events of the " + kind + " " + (pace ? "at their pace" : "all at once") + ": " + s.added +
                                " in, " + s.shown + " shown in " + s.frames + " frames; from Qt's queue to the canvas p50 " +
                                s.qtP50.toFixed(2) + " ms, p95 " + s.qtP95.toFixed(2) + " ms")
                }
            compare(c.steps.length, 4)
            const s = c.stats()
            if (c.gpu)  // the stroke's frames, faster than the window's, reuse a few textures, each imported once
                verify(s.textures <= 8 && s.imports <= s.textures, s.textures + " textures, " + s.imports + " imports")
        }

        // ---- PB_CANVAS=<file> ---------------------------------------------------------------------------------------
        function percentile(a, q) { return a.length ? a.slice().sort((x, y) => x - y)[Math.min(a.length - 1, Math.floor(a.length * q))] : NaN }
        function ms(a) { return [0.5, 0.95, 0.99].map(q => percentile(a, q).toFixed(2)).join(" / ") }
        function quiet(c) {  // until no frame came for 500 ms: the view's tiles and mips made, as E3's benches paint
            let n = -1
            for (let k = 0; k < 60 && c.frames !== n; k++) {
                n = c.frames
                wait(500)
            }
        }
        function cleanup() {
            root.width = 400
            root.height = 300
        }
        // C4's D1 strokes (pb-bench make-strokes: 400 events at 200 Hz, 300 px with pressure on the size) through the
        // pen's way in, at their pace, on PB_CANVAS's file at 1920 x 1080 device pixels, the view at the logs' zoom
        // (0.5) about each stroke, sharp before it: every event in a frame; the latency from each event's arrival to the
        // submit of the first frame with it (Painting::stats(), G1: p95 ≤ 8 ms on the Iris Xe), from play() handing it
        // to Qt to its arrival, from a frame's submit to the window's swap with it.
        function test_file_paint() {
            if (CanvasEnv.document === "" || CanvasEnv.strokes === "")
                skip("PB_CANVAS=<file> and PB_STROKES=<folder> run this")
            root.width = Math.round(1920 / Screen.devicePixelRatio)
            root.height = Math.round(1080 / Screen.devicePixelRatio)
            const c = createTemporaryObject(dcC, root, { width: root.width, height: root.height, source: CanvasEnv.document, tool: "brush" })
            tryVerify(() => c.docSize.width > 0 || c.error !== "", 60000, "open " + CanvasEnv.document)
            compare(c.error, "")
            const all = { latency: [], qt: [], screen: [], frames: 0 }
            for (let k = 1; k <= 4; k++) {
                const log = CanvasEnv.strokes + "/d1-" + k + ".jsonl", L = c.strokeLog(log)
                verify(L.events > 0, log)
                show(c, { x: L.cx, y: L.cy, zoom: L.zoom, angle: 0 })
                quiet(c)
                const n = c.play(log, "pen")
                c.wait()
                const s = counts(c, n)
                all.latency = all.latency.concat(s.latency)
                all.qt = all.qt.concat(s.qt || [])
                all.screen = all.screen.concat(s.screen || [])
                all.frames += s.frames
                console.log("C3b " + log + " through the " + (c.gpu ? "GPU" : "CPU") + ": " + n + " events in " + s.frames + " frames; latency p50 / p95 / p99 " +
                            ms(s.latency) + " ms, max " + s.max.toFixed(2) + "; frame p50 " + s.frameP50.toFixed(2) + ", p95 " + s.frameP95.toFixed(2) +
                            " ms; Qt's queue to the canvas p50 / p95 / p99 " + ms(s.qt) + " ms; submit to swap " + ms(s.screen) + " ms")
                if (k === 1)
                    check(c, 7, 10000)
            }
            const t = c.stats()
            console.log("C3b all " + all.latency.length + " events through the " + (c.gpu ? "GPU" : "CPU") + " in " + all.frames +
                        " frames: latency p50 / p95 / p99 " + ms(all.latency) + " ms; Qt's queue to the canvas " + ms(all.qt) +
                        " ms; submit to swap " + ms(all.screen) + " ms; " + t.textures + " textures, " + t.imports + " imports")
            compare(c.steps, ["Brush", "Brush", "Brush", "Eraser"])
        }
        // tst_v2b's check(): c's frame against the CPU's composite at its view, every `step`-th device pixel; below 100 %
        // bilinear at the view's level, within the 2 x 2 texels around ±1; see-through texels and the canvas's edge left out.
        function check(c, step, least) {
            const img = grabImage(c), s = sizes(c), w = c.view, d = c.docSize, eps = 1e-3
            const L = Math.max(0, Math.floor(-Math.log2(w.zoom))), k = Math.pow(2, L)
            const tw = Math.ceil(d.width / k) - 1, th = Math.ceil(d.height / k) - 1
            let checked = 0, wrong = ""
            for (let j = step >> 1; j < img.height && !wrong; j += step)
                for (let i = step >> 1; i < img.width && !wrong; i += step) {
                    const p = under(c, s, i, j), got = [img.red(i, j), img.green(i, j), img.blue(i, j)]
                    if (p.x < eps || p.y < eps || p.x > d.width - eps || p.y > d.height - eps)
                        continue
                    const x0 = Math.floor(p.x / k - 0.5), y0 = Math.floor(p.y / k - 0.5)
                    let lo = [255, 255, 255], hi = [0, 0, 0], opaque = true
                    for (const q of [[x0, y0], [x0 + 1, y0], [x0, y0 + 1], [x0 + 1, y0 + 1]]) {
                        const t = c.composite(L, Math.min(Math.max(q[0], 0), tw), Math.min(Math.max(q[1], 0), th))
                        opaque = opaque && t[3] === 65535
                        for (let m = 0; m < 3; m++) {
                            lo[m] = Math.min(lo[m], byte(t[m]) - 1)
                            hi[m] = Math.max(hi[m], byte(t[m]) + 1)
                        }
                    }
                    if (!opaque)
                        continue
                    if ([0, 1, 2].some(m => got[m] < lo[m] || got[m] > hi[m]))
                        wrong = i + ", " + j + " (document " + p.x.toFixed(2) + ", " + p.y.toFixed(2) + "): " + got + " not in " + lo + " … " + hi
                    checked++
                }
            verify(!wrong, JSON.stringify(w) + " at " + wrong)
            verify(checked >= least, checked + " pixels checked of " + JSON.stringify(w))
        }

        // pb-ui's Viewport in Main with PB_CANVAS's file: Brush and Eraser paint on its canvas (steps in the mock's
        // History too), Ctrl Z and Ctrl Shift Z undo and redo them there, Hand and the middle button pan.
        function test_file_main() {
            if (CanvasEnv.document === "")
                skip("PB_CANVAS=<file> runs this")
            Docs.reset()
            Tools.reset()
            const w = createTemporaryObject(mainC, root)
            w.requestActivate()
            tryVerify(() => w.active)
            const find = item => {
                for (const ch of item.children) {
                    const f = ch instanceof DocCanvas ? ch : find(ch)
                    if (f)
                        return f
                }
                return null
            }
            const dc = find(w.contentItem)
            verify(dc && dc.visible)
            tryVerify(() => dc.docSize.width > 0 && dc.frames > 0 && !dc.shown.pending && !dc.shown.preview, 120000, "open")
            const d = Docs.current, at = Qt.point(dc.width / 2, dc.height / 2), p = dc.toDoc(at)
            const x = Math.floor(p.x), y = Math.floor(p.y), was = dc.composite(0, x, y), steps = d.history.length
            Tools.setTool("brush")
            Tools.fg = "#00ff00"
            compare(dc.tool, "brush")
            mousePress(dc, at.x - 30, at.y)
            for (let k = -25; k <= 30; k += 5)
                mouseMove(dc, at.x + k, at.y)
            mouseRelease(dc, at.x + 30, at.y)
            dc.wait()
            compare(d.history.length, steps + 1, "a step in the mock's History")
            compare(d.history[d.history.length - 1].name, "Brush")
            const green = dc.composite(0, x, y)  // on D1's Burn, Soft Light: greener
            verify(green[1] - was[1] > green[0] - was[0] && green[1] - was[1] > green[2] - was[2], "painted green: " + green + ", was " + was)
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            dc.wait()
            compare(dc.composite(0, x, y), was, "Ctrl Z")
            keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
            dc.wait()
            compare(dc.composite(0, x, y), green, "Ctrl Shift Z")
            // the pen's through what lies over the canvas in Main (BrushCursor's handlers): Qt still makes its mouse events
            const q = dc.toDoc(Qt.point(at.x + 20, at.y + 40)), under = dc.composite(0, Math.floor(q.x), Math.floor(q.y)), n = d.history.length
            for (let k = 0; k <= 40; k += 5)
                dc.pen(k === 0 ? "press" : "move", Qt.point(at.x + k, at.y + 40), 0.5)
            dc.pen("release", Qt.point(at.x + 40, at.y + 40), 0)
            dc.wait()
            compare(d.history.length, n + 1, "the pen's step")
            verify(String(dc.composite(0, Math.floor(q.x), Math.floor(q.y))) !== String(under), "the pen painted")
            Tools.setTool("eraser")
            mousePress(dc, at.x, at.y - 30)
            mouseMove(dc, at.x, at.y)
            mouseMove(dc, at.x, at.y + 30)
            mouseRelease(dc, at.x, at.y + 30)
            dc.wait()
            compare(d.history[d.history.length - 1].name, "Eraser")
            verify(String(dc.composite(0, x, y)) !== String(green), "erased: " + dc.composite(0, x, y))
            const vp = dc.parent.parent  // Viewport: at fit there is nothing to pan
            vp.show({ x: p.x, y: p.y, zoom: 0.5, angle: 0 })
            tryVerify(() => dc.view.zoom === 0.5)
            Tools.setTool("hand")  // the left button pans again
            const x0 = dc.view.x  // a number: a QVariantMap property reads again through what it gave
            mousePress(dc, at.x, at.y)
            mouseMove(dc, at.x + 40, at.y)
            mouseRelease(dc, at.x + 40, at.y)
            tryVerify(() => dc.view.x !== x0, 5000, "the Hand pans")
            compare(d.history[d.history.length - 1].name, "Eraser", "no stroke with the Hand")
            Tools.setTool("brush")
            const x1 = dc.view.x
            mousePress(dc, at.x, at.y, Qt.MiddleButton)
            mouseMove(dc, at.x - 40, at.y, -1, Qt.MiddleButton)
            mouseRelease(dc, at.x - 40, at.y, Qt.MiddleButton)
            tryVerify(() => dc.view.x !== x1, 5000, "the middle button pans")
            compare(d.history[d.history.length - 1].name, "Eraser", "no stroke with the middle button")
            Docs.reset()
            Tools.reset()
        }
    }
}
