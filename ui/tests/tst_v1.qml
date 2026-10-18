import QtQuick
import QtTest
import PhotoBeauty
import "../qml/View.js" as View

// V1: DocCanvas, here through the CPU (offscreen): its test card at the device pixels View.js puts the document at,
// toCanvas() and toDoc() as View.js's, no frame while nothing changes; in Viewport with PB_CANVAS=pattern (run this file
// with it too). With QT_QPA_PLATFORM=windows and PB_CANVAS=pattern on a GPU that shares memory, frames go through the
// GPU; test_frames says which and how long.
Item {
    id: root
    width: 320
    height: 200
    Component { id: vpC; Viewport {} }
    Component { id: dcC; DocCanvas {} }

    DocCanvas {
        id: canvas
        anchors.fill: parent
    }
    property int swaps: 0  // the window's frames
    Connections {
        target: root.Window.window
        function onFrameSwapped() { root.swaps++ }
    }

    TestCase {
        name: "V1"
        when: windowShown

        function near(a, b, msg) {
            verify(Math.abs(a.x - b.x) < 1e-9 * (1 + Math.abs(b.x)) && Math.abs(a.y - b.y) < 1e-9 * (1 + Math.abs(b.y)),
                   msg + ": " + JSON.stringify(a) + " ≠ " + JSON.stringify(b))
        }
        function size() { return { w: canvas.width, h: canvas.height, dpr: Screen.devicePixelRatio } }
        // The frame up shows view v (shown: V2b). Frames come from a job: one of an earlier view may come up after v is set.
        function up(item, v) { const s = item.shown; return s.x === v.x && s.y === v.y && s.zoom === v.zoom && s.angle === v.angle }
        function show(view) {  // grabImage() renders what is up: no waitForRendering(), which waits for the next frame
            canvas.view = view
            tryVerify(() => up(canvas, view), 5000, "a frame of " + JSON.stringify(view))
        }
        // What device pixel (i, j) of a canvas of size s ({ w, h, dpr }) shows of the test card (render.h) at `view`, by
        // View.js: [R, G, B], or null where the document points under it and the pixels left and above lie too near a
        // pixel's edge for float to agree.
        function card(view, i, j, s) {
            const w = Math.round(s.w * s.dpr), h = Math.round(s.h * s.dpr), sx = s.w / w, sy = s.h / h
            const at = (a, b) => View.toDoc(view, s, { x: (a + 0.5) * sx, y: (b + 0.5) * sy })
            const o = at(0, 0), d = at(i, j), l = at(i - 1, j), u = at(i, j - 1)
            const eps = 1e-6 * (1 + Math.abs(o.x) + Math.abs(o.y) + (w + h) * 2 * sx * s.dpr / view.zoom)
            for (const q of [d, l, u])
                for (const c of [q.x, q.y])
                    if (Math.abs(c - Math.round(c)) < eps)
                        return null
            let g = 0
            while (g < 20 && view.zoom * Math.pow(2, g) < 4)
                g++
            const cell = q => [Math.floor(q.x) >> g, Math.floor(q.y) >> g], c = cell(d), cl = cell(l), cu = cell(u)
            const rgb = [(Math.floor(d.x) >> 4) & 255, (Math.floor(d.y) >> 4) & 255, ((c[0] >> 3) + (c[1] >> 3)) & 1 ? 224 : 96]
            return c[0] !== cl[0] || c[1] !== cl[1] || c[0] !== cu[0] || c[1] !== cu[1] ? rgb.map(v => 255 - v) : rgb
        }

        // A canvas's frame against the test card at its view, every third pixel: at least `least` checked, all right.
        function check(item, least) {
            const s = { w: item.width, h: item.height, dpr: Screen.devicePixelRatio }, img = grabImage(item), v = item.view
            let checked = 0, wrong = ""
            for (let j = 0; j < img.height; j += 3)
                for (let i = 0; i < img.width; i += 3) {
                    const want = card(v, i, j, s)
                    if (!want)
                        continue
                    const got = [img.red(i, j), img.green(i, j), img.blue(i, j)]
                    if (!wrong && (got[0] !== want[0] || got[1] !== want[1] || got[2] !== want[2]))
                        wrong = i + ", " + j + ": " + got + " ≠ " + want
                    checked++
                }
            verify(!wrong, JSON.stringify(v) + " at " + wrong)
            verify(checked > least, checked + " pixels checked of " + JSON.stringify(v))
        }

        function test_card() {
            const views = [{ x: 100.3, y: 50.6, zoom: 1, angle: 0 }, { x: 1000.3, y: 2000.7, zoom: 0.125, angle: 30 },
                           { x: 10.37, y: 20.11, zoom: 8, angle: -90 }, { x: -5.5, y: 77.25, zoom: 3.7, angle: 180 },
                           { x: 2200, y: 2800, zoom: 0.0833, angle: 45 }, { x: 0.5, y: 0.5, zoom: 128, angle: 0 }]
            for (const v of views) {
                show(v)
                check(canvas, 3000)
            }
        }

        // Two canvases at once, as Viewport's and a space's (U15): each its own frames, textures and picture; none while
        // still.
        function test_two() {
            canvas.visible = false
            const a = createTemporaryObject(dcC, root, { width: 160, height: 200 })
            const b = createTemporaryObject(dcC, root, { x: 160, width: 160, height: 200 })
            for (let k = 0; k < 8; k++) {  // past the textures' pools
                a.view = { x: 100.3 + k, y: 50.6, zoom: 1, angle: 0 }
                b.view = { x: 1000.3, y: 2000.7 + k, zoom: 0.125, angle: 30 }
                tryVerify(() => up(a, a.view) && up(b, b.view), 5000, "a frame of each")
            }
            check(a, 1000)
            check(b, 1000)
            wait(200)
            const na = a.frames, nb = b.frames, swaps = root.swaps
            wait(500)
            compare([a.frames, b.frames, root.swaps], [na, nb, swaps], "still: no frame")
            canvas.visible = true
        }

        function test_to_canvas_to_doc() {
            for (const v of [{ x: 2200, y: 2800, zoom: 0.125, angle: 0 }, { x: 1000.5, y: -20, zoom: 2, angle: 30 }, { x: 0, y: 0, zoom: 128, angle: -45 }]) {
                canvas.view = v
                for (const p of [{ x: 0, y: 0 }, { x: 4400, y: 5600 }, { x: -123.5, y: 77.25 }]) {
                    near(canvas.toCanvas(Qt.point(p.x, p.y)), View.toCanvas(v, size(), p), "toCanvas " + JSON.stringify(v))
                    near(canvas.toDoc(Qt.point(p.x / 10, p.y / 10)), View.toDoc(v, size(), { x: p.x / 10, y: p.y / 10 }), "toDoc " + JSON.stringify(v))
                }
            }
            canvas.view = { x: 1, y: 2 }  // what it lacks: zoom 1, angle 0
            near(canvas.toCanvas(Qt.point(11, 12)), View.toCanvas({ x: 1, y: 2, zoom: 1, angle: 0 }, size(), { x: 11, y: 12 }),
                 "the view's defaults")
        }

        function test_still() {  // a frame when what it shows changes, none otherwise
            show({ x: 5, y: 5, zoom: 2, angle: 10 })
            const n = canvas.frames
            canvas.view = { x: 5, y: 5, zoom: 2, angle: 10 }
            wait(300)
            compare(canvas.frames, n, "the same view")
            canvas.view = { x: 5, y: 5, zoom: 2, angle: 11 }
            tryCompare(canvas, "frames", n + 1)
            root.width = 321
            tryCompare(canvas, "frames", n + 2)
            root.width = 320
            tryCompare(canvas, "frames", n + 3)
            canvas.visible = false
            canvas.view = { x: 6, y: 5, zoom: 2, angle: 11 }
            wait(100)
            compare(canvas.frames, n + 3, "hidden")
            canvas.visible = true
            tryCompare(canvas, "frames", n + 4)
            wait(300)
            compare(canvas.frames, n + 4)
            canvas.view = { x: 6, y: 5, zoom: 2, angle: 11, more: 1 }  // another map, the same view
            canvas.visible = false
            canvas.visible = true  // hidden and shown again as it was
            wait(300)
            compare(canvas.frames, n + 4, "the same view")
        }

        // Viewport's DocCanvas: on Viewport.view with PB_CANVAS=pattern, its points where View.js has the overlays'; none
        // without.
        // Viewport's DocCanvas: on Viewport.view with PB_CANVAS=pattern, its points where View.js has the overlays'; none
        // without. Two Viewports on one document, as the window's and a space's (U15).
        function test_viewport() {
            Docs.reset()  // the prototype's documents, whatever the files before left
            const find = item => {
                for (const c of item.children) {
                    const f = c instanceof DocCanvas ? c : find(c)
                    if (f)
                        return f
                }
                return null
            }
            const vps = [createTemporaryObject(vpC, root, { width: 300, height: 200 }),
                         createTemporaryObject(vpC, root, { width: 200, height: 150 })]
            for (const vp of vps) {
                const dc = find(vp)
                verify(dc && vp.view, "a DocCanvas in the frame, a document's view")
                compare(dc.visible, CanvasEnv.pattern)
                if (!CanvasEnv.pattern)
                    continue
                tryVerify(() => dc.frames > 0)
                compare(dc.view, vp.view)
                for (const p of [{ x: 0, y: 0 }, { x: 4400, y: 5600 }, { x: 1234.5, y: 77 }])
                    near(dc.toCanvas(Qt.point(p.x, p.y)), View.toCanvas(vp.view, vp.size, p), "the overlays' points")
            }
            wait(200)
            const n = vps.map(vp => find(vp).frames)
            wait(300)
            compare(vps.map(vp => find(vp).frames), n, "still: no frame")
        }

        // The route and the time a frame takes, 120 frames of a pan at 1920 x 1080 device pixels, then a second of none.
        function test_frames() {
            root.width = Math.round(1920 / Screen.devicePixelRatio)
            root.height = Math.round(1080 / Screen.devicePixelRatio)
            const times = []
            for (let k = 0; k < 120; k++) {
                show({ x: 1000 + k, y: 700, zoom: 1, angle: 0 })
                times.push(canvas.frameMs)
            }
            times.sort((a, b) => a - b)
            console.log("V1 frames through the " + (canvas.gpu ? "GPU" : "CPU") + " (" + ["?", "Software", "OpenVG", "OpenGL", "Direct3D 11", "Vulkan", "Metal", "Null", "Direct3D 12"][canvas.GraphicsInfo.api] +
                        "), " + canvas.width * Screen.devicePixelRatio + " x " + canvas.height * Screen.devicePixelRatio + ": median " + times[60].toFixed(3) + " ms, p95 " + times[114].toFixed(3) + " ms")
            wait(200)  // the last frame on screen
            const n = canvas.frames, s = root.swaps
            wait(1000)
            console.log("V1 still for 1 s: " + (canvas.frames - n) + " frames of the canvas, " + (root.swaps - s) + " of the window")
            compare(canvas.frames, n, "still: no frame")
            compare(root.swaps, s, "still: the window draws none either")
            if (CanvasEnv.pattern && Qt.platform.pluginName === "windows")
                compare(canvas.gpu, CanvasEnv.gpuFrames, "PB_CANVAS=pattern in a window: through the GPU, unless PB_GPU_FRAMES=0")
            root.width = 320
            root.height = 200
        }
    }
}
