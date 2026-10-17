import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty

// U12: the History, Navigator, Histogram and Info panels, opened beside the icon column.
Item {
    id: root
    width: 200
    height: 100
    Component { id: mainC; Main {} }

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
        name: "U12"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanupTestCase() { init() }

        function open() {
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        // The side panel of that name opened, as its icon does: the one panel of its type.
        function side(w, name, type) {
            Tools.sidePanel = name
            waitForRendering(w.contentItem)
            const m = all(w.contentItem, o => o instanceof type)
            compare(m.length, 1, name)
            verify(m[0].visible, name)
            return m[0]
        }
        function box(w, o) {
            const p = o.mapToItem(w.contentItem, 0, 0)
            return [p.x, p.y, o.width, o.height]
        }
        function click(w, item) {
            verify(item, "nothing to click")
            mouseClick(item)
            waitForRendering(w.contentItem)
        }
        function button(p, icon) { return find(p, o => o instanceof ToolToggle && o.icon.name === icon) }

        // ---- History -----------------------------------------------------------------------------------------------
        function steps(p) { return all(p, o => o instanceof AbstractButton && o.height === 26) }
        function snaps(p) { return all(p, o => o instanceof AbstractButton && o.height === 38) }
        function lit(row) { return Qt.colorEqual(row.background.color, Theme.acc2) }

        // The portrait's snapshots and ten steps as the prototype's, the last one lit; 385 tall as U8 left room for.
        function test_history_rows() {
            const w = open(), p = side(w, "History", HistoryPanel), d = Docs.current
            compare(p.implicitHeight, 385)
            const s = snaps(p), h = steps(p)
            compare(s.map(r => r.Accessible.name), ["chân dung.psd", "Trước khi Liquify"])
            compare(h.map(r => r.Accessible.name), d.history.map(x => x.name))
            compare(h.length, 10)
            compare(box(w, s[0]).slice(1), [86 + 30 + 4, 280, 38])
            compare(box(w, h[0]).slice(1), [86 + 30 + 89, 280, 26])
            compare(box(w, button(p, "delete"))[1], 86 + 30 + 385 - 28)
            compare(h.map(lit), [false, false, false, false, false, false, false, false, false, true])
            compare(h.filter(r => Qt.colorEqual(r.ink, Theme.t2)).length, 9)
            compare(s.map(lit), [false, false])
            const icons = all(p, o => o instanceof Icon && o.size === 16).map(i => i.name)
            compare(icons, d.history.map(x => x.icon))
        }
        // A click on a step takes the document back (or on) to it: the steps after it dimmed; on a snapshot, to its
        // state, no step lit; undo and redo move the light.
        function test_history_jump() {
            const w = open(), p = side(w, "History", HistoryPanel), d = Docs.current
            click(w, steps(p)[3])
            compare([d.index, d.snapshot], [3, -1])
            verify(d.state === d.history[3].state)
            let h = steps(p)
            compare(h.map(lit), [false, false, false, true, false, false, false, false, false, false])
            compare(h.map(r => Qt.colorEqual(r.ink, Theme.t4)), [false, false, false, false, true, true, true, true, true, true])
            verify(Qt.colorEqual(h[3].ink, Theme.t1) && Qt.colorEqual(h[2].ink, Theme.t2))
            click(w, steps(p)[9])
            compare(d.index, 9)
            verify(d.state === d.history[9].state)
            click(w, snaps(p)[1])
            compare([d.index, d.snapshot], [3, 1])
            verify(d.state === d.snapshots[1].state)
            compare(snaps(p).map(lit), [false, true])
            compare(steps(p).filter(lit).length, 0)
            compare(steps(p).filter(r => Qt.colorEqual(r.ink, Theme.t4)).length, 6)
            click(w, snaps(p)[0])
            compare([d.index, d.snapshot], [0, 0])
            Docs.exec("@redo")
            waitForRendering(w.contentItem)
            compare([d.index, d.snapshot], [1, -1])
            compare(steps(p).map(lit).indexOf(true), 1)
            compare(snaps(p).filter(lit).length, 0)
        }
        // The camera takes a snapshot of the state shown: a row more.
        function test_history_snapshot() {
            const w = open(), p = side(w, "History", HistoryPanel), d = Docs.current
            click(w, steps(p)[5])
            click(w, button(p, "photo_camera"))
            compare(d.snapshots.length, 3)
            compare([d.snapshots[2].name, d.snapshots[2].at], ["Snapshot 2", 5])
            verify(d.snapshots[2].state === d.history[5].state)
            compare(snaps(p).map(r => r.Accessible.name), ["chân dung.psd", "Trước khi Liquify", "Snapshot 2"])
            compare(p.implicitHeight, 385 + 38)
            click(w, steps(p)[9])
            click(w, snaps(p)[2])
            compare([d.index, d.snapshot], [5, 2])
        }
        // The bin deletes the step shown and those after it; on the first step it cannot.
        function test_history_delete() {
            const w = open(), p = side(w, "History", HistoryPanel), d = Docs.current
            click(w, steps(p)[6])
            click(w, button(p, "delete"))
            compare([d.history.length, d.index], [6, 5])
            compare(steps(p).length, 6)
            compare(steps(p).map(lit).indexOf(true), 5)
            compare(p.implicitHeight, 385 - 4 * 26)
            click(w, steps(p)[0])
            verify(!button(p, "delete").enabled)
            mouseClick(button(p, "delete"))
            compare([d.history.length, d.index], [6, 0])
            const m = p.menu
            compare(m.map(x => x.l), ["Tài liệu mới từ bước này", "Tạo snapshot", "Xóa bước này và các bước sau"])
            verify(m[2].disabled)
            click(w, steps(p)[2])
            verify(!p.menu[2].disabled)
            p.menu[2].act()
            compare([d.history.length, d.index], [2, 1])
            p.menu[1].act()
            compare(d.snapshots.length, 3)
        }
        // A new document from the state shown: a tab of its own, History showing it.
        function test_history_new_doc() {
            const w = open(), p = side(w, "History", HistoryPanel), d = Docs.current
            click(w, steps(p)[4])
            click(w, button(p, "note_add"))
            compare(Docs.docs.length, 4)
            const c = Docs.current
            verify(c !== d)
            compare(c.file, "chân dung bản sao.psd")
            verify(c.state === d.history[4].state)
            compare(steps(p).map(r => r.Accessible.name), ["Nhân bản"])
            compare(snaps(p).map(r => r.Accessible.name), ["Bản sao"])
            Docs.select(1)  // san-pham.webp, a step on from its snapshot
            const e = Docs.current
            verify(e.run({ op: "newLayer" }))
            click(w, snaps(p)[0])
            p.menu[0].act()
            compare(Docs.docs.length, 5)
            compare(Docs.current.file, "san-pham bản sao.webp")
            verify(Docs.current.state === e.snapshots[0].state, "from a snapshot, its state")
            verify(Docs.current.state !== e.history[1].state)
        }
        // Tab reaches the rows and the foot, Enter presses them.
        function test_history_keys() {
            const w = open(), p = side(w, "History", HistoryPanel), d = Docs.current
            steps(p)[1].forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            verify(steps(p)[2].activeFocus)
            keyClick(Qt.Key_Return)
            compare(d.index, 2)
            snaps(p)[1].forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Enter)
            compare(d.snapshot, 1)
            button(p, "photo_camera").forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            compare(d.snapshots.length, 3)
        }

        // ---- Histogram ---------------------------------------------------------------------------------------------
        function sum(a) { return a.reduce((x, y) => x + y, 0) }

        function test_histogram_tally() {
            const w = open(), p = side(w, "Histogram", HistogramPanel)
            const h = p.tally([10, 20, 30, 255, 10, 200, 30, 255, 255, 0, 128, 0])
            compare(h.map(c => c.length), [256, 256, 256])
            compare([h[0][10], h[0][255], h[1][20], h[1][200], h[1][0], h[2][30], h[2][128]], [2, 1, 1, 1, 1, 2, 1])
            compare(h.map(sum), [3, 3, 3])
        }
        // A picture known beforehand: a document of one colour has all its 160 × 80 pixels in one bin of each channel.
        function test_histogram_known() {
            const w = open()
            verify(Docs.create({ name: "một màu", w: 200, h: 100, background: "custom", color: "#336699" }))
            const p = side(w, "Histogram", HistogramPanel)
            tryVerify(() => !!p.counts)
            const n = 160 * 80
            compare([p.counts[0][0x33], p.counts[1][0x66], p.counts[2][0x99]], [n, n, n])
            compare(p.counts.map(sum), [n, n, n])
            verify(find(p, o => o instanceof Histogram).counts === p.counts)
        }
        // The portrait's photo over its 4.400 × 5.600 canvas, 160 × 204: spread over the bins; the refresh button counts
        // again; another document, its photo; a transparent one, none.
        function test_histogram_photo() {
            const w = open(), p = side(w, "Histogram", HistogramPanel)
            compare(p.implicitHeight, 142)
            tryVerify(() => !!p.counts)
            compare(p.counts.map(sum), [160 * 204, 160 * 204, 160 * 204])
            for (const c of p.counts)
                verify(c.filter(x => x > 0).length > 100)
            const before = p.counts
            click(w, button(p, "refresh"))
            tryVerify(() => p.counts !== before)
            compare(p.counts, before)
            Docs.select(1)  // san-pham.webp, 1.800 × 1.200
            tryVerify(() => p.counts && sum(p.counts[0]) === 160 * 107)
            verify(JSON.stringify(p.counts) !== JSON.stringify(before))
            verify(Docs.create({ name: "trong", w: 100, h: 100, background: "transparent" }))
            tryVerify(() => p.counts === null)
        }

        // ---- Navigator ---------------------------------------------------------------------------------------------
        function near(a, b, tol) { return Math.abs(a - b) <= (tol || 0.01) }
        function nearView(v, x, y) { return near(v.x, x) && near(v.y, y) }

        // The portrait 157 × 200 in the middle of the 212 high band; the frame is the canvas's view of it: 1.034 × 750
        // device px of the frame at the zoom, about the view's centre, turned as it is.
        function test_navigator_frame() {
            const w = open(), p = side(w, "Navigator", NavigatorPanel), d = Docs.current
            compare(p.implicitHeight, 252)
            const t = find(p, o => o.objectName === "thumb"), f = find(p, o => o.objectName === "frame")
            const b = box(w, t)
            verify(near(b[0], 812 + 61.5, 0.5), b[0])
            compare(b.slice(1), [86 + 30 + 6, 157, 200])
            compare(Docs.viewport, { w: 1034, h: 750 })
            const k = 157 / 4400
            d.setView({ zoom: 1 })
            verify(near(f.width, 1034 * k) && near(f.height, 750 * k), f.width + " × " + f.height)
            verify(near(f.x + f.width / 2, 2200 * k) && near(f.y + f.height / 2, 2800 * k))
            d.setView({ x: 1000, y: 4000, zoom: 0.5, angle: 30 })
            verify(near(f.width, 2068 * k) && near(f.height, 1500 * k))
            verify(near(f.x + f.width / 2, 1000 * k) && near(f.y + f.height / 2, 4000 * k))
            compare(f.rotation, -30)
            d.setView({ x: 100, y: 100, angle: 0 })  // shown as the Viewport shows it: constrained
            verify(nearView(p.view, 1034, 750), JSON.stringify(p.view))
            verify(near(f.x, 0) && near(f.y, 0))
            const mc = find(t, o => o instanceof MockCanvas)
            verify(mc && mc.doc === d && mc.showPhoto)
            compare([mc.view.zoom, mc.view.angle], [k, 0])
        }
        // Dragging the frame pans the canvas by as much of the document, no further than its edge; a press off the
        // frame centres the view there first.
        function test_navigator_drag() {
            const w = open(), p = side(w, "Navigator", NavigatorPanel), d = Docs.current
            const t = find(p, o => o.objectName === "thumb"), k = 157 / 4400
            d.setView({ zoom: 1 })
            mouseDrag(t, 2200 * k, 2800 * k, 20, 10)
            verify(nearView(d.view, 2200 + 20 / k, 2800 + 10 / k), JSON.stringify(d.view))
            mouseDrag(t, (2200 + 20 / k) * k, (2800 + 10 / k) * k, 150, -10)
            verify(nearView(d.view, 4400 - 517, 2800), JSON.stringify(d.view))
            mouseClick(t, 100, 30)
            verify(nearView(d.view, 100 / k, 30 / k), JSON.stringify(d.view))
            mouseClick(t, 2, 2)
            verify(nearView(d.view, 517, 375), JSON.stringify(d.view))
            d.setView({ x: 2200, y: 2800, angle: 90 })
            mouseDrag(t, 2200 * k, 2800 * k + 16, -20, 0)  // inside the turned frame, not the upright one: no jump
            verify(nearView(d.view, 2200 - 20 / k, 2800), JSON.stringify(d.view))
        }
        // The zoom box takes what is typed (Enter), gives it back on Esc; − and + step as View › Thu nhỏ, Phóng to; the
        // slider runs 1 % to 3.200 % on a log scale.
        function test_navigator_zoom() {
            const w = open(), p = side(w, "Navigator", NavigatorPanel), d = Docs.current
            const field = find(p, o => o instanceof TextField), slider = find(p, o => o instanceof SliderField)
            compare(field.text, "12,5%")
            verify(near(slider.value, Math.log(12.5) / Math.log(3200) * 1000, 1))
            field.forceActiveFocus()
            field.selectAll()
            keyClick(Qt.Key_5)
            keyClick(Qt.Key_0)
            keyClick(Qt.Key_Return)
            compare(d.view.zoom, 0.5)
            compare(field.text, "50%")
            field.selectAll()
            keyClick(Qt.Key_7)
            keyClick(Qt.Key_Escape)
            compare([field.text, d.view.zoom], ["50%", 0.5])
            mouseClick(button(p, "zoom_in"))
            compare(d.view.zoom, 0.6667)
            mouseClick(button(p, "zoom_out"))
            mouseClick(button(p, "zoom_out"))
            compare(d.view.zoom, 0.3333)
            mouseClick(slider, slider.width * 0.75, 7)
            verify(near(d.view.zoom * 100, Math.exp(0.75 * Math.log(3200)), 0.6), d.view.zoom)
            verify(near(slider.value, 750, 1), slider.value)
            mouseClick(slider, 0, 7)
            compare(d.view.zoom, 0.01)
        }

        // ---- Info ------------------------------------------------------------------------------------------------------
        function texts(p) { return all(p, o => o instanceof Text && !(o instanceof Icon)).map(o => o.text) }

        // The foreground colour read as RGB and HSB; X, Y the pixel under the pointer on the canvas, "—" off it; W, H only
        // in Free Transform; the document's memory; the tool's hint.
        function test_info() {
            const w = open(), p = side(w, "Info", InfoPanel)
            const vp = find(w.contentItem, o => o instanceof Viewport), frame = find(vp, o => o instanceof MockCanvas).parent
            Tools.setFg("#e9c7b3")
            mouseMove(vp, 9, 9)  // the rulers' corner: off the canvas
            const hint = "Brush: nhấp và kéo để vẽ. Shift nhấp để vẽ đường thẳng; giữ Alt để lấy màu."
            compare(texts(p), ["R", "233", "G", "199", "B", "179", "H", "22°", "S", "23%", "B", "91%", "X", "—", "Y", "—", "W", "—", "H", "—",
                "Tài liệu: ", "148 MB / 1,02 GB", hint])
            mouseMove(frame, 300, 200)
            compare(texts(p).slice(12, 16), ["X", "464", "Y", "1.400"])  // 2.200 + (300 − 517) × 8, 2.800 + (200 − 375) × 8
            mouseMove(frame, 5, 5)
            compare(texts(p).slice(12, 16), ["X", "−1.896", "Y", "−160"])  // off the image, as Photoshop's
            mouseMove(vp, 9, 9)
            compare(texts(p).slice(12, 16), ["X", "—", "Y", "—"])
            Tools.xf = true
            compare(texts(p).slice(16, 20), ["W", "4.400", "H", "5.600"])
            Tools.setTool("hand")
            compare(texts(p).pop(), "Hand: nhấp lên canvas để dùng.")
            Tools.setFg("#808080")
            compare(texts(p).slice(0, 12), ["R", "128", "G", "128", "B", "128", "H", "0°", "S", "0%", "B", "50%"])
            Tools.setFg("#2050ff")
            compare(texts(p).slice(6, 12), ["H", "227°", "S", "87%", "B", "100%"])
            Docs.select(1)
            compare(texts(p)[21], "6,48 MB / 6,48 MB")
        }
    }
}
