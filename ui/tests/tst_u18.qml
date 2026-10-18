pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty
import "../qml/MenuSpec.js" as MenuSpec

// U18: Layer Style and SplitSlider (docs/ui-tasks.md). The dialog runs on the mock portrait as Main would open it:
// pages and their defaults as the artboards, effects on and off, Alt drags splitting a Blend If triangle, OK one
// History step "Layer Style", Hủy (and Esc, ×) leaving document and History as they were, Preview, the defaults kept.
Item {
    id: root
    width: 1100
    height: 760
    property var log: []

    Component { id: dlgC; LayerStyleDialog {} }
    Component { id: mainC; Main {} }
    Component { id: splitC; SplitSlider { width: 400; label: "This Layer"; onMoved: v => value = v } }

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
    function within(o, type) {  // o is inside an item of type
        for (let p = o.parent; p; p = p.parent)
            if (p instanceof type) return true
        return false
    }
    function record(c) {  // the value protocol of c into log
        root.log = []
        c.started.connect(() => root.log.push("s"))
        c.moved.connect(v => root.log.push("m" + v))
        c.committed.connect(v => root.log.push("c" + v))
        return c
    }

    TestCase {
        id: tc
        name: "U18"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
            Theme.dark = true
        }

        function open(params) {
            const d = createTemporaryObject(dlgC, root, { params: params || {} })
            d.open()
            tryCompare(d, "opened", true)
            settle(d)
            return d
        }
        function settle(d) {  // layouts placed: after open, after a page shows
            for (const l of root.all(d.contentItem, o => o instanceof ColumnLayout || o instanceof RowLayout))
                waitForPolish(l)
        }
        function list(d) { return d.contentItem.children[0] }  // the effects on the left
        function page(d) { return d.contentItem.children.find(c => c instanceof ColumnLayout) }
        function side(d) { return d.contentItem.children[d.contentItem.children.length - 1] }  // OK, Hủy, Preview
        function btn(item, text) { return root.find(item, o => o instanceof Btn && o.text === text) }
        function boxes(d) { return root.all(list(d), o => o instanceof CheckRow) }  // the effects' check boxes
        function names(d) { return root.all(list(d), o => o instanceof AbstractButton && !(o instanceof CheckRow)) }  // Blending Options, the effects
        function shown(d, type) { return root.all(page(d), o => o instanceof type && o.visible && !root.within(o, SplitSlider)) }
        function field(d, n) { return shown(d, NumberField)[n] }
        function input(f) { return root.find(f, o => o instanceof TextField) }
        function type(f, text) {  // into number field f, then Enter
            mouseClick(input(f))
            tryVerify(() => f.editing)
            input(f).text = text
            keyClick(Qt.Key_Return)
        }
        function rows(d) {  // the names of the page's rows
            return root.all(page(d), o => o instanceof Text && o.visible && Qt.colorEqual(o.color, Theme.t2) && o.mapToItem(page(d), 0, 0).x === 0).map(t => t.text)
        }
        function history(doc) { return JSON.stringify([doc.index, doc.history.map(h => [h.name, h.icon, h.cmds])]) }
        // A drag on slider s from value n by dx px, Alt held at the press, moving the way dx goes.
        function drag(s, n, dx, alt, half) {
            const x = 7 + n / 255 * (s.width - 14) + (half || 0), y = 47
            mousePress(s, x, y, Qt.LeftButton, alt ? Qt.AltModifier : Qt.NoModifier)
            for (let k = 1; k <= 4; k++)
                mouseMove(s, x + dx * k / 4, y, -1, Qt.LeftButton)
            mouseRelease(s, x + dx, y)
        }

        // ---- pages ------------------------------------------------------------------------------------------------
        // Each page has the artboard's rows and fields, at its defaults ([value, unit, min, max] per number field).
        function test_pages_defaults() {
            const want = {
                blendingOptions: { rows: ["Blend Mode", "Opacity", "Fill Opacity", "Kênh", "Kênh", "This Layer", "Underlying Layer"], fields: [[100, "%", 0, 100], [100, "%", 0, 100]],
                    drops: ["softLight", "gray"], checks: [["R", true], ["G", true], ["B", true]], colors: [] },
                dropShadow: { rows: ["Blend Mode", "Opacity", "Angle", "Distance", "Spread", "Size", "Noise"],
                    fields: [[35, "%", 0, 100], [120, "°", -180, 180], [12, "px", 0, 30000], [0, "%", 0, 100], [24, "px", 0, 250], [0, "%", 0, 100]],
                    drops: ["multiply"], checks: [["Layer Knocks Out Drop Shadow", true]], colors: ["#2b1d4e"] },
                stroke: { rows: ["Size", "Position", "Blend Mode", "Opacity", "Color"], fields: [[6, "px", 1, 250], [100, "%", 0, 100]], drops: ["outside", "normal"], checks: [],
                    colors: ["#f7c873"] },
                colorOverlay: { rows: ["Blend Mode", "Opacity"], fields: [[100, "%", 0, 100]], drops: ["normal"], checks: [], colors: ["#ff0000"] },
                gradientOverlay: { rows: ["Blend Mode", "Opacity", "Gradient", "Style", "Angle", "Scale"], fields: [[100, "%", 0, 100], [90, "°", -180, 180], [100, "%", 10, 150]],
                    drops: ["normal", "0,0", "linear"], checks: [["Dither", false], ["Reverse", false], ["Align with Layer", true]], colors: [] }
            }
            const n = Docs.current.history.length
            const d = open()
            compare(d.page, "blendingOptions", "no page: Blending Options")
            compare(d.header.children[0].children[1].text, "· Dodge & Burn", "the layer in the title")
            compare(names(d).map(b => b.text), ["Blending Options", "Drop Shadow", "Stroke", "Color Overlay", "Gradient Overlay"])
            compare(boxes(d).map(b => b.checked), [false, false, false, false], "a layer without style: no effect on")
            for (const k in want) {
                d.page = k  // shown without turning it on
                settle(d)
                const w = want[k]
                compare(rows(d), w.rows, k + ": rows")
                compare(shown(d, NumberField).map(f => [f.value, f.unit, f.min, f.max]), w.fields, k + ": number fields")
                compare(shown(d, Dropdown).map(x => x.value), w.drops, k + ": drop-downs")
                compare(shown(d, CheckRow).map(c => [c.text, c.checked]), w.checks, k + ": check boxes")
                compare(shown(d, Swatch).map(s => String(s.color)), w.colors, k + ": colours")
                compare(shown(d, SplitSlider).length, k === "blendingOptions" ? 2 : 0)
                compare(!!btn(page(d), "Đặt làm mặc định") && btn(page(d), "Đặt làm mặc định").visible, k !== "blendingOptions", k + ": Make Default on effect pages")
            }
            d.page = "blendingOptions"
            compare(shown(d, SplitSlider).map(s => s.value), [[0, 0, 255, 255], [0, 0, 255, 255]])
            compare(shown(d, Dropdown)[0].options.length, 27, "27 modes, no Pass Through for a pixel layer")
            d.page = "gradientOverlay"
            compare(shown(d, Dropdown)[1].displayText, "Màu trước sang màu sau")
            compare(Docs.current.history.length, n, "showing pages changes nothing")
        }
        // params.page opens a page, an effect's turned on; params.layer a layer other than the active one; a group's
        // blend mode list has Pass Through.
        function test_params() {
            const doc = Docs.current, curves = doc.layers.find(l => l.props.name === "Curves 1"), db = doc.active
            let d = open({ page: "stroke" })
            compare(d.page, "stroke")
            verify(d.v.stroke.on, "the effect asked for is on")
            compare(doc.layer(db.id).props.fx.stroke.on, true, "Preview: on the layer at once")
            d.close()
            tryCompare(d, "visible", false)
            compare(doc.layer(db.id).props.fx, null)
            d = open({ page: "bevelEmboss", layer: curves.id })
            compare(d.page, "blendingOptions", "a page it does not know: Blending Options")
            compare(d.header.children[0].children[1].text, "· Curves 1")
            mouseClick(boxes(d)[2])
            mouseClick(btn(side(d), "OK"))
            tryCompare(d, "visible", false)
            compare(doc.layer(curves.id).props.fx.colorOverlay.on, true)
            compare(doc.layer(db.id).props.fx, null, "not the active layer")
            const fs = doc.layers.find(l => l.kind === "group")
            d = open({ layer: fs.id })
            const mode = shown(d, Dropdown)[0]
            compare(mode.value, "passThrough")
            compare(mode.displayText, "Pass Through")
            compare(mode.options.length, 28)
        }

        // ---- effects on and off -----------------------------------------------------------------------------------
        function test_toggle_effects() {
            const doc = Docs.current, id = doc.active.id, n = doc.history.length
            const d = open()
            mouseClick(boxes(d)[0])
            verify(d.v.dropShadow.on)
            compare(d.page, "blendingOptions", "the box leaves the page")
            compare(doc.layer(id).props.fx.dropShadow.on, true, "on the layer at once")
            compare(doc.history.length, n + 1)
            compare(doc.history[doc.index].name, "Layer Style")
            mouseClick(boxes(d)[0])
            verify(!d.v.dropShadow.on)
            compare(doc.layer(id).props.fx, null, "off, and the layer had none: no style")
            mouseClick(names(d)[2])
            compare(d.page, "stroke", "a name shows its page")
            verify(d.v.stroke.on, "and turns the effect on")
            verify(boxes(d)[1].checked)
            compare(names(d)[2].parent.color, Theme.acc2, "the page's row in acc2")
            mouseClick(names(d)[2])
            verify(d.v.stroke.on, "a second click keeps it on")
            mouseClick(boxes(d)[3])
            mouseClick(btn(side(d), "OK"))
            tryCompare(d, "visible", false)
            const fx = doc.layer(id).props.fx
            compare(Object.keys(fx), ["stroke", "gradientOverlay"], "the effects on")
            compare(fx.stroke, { on: true, size: 6, position: "outside", blend: "normal", opacity: 100, color: "#f7c873" })
            compare(doc.history.length, n + 1)
            doc.undo()
            compare(doc.layer(id).props.fx, null)
        }
        // An effect the layer had stays in props.fx when turned off (its eye in Layers), with what it was set to.
        function test_off_stays_listed() {
            const doc = Docs.current, id = doc.active.id
            doc.run({ op: "setProps", props: { fx: { dropShadow: { on: true, blend: "normal", color: "#000000", opacity: 75, angle: 90, distance: 5, spread: 0, size: 5, noise: 0,
                knockOut: true } } } })
            const d = open()
            verify(boxes(d)[0].checked)
            mouseClick(boxes(d)[0])
            mouseClick(btn(side(d), "OK"))
            tryCompare(d, "visible", false)
            const fx = doc.layer(id).props.fx
            compare(Object.keys(fx), ["dropShadow"])
            compare(fx.dropShadow.on, false)
            compare(fx.dropShadow.opacity, 75, "its values kept")
        }

        // ---- OK, Hủy, Preview -------------------------------------------------------------------------------------
        function test_ok_one_step() {
            const doc = Docs.current, id = doc.active.id, n = doc.history.length
            const d = open()
            type(field(d, 1), "40")  // Fill Opacity first
            shown(d, Dropdown)[0].activated(3)  // the layer's mode: Multiply
            mouseClick(shown(d, CheckRow)[0])  // R off
            mouseClick(names(d)[1])
            type(field(d, 0), "50")  // Opacity
            type(field(d, 1), "−30")  // Angle
            shown(d, Dropdown)[0].activated(4)  // Color Burn
            compare(doc.history.length, n + 1, "while the dialog is open: its step only")
            compare(doc.history[n].name, "Layer Style", "whatever changed first")
            mouseClick(btn(side(d), "OK"))
            tryCompare(d, "visible", false)
            compare(doc.history.length, n + 1, "one step")
            compare(doc.index, n)
            compare(doc.history[n].name, "Layer Style")
            compare(doc.history[n].icon, "shadow")
            const p = doc.layer(id).props
            compare([p.blend, p.opacity, p.fill], ["multiply", 1, 0.4])
            compare(p.fx.channels, { r: false, g: true, b: true })
            compare([p.fx.dropShadow.on, p.fx.dropShadow.opacity, p.fx.dropShadow.angle, p.fx.dropShadow.blend], [true, 50, -30, "colorBurn"])
            doc.run({ op: "setProps", props: { opacity: 0.5 } })
            compare(doc.history.length, n + 2, "the gesture ended with OK: the next command is a step of its own")
            doc.undo()
            doc.undo()
            compare(doc.layer(id).props.fx, null)
            compare([doc.layer(id).props.blend, doc.layer(id).props.fill], ["softLight", 1])
        }
        // OK with nothing changed, or changed and changed back, makes no step.
        function test_ok_nothing() {
            const doc = Docs.current, before = history(doc)
            let d = open()
            mouseClick(btn(side(d), "OK"))
            tryCompare(d, "visible", false)
            compare(history(doc), before)
            d = open({ page: "colorOverlay" })
            mouseClick(boxes(d)[2])
            verify(!d.v.colorOverlay.on)
            mouseClick(btn(side(d), "OK"))
            tryCompare(d, "visible", false)
            compare(history(doc), before)
        }
        // Hủy, Esc and × leave the layer, History and the unsaved dot as they were.
        function test_cancel() {
            for (const how of ["Hủy", "Esc", "×"]) {
                Docs.reset()
                Docs.select(1)  // san-pham.webp: saved, one layer
                const doc = Docs.current, id = doc.active.id, before = history(doc), props = JSON.stringify(doc.layer(id).props)
                verify(!doc.dirty)
                const d = open({ page: "gradientOverlay" })
                type(field(d, 2), "50")  // Scale
                mouseClick(names(d)[0])
                type(field(d, 0), "20")  // Opacity
                drag(shown(d, SplitSlider)[0], 0, 40, true)
                mouseClick(boxes(d)[1])  // Stroke on, the focus on a check box
                verify(JSON.stringify(doc.layer(id).props) !== props, how + ": Preview changed the layer")
                if (how === "Hủy")
                    mouseClick(btn(side(d), "Hủy"))
                else if (how === "Esc")
                    keyClick(Qt.Key_Escape)
                else
                    mouseClick(root.find(d.header, o => o instanceof IconButton && o.icon.name === "close"))
                tryCompare(d, "visible", false)
                compare(JSON.stringify(doc.layer(id).props), props, how + ": the layer as it was")
                compare(history(doc), before, how + ": History as it was")
                verify(!doc.dirty, how + ": still saved")
                doc.run({ op: "setProps", props: { opacity: 0.5 } })
                compare(doc.history.length, 2, how + ": no gesture left open")
            }
        }
        // Preview off: the layer as it was until OK; on again: the values at once.
        function test_preview() {
            const doc = Docs.current, id = doc.active.id, before = history(doc)
            const d = open({ page: "colorOverlay" })
            compare(doc.layer(id).props.fx.colorOverlay.on, true)
            const pv = root.find(side(d), o => o instanceof CheckRow)
            compare(pv.text, "Preview")
            verify(pv.checked)
            mouseClick(pv)
            verify(!d.preview)
            compare(doc.layer(id).props.fx, null, "Preview off: as it was")
            compare(history(doc), before, "and no step")
            type(field(d, 0), "50")
            compare(doc.layer(id).props.fx, null, "values wait for OK")
            mouseClick(pv)
            compare(doc.layer(id).props.fx.colorOverlay.opacity, 50, "Preview on: at once")
            mouseClick(pv)
            mouseClick(btn(side(d), "OK"))
            tryCompare(d, "visible", false)
            compare(doc.history.length, JSON.parse(before)[1].length + 1)
            compare(doc.layer(id).props.fx.colorOverlay, { on: true, blend: "normal", color: "#ff0000", opacity: 50 })
        }

        // ---- Blend If -----------------------------------------------------------------------------------------------
        function test_split_slider() {
            const s = root.record(createTemporaryObject(splitC, root, { value: [0, 0, 255, 255] }))
            waitForRendering(s)
            const fields = () => root.all(s, o => o instanceof NumberField && o.visible)
            compare(fields().map(f => f.value), [0, 255], "a field a triangle")
            compare([s.height].concat(fields().map(f => [f.height, f.horizontalAlignment])), [54, [22, TextInput.AlignRight], [22, TextInput.AlignRight]],
                    "the artboard's .bf, 22 high, its number to the right; 4, the 28 px bar (U23)")
            drag(s, 0, 40, false)
            compare(s.value, [26, 26, 255, 255], "a plain drag moves the whole triangle")
            compare(root.log[0], "s")
            compare(root.log[root.log.length - 1], "c26,26,255,255")
            root.log = []
            drag(s, 26, 20, true)
            compare(s.value, [26, 39, 255, 255], "Alt drag to the right splits it: the right half moves")
            verify(s.blackSplit)
            compare(fields().map(f => f.value), [26, 39, 255], "a field a half")
            compare(fields().map(f => f.Accessible.name), ["This Layer: nửa trái tam giác đen", "This Layer: nửa phải tam giác đen", "This Layer: tam giác trắng"])
            compare(root.log[0], "s")
            drag(s, 26, -20, false, -3.5)
            compare(s.value, [13, 39, 255, 255], "a split half drags alone")
            drag(s, 255, -30, true)
            compare(s.value, [13, 39, 235, 255], "Alt drag to the left: the left half moves")
            drag(s, 39, 400, false, 3.5)
            compare(s.value, [13, 235, 235, 255], "held by its neighbour")
            root.log = []
            type(fields()[2], "300")
            compare(s.value, [13, 235, 255, 255], "typed past the next half: held there")
            compare(root.log, ["s", "m13,235,255,255", "c13,235,255,255"])
            compare(fields().length, 3, "the white halves met: one triangle")
            type(fields()[0], "250")
            compare(s.value, [235, 235, 255, 255])
            verify(!s.blackSplit)
            type(fields()[0], "100")
            compare(s.value, [100, 100, 255, 255], "a whole triangle's field moves both halves")
            root.log = []
            drag(s, 175, 30, false)
            compare(root.log, [], "nothing under the mouse")
        }
        function test_blend_if() {
            const doc = Docs.current, id = doc.active.id
            const d = open()
            const sl = shown(d, SplitSlider)
            compare(sl.map(x => x.label), ["This Layer", "Underlying Layer"])
            drag(sl[0], 0, 40, true)
            const n = d.v.blendIf.gray.layer[1]
            verify(n > 0)
            compare(d.v.blendIf.gray.layer, [0, n, 255, 255])
            compare(doc.layer(id).props.fx.blendIf.gray.layer, [0, n, 255, 255], "on the layer")
            drag(sl[1], 255, -40, true)
            compare(d.v.blendIf.gray.under[3], 255)
            verify(d.v.blendIf.gray.under[2] < 255)
            const note = root.find(page(d), o => o instanceof Text && o.visible && o.text.startsWith("Alt kéo"))
            verify(note.text.indexOf("từ 0 tới " + n + ";") > 0, note.text)
            shown(d, Dropdown)[1].activated(1)
            compare(d.channel, "red")
            compare(sl[0].value, [0, 0, 255, 255], "a channel of its own")
            drag(sl[0], 255, -20, false)
            verify(d.v.blendIf.red.layer[2] < 255)
            compare(d.v.blendIf.gray.layer, [0, n, 255, 255])
            mouseClick(btn(side(d), "OK"))
            tryCompare(d, "visible", false)
            compare(Object.keys(doc.layer(id).props.fx), ["blendIf"])
        }

        // A Gray document (U23): Kênh has a box for Gray alone, Blend If the Gray channel alone.
        function test_gray() {
            const doc = Docs.current, id = doc.active.id
            doc.run({ op: "setMode", mode: "Gray" })
            const d = open()
            compare(shown(d, CheckRow).map(c => [c.text, c.checked]), [["Gray", true]])
            compare(shown(d, Dropdown)[1].pairs, [["gray", "Gray"]])
            mouseClick(shown(d, CheckRow)[0])
            mouseClick(btn(side(d), "OK"))
            tryCompare(d, "visible", false)
            compare(doc.layer(id).props.fx, { channels: { gray: false } })
            doc.run({ op: "setMode", mode: "RGB" })
            const e = open()
            compare(shown(e, CheckRow).map(c => c.text), ["R", "G", "B"])
            compare(shown(e, Dropdown)[1].pairs.length, 4)
        }
        // The artboards' boxes: number fields and drop-downs 26 high (U23); Blend If's halves stay 24 (SplitSlider).
        function test_box_heights() {
            const d = open()
            for (const k of ["blendingOptions", "dropShadow", "stroke", "colorOverlay", "gradientOverlay"]) {
                d.page = k
                settle(d)
                const boxes = shown(d, NumberField).concat(shown(d, Dropdown))
                compare(boxes.map(o => o.height), boxes.map(() => 26), k)
                // .fld input{text-align:right}, the unit apart in t3 ("35 %") (U23)
                const nf = shown(d, NumberField)
                compare(nf.map(f => [f.unitApart, f.horizontalAlignment]), nf.map(() => [true, TextInput.AlignRight]), k)
                const caps = root.all(page(d), o => o instanceof Text && o.visible && o.font.capitalization === Font.AllUppercase && o.parent instanceof ColumnLayout)  // not the one by the title
                compare(caps.map(c => c.height), caps.map(() => 14), k + ": .cap, 10.5 px × 1.35")
            }
        }

        // ---- the rest ---------------------------------------------------------------------------------------------
        // Đặt làm mặc định keeps the page's values for effects turned on from now on; Về mặc định brings them back.
        function test_defaults() {
            const doc = Docs.current
            let d = open({ page: "dropShadow" })
            type(field(d, 0), "50")
            mouseClick(btn(page(d), "Đặt làm mặc định"))
            const kept = Prefs.value("layerStyleDefaults").dropShadow
            compare(kept.opacity, 50)
            verify(!("on" in kept), "on is no default")
            type(field(d, 0), "70")
            type(field(d, 2), "99")
            mouseClick(btn(page(d), "Về mặc định"))
            compare([field(d, 0).value, field(d, 2).value], [50, 12])
            verify(d.v.dropShadow.on, "still on")
            mouseClick(btn(side(d), "Hủy"))
            tryCompare(d, "visible", false)
            d = open({ page: "dropShadow", layer: doc.layers.find(l => l.props.name === "Heal").id })
            compare(field(d, 0).value, 50, "a new effect starts so")
        }
        function test_gradient_overlay() {
            const d = open({ page: "gradientOverlay" })
            type(field(d, 1), "30")
            type(field(d, 2), "50")
            mouseClick(btn(page(d), "Đặt lại căn chỉnh"))
            compare([d.v.gradientOverlay.angle, d.v.gradientOverlay.scale], [90, 100])
            const g = shown(d, Dropdown)[1]
            compare(g.options.length, 15, "the gradients of the presets")
            mouseClick(g)
            tryVerify(() => g.popup.opened)
            waitForRendering(g.popup.contentItem)
            const row = root.find(g.popup.contentItem, o => o instanceof ItemDelegate && o.text === "Tím cam")
            verify(row)
            compare(root.find(row, o => o instanceof Canvas).css, "linear-gradient(135deg, #2b1d4e, #c0587e, #f7c873)")
            mouseClick(row)
            compare(d.v.gradientOverlay.gradient, [2, 0])
            tryVerify(() => !g.popup.visible)
            compare(root.find(g, o => o instanceof Canvas).css, "linear-gradient(135deg, #2b1d4e, #c0587e, #f7c873)", "the box shows it")
            mouseClick(shown(d, CheckRow)[1])
            verify(d.v.gradientOverlay.reverse)
            compare(Docs.current.active.props.fx.gradientOverlay.gradient, [2, 0])
        }
        // Layer › Layer Style (U4): each item opens its page; the dialog is Main's, gone once closed; --state too.
        function test_menu() {
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)  // the menu bar placed
            const doc = Docs.current, n = doc.history.length
            const bar = root.find(w.contentItem, o => o instanceof AppMenu), layer = bar.menuAt(3)
            mouseClick(bar.itemAt(3))
            tryVerify(() => layer.opened)
            waitForRendering(w.contentItem)
            const row = (m, label) => Array.from({ length: m.count }, (x, i) => m.itemAt(i)).find(it => it instanceof MenuItem && it.text === label)
            const style = row(layer, "Layer Style")
            mouseClick(style)
            tryVerify(() => style.subMenu.opened)
            waitForRendering(w.contentItem)
            const shadow = row(style.subMenu, "Drop Shadow…")
            verify(shadow.enabled, "not greyed: the dialog is in the module")
            mouseClick(shadow)
            let d = null
            tryVerify(() => (d = w.dialogs[0]) && d.opened)
            verify(d instanceof LayerStyleDialog)
            compare(d.page, "dropShadow")
            verify(d.v.dropShadow.on)
            settle(d)
            mouseClick(btn(side(d), "OK"))
            tryVerify(() => w.dialogs.length === 0, 2000, "destroyed once closed")
            compare(doc.history.length, n + 1)
            compare(doc.history[n].name, "Layer Style")
            const items = MenuSpec.MENUS[3].items.find(it => it.l === "Layer Style").sub.filter(it => it.dialog)
            compare(items.map(it => it.params.page), ["blendingOptions", "dropShadow", "stroke", "colorOverlay", "gradientOverlay"])
            for (const it of items) {
                verify(w.act(it))
                tryVerify(() => (d = w.dialogs[0]) && d.opened)
                compare(d.page, it.params.page, it.l)
                d.close()
                tryVerify(() => w.dialogs.length === 0)
            }
            compare(doc.history.length, n + 1, "Hủy left no step")
            w.start("LayerStyle:gradientOverlay")
            tryVerify(() => (d = w.dialogs[0]) && d.opened)
            compare(d.page, "gradientOverlay")
            d.close()
            tryVerify(() => w.dialogs.length === 0)
        }
        // Tab reaches OK, Enter presses it.
        function test_keyboard() {
            const doc = Docs.current, n = doc.history.length
            const d = open({ page: "stroke" })
            const ok = btn(side(d), "OK")
            for (let k = 0; k < 80 && !ok.activeFocus; k++)
                keyClick(Qt.Key_Tab)
            verify(ok.activeFocus, "Tab gets to OK")
            keyClick(Qt.Key_Return)
            tryCompare(d, "visible", false)
            compare(doc.history.length, n + 1)
            compare(doc.active.props.fx.stroke.on, true)
        }
    }
}
