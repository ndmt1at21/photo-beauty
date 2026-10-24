import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty
import "../qml/ToolSpec.js" as ToolSpec

// N3: Brush and Eraser on the real canvas. On a real document (Tools.real; here a white test file opened for real,
// Session's, as PB_CANVAS opens D1 in pb-ui): the tool keys, held keys and the toolbar set DocCanvas.tool; each key and
// cell of the options bar, the brush picker and Brush Settings' Spacing set DocCanvas.brush as BrushParams takes them,
// Eraser by its own bar; strokes paint by them (the brush pointer as wide as the stroke on screen), each a step "Brush"
// or "Eraser" in History, undone and redone by Ctrl Z, Ctrl Shift Z; what the core lacks is dimmed and locked (the tools
// but Brush, Eraser, Hand, Rotate View, Zoom; Mode, Airbrush, Smoothing, the tip's angle; the tips but the round ones,
// Brush Settings' shape and dynamics). The mock document keeps every tool and option. With PB_CANVAS=<file> (D1) also
// a stroke there by the bar, undone and redone.
Item {
    id: root
    width: 200
    height: 100
    Component { id: mainC; Main {} }
    Canvas {  // the test file: 1600 x 1000, white, opaque
        id: art
        property bool painted: false
        x: root.width  // out of the window
        width: 1600 / Screen.devicePixelRatio  // it saves device pixels
        height: 1000 / Screen.devicePixelRatio
        onPaint: {
            const ctx = getContext("2d")
            ctx.fillStyle = "#ffffff"
            ctx.fillRect(0, 0, width, height)
            painted = true
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
    function findAll(item, pred, out) {
        out = out || []
        for (const c of item.children) {
            if (pred(c))
                out.push(c)
            findAll(c, pred, out)
        }
        return out
    }

    TestCase {
        name: "N3"
        when: windowShown
        readonly property string dir: local(StandardPaths.writableLocation(StandardPaths.TempLocation))
        readonly property string png: dir + "/pb-n3.png"
        readonly property int white: 65535

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
        function cleanup() {
            Docs.reset()
            Tools.reset()  // Tools.real on its binding again
            Prefs.reset()
        }

        // Main on a real document (the test file opened for real, Tools.real) or, real false, on the mock one (in front of
        // PB_CANVAS's file): { w, dc (DocCanvas), vp (Viewport), cursor (BrushCursor) }.
        function open(real) {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
            Tools.real = real !== false
            const w = createTemporaryObject(mainC, root)  // no startState: its start() would reset Tools
            w.requestActivate()
            tryVerify(() => w.active)
            const m = { w: w, dc: region(w, DocCanvas), vp: region(w, Viewport), cursor: region(w, BrushCursor) }
            if (Tools.real) {
                const d = Docs.openFile(png)
                verify(d && d.real, "opened for real")
                tryVerify(() => !d.model.loading, 20000, "open")
                compare(d.model.error, "")
                tryVerify(() => m.dc.docSize.width > 0 && m.dc.frames > 0 && !m.dc.shown.pending, 20000, "a frame")
            } else {
                Docs.select(0)
            }
            waitForRendering(w.contentItem)
            w.contentItem.forceActiveFocus()
            return m
        }
        function region(w, type) { return root.find(w.contentItem, o => o instanceof type) }
        function cells(w) { return root.find(region(w, OptionsBar), o => o instanceof RowLayout).children.filter(c => c instanceof Loader) }
        function cell(w, i) { return cells(w)[i].item }
        function field(it) { return root.find(it, o => o instanceof NumberField) }
        function type(nf, text) {
            const tf = root.find(nf, o => o instanceof TextField)
            mouseClick(tf)
            tryVerify(() => tf.activeFocus)
            keySequence("Ctrl+A")
            for (const ch of text)
                keyClick(ch)
            keyClick(Qt.Key_Return)
        }
        function nudge(it, key, mods) {  // a slider by a key: one step (Shift: ten)
            it.forceActiveFocus()
            keyClick(key, mods || Qt.NoModifier)
        }
        // DocCanvas.brush against BrushParams' keys: size px, the rest as given.
        function brush(dc, want, msg) {
            const b = dc.brush
            for (const k in want) {
                if (typeof want[k] === "number")
                    fuzzyCompare(b[k], want[k], 1e-9, (msg || "") + " " + k)
                else
                    compare(b[k], want[k], (msg || "") + " " + k)
            }
        }

        // ---- painting ---------------------------------------------------------------------------------------------------
        function shows(dc, v) { const s = dc.shown; return s.x === v.x && s.y === v.y && s.zoom === v.zoom && s.angle === (v.angle || 0) && !s.pending }
        function show(m, v) {  // the view (as Viewport keeps it in bounds), its sharp frame up
            m.vp.show(v)
            tryVerify(() => shows(m.dc, m.vp.view), 20000, "a sharp frame of " + JSON.stringify(m.vp.view))
        }
        // A stroke through document points: the mouse's, or the pen's at `pressure`; until its step is made.
        function stroke(m, pts, pressure) {
            pts.forEach((p, i) => {
                const q = m.dc.toCanvas(Qt.point(p[0], p[1]))
                if (pressure !== undefined)
                    m.dc.pen(i === 0 ? "press" : "move", q, pressure)
                else if (i === 0)
                    mousePress(m.dc, q.x, q.y)
                else
                    mouseMove(m.dc, q.x, q.y)
            })
            const e = pts[pts.length - 1], q = m.dc.toCanvas(Qt.point(e[0], e[1]))
            if (pressure !== undefined)
                m.dc.pen("release", q, 0)
            else
                mouseRelease(m.dc, q.x, q.y)
            m.dc.wait()
        }
        function line(y, x0, x1) {  // document points every 5 px
            const p = []
            for (let x = x0; x <= x1; x += 5)
                p.push([x, y])
            return p
        }
        function px(dc, x, y) { return dc.composite(0, x, y) }
        function grey(dc, x, y) { return px(dc, x, y)[0] }
        function last() { const d = Docs.current; return d.history[d.index].name }

        // ---- keys -------------------------------------------------------------------------------------------------------
        // The tool keys, Shift and a key, the toolbar and the held keys put Brush, Eraser or nothing in DocCanvas.tool;
        // the brush keys change DocCanvas.brush: [ ] the size, Shift [ ] the hardness, 0 … 9 Opacity, Shift 0 … 9 Flow,
        // D, X the colour; Eraser's from its own bar.
        function test_keys() {
            const m = open(), dc = m.dc
            compare(dc.tool, "brush")
            brush(dc, { size: 400, hardness: 0, opacity: 0.06, flow: 1, spacing: 0.25, pressureSize: false, pressureOpacity: true, pressureFlow: false, color: "#ffffff" },
                  "Brush's bar as it starts")
            keyClick(Qt.Key_E)
            compare([Tools.tool, dc.tool], ["eraser", "eraser"])
            brush(dc, { size: 200, hardness: 0, opacity: 1, flow: 1, spacing: 0.25, pressureSize: false, pressureOpacity: false, pressureFlow: false }, "Eraser's")
            keyClick(Qt.Key_B)
            compare([Tools.tool, dc.tool], ["brush", "brush"])
            keyClick(Qt.Key_B, Qt.ShiftModifier)  // Pencil: locked
            compare([Tools.tool, dc.tool], ["brush", "brush"], "Shift B")
            for (const k of ["V", "M", "L", "W", "C", "I", "J", "S", "G", "O", "T", "U"]) {
                keyClick(k)
                keyClick(k, Qt.ShiftModifier)
                compare(Tools.tool, "brush", k + ": locked on a real document")
            }
            for (const k of [["H", "hand"], ["R", "rot"], ["Z", "zoom"]]) {
                keyClick(k[0])
                compare([Tools.tool, dc.tool], [k[1], ""], k[0])
            }
            keyClick(Qt.Key_E)
            keyPress(Qt.Key_Space)  // held: Hand, the canvas's left button off; back on release
            compare([Tools.tool, dc.tool], ["hand", ""])
            keyRelease(Qt.Key_Space)
            compare([Tools.tool, dc.tool], ["eraser", "eraser"])
            keyClick(Qt.Key_B)
            for (const k of [Qt.Key_Alt, Qt.Key_Control]) {  // Eyedropper, Move: locked
                keyPress(k)
                wait(350)
                compare([Tools.tool, dc.tool], ["brush", "brush"], "held " + k)
                keyRelease(k, k === Qt.Key_Alt ? Qt.AltModifier : Qt.NoModifier)
            }
            const tb = region(m.w, Toolbar), group = gid => root.findAll(tb, o => o instanceof ToolBtn && o.gid === gid)[0]
            mouseClick(group("eraser"))
            compare(dc.tool, "eraser")
            mouseClick(group("hand"))
            compare(dc.tool, "")
            mouseClick(group("brush"))
            compare(dc.tool, "brush")
            Tools.xf = true  // Free Transform: no painting
            compare(dc.tool, "")
            Tools.xf = false
            compare(dc.tool, "brush")

            keyClick(Qt.Key_BracketLeft)
            brush(dc, { size: 300 }, "[")
            keyClick(Qt.Key_BracketRight)
            keyClick(Qt.Key_BracketRight)
            brush(dc, { size: 500 }, "] ]")
            keyClick(Qt.Key_BracketRight, Qt.ShiftModifier)
            brush(dc, { hardness: 0.25 }, "Shift ]")
            keyClick(Qt.Key_BracketLeft, Qt.ShiftModifier)
            brush(dc, { hardness: 0 }, "Shift [")
            keyClick(Qt.Key_5)
            brush(dc, { opacity: 0.5 }, "5")
            keyClick(Qt.Key_0)
            brush(dc, { opacity: 1 }, "0")
            keyClick(Qt.Key_3, Qt.ShiftModifier)
            brush(dc, { flow: 0.3 }, "Shift 3")
            keyClick(Qt.Key_D)
            brush(dc, { color: "#000000" }, "D")
            keyClick(Qt.Key_X)
            brush(dc, { color: "#ffffff" }, "X")
            keyClick(Qt.Key_E)  // Eraser's own values
            brush(dc, { size: 200, opacity: 1, flow: 1 }, "Eraser")
            keyClick(Qt.Key_4)
            keyClick(Qt.Key_7, Qt.ShiftModifier)
            keyClick(Qt.Key_BracketRight)
            brush(dc, { size: 250, opacity: 0.4, flow: 0.7 }, "Eraser's keys")
            keyClick(Qt.Key_B)
            brush(dc, { size: 500, opacity: 1, flow: 0.3, hardness: 0 }, "Brush's kept")
        }

        // ---- cells ------------------------------------------------------------------------------------------------------
        // The options bar's Opacity, Flow, the pen's three buttons, the brush picker's Size and Hardness, Brush Settings'
        // Spacing (its box too): DocCanvas.brush; Eraser's bar for Eraser.
        function test_cells() {
            const m = open(), dc = m.dc, w = m.w
            type(field(cell(w, 7)), "35")
            brush(dc, { opacity: 0.35 }, "Opacity")
            type(field(cell(w, 9)), "60")
            brush(dc, { flow: 0.6 }, "Flow")
            mouseClick(cell(w, 8))
            brush(dc, { pressureOpacity: false }, "the pen on Opacity")
            mouseClick(cell(w, 10))
            brush(dc, { pressureFlow: true }, "the pen on Flow")
            mouseClick(cell(w, 17))
            brush(dc, { pressureSize: true }, "the pen on the size")
            const bp = cell(w, 2), pk = bp.picker
            mouseClick(bp)
            tryVerify(() => pk.opened)
            waitForRendering(w.contentItem)
            const sliders = root.findAll(pk.contentItem, o => o instanceof SliderField)
            nudge(sliders[0], Qt.Key_Right)
            brush(dc, { size: 401 }, "the picker's Size")
            nudge(sliders[1], Qt.Key_Right, Qt.ShiftModifier)
            brush(dc, { hardness: 0.1 }, "the picker's Hardness")
            const presets = root.findAll(pk.contentItem, o => o instanceof AbstractButton && o.height === 32)
            mouseClick(presets[1])  // Tròn cứng: 60 px, 100 %
            brush(dc, { size: 60, hardness: 1 }, "a preset")
            keyClick(Qt.Key_Escape)
            tryVerify(() => !pk.visible)
            Tools.showPanel("Brush Settings")
            tryVerify(() => !!root.find(w.contentItem, o => o instanceof BrushSettingsPanel && o.visible))
            const bs = root.find(w.contentItem, o => o instanceof BrushSettingsPanel && o.visible)
            waitForRendering(w.contentItem)
            const spacing = root.find(bs, o => o instanceof SliderField && o.Accessible.name === "Spacing")
            nudge(spacing, Qt.Key_Right, Qt.ShiftModifier)
            brush(dc, { spacing: 0.35 }, "Brush Settings' Spacing")
            Tools.setValue("brush:spacingOn", false)  // its box: the core's dabs go by the spacing still (dimmed here)
            brush(dc, { spacing: 0.35 }, "Spacing's box off")
            verify(spacing.enabled, "Spacing stays on a real document")
            brush(dc, { size: 60, hardness: 1, opacity: 0.35, flow: 0.6, spacing: 0.35, pressureSize: true, pressureOpacity: false, pressureFlow: true }, "all of Brush's")
            keyClick(Qt.Key_E)
            waitForRendering(w.contentItem)
            compare(region(w, OptionsBar).bar, "eraser")
            type(field(cell(w, 7)), "45")
            type(field(cell(w, 9)), "80")
            mouseClick(cell(w, 8))
            mouseClick(cell(w, 17))
            brush(dc, { size: 200, hardness: 0, opacity: 0.45, flow: 0.8, spacing: 0.25, pressureSize: true, pressureOpacity: true, pressureFlow: false }, "Eraser's bar")
            nudge(spacing, Qt.Key_Left)  // Brush Settings: Eraser's brush now
            brush(dc, { spacing: 0.24 }, "Eraser's Spacing")
            keyClick(Qt.Key_B)
            brush(dc, { size: 60, opacity: 0.35, spacing: 0.35 }, "Brush's again")
        }

        // ---- strokes ----------------------------------------------------------------------------------------------------
        // Strokes by the bar's values on the white test file: Opacity, the size (as wide on screen as the brush pointer at
        // 100 % and 200 %), Flow, Spacing, the pen's pressure on the size and Opacity, Eraser by its bar; each a step
        // "Brush" or "Eraser" in History, Ctrl Z and Ctrl Shift Z on both.
        function test_paint() {
            const m = open(), dc = m.dc
            show(m, { x: 800, y: 500, zoom: 1, angle: 0 })
            Tools.resetColors()  // black
            Tools.setValues({ "brush:size": 40, "brush:hard": 100, "brush:7": 50 })
            const steps = Docs.current.history.length
            stroke(m, line(400, 600, 1000))
            compare(last(), "Brush")
            compare(Docs.current.history.length, steps + 1, "a step in History")
            const half = grey(dc, 800, 400)
            verify(Math.abs(half - white / 2) < 700, "Opacity 50 %: " + half)
            compare(px(dc, 800, 400)[3], white, "still opaque")
            verify(Math.abs(grey(dc, 800, 418) - half) < 700 && grey(dc, 800, 422) === white, "40 px wide: " + grey(dc, 800, 418) + ", " + grey(dc, 800, 422))
            compare(m.cursor.diameter, 40)
            compare(m.cursor.d, 40, "the pointer at 100 %")
            for (const z of [2, 0.5]) {  // the pointer as wide as the stroke on screen
                show(m, { x: 800, y: 400, zoom: z, angle: 0 })
                // the window's (grabImage() crops by an item's x, y in its parent), down the stroke's middle, twice the
                // pointer's width about it (the document only)
                const img = grabImage(m.w.contentItem), o = dc.mapToItem(m.w.contentItem, 0, 0), q = dc.toCanvas(Qt.point(800, 400))
                const x = Math.round(o.x + q.x), y = Math.round(o.y + q.y)
                let n = 0
                for (let j = y - m.cursor.d; j <= y + m.cursor.d; j++)
                    n += img.red(x, j) < 200 ? 1 : 0
                verify(Math.abs(n - m.cursor.d) <= 2, "at " + z + ": painted " + n + " px, the pointer " + m.cursor.d)
                compare(m.cursor.d, 40 * z)
            }
            show(m, { x: 800, y: 500, zoom: 1, angle: 0 })

            Tools.setValues({ "brush:7": 100, "brush:9": 30 })  // Flow 30 %: lighter than full, darker than one dab's
            stroke(m, line(480, 600, 1000))
            const flow = grey(dc, 800, 480)
            verify(flow > 1000 && flow < white * 0.6, "Flow 30 %: " + flow)
            Tools.setValues({ "brush:9": 100, "brush:size": 20, "brush:spacing": 200 })  // dabs 40 px apart, 20 wide: gaps
            stroke(m, line(540, 600, 1000))
            compare(grey(dc, 600, 540), 0, "a dab")
            compare(grey(dc, 620, 540), white, "Spacing 200 %: a gap")
            Tools.setValue("brush:spacing", 25)
            stroke(m, line(580, 600, 1000))
            compare(grey(dc, 620, 580), 0, "Spacing 25 %: none")

            Tools.setValues({ "brush:size": 40, "brush:17": true, "brush:8": false })  // the pen at a quarter: a quarter of the size
            stroke(m, line(640, 600, 1000), 0.25)
            compare(grey(dc, 800, 640), 0)
            compare(grey(dc, 800, 652), white, "the size by the pen")
            Tools.setValues({ "brush:17": false, "brush:8": true })  // half the pressure: half the Opacity, the whole size
            stroke(m, line(700, 600, 1000), 0.5)
            verify(Math.abs(grey(dc, 800, 700) - white / 2) < 700 && grey(dc, 800, 715) < white, "Opacity by the pen: " + grey(dc, 800, 700))

            keyClick(Qt.Key_E)  // Eraser by its bar: 50 %, then all
            Tools.setValues({ "eraser:size": 40, "eraser:hard": 100 })
            keyClick(Qt.Key_5)
            stroke(m, line(760, 600, 1000))
            compare(last(), "Eraser")
            const a = px(dc, 800, 760)[3]
            verify(Math.abs(a - white / 2) < 700, "erased by half: " + a)
            keyClick(Qt.Key_0)
            stroke(m, line(820, 600, 1000))
            compare(px(dc, 800, 820)[3], 0, "erased")
            compare(px(dc, 800, 843)[3], white, "40 px wide")

            const was = px(dc, 800, 820), n = Docs.current.index
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            dc.wait()
            compare(px(dc, 800, 820), [white, white, white, white], "Ctrl Z")
            compare([Docs.current.index, last()], [n - 1, "Eraser"])
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            dc.wait()
            compare(px(dc, 800, 760)[3], white, "Ctrl Z again")
            keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
            keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
            dc.wait()
            compare([px(dc, 800, 820), Docs.current.index], [was, n], "Ctrl Shift Z twice")
            compare(px(dc, 800, 760)[3], a)
        }

        // ---- locked -----------------------------------------------------------------------------------------------------
        // On a real document the toolbar dims every group but Brush, Eraser, Hand, Zoom (Pencil greyed in Brush's flyout,
        // Brush on the group's face though Pencil is on top), Tools.setTool() leaves the tool; the options bar dims Mode,
        // Airbrush, Smoothing and its settings, the tip's angle; the brush picker and Brush Settings the tips but the round
        // ones, Brush Settings Flip, angle, roundness, Spacing's box, the sections' boxes and values. A document becoming
        // real takes a locked tool to Brush, a tip it lacks to the round one nearest its hardness.
        function test_locked() {
            Docs.reset()
            Tools.reset()
            Tools.real = false  // PB_CANVAS set: the mock first
            Tools.setTool("pencil", "brush")
            Tools.setValues({ "brush:tip": 3, "brush:hard": 100, "brush:angle": 20, "eraser:tip": 4, "eraser:hard": 40 })
            Tools.real = true
            compare([Tools.tool, Tools.top.brush], ["brush", "brush"], "Pencil: to Brush")
            compare([Tools.value("brush:tip"), Tools.value("brush:angle"), Tools.value("eraser:tip"), Tools.value("eraser:hard")], [1, undefined, 2, 40],
                    "Dẹt, Vuông: to Tròn cứng, Tròn vừa by hardness")
            const m = open(), w = m.w, tb = region(w, Toolbar)
            compare(Tools.tool, "brush")
            Tools.real = false
            Tools.setTool("pencil", "brush")  // on top of its group
            Tools.setTool("brush")
            Tools.real = true
            waitForRendering(w.contentItem)
            const buttons = root.findAll(tb, o => o instanceof ToolBtn && o.gid !== undefined)
            compare(buttons.filter(b => b.enabled).map(b => b.gid), ["brush", "eraser", "hand", "zoom"])
            verify(buttons.every(b => b.opacity === (b.enabled ? 1 : 0.4)), "dimmed")
            const bb = buttons.find(b => b.gid === "brush")
            compare([Tools.top.brush, bb.topId, bb.icon.name], ["pencil", "brush", "brush"], "Brush on the face")
            mouseClick(bb, 16, 16, Qt.RightButton)
            tryVerify(() => tb.flyout.opened)
            compare([tb.flyout.itemAt(0).enabled, tb.flyout.itemAt(1).enabled], [true, false], "Pencil greyed")
            mouseClick(tb.flyout.itemAt(1))
            compare(Tools.tool, "brush")
            keyClick(Qt.Key_Escape)
            tryVerify(() => !tb.flyout.visible)
            mouseClick(buttons.find(b => b.gid === "marquee"))
            compare(Tools.tool, "brush", "a dimmed button")
            keyClick(Qt.Key_H)
            keyClick(Qt.Key_B)  // Pencil on top: B picks Brush
            compare([Tools.tool, Tools.top.brush], ["brush", "brush"], "B with Pencil on top")
            mouseClick(buttons.find(b => b.gid === "hand"))
            Tools.real = false
            Tools.setTool("pencil", "brush")
            Tools.setTool("hand")
            Tools.real = true
            mouseClick(bb)  // the face picks Brush
            compare(Tools.tool, "brush", "the face")
            for (const id of Object.keys(ToolSpec.TOOL)) {
                const was = Tools.tool
                Tools.setTool(id)
                compare(Tools.tool, ["brush", "eraser", "hand", "rot", "zoom"].indexOf(id) >= 0 ? id : was, id)
            }
            for (const bar of ["brush", "eraser"]) {
                Tools.setTool(bar)
                waitForRendering(w.contentItem)
                const cs = cells(w), off = []
                grabImage(region(w, OptionsBar)).save(dir + "/pb-n3-bar-" + bar + ".png")
                cs.forEach((c, i) => { if (c.item && (!c.item.enabled || c.opacity < 1)) off.push(i) })
                compare(off, [5, 6, 11, 13, 14, 16], bar + ": Mode, Airbrush, Smoothing, its settings, the angle")
                verify([5, 6, 11, 13, 14, 16].every(i => cs[i].opacity === 0.4))
                const pk = cell(w, 2).picker
                mouseClick(cell(w, 2))
                tryVerify(() => pk.opened)
                const tips = root.findAll(pk.contentItem, o => o instanceof AbstractButton && o.height === 40)
                waitForRendering(w.contentItem)
                grabImage(w.contentItem).save(dir + "/pb-n3-picker-" + bar + ".png")  // the window: the picker open
                compare(tips.map(t => t.enabled), [true, true, true, false, false, false], bar + ": the round tips")
                mouseClick(tips[4])
                compare(Tools.value(bar + ":tip", 0), 0, "Vuông: locked")
                keyClick(Qt.Key_Escape)
                tryVerify(() => !pk.visible)
            }
            Tools.showPanel("Brush Settings")
            tryVerify(() => !!root.find(w.contentItem, o => o instanceof BrushSettingsPanel && o.visible))
            const bs = root.find(w.contentItem, o => o instanceof BrushSettingsPanel && o.visible)
            waitForRendering(w.contentItem)
            wait(300)  // TipShape's Canvas paints after it is made
            grabImage(w.contentItem).save(dir + "/pb-n3-real.png")  // to look at: the toolbar, Eraser's bar, Brush Settings
            const tips = root.findAll(bs, o => o instanceof AbstractButton && o.height === 40)
            compare(tips.map(t => t.enabled), [true, true, true, false, false, false], "Brush Settings: the round tips")
            const checks = root.findAll(bs, o => o instanceof CheckRow && o.visible)
            compare(checks.map(c => c.text + (c.enabled ? "" : " off")), ["Flip X off", "Flip Y off", "Spacing off", "Shape Dynamics off", "Transfer off",
                                                                         "Smoothing off", "Build-up off", "Noise off", "Wet Edges off"])
            verify(!root.find(bs, o => o instanceof TipShape && o.dot !== undefined).enabled, "angle and roundness")
            compare(root.findAll(bs, o => o instanceof NumberField && o.visible).map(f => f.enabled), [false, false])
            const sliders = root.findAll(bs, o => o instanceof SliderField && o.visible)
            compare(sliders.map(s => s.enabled), [true, true, true].concat(sliders.slice(3).map(() => false)), "Size, Hardness, Spacing; the dynamics off")
            verify(sliders.length > 3, "Shape Dynamics open")
            const chevrons = root.findAll(bs, o => o instanceof ToolToggle && o.visible && /^Mở /.test(o.Accessible.name))
            verify(chevrons.length === 2 && chevrons.every(c => c.enabled), "the sections still open")
            const preview = root.find(bs, o => o instanceof StrokePreview)
            Tools.setValues({ [bs.bar + ":roundness"]: 40, [bs.bar + ":angle"]: 30 })
            compare([preview.roundness, preview.angle, preview.minSize, preview.minOpacity], [100, 0, 1, 1], "the preview: what the core paints")

            Tools.real = false  // the mock document: all of it again
            waitForRendering(w.contentItem)
            grabImage(w.contentItem).save(dir + "/pb-n3-mock.png")  // to look at
            verify(root.findAll(tb, o => o instanceof ToolBtn && o.gid !== undefined).every(b => b.enabled && b.opacity === 1), "the mock: every tool")
            verify(root.findAll(bs, o => o instanceof CheckRow && o.visible).every(c => c.enabled))
            compare(root.findAll(bs, o => o instanceof AbstractButton && o.height === 40).map(t => t.enabled), [true, true, true, true, true, true])
            compare(preview.roundness, 40)
            verify(cells(w).every(c => !c.item || c.opacity === 1), "no cell dimmed")
            Tools.setTool("rect")
            compare(Tools.tool, "rect")
        }

        // The mock document (Tools.real false): the mock canvas, no DocCanvas, which takes no tool; the brush still mapped.
        function test_mock() {
            verify(Tools.isReal({ real: true }), "a document that says real: true (N1's DocModel)")
            verify(!Tools.isReal(Docs.current), "the mock Doc (PB_CANVAS's file is a document of its own, N1)")
            const m = open(false)
            verify(!Tools.real)
            verify(!m.dc.visible && root.find(m.vp, o => o instanceof MockCanvas).visible)
            compare(m.dc.tool, "")
            verify(!m.vp.paints)
            brush(m.dc, { size: 400, opacity: 0.06 })
            keyClick(Qt.Key_B, Qt.ShiftModifier)
            compare(Tools.tool, "pencil")
            keyPress(Qt.Key_Alt)
            tryCompare(Tools, "tool", "eyedrop", 1000)
            keyRelease(Qt.Key_Alt, Qt.AltModifier)
            keyClick(Qt.Key_M)
            compare(Tools.tool, "rect")
            const steps = Docs.current.history.length
            Tools.setTool("brush")
            mouseClick(m.vp, 400, 400)  // CanvasClicks' step, as before
            compare([Docs.current.history.length, last()], [steps + 1, "Brush"])
        }

        // ---- PB_CANVAS=<file> -------------------------------------------------------------------------------------------
        // pb-ui's Viewport on PB_CANVAS's file (D1): a stroke by the bar's size, hardness, Opacity, then Eraser's, each a
        // step; Ctrl Z, Ctrl Shift Z.
        function test_file() {
            if (CanvasEnv.document === "")
                skip("PB_CANVAS=<file> runs this")
            Docs.reset()
            Tools.reset()
            const w = createTemporaryObject(mainC, root)  // opens PB_CANVAS's file in front, as the command line's (N1)
            w.requestActivate()
            tryVerify(() => w.active)
            verify(Tools.real)
            const dc = region(w, DocCanvas)
            tryVerify(() => dc.docSize.width > 0 && dc.frames > 0 && !dc.shown.pending && !dc.shown.preview, 120000, "open")
            const m = { w: w, dc: dc, vp: region(w, Viewport), cursor: region(w, BrushCursor) }
            const c = Qt.point(Math.round(dc.docSize.width / 2), Math.round(dc.docSize.height / 2))
            show(m, { x: c.x, y: c.y, zoom: 0.5, angle: 0 })
            Tools.setFg("#000000")
            Tools.setValues({ "brush:size": 120, "brush:hard": 100, "brush:7": 80 })
            const at = [[c.x, c.y], [c.x, c.y + 50], [c.x, c.y + 75]].map(p => px(dc, p[0], p[1]))
            stroke(m, line(c.y, c.x - 200, c.x + 200))
            compare(last(), "Brush")
            const painted = px(dc, c.x, c.y), p50 = px(dc, c.x, c.y + 50)
            verify(String(painted) !== String(at[0]) && String(p50) !== String(at[1]), "painted 120 px wide: " + painted)
            compare(px(dc, c.x, c.y + 75), at[2], "nothing past it")
            keyClick(Qt.Key_E)
            Tools.setValues({ "eraser:size": 60, "eraser:hard": 100 })
            stroke(m, line(c.y, c.x - 100, c.x + 100))
            compare(last(), "Eraser")
            const erased = px(dc, c.x, c.y)
            verify(String(erased) !== String(painted), "erased: " + erased)
            compare(px(dc, c.x, c.y + 50), p50, "Eraser 60 px wide")
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            dc.wait()
            compare(px(dc, c.x, c.y), painted, "Ctrl Z")
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            dc.wait()
            compare(px(dc, c.x, c.y), at[0], "Ctrl Z again")
            keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
            keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
            dc.wait()
            compare(px(dc, c.x, c.y), erased, "Ctrl Shift Z twice")
            grabImage(w.contentItem).save(dir + "/pb-n3-file.png")  // to look at
        }
    }
}
