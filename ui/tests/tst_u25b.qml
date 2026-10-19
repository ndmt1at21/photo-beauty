import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import QtQuick.Templates as T
import QtTest
import PhotoBeauty
import "../qml/MenuSpec.js" as MenuSpec
import "../qml/View.js" as View

// U25b: Edit › Fade…, Define Brush Preset…, Image › Image Rotation › Tùy ý…, Trim… (build/design2/project/
// EditDialogs.dc.html, ImageDialogs.dc.html) and Liquify's None, Mask All, Invert All. The dialogs made straight from
// their files as Main would, on Docs.current (chân dung.psd: 4.400 × 5.600 px, a Background layer, History on Brush);
// then from the menus and keys of a Main.
Item {
    id: root
    width: 1280
    height: 900

    Component { id: fadeC; FadeDialog {} }
    Component { id: brushC; DefineBrushDialog {} }
    Component { id: rotateC; RotateCanvasDialog {} }
    Component { id: trimC; TrimDialog {} }
    Component { id: brushesC; BrushesPanel {} }
    Component { id: mainC; Main {} }

    function find(item, pred) {
        for (const c of item.children) {
            if (pred(c)) return c
            const f = find(c, pred)
            if (f) return f
        }
        return null
    }
    function all(item, pred, out) {
        out = out || []
        for (const c of item.children) {
            if (pred(c)) out.push(c)
            all(c, pred, out)
        }
        return out
    }

    TestCase {
        name: "U25b"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
        }
        function cleanupTestCase() { init() }

        function show(c, params) {
            const d = createTemporaryObject(c, root, { params: params || {} })
            d.open()
            tryVerify(() => d.opened)  // the enter transition rendered: laid out
            return d
        }
        function of(d, type) { return root.all(d.contentItem, o => o instanceof type && o.visible) }
        function text(d, t) { return root.find(d.contentItem, o => o instanceof Text && o.visible && o.text === t) }
        function button(d, t) { return root.find(d.footer, o => o instanceof Btn && o.text === t) }
        function input(f) { return root.find(f, o => o instanceof T.TextField) }
        function type(f, t) {  // click into the field, the text over its value, Enter
            mouseClick(input(f))
            tryVerify(() => f.editing)
            input(f).text = t
            keyClick(Qt.Key_Return)
            verify(!f.editing)
        }
        function ok(d) {
            mouseClick(button(d, "OK"))
            tryVerify(() => !d.visible)
        }
        function cancel(d) {
            mouseClick(button(d, "Hủy"))
            tryVerify(() => !d.visible)
        }
        function last(doc) { return doc.history[doc.index] }
        // x, y of an item in the dialog's box (from its title bar's corner), its width and height, each within 2 px of
        // the artboard's (Chrome's boxes less the dialog's corner); null where it does not count.
        function near(d, o, want, what) {
            verify(o, what)
            const p = o.mapToItem(d.header, 0, 0), got = [p.x, p.y, o.width, o.height]
            for (let i = 0; i < want.length; i++)
                if (want[i] !== null)
                    verify(Math.abs(got[i] - want[i]) <= 2, what + ": " + got.map(v => Math.round(v * 10) / 10) + " for the artboard's " + want)
        }
        function size(d, w, h) {
            verify(Math.abs(d.width - w) <= 2 && Math.abs(d.height - h) <= 2, d.title + ": " + [d.width, d.height] + " for the artboard's " + [w, h])
        }

        // ---- Fade ------------------------------------------------------------------------------------------------
        function test_fade() {
            const doc = Docs.current, n = doc.history.length
            compare(last(doc).name, "Brush")
            let d = show(fadeC)
            compare(d.title, "Fade Brush")
            const f = of(d, NumberField)[0], s = of(d, SliderField)[0], m = of(d, Dropdown)[0], p = of(d, CheckRow)[0]
            compare([f.value, s.value, m.value, p.text, p.checked], [100, 100, "normal", "Preview", true])
            compare(m.pairs.map(o => o[1]), ["Normal", "Dissolve", "Darken", "Multiply", "Color Burn", "Linear Burn", "Darker Color", "Lighten", "Screen",
                "Color Dodge", "Linear Dodge (Add)", "Lighter Color", "Overlay", "Soft Light", "Hard Light", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix",
                "Difference", "Exclusion", "Subtract", "Divide", "Hue", "Saturation", "Color", "Luminosity"], "the artboard's MODES")
            type(f, "40")
            compare([d.fadeOpacity, s.value], [40, 40])
            type(f, "-5")
            compare(d.fadeOpacity, 0, "0 % at least")
            type(f, "150")
            compare(d.fadeOpacity, 100, "100 % at most")
            s.moved(65)
            compare([d.fadeOpacity, f.value], [65, 65])
            m.activated(m.pairs.findIndex(o => o[0] === "multiply"))
            compare(d.mode, "multiply")
            mouseClick(p)
            verify(!d.preview)
            ok(d)
            compare(doc.history.length, n + 1)
            const c = last(doc).cmds[0]
            compare([last(doc).name, last(doc).icon, c.opacity, c.mode], ["Fade Brush", "brush", 0.65, "multiply"])
            // Hủy, Esc, ×: no step
            d = show(fadeC)
            compare(d.title, "Fade Fade Brush")
            type(of(d, NumberField)[0], "10")
            cancel(d)
            d = show(fadeC)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !d.visible)
            d = show(fadeC)
            mouseClick(root.find(d.header, o => o instanceof IconButton && o.visible))
            tryVerify(() => !d.visible)
            compare(doc.history.length, n + 1)
            // nothing to fade on the first step
            doc.jumpTo(0)
            d = show(fadeC)
            compare(d.title, "Fade")
            verify(!button(d, "OK").enabled)
        }

        // ---- Define Brush Preset ---------------------------------------------------------------------------------
        function test_define_brush() {
            const doc = Docs.current, n = doc.history.length
            doc.run({ op: "select", shape: "rect" })  // 0,375 × 0,46 of 4.400 × 5.600: 1.650 × 2.576
            let d = show(brushC)
            compare(d.title, "Brush Name")
            compare(d.size, 2576)
            verify(text(d, "2.576 px"), "the size under the tip")
            compare(of(d, TextBox)[0].text, "Cọ mẫu 1")
            ok(d)
            compare(doc.history.length, n + 2)
            compare([last(doc).name, last(doc).icon, last(doc).cmds[0].brush, last(doc).cmds[0].size], ["Define Brush Preset", "brush", "Cọ mẫu 1", 2576])
            let gs = Tools.value("brushes:groups", null)
            compare(gs.map(g => g[0]), ["Cơ bản", "Retouch", "Khô và mực", "Của tôi"], "Của tôi made at the end")
            compare(JSON.stringify(gs[3][1]), JSON.stringify([["Cọ mẫu 1", 2576, 0, ""]]))
            compare(Tools.value("brushes:open", null), [true, true, false, true], "and opened")
            // the next one: N on, a name of its own, the same group
            d = show(brushC)
            const name = of(d, TextBox)[0]
            compare(name.text, "Cọ mẫu 2")
            mouseClick(name)
            name.selectAll()
            keyClick(Qt.Key_T)
            keyClick(Qt.Key_O)
            compare(name.text, "to")
            ok(d)
            gs = Tools.value("brushes:groups", null)
            compare(gs.length, 4)
            compare(gs[3][1].map(b => b[0]), ["Cọ mẫu 1", "to"])
            // a blank name: no OK; Hủy: nothing
            d = show(brushC)
            compare(of(d, TextBox)[0].text, "Cọ mẫu 2", "\"to\" took no number")
            of(d, TextBox)[0].text = "  "
            verify(!button(d, "OK").enabled)
            cancel(d)
            compare([Tools.value("brushes:groups", null)[3][1].length, doc.history.length], [2, n + 3])
            // in the Brushes panel, its group open
            const panel = createTemporaryObject(brushesC, root, { width: 280 })
            waitForRendering(panel)
            verify(root.find(panel, o => o instanceof Text && o.visible && o.text === "Của tôi"), "the group")
            verify(root.find(panel, o => o instanceof AbstractButton && o.visible && o.text === "to"), "the preset")
            // the size: the selection's bounds, the whole image's without one, 5.000 px at most
            doc.run({ op: "select", shape: "lips" })  // 0,146 × 0,042: 642 × 235
            compare(show(brushC).size, 642)
            doc.run({ op: "deselect" })
            compare(show(brushC).size, 5000)
        }

        // ---- Rotate Canvas ---------------------------------------------------------------------------------------
        function test_rotate_canvas() {
            const doc = Docs.current, n = doc.history.length
            let d = show(rotateC)
            compare(d.title, "Rotate Canvas")
            const f = of(d, NumberField)[0]
            compare([f.value, f.decimals, f.unit], [0, 2, "°"])
            compare(of(d, Radio).map(o => [o.text, o.checked]), [["Chiều kim đồng hồ", true], ["Ngược chiều kim đồng hồ", false]])
            type(f, "400")
            compare(d.angle, 359.99)
            type(f, "-400")
            compare(d.angle, -359.99)
            type(f, "33,333")
            compare(d.angle, 33.33, "two decimals")
            ok(d)  // 33,33° clockwise: the step, the image as it was
            compare(doc.history.length, n + 1)
            compare([last(doc).name, last(doc).icon, last(doc).cmds.length, last(doc).cmds[0].angle, doc.rot], ["Rotate Canvas", "rotate_right", 1, 33.33, 0])
            d = show(rotateC)
            type(of(d, NumberField)[0], "90")
            mouseClick(of(d, Radio)[1])
            verify(!d.clockwise)
            ok(d)  // −90°: a quarter, which the mock turns, in the one step
            compare(doc.history.length, n + 2)
            compare([last(doc).name, last(doc).cmds.map(c => c.op), last(doc).cmds[0].angle, doc.rot, doc.w, doc.h],
                    ["Rotate Canvas", ["step", "rotateCanvas"], -90, 270, 5600, 4400])
            ok(show(rotateC))  // 0°: nothing
            d = show(rotateC)
            type(of(d, NumberField)[0], "45")
            cancel(d)
            compare(doc.history.length, n + 2)
        }

        // ---- Trim ------------------------------------------------------------------------------------------------
        function test_trim() {
            const doc = Docs.current, n = doc.history.length
            let d = show(trimC)
            compare(d.title, "Trim")
            const r = of(d, Radio), c = of(d, CheckRow)
            compare(r.map(o => [o.text, o.enabled, o.checked]), [["Điểm ảnh trong suốt", false, false], ["Màu điểm ảnh góc trên trái", true, true],
                                                                  ["Màu điểm ảnh góc dưới phải", true, false]], "a Background layer: nothing transparent")
            compare(c.map(o => [o.text, o.checked]), [["Trên", true], ["Trái", true], ["Dưới", true], ["Phải", true]])
            mouseClick(r[0])
            compare(d.basedOn, "topLeft")
            mouseClick(r[2])
            mouseClick(c[1])
            compare([d.basedOn, c[1].checked], ["bottomRight", false])
            ok(d)
            compare(doc.history.length, n + 1)
            const t = last(doc).cmds[0]
            compare([last(doc).name, last(doc).icon, t.basedOn, t.top, t.left, t.bottom, t.right], ["Trim", "crop", "bottomRight", true, false, true, true])
            cancel(show(trimC))
            compare(doc.history.length, n + 1)
            Docs.create({ w: 800, h: 600, background: "transparent" })  // no Background layer
            d = show(trimC)
            verify(of(d, Radio)[0].enabled)
            mouseClick(of(d, Radio)[0])
            compare(d.basedOn, "transparent")
        }

        // Tab from control to control, Space, Enter on OK, Esc.
        function test_keys() {
            const doc = Docs.current, n = doc.history.length
            let d = show(trimC)
            const r = of(d, Radio)
            r[1].forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            verify(r[2].activeFocus, "Tab: the next radio")
            keyClick(Qt.Key_Space)
            compare(d.basedOn, "bottomRight")
            keyClick(Qt.Key_Tab)
            verify(of(d, CheckRow)[0].activeFocus, "then Trên")
            keyClick(Qt.Key_Space)
            verify(!d.sides.top)
            button(d, "OK").forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            tryVerify(() => !d.visible)
            compare([doc.history.length, last(doc).cmds[0].top], [n + 1, false])
            d = show(rotateC)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !d.visible)
            compare(doc.history.length, n + 1)
        }

        // ---- the artboards: sizes and places within 2 px -------------------------------------------------------
        function test_artboard_fade() {
            const d = show(fadeC)
            size(d, 400, 230.2)
            near(d, text(d, "Opacity"), [18, 64.9, null, 16.2], "Opacity")
            near(d, of(d, NumberField)[0], [296, 60, 86, 26], "its field")
            near(d, of(d, SliderField)[0], [18, 92, 364, 3], "its slider")
            near(d, text(d, "Mode"), [18, 111.9, 60, 16.2], "Mode")
            near(d, of(d, Dropdown)[0], [88, 107, 294, 26], "its list")
            near(d, of(d, CheckRow)[0], [18, 145, null, 16.2], "Preview")
            near(d, button(d, "OK"), [300, 190.2, 84, 28], "OK")
        }
        function test_artboard_brush() {
            const d = show(brushC)
            size(d, 440, 233.8)
            near(d, root.find(d.contentItem, o => o instanceof Rectangle && o.width === 84), [18, 60, 84, 84], "the tip's box")
            near(d, root.find(d.contentItem, o => o instanceof Shape), [29, 71, 62, 62], "the dot")
            const cap = text(d, "5.000 px"), mid = cap.mapToItem(d.header, cap.width / 2, 0)
            verify(Math.abs(mid.x - 60) <= 2 && Math.abs(mid.y - 150) <= 2, "the size centred under the box: " + [mid.x, mid.y])
            near(d, text(d, "Tên"), [118, 60, null, 16.2], "Tên")
            near(d, of(d, TextBox)[0], [118, 84.2, 304, 26], "the name")
            near(d, text(d, "Lưu vào nhóm Của tôi trong panel Brushes."), [118, 118.2, 304, 14.8], "the note")
            near(d, button(d, "OK"), [340, 193.8, 84, 28], "OK")
        }
        function test_artboard_rotate() {
            const d = show(rotateC), r = of(d, Radio)
            size(d, 360, 253.1)
            near(d, text(d, "Góc"), [18, 67.9, 60, 16.2], "Góc")
            near(d, of(d, NumberField)[0], [88, 62, 96, 28], "its field")
            near(d, r[0], [88, 102, null, 16.2], "clockwise")
            near(d, r[1], [88, 126.2, null, 16.2], "counterclockwise")
            near(d, text(d, "Canvas nới ra cho vừa ảnh đã xoay; phần mới của Background tô màu sau."), [18, 154.4, 324, 29.7], "the note, two lines")
            near(d, button(d, "OK"), [260, 213.1, 84, 28], "OK")
        }
        function test_artboard_trim() {
            const d = show(trimC), r = of(d, Radio), c = of(d, CheckRow)
            size(d, 400, 304.3)
            near(d, text(d, "Dựa trên"), [18, 62, null, 14.2], "Dựa trên")
            near(d, r[0], [18, 88.2, null, 16.2], "Điểm ảnh trong suốt")
            near(d, r[1], [18, 112.4, null, 16.2], "góc trên trái")
            near(d, r[2], [18, 136.5, null, 16.2], "góc dưới phải")
            near(d, text(d, "Cắt bỏ"), [18, 168.7, null, 14.2], "Cắt bỏ")
            near(d, c[0], [18, 194.9, 173, 16.2], "Trên")
            near(d, c[1], [209, 194.9, 173, 16.2], "Trái")
            near(d, c[2], [18, 219.1, 173, 16.2], "Dưới")
            near(d, c[3], [209, 219.1, 173, 16.2], "Phải")
            near(d, button(d, "OK"), [300, 264.3, 84, 28], "OK")
        }

        // ---- from the window (U4) ------------------------------------------------------------------------------
        function window() {
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function topDialog(w) { return w.dialogs[w.dialogs.length - 1] || null }
        function test_menus() {  // each item opens its dialog; Fade by its key too, and not without a step to fade
            const w = window(), items = {}
            const walk = list => {
                for (const it of list) {
                    if (Array.isArray(it.sub)) walk(it.sub)
                    if (it.dialog) items[it.l] = it
                }
            }
            for (const m of MenuSpec.MENUS) walk(m.items)
            compare([items["Fade…"].k, items["Fade…"].fade, items["Fade…"].req], ["Ctrl ⇧ F", true, "undo"])
            Docs.current.run({ op: "select", shape: "rect" })
            for (const [l, name, title] of [["Fade…", "Fade", "Fade Rectangular Marquee"], ["Define Brush Preset…", "DefineBrush", "Brush Name"],
                                            ["Tùy ý…", "RotateCanvas", "Rotate Canvas"], ["Trim…", "Trim", "Trim"]]) {
                const it = items[l]
                verify(it, l)
                compare(it.dialog, name, l)
                verify(w.can(it), l)
                verify(w.act(it), l)
                const d = topDialog(w)
                tryVerify(() => d.opened, 1000, l)
                compare(d.title, title, l)
                d.close()
                tryVerify(() => !topDialog(w), 1000, l)
            }
            keySequence("Ctrl+Shift+F")
            let d = null
            tryVerify(() => (d = topDialog(w)) && d.opened)
            verify(d instanceof FadeDialog)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !topDialog(w))
            Docs.current.jumpTo(0)
            verify(!w.can(items["Fade…"]), "no step to fade")
        }

        // ---- Liquify's Mask Options ----------------------------------------------------------------------------
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
        function test_liquify_mask() {
            const w = window()
            keySequence("Ctrl+Shift+X")
            const s = w.space
            verify(s instanceof LiquifySpace)
            tryCompare(Tools, "tool", s.startTool)
            keyClick(Qt.Key_W)  // Forward Warp: no face drawn over the photo
            waitForRendering(w.contentItem)
            const a = s.overlay.find(o => o instanceof Ants), d = s.doc
            const button = t => root.find(s, o => o instanceof Btn && o.visible && o.text === t)
            const centre = () => {  // the colour at the document's centre, in the window
                const q = View.toCanvas(s.view, s.size, { x: d.w / 2, y: d.h / 2 }), p = a.mapToItem(w.contentItem, q.x, q.y)
                return grabImage(w.contentItem).pixel(Math.round(p.x), Math.round(p.y))
            }
            compare(a.maskPath, "", "nothing frozen")
            const c0 = centre()
            click(button("Mask All"))
            verify(s.get("frozen", false))
            verify(a.maskPath !== "" && a.visible)
            verify(Qt.colorEqual(a.mask, Qt.rgba(1, 0, 0, 0.5)), "Mask Color Red at half")
            tryVerify(() => {
                const c = centre()
                return Math.abs(c.r - (1 + c0.r) / 2) < 0.03 && Math.abs(c.g - c0.g / 2) < 0.03 && Math.abs(c.b - c0.b / 2) < 0.03
            }, 2000, "the photo under red at half")
            click(button("Invert All"))
            compare([s.get("frozen", false), a.maskPath], [false, ""])
            tryVerify(() => Qt.colorEqual(centre(), c0), 2000, "the photo again")
            click(button("Invert All"))
            verify(s.get("frozen", false))
            click(button("None"))
            compare([s.get("frozen", false), a.maskPath], [false, ""])
            click(button("Mask All"))
            s.put({ vMask: false })
            compare(a.maskPath, "", "Show Mask off")
            s.put({ vMask: true, maskColor: "Green" })
            verify(a.maskPath !== "" && Qt.colorEqual(a.mask, Qt.rgba(0, 1, 0, 0.5)), "Mask Color")
            const n = d.history.length
            keyClick(Qt.Key_Escape)  // Hủy: the document as it was
            tryVerify(() => !w.space)
            compare(d.history.length, n)
        }
    }
}
