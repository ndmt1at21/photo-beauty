import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtTest
import PhotoBeauty
import "../qml/AdjustSpec.js" as Spec
import "../qml/MenuSpec.js" as MenuSpec

// U10: Properties (a page for each kind of thing picked), Adjustments (the 16 adjustment layers), the rows of the 16
// adjustments as the Adjustments artboard has them (AdjustRows, CurveBox, LevelsBar, HueRange) and the Adjust dialog of
// Image › Adjustments; on chân dung.psd as Docs.reset() opens it.
Item {
    id: root
    width: 1280
    height: 900

    Component { id: mainC; Main {} }
    Component {
        id: propsC
        PropertiesPanel {
            width: 307
            height: implicitHeight
        }
    }
    Component {
        id: rowsC
        AdjustRows {
            id: ar
            width: 283
            onMoved: v => ar.values = v  // the dialog's way: the values are its own
        }
    }
    Component { id: dialogC; AdjustDialog {} }
    Component { id: adjsC; Rectangle { width: 308; height: 260; color: "black"; AdjustmentsPanel { width: 308 } } }

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
        name: "U10"
        when: windowShown

        // Each adjustment's rows on the Adjustments artboard (build/proto/Adjustments.dc.html), as sign() reads the rows
        // built: the DSL's kind and what tells it apart.
        readonly property var artboard: ({
            "Brightness/Contrast": ["BTN Auto", "S Brightness", "S Contrast", "CHK Use Legacy"],
            "Levels": ["PRE", "DDL Auto ⚙", "TOOLS b|g|w", "HIST 3", "FLDS 3", "OUT 2", "FLDS 2"],
            "Curves": ["PRE", "DDL Auto", "TOOLS h|b|g|w", "CURVE", "LF Input|Output"],
            "Exposure": ["S Exposure", "S Offset", "S Gamma Correction", "TOOLS b|g|w"],
            "Vibrance": ["S Vibrance", "S Saturation"],
            "Hue/Saturation": ["PRE", "DDL", "TOOLS h", "S Hue", "S Saturation", "S Lightness", "CHK Colorize", "TOOLS d|dp|dm", "RANGE"],
            "Color Balance": ["SEG Shadows|Midtones|Highlights", "S Cyan · Red", "S Magenta · Green", "S Yellow · Blue", "CHK Preserve Luminosity"],
            "Black & White": ["PRE", "TINT", "BTN Auto", "TOOLS h", "S Reds", "S Yellows", "S Greens", "S Cyans", "S Blues", "S Magentas"],
            "Photo Filter": ["RADIO Filter", "RADIO Color", "S Density", "CHK Preserve Luminosity"],
            "Channel Mixer": ["DDL Output Channel", "CHK Monochrome", "S Red", "S Green", "S Blue", "TOT Total", "S Constant"],
            "Color Lookup": ["RADIO 3DLUT File", "RADIO Abstract", "RADIO Device Link", "CHK Dither"],
            "Invert": ["NOTE"],
            "Posterize": ["S Levels"],
            "Threshold": ["HIST 1", "LF Threshold Level"],
            "Gradient Map": ["GRAD", "CHK Dither", "CHK Reverse", "DDL Method"],
            "Selective Color": ["DDL Colors", "S Cyan", "S Magenta", "S Yellow", "S Black", "SEG Relative|Absolute"]
        })
        // Each slider: plan.md's range (Adjustment), the default as the artboard prints it.
        readonly property var sliders: ({
            "Brightness/Contrast": [["Brightness", -150, 150, "0"], ["Contrast", -50, 100, "0"]],
            "Exposure": [["Exposure", -20, 20, "0,00"], ["Offset", -0.5, 0.5, "0,0000"], ["Gamma Correction", 0.01, 9.99, "1,00"]],
            "Vibrance": [["Vibrance", -100, 100, "0"], ["Saturation", -100, 100, "0"]],
            "Hue/Saturation": [["Hue", -180, 180, "0"], ["Saturation", -100, 100, "0"], ["Lightness", -100, 100, "0"]],
            "Color Balance": [["Cyan · Red", -100, 100, "0"], ["Magenta · Green", -100, 100, "0"], ["Yellow · Blue", -100, 100, "0"]],
            "Black & White": [["Reds", -200, 300, "40%"], ["Yellows", -200, 300, "60%"], ["Greens", -200, 300, "40%"], ["Cyans", -200, 300, "60%"],
                ["Blues", -200, 300, "20%"], ["Magentas", -200, 300, "80%"]],
            "Photo Filter": [["Density", 1, 100, "25%"]],
            "Channel Mixer": [["Red", -200, 200, "+100%"], ["Green", -200, 200, "0%"], ["Blue", -200, 200, "0%"], ["Constant", -200, 200, "0%"]],
            "Posterize": [["Levels", 2, 255, "4"]],
            "Selective Color": [["Cyan", -100, 100, "0%"], ["Magenta", -100, 100, "0%"], ["Yellow", -100, 100, "0%"], ["Black", -100, 100, "0%"]]
        })

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanupTestCase() { init() }

        function doc() { return Docs.current }
        function last(d) { return d.history[d.index] }
        function id(name) { return doc().layers.find(l => l.props.name === name).id }
        function pick(layerId) { doc().run({ op: "selectLayers", layers: layerId === undefined ? [] : [layerId] }) }
        function addAdj(name) {
            verify(doc().run({ op: "newAdjustmentLayer", adjustment: name }), name)
            return doc().active
        }
        function panel() {
            const p = createTemporaryObject(propsC, root)
            waitForRendering(p)
            return p
        }
        function rowsOf(item) { return all(item, o => o.objectName.indexOf("row:") === 0).filter(o => o.visible) }
        function sign(l) {  // a row built, as the artboard tells them apart
            const k = l.objectName.slice(4), it = l.item
            const shown = type => all(it, o => o instanceof type && o.visible)
            switch (k) {
            case "DDL": {
                const lab = it.children[0].visible ? " " + it.children[0].text : ""
                return "DDL" + lab + (shown(Btn).length ? " Auto" : "") + (shown(ToolToggle).length ? " ⚙" : "")
            }
            case "BTN": return "BTN " + shown(Btn)[0].text
            case "S": return "S " + it.label
            case "CHK": return "CHK " + it.text
            case "SEG": return "SEG " + it.options.join("|")
            case "TOOLS": return "TOOLS " + shown(ToolToggle).map(t => t.modelData).join("|")
            case "HIST": case "OUT": return k + " " + it.stops.length
            case "FLDS": return "FLDS " + shown(NumberField).length
            case "LF": return "LF " + shown(NumberField).map(f => f.label).join("|")
            case "RADIO": return "RADIO " + find(it, o => o.objectName === "radio").text
            case "TOT": return "TOT " + it.children[0].text
            default: return k
            }
        }
        function slider(item, label) { return find(item, o => o instanceof SliderField && o.visible && o.label === label) }
        function fields(item) { return all(item, o => o instanceof NumberField && o.visible) }
        function dropdown(item, value) { return find(item, o => o instanceof Dropdown && o.visible && o.value === value) }
        function choose(dd, value) {  // as a click on that row of the open list
            dd.popup.open()
            tryVerify(() => dd.popup.opened)
            dd.picked(value)
            dd.popup.close()
        }
        function edit(f) {  // a number field's text, focused
            const t = find(f, o => o instanceof TextField)
            t.forceActiveFocus()
            return t
        }
        function type(text) {
            for (const ch of text)
                keyClick(ch)
        }
        function drag(item, x0, y0, x1, y1) {
            mousePress(item, x0, y0)
            for (let i = 1; i <= 8; i++)
                mouseMove(item, x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8)
            mouseRelease(item, x1, y1)
        }

        function test_rows_as_artboard() {  // every adjustment: its rows, in order
            compare(Spec.ORDER, Object.keys(artboard))
            for (const n of Spec.ORDER) {
                const r = createTemporaryObject(rowsC, root, { name: n })
                waitForRendering(r)
                compare(rowsOf(r).map(sign), artboard[n], n)
                verify(rowsOf(r).every(l => l.status === Loader.Ready && l.item), n)
            }
        }
        function test_defaults_and_limits() {
            for (const n in sliders) {
                const r = createTemporaryObject(rowsC, root, { name: n })
                for (const s of sliders[n]) {
                    const f = slider(r, s[0])
                    verify(f, n + " " + s[0])
                    compare([f.min, f.max, f.valueText], [s[1], s[2], s[3]], n + " " + s[0])
                }
            }
            const lv = createTemporaryObject(rowsC, root, { name: "Levels" })
            compare(fields(lv).map(f => [f.value, f.min, f.max, f.decimals]), [[0, 0, 253, 0], [1, 0.01, 9.99, 2], [255, 2, 255, 0], [0, 0, 255, 0], [255, 0, 255, 0]])
            compare(fields(lv)[1].shown, "1,00")
            const cv = createTemporaryObject(rowsC, root, { name: "Curves" })  // Input, Output: the black end without a point between
            compare(fields(cv).map(f => [f.label, f.value, f.min, f.max]), [["Input", 0, 0, 255], ["Output", 0, 0, 255]])
            const th = createTemporaryObject(rowsC, root, { name: "Threshold" })
            compare(fields(th).map(f => [f.value, f.min, f.max]), [[128, 1, 255]])
            compare(find(th, o => o instanceof LevelsBar).stops, [128])
            const tot = createTemporaryObject(rowsC, root, { name: "Channel Mixer" })
            verify(find(tot, o => o instanceof Text && o.text === "+100%"), "Total")
            const lu = createTemporaryObject(rowsC, root, { name: "Color Lookup" })
            compare(all(lu, o => o instanceof Dropdown && o.visible).map(o => o.opacity), [1, 0.5, 0.5], "the radio on is the first")
            mouseClick(all(lu, o => o.objectName === "radio")[1])
            compare([lu.values.lf, all(lu, o => o instanceof Dropdown && o.visible).map(o => o.opacity)], [1, [0.5, 1, 0.5]])
        }
        function test_presets_change_values() {  // and Default gives the defaults back
            const lv = createTemporaryObject(rowsC, root, { name: "Levels" })
            choose(dropdown(lv, "Default"), "Increase Contrast 2")
            compare(fields(lv).map(f => f.value), [18, 1, 237, 0, 255])
            choose(dropdown(lv, "Increase Contrast 2"), "Midtones Darker")
            compare(fields(lv).map(f => f.value), [0, 0.8, 255, 0, 255])
            compare(find(lv, o => o instanceof LevelsBar && o.histogram !== "").stops[1], 255 * Math.pow(0.5, 0.8))
            choose(dropdown(lv, "Midtones Darker"), "Default")
            compare(lv.values, Spec.defaults("Levels"))
            const cv = createTemporaryObject(rowsC, root, { name: "Curves" })
            choose(dropdown(cv, "Default"), "Medium Contrast (RGB)")
            compare(find(cv, o => o instanceof CurveBox).points, [[0, 0], [64, 52], [192, 206], [255, 255]])
            compare(fields(cv).map(f => f.value), [64, 52], "Input, Output: the point between the ends")
            choose(dropdown(cv, "Medium Contrast (RGB)"), "Negative (RGB)")
            compare(find(cv, o => o instanceof CurveBox).points, [[0, 255], [255, 0]])
            const hs = createTemporaryObject(rowsC, root, { name: "Hue/Saturation" })
            choose(dropdown(hs, "Default"), "Sepia")
            compare([slider(hs, "Hue").value, slider(hs, "Saturation").value, find(hs, o => o instanceof CheckRow).checked], [35, 25, true])
            choose(dropdown(hs, "Sepia"), "Red Boost")
            compare([hs.values.rng, slider(hs, "Saturation").value, hs.values.colorize, find(hs, o => o instanceof HueRange).span], ["Reds", 50, false, [315, 345, 15, 45]])
            const bw = createTemporaryObject(rowsC, root, { name: "Black & White" })
            choose(dropdown(bw, "Default"), "Infrared")
            compare(["Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"].map(s => slider(bw, s).valueText), ["−40%", "235%", "144%", "−68%", "−3%", "−107%"])
        }
        function test_pages() {  // each kind of thing picked: its page
            const d = doc(), p = panel()
            const page = () => find(p, o => o.objectName.indexOf("page:") === 0 && o.visible)
            const is = (name, what) => {
                tryCompare(p, "page", name, 1000, what)
                compare(page().objectName, "page:" + name, what)
            }
            is("px", "Dodge & Burn: a pixel layer")
            verify(find(page(), o => o instanceof Text && o.text === "Layer điểm ảnh"))
            pick(id("Tách tần số"))
            is("px", "a group")
            verify(find(page(), o => o instanceof Text && o.text === "Nhóm layer"))
            pick()
            is("doc", "no layer")
            compare(fields(p).slice(0, 3).map(f => f.shown), ["4.400 px", "5.600 px", "300 ppi"])
            verify(dropdown(p, "RGB, 16 bit") && dropdown(p, "sRGB IEC61966-2.1"))
            pick(id("Curves 1"))
            is("adj", "an adjustment layer")
            compare(find(p, o => o instanceof AdjustRows).name, "Curves")
            verify(find(page(), o => o instanceof Text && o.text === "Curves"), "its kind heads it, not the layer's name (U23)")
            compare(find(p, o => o instanceof CurveBox).points, Spec.presetPoints("Medium Contrast (RGB)"), "its preset's curve")
            d.setEditMask(true)
            is("mask", "its mask")
            compare(slider(p, "Density").valueText, "100%")
            d.setEditMask(false)
            is("adj", "back")
            d.run({ op: "newTypeLayer" })
            is("text", "a type layer")
            verify(dropdown(p, "Be Vietnam Pro"))
            d.run({ op: "newShapeLayer", shape: "ellipS" })
            is("shape", "a shape layer")
            verify(find(page(), o => o instanceof Text && o.text === "Hình elip"))
            compare(find(page(), o => o instanceof Swatch).color, Qt.color("#c0587e"))
            d.run({ op: "newFillLayer", fill: "Solid Color", color: "#336699" })
            is("fill", "a fill layer")
            compare(find(page(), o => o instanceof Swatch).color, Qt.color("#336699"))
            d.run({ op: "placeEmbedded" })
            is("so", "a smart object")
            verify(find(page(), o => o instanceof Text && o.text === "cau-vang.jpg"))
            d.run({ op: "mask", action: "reveal" })
            d.setEditMask(true)
            is("mask", "a smart object's mask")
            Docs.closeAll()
            is("none", "no document")
        }
        function test_drag_is_one_step() {  // a slider, a curve point, a triangle, a range: live, one step each, undo
            const d = doc(), p = panel()
            const l = addAdj("Brightness/Contrast")
            tryCompare(p, "page", "adj")
            waitForRendering(p)
            const s = slider(p, "Brightness"), n = d.history.length, seen = []
            const watch = () => seen.push(d.active.props.vals.b)
            d.revChanged.connect(watch)
            drag(s, s.width / 2, 23, s.width * 0.75, 23)
            d.revChanged.disconnect(watch)
            compare(d.history.length, n + 1)
            compare(last(d).name, "Sửa " + l.props.name)
            compare(d.active.props.vals.b, 75)
            verify(seen.length > 2, "the value follows the mouse")
            d.undo()
            compare(d.active.props.vals, {})
            compare(s.value, 0)

            const c = addAdj("Curves")
            tryCompare(p, "page", "adj")
            waitForRendering(p)
            const box = find(p, o => o instanceof CurveBox), k = d.history.length
            drag(box, box.width / 2, box.height / 2, box.width / 2, box.height / 4)  // a point added on the diagonal, dragged up
            compare(d.history.length, k + 1)
            const pts = d.active.props.vals.pts.RGB
            compare(pts.length, 3)
            verify(pts[1][1] > 180, "dragged up: " + pts[1])
            compare(fields(p).map(f => f.value), pts[1], "Input, Output follow the point")

            const v = addAdj("Levels")
            tryCompare(p, "page", "adj")
            waitForRendering(p)
            const bar = find(p, o => o instanceof LevelsBar && o.histogram !== ""), m = d.history.length
            drag(bar, bar.xOf(127.5), 100, bar.xOf(64), 100)  // the grey a quarter in: gamma 2, brighter
            compare(d.history.length, m + 1)
            verify(Math.abs(d.active.props.vals.gam - 2) < 0.06, "gamma " + d.active.props.vals.gam)
            compare(fields(p)[1].value, d.active.props.vals.gam)
            drag(bar, bar.xOf(0), 100, bar.width + 40, 100)  // the black input past the white: it stops 2 short
            compare(d.history.length, m + 2)
            compare(d.active.props.vals.in0, 253)
            const out = find(p, o => o instanceof LevelsBar && o.histogram === "")
            drag(out, out.xOf(255), 18, out.xOf(200), 18)
            compare([d.history.length, d.active.props.vals.out1], [m + 3, 200])

            addAdj("Hue/Saturation")
            tryCompare(p, "page", "adj")
            waitForRendering(p)
            choose(dropdown(p, "Master"), "Yellows")
            const hr = find(p, o => o instanceof HueRange), h = d.history.length
            compare(hr.span, [15, 45, 75, 105])
            const mid = Math.round(hr.xAt(0)), deg = Math.round(24 / hr.width * 360)  // the middle of the range: all four move
            drag(hr, mid, 23, mid + 24, 23)
            compare([d.history.length, d.active.props.vals.span], [h + 1, [15 + deg, 45 + deg, 75 + deg, 105 + deg]])
            drag(hr, hr.xAt(hr.r[0]) - 3, 23, -60, 23)  // the left falloff, out to the end
            compare(d.active.props.vals.span, [240, 45 + deg, 75 + deg, 105 + deg])
        }
        function test_one_step_each() {  // a pick, a box, a key: one step; nothing changed, none
            const d = doc(), p = panel()
            addAdj("Color Balance")
            tryCompare(p, "page", "adj")
            waitForRendering(p)
            const n = d.history.length
            mouseClick(find(p, o => o instanceof Segmented), 10, 12)
            compare([d.history.length, d.active.props.vals.tone], [n + 1, 0])
            mouseClick(find(p, o => o instanceof CheckRow))
            compare([d.history.length, d.active.props.vals.pl], [n + 2, false])
            const s = slider(p, "Cyan · Red")
            s.forceActiveFocus()
            keyClick(Qt.Key_Right, Qt.ShiftModifier)
            compare([d.history.length, d.active.props.vals.cr], [n + 3, 10])
            keyClick(Qt.Key_Left, Qt.ShiftModifier)
            compare([d.history.length, d.active.props.vals], [n + 4, { tone: 0, pl: false }], "back at a default: not kept")
            mouseClick(find(p, o => o instanceof ToolToggle && o.icon.name === "restart_alt"))
            compare([d.history.length, d.active.props.vals, last(d).name], [n + 5, {}, "Đặt lại Color Balance 1"])
            mouseClick(find(p, o => o instanceof ToolToggle && o.icon.name === "restart_alt"))
            compare(d.history.length, n + 5, "reset again: nothing changes")
        }
        function test_adjustment_foot() {  // clip, eye, mask, delete
            const d = doc(), p = panel()
            const l = addAdj("Vibrance")
            tryCompare(p, "page", "adj")
            waitForRendering(p)
            const foot = name => find(p, o => o instanceof ToolToggle && o.visible && o.icon.name === name)
            mouseClick(foot("subdirectory_arrow_right"))
            verify(d.active.props.clip)
            verify(foot("subdirectory_arrow_right").checked)
            mouseClick(foot("visibility"))
            compare([d.active.props.visible, last(d).name], [false, "Ẩn layer"])
            verify(foot("visibility_off"))
            mouseClick(foot("mask"))
            verify(d.editMask)
            tryCompare(p, "page", "mask")
            waitForRendering(p)
            verify(!find(p, o => o instanceof ToolToggle && o.visible && o.icon.name === Spec.ADJ.Vibrance.ic),
                   "the mask's page as PanelsA's: back to the rows by the layer's thumbnail in Layers (U23)")
            d.setEditMask(false)
            tryCompare(p, "page", "adj")
            waitForRendering(p)
            const n = d.layers.length
            mouseClick(foot("delete"))
            compare(d.layers.length, n - 1)
            verify(!d.layer(l.id))
        }
        function test_mask_page() {  // Density, Feather one step a drag; Invert, apply, disable, delete; the space, the dialog
            const d = doc(), p = panel()
            pick(id("Làm trắng răng"))
            d.setEditMask(true)
            tryCompare(p, "page", "mask")
            waitForRendering(p)
            const n = d.history.length, s = slider(p, "Density")
            drag(s, s.width - 1, 23, s.width / 2, 23)
            compare([d.history.length, last(d).name, d.active.props.maskDensity], [n + 1, "Sửa Layer Mask", 0.5])
            const f = slider(p, "Feather")
            drag(f, 1, 23, f.width / 10, 23)
            verify(d.active.props.maskFeather > 0)
            compare(d.history.length, n + 2)
            const btn = t => find(p, o => o instanceof Btn && o.text === t)
            verify(btn("Select and Mask…").enabled, "opens Select and Mask once U15 has it")
            verify(!btn("Color Range…").enabled, "no such dialog yet")
            const mask = d.active.props.mask
            mouseClick(btn("Invert"))
            verify(d.active.props.mask !== mask)
            const foot = name => find(p, o => o instanceof ToolToggle && o.visible && o.icon.name === name)
            mouseClick(foot("visibility"))
            verify(d.active.props.maskOff && foot("visibility").checked)
            mouseClick(foot("load_sel"))
            compare([d.selection, last(d).name], ["skin", "Nạp vùng chọn từ Mask"])
            mouseClick(foot("delete"))
            compare(d.active.props.mask, null)
            tryCompare(p, "page", "adj", 1000, "no mask: the rows")
        }
        function test_page_rows() {  // where PanelsA's CSS puts the rows, to a layout's whole pixels: .cap's line box 10,5 × 1,35 (U23)
            const d = doc(), p = panel()
            const near = (name, css) => {
                tryCompare(p, "page", name)
                waitForRendering(p)
                const qt = []
                for (const c of find(p, o => o.objectName === "page:" + name).children[0].children)
                    if (c.visible)
                        qt.push(c.y)
                qt.push(p.implicitHeight)
                verify(qt.length === css.length && qt.every((y, i) => Math.abs(y - css[i]) < 0.6), name + ": " + qt + " for " + css)
            }
            pick()
            near("doc", [0, 28, 50.175, 82.175, 114.175, 150.175, 172.35, 216.35])  // the rows, then the page's height
            d.run({ op: "newShapeLayer", shape: "rectS" })
            near("shape", [0, 28, 92, 114.175, 148.175, 170.35, 234.35, 256.525, 298.525])
            d.run({ op: "placeEmbedded" })
            near("so", [0, 28, 78.375, 140.375, 184.375])  // the file's box 6 + 16,2 + 14,175 + 6
        }
        function test_document_page() {  // canvas size, orientation, resolution, mode, profile, the quick buttons
            const d = doc(), p = panel()
            pick()
            tryCompare(p, "page", "doc")
            waitForRendering(p)
            const n = d.history.length, w = fields(p)[0]
            edit(w)
            keyClick(Qt.Key_A, Qt.ControlModifier)
            type("2000")
            keyClick(Qt.Key_Return)
            compare([d.w, last(d).name], [2000, "Canvas Size"])
            mouseClick(find(p, o => o instanceof ToolToggle && o.icon.name === "crop_portrait"))
            compare(d.history.length, n + 1, "portrait already")
            mouseClick(find(p, o => o instanceof ToolToggle && o.icon.name === "crop_landscape"))
            compare([d.w, d.h, d.history.length], [5600, 2000, n + 2])
            edit(fields(p)[2])
            keyClick(Qt.Key_Up)
            compare([d.ppi, d.w], [301, 5600])
            choose(dropdown(p, "RGB, 16 bit"), "Grayscale, 8 bit")
            compare([d.mode, d.bits], ["Gray", 8])
            choose(dropdown(p, "sRGB IEC61966-2.1"), "Display P3")
            compare(d.profile, "Display P3")
            mouseClick(find(p, o => o instanceof Btn && o.text === "Select Subject"))
            compare(d.selection, "subject")
            mouseClick(find(p, o => o instanceof Btn && o.text === "Remove Background"))
            compare(last(d).name, "Remove Background")
        }
        function test_layer_pages() {  // fields kept out of the document, flips and alignment steps, Rasterize
            const d = doc(), p = panel()
            const db = id("Dodge & Burn"), n = d.history.length
            const w = fields(p)[0]
            compare(w.shown, "4.400 px")
            edit(w)
            keyClick(Qt.Key_Down)
            compare([Tools.value("L:" + db + ":w", 0), d.history.length], [4399, n])
            mouseClick(find(p, o => o instanceof ToolToggle && o.icon.name === "flip"))
            compare(last(d).name, "Lật ngang")
            mouseClick(find(p, o => o instanceof ToolToggle && o.icon.name === "align_vertical_bottom"))
            compare(last(d).name, "Căn dưới")
            d.run({ op: "placeEmbedded" })
            tryCompare(p, "page", "so")
            waitForRendering(p)
            compare(fields(p).map(f => f.value), [2100, 1312, 190, 2040])
            mouseClick(find(p, o => o instanceof Btn && o.text === "Rasterize"))
            compare(d.active.kind, "pixel")
            d.run({ op: "newTypeLayer" })
            tryCompare(p, "page", "text")
            waitForRendering(p)
            choose(dropdown(p, "aA Sharp"), "aA Smooth")
            compare(Tools.antiAlias, "Smooth")
            mouseClick(find(p, o => o instanceof ToolToggle && o.icon.name === "format_align_center"))
            compare(Tools.value("para:align", 0), 1, "Paragraph's value")
            Tools.setValue("char:size", 72)
            verify(find(p, o => o instanceof NumberField && o.visible && o.value === 72), "Character's size")
            mouseClick(find(p, o => o instanceof Btn && o.text === "Mở Character"))
            compare(Tools.sidePanel, "Character")
        }
        function test_curve_box() {  // add, clamp between neighbours, Input and Output, delete; the ends stay
            const r = createTemporaryObject(rowsC, root, { name: "Curves" })
            waitForRendering(r)
            const box = find(r, o => o instanceof CurveBox), at = (x, y) => [x / 255 * box.width, (1 - y / 255) * box.height]
            let q = at(128, 128)
            mouseClick(box, q[0], q[1])
            compare(r.values.pts.RGB, [[0, 0], [128, 128], [255, 255]])
            compare(r.shownPoint, 1)
            q = at(128, 128)
            drag(box, q[0], q[1], box.width + 30, at(0, 200)[1])  // right past the white end: it stops 1 short
            compare(r.values.pts.RGB[1], [254, 200])
            edit(fields(r)[0])
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            compare(r.values.pts.RGB[1], [244, 200], "Input")
            choose(dropdown(r, "RGB"), "Red")
            compare([box.points, r.values.pts.RGB.length], [[[0, 0], [255, 255]], 3], "each channel its curve")
            verify(Qt.colorEqual(box.ink, Theme.h1))
            choose(dropdown(r, "Red"), "RGB")
            q = at(244, 200)
            mouseDoubleClickSequence(box, q[0], q[1])
            compare(r.values.pts.RGB, [[0, 0], [255, 255]])
            q = at(0, 0)
            mouseDoubleClickSequence(box, q[0] + 1, q[1] - 1)
            compare(r.values.pts.RGB, [[0, 0], [255, 255]], "an end stays")
        }
        function test_adjustments_panel() {  // each of the 16: its layer, shown in Properties
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            const d = doc()
            for (const name of Spec.ORDER) {
                Tools.showPanel("Adjustments")
                waitForRendering(w.contentItem)
                const a = find(w.contentItem, o => o instanceof AdjustmentsPanel && o.visible)
                verify(a, name)
                compare(a.implicitHeight, 254)
                const b = find(a, o => o instanceof AbstractButton && o.modelData === name)
                const n = d.history.length
                mouseClick(b)
                compare([d.history.length, last(d).name, d.active.kind, d.active.props.adj], [n + 1, "Layer " + name + " mới", "adj", name])
                compare(Tools.dockTab.p, "Properties")
                waitForRendering(w.contentItem)
                const p = find(w.contentItem, o => o instanceof PropertiesPanel && o.visible)
                compare([p.page, find(p, o => o instanceof AdjustRows).name], ["adj", name])
            }
        }
        // A button's label starts 10 px under its icon's ink, as in PanelsA (a column 5 apart in CSS's line box) (U23).
        function test_adjustments_label() {
            const r = createTemporaryObject(adjsC, root)
            waitForRendering(r)
            const g = grabImage(r), b = find(r, o => o instanceof AbstractButton && o.modelData === "Brightness/Contrast"), p = b.mapToItem(r, 0, 0)
            const runs = []  // the rows with ink, top down: the icon's, the label's
            for (let y = Math.round(p.y); y < p.y + b.height; y++) {
                let on = false
                for (let x = Math.round(p.x); x < p.x + b.width && !on; x++)
                    on = g.red(x, y) > 60
                if (on && runs.length && runs[runs.length - 1][1] === y - 1)
                    runs[runs.length - 1][1] = y
                else if (on)
                    runs.push([y, y])
            }
            compare(runs.length, 2, JSON.stringify(runs))
            verify(Math.abs(runs[1][0] - runs[0][1] - 10) <= 1, "icon to label " + JSON.stringify(runs))
        }
        function test_color_picker() {  // a swatch of a page, of the rows: U11's Color Picker; OK is one step with its colour
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            const d = doc(), top = () => w.dialogs[w.dialogs.length - 1] || null
            const pickHex = (p, hex) => {
                waitForRendering(w.contentItem)
                mouseClick(find(p, o => o instanceof Swatch && o.visible))
                let dl = null
                tryVerify(() => (dl = top()) instanceof ColorPickerDialog && dl.opened)
                const t = find(dl.contentItem, o => o instanceof TextBox)
                mouseClick(t)
                tryVerify(() => t.activeFocus)
                t.text = hex
                keyClick(Qt.Key_Return)
                mouseClick(find(dl.footer, o => o instanceof Btn && o.text === "OK"))
                tryVerify(() => !top())
            }
            d.run({ op: "newFillLayer", fill: "Solid Color", color: "#336699" })
            Tools.showPanel("Properties")
            waitForRendering(w.contentItem)
            const p = find(w.contentItem, o => o instanceof PropertiesPanel && o.visible), n = d.history.length
            tryCompare(p, "page", "fill")
            pickHex(p, "123456")
            compare([d.history.length, d.active.props.color], [n + 1, "#123456"])
            addAdj("Photo Filter")
            tryCompare(p, "page", "adj")
            pickHex(p, "abcdef")  // the Color radio's
            compare([d.history.length, d.active.props.vals.pf1], [n + 3, "#abcdef"])
        }
        function test_dialog() {  // its rows, the values its own until OK, Hủy and Esc leave the document
            const d = doc(), n = d.history.length
            const dl = createTemporaryObject(dialogC, root, { params: { adjust: "levels" } })
            dl.open()
            tryVerify(() => dl.opened)
            compare([dl.title, rowsOf(dl.contentItem).length], ["Levels", 7])
            choose(dropdown(dl.contentItem, "Default"), "Lighter")
            compare([dl.values.in1, d.history.length], [232, n])
            const pv = find(dl.contentItem, o => o instanceof CheckRow && o.text === "Preview")
            verify(pv.checked)
            mouseClick(pv)
            verify(!dl.preview)
            mouseClick(find(dl.footer, o => o instanceof Btn && o.text === "OK"))
            tryVerify(() => !dl.visible)
            compare([d.history.length, last(d).name, last(d).cmds[0].adjust, last(d).cmds[0].values], [n + 1, "Levels", "levels", { pre: "Lighter", in1: 232 }])
            const e = createTemporaryObject(dialogC, root, { params: { adjust: "posterize" } })
            e.open()
            tryVerify(() => e.opened)
            slider(e.contentItem, "Levels").forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(e.values.lv, 5)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !e.visible)
            compare(d.history.length, n + 1)
        }
        function test_menu_opens_it() {  // every Image › Adjustments "…" item, the menu by mouse, Ctrl M
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            const top = () => w.dialogs[w.dialogs.length - 1] || null
            const items = MenuSpec.MENUS[2].items.find(it => it.l === "Adjustments").sub.filter(it => it.dialog)
            compare(items.map(it => it.params.adjust), Spec.ORDER.filter(x => x !== "Invert").map(Spec.key))
            for (const it of items) {
                verify(w.can(it), it.l)
                verify(w.act(it), it.l)
                const dl = top()
                tryVerify(() => dl.opened)
                verify(dl instanceof AdjustDialog, it.l)
                compare([dl.title + "…", rowsOf(dl.contentItem).length], [it.l, Spec.rows(dl.name).length])
                dl.close()
                tryVerify(() => !top(), 1000, it.l)
            }
            const bar = find(w.contentItem, o => o instanceof AppMenu), m = bar.menuAt(2)
            mouseClick(bar.itemAt(2))
            tryVerify(() => m.opened)
            waitForRendering(w.contentItem)
            const row = (menu, label) => {
                for (let i = 0; i < menu.count; i++)
                    if (menu.itemAt(i) instanceof T.MenuItem && menu.itemAt(i).text === label)
                        return menu.itemAt(i)
                return null
            }
            const adj = row(m, "Adjustments")
            mouseClick(adj)
            tryVerify(() => adj.subMenu.opened)
            waitForRendering(w.contentItem)
            mouseClick(row(adj.subMenu, "Hue/Saturation…"))
            let dl = null
            tryVerify(() => (dl = top()) && dl.opened)
            compare(dl.name, "Hue/Saturation")
            keyClick(Qt.Key_Escape)
            tryVerify(() => !top())
            keySequence("Ctrl+M")
            tryVerify(() => (dl = top()) && dl.opened)
            compare(dl.name, "Curves")
            dl.close()
            tryVerify(() => !top())
            w.start("Adjust:selectiveColor")  // --state Adjust:selectiveColor
            tryVerify(() => (dl = top()) && dl.opened)
            compare(dl.name, "Selective Color")
        }
        function test_keys() {  // Tab goes from row to row, Enter on a button, Esc closes the dialog
            const d = doc()
            const dl = createTemporaryObject(dialogC, root, { params: { adjust: "brightnessContrast" } })
            dl.open()
            tryVerify(() => dl.opened)
            const auto = find(dl.contentItem, o => o instanceof Btn && o.text === "Auto")
            auto.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            compare([dl.values.b, dl.values.c], [12, 8])
            keyClick(Qt.Key_Tab)
            verify(slider(dl.contentItem, "Brightness").activeFocus, "Tab: the next row")
            keyClick(Qt.Key_Right)
            compare(dl.values.b, 13)
            const ok = find(dl.footer, o => o instanceof Btn && o.text === "OK"), n = d.history.length
            ok.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            tryVerify(() => !dl.visible)
            compare([d.history.length, last(d).cmds[0].values], [n + 1, { b: 13, c: 8 }])
        }
    }
}
