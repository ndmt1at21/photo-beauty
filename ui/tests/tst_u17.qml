import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtTest
import PhotoBeauty
import "../qml/FilterSpec.js" as Spec
import "../qml/MenuSpec.js" as MenuSpec

// U17: Image Size, Canvas Size, Fill, Stroke and the filter dialogs (docs/ui-tasks.md), each made straight from its
// file as Main would, on Docs.current (chân dung.psd: 4.400 × 5.600 px, 300 ppi, RGB/16).
Item {
    id: root
    width: 1280
    height: 900

    Component { id: imageSizeC; ImageSizeDialog {} }
    Component { id: canvasSizeC; CanvasSizeDialog {} }
    Component { id: fillC; FillDialog {} }
    Component { id: strokeC; StrokeDialog {} }
    Component { id: filterC; FilterDialog {} }
    Component { id: mainC; Main {} }
    Component { id: radioC; Radio {} }
    Component { id: colorButtonC; ColorButton {} }
    Component {  // U11's Color Picker, as Main.openDialog would make it
        id: pickerC
        DialogBox {
            property var params: ({})
            title: "Picker"
        }
    }

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
        name: "U17"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
        }

        function show(c, params) {
            const d = createTemporaryObject(c, root, { params: params || {} })
            d.open()
            tryVerify(() => d.opened)  // the enter transition rendered: laid out (waitForRendering would wait for a frame no more)
            return d
        }
        function fields(d) { return root.all(d.contentItem, o => o instanceof NumberField && o.visible) }
        function drops(d) { return root.all(d.contentItem, o => o instanceof Dropdown) }
        function checkRow(d, text) { return root.find(d.contentItem, o => o instanceof CheckRow && o.text === text) }
        function button(d, text) { return root.find(d.footer, o => o instanceof Btn && o.text === text) }
        function texts(d) { return root.all(d.contentItem, o => o instanceof Text && o.visible).map(o => o.text) }
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

        // ---- Image Size ----------------------------------------------------------------------------------------
        function test_image_size_opens() {  // as the document is
            const doc = Docs.current, d = show(imageSizeC)
            compare(d.title, "Image Size")
            compare(fields(d).map(f => f.value), [4400, 5600, 300])
            compare(drops(d).map(o => o.value), [0, "px", "px", "in", "automatic"])
            compare(drops(d)[0].displayText, "Gốc")
            const t = texts(d)
            verify(t.indexOf("148 MB") >= 0, t)
            verify(t.indexOf("4.400 × 5.600 px") >= 0, t)
            verify(t.indexOf(" (trước ") < 0, "nothing changed yet")
            verify(checkRow(d, "Resample").checked)
            verify(!checkRow(d, "Scale Styles").enabled, "no layer styles")
            verify(root.find(d.contentItem, o => o instanceof Image && o.visible).source.toString().endsWith("chandung2.jpg"))
            compare(doc.history.length, 10)
        }
        function test_image_size_units() {  // đổi đơn vị: the field shows the same size in the new unit
            const d = show(imageSizeC), dd = drops(d)
            pick(dd[1], "in")
            compare(fields(d)[0].value, 14.67)
            compare(fields(d)[0].decimals, 2)
            compare(fields(d)[1].value, 5600, "H keeps its own unit")
            pick(dd[2], "cm")
            compare(fields(d)[1].value, 47.41)
            pick(dd[1], "mm")
            compare(fields(d)[0].value, 372.53)
            pick(dd[1], "pt")
            compare(fields(d)[0].value, 1056)
            pick(dd[1], "pc")
            compare(fields(d)[0].value, 88)
            pick(dd[1], "%")
            compare(fields(d)[0].value, 100)
            type(fields(d)[0], "50")
            compare(d.size, [2200, 2800], "50 % of 4.400, H chained")
            compare(fields(d)[1].value, 23.71)
            const t = texts(d)
            verify(t.indexOf("2.200 × 2.800 px") >= 0, t)
            verify(t.indexOf("37 MB") >= 0, t)
            verify(t.indexOf(" (trước ") >= 0 && t.indexOf("148 MB") >= 0, "the size before")
            pick(dd[3], "cm")
            compare(fields(d)[2].value, 118.11, "px per centimetre")
            type(fields(d)[2], "59,06")
            fuzzyCompare(d.ppi, 150, 0.02)
            compare(d.size.map(Math.round), [1100, 1400], "Resample keeps the print size")
            compare(drops(d)[0].value, Spec.FIT.length - 1, "Fit To: Tùy chỉnh")
        }
        function test_image_size_chain() {  // xích giữ tỉ lệ
            const d = show(imageSizeC), chain = root.find(d.contentItem, o => o instanceof IconButton && o.icon.name === "link")
            verify(chain.on && chain.enabled)
            type(fields(d)[0], "2200")
            compare(d.size, [2200, 2800])
            mouseClick(chain)
            verify(!d.linked && !chain.on)
            type(fields(d)[0], "1000")
            compare(d.size, [1000, 2800], "unchained: H stays")
            type(fields(d)[1], "1400")
            compare(d.size, [1000, 1400])
            mouseClick(chain)
            verify(d.linked)
            type(fields(d)[1], "2800")
            compare(d.size, [2000, 2800], "chained again at the new ratio")
            type(fields(d)[0], "400000")
            compare(d.size, [300000, 300000], "within 300.000 px")
        }
        function test_image_size_ok_is_one_step() {
            const doc = Docs.current, n = doc.history.length, d = show(imageSizeC)
            type(fields(d)[0], "2200")
            pick(drops(d)[4], "lanczos")
            ok(d)
            compare([doc.w, doc.h, doc.ppi], [2200, 2800, 300])
            compare(doc.history.length, n + 1)
            compare(last(doc).name, "Image Size")
            compare(last(doc).icon, "image")
            compare(last(doc).cmds[0].method, "lanczos")
            doc.undo()
            compare([doc.w, doc.h], [4400, 5600])
            const same = show(imageSizeC)
            ok(same)
            compare([doc.history.length, doc.index], [n + 1, n - 1], "nothing changed: no step, Làm lại still there")
        }
        function test_image_size_cancel() {  // Hủy, Esc, ×: nothing changes
            const doc = Docs.current, n = doc.history.length
            for (const how of ["cancel", "esc", "x"]) {
                const d = show(imageSizeC)
                type(fields(d)[0], "1000")
                if (how === "cancel") mouseClick(button(d, "Hủy"))
                else if (how === "esc") keyClick(Qt.Key_Escape)
                else mouseClick(root.find(d.header, o => o instanceof IconButton && o.icon.name === "close"))
                tryVerify(() => !d.visible, 1000, how)
                compare([doc.w, doc.h, doc.history.length], [4400, 5600, n], how)
            }
        }
        function test_image_size_resample_off() {  // the pixels stay; a print size sets the resolution
            const doc = Docs.current, d = show(imageSizeC)
            type(fields(d)[0], "2200")
            mouseClick(checkRow(d, "Resample"))
            compare(d.size, [4400, 5600], "back to the document's pixels")
            const chain = root.find(d.contentItem, o => o instanceof IconButton && o.icon.name === "link")
            verify(d.linked && !chain.enabled, "always chained")
            verify(!fields(d)[0].enabled && !fields(d)[1].enabled, "no pixels to type")
            verify(!drops(d)[4].enabled, "no method")
            pick(drops(d)[1], "in")
            verify(fields(d)[0].enabled)
            type(fields(d)[0], "7,33")
            fuzzyCompare(d.ppi, 600.27, 0.01)
            compare(fields(d)[2].value, 600)
            compare(d.size, [4400, 5600])
            ok(d)
            compare([doc.w, doc.h], [4400, 5600])
            fuzzyCompare(doc.ppi, 600.27, 0.01)
            compare(last(doc).name, "Image Size")
            verify(last(doc).cmds[0].method === undefined, "no resampling")
        }
        function test_image_size_fit_to() {
            const d = show(imageSizeC), fit = drops(d)[0]
            pick(fit, 1)  // 4 × 6 in 300 ppi
            compare(d.units, ["in", "in"])
            compare(d.ppi, 300)
            compare(d.size.map(Math.round), [1200, 1527], "within 1.200 × 1.800, chained")
            compare(fields(d).map(f => f.value), [4, 5.09, 300])
            pick(fit, 6)  // 1.920 × 1.080 px 72 ppi
            compare(d.units, ["px", "px"])
            compare(d.size.map(Math.round), [849, 1080])
            compare(d.ppi, 72)
            mouseClick(root.find(d.contentItem, o => o instanceof IconButton && o.icon.name === "link"))
            pick(fit, 4)  // A4 210 × 297 mm 300 ppi, unchained: exactly that
            compare(d.size.map(Math.round), [2480, 3508])
            pick(fit, 0)  // Gốc
            compare([d.size, d.ppi, d.units], [[4400, 5600], 300, ["px", "px"]])
        }

        // ---- Canvas Size ---------------------------------------------------------------------------------------
        function test_canvas_size_anchor() {  // Canvas Size theo neo
            const doc = Docs.current, n = doc.history.length, d = show(canvasSizeC)
            compare(d.title, "Canvas Size")
            compare(fields(d).map(f => f.value), [4400, 5600])
            verify(texts(d).indexOf("148 MB") >= 0)
            const grid = root.find(d.contentItem, o => o instanceof AnchorGrid)
            compare(grid.anchor, 4)
            compare(grid.cell, 26)
            mouseClick(checkRow(d, "Relative"))
            compare(fields(d).map(f => f.value), [0, 0], "the change")
            type(fields(d)[0], "400")
            compare(d.size, [4800, 5600])
            verify(texts(d).indexOf("161 MB") >= 0, "Cỡ mới")
            pick(drops(d)[0], "%")
            compare(fields(d)[0].value, 9.09)
            mouseClick(checkRow(d, "Relative"))
            compare(fields(d)[0].value, 109.09)
            const cells = root.all(grid, o => o instanceof T.AbstractButton)
            mouseClick(cells[0])
            compare(grid.anchor, 0, "top left")
            compare(d.extension, "bg")
            compare(d.extColor, Tools.bg)
            pick(drops(d)[2], "white")
            ok(d)
            compare([doc.w, doc.h], [4800, 5600])
            compare(doc.history.length, n + 1)
            const c = last(doc).cmds[0]
            compare([last(doc).name, c.op, c.anchor, c.color], ["Canvas Size", "canvasSize", 0, "#ffffff"])
        }
        function test_canvas_size_extension() {  // the colour of what is added
            const d = show(canvasSizeC), dd = drops(d)[2]
            Tools.setFg("#123456")
            pick(dd, "fg")
            compare(d.extColor, "#123456")
            pick(dd, "gray")
            compare(d.extColor, "#808080")
            pick(dd, "other")
            compare(d.extColor, "#e9c7b3")
            d.close()
            Docs.create({ w: 800, h: 600, background: "transparent" })  // no Background layer: nothing to colour
            const e = show(canvasSizeC)
            verify(!e.hasBackground)
            verify(!drops(e)[2].enabled && !root.find(e.contentItem, o => o instanceof Swatch).enabled)
            type(fields(e)[0], "1000")
            ok(e)
            compare(last(Docs.current).cmds[0].color, null)
        }

        // ---- Fill, Stroke --------------------------------------------------------------------------------------
        function test_fill() {
            const doc = Docs.current, n = doc.history.length, d = show(fillC)
            compare(d.title, "Fill")
            compare(drops(d).map(o => o.value), ["fg", "normal"])
            compare(drops(d)[0].pairs.map(o => o[1]), ["Màu trước", "Màu sau", "Màu…", "Lịch sử", "Đen", "Xám 50%", "Trắng"])
            compare(drops(d)[1].pairs.length, 29)
            compare(drops(d)[1].pairs.slice(0, 5).map(o => o[1]), ["Normal", "Dissolve", "Behind", "Clear", "Darken"])
            compare(fields(d).map(f => f.value), [100])
            const swatch = () => root.find(d.contentItem, o => o instanceof Swatch)
            verify(!swatch().visible)
            pick(drops(d)[0], "color")
            verify(swatch().visible)
            verify(Qt.colorEqual(swatch().color, "#e9c7b3"))
            pick(drops(d)[1], "multiply")
            type(fields(d)[0], "50")
            mouseClick(checkRow(d, "Preserve Transparency"))
            ok(d)
            compare(doc.history.length, n + 1)
            const c = last(doc).cmds[0]
            compare([last(doc).name, last(doc).icon], ["Fill", "format_color_fill"])
            compare([c.contents, c.color, c.mode, c.opacity, c.preserveTransparency], ["color", "#e9c7b3", "multiply", 0.5, true])
            const e = show(fillC)
            ok(e)
            compare([last(doc).cmds[0].contents, last(doc).cmds[0].color], ["fg", Tools.fg], "the foreground colour")
            const f = show(fillC)
            pick(drops(f)[0], "white")
            mouseClick(button(f, "Hủy"))
            tryVerify(() => !f.visible)
            compare(doc.history.length, n + 2, "Hủy: no step")
        }
        function test_stroke() {
            const doc = Docs.current, n = doc.history.length, d = show(strokeC)
            compare(d.title, "Stroke")
            compare(fields(d).map(f => f.value), [4, 100])
            compare(fields(d)[0].max, 250)
            verify(Qt.colorEqual(root.find(d.contentItem, o => o instanceof Swatch).color, Tools.fg))
            const radios = root.all(d.contentItem, o => o.objectName === "radio")
            compare(radios.map(r => r.text), ["Trong", "Giữa", "Ngoài"])
            compare(radios.map(r => r.checked), [false, true, false])
            mouseClick(radios[2])
            compare(radios.map(r => r.checked), [false, false, true])
            type(fields(d)[0], "10")
            type(fields(d)[1], "300")
            compare(fields(d)[1].value, 100, "Opacity at most 100 %")
            pick(drops(d)[0], "screen")
            ok(d)
            compare(doc.history.length, n + 1)
            const c = last(doc).cmds[0]
            compare([last(doc).name, c.width, c.color, c.location, c.mode, c.opacity], ["Stroke", 10, "#ffffff", "outside", "screen", 1])
        }

        // ---- filters -------------------------------------------------------------------------------------------
        // Photoshop's: [title, [key, default, min, max, decimals, unit] of the sliders, other rows' defaults]
        readonly property var expected: ({
            boxBlur: ["Box Blur", [["radius", 10, 1, 2000, 0, "px"]], {}],
            gaussianBlur: ["Gaussian Blur", [["radius", 4, 0.1, 1000, 1, "px"]], {}],
            motionBlur: ["Motion Blur", [["angle", 0, -360, 360, 0, "°"], ["distance", 10, 1, 2000, 0, "px"]], {}],
            surfaceBlur: ["Surface Blur", [["radius", 5, 1, 100, 0, "px"], ["threshold", 15, 2, 255, 0, "mức"]], {}],
            addNoise: ["Add Noise", [["amount", 4, 0.1, 400, 1, "%"]], { distribution: "gaussian", monochromatic: true }],
            dustScratches: ["Dust & Scratches", [["radius", 1, 1, 500, 0, "px"], ["threshold", 0, 0, 255, 0, "mức"]], {}],
            median: ["Median", [["radius", 1, 1, 500, 0, "px"]], {}],
            reduceNoise: ["Reduce Noise", [["strength", 6, 0, 10, 0, ""], ["preserveDetails", 60, 0, 100, 0, "%"], ["reduceColorNoise", 45, 0, 100, 0, "%"],
                ["sharpenDetails", 25, 0, 100, 0, "%"]], { removeJpegArtifact: false }],
            unsharpMask: ["Unsharp Mask", [["amount", 120, 1, 500, 0, "%"], ["radius", 1.2, 0.1, 1000, 1, "px"], ["threshold", 3, 0, 255, 0, "mức"]], {}],
            custom: ["Custom", [], { kernel: [0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, -1, 5, -1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0], scale: 1, offset: 0 }],
            highPass: ["High Pass", [["radius", 10, 0.1, 1000, 1, "px"]], {}],
            maximum: ["Maximum", [["radius", 1, 1, 500, 0, "px"]], { preserve: "squareness" }],
            minimum: ["Minimum", [["radius", 1, 1, 500, 0, "px"]], { preserve: "squareness" }],
            offset: ["Offset", [["horizontal", 0, -30000, 30000, 0, "px"], ["vertical", 0, -30000, 30000, 0, "px"]], { undefinedAreas: "wrap" }]
        })
        function test_filters_open_with_their_values() {  // mỗi bộ lọc: đúng thông số và mặc định, OK là một bước
            const doc = Docs.current
            compare(Object.keys(Spec.FILTERS), Object.keys(expected), "the Filter menu's … items")
            for (const key in expected) {
                const [title, sliders, others] = expected[key]
                const n = doc.history.length, d = show(filterC, { filter: key })
                compare(d.title, title)
                compare(d.boxWidth, key === "custom" ? 440 : 400)
                const nf = fields(d), sf = root.all(d.contentItem, o => o instanceof SliderField)
                compare(sf.length, sliders.length, key)
                sliders.forEach((s, i) => {
                    compare([nf[i].value, nf[i].min, nf[i].max, nf[i].decimals, nf[i].unit], s.slice(1), key + " " + s[0])
                    compare([sf[i].value, sf[i].min, sf[i].max], s.slice(1, 4), key + " " + s[0])
                    compare(d.values[s[0]], s[1])
                })
                for (const k in others)
                    compare(d.values[k], others[k], key + " " + k)
                const radios = root.all(d.contentItem, o => o.objectName === "radio" && o.checked)
                compare(radios.length, "distribution" in others || "undefinedAreas" in others ? 1 : 0, key)
                if ("distribution" in others) compare(radios[0].text, "Gaussian")
                if ("undefinedAreas" in others) compare(radios[0].text, "Wrap Around")
                if ("monochromatic" in others) verify(checkRow(d, "Monochromatic").checked)
                if ("removeJpegArtifact" in others) verify(!checkRow(d, "Remove JPEG Artifact").checked)
                if ("preserve" in others) compare(drops(d)[0].displayText, "Squareness")
                if (key === "custom") {
                    compare(nf.length, 27, "25 cells, Scale, Offset")
                    compare(nf.map(f => f.value), others.kernel.concat([1, 0]))
                    compare(input(nf[7]).text, "−1")
                }
                verify(button(d, "OK").enabled)
                ok(d)
                compare(doc.history.length, n + 1, key)
                compare([last(doc).name, last(doc).icon, last(doc).cmds[0].filter], [title, "filter", key])
                compare(last(doc).cmds[0].values, Spec.defaults(Spec.FILTERS[key]))
            }
        }
        function test_filter_values_and_cancel() {
            const doc = Docs.current, n = doc.history.length, d = show(filterC, { filter: "unsharpMask" })
            const nf = fields(d), sf = root.all(d.contentItem, o => o instanceof SliderField)
            type(nf[0], "250")
            compare(sf[0].value, 250, "the slider follows the field")
            sf[2].forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Right)
            compare(nf[2].value, 4, "and the field the slider")
            type(nf[1], "2000")
            compare(nf[1].value, 1000, "Radius at most 1.000 px")
            compare([d.values.amount, d.values.radius, d.values.threshold], [250, 1000, 4], "every change kept")
            mouseClick(button(d, "Hủy"))
            tryVerify(() => !d.visible)
            compare(doc.history.length, n)
            const e = show(filterC, { filter: "unsharpMask" })
            compare(e.values.amount, 120, "opens at the defaults again")
            const radio = show(filterC, { filter: "addNoise" })
            mouseClick(root.find(radio.contentItem, o => o.objectName === "radio" && o.text === "Uniform"))
            mouseClick(checkRow(radio, "Monochromatic"))
            ok(radio)
            compare([last(doc).cmds[0].values.distribution, last(doc).cmds[0].values.monochromatic], ["uniform", false])
            const custom = show(filterC, { filter: "custom" })
            type(fields(custom)[12], "9")
            type(fields(custom)[25], "5")
            ok(custom)
            compare(last(doc).cmds[0].values.kernel[12], 9)
            compare(last(doc).cmds[0].values.scale, 5)
            const max = show(filterC, { filter: "maximum" })
            pick(drops(max)[0], "roundness")
            ok(max)
            compare(last(doc).cmds[0].values.preserve, "roundness")
        }
        function test_filter_preview() {  // − + zoom, held for the original, Preview
            const d = show(filterC, { filter: "gaussianBlur" })
            const zoom = () => root.find(d.contentItem, o => o instanceof Text && /%$/.test(o.text) && o.font.family === Theme.mono).text
            compare(zoom(), "100%")
            mouseClick(root.find(d.contentItem, o => o instanceof Btn && o.text === "+"))
            compare(zoom(), "200%")
            const minus = root.find(d.contentItem, o => o instanceof Btn && o.text === "−")
            mouseClick(minus)
            mouseClick(minus)
            compare(zoom(), "66%")
            for (let i = 0; i < 6; i++) mouseClick(minus)
            compare(zoom(), "6%")
            verify(!minus.enabled, "the smallest")
            const img = root.find(d.contentItem, o => o instanceof Image)
            compare(img.transform[0].xScale, 0.06)
            verify(img.source.toString().endsWith("chandung2.jpg"))
            const pv = img.parent
            verify(!d.comparing)
            mousePress(pv, pv.width / 2, pv.height / 2)
            verify(d.comparing, "the original while held")
            mouseRelease(pv, pv.width / 2, pv.height / 2)
            verify(!d.comparing)
            const p = checkRow(d, "Preview")
            verify(p.checked)
            mouseClick(p)
            verify(!d.preview && !p.checked)
        }
        function test_filter_unknown() {
            const d = show(filterC, { filter: "nope" })
            compare(d.title, "")
            verify(!button(d, "OK").enabled)
            compare(fields(d).length, 0)
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
        function menuRow(m, label) {
            for (let i = 0; i < m.count; i++)
                if (m.itemAt(i) instanceof T.MenuItem && m.itemAt(i).text === label)
                    return m.itemAt(i)
            return null
        }
        function test_menus_open_them() {  // every item of MenuSpec that names one of them, with its params
            const w = window(), items = []
            const walk = list => {
                for (const it of list) {
                    if (Array.isArray(it.sub)) walk(it.sub)
                    if (["ImageSize", "CanvasSize", "Fill", "Stroke", "Filter"].indexOf(it.dialog) >= 0) items.push(it)
                }
            }
            for (const m of MenuSpec.MENUS) walk(m.items)
            compare(items.filter(it => it.dialog === "Filter").map(it => it.params.filter), Object.keys(Spec.FILTERS), "the Filter menu's … items, FilterSpec's keys")
            compare(items.length, 18)
            Docs.current.run({ op: "select", shape: "rect" })  // Stroke's
            const titles = { ImageSize: "Image Size", CanvasSize: "Canvas Size", Fill: "Fill", Stroke: "Stroke" }
            for (const it of items) {
                verify(w.can(it), it.l)
                verify(w.act(it), it.l)
                const d = topDialog(w)
                tryVerify(() => d.opened)
                compare(d.title, it.dialog === "Filter" ? Spec.FILTERS[it.params.filter].title : titles[it.dialog], it.l)
                d.close()
                tryVerify(() => !topDialog(w), 1000, it.l)
            }
        }
        function test_menu_to_step() {  // Filter › Blur › Gaussian Blur…, OK: its step; Ctrl Alt I: Image Size
            const w = window(), doc = Docs.current, n = doc.history.length
            const bar = root.find(w.contentItem, o => o instanceof AppMenu), m = bar.menuAt(6)
            mouseClick(bar.itemAt(6))
            tryVerify(() => m.opened)
            waitForRendering(w.contentItem)
            const blur = menuRow(m, "Blur")
            mouseClick(blur)
            tryVerify(() => blur.subMenu.opened)
            waitForRendering(w.contentItem)
            mouseClick(menuRow(blur.subMenu, "Gaussian Blur…"))
            let d = null
            tryVerify(() => (d = topDialog(w)) && d.opened)
            verify(d instanceof FilterDialog)
            compare(d.values.radius, 4)
            ok(d)
            compare(doc.history.length, n + 1)
            compare([last(doc).name, last(doc).cmds[0].filter], ["Gaussian Blur", "gaussianBlur"])
            keySequence("Ctrl+Alt+I")
            tryVerify(() => (d = topDialog(w)) && d.opened)
            verify(d instanceof ImageSizeDialog)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !topDialog(w))
            w.start("Filter:highPass")  // --state Filter:highPass
            tryVerify(() => (d = topDialog(w)) && d.opened)
            compare(d.title, "High Pass")
        }
        function test_color_picker() {  // a colour cell opens the Color Picker on its colour; the one picked comes back
            const w = window()
            w.types = Object.assign({}, w.types, { ColorPickerDialog: pickerC })
            const d = w.openDialog("Fill")
            tryVerify(() => d.opened)
            pick(drops(d)[0], "color")
            mouseClick(root.find(d.contentItem, o => o instanceof Swatch))
            let p = null
            tryVerify(() => (p = topDialog(w)) && p.title === "Picker" && p.opened)
            compare([p.params.color, p.params.title], ["#e9c7b3", "Màu tô"])
            p.params.picked("#123456")
            p.close()
            tryVerify(() => topDialog(w) === d)
            verify(Qt.colorEqual(d.custom, "#123456"))
            ok(d)
            compare(last(Docs.current).cmds[0].color, "#123456")
        }

        // The artboards' boxes (U23): number fields and drop-downs 28 high in Image Size, Canvas Size, Fill, Stroke
        // (ImageSize, FillStroke), 26 in the filters (Filters), Custom's grid 32.
        function test_box_heights() {
            for (const [c, h, name] of [[imageSizeC, 28, "Image Size"], [canvasSizeC, 28, "Canvas Size"], [fillC, 28, "Fill"], [strokeC, 28, "Stroke"]]) {
                const d = show(c), boxes = fields(d).concat(drops(d).filter(o => o.visible))
                verify(boxes.length >= 2, name)
                compare(boxes.map(o => o.height), boxes.map(() => h), name)
                d.close()
            }
            const m = show(filterC, { filter: "maximum" }), boxes = fields(m).concat(drops(m))
            compare(boxes.map(o => o.height), [26, 26], "Maximum: Radius, Preserve")
            const k = show(filterC, { filter: "custom" }), cells = fields(k)
            compare(cells.length, 27)
            compare(cells.slice(0, 25).every(o => o.height === 32), true, "the grid")
            compare([cells[25].height, cells[26].height], [26, 26], "Scale, Offset")
        }
        // Radio and ColorButton, controls of their own (U23; once inline components of StrokeDialog and FillDialog).
        function test_shared_controls() {
            const r = createTemporaryObject(radioC, root, { text: "Center", checked: true })
            compare([r.objectName, r.text, r.checked, r.height], ["radio", "Center", true, 20])
            const cb = createTemporaryObject(colorButtonC, root, { color: "#123456", title: qsTr("Màu viền") })
            verify(cb instanceof Swatch)
            compare([cb.height, cb.Accessible.name], [24, "Màu viền"])
            const e = show(strokeC)
            compare(root.all(e.contentItem, o => o instanceof Radio).length, 3, "Stroke's Vị trí")
            verify(root.find(e.contentItem, o => o instanceof ColorButton), "Stroke's Màu")
        }

        function test_keys() {  // Enter on the button Tab reached, Esc closes
            const doc = Docs.current, n = doc.history.length, d = show(filterC, { filter: "median" })
            button(d, "OK").forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            tryVerify(() => !d.visible)
            compare(doc.history.length, n + 1)
            const e = show(strokeC)
            mouseClick(input(fields(e)[0]))
            keyClick(Qt.Key_9)
            keyClick(Qt.Key_Escape)
            verify(e.visible && !fields(e)[0].editing, "the first Esc drops the typing")
            compare(input(fields(e)[0]).text, "4 px")
            keyClick(Qt.Key_Escape)
            tryVerify(() => !e.visible)
            compare(doc.history.length, n + 1)
            const f = show(fillC)
            mouseClick(input(fields(f)[0]))
            keyClick(Qt.Key_Tab)
            verify(checkRow(f, "Preserve Transparency").activeFocus, "Tab goes on to the next control")
            keyClick(Qt.Key_Space)
            verify(f.preserve)
        }
    }
}
