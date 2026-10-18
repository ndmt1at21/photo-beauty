import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtTest
import PhotoBeauty
import "../qml/View.js" as View
import "../qml/Mock.js" as Mock
import "../qml/Fmt.js" as Fmt

// U6: View.js (the cases of V1's test_view), the canvas area (Viewport: pan, zoom, rotation, rulers, the right-click
// menu, the pointer), the mock canvas, the document tabs.
Item {
    id: root
    width: 200
    height: 100
    Component { id: mainC; Main {} }
    Component { id: vpC; Viewport {} }

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
        name: "U6"
        when: windowShown

        function init() {
            Docs.viewport = { w: 1034, h: 750 }  // as the prototype opens, whatever the last window left
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanupTestCase() {
            init()
            Theme.dark = true
        }

        function open(props) {
            const w = createTemporaryObject(mainC, root, props || { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function region(w, type) { return find(w.contentItem, o => o instanceof type) }
        // The canvas area's parts: vp, the frame (the canvas's item), its pointer area, the mock canvas and its page.
        function parts(w) {
            const vp = region(w, Viewport), mc = find(vp, o => o instanceof MockCanvas)
            return { vp: vp, frame: mc.parent, mc: mc, page: mc.children[0], input: find(vp, o => o instanceof MouseArea && o.hoverEnabled && !!(o.acceptedButtons & Qt.MiddleButton)) }
        }
        function near(a, b, eps, msg) {
            eps = eps || 1e-6
            verify(Math.abs(a.x - b.x) < eps && Math.abs(a.y - b.y) < eps, (msg || "") + " " + JSON.stringify(a) + " ≠ " + JSON.stringify(b))
        }
        // Keys, then a turn of the event loop: Viewport writes the view it shows back into Doc.view after its binding.
        function keys(s) {
            keySequence(s)
            wait(0)
        }
        // The window's pixel at a point of item (grabImage() crops by the item's x, y in its parent).
        function pixel(w, item, x, y) {
            const q = item.mapToItem(w.contentItem, x, y)
            return grabImage(w.contentItem).pixel(q.x, q.y)
        }
        function menuOf(item) { return item.resources.find(o => o instanceof PopupMenu) }
        function rows(m) {  // "!" greyed, "*" ticked, ">" a submenu, "-" a separator
            const out = []
            for (let i = 0; i < m.count; i++) {
                const it = m.itemAt(i)
                out.push(!(it instanceof MenuItem) ? "-" : (it.enabled ? "" : "!") + (it.tick ? "*" : "") + (it.subMenu ? ">" : "") + it.text)
            }
            return out
        }
        function pick(w, m, label) {
            waitForRendering(w.contentItem)
            for (let i = 0; i < m.count; i++)
                if (m.itemAt(i) instanceof MenuItem && m.itemAt(i).text === label)
                    return mouseClick(m.itemAt(i))
            fail("no " + label + " in " + rows(m))
        }

        // ---- View.js: the cases of V1's test_view (api/u6.md) --------------------------------------------------------
        function test_view_known() {  // chân dung.psd at 12,5 % in the 1.034 × 750 frame, as the prototype lays it out
            const size = { w: 1034, h: 750, dpr: 1 }, v = { x: 2200, y: 2800, zoom: 0.125, angle: 0 }
            compare(View.toCanvas(v, size, { x: 0, y: 0 }), { x: 242, y: 25 })
            compare(View.toCanvas(v, size, { x: 4400, y: 5600 }), { x: 792, y: 725 })
            compare(View.toDoc(v, size, { x: 517, y: 375 }), { x: 2200, y: 2800 })
            near(View.toCanvas({ x: 2200, y: 2800, zoom: 0.125, angle: 90 }, size, { x: 0, y: 0 }), { x: 867, y: 100 }, 1e-9, "90° clockwise: the top left goes top right")
            near(View.toCanvas({ x: 2200, y: 2800, zoom: 0.125, angle: -90 }, size, { x: 0, y: 0 }), { x: 167, y: 650 }, 1e-9)
            compare(View.toCanvas({ x: 2200, y: 2800, zoom: 1, angle: 0 }, { w: 1034, h: 750, dpr: 2 }, { x: 2300, y: 2900 }), { x: 567, y: 425 }, "100 % at dpr 2: a document pixel a device pixel, half a logical one")
            compare(View.scale({ zoom: 3, angle: 0, x: 0, y: 0 }, { w: 1, h: 1, dpr: 1.5 }), 2)
            compare(View.toCanvas(v, { w: 1034, h: 750 }, { x: 0, y: 0 }), { x: 242, y: 25 }, "no dpr: 1")
        }
        function test_view_round_trip() {  // document → canvas → document, any view
            const sizes = [{ w: 1034, h: 750, dpr: 1 }, { w: 640, h: 480, dpr: 2 }, { w: 1052, h: 768, dpr: 1.25 }]
            const views = [{ x: 2200, y: 2800, zoom: 0.125, angle: 0 }, { x: 1000.5, y: -20, zoom: 2, angle: 30 }, { x: 0, y: 0, zoom: 128, angle: -45 },
                { x: 3.25, y: 7, zoom: 0.01, angle: 180 }, { x: 900, y: 600, zoom: 0.5522, angle: 90 }]
            const pts = [{ x: 0, y: 0 }, { x: 4400, y: 5600 }, { x: -123.5, y: 77.25 }, { x: 1e5, y: -3e4 }]
            for (const size of sizes)
                for (const v of views)
                    for (const p of pts) {
                        near(View.toDoc(v, size, View.toCanvas(v, size, p)), p, 1e-6 * (1 + Math.abs(p.x) + Math.abs(p.y)))
                        const q = { x: p.x / 100, y: p.y / 100 }  // canvas points too
                        near(View.toCanvas(v, size, View.toDoc(v, size, q)), q, 1e-6)
                    }
        }
        function test_view_zoom_about_point() {  // the document point under p stays there, the zoom clamped and rounded
            const size = { w: 1034, h: 750, dpr: 1.5 }
            for (const v of [{ x: 2200, y: 2800, zoom: 0.125, angle: 0 }, { x: 10, y: 20, zoom: 3, angle: 30 }, { x: -50, y: 900, zoom: 0.5, angle: -135 }])
                for (const p of [{ x: 0, y: 0 }, { x: 300, y: 200 }, { x: 1034, y: 750 }, { x: 517, y: 375 }])
                    for (const z of [0.01, 0.0833, 1, 6.25, 128]) {
                        const u = View.zoomAt(v, size, p, z)
                        compare(u.zoom, z)
                        compare(u.angle, v.angle)
                        near(View.toDoc(u, size, p), View.toDoc(v, size, p), 1e-6, "zoom " + z + " about " + JSON.stringify(p))
                    }
            const v = { x: 1, y: 2, zoom: 1, angle: 0 }
            compare(View.zoomAt(v, size, { x: 0, y: 0 }, 500).zoom, 128)
            compare(View.zoomAt(v, size, { x: 0, y: 0 }, 0.001).zoom, 0.01)
            compare(View.zoomAt(v, size, { x: 0, y: 0 }, 0.123456).zoom, 0.1235)
            compare(View.zoomAt(v, size, { x: 517 / 1.5 * 1.5, y: 375 }, 4).x, 1, "about the centre: the pan stays")
        }
        function test_view_rotate_about_centre() {  // the point at the frame's centre stays; the rest turns about it, clockwise
            const size = { w: 1034, h: 750, dpr: 1 }, v = { x: 2200, y: 2800, zoom: 0.25, angle: 0 }, c = { x: 517, y: 375 }
            for (const a of [30, 90, -45, 180, 270, 359]) {
                const u = View.rotateTo(v, a), r = a * Math.PI / 180
                compare([u.x, u.y, u.zoom], [v.x, v.y, v.zoom])
                near(View.toCanvas(u, size, { x: v.x, y: v.y }), c, 1e-9)
                for (const p of [{ x: 0, y: 0 }, { x: 4400, y: 0 }, { x: 1234, y: 4321 }]) {
                    const o = View.toCanvas(v, size, p), t = View.toCanvas(u, size, p), dx = o.x - c.x, dy = o.y - c.y
                    near(t, { x: c.x + dx * Math.cos(r) - dy * Math.sin(r), y: c.y + dx * Math.sin(r) + dy * Math.cos(r) }, 1e-6, a + "°")
                }
            }
            compare(View.rotateTo(v, 200).angle, -160)
            compare(View.rotateTo(v, -180).angle, 180)
            compare(View.rotateTo(v, 540).angle, 180)
            compare(View.rotateTo(v, -30).angle, -30)
        }
        function test_view_fit_pan_constrain() {
            const size = { w: 1034, h: 750, dpr: 1 }
            compare(View.fit(4400, 5600, size, 0) * 100, Mock.fitZoom(4400, 5600, 1034, 750), "Vừa màn hình as Mock.fitZoom()")
            compare(View.fit(4400, 5600, { w: 1034, h: 750, dpr: 2 }, 0), 2 * View.fit(4400, 5600, size, 0))
            compare(View.fit(4400, 5600, size, 90), Math.round(Math.min(994 / 5600, 710 / 4400) * 1e4) / 1e4, "turned: its box")
            const v = { x: 2200, y: 2800, zoom: 1, angle: 0 }
            compare(View.pan(v, size, 100, -50), { x: 2100, y: 2850, zoom: 1, angle: 0 })
            near(View.toCanvas(View.pan({ x: 5, y: 6, zoom: 3, angle: 30 }, size, 40, 25), size, { x: 5, y: 6 }), { x: 557, y: 400 }, 1e-9)
            // In bounds: a box smaller than the frame sits in its middle, a bigger one covers it.
            compare(View.constrain(v, size, 4400, 5600), v, "covers: as it is")
            compare(View.constrain({ x: 100, y: 2800, zoom: 1, angle: 0 }, size, 4400, 5600).x, 517, "the left edge on the frame's")
            compare(View.constrain({ x: 4400, y: 2800, zoom: 1, angle: 0 }, size, 4400, 5600).x, 4400 - 517)
            compare(View.constrain({ x: 0, y: 0, zoom: 0.125, angle: 0 }, size, 4400, 5600), { x: 2200, y: 2800, zoom: 0.125, angle: 0 }, "small: centred")
            const u = View.constrain({ x: 900, y: 2800, zoom: 0.2, angle: 0 }, size, 4400, 5600)  // 880 wide: centred; 1.120 high: in bounds
            compare(u.x, 2200)
            compare(u.y, 2800)
            const t = View.constrain({ x: 0, y: 0, zoom: 0.08, angle: 30 }, size, 4400, 5600)  // its box 529 × 564
            near({ x: t.x, y: t.y }, { x: 2200, y: 2800 }, 1e-6, "turned, its box centred")
            const big = View.constrain({ x: -1e4, y: 2800, zoom: 1, angle: 45 }, size, 4400, 5600), corners = [[0, 0], [4400, 0], [0, 5600], [4400, 5600]].map(p => View.toCanvas(big, size, { x: p[0], y: p[1] }).x)
            verify(Math.abs(Math.min.apply(null, corners)) < 1e-6, "turned, its box's left edge on the frame's")
        }
        function test_view_steps() {  // Mock.ZOOMS up and down, and past its ends
            let z = 0.01
            for (let i = 1; i < Mock.ZOOMS.length; i++) {
                z = View.step(z, 1)
                compare(z * 100, Mock.ZOOMS[i])
            }
            compare(View.step(z, 1), 128)
            for (let i = Mock.ZOOMS.length - 2; i >= 0; i--) {
                z = View.step(z, -1)
                compare(z * 100, Mock.ZOOMS[i])
            }
            compare(View.step(0.01, -1), 0.01)
            compare(View.step(0.15, 1), 0.1667)
            compare(View.step(0.15, -1), 0.125)
        }

        // ---- zoom: the steps and the status bar's box --------------------------------------------------------------
        // Bậc zoom và ô zoom của thanh trạng thái khớp nhau: Ctrl + and − walk ZOOMS, the box, the tab and the canvas
        // show each step; Alt + the wheel takes the same steps; the box typed moves the canvas.
        function test_zoom_steps() {
            const w = open(), c = parts(w), d = Docs.current
            const box = find(region(w, StatusBar), o => o instanceof TextField), tab = find(region(w, DocTabs), o => o instanceof AbstractButton && o.text.startsWith("chân dung.psd"))
            const shows = pct => {
                compare(d.view.zoom * 100, pct)
                compare(box.text, Fmt.zt(pct))
                verify(tab.text.indexOf("@ " + Fmt.zt(pct) + " (") > 0, tab.text)
                compare(c.page.width, 4400 * pct / 100)
                compare(c.page.height, 5600 * pct / 100)
            }
            d.zoomTo(1)
            shows(1)
            for (let i = 1; i < Mock.ZOOMS.length; i++) {
                keys("Ctrl+=")
                shows(Mock.ZOOMS[i])
            }
            for (let i = Mock.ZOOMS.length - 2; i >= 0; i--) {
                keys("Ctrl+-")
                shows(Mock.ZOOMS[i])
            }
            keys("Ctrl+1")
            shows(100)
            for (const pct of [200, 300, 400, 600]) {
                mouseWheel(c.frame, 517, 375, 0, 120, Qt.NoButton, Qt.AltModifier)
                shows(pct)
            }
            mouseWheel(c.frame, 517, 375, 0, -240, Qt.NoButton, Qt.AltModifier)  // two notches at once
            shows(300)
            mouseClick(box)
            box.selectAll()
            keyClick(Qt.Key_8)
            keyClick(Qt.Key_Comma)
            keyClick(Qt.Key_3)
            keyClick(Qt.Key_3)
            keyClick(Qt.Key_Return)
            shows(8.33)
            keys("Ctrl+=")
            shows(12.5)
            keys("Ctrl+0")  // Vừa màn hình, centred
            shows(Mock.fitZoom(4400, 5600, 1034, 750))
            compare([c.page.x, c.page.y], [517 - c.page.width / 2, 375 - c.page.height / 2])
        }
        function test_wheel() {  // Alt: zoom about the pointer; else a pan, Shift or Ctrl across; Zoom bằng con lăn swaps them
            const w = open(), c = parts(w), d = Docs.current
            keys("Ctrl+1")
            const size = c.vp.size, p = { x: 300, y: 200 }, at = View.toDoc(d.view, size, p)
            mouseWheel(c.frame, p.x, p.y, 0, 120, Qt.NoButton, Qt.AltModifier)
            compare(d.view.zoom, 2)
            near(View.toDoc(d.view, size, p), at, 1e-6, "the point under the pointer stays")
            mouseWheel(c.frame, p.x, p.y, 0, -120, Qt.NoButton, Qt.AltModifier)
            compare(d.view.zoom, 1)
            near(View.toDoc(d.view, size, p), at, 1e-6)
            const v = d.view
            mouseWheel(c.frame, p.x, p.y, 0, -120)  // down: the picture goes up, 60 px a notch
            compare([d.view.x, d.view.y, d.view.zoom], [v.x, v.y + 60, 1])
            mouseWheel(c.frame, p.x, p.y, 0, -120, Qt.NoButton, Qt.ShiftModifier)
            compare([d.view.x, d.view.y], [v.x + 60, v.y + 60])
            mouseWheel(c.frame, p.x, p.y, 0, 240, Qt.NoButton, Qt.ControlModifier)
            compare([d.view.x, d.view.y], [v.x - 60, v.y + 60])
            mouseWheel(c.frame, p.x, p.y, 0, 60)  // half a notch: half the pan, no zoom yet
            compare(d.view.y, v.y + 30)
            Prefs.set("wheelZoom", true)
            mouseWheel(c.frame, p.x, p.y, 0, 60)
            mouseWheel(c.frame, p.x, p.y, 0, 60)
            compare(d.view.zoom, 2, "two halves make a notch")
            mouseWheel(c.frame, p.x, p.y, 0, -120, Qt.NoButton, Qt.AltModifier)
            compare(d.view.zoom, 2, "Alt: a pan then")
            keys("Ctrl+0")
            const fit = d.view
            Prefs.set("wheelZoom", false)
            mouseWheel(c.frame, p.x, p.y, 0, -360)
            compare(d.view, fit, "the whole picture in the frame: nowhere to pan")
        }

        // ---- pan, the Zoom tool, Rotate View ---------------------------------------------------------------------------
        function test_pan() {  // Hand, the middle button; the picture covers the frame or sits in its middle
            const w = open(), c = parts(w), d = Docs.current
            keys("Ctrl+1")
            Tools.tool = "hand"
            compare(c.input.cursorShape, Qt.OpenHandCursor)
            mousePress(c.frame, 500, 400)
            mouseMove(c.frame, 400, 300, -1, Qt.LeftButton)
            compare(c.input.cursorShape, Qt.ClosedHandCursor)
            compare([d.view.x, d.view.y], [2300, 2900])
            mouseMove(c.frame, 450, 420, -1, Qt.LeftButton)
            compare([d.view.x, d.view.y], [2250, 2780])
            mouseRelease(c.frame, 450, 420)
            compare(c.input.cursorShape, Qt.OpenHandCursor)
            Tools.tool = "brush"
            mousePress(c.frame, 500, 400, Qt.MiddleButton)
            mouseMove(c.frame, 520, 400, -1, Qt.MiddleButton)
            mouseRelease(c.frame, 520, 400, Qt.MiddleButton)
            compare([d.view.x, d.view.y], [2230, 2780])
            mousePress(c.frame, 10, 10, Qt.MiddleButton)
            mouseMove(c.frame, 3000, 4000, -1, Qt.MiddleButton)  // past the window: the press holds on
            mouseRelease(c.frame, 3000, 4000, Qt.MiddleButton)
            near(View.toCanvas(d.view, c.vp.size, { x: 0, y: 0 }), { x: 0, y: 0 }, 1e-6, "the top left at the frame's: no further")
            near(c.page.mapToItem(c.frame, 0, 0), { x: 0, y: 0 }, 1e-3)
            keys("Ctrl+0")
            const fit = d.view
            Tools.tool = "hand"
            mousePress(c.frame, 500, 400)
            mouseMove(c.frame, 300, 100, -1, Qt.LeftButton)
            mouseRelease(c.frame, 300, 100)
            compare(d.view, fit, "small: centred")
            compare([d.view.x, d.view.y], [2200, 2800])
        }
        function test_zoom_tool() {  // a click a step about it (Alt, or its bar: out), Scrubby Zoom's drag, held still
            const w = open(), c = parts(w), d = Docs.current
            keys("Ctrl+1")
            Tools.tool = "zoom"
            const size = c.vp.size, p = { x: 300, y: 200 }, at = View.toDoc(d.view, size, p)
            mouseClick(c.frame, p.x, p.y)
            compare(d.view.zoom, 2)
            near(View.toDoc(d.view, size, p), at, 1e-6, "about the click")
            mouseClick(c.frame, p.x, p.y, Qt.LeftButton, Qt.AltModifier)
            compare(d.view.zoom, 1)
            near(View.toDoc(d.view, size, p), at, 1e-6)
            Tools.setValue("zoom:2", 1)  // Thu
            mouseClick(c.frame, p.x, p.y)
            compare(d.view.zoom, 0.6667)
            mouseClick(c.frame, p.x, p.y, Qt.LeftButton, Qt.AltModifier)
            compare(d.view.zoom, 1)
            Tools.setValue("zoom:2", 0)
            Prefs.set("zoomToClick", false)
            const v = d.view
            mouseClick(c.frame, p.x, p.y)
            compare([d.view.x, d.view.y, d.view.zoom], [v.x, v.y, 2], "about the centre")
            Prefs.set("zoomToClick", true)
            keys("Ctrl+1")
            const at2 = View.toDoc(d.view, size, p)
            mousePress(c.frame, p.x, p.y)  // Scrubby Zoom: right in, 150 px twice as near
            mouseMove(c.frame, p.x + 75, p.y, -1, Qt.LeftButton)
            compare(d.view.zoom, Math.round(Math.SQRT2 * 1e4) / 1e4)
            mouseMove(c.frame, p.x + 150, p.y + 30, -1, Qt.LeftButton)
            compare(d.view.zoom, 2)
            near(View.toDoc(d.view, size, p), at2, 1e-6, "about the press")
            mouseMove(c.frame, p.x - 150, p.y, -1, Qt.LeftButton)
            compare(d.view.zoom, 0.5)
            mouseRelease(c.frame, p.x - 150, p.y)
            compare(d.view.zoom, 0.5, "dragged: no step on release")
            Tools.setValue("zoom:4", false)
            keys("Ctrl+1")
            mousePress(c.frame, p.x, p.y)
            mouseMove(c.frame, p.x + 150, p.y, -1, Qt.LeftButton)
            mouseRelease(c.frame, p.x + 150, p.y)
            compare(d.view.zoom, 1, "no Scrubby Zoom: a drag does nothing")
            const at3 = View.toDoc(d.view, size, p)
            mousePress(c.frame, p.x, p.y)  // Zoom có chuyển động: held, it zooms on
            tryVerify(() => c.input.held && d.view.zoom > 1.1)
            const zb = d.view.zoom
            mouseRelease(c.frame, p.x, p.y)
            const za = d.view.zoom
            verify(za < zb * 1.5, "no step on release: " + zb + " → " + za)
            wait(100)
            compare(d.view.zoom, za, "stopped")
            near(View.toDoc(d.view, size, p), at3, 1e-3)
            Prefs.set("animatedZoom", false)
            keys("Ctrl+1")
            mousePress(c.frame, p.x, p.y)
            wait(500)
            mouseRelease(c.frame, p.x, p.y)
            compare(d.view.zoom, 2, "off: a click, however long")
        }
        function test_rotate() {  // Rotate View: about the frame's centre, Shift 15° steps, Esc back
            const w = open(), c = parts(w), d = Docs.current, size = c.vp.size
            Tools.tool = "rot"
            compare(c.input.cursorShape, Qt.CrossCursor)
            mousePress(c.frame, 717, 375)
            mouseMove(c.frame, 517, 575, -1, Qt.LeftButton)  // a quarter turn clockwise
            compare([d.view.x, d.view.y, d.view.zoom], [2200, 2800, 0.125])
            verify(Math.abs(d.view.angle - 90) < 1e-9, d.view.angle)
            mouseMove(c.frame, 717, 175, -1, Qt.LeftButton)
            verify(Math.abs(d.view.angle + 45) < 1e-9, d.view.angle)
            mouseMove(c.frame, 677, 495, -1, Qt.LeftButton, Qt.ShiftModifier)  // 36,87°: 15° steps
            compare(d.view.angle, 30)
            mouseRelease(c.frame, 677, 495, Qt.LeftButton, Qt.ShiftModifier)
            compare(c.page.rotation, 30)
            for (const p of [{ x: 0, y: 0 }, { x: 4400, y: 0 }, { x: 0, y: 5600 }]) {
                const q = c.page.mapToItem(c.frame, p.x * 0.125, p.y * 0.125)
                near(q, View.toCanvas(d.view, size, p), 1e-3, "the canvas where View.js says")
            }
            mousePress(c.frame, 600, 375)  // on from where it is
            mouseMove(c.frame, 517, 458, -1, Qt.LeftButton)
            mouseRelease(c.frame, 517, 458)
            verify(Math.abs(d.view.angle - 120) < 1e-9, d.view.angle)
            keyClick(Qt.Key_Escape)
            compare(d.view.angle, 0)
            mousePress(c.frame, 300, 375)
            mouseMove(c.frame, 517, 158, -1, Qt.LeftButton)  // from the left to the top: a quarter turn
            mouseMove(c.frame, 734, 375, -1, Qt.LeftButton)  // on to the right: half
            mouseMove(c.frame, 517, 592, -1, Qt.LeftButton)
            mouseRelease(c.frame, 517, 592)
            compare(Math.round(d.view.angle), -90, "270° is −90°")
            Tools.tool = "brush"
            keyClick(Qt.Key_Escape)
            compare(Math.round(d.view.angle), -90, "Esc: only with Rotate View")
        }

        // Tools.pointer (Info, U12): the document pixel under the pointer on the canvas, outside the image too; null off it.
        function test_pointer() {
            const w = open(), c = parts(w), d = Docs.current, size = c.vp.size
            const under = (x, y) => {
                const p = View.toDoc(c.vp.view, size, { x: x, y: y })
                return { x: Math.floor(p.x), y: Math.floor(p.y) }
            }
            compare(Tools.pointer, null)
            mouseMove(c.frame, 300, 200)
            compare(Tools.pointer, { x: 464, y: 1400 })  // 2.200 + (300 − 517) × 8, 2.800 + (200 − 375) × 8
            keys("Ctrl+1")
            compare(Tools.pointer, { x: 1983, y: 2625 }, "the view moved under a still pointer")
            mouseWheel(c.frame, 300, 200, 0, 120, Qt.NoButton, Qt.AltModifier)
            compare(d.view.zoom, 2)
            compare(Tools.pointer, { x: 1983, y: 2625 }, "zoomed about it: the same pixel")
            mouseMove(c.frame, 301, 201)
            compare(Tools.pointer, { x: 1983, y: 2625 }, "half a pixel on at 200 %")
            mouseMove(c.frame, 302, 202)
            compare(Tools.pointer, { x: 1984, y: 2626 })
            d.setView({ angle: 30 })
            compare(Tools.pointer, under(302, 202), "turned under it")
            d.setView({ angle: 0 })
            keys("Ctrl+0")  // the whole image, the grey around it
            mouseMove(c.frame, 5, 5)
            compare(Tools.pointer, under(5, 5))
            verify(Tools.pointer.x < 0 && Tools.pointer.y < 0, "off the image, above and left of it: as Photoshop's Info " + JSON.stringify(Tools.pointer))
            mouseMove(c.frame, c.frame.width - 5, c.frame.height - 5)
            compare(Tools.pointer, under(c.frame.width - 5, c.frame.height - 5))
            verify(Tools.pointer.x >= d.w && Tools.pointer.y >= d.h, "below and right of it: past w, h " + JSON.stringify(Tools.pointer))
            mouseMove(c.vp, 9, 9)  // the rulers' corner: off the canvas
            compare(Tools.pointer, null)
            mouseMove(c.frame, 600, 300)
            compare(Tools.pointer, under(600, 300))
            createTemporaryObject(vpC, root, { doc: Docs.docs[1], width: 300, height: 200 }).destroy()  // a second canvas (U15)
            wait(0)
            compare(Tools.pointer, under(600, 300), "a canvas away from the pointer keeps out, coming and going")
            Docs.closeAll()  // no document: no canvas
            compare(Tools.pointer, null)
            Docs.reset()
            waitForRendering(w.contentItem)
            mouseMove(c.vp, 9, 9)  // Qt takes the canvas for hovered still: off it and back
            mouseMove(c.frame, 600, 300)
            compare(Tools.pointer, under(600, 300))
            w.destroy()  // the canvas gone from under the pointer (U15's space closed)
            wait(0)
            compare(Tools.pointer, null)
        }

        // ---- the mock canvas ---------------------------------------------------------------------------------------
        function test_canvas() {  // the photo turned and flipped as the prototype's; checker, colour, border, the grey around
            const w = open(), c = parts(w), d = Docs.current, mc = c.mc
            const photo = find(mc, o => o instanceof Image && o.fillMode === Image.PreserveAspectCrop)
            const checker = find(mc, o => o instanceof Image && o.fillMode === Image.Tile)
            const line = find(mc, o => o instanceof Rectangle && o.border.width === 1 && o.x === -1)
            const shadow = find(mc, o => o instanceof RectangularShadow)
            compare([c.page.x, c.page.y, c.page.width, c.page.height], [242, 25, 550, 700])
            verify(photo.visible && !checker.visible && line.visible && shadow.visible)
            compare(photo.source, Qt.resolvedUrl("qrc:/qt/qml/PhotoBeauty/mock/photos/chandung2.jpg"))
            compare([photo.width, photo.height, photo.rotation], [550, 700, 0])
            const px = (x, y) => {  // the checker's tile is a data URL, loaded in the background
                tryVerify(() => checker.status === Image.Ready || !checker.visible)
                waitForRendering(w.contentItem)
                return pixel(w, c.frame, x, y)
            }
            const bg = d.layers.find(l => l.props.background).id
            d.run({ op: "setProps", layer: bg, props: { visible: false } })
            waitForRendering(w.contentItem)
            verify(!photo.visible && checker.visible)
            // The prototype's checker: 4 px squares, white at the top left, #c9c9c9 beside it
            verify(Qt.colorEqual(px(243, 26), "#ffffff") && Qt.colorEqual(px(247, 26), "#c9c9c9") && Qt.colorEqual(px(243, 30), "#c9c9c9") && Qt.colorEqual(px(247, 30), "#ffffff"), px(247, 26))
            Prefs.set("checkerSize", 3)
            Prefs.set("checkerColor2", "#3366ff")
            waitForRendering(w.contentItem)
            verify(Qt.colorEqual(px(249, 26), "#ffffff") && Qt.colorEqual(px(251, 26), "#3366ff"), px(251, 26))
            Prefs.set("checkerSize", 0)
            waitForRendering(w.contentItem)
            verify(!checker.visible && Qt.colorEqual(px(251, 26), "#ffffff"))
            Prefs.reset()
            d.undo()
            verify(photo.visible && !checker.visible)
            d.run({ op: "rotateLayer", angle: 90 })  // the layer turned: transparent where it no longer covers
            compare(photo.rotation, 90)
            verify(photo.visible && checker.visible)
            waitForRendering(w.contentItem)
            verify(Qt.colorEqual(px(243, 26), "#ffffff"), "the checker above the turned layer")
            d.undo()
            d.run({ op: "rotateCanvas", angle: 90 })  // the canvas: its sides swap, the photo still covers it
            compare([d.w, d.h], [5600, 4400])
            compare([photo.width, photo.height, photo.rotation], [550, 700, 90])
            compare([c.page.width, c.page.height], [700, 550])
            verify(!checker.visible)
            d.run({ op: "rotateCanvas", angle: 180 })
            compare(photo.rotation, 270)
            d.run({ op: "rotateLayer", angle: 90 })
            compare(photo.rotation, 0)
            verify(checker.visible)
            const flips = () => [photo.transform[0].xScale, photo.transform[0].yScale]
            compare(flips(), [1, 1])
            d.run({ op: "flipCanvas", axis: "h" })
            compare(flips(), [-1, 1])
            d.run({ op: "flipLayer", axis: "h" })
            compare(flips(), [1, 1], "flipped twice")
            d.run({ op: "flipLayer", axis: "v" })
            compare(flips(), [1, -1])
            Prefs.set("canvasBorder", 1)
            verify(line.visible && !shadow.visible)
            Prefs.set("canvasBorder", 2)
            verify(!line.visible && !shadow.visible)
            // New documents: their background colour, or the checker when transparent
            Docs.create({ name: "trắng", w: 800, h: 600, background: "custom", color: "#336699" })
            waitForRendering(w.contentItem)
            verify(!photo.visible && !checker.visible && mc.solid)
            verify(Qt.colorEqual(px(517, 375), "#336699"), px(517, 375))
            Docs.create({ name: "trong", w: 800, h: 600, background: "transparent" })
            waitForRendering(w.contentItem)
            verify(!photo.visible && checker.visible && !mc.solid)
        }
        function test_surround() {  // Nền quanh ảnh for each screen mode, from Thiết lập
            const w = open(), vp = region(w, Viewport)
            verify(Qt.colorEqual(vp.color, "#282828"), vp.color)
            Prefs.set("canvasStandard", 3)
            verify(Qt.colorEqual(vp.color, "#8e8e8e"))
            Prefs.set("canvasStandard", 4)  // Tùy chọn…: a colour of its own a screen mode, Thiết lập's Color Picker sets it (U23)
            Prefs.set("canvasStandardColor", "#336699")
            verify(Qt.colorEqual(vp.color, "#336699"), vp.color)
            Tools.screen = 1
            verify(Qt.colorEqual(vp.color, "#282828"))
            Prefs.set("canvasFullMenu", 2)
            verify(Qt.colorEqual(vp.color, "#535353"))
            Tools.screen = 2
            verify(Qt.colorEqual(vp.color, "#000000"))
            Prefs.set("canvasFull", 3)
            verify(Qt.colorEqual(vp.color, "#8e8e8e"))
            Prefs.set("canvasFull", 4)
            verify(Qt.colorEqual(vp.color, "#000000"), "its own, black until set")
            Tools.screen = 0
        }
        function test_cursor() {  // the pointer by tool; the Zoom tool's drawn
            const w = open(), c = parts(w)
            const shape = { brush: Qt.CrossCursor, rect: Qt.CrossCursor, hand: Qt.OpenHandCursor, zoom: Qt.BlankCursor, type: Qt.IBeamCursor, move: Qt.ArrowCursor, rot: Qt.CrossCursor }
            for (const t in shape) {
                Tools.tool = t
                compare(c.input.cursorShape, shape[t], t)
            }
            // The lens is the frame's own Icon; an overlay's icon comes before it in the frame (U23: was the first Icon).
            createTemporaryQmlObject("import PhotoBeauty; Icon { name: 'brush' }", find(c.frame, o => o instanceof Overlays))
            const lens = find(c.frame, o => o instanceof Icon && o.parent === c.frame)
            Tools.tool = "zoom"
            mouseMove(c.frame, 300, 200)
            verify(lens.visible)
            compare([lens.x, lens.y, lens.name], [292, 192, "zoom_in"])
            Tools.setValue("zoom:2", 1)
            compare(lens.name, "zoom_out")
            Tools.tool = "brush"
            verify(!lens.visible)
            Tools.xf = true
            compare(c.input.cursorShape, Qt.SizeAllCursor)
        }

        // ---- rulers --------------------------------------------------------------------------------------------------
        function ticks(r) {  // the numbers of a ruler and where they are, in order
            return all(r, o => o instanceof Text && o.visible && o.text !== "").map(t => ({ t: t.text, at: r.vertical ? t.parent.y : t.parent.x })).sort((a, b) => a.at - b.at)
        }
        function at(r, label) {
            const t = ticks(r).find(x => x.t === label)
            return t ? t.at : NaN
        }
        function test_rulers() {  // 0 at the image's top left, numbers 90 px apart or more, in the unit of Thiết lập
            const w = open(), c = parts(w), d = Docs.current
            const rs = all(c.vp, o => o instanceof Ruler), hr = rs.find(r => !r.vertical), vr = rs.find(r => r.vertical)
            compare([hr.x, hr.y, hr.width, hr.height], [18, 0, 1034, 18])
            compare([vr.x, vr.y, vr.width, vr.height], [0, 18, 18, 750])
            compare([c.frame.x, c.frame.y], [18, 18])
            compare(at(hr, "0"), 242)
            compare(at(hr, "1.000"), 367)
            compare(at(hr, "−1.000"), 117)
            compare(at(vr, "0"), 25)
            compare(at(vr, "5.000"), 650)
            compare(ticks(hr).map(x => x.t), ["−2.000", "−1.000", "0", "1.000", "2.000", "3.000", "4.000", "5.000", "6.000"], "as the artboard")
            keys("Ctrl+1")
            compare(at(hr, "2.200"), 517)
            compare(at(hr, "2.300"), 617)
            d.zoomTo(800)
            compare(at(hr, "2.200"), 517)
            compare(at(hr, "2.220"), 677)
            Prefs.set("rulerUnits", 1)  // inches at 300 ppi: 37,5 px each at 12,5 %, so every 5
            d.zoomTo(12.5)
            compare(at(hr, "0"), 242)
            compare(at(hr, "5"), 430)
            compare(at(hr, "10"), 617)
            d.zoomTo(1600)  // 1 in is 4.800 px: numbers every 0,02 in
            compare(hr.major, 0.02)
            compare(at(hr, "7,34"), Math.round(517 + (7.34 * 300 - 2200) * 16))
            Prefs.set("rulerUnits", 6)  // % of the side
            d.zoomTo(12.5)
            compare(at(hr, "0"), 242)
            compare(at(hr, "20"), 352)
            compare(at(hr, "100"), 792)
            compare(at(vr, "100"), 725)
            // Right click: the units, the one in use ticked
            mouseClick(hr, 100, 9, Qt.RightButton)
            const m = menuOf(hr)
            tryVerify(() => m.opened)
            compare(rows(m), ["Pixels", "Inches", "Centimeters", "Millimeters", "Points", "Picas", "*Percent"])
            pick(w, m, "Centimeters")
            compare(Prefs.value("rulerUnits"), 2)
            tryVerify(() => !m.visible)
            mouseClick(vr, 9, 300, Qt.RightButton)
            tryVerify(() => menuOf(vr).opened)
            pick(w, menuOf(vr), "Pixels")
            compare(Prefs.value("rulerUnits"), 0)
            // The pointer's bright tick
            verify(isNaN(hr.mark) && isNaN(vr.mark))
            mouseMove(c.frame, 300, 200)
            compare([hr.mark, vr.mark], [300, 200])
            const bright = find(hr, o => o instanceof Rectangle && Qt.colorEqual(o.color, Theme.t1))
            verify(bright.visible)
            compare(bright.x, 300)
            // The corner dragged: 0 there; double clicked: back at the top left
            mousePress(c.vp, 9, 9)
            mouseMove(c.vp, 318, 218, -1, Qt.LeftButton)
            mouseRelease(c.vp, 318, 218)
            compare(c.vp.origin, { x: 464, y: 1400 })
            compare(at(hr, "0"), 300)
            compare(at(vr, "0"), 200)
            Docs.select(1)
            compare(at(hr, "0"), View.toCanvas(c.vp.view, c.vp.size, { x: 0, y: 0 }).x | 0, "each document its own")
            Docs.select(0)
            compare(at(hr, "0"), 300)
            mouseDoubleClickSequence(c.vp, 9, 9)
            compare(at(hr, "0"), 242)
            // Ctrl R: no rulers, the frame takes their room, Vừa màn hình fits that
            keys("Ctrl+R")
            verify(!hr.visible && !vr.visible && !Tools.rulers)
            compare([c.frame.x, c.frame.y, c.frame.width, c.frame.height], [0, 0, 1052, 768])
            compare(Docs.viewport, { w: 1052, h: 768 })
            keys("Ctrl+0")
            compare(d.view.zoom * 100, Mock.fitZoom(4400, 5600, 1052, 768))
            keys("Ctrl+R")
            verify(hr.visible && vr.visible)
            compare(Docs.viewport, { w: 1034, h: 750 })
        }

        // ---- the right-click menu ------------------------------------------------------------------------------------
        function test_context_menu() {  // the tool's: Select's, Layer's, View's, Edit's items; Free Transform's
            const w = open(), c = parts(w), d = Docs.current, m = menuOf(c.vp)
            const openAt = () => {
                mouseClick(c.frame, 300, 200, Qt.RightButton)
                tryVerify(() => m.opened)
                waitForRendering(w.contentItem)
            }
            const close = () => {
                keyClick(Qt.Key_Escape)
                tryVerify(() => !m.visible)
            }
            const fill = w.hasDialog("Fill") ? "Fill…" : "!Fill…", sm = w.hasSpace("SelectMask") ? "Select and Mask…" : "!Select and Mask…"  // greyed till their file is there
            openAt()  // Brush
            compare(rows(m), ["!Cắt", "!Sao chép", "Dán", "-", fill, "!Stroke…", "-", "Free Transform", ">Transform"])
            close()
            Tools.tool = "rect"
            openAt()
            compare(rows(m), ["!Bỏ chọn", "Chọn tất cả", "!Chọn lại", "!Đảo vùng chọn", "-", sm, "!Feather…", "!Transform Selection", "!Lưu vùng chọn…", "-",
                "Sao chép sang layer mới", "!Cắt sang layer mới", "-", "Free Transform", fill, "!Stroke…"])
            pick(w, m, "Chọn tất cả")
            compare(d.selection, "all")
            tryVerify(() => !m.visible)
            openAt()
            compare(rows(m).slice(0, 4), ["Bỏ chọn", "Chọn tất cả", "!Chọn lại", "Đảo vùng chọn"])
            compare(rows(m).slice(-1), [w.hasDialog("Stroke") ? "Stroke…" : "!Stroke…"], "with a selection")
            pick(w, m, "Bỏ chọn")
            compare(d.selection, null)
            compare(d.history[d.index].name, "Bỏ chọn")
            Tools.tool = "move"
            openAt()
            compare(rows(m), ["Nhân bản layer…", "Đổi tên layer…", ">Xóa", "-", "Nhóm layer", "!Bỏ nhóm", "Ẩn layer", "-", ">Sắp xếp", "-", "Gộp xuống", "Gộp layer đang hiện",
                "Làm phẳng ảnh", "-", "Free Transform"])
            const n = d.layers.length
            pick(w, m, "Nhóm layer")
            compare(d.layers.length, n + 1)
            Tools.tool = "zoom"
            tryVerify(() => !m.visible)
            openAt()
            compare(rows(m), ["Phóng to", "Thu nhỏ", "Vừa màn hình", "100%", "Lấp đầy màn hình", "-", "!Đặt lại view"])
            pick(w, m, "100%")
            compare(d.view.zoom, 1)
            tryVerify(() => !m.visible)
            d.setView({ angle: 45 })
            openAt()
            pick(w, m, "Đặt lại view")
            compare(d.view.angle, 0)
            tryVerify(() => !m.visible)
            openAt()  // the press that closes it ends there: no Zoom click under it
            mouseClick(c.frame, 600, 300)
            tryVerify(() => !m.visible)
            compare(d.view.zoom, 1)
            Tools.xf = true
            openAt()
            compare(rows(m), ["Scale", "Rotate", "Skew", "Distort", "Perspective", "Warp", "-", "Xoay 180°", "Xoay 90° theo chiều kim đồng hồ", "Xoay 90° ngược chiều kim đồng hồ", "-",
                "Lật ngang", "Lật dọc"])
            pick(w, m, "Lật ngang")
            verify(d.layerFlipH)
        }

        // ---- the tabs ------------------------------------------------------------------------------------------------
        function tabs(w) {  // once the tabs of documents gone are
            wait(0)
            return all(region(w, DocTabs), o => o instanceof AbstractButton && o.Accessible.name === o.text && o.text.indexOf(" @ ") > 0)
        }
        function dontSave(w) {  // Lưu thay đổi (U20) asks about each one not saved: Không lưu to all, then they close
            let ask = null
            tryVerify(() => (ask = w.dialogs.find(x => x instanceof SaveChangesDialog) || null) && ask.opened)
            const no = find(ask.footer, o => o instanceof AbstractButton && o.text === "Không lưu")
            for (let i = 0; i < ask.docs.length; i++)
                mouseClick(no)
            tryVerify(() => !w.dialogs.length)  // faded out: a click is the window's again
        }
        function test_tabs() {  // the artboard's labels; a click picks; the dot becomes ×; Đóng, Đóng tab khác
            const w = open(), bar = region(w, DocTabs)
            let ts = tabs(w)
            compare(ts.map(t => t.text), ["chân dung.psd @ 12,5% (Dodge & Burn, RGB/16)", "san-pham.webp @ 55,22% (Background, RGB/8)", "hoang-hon.tif @ 55,22% (Background, RGB/8)"])
            compare(ts.map(t => [t.x, t.height]), [[0, 27], [ts[0].width, 27], [ts[0].width + ts[1].width, 27]])
            verify(ts[0].current && !ts[1].current)
            compare(ts[0].width, ts[0].natural)
            const d = Docs.current
            Tools.quickMask = true
            compare(ts[0].text, "chân dung.psd @ 12,5% (Quick Mask/16)")
            compare(ts[1].text, "san-pham.webp @ 55,22% (Background, RGB/8)", "the current document's only")
            Tools.quickMask = false
            d.run({ op: "selectLayers", layers: [d.layers.find(l => l.props.name === "Curves 1").id] })
            d.setEditMask(true)
            compare(ts[0].text, "chân dung.psd @ 12,5% (Curves 1, Layer Mask/16)")
            d.run({ op: "setMode", mode: "Gray" })
            d.setEditMask(false)
            compare(ts[0].text, "chân dung.psd @ 12,5% (Curves 1, Gray/16)")
            mouseClick(ts[1])
            compare(Docs.index, 1)
            verify(ts[1].current && !ts[0].current)
            // Unsaved: the dot, a close button under the pointer
            const slot = t => {
                const dot = find(t, o => o instanceof Rectangle && o.width === 6), x = find(t, o => o instanceof AbstractButton)
                return [dot.visible, x.visible]
            }
            mouseMove(bar, 1040, 14)
            compare(slot(ts[2]), [true, false])
            compare(slot(ts[1]), [false, true])
            mouseMove(ts[2], 20, 14)
            compare(slot(ts[2]), [false, true])
            const x2 = find(ts[2], o => o instanceof AbstractButton)
            mouseMove(x2, 9, 9)
            compare(slot(ts[2]), [false, true], "on the button still")
            mouseClick(x2)
            dontSave(w)
            compare(Docs.docs.map(x => x.file), ["chân dung.psd", "san-pham.webp"])
            // Right click
            ts = tabs(w)
            mouseClick(ts[0], 30, 14, Qt.RightButton)
            const m = menuOf(bar)
            tryVerify(() => m.opened)
            compare(rows(m), ["Đóng", "Đóng tab khác"])
            pick(w, m, "Đóng")
            dontSave(w)
            compare(Docs.docs.map(x => x.file), ["san-pham.webp"])
            tryVerify(() => !m.visible)
            Docs.reset()
            Docs.select(2)
            ts = tabs(w)
            mouseClick(ts[1], 30, 14, Qt.RightButton)
            tryVerify(() => m.opened)
            pick(w, m, "Đóng tab khác")
            dontSave(w)
            compare(Docs.docs.map(x => x.file), ["san-pham.webp"])
            compare(Docs.current.file, "san-pham.webp")
            tryVerify(() => !m.visible)
            mouseClick(tabs(w)[0], 30, 14, Qt.RightButton)
            tryVerify(() => m.opened)
            compare(rows(m), ["Đóng", "!Đóng tab khác"])
            keyClick(Qt.Key_Escape)
        }
        function test_tabs_room() {  // short of room: all shrunk alike, then +N with the rest, the current one shown
            const w = open(), bar = region(w, DocTabs)
            w.width = 1280
            waitForRendering(w.contentItem)
            compare(bar.width, 892)
            let ts = tabs(w)
            const nat = ts.map(t => t.natural), sum = nat[0] + nat[1] + nat[2]
            verify(sum > 892, sum)
            compare(ts.filter(t => t.visible).length, 3)
            for (const t of ts)
                verify(t.width < t.natural && t.width >= bar.minTab, t.width)
            compare(ts[2].x + ts[2].width, 892)
            const more = find(bar, o => o instanceof AbstractButton && o.text.startsWith("+"))
            verify(!more.visible)
            for (const r of Mock.RECENT)
                Docs.open(r.file)
            compare(Docs.docs.length, 8)
            Docs.select(0)
            waitForRendering(w.contentItem)
            ts = tabs(w)
            compare(bar.fits, 6)  // (892 − 40) / 140
            compare(ts.filter(t => t.visible).length, 6)
            verify(more.visible)
            compare(more.text, "+2")
            mouseClick(more)
            const m = menuOf(bar)
            tryVerify(() => m.opened)
            compare(rows(m), Docs.docs.slice(6).map(x => x.file))
            pick(w, m, Docs.docs[7].file)
            compare(Docs.index, 7)
            tryVerify(() => !m.visible)
            verify(ts[7].visible && ts[7].current && !ts[5].visible, "the current one takes the last place")
            compare(more.text, "+2")
            compare(ts.filter(t => t.visible).length, 6)
            w.width = 1440
            waitForRendering(w.contentItem)
            compare(bar.fits, 7)  // 1.052 px: 7 of 140 beside +N
            compare(more.text, "+1")
        }
        function test_tabs_keys() {  // Tab reaches the tabs and their close buttons, Enter picks
            const w = open(), ts = tabs(w)
            ts[1].forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            compare(Docs.index, 1)
            keyClick(Qt.Key_Tab)
            verify(find(ts[1], o => o instanceof AbstractButton).activeFocus, "its close button next")
            keyClick(Qt.Key_Return)
            compare(Docs.docs.length, 2)
        }
    }
}
