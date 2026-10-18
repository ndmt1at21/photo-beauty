import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty
import "../qml/View.js" as View

// U15: Select and Mask and Liquify (Workspace): in and out by OK, Enter, Hủy, Esc with the document, its view and the
// tool as they were; the spaces' keys; what each Output To makes of the document, as one step; the views drawn over the
// photo by the selection's shape; Remember Settings; Liquify's sliders.
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

    TestCase {
        name: "U15"
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
        // The space by its menu key, once it has set its tool (after Main noted the one to give back).
        function enter(w, name) {
            keySequence(name === "SelectMask" ? "Ctrl+Alt+R" : "Ctrl+Shift+X")
            verify(w.space, name)
            tryCompare(Tools, "tool", w.space.startTool)
            waitForRendering(w.contentItem)
            return w.space
        }
        // The space's own over its canvas (Workspace.overlay), not the window's overlays (U7's) hidden in its Viewport.
        function over(s, type) { return s.overlay.find(o => o instanceof type) }
        function control(s, type, text) { return find(s, o => o instanceof type && o.visible && (text === undefined || o.text === text || o.label === text)) }
        function json(x) { return JSON.stringify(x) }
        // Properties scrolled to an item of it, then a click on it.
        function click(item) {
            let f = item.parent
            while (f && !(f instanceof Flickable))
                f = f.parent
            if (f) {
                const y = item.mapToItem(f.contentItem, 0, 0).y
                f.contentY = Math.max(0, Math.min(f.contentHeight - f.height, y - f.height / 2))
                waitForRendering(item)
            }
            mouseClick(item)
        }
        // A control in the popup a button opens.
        function inPopup(button, type, text) {
            const p = button.data.find(o => o instanceof Popup)
            return p && p.opened ? find(p.contentItem.parent, o => o instanceof type && o.visible && (o.text === text || o.label === text)) : null
        }
        // The colour at document point p of the space's canvas, in the window.
        function at(w, s, p) {
            const shade = over(s, Canvas)
            const q = View.toCanvas(s.view, s.size, p), r = shade.mapToItem(w.contentItem, q.x, q.y)
            return grabImage(w.contentItem).pixel(Math.round(r.x), Math.round(r.y))
        }

        // ---- in and out ------------------------------------------------------------------------------------------------
        function test_in_out_data() {
            return [
                { tag: "Select and Mask, OK", space: "SelectMask", how: "OK", step: "Select and Mask" },
                { tag: "Select and Mask, Enter", space: "SelectMask", how: "Enter", step: "Select and Mask" },
                { tag: "Select and Mask, Hủy", space: "SelectMask", how: "Hủy" },
                { tag: "Select and Mask, Esc", space: "SelectMask", how: "Esc" },
                { tag: "Liquify, OK", space: "Liquify", how: "OK", step: "Liquify" },
                { tag: "Liquify, Enter", space: "Liquify", how: "Enter", step: "Liquify" },
                { tag: "Liquify, Hủy", space: "Liquify", how: "Hủy" },
                { tag: "Liquify, Esc", space: "Liquify", how: "Esc" }
            ]
        }
        function test_in_out(data) {
            const w = open()
            Tools.setTool("brush", "brush")
            const d = Docs.current, n = d.history.length, view = json(d.view), port = json(Docs.viewport), layers = json(d.layers)
            const s = enter(w, data.space)
            compare(s.doc, d)
            verify(!find(w.contentItem, o => o instanceof TopBar).visible, "in the window's place")
            keySequence("Ctrl+1")  // the space's own zoom
            tryVerify(() => d.view.zoom === 1)
            if (data.how === "Esc" || data.how === "Enter")
                keyClick(data.how === "Esc" ? Qt.Key_Escape : Qt.Key_Return)
            else
                mouseClick(control(s, Btn, data.how))
            compare(w.space, null)
            compare(Docs.current, d)
            compare(Tools.tool, "brush")
            compare(json(d.view), view, "the view as it was")
            compare(json(Docs.viewport), port, "Vừa màn hình fits the window's canvas again")
            compare(d.history.length, n + (data.step ? 1 : 0))
            if (data.step)
                compare(d.history[d.index].name, data.step)
            else
                compare(json(d.layers), layers)
        }
        function test_quick_out() {  // out before the space's tool was set: the tool as it was all the same
            const w = open()
            Tools.setTool("brush", "brush")
            const s = w.openSpace("SelectMask", {})
            verify(s)
            s.finished(false)
            wait(50)
            compare(Tools.tool, "brush")
        }
        // On another document than the first; the tool given back is the one before, whatever the space picked.
        function test_back_to_document() {
            const w = open()
            Docs.select(2)
            Tools.setTool("lasso", "lasso")
            const d = Docs.current
            let s = enter(w, "SelectMask")
            compare(s.doc, d)
            keyClick(Qt.Key_Z)
            keyClick(Qt.Key_Escape)
            compare(Docs.current, d)
            compare(Tools.tool, "lasso")
            s = enter(w, "Liquify")
            compare(Tools.tool, "lqWarp", "no face to work on")
            verify(!find(s, o => o instanceof Dropdown && o.displayText === "Không có mặt").enabled, "Face-Aware without a face")
            keyClick(Qt.Key_H)
            mouseClick(control(s, Btn, "Hủy"))
            compare([Docs.current, Tools.tool], [d, "lasso"])
        }
        function test_header() {
            const w = open()
            Docs.current.run({ op: "selectLayers", layers: [Docs.current.layers.find(l => l.props.background).id] })
            const s = enter(w, "SelectMask")
            const texts = []
            for (const o of [find(s, x => x instanceof Text && x.text === "Select and Mask"), find(s, x => x instanceof Text && x.text === "· chân dung.psd · layer "),
                find(s, x => x instanceof Text && x.text === "Background")])
                texts.push(!!o && o.visible)
            compare(texts, [true, true, true])
            verify(find(s, x => x instanceof Text && x.text === "Background").font.italic)
        }

        // ---- the canvas --------------------------------------------------------------------------------------------------
        // Hand and Zoom are the canvas's; no right-click menu, none of the window's overlays.
        function test_canvas() {
            const w = open()
            const s = enter(w, "SelectMask")
            const d = s.doc, vp = find(s, o => o instanceof Viewport), shade = over(s, Canvas)
            verify(vp.rulers)
            compare(json(vp.mapToItem(s, 18, 18)), json(shade.mapToItem(s, 0, 0)), "the frame where the area is, the rulers out of sight")
            compare(json([vp.size.w, vp.size.h]), json([shade.width, shade.height]))
            verify(!find(vp, o => o instanceof Overlays).visible)
            s.forceActiveFocus()
            mouseClick(vp, vp.width / 2, vp.height / 2, Qt.RightButton)
            verify(!vp.activeFocus, "no right-click menu: the press stops over the canvas")
            const z = d.view.zoom
            keyClick(Qt.Key_Z)
            mouseClick(vp, vp.width / 2, vp.height / 2)
            tryVerify(() => d.view.zoom > z)
            keyClick(Qt.Key_H)
            keySequence("Ctrl+1")  // past the frame: a picture smaller than it stays in its middle
            const x = d.view.x
            mouseDrag(vp, vp.width / 2, vp.height / 2, 100, 0)
            tryVerify(() => d.view.x < x)
        }
        // Black & White: white where selected, black where not; inverted the other way; on the shape of the selection.
        function test_views() {
            const w = open()
            const s = enter(w, "SelectMask")
            const d = s.doc, inside = { x: d.w * 0.58, y: d.h * 0.4 }, outside = { x: d.w * 0.1, y: d.h * 0.1 }
            compare(json(s.sel), json({ shape: "subject", inverse: false }), "the subject without a selection")
            keyClick(Qt.Key_K)
            const is = (c, v) => Math.abs(c.r - v) < 0.02 && Math.abs(c.g - v) < 0.02 && Math.abs(c.b - v) < 0.02
            tryVerify(() => is(at(w, s, inside), 1) && is(at(w, s, outside), 0))
            mouseClick(control(s, Btn, "Invert"))
            tryVerify(() => is(at(w, s, inside), 0) && is(at(w, s, outside), 1))
            mouseClick(control(s, Btn, "Clear Selection"))
            tryVerify(() => is(at(w, s, inside), 0) && is(at(w, s, outside), 0))
            mouseClick(control(s, Btn, "Invert"))  // everything
            tryVerify(() => is(at(w, s, inside), 1) && is(at(w, s, outside), 1))
            mouseClick(control(s, Btn, "Select Subject"))
            keyClick(Qt.Key_A)  // On Black at 100 %: the photo where selected
            s.put({ op: 100 })
            tryVerify(() => !is(at(w, s, inside), 0) && is(at(w, s, outside), 0))
            keyClick(Qt.Key_O)  // Onion Skin at 100 %: the checker of transparency, white and #c9c9c9
            tryVerify(() => (is(at(w, s, outside), 1) || is(at(w, s, outside), 201 / 255)) && !is(at(w, s, inside), 1))
            keyClick(Qt.Key_P)  // Show Original: the photo all over
            tryVerify(() => !is(at(w, s, outside), 0))
            compare(control(s, Text, "Show Original · ảnh gốc").visible, true)
            keyClick(Qt.Key_P)
            keyClick(Qt.Key_V)
            compare(s.label, "Overlay · 100% · phần đỏ là phần không được chọn")
        }
        // Alt and the right button dragged across the canvas from its middle: the size the brush asks for.
        function altDrag(s, dx) {
            const vp = find(s, o => o instanceof Viewport), c = { x: vp.width / 2, y: vp.height / 2 }
            mousePress(vp, c.x, c.y, Qt.RightButton, Qt.AltModifier)
            mouseMove(vp, c.x + dx, c.y, -1, Qt.RightButton, Qt.AltModifier)
            const adjusting = over(s, BrushCursor).adjusting
            mouseRelease(vp, c.x + dx, c.y, Qt.RightButton, Qt.AltModifier)
            verify(adjusting, "Alt and the right button: the brush's, not the canvas's")
        }
        // U7's: the ants in Marching Ants only, on the selection being refined; the brush on the brush tools at their size,
        // over the canvas that keeps its hover, Alt and the right button resizing it.
        function test_ants_brush() {
            const w = open()
            const s = enter(w, "SelectMask")
            const vp = find(s, o => o instanceof Viewport)
            const ants = over(s, Ants), cursor = over(s, BrushCursor)
            verify(!ants.visible)
            keyClick(Qt.Key_M)
            verify(ants.visible)
            compare([ants.shape, ants.inverse, ants.docW, ants.docH], ["subject", false, s.doc.w, s.doc.h])
            mouseClick(control(s, Btn, "Invert"))
            compare([ants.shape, ants.inverse], ["subject", true])
            keyClick(Qt.Key_P)
            verify(!ants.visible, "Show Original: the photo alone")
            keyClick(Qt.Key_P)
            verify(cursor.visible)
            compare(cursor.diameter, 30)
            mouseMove(vp, vp.width / 2, vp.height / 2)
            tryVerify(() => !!cursor.point && !!vp.pointer, 1000, "the brush over the canvas, which keeps its hover")
            const k = View.scale(s.view, s.size)
            altDrag(s, 50)
            compare([s.brushSize, cursor.diameter], [Math.round(30 + 100 / k), Math.round(30 + 100 / k)])
            Prefs.set("paintCursor", 0)
            verify(!cursor.drawn, "Thiết lập › Con trỏ: Standard, the system's pointer")
            keyClick(Qt.Key_L)
            verify(!cursor.visible)
            keyClick(Qt.Key_Escape)
            const l = enter(w, "Liquify")
            const lc = over(l, BrushCursor)
            verify(!lc.visible, "Face has no brush")
            keyClick(Qt.Key_W)
            verify(lc.visible)
            compare([lc.diameter, lc.maxDiameter], [300, 15000])
            altDrag(l, 100)
            compare(l.get("size", 0), Math.min(15000, Math.round(300 + 200 / View.scale(l.view, l.size))))
        }
        function test_from_selection() {  // the document's selection, inverted or not
            const w = open()
            Docs.current.run({ op: "select", shape: "lasso" })
            Docs.current.run({ op: "inverse" })
            const s = enter(w, "SelectMask")
            compare(json(s.sel), json({ shape: "lasso", inverse: true }))
        }

        // ---- keys ----------------------------------------------------------------------------------------------------
        function test_selectmask_keys() {
            const w = open()
            const s = enter(w, "SelectMask")
            for (const k of [[Qt.Key_O, "onion"], [Qt.Key_M, "ants"], [Qt.Key_V, "overlay"], [Qt.Key_A, "black"], [Qt.Key_T, "white"], [Qt.Key_K, "bw"], [Qt.Key_Y, "layers"]]) {
                keyClick(k[0])
                compare(s.mode, k[1])
            }
            keyClick(Qt.Key_J)
            verify(s.get("edge", false), "Show Edge")
            verify(control(s, CheckRow, "Show Edge (J)").checked)
            keyClick(Qt.Key_J)
            verify(!s.get("edge", false))
            keyClick(Qt.Key_P)
            verify(s.original, "Show Original")
            keyClick(Qt.Key_P)
            verify(!s.original)
            for (const k of [[Qt.Key_B, "smBrush"], [Qt.Key_L, "smLasso"], [Qt.Key_H, "hand"], [Qt.Key_Z, "zoom"], [Qt.Key_R, "smRefine"]]) {
                keyClick(k[0])
                compare(Tools.tool, k[1])
            }
            keyClick(Qt.Key_Tab)  // into the space's controls, not the window's hidden ones
            let f = w.activeFocusItem
            while (f && f !== s)
                f = f.parent
            compare(f, s)
        }
        function test_liquify_keys() {
            const w = open()
            enter(w, "Liquify")
            for (const k of [[Qt.Key_W, "lqWarp"], [Qt.Key_R, "lqRecon"], [Qt.Key_E, "lqSmooth"], [Qt.Key_C, "lqTwirl"], [Qt.Key_S, "lqPucker"], [Qt.Key_B, "lqBloat"],
                [Qt.Key_O, "lqPush"], [Qt.Key_F, "lqFreeze"], [Qt.Key_D, "lqThaw"], [Qt.Key_A, "lqFace"], [Qt.Key_H, "hand"], [Qt.Key_Z, "zoom"]]) {
                keyClick(k[0])
                compare(Tools.tool, k[1])
            }
            compare(Tools.screen, 0, "F is Freeze Mask, not the screen mode")
        }
        function test_tool_column() {  // a click picks; the one on lit
            const w = open()
            const s = enter(w, "Liquify")
            const b = find(s, o => o instanceof AbstractButton && o.text === "Bloat (B)")
            mouseClick(b)
            compare(Tools.tool, "lqBloat")
            verify(b.on)
            compare(s.kind, "r")
        }

        // ---- Select and Mask's output ----------------------------------------------------------------------------------
        function test_output_data() {
            return [
                { tag: "Selection", out: "Selection", selection: "subject", inverse: false },
                { tag: "Selection, inverted", out: "Selection", invert: 1, selection: "subject", inverse: true },
                { tag: "Selection, cleared", out: "Selection", clear: true, selection: null, inverse: false },
                { tag: "Selection, everything", out: "Selection", clear: true, invert: 1, selection: "all", inverse: false },
                { tag: "Layer Mask", out: "Layer Mask", mask: true, layers: 0 },
                { tag: "Layer Mask, inverted", out: "Layer Mask", invert: 1, mask: true, layers: 0, hidden: true },
                { tag: "New Layer", out: "New Layer", mask: false, layers: 1 },
                { tag: "New Layer with Layer Mask", out: "New Layer with Layer Mask", mask: true, layers: 1 },
                { tag: "New Document", out: "New Document", docs: 1 }
            ]
        }
        function test_output(data) {
            const w = open()
            const d = Docs.current, n = d.history.length, count = d.layers.length, docs = Docs.docs.length
            const s = enter(w, "SelectMask")
            if (data.clear)
                mouseClick(control(s, Btn, "Clear Selection"))
            for (let i = 0; i < (data.invert || 0); i++)
                mouseClick(control(s, Btn, "Invert"))
            s.put({ out: data.out })
            mouseClick(control(s, Btn, "OK"))
            compare(Docs.current, d)
            if (data.docs) {
                compare(Docs.docs.length, docs + 1)
                compare(Docs.docs[docs].file, "chân dung bản sao.psd")
                compare(d.history.length, n, "nothing on this one")
                return
            }
            compare(Docs.docs.length, docs)
            compare(d.history.length, n + 1, "one step")
            compare(d.history[d.index].name, "Select and Mask")
            if (data.out === "Selection") {
                compare([d.selection, d.inverse], [data.selection, data.inverse])
                compare(d.layers.length, count)
                return
            }
            compare(d.layers.length, count + data.layers)
            compare(!!d.active.props.mask, data.mask)
            if (data.mask)  // the mock's masks: the selection's ellipse white on black, or black on white inverted
                compare(d.active.props.mask.indexOf("#000000 0 70%") >= 0, !!data.hidden)
            if (data.layers)
                verify(d.active.props.name.endsWith(" bản sao"))
            d.undo()
            compare(d.layers.length, count, "undone at once")
        }
        function test_output_list() {  // Output To from its list
            const w = open()
            const s = enter(w, "SelectMask")
            const dd = find(s, o => o instanceof Dropdown && o.options.length === 5 && o.options[0][0] === "Selection")
            compare(dd.value, "Layer Mask")
            mouseClick(dd)
            tryVerify(() => dd.popup.opened)
            const row = dd.popup.contentItem.itemAtIndex(0)
            waitForRendering(row)
            mouseClick(row)
            compare(s.get("out", ""), "Selection")
            compare(dd.value, "Selection")
        }
        function test_remember() {
            const w = open()
            let s = enter(w, "SelectMask")
            keyClick(Qt.Key_K)
            s.put({ radius: 40 })
            mouseClick(control(s, CheckRow, "Remember Settings"))
            verify(s.checked)
            mouseClick(control(s, Btn, "OK"))
            s = enter(w, "SelectMask")
            compare([s.mode, s.get("radius", 6), s.checked], ["bw", 40, true])
            keyClick(Qt.Key_A)
            keyClick(Qt.Key_Escape)  // Hủy keeps nothing
            s = enter(w, "SelectMask")
            compare(s.mode, "bw")
            mouseClick(control(s, CheckRow, "Remember Settings"))
            mouseClick(control(s, Btn, "OK"))
            compare(Tools.value("SelectMask", null), null)
            s = enter(w, "SelectMask")
            compare([s.mode, s.get("radius", 6), s.checked], ["overlay", 6, false])
        }
        function test_controls() {  // Properties and the options bar into the settings
            const w = open()
            const s = enter(w, "SelectMask")
            const op = control(s, SliderField, "Opacity")
            verify(op.enabled)
            op.moved(70)
            compare(s.opacityPct, 70)
            keyClick(Qt.Key_M)
            verify(!op.enabled, "no Opacity for Marching Ants")
            const shift = control(s, SliderField, "Shift Edge")
            compare(shift.valueText, "−8%")
            shift.moved(-20)
            compare(s.get("shift", 0), -20)
            const seg = find(s, o => o instanceof Segmented && o.visible && o.options.length === 2 && o.options[0] === "Color Aware")
            seg.picked(0)
            compare(s.get("rm", ""), "color")
            const modes = find(s, o => o instanceof Segmented && o.visible && o.options.length === 2 && o.options[0].t === "+")
            modes.picked(1)
            compare(s.get("mode_smRefine", 0), 1)
            keyClick(Qt.Key_Z)
            modes.picked(1)
            compare(Tools.value("zoom:2", 0), 1, "Zoom's −: the Zoom tool's own")
            keyClick(Qt.Key_B)
            verify(control(s, AbstractButton, "Cỡ brush: 30 px"))
            mouseClick(control(s, Btn, "Refine Hair"))
            verify(s.get("hair", false))
        }

        // ---- Liquify --------------------------------------------------------------------------------------------------
        function test_liquify_brush() {  // the brush's sliders by tool: none on Face, Rate on the "r" tools
            const w = open()
            const s = enter(w, "Liquify")
            const size = control(s, SliderField, "Size"), rate = control(s, SliderField, "Rate")
            compare(Tools.tool, "lqFace")
            verify(!size.enabled && !rate.enabled)
            keyClick(Qt.Key_W)
            verify(size.enabled && !rate.enabled)
            keyClick(Qt.Key_S)
            verify(size.enabled && rate.enabled)
            size.moved(15000)
            compare(size.valueText, "15.000")
        }
        function test_liquify_face() {
            const w = open()
            const s = enter(w, "Liquify")
            compare([s.face("eyeSizeL"), s.face("eyeSizeR"), s.face("eyeDist"), s.face("smile")], [12, 12, -4, 0])
            s.setFace("eyeSize", true, "L", 30)
            compare([s.face("eyeSizeL"), s.face("eyeSizeR")], [30, 30], "linked")
            click(find(s, o => o instanceof IconButton && o.visible && o.icon.name === "link"))
            verify(!s.get("lk_eyeSize", true))
            s.setFace("eyeSize", true, "L", 50)
            compare([s.face("eyeSizeL"), s.face("eyeSizeR")], [50, 30], "one eye")
            s.put({ recon: 50 })
            s.reconstruct()
            compare([s.face("eyeSizeL"), s.face("eyeSizeR"), s.face("eyeDist"), s.get("recon", 0)], [25, 15, -2, 100])
            click(control(s, Btn, "Restore All"))
            verify(s.faceKeys.every(k => s.face(k) === 0))
            compare(s.faceKeys.length, 20)
            const nose = find(s, o => o instanceof AbstractButton && o.text === "Nose")
            verify(!find(s, o => o instanceof Text && o.text === "Nose Width").visible)
            click(nose)
            waitForRendering(w.contentItem)
            verify(find(s, o => o instanceof Text && o.text === "Nose Width").visible)
        }
        function test_liquify_reconstruct() {  // its popup: Amount, OK scales; Hủy leaves the sliders
            const w = open()
            const s = enter(w, "Liquify")
            const b = control(s, Btn, "Reconstruct…")
            click(b)
            let amount = null
            tryVerify(() => (amount = inPopup(b, SliderField, "Amount")) !== null)
            waitForRendering(amount)
            amount.moved(0)
            compare(s.get("recon", 0), 0)
            mouseClick(inPopup(b, Btn, "Hủy"))
            tryVerify(() => !inPopup(b, SliderField, "Amount"))
            compare([s.face("eyeSizeL"), s.get("recon", 0)], [12, 100])
            click(b)
            tryVerify(() => (amount = inPopup(b, SliderField, "Amount")) !== null)
            waitForRendering(amount)
            amount.moved(0)
            mouseClick(inPopup(b, Btn, "OK"))
            compare([s.face("eyeSizeL"), s.face("eyeDist")], [0, 0])
        }
    }
}
