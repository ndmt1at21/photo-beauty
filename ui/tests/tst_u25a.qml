import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtTest
import PhotoBeauty
import "../qml/MenuSpec.js" as MenuSpec

// U25a: Select › Color Range… and Select › Modify › Border, Smooth, Expand, Contract, Feather (docs/ui-tasks.md), drawn
// on the canvas of build/design2/project (Main.dc.html, SelectModify.dc.html); made straight from their files as Main
// would, on Docs.current (chân dung.psd), then from the menus, the key and Properties' mask page.
Item {
    id: root
    width: 1280
    height: 900

    Component { id: colorRangeC; ColorRangeDialog {} }
    Component { id: modifyC; ModifySelectionDialog {} }
    Component { id: mainC; Main {} }
    Component { id: checkRowC; CheckRow { text: "Apply effect at canvas bounds" } }

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
        name: "U25a"
        when: windowShown

        // The artboards' dialogs: title, label, default, min, max (SelectModify.dc.html's D() rows), the step.
        readonly property var kinds: ({
            border: ["Border Selection", "Width", 10, 1, 200, "Border"],
            smooth: ["Smooth Selection", "Sample Radius", 5, 1, 500, "Smooth"],
            expand: ["Expand Selection", "Expand By", 5, 1, 500, "Expand"],
            contract: ["Contract Selection", "Contract By", 5, 1, 500, "Contract"],
            feather: ["Feather Selection", "Feather Radius", 2, 0.1, 1000, "Feather"]
        })

        function init() {
            Docs.reset()
            Tools.reset()
        }

        function show(c, params) {
            const d = createTemporaryObject(c, root, { params: params || {} })
            d.open()
            tryVerify(() => d.opened)
            return d
        }
        function fields(d) { return root.all(d.contentItem, o => o instanceof NumberField && o.visible) }
        function sliders(d) { return root.all(d.contentItem, o => o instanceof SliderField) }
        function drops(d) { return root.all(d.contentItem, o => o instanceof Dropdown) }
        function checkRow(d, text) { return root.find(d.contentItem, o => o instanceof CheckRow && o.text === text) }
        function radio(d, text) { return root.find(d.contentItem, o => o instanceof Radio && o.text === text) }
        function droppers(d) { return root.all(d.contentItem, o => o instanceof IconButton) }
        function mark(b) { return root.find(b, o => o instanceof Text && !(o instanceof Icon)) }  // + or −
        function mask(d) { return root.find(d.contentItem, o => o instanceof Canvas) }
        function button(d, text) { return root.find(d.footer, o => o instanceof Btn && o.text === text) }
        function input(f) { return root.find(f, o => o instanceof T.TextField) }
        function type(f, text) {  // click into the field, the text over its value, Enter
            mouseClick(input(f))
            tryVerify(() => f.editing)
            input(f).text = text
            keyClick(Qt.Key_Return)
            verify(!f.editing)
        }
        function pick(dd, value) {  // an option of a Dropdown, as its list gives it (U2 tests the list)
            const i = dd.pairs.findIndex(o => o[0] === value)
            verify(i >= 0, value)
            dd.activated(i)
            compare(dd.value, value)
        }
        function ok(d) {
            mouseClick(button(d, "OK"))
            tryVerify(() => !d.visible)
        }
        function last(doc) { return doc.history[doc.index] }

        // ---- Color Range -----------------------------------------------------------------------------------------
        function test_color_range_opens() {
            const d = show(colorRangeC), dd = drops(d)
            compare(d.title, "Color Range")
            compare(dd.map(o => o.value), ["sampledColors", "none"])
            compare(dd[0].pairs.map(o => o[1]), ["Sampled Colors", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas", "Highlights", "Midtones", "Shadows",
                                                 "Skin Tones", "Out Of Gamut"])
            compare(dd[1].pairs.map(o => o[1]), ["None", "Grayscale", "Black Matte", "White Matte", "Quick Mask"])
            compare(fields(d).map(f => [f.value, f.min, f.max, f.unit]), [[40, 0, 200, ""], [100, 0, 100, "%"]])
            compare(sliders(d).map(s => [s.value, s.min, s.max]), [[40, 0, 200], [100, 0, 100]])
            verify(!checkRow(d, "Detect Faces").enabled && !checkRow(d, "Localized Color Clusters").checked)
            verify(radio(d, "Selection").checked && !radio(d, "Image").checked && !checkRow(d, "Invert").checked)
            compare(droppers(d).map(b => [b.icon.name, b.on, b.Accessible.name, b.ToolTip.text]),
                    [["colorize", true, "Lấy mẫu màu", "Lấy mẫu"], ["colorize", false, "Thêm vào mẫu", "Thêm vào mẫu (Shift)"],
                     ["colorize", false, "Bớt khỏi mẫu", "Bớt khỏi mẫu (Alt)"]])
            compare(droppers(d).map(b => mark(b).text), ["", "+", "−"])
        }
        function test_color_range_controls() {  // dropdowns change; Detect Faces with Skin Tones only; Range with Localized only; fields clamp
            const d = show(colorRangeC), dd = drops(d), faces = checkRow(d, "Detect Faces"), range = fields(d)[1].parent.parent
            verify(!faces.enabled)
            compare(faces.opacity, 0.4)
            pick(dd[0], "skinTones")
            compare(d.pick, "skinTones")
            verify(faces.enabled)
            compare(faces.opacity, 1)
            mouseClick(faces)
            verify(faces.checked && d.faces)
            pick(dd[0], "reds")
            verify(!faces.enabled && !faces.checked, "off with any other pick")
            pick(dd[1], "quickMask")
            compare(d.matte, "quickMask")

            verify(!range.enabled && !fields(d)[1].enabled && !sliders(d)[1].enabled)
            compare(range.opacity, 0.4)
            mouseClick(checkRow(d, "Localized Color Clusters"))
            verify(d.localized && range.enabled && fields(d)[1].enabled)
            compare(range.opacity, 1)

            const fz = fields(d)[0], rg = fields(d)[1]
            type(fz, "250")
            compare(d.fuzziness, 200)
            type(fz, "−5")
            compare(d.fuzziness, 0)
            type(fz, "73")
            compare([d.fuzziness, sliders(d)[0].value], [73, 73])
            type(rg, "150")
            compare(d.range, 100)
            type(rg, "35")
            compare(d.range, 35)
            const s = sliders(d)[0]
            mouseClick(s, Math.round(5.5 + 0.5 * (s.width - 11)), 1)
            compare([d.fuzziness, fz.value], [100, 100])
            mouseClick(checkRow(d, "Localized Color Clusters"))
            verify(!range.enabled)

            const b = droppers(d)
            mouseClick(b[2])
            compare(b.map(x => x.on), [false, false, true])
            mouseClick(b[1])
            compare(b.map(x => x.on), [false, true, false])
            compare(d.dropper, 1)
        }
        // The preview: the photo as a grey mask in Selection (Rec. 709 grey, brighter by the pick, harder as Fuzziness
        // falls, Invert turns it over), the photo itself in Image. Read from the window, as drawn.
        function test_color_range_preview() {
            const d = show(colorRangeC), m = mask(d), pv = m.parent
            compare([pv.width, pv.height], [520, 300])
            compare([m.width, m.height, m.scale], [260, 150, 2], "worked out at half size")
            const grid = () => {
                tryVerify(() => m.ready, 5000)
                const img = grabImage(root), out = []
                for (const fy of [0.15, 0.35, 0.55, 0.75, 0.9])
                    for (const fx of [0.1, 0.3, 0.5, 0.7, 0.9]) {
                        const p = pv.mapToItem(root, fx * pv.width, fy * pv.height), x = Math.round(p.x), y = Math.round(p.y)
                        out.push([img.red(x, y), img.green(x, y), img.blue(x, y)])
                    }
                return out
            }
            const hard = v => v.reduce((s, c) => s + Math.abs(c[0] - 127.5), 0) / v.length  // how far from mid grey
            const mean = v => v.reduce((s, c) => s + c[0], 0) / v.length
            const a = grid()
            verify(a.every(c => c[0] === c[1] && c[1] === c[2]), "grey: " + JSON.stringify(a))
            verify(Math.min(...a.map(c => c[0])) < 160 && Math.max(...a.map(c => c[0])) > 250, "a mask, not a flat fill: " + JSON.stringify(a))
            // The artboard's grayscale(1) brightness(1.25) contrast(1 + 160 / 45) [invert(1)], worked out apart (Python).
            compare([0, 100, 110, 255].map(l => d.level(l)), [0, 116, 173, 255])

            d.invert = true
            compare(d.level(100), 139)
            const inv = grid()
            verify(inv.every((c, i) => Math.abs(c[0] + a[i][0] - 255) <= 2), "Invert: " + JSON.stringify(a) + " " + JSON.stringify(inv))
            d.invert = false

            d.fuzziness = 200
            const soft = grid()
            d.fuzziness = 0
            const sharp = grid()
            verify(hard(sharp) >= hard(a) && hard(a) > hard(soft) + 10, "contrast by Fuzziness: " + [hard(sharp), hard(a), hard(soft)])
            d.fuzziness = 40
            pick(drops(d)[0], "shadows")
            compare(d.level(200), 48)
            const dark = mean(grid())
            pick(drops(d)[0], "highlights")
            compare(d.level(80), 130)
            verify(mean(grid()) > dark + 40, "Highlights brighter than Shadows")

            mouseClick(radio(d, "Image"))
            verify(d.showImage && !m.visible)
            const img = grabImage(root), p = pv.mapToItem(root, pv.width / 2, pv.height / 2)
            const photo = [];
            [0.2, 0.4, 0.5, 0.6, 0.8].forEach(f => {
                const q = pv.mapToItem(root, f * pv.width, 0.45 * pv.height)
                photo.push([img.red(Math.round(q.x), Math.round(q.y)), img.blue(Math.round(q.x), Math.round(q.y))])
            })
            verify(photo.some(c => Math.abs(c[0] - c[1]) > 20), "the photo in colour: " + JSON.stringify(photo))
            mouseClick(radio(d, "Selection"))
            verify(!d.showImage && m.visible)
        }
        function test_color_range_ok() {  // one step "Color Range": the subject, or skin for Skin Tones; Invert in the same step
            const doc = Docs.current, n = doc.history.length
            let d = show(colorRangeC)
            ok(d)
            compare(doc.history.length, n + 1)
            compare([last(doc).name, last(doc).icon, doc.selection, doc.inverse], ["Color Range", "select", "subject", false])
            const c = last(doc).cmds[0]
            compare([c.select, c.fuzziness, c.localized, c.range, c.detectFaces, c.invert], ["sampledColors", 40, false, undefined, false, false])

            d = show(colorRangeC)
            pick(drops(d)[0], "skinTones")
            mouseClick(checkRow(d, "Detect Faces"))
            mouseClick(checkRow(d, "Localized Color Clusters"))
            type(fields(d)[1], "60")
            mouseClick(checkRow(d, "Invert"))
            ok(d)
            compare(doc.history.length, n + 2, "one step")
            compare([last(doc).name, doc.selection, doc.inverse, last(doc).cmds.length], ["Color Range", "skin", true, 2])
            const e = last(doc).cmds[0]
            compare([e.select, e.localized, e.range, e.detectFaces, e.invert], ["skinTones", true, 60, true, true])

            for (const how of ["Hủy", "Esc", "×"]) {  // nothing
                d = show(colorRangeC)
                pick(drops(d)[0], "reds")
                if (how === "Hủy")
                    mouseClick(button(d, "Hủy"))
                else if (how === "Esc")
                    keyClick(Qt.Key_Escape)
                else
                    mouseClick(root.find(d.header, o => o instanceof IconButton))
                tryVerify(() => !d.visible, 1000, how)
                compare([doc.history.length, doc.selection], [n + 2, "skin"], how)
            }
        }

        // ---- Select › Modify ---------------------------------------------------------------------------------------
        function test_modify_dialogs() {  // each kind: its title, label, range, default; the bounds box but for Border; OK one step
            const doc = Docs.current
            doc.run({ op: "select", shape: "rect" })
            for (const k of Object.keys(kinds)) {
                const s = kinds[k], n = doc.history.length
                let d = show(modifyC, { kind: k }), f = fields(d)[0]
                compare(d.title, s[0])
                verify(root.find(d.contentItem, o => o instanceof Text && o.text === s[1]), k)
                compare([f.value, f.min, f.max, f.unit, f.decimals], [s[2], s[3], s[4], "px", k === "feather" ? 1 : 0], k)
                const box = checkRow(d, "Apply effect at canvas bounds")
                compare(box.visible, k !== "border", k)
                type(f, String(s[4] * 2))
                compare(d.px, s[4], k + ": clamped to the max")
                type(f, "0")
                compare(d.px, s[3], k + ": clamped to the min")
                type(f, "7")
                if (box.visible)
                    mouseClick(box)
                ok(d)
                compare(doc.history.length, n + 1, k)
                compare([last(doc).name, last(doc).icon, doc.selection], [s[5], "select", "rect"], k)
                const c = last(doc).cmds[0]
                compare([c.op, c.modify, c.px, c.canvasBounds], ["step", k, 7, k !== "border"], k)

                d = show(modifyC, { kind: k })
                type(fields(d)[0], "9")
                mouseClick(button(d, "Hủy"))
                tryVerify(() => !d.visible)
                compare(doc.history.length, n + 1, k + ": Hủy, nothing")
            }
            compare(show(modifyC).title, "Feather Selection", "no kind: Feather")
            compare(input(fields(show(modifyC, { kind: "feather" }))[0]).text, "2,0 px")
        }
        function test_modify_keys() {  // Enter is OK, Esc leaves
            const doc = Docs.current
            doc.run({ op: "select", shape: "rect" })
            const n = doc.history.length
            let d = show(modifyC, { kind: "expand" })
            keyClick(Qt.Key_Escape)
            tryVerify(() => !d.visible)
            compare(doc.history.length, n)
            d = show(modifyC, { kind: "expand" })
            keyClick(Qt.Key_Return)
            tryVerify(() => !d.visible)
            compare([doc.history.length, last(doc).name], [n + 1, "Expand"])
        }

        function test_check_row_wrap() {  // CheckRow.wrapWidth (U2's control): 0 one 16 px line; else the label wraps in that width
            const c = createTemporaryObject(checkRowC, root), label = root.find(c, o => o instanceof Text), tick = label.parent.children[0]
            compare([c.height, label.lineCount], [16, 1])
            verify(c.width > 160, "one line: " + c.width)
            c.spacing = 8
            c.wrapWidth = 154
            tryCompare(c, "width", 154)  // the Row lays out at polish
            compare(label.lineCount, 2)
            near([c.height], [32.4], "two lines of 12 × 1.35")
            near([tick.y], [(c.height - 13) / 2], "the box in the middle")
            c.wrapWidth = 0
            compare([c.height, label.lineCount], [16, 1])
        }

        // ---- size and place, against the artboards (Chrome's boxes of build/shots/u25a/proto/*.boxes.json) ----------
        function box(d, item) {  // x, y, width, height in the dialog's box
            const p = item.mapToItem(d.contentItem.parent, 0, 0)
            return [p.x, p.y, item.width, item.height]
        }
        function near(a, e, what) {
            verify(a.length === e.length && a.every((v, i) => e[i] === null || Math.abs(v - e[i]) <= 2), what + ": " + a.map(v => Math.round(v * 10) / 10) + " vs " + e)
        }
        function test_color_range_geometry() {
            const d = show(colorRangeC), dd = drops(d), nf = fields(d), sl = sliders(d), b = droppers(d)
            near([d.width, d.height], [600, 655.4], "the box")
            near(box(d, dd[0]), [98, 60, 440, 26], "Chọn")
            near(box(d, root.find(d.contentItem, o => o instanceof Text && o.text === "Chọn")).slice(0, 1), [18], "Chọn's label")
            near(box(d, checkRow(d, "Detect Faces")), [98, 98, 97.5, 16.2], "Detect Faces")
            near(box(d, checkRow(d, "Localized Color Clusters")), [213.5, 98, 159.2, 16.2], "Localized Color Clusters")
            near(box(d, nf[0]), [452, 126.2, 86, 26], "Fuzziness")
            near(box(d, sl[0]), [18, 158.2, 520, 3], "Fuzziness' track")
            near(box(d, nf[1]), [452, 173.2, 86, 26], "Range")
            near(box(d, sl[1]), [18, 205.2, 520, 3], "Range's track")
            near(box(d, mask(d).parent), [18, 220.2, 520, 300], "the preview")
            near(box(d, radio(d, "Selection")), [18, 532.2, 75.7, 16.2], "Selection")
            near(box(d, radio(d, "Image")), [111.7, 532.2, 55.8, 16.2], "Image")
            near(box(d, checkRow(d, "Invert")), [483.5, 532.2, 54.5, 16.2], "Invert")
            near(box(d, dd[1]), [148, 560.4, 390, 26], "Xem trước trên ảnh")
            near(box(d, root.find(d.contentItem, o => o instanceof Text && o.text === "Xem trước trên ảnh")).slice(0, 1), [18], "its label")
            near(box(d, b[0]), [552, 62, 30, 30], "Lấy mẫu")
            near(box(d, b[1]), [552, 98, 30, 30], "Thêm vào mẫu")
            near(box(d, b[2]), [552, 134, 30, 30], "Bớt khỏi mẫu")
            near(box(d, mark(b[1])).slice(0, 2), [20.2 + 552, 17 + 98], "the + mark")
            near(box(d, button(d, "OK")), [500, 615.4, 84, 28], "OK")
            near(box(d, button(d, "Hủy")), [441.4, 615.4, 50.6, 28], "Hủy")
        }
        function test_modify_geometry() {
            for (const k of Object.keys(kinds)) {
                const d = show(modifyC, { kind: k }), bounds = k !== "border"
                near([d.width, d.height], [300, bounds ? 203.4 : 159], k + ": the box")
                near(box(d, fields(d)[0]), [128, 62, 110, 28], k + ": the field")
                near(box(d, root.find(d.contentItem, o => o instanceof Text && o.text === kinds[k][1])).slice(0, 1), [18], k + ": the label")
                if (bounds)
                    near(box(d, checkRow(d, "Apply effect at canvas bounds")), [128, 102, 154, 32.4], k + ": Apply effect at canvas bounds")
                near(box(d, button(d, "OK")), [200, bounds ? 163.4 : 119, 84, 28], k + ": OK")
                d.close()
            }
        }

        // ---- from the window (U4): the menus, Feather's key, Properties' mask page ------------------------------------
        function window() {
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function topDialog(w) { return w.dialogs[w.dialogs.length - 1] || null }
        function test_menus_open_them() {
            const w = window(), items = []
            const walk = list => {
                for (const it of list) {
                    if (Array.isArray(it.sub)) walk(it.sub)
                    if (it.dialog === "ColorRange" || it.dialog === "ModifySelection") items.push(it)
                }
            }
            for (const m of MenuSpec.MENUS) walk(m.items)
            compare(items.map(it => [it.l, it.params ? it.params.kind : "", it.k || ""]),
                    [["Color Range…", "", ""], ["Border…", "border", ""], ["Smooth…", "smooth", ""], ["Expand…", "expand", ""], ["Contract…", "contract", ""],
                     ["Feather…", "feather", "⇧ F6"]])
            compare(items.map(it => w.can(it)), [true, false, false, false, false, false], "Modify needs a selection")
            Docs.current.run({ op: "select", shape: "rect" })
            for (const it of items) {
                verify(w.act(it), it.l)
                const d = topDialog(w)
                tryVerify(() => d.opened)
                compare(d.title, it.dialog === "ColorRange" ? "Color Range" : kinds[it.params.kind][0], it.l)
                d.close()
                tryVerify(() => !topDialog(w), 1000, it.l)
            }
            keySequence("Shift+F6")
            let d = null
            tryVerify(() => (d = topDialog(w)) && d.opened)
            compare(d.title, "Feather Selection")
            keyClick(Qt.Key_Escape)
            tryVerify(() => !topDialog(w))
        }
        function test_mask_page_button() {  // Properties' Color Range… on a layer mask
            const w = window(), doc = Docs.current, p = root.find(w.contentItem, o => o instanceof PropertiesPanel)
            verify(p)
            doc.run({ op: "selectLayers", layers: [doc.layers.find(l => l.props.name === "Làm trắng răng").id] })
            doc.setEditMask(true)
            tryCompare(p, "page", "mask")
            const b = root.find(p, o => o instanceof Btn && o.text === "Color Range…")
            verify(b.enabled)
            b.clicked()
            let d = null
            tryVerify(() => (d = topDialog(w)) && d.opened)
            verify(d instanceof ColorRangeDialog)
        }
    }
}
