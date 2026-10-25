import QtQuick
import QtQuick.Templates as T
import QtTest
import PhotoBeauty

// I30: the pen on Windows, through Pen's test pens handed over as a platform hands a tablet's (QWindowSystemInterface;
// offscreen, the mouse event Windows' Qt makes of a tablet event nothing took handed over with it). Thiết lập › Bút's
// Windows Ink or Wintab as main.cpp reads it from Prefs' JSON, with its note; the eraser end turning the tool to the
// Eraser and back, as Photoshop, each end keeping its tool, on the mock document and a real one; the pen's pressure,
// tilt and rotation into PenEvent, as the stroke's log (PB_RECORD_STROKES) has them; the first dab's pressure when the pen
// comes down with none; the brush pointer following the pen as it hovers; Wintab beside Windows Ink: Windows Ink's
// events of the pen Wintab has and the driver's mouse of it kept out (offscreen, as Pen.api "wintab" filters there);
// on Windows' own windows (QT_QPA_PLATFORM=windows) Windows' pen feedback off as GetWindowFeedbackSetting() reads it.
Item {
    id: root
    width: 400
    height: 300
    Component { id: mainC; Main {} }
    Component {
        id: dcC
        DocCanvas {
            property var steps: []  // painted()'s
            onPainted: step => steps.push(step)
        }
    }
    Component { id: settingsC; SettingsDialog {} }
    Component {
        id: hostC
        Window {  // the window as Thiết lập sees Main (tst_u21's)
            function openDialog(name, params) {}
            function hasDialog(name) { return false }
            width: 1440
            height: 900
            visible: true
            color: Theme.bg
        }
    }

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

    TestCase {
        name: "I30"
        when: windowShown
        property string file: ""  // 800 x 600, half see-through: a layer of its own, not a Background (A40)
        property string apiAtStart: ""
        readonly property bool windows: Qt.platform.pluginName === "windows"

        function initTestCase() {
            file = fixtures.image("i30.png", "png", 800, 600, 8, false, true)
            verify(file, "the test file")
            apiAtStart = Pen.api
        }
        function cleanup() {
            Pen.api = apiAtStart
            Pen.firstPressure = true
            if (Pen.eraser)  // the tip again, for the files after
                Pen.send(root, "near", Qt.point(1, 1), {})
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }

        // ---- helpers ----------------------------------------------------------------------------------------------------
        // DocCanvas on the test file at 100 % (the window shows its 200..600 x 150..450), Brush: 30 px, hard, blue, the
        // pen's pressure on its size.
        function open(props) {
            const c = createTemporaryObject(dcC, root, Object.assign({ width: root.width, height: root.height, source: file,
                                                                       view: { x: 400, y: 300, zoom: 1, angle: 0 }, tool: "brush",
                                                                       brush: { size: 30, hardness: 1, color: "#0000ff", pressureSize: true } }, props || {}))
            tryVerify(() => c.docSize.width > 0 || c.error !== "", 20000, "open")
            compare(c.error, "")
            tryVerify(() => c.frames > 0 && !c.shown.pending, 20000, "a frame")
            return c
        }
        // Main on the mock document, or (real) on the test file opened for real: { w, dc, vp, cursor } (tst_n3's).
        function openMain(real) {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
            const w = createTemporaryObject(mainC, root)  // no startState: its start() would reset Tools
            w.requestActivate()
            tryVerify(() => w.active)
            const m = { w: w, dc: region(w, DocCanvas), vp: region(w, Viewport), cursor: region(w, BrushCursor) }
            if (real) {
                const d = Docs.openFile(file)
                verify(d && d.real, "opened for real")
                tryVerify(() => !d.model.loading, 20000, "open")
                compare(d.model.error, "")
                tryVerify(() => m.dc.docSize.width > 0 && m.dc.frames > 0 && !m.dc.shown.pending, 20000, "a frame")
            } else {
                Docs.select(0)
            }
            waitForRendering(w.contentItem)
            return m
        }
        function region(w, type) { return root.find(w.contentItem, o => o instanceof type) }
        function last() { const d = Docs.current; return d.history[d.index].name }
        // A stroke of the test pen through document points [x, y, with], `with` for all of them besides; until its step.
        function stroke(c, pts, with_) {
            pts.forEach((p, i) => Pen.send(c, i === 0 ? "press" : "move", c.toCanvas(Qt.point(p[0], p[1])), Object.assign({}, with_ || {}, p[2] || {})))
            const e = pts[pts.length - 1]
            Pen.send(c, "release", c.toCanvas(Qt.point(e[0], e[1])), with_ || {})
            c.wait()
        }
        function logged(c, dir, n) {  // the log of the stroke in dir, once written (after pen up, in the background)
            tryVerify(() => c.strokeLog(dir).events === n, 20000, "the stroke's log in " + dir)
            return c.strokeLog(dir)
        }

        // ---- Thiết lập › Bút --------------------------------------------------------------------------------------------
        // Windows Ink or Wintab as main.cpp's usePen() takes it from Prefs' JSON as the last start kept it; the dialog's
        // segments write it; its note says from the next start; tests start without usePen(): Windows Ink on Windows'
        // own windows, Qt's own way elsewhere.
        function test_prefs() {
            compare(Pen.tabletApi(JSON.stringify({ tabletApi: 1 })), "wintab")
            for (const j of [{ tabletApi: 0 }, {}, { tabletApi: "1" }, { tabletApi: 2 }, { tabletApi: true }, { language: 1 }])
                compare(Pen.tabletApi(JSON.stringify(j)), "ink", JSON.stringify(j))
            compare(Pen.tabletApi("not JSON"), "ink")
            compare(Pen.tabletApi(""), "ink")
            compare(Pen.asked, "", "usePen() is pb-ui's")
            compare(Pen.api, windows ? "ink" : "")

            const host = createTemporaryObject(hostC, root)
            const d = createTemporaryObject(settingsC, host.contentItem, { params: { page: "pen" } })
            d.open()
            tryCompare(d, "opened", true)
            const row = root.find(d.contentItem, o => o.modelData && o.r !== undefined && o.modelData.key === "tabletApi")
            const seg = root.find(row, o => o instanceof Segmented)
            const button = text => root.find(seg, o => o instanceof T.AbstractButton && o.text === text && o.visible)
            mouseClick(button("Wintab"))
            compare(Prefs.values.tabletApi, 1)
            compare(Pen.tabletApi(JSON.stringify(Prefs.values)), "wintab", "the JSON Prefs keeps: Wintab")
            mouseClick(button("Windows Ink"))
            compare(Pen.tabletApi(JSON.stringify(Prefs.values)), "ink")
            const note = "Wintab cho bảng vẽ Wacom đời cũ. Có hiệu lực từ lần mở app sau."
            const t = root.find(d.contentItem, o => o instanceof Text && o.visible && o.text === note)
            verify(t, "the note, as Ngôn ngữ's")
            compare(t.color, Theme.t3)
            compare(Language.translate("en", "Prefs", note), "Wintab for older Wacom tablets. Takes effect the next time the app opens.")
            d.close()
        }

        // Pen.api set: Qt's Wintab opened on Windows' own windows when there is a Wintab driver (Windows Ink again when
        // there is none, as on the test laptop); elsewhere what is kept out alone (test_wintab).
        function test_api() {
            Pen.api = "wintab"
            if (windows) {
                verify(Pen.api === "wintab" || Pen.api === "ink", Pen.api)
                console.log("I30: Wintab " + (Pen.api === "wintab" ? "opened" : "not here (no Wintab driver): Windows Ink"))
            } else {
                compare(Pen.api, "wintab")
            }
            Pen.api = "ink"
            compare(Pen.api, "ink")
            if (!windows)
                compare(Pen.feedback(root.Window.window), {}, "Windows' pen feedback: on its own windows alone")
        }

        // Windows' pen feedback off on every window as it is made: the test's, Main's.
        function test_feedback() {
            if (!windows)
                skip("Windows' own windows (QT_QPA_PLATFORM=windows)")
            const off = { barrel: false, tap: false, doubleTap: false, pressAndHold: false, rightTap: false }
            compare(Pen.feedback(root.Window.window), off, "the test's window")
            const m = openMain(false)
            compare(Pen.feedback(m.w), off, "Main's")
        }

        // ---- the canvas -------------------------------------------------------------------------------------------------
        // The pen's pressure, tilt and rotation of each tablet event into the stroke's PenEvents, as its log has them.
        function test_values() {
            const c = open(), dir = fixtures.folder() + "/i30-values"
            const pts = [[300, 250, { pressure: 0.2, tiltX: 10, tiltY: -20, rotation: 30 }], [310, 255, { pressure: 0.45, tiltX: 25, tiltY: -5, rotation: 75 }],
                         [320, 260, { pressure: 0.7, tiltX: -35, tiltY: 15, rotation: -100 }], [330, 265, { pressure: 0.9, tiltX: 55, tiltY: 45, rotation: 170 }],
                         [340, 270, { pressure: 1, tiltX: -60, tiltY: -60, rotation: -179.5 }]]
            c.record(dir)
            stroke(c, pts)
            c.record("")
            compare(c.steps, ["Brush"])
            const L = logged(c, dir, pts.length)
            compare(L.erase, false)
            L.points.forEach((q, i) => {
                const p = pts[i]
                fuzzyCompare(q[1], p[0], 0.01, "x of " + i)
                fuzzyCompare(q[2], p[1], 0.01, "y of " + i)
                fuzzyCompare(q[3], p[2].pressure, 1e-6, "pressure of " + i)
                fuzzyCompare(q[4], p[2].tiltX, 1e-6, "tiltX of " + i)
                fuzzyCompare(q[5], p[2].tiltY, 1e-6, "tiltY of " + i)
                fuzzyCompare(q[6], p[2].rotation, 1e-6, "rotation of " + i)
            })
            verify(L.points[4][0] > L.points[0][0], "times in order")
            // the eraser end's the same, into the Eraser's stroke
            c.tool = "eraser"
            const dir2 = fixtures.folder() + "/i30-values-eraser"
            c.record(dir2)
            stroke(c, pts.map(p => [p[0], p[1] + 100, p[2]]), { eraser: true })
            c.record("")
            compare(c.steps, ["Brush", "Eraser"])
            const E = logged(c, dir2, pts.length)
            compare(E.erase, true)
            E.points.forEach((q, i) => compare([q[3], q[4], q[5], q[6]].map(v => Math.round(v * 1000) / 1000),
                                               [pts[i][2].pressure, pts[i][2].tiltX, pts[i][2].tiltY, pts[i][2].rotation], "the eraser end's " + i))
        }

        // A pen down without pressure (Windows Ink's touch-down, some drivers'): its first dab takes the next event's,
        // unless Pen.firstPressure is off; a press with pressure, the mouse's (1), a tap with nothing after it: as they come.
        function test_first_pressure() {
            const c = open()
            for (const on of [true, false]) {
                Pen.firstPressure = on
                const dir = fixtures.folder() + "/i30-first-" + on, y = on ? 330 : 360
                c.record(dir)
                stroke(c, [[300, y, { pressure: 0 }], [305, y, { pressure: 0.6 }], [310, y, { pressure: 0.8 }]])
                c.record("")
                const L = logged(c, dir, 3)
                compare(L.points.map(q => Math.round(q[3] * 100) / 100), on ? [0.6, 0.6, 0.8] : [0, 0.6, 0.8], "first pressure " + (on ? "on" : "off"))
                fuzzyCompare(L.points[0][1], 300, 0.01, "the first dab where the pen came down")
            }
            Pen.firstPressure = true
            const cases = [["a press with pressure", [[400, 390, { pressure: 0.3 }], [410, 390, { pressure: 0.9 }]], [0.3, 0.9]],
                           ["a tap without", [[450, 410, { pressure: 0 }]], [0]]]
            for (const [what, pts, want] of cases) {
                const dir = fixtures.folder() + "/i30-first-" + want.length
                c.record(dir)
                stroke(c, pts)
                c.record("")
                compare(logged(c, dir, want.length).points.map(q => Math.round(q[3] * 100) / 100), want, what)
            }
            const dir = fixtures.folder() + "/i30-first-mouse"
            c.record(dir)
            const a = c.toCanvas(Qt.point(500, 430)), b = c.toCanvas(Qt.point(520, 430))
            mousePress(c, a.x, a.y)
            mouseMove(c, b.x, b.y)
            mouseRelease(c, b.x, b.y)
            c.wait()
            c.record("")
            compare(logged(c, dir, 2).points.map(q => q[3]), [1, 1], "the mouse's")
            compare(c.steps.length, 5)
        }

        // ---- the eraser end ---------------------------------------------------------------------------------------------
        // As Photoshop: the eraser end near the canvas takes the Eraser, the tip the tool before; each end keeps the one
        // last used with it; it going away changes nothing; none during Free Transform.
        function test_eraser_end() {
            const m = openMain(false), at = Qt.point(m.vp.width / 2, m.vp.height / 2)
            compare(Tools.tool, "brush")
            Pen.send(m.vp, "near", at, { eraser: true })
            compare(Pen.eraser, true)
            compare(Tools.tool, "eraser", "the eraser end near the canvas: the Eraser")
            compare(Tools.top.eraser, "eraser", "its group shows it")
            Pen.send(m.vp, "hover", at, { eraser: true })
            Pen.send(m.vp, "away", at, { eraser: true })
            compare(Tools.tool, "eraser", "gone: the tool stays")
            Pen.send(m.vp, "near", at, {})
            compare(Pen.eraser, false)
            compare(Tools.tool, "brush", "the tip again: the tool before")

            Tools.setTool("lasso", "lasso")
            Pen.send(m.vp, "near", at, { eraser: true })
            compare(Tools.tool, "eraser")
            Tools.setTool("dodge", "dodge")
            Pen.send(m.vp, "near", at, {})
            compare(Tools.tool, "lasso", "the tip's own")
            Pen.send(m.vp, "hover", at, { eraser: true })  // an event of the eraser end, no proximity first
            compare(Tools.tool, "dodge", "the eraser end's own")
            Pen.send(m.vp, "hover", at, {})
            compare(Tools.tool, "lasso")

            Tools.xf = true
            Pen.send(m.vp, "near", at, { eraser: true })
            compare([Tools.tool, Tools.xf], ["lasso", true], "Free Transform: nothing turned")
            Tools.xf = false
            Pen.send(m.vp, "near", at, {})
            compare(Tools.tool, "lasso", "the tip: what it had")
            Pen.send(m.vp, "near", at, { eraser: true })
            compare(Tools.tool, "dodge")
            Pen.send(m.vp, "near", at, {})
            compare(Tools.tool, "lasso")

            // a space's canvas: its tools are its own, nothing turned there; Main's again after it
            const s = m.w.openSpace("SelectMask", {})
            verify(s, "Select and Mask")
            tryVerify(() => !!root.find(s, o => o instanceof Viewport) && Tools.tool !== "lasso", 5000, "its canvas, its tool")
            const own = Tools.tool, svp = root.find(s, o => o instanceof Viewport), mid = Qt.point(svp.width / 2, svp.height / 2)
            Pen.send(svp, "near", mid, { eraser: true })
            compare(Tools.tool, own, "the eraser end in a space: its tool kept")
            Pen.send(svp, "near", mid, {})
            s.finished(false)
            tryCompare(Tools, "tool", "lasso")
            Pen.send(m.vp, "near", at, { eraser: true })
            compare(Tools.tool, "dodge", "Main's canvas again")
        }

        // On a real document: the eraser end's stroke is the Eraser's step, the tip's again the Brush's; a tool the real
        // document locks gives the Brush.
        function test_eraser_end_real() {
            const m = openMain(true), steps = Docs.current.history.length
            compare(Tools.tool, "brush")
            const was = m.dc.composite(0, 230, 210)
            verify(was[3] > 60000, "opaque there: " + was)
            Pen.send(m.dc, "near", m.dc.toCanvas(Qt.point(200, 200)), { eraser: true })
            compare([Tools.tool, m.dc.tool], ["eraser", "eraser"])
            stroke(m.dc, [[200, 200], [230, 210], [260, 220]], { eraser: true })
            tryVerify(() => Docs.current.history.length === steps + 1, 5000, "a step")
            compare(last(), "Eraser")
            verify(m.dc.composite(0, 230, 210)[3] < was[3], "erased: " + m.dc.composite(0, 230, 210))
            Pen.send(m.dc, "near", m.dc.toCanvas(Qt.point(200, 300)), {})
            compare([Tools.tool, m.dc.tool], ["brush", "brush"])
            stroke(m.dc, [[200, 300], [260, 300]])
            tryVerify(() => Docs.current.history.length === steps + 2, 5000, "a step")
            compare(last(), "Brush")
            // the other end's tool locked on the real document: the Brush, not the end's tool kept
            const at = m.dc.toCanvas(Qt.point(200, 300))
            Tools.real = false
            Tools.setTool("lasso", "lasso")  // the tip's
            Pen.send(m.dc, "near", at, { eraser: true })
            Tools.real = true  // the Eraser stays, the Lasso locked
            compare(Tools.tool, "eraser")
            Pen.send(m.dc, "near", at, {})
            compare(Tools.tool, "brush", "the tip's Lasso locked: the Brush")
            Pen.send(m.dc, "near", at, { eraser: true })
            compare(Tools.tool, "eraser", "the eraser end's own")
        }

        // ---- hovering ---------------------------------------------------------------------------------------------------
        // The brush's pointer follows the pen in the air, and goes with it.
        function test_hover() {
            const m = openMain(false), cur = m.cursor
            verify(cur.visible)
            const p = Qt.point(cur.width * 0.4, cur.height * 0.5)
            Pen.send(cur, "near", p, {})
            let q = p
            for (let k = 0; k < 4; k++) {
                q = Qt.point(p.x + 20 * k, p.y + 10 * k)
                Pen.send(cur, "hover", q, {})
                verify(cur.point, "the brush's pointer up as the pen hovers")
                fuzzyCompare(cur.point.x, q.x, 0.01, "x " + k)
                fuzzyCompare(cur.point.y, q.y, 0.01, "y " + k)
            }
            Pen.send(cur, "away", q, {})
            compare(cur.point, null, "the pen gone: no pointer")
        }

        // ---- Wintab beside Windows Ink ----------------------------------------------------------------------------------
        // Once Wintab has had the pen, Windows Ink's events of it are kept out, and the mouse a driver makes of the pen
        // near it (within 8 px); a mouse away from it: the pen has gone. Windows Ink alone: nothing kept out.
        function test_wintab() {
            if (windows)
                skip("the filter with test pens, offscreen (Pen.api \"wintab\" opens Qt's Wintab here)")
            const c = open()
            Pen.api = "wintab"
            stroke(c, [[300, 170], [340, 180]], { source: "ink" })
            compare(c.steps, ["Brush"], "Windows Ink's while Wintab has had no pen")
            Pen.send(c, "near", c.toCanvas(Qt.point(300, 200)), { source: "wintab" })
            stroke(c, [[300, 200], [340, 210]], { source: "wintab" })
            compare(c.steps.length, 2, "Wintab's")
            stroke(c, [[300, 230], [340, 240]], { source: "ink" })
            compare(c.steps.length, 2, "Windows Ink's of the pen Wintab has: kept out")

            const pts = [[300, 250], [310, 252], [320, 254], [330, 256]]  // both at once: a driver with Windows Ink on
            pts.forEach((p, i) => {
                for (const s of ["wintab", "ink"])
                    Pen.send(c, i === 0 ? "press" : "move", c.toCanvas(Qt.point(p[0], p[1])), { source: s, pressure: 0.5 })
            })
            for (const s of ["wintab", "ink"])
                Pen.send(c, "release", c.toCanvas(Qt.point(330, 256)), { source: s })
            c.wait()
            compare(c.steps.length, 3, "one stroke")
            compare(c.stats().added, pts.length, "of Wintab's events alone")

            const q = c.toCanvas(Qt.point(400, 300))
            Pen.send(c, "hover", q, { source: "wintab" })
            mousePress(c, q.x, q.y)
            mouseMove(c, q.x + 3, q.y + 2)
            mouseRelease(c, q.x + 3, q.y + 2)
            c.wait()
            compare(c.steps.length, 3, "the driver's mouse of the pen near it: kept out")
            mousePress(c, q.x + 100, q.y)
            mouseMove(c, q.x + 120, q.y)
            mouseRelease(c, q.x + 120, q.y)
            c.wait()
            compare(c.steps.length, 4, "a mouse away from the pen: the pen gone, the mouse paints")

            Pen.api = "ink"
            stroke(c, [[300, 400], [340, 410]], { source: "ink" })
            compare(c.steps.length, 5, "Windows Ink alone: nothing kept out")
            Pen.send(c, "hover", q, { source: "wintab" })
            mousePress(c, q.x, q.y + 40)
            mouseMove(c, q.x + 20, q.y + 40)
            mouseRelease(c, q.x + 20, q.y + 40)
            c.wait()
            compare(c.steps.length, 6, "nor the mouse")
        }
    }
}
