import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty
import "../qml/Mock.js" as Mock

// U9: the Layers panel (LayersPanel, LayerRow) on the portrait of the artboards: its rows, picking, the eye, renaming,
// Blend, Opacity, Fill, the locks, groups, dragging, the effects, the menus; each change a Doc command, one History
// step, Hoàn tác giving back what was; 1.000 layers scrolled a row a step within a frame.
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
        name: "U9"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanupTestCase() { init() }

        // Main as the Main artboard, its Properties group folded unless asIs: the Layers panel's room then does not follow
        // what Properties shows for the layer picked (U10's pages differ in height).
        function open(asIs) {
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            if (!asIs)
                Tools.dockMin = Object.assign({}, Tools.dockMin, { p: true })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function settle(w) { waitForRendering(w.contentItem) }
        function panel(w) { return find(w.contentItem, o => o instanceof LayersPanel && o.visible) }
        function list(w) { return find(panel(w), o => o instanceof ListView) }
        // The rows made and shown, top first; one by its text (an effect's row by its effect).
        function rows(w) {
            const p = panel(w)
            return all(p, o => o instanceof LayerRow && o.visible && !!o.entry && o.y + o.height > list(w).contentY && o.y < list(w).contentY + list(w).height)
                .sort((a, b) => a.y - b.y)
        }
        function names(w) { return rows(w).map(r => r.text) }
        // Brought into view first.
        function row(w, text, fx) {
            const p = panel(w), i = p.shown.findIndex(x => (x.fx ? p.effectName(x.fx) : x.l.props.name) === text && x.fx === (fx || ""))
            if (i < 0)
                return null
            list(w).positionViewAtIndex(i, ListView.Contain)
            list(w).forceLayout()
            return rows(w).find(r => r.text === text && r.fx === (fx || "")) || null
        }
        function named(name) { return Docs.current.layers.find(l => l.props.name === name) || null }
        function control(w, type, name) { return find(panel(w), o => o instanceof type && o.visible && o.Accessible.name === name) }
        function button(w, name) { return control(w, ToolToggle, name) }
        function field(w, label) { return find(panel(w), o => o instanceof NumberField && o.label === label) }
        function steps() { return Docs.current.history.map(h => h.name) }
        function last() { return Docs.current.history[Docs.current.index].name }
        function snapshot() { return JSON.stringify([Docs.current.tree, Docs.current.selected]) }
        function tree() { return JSON.stringify(Docs.current.tree) }  // a pick joins the step History is on: undo keeps it
        // The x of a part of row r ("thumb", "mask", "name"), from its own hit test.
        function xOf(r, part) {
            for (let x = 31; x < r.width; x++)
                if (r.partAt(x) === part)
                    return x + 3
            return -1
        }
        function click(w, item, x, y, button, mods) {
            verify(item, "nothing to click")
            mouseClick(item, x === undefined ? item.width / 2 : x, y === undefined ? item.height / 2 : y, button || Qt.LeftButton, mods || Qt.NoModifier)
            settle(w)
        }
        function pick(w, name, mods) { click(w, row(w, name), xOf(row(w, name), "name"), undefined, Qt.LeftButton, mods) }
        function eye(w, r, mods) { click(w, r, 15, r.height / 2, Qt.LeftButton, mods) }
        // The panel's menu (the right button's, the foot's) open, and its row of that text.
        function popup(w) {
            const res = panel(w).resources
            for (let i = 0; i < res.length; i++)
                if (res[i] instanceof PopupMenu)
                    return res[i]
            return null
        }
        function texts(m) {
            const out = []
            for (let i = 0; i < m.count; i++)
                out.push(m.itemAt(i) instanceof MenuItem ? m.itemAt(i).text : "-")
            return out
        }
        function item(m, text) {
            for (let i = 0; i < m.count; i++)
                if (m.itemAt(i) instanceof MenuItem && m.itemAt(i).text === text)
                    return m.itemAt(i)
            return null
        }
        function choose(w, m, text) {
            tryVerify(() => m.opened)
            settle(w)
            const it = item(m, text)
            verify(it, "no " + text + " in the menu")
            mouseClick(it)
            settle(w)
        }
        function contextMenu(w, name) {
            const r = row(w, name)
            click(w, r, xOf(r, "name"), r.height / 2, Qt.RightButton)
            const m = popup(w)
            tryVerify(() => m.opened)
            settle(w)
            return m
        }
        // A left drag on row r from its name by dy, Alt held at the release.
        function drag(w, r, dy, mods) {
            const x = xOf(r, "name"), y = r.height / 2
            mousePress(r, x, y)
            for (let k = 1; k <= 5; k++)
                mouseMove(r, x, y + dy * k / 5, -1, Qt.LeftButton)
            mouseRelease(r, x, y + dy, Qt.LeftButton, mods || Qt.NoModifier)
            settle(w)
        }
        function undo(w, n) {
            for (let k = 0; k < (n || 1); k++)
                Docs.current.undo()
            settle(w)
        }

        // ---- rows ---------------------------------------------------------------------------------------------------
        // The portrait as the artboard has it: its nine layers, 32 tall, the group's two 16 in, what each one shows.
        function test_rows() {
            const w = open(true)
            const p = panel(w)
            compare([p.width, p.height], [307, 418])
            compare(names(w), ["Curves 1", "Làm trắng răng", "Làm sáng mắt", "Dodge & Burn", "Tách tần số", "Cao", "Thấp", "Heal", "Background"])
            const r = rows(w)
            compare(r.map(x => x.height), [32, 32, 32, 32, 32, 32, 32, 32, 32])
            compare(r.map(x => x.thumb), ["adj", "adj", "adj", "gray", "", "photo", "photo", "heal", "photo"])
            compare(r.map(x => x.indent), [0, 0, 0, 0, 0, 16, 16, 0, 0])
            compare(r.map(x => !!x.p.mask), [true, true, true, false, false, false, false, false, false])
            compare(r.map(x => x.selected), [false, false, false, true, false, false, false, false, false])
            compare(r.map(x => x.locked), [false, false, false, false, false, false, false, false, true])
            verify(find(r[8], o => o instanceof Text && o.text === "Background").font.italic)
            verify(!find(r[7], o => o instanceof Text && o.text === "Heal").font.italic)
            compare(r[4].open, true)
            compare(row(w, "Thấp").src, Mock.photoUrl(0))
            compare(list(w).y, 97)
            compare(list(w).height, 418 - 97 - 32)
        }
        // Blend, Opacity, Fill greyed without a layer and on the Background; the locks without a layer only.
        function test_head_greyed() {
            const w = open()
            const blend = find(panel(w), o => o instanceof Dropdown)
            const locks = ["Khóa điểm ảnh trong suốt · Lock transparent pixels", "Khóa điểm ảnh · Lock image pixels", "Khóa vị trí · Lock position", "Khóa tất cả · Lock all"]
            const on = () => [blend.enabled, field(w, "Opacity").enabled, field(w, "Fill").enabled].concat(locks.map(n => button(w, n).enabled))
            compare(on(), [true, true, true, true, true, true, true])
            compare([blend.displayText, field(w, "Opacity").value, field(w, "Fill").value], ["Soft Light", 100, 100])
            pick(w, "Background")
            compare(on(), [false, false, false, true, true, true, true])
            compare(locks.map(n => button(w, n).checked), [true, true, true, true], "the Background is locked all")
            list(w).positionViewAtEnd()
            settle(w)
            click(w, find(list(w), o => o instanceof AbstractButton && o.Accessible.name === "Bỏ chọn layer, xem thuộc tính tài liệu"))
            compare(Docs.current.selected, [])
            compare(on(), [false, false, false, false, false, false, false])
        }

        // ---- picking ------------------------------------------------------------------------------------------------
        // A click picks a layer alone, Ctrl adds or takes one off, Shift the rows between; not History steps. The mask's
        // thumbnail: the mask takes the brush, the ring goes round it.
        function test_pick() {
            const w = open()
            const d = Docs.current, n = d.history.length, id = name => named(name).id
            pick(w, "Heal")
            compare(d.selected, [id("Heal")])
            pick(w, "Curves 1", Qt.ShiftModifier)
            compare(d.selected, [id("Heal"), id("Thấp"), id("Cao"), id("Tách tần số"), id("Dodge & Burn"), id("Làm sáng mắt"), id("Làm trắng răng"), id("Curves 1")])
            compare(d.active.props.name, "Curves 1")
            pick(w, "Cao", Qt.ControlModifier)
            verify(d.selected.indexOf(id("Cao")) < 0)
            pick(w, "Cao", Qt.ControlModifier)
            compare(d.selected[d.selected.length - 1], id("Cao"))
            compare(rows(w).filter(r => r.selected).length, 8)
            const r = row(w, "Làm sáng mắt")
            click(w, r, xOf(r, "mask"))
            compare([d.selected, d.editMask, r.editMask], [[id("Làm sáng mắt")], true, true])
            click(w, r, xOf(r, "thumb"))
            compare([d.editMask, r.editMask], [false, false])
            compare(d.history.length, n, "picking is no step")
        }
        // Shift on a mask turns it off (striped) and on again, one step each.
        function test_mask_off() {
            const w = open()
            const r = row(w, "Curves 1")
            click(w, r, xOf(r, "mask"), undefined, Qt.LeftButton, Qt.ShiftModifier)
            compare([named("Curves 1").props.maskOff, last()], [true, "Tắt Layer Mask"])
            verify(row(w, "Curves 1").p.maskOff)
            undo(w)
            verify(!named("Curves 1").props.maskOff)
        }

        // ---- eye ----------------------------------------------------------------------------------------------------
        function test_eye() {
            const w = open()
            const before = snapshot()
            eye(w, row(w, "Heal"))
            compare([named("Heal").props.visible, last(), row(w, "Heal").shown], [false, "Ẩn layer", false])
            eye(w, row(w, "Heal"))
            compare([named("Heal").props.visible, last()], [true, "Hiện layer"])
            undo(w, 2)
            compare(snapshot(), before)
            // Alt: this one alone, and the group it is in; again, all of them.
            const n = Docs.current.index
            eye(w, row(w, "Cao"), Qt.AltModifier)
            compare(Docs.current.layers.filter(l => l.props.visible).map(l => l.props.name), ["Tách tần số", "Cao"])
            compare(Docs.current.index, n + 1, "one step")
            eye(w, row(w, "Cao"), Qt.AltModifier)
            compare(Docs.current.layers.every(l => l.props.visible), true)
            undo(w, 2)
            compare(snapshot(), before)
        }

        // ---- adding, deleting ---------------------------------------------------------------------------------------
        function test_add_delete() {
            const w = open()
            const before = snapshot()
            click(w, button(w, "Layer mới"))
            compare(names(w).slice(3, 5), ["Layer 1", "Dodge & Burn"], "above the active one")
            compare([Docs.current.active.props.name, last(), row(w, "Layer 1").thumb], ["Layer 1", "Layer mới", "empty"])
            click(w, button(w, "Nhóm mới"))
            compare([Docs.current.active.kind, Docs.current.active.props.name, last()], ["group", "Group 1", "Nhóm mới"])
            undo(w, 2)
            compare(snapshot(), before)
            click(w, button(w, "Xóa layer"))
            compare([named("Dodge & Burn"), last(), Docs.current.active.props.name], [null, "Xóa layer", "Tách tần số"])
            undo(w)
            compare(snapshot(), before)
            Docs.select(1)  // san-pham.webp: its Background alone
            settle(w)
            verify(!button(w, "Xóa layer").enabled)
        }
        // The fill and adjustment menu: Solid Color, Gradient, the 16 adjustments, MenuSpec's; one adds its layer.
        function test_adjustment_menu() {
            const w = open()
            click(w, button(w, "Thêm fill hay adjustment layer"))
            const m = popup(w)
            tryVerify(() => m.opened)
            compare(texts(m).length, 19)
            compare(texts(m).slice(0, 4), ["Solid Color…", "Gradient…", "-", "Brightness/Contrast…"])
            verify(m.y + m.height <= panel(w).height - 32 + 1, "above the foot")
            choose(w, m, "Curves…")
            compare([Docs.current.active.kind, Docs.current.active.props.adj, Docs.current.active.props.name, last()], ["adj", "Curves", "Curves 2", "Layer Curves mới"])
            compare(Tools.dockTab.p, "Properties")
            click(w, button(w, "Thêm fill hay adjustment layer"))
            choose(w, popup(w), "Solid Color…")
            compare([Docs.current.active.kind, row(w, Docs.current.active.props.name).thumb], ["fill", "fill"])
            click(w, button(w, "Thêm Layer Mask"))
            compare(last(), "Layer Solid Color mới", "it has a mask: no new one")
            pick(w, "Heal")
            click(w, button(w, "Thêm Layer Mask"))
            compare([named("Heal").props.mask, last()], ["#ffffff", "Thêm Layer Mask"])
        }

        // ---- renaming -----------------------------------------------------------------------------------------------
        // A double click on the name: a field; Enter keeps the name typed, Esc the old one; Layer › Đổi tên layer… too.
        function test_rename() {
            const w = open()
            const r = row(w, "Heal")
            mouseDoubleClickSequence(r, xOf(r, "name"), r.height / 2)
            settle(w)
            const f = control(w, TextBox, "Tên layer")
            verify(f && f.activeFocus)
            compare([f.text, f.selectedText], ["Heal", "Heal"])
            f.text = "Heal da"
            keyClick(Qt.Key_Return)
            settle(w)
            compare([named("Heal da").id, last(), control(w, TextBox, "Tên layer")], [2, "Đổi tên layer", null])
            const n = Docs.current.history.length
            mouseDoubleClickSequence(row(w, "Heal da"), xOf(row(w, "Heal da"), "name"), 16)
            control(w, TextBox, "Tên layer").text = "Không"
            keyClick(Qt.Key_Escape)
            settle(w)
            compare([named("Heal da").id, Docs.current.history.length], [2, n])
            pick(w, "Cao")
            w.act({ act: "@rename" })  // Layer › Đổi tên layer…
            settle(w)
            const g = control(w, TextBox, "Tên layer")
            verify(g && g.activeFocus)
            g.text = "  Tần số cao  "
            keyClick(Qt.Key_Return)
            compare(Docs.current.active.props.name, "Tần số cao")
            undo(w, 2)
            compare([named("Heal").id, named("Cao").id], [2, 4])
        }
        // A double click elsewhere on the row: Layer Style, for that layer.
        function test_layer_style() {
            const w = open()
            const r = row(w, "Heal")
            mouseDoubleClickSequence(r, xOf(r, "thumb"), r.height / 2)
            settle(w)
            const d = w.dialogs[w.dialogs.length - 1]
            verify(d instanceof LayerStyleDialog)
            compare([d.page, d.layerId], ["blendingOptions", 2])
            d.close()
            tryVerify(() => !w.dialogs.length)
            const bg = row(w, "Background")
            mouseDoubleClickSequence(bg, xOf(bg, "thumb"), bg.height / 2)
            settle(w)
            compare(w.dialogs.length, 0, "none for the Background")
            // the foot's fx: Layer Style's pages, as Layer › Layer Style
            pick(w, "Heal")
            click(w, find(panel(w), o => o instanceof ToolToggle && o.Accessible.name === "Thêm layer style"))
            choose(w, popup(w), "Stroke…")
            compare([w.dialogs[0].page, w.dialogs[0].layerId], ["stroke", 2])
            w.dialogs[0].close()
            tryVerify(() => !w.dialogs.length)
        }

        // ---- Blend, Opacity, Fill, locks ----------------------------------------------------------------------------
        function test_blend() {
            const w = open()
            const blend = find(panel(w), o => o instanceof Dropdown)
            compare(blend.options.length, 27)
            verify(!blend.options.some(o => o[0] === "passThrough"))
            click(w, blend)
            tryVerify(() => blend.popup.opened)
            settle(w)
            const it = find(blend.popup.contentItem, o => o instanceof ItemDelegate && o.text === "Hard Light")  // in view: the list opens at Soft Light
            click(w, it)
            compare([Docs.current.active.props.blend, last(), blend.displayText], ["hardLight", "Blend mode", "Hard Light"])
            pick(w, "Tách tần số")
            compare([blend.options.length, blend.options[0][0], blend.displayText], [28, "passThrough", "Pass Through"])
            undo(w)
            compare(named("Dodge & Burn").props.blend, "softLight")
        }
        // A drag on the label over 100 values is one step; typing a value, one more.
        function test_opacity_fill() {
            const w = open()
            const op = field(w, "Opacity"), n = Docs.current.history.length
            const label = find(op, o => o instanceof Text && o.text === "Opacity")
            mousePress(label, 5, 8)
            for (let k = 1; k <= 20; k++)
                mouseMove(label, 5 - 6 * k, 8, -1, Qt.LeftButton)
            mouseRelease(label, 5 - 120, 8)
            settle(w)
            compare([named("Dodge & Burn").props.opacity, op.value, Docs.current.history.length, last()], [0, 0, n + 1, "Opacity"])
            const input = find(op, o => o instanceof TextField)
            mouseClick(input)
            tryVerify(() => op.editing)
            input.text = "45"
            keyClick(Qt.Key_Return)
            settle(w)
            compare([named("Dodge & Burn").props.opacity, Docs.current.history.length], [0.45, n + 2])
            const fill = field(w, "Fill")
            const fl = find(fill, o => o instanceof Text && o.text === "Fill")
            mousePress(fl, 5, 8)
            for (let k = 1; k <= 10; k++)
                mouseMove(fl, 5 - 3 * k, 8, -1, Qt.LeftButton)
            mouseRelease(fl, 5 - 30, 8)
            settle(w)
            compare([named("Dodge & Burn").props.fill, Docs.current.history.length, last()], [0.7, n + 3, "Fill"])
            undo(w, 3)
            compare([named("Dodge & Burn").props.opacity, named("Dodge & Burn").props.fill], [1, 1])
            // with two layers picked, both
            pick(w, "Heal", Qt.ControlModifier)
            mouseClick(input)
            input.text = "50"
            keyClick(Qt.Key_Return)
            compare([named("Heal").props.opacity, named("Dodge & Burn").props.opacity, Docs.current.history.length], [0.5, 0.5, n + 1])
        }
        // All on its own, or the other three: one takes all off; a step each.
        function test_locks() {
            const w = open()
            pick(w, "Heal")
            const before = snapshot()
            const k = n => button(w, n)
            const p = () => { const x = named("Heal").props; return [x.lockTransparency, x.lockPixels, x.lockPosition, x.lockAll] }
            click(w, k("Khóa vị trí · Lock position"))
            compare([p(), last(), row(w, "Heal").locked], [[false, false, true, false], "Khóa layer", true])
            click(w, k("Khóa điểm ảnh trong suốt · Lock transparent pixels"))
            compare(p(), [true, false, true, false])
            click(w, k("Khóa tất cả · Lock all"))
            compare(p(), [false, false, false, true])
            verify(k("Khóa điểm ảnh · Lock image pixels").checked)
            click(w, k("Khóa điểm ảnh · Lock image pixels"))
            compare(p(), [false, true, false, false])
            click(w, k("Khóa điểm ảnh · Lock image pixels"))
            compare([p(), row(w, "Heal").locked], [[false, false, false, false], false])
            undo(w, 5)
            compare(snapshot(), before)
        }

        // ---- groups -------------------------------------------------------------------------------------------------
        // The right button's Nhóm layer on two layers picked; the twist folds and opens it, no step.
        function test_group() {
            const w = open()
            const before = tree()
            pick(w, "Heal", Qt.ControlModifier)
            choose(w, contextMenu(w, "Heal"), "Nhóm layer")
            compare(names(w).slice(3), ["Group 1", "Dodge & Burn", "Heal", "Tách tần số", "Cao", "Thấp", "Background"])
            compare([named("Dodge & Burn").parent, named("Heal").parent], [named("Group 1").id, named("Group 1").id])
            compare([row(w, "Heal").indent, last()], [16, "Nhóm layer"])
            const n = Docs.current.history.length
            click(w, find(row(w, "Group 1"), o => o instanceof AbstractButton && o.Accessible.name === "Mở hay thu nhóm Group 1"))
            compare(names(w).slice(3, 5), ["Group 1", "Tách tần số"])
            verify(!row(w, "Group 1").open)
            click(w, find(row(w, "Group 1"), o => o instanceof AbstractButton && o.Accessible.name === "Mở hay thu nhóm Group 1"))
            compare(names(w)[4], "Dodge & Burn")
            compare(Docs.current.history.length, n)
            undo(w)
            compare(tree(), before)
        }

        // ---- dragging -----------------------------------------------------------------------------------------------
        // Above a row, below it, into a group's middle; Alt, copies; a step each; the Background stays at the bottom.
        function test_drag() {
            const w = open()
            const before = tree()
            drag(w, row(w, "Heal"), -32 * 4 - 8)  // the upper half of Dodge & Burn
            compare(names(w).slice(2, 5), ["Làm sáng mắt", "Heal", "Dodge & Burn"])
            compare([last(), Docs.current.selected], ["Sắp xếp layer", [2]])
            undo(w)
            compare(tree(), before)
            drag(w, row(w, "Heal"), -32 * 3)  // Tách tần số's middle
            compare([named("Heal").parent, names(w).slice(4, 6)], [named("Tách tần số").id, ["Tách tần số", "Heal"]])
            undo(w)
            drag(w, row(w, "Curves 1"), 32 * 7 + 12)  // the lower half of Heal
            compare(names(w).slice(6), ["Heal", "Curves 1", "Background"])
            undo(w)
            const n = Docs.current.index
            drag(w, row(w, "Heal"), 32 + 12)  // below the Background: not there
            compare([tree(), Docs.current.index], [before, n])
            drag(w, row(w, "Dodge & Burn"), -32 * 3 - 8, Qt.AltModifier)  // a copy above Curves 1
            compare(names(w).slice(0, 5), ["Dodge & Burn bản sao", "Curves 1", "Làm trắng răng", "Làm sáng mắt", "Dodge & Burn"])
            compare([last(), Docs.current.active.props.name], ["Nhân bản layer", "Dodge & Burn bản sao"])
            undo(w)
            compare(tree(), before)
            // two picked go together, in their order
            pick(w, "Curves 1", Qt.ControlModifier)
            drag(w, row(w, "Curves 1"), 32 * 7 - 8)  // the upper half of Heal
            compare(names(w).slice(3), ["Cao", "Thấp", "Curves 1", "Dodge & Burn", "Heal", "Background"])
            compare([Docs.current.index, last()], [n + 1, "Sắp xếp layer"])
        }

        // ---- effects ------------------------------------------------------------------------------------------------
        // A layer with a style: fx, then Hiệu ứng and a row per effect, each with its eye; fx folds them.
        function test_effects() {
            const w = open()
            const fx = { dropShadow: { on: true, blend: "multiply", color: "#2b1d4e", opacity: 35, angle: 120, distance: 12, spread: 0, size: 24, noise: 0, knockOut: true },
                stroke: { on: false, size: 6, position: "outside", blend: "normal", opacity: 100, color: "#f7c873" } }
            Docs.current.toggleOpen(named("Tách tần số").id)  // room for the four rows
            Docs.current.run({ op: "setProps", layer: named("Heal").id, props: { fx: fx } })
            settle(w)
            compare(names(w).slice(5), ["Heal", "Hiệu ứng", "Drop Shadow", "Stroke", "Background"])
            compare(rows(w).slice(5, 9).map(r => r.shown), [true, true, true, false])
            compare(rows(w).slice(6, 8).map(r => r.indent), [30, 42])
            verify(row(w, "Heal").hasFx)
            const n = Docs.current.history.length
            eye(w, row(w, "Stroke", "stroke"))
            compare([named("Heal").props.fx.stroke.on, named("Heal").props.fx.dropShadow.on, last(), Docs.current.history.length], [true, true, "Layer Style", n + 1])
            eye(w, row(w, "Hiệu ứng", "effects"))
            compare([named("Heal").props.fx.dropShadow.on, named("Heal").props.fx.stroke.on], [false, false])
            eye(w, row(w, "Hiệu ứng", "effects"))
            compare([named("Heal").props.fx.dropShadow.on, named("Heal").props.fx.stroke.on], [true, true])
            click(w, find(row(w, "Heal"), o => o instanceof AbstractButton && o.Accessible.name === "Hiệu ứng của Heal"))
            compare(names(w).slice(5), ["Heal", "Background"])
            compare(Docs.current.history.length, n + 3, "folding is no step")
            click(w, find(row(w, "Heal"), o => o instanceof AbstractButton && o.Accessible.name === "Hiệu ứng của Heal"))
            const ds = row(w, "Drop Shadow", "dropShadow")
            mouseDoubleClickSequence(ds, 120, 16)
            settle(w)
            compare([w.dialogs[0].page, w.dialogs[0].layerId], ["dropShadow", 2])
            w.dialogs[0].close()
            tryVerify(() => !w.dialogs.length)
            undo(w, 4)
            compare(named("Heal").props.fx, null)
        }

        // ---- menus --------------------------------------------------------------------------------------------------
        // The right button: the row picked, MenuSpec's Layer items but those making layers or aligning, the labels.
        function test_context_menu() {
            const w = open()
            const m = contextMenu(w, "Heal")
            compare(Docs.current.selected, [2])
            const t = texts(m)
            compare(t.slice(0, 3), ["Nhân bản layer…", "Đổi tên layer…", "Xóa"])
            for (const x of ["Layer Style", "Layer Mask", "Tạo Clipping Mask", "Smart Objects", "Nhóm layer", "Bỏ nhóm", "Sắp xếp", "Khóa layer…", "Gộp xuống", "Màu nhãn"])
                verify(t.indexOf(x) >= 0, x)
            for (const x of ["Mới", "Fill Layer mới", "Adjustment Layer mới", "Căn", "Phân bố", "Liên kết layer"])
                verify(t.indexOf(x) < 0, x)
            verify(!t.some((x, i) => x === "-" && (i === 0 || t[i - 1] === "-")), "no two rules in a row")
            verify(!item(m, "Bỏ nhóm").enabled, "Heal is no group")
            choose(w, m, "Nhân bản layer…")
            compare([names(w)[7], last()], ["Heal bản sao", "Nhân bản layer"])
            // a colour label, on the left edge
            const l = contextMenu(w, "Heal")
            const sub = item(l, "Màu nhãn").subMenu
            mouseClick(item(l, "Màu nhãn"))
            tryVerify(() => sub.opened)
            settle(w)
            choose(w, sub, "Cam")
            compare(named("Heal").props.tag, "#e5a33a")
            const r = row(w, "Heal")
            verify(find(r, o => o instanceof Rectangle && o.width === 3 && o.visible && Qt.colorEqual(o.color, "#e5a33a")))
        }
        // A press outside a menu of the panel closes it and goes no further: not onto the canvas.
        function test_menu_holds_presses() {
            const w = open()
            const vp = find(w.contentItem, o => o instanceof Viewport)
            const probe = Qt.createQmlObject("import QtQuick; MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; property int n: 0; onPressed: n++ }", vp)
            const m = contextMenu(w, "Heal")
            mouseClick(vp, 300, 300)
            tryVerify(() => !m.visible)
            compare(probe.n, 0)
            const blend = find(panel(w), o => o instanceof Dropdown)
            click(w, blend)
            tryVerify(() => blend.popup.opened)
            mouseClick(vp, 300, 300)
            tryVerify(() => !blend.popup.visible)
            compare(probe.n, 0)
            mouseClick(vp, 300, 300)
            compare(probe.n, 1)
            probe.destroy()
        }
        // The panel's menu: thumbnails Nhỏ, Vừa, Lớn, kept in Tools while tabs change.
        function test_thumb_size() {
            const w = open()
            const p = panel(w)
            compare(p.menu.map(i => [i.l, i.check]), [["Thumbnail nhỏ", false], ["Thumbnail vừa", true], ["Thumbnail lớn", false]])
            p.menu[2].act()
            settle(w)
            compare([rows(w)[0].height, rows(w)[0].size], [48, 40])
            Tools.showPanel("Channels")
            settle(w)
            Tools.showPanel("Layers")
            settle(w)
            compare([rows(w)[0].height, panel(w).menu[2].check], [48, true])
            panel(w).menu[0].act()
            settle(w)
            compare(rows(w)[0].height, 24)
        }

        // ---- filter -------------------------------------------------------------------------------------------------
        // By name (no matter the accents) and by kind, kept while tabs change.
        function test_filter() {
            const w = open()
            const box = control(w, TextBox, "Tìm layer theo tên")
            mouseClick(box)
            keyClick(Qt.Key_L)
            keyClick(Qt.Key_A)
            keyClick(Qt.Key_M)
            settle(w)
            compare(names(w), ["Làm trắng răng", "Làm sáng mắt"])
            click(w, button(w, "Lọc: adjustment layer · Filter for adjustment layers"))
            compare(names(w), ["Làm trắng răng", "Làm sáng mắt"])
            mouseClick(box)
            box.selectAll()
            keyClick(Qt.Key_Backspace)
            settle(w)
            compare(names(w), ["Curves 1", "Làm trắng răng", "Làm sáng mắt"])
            Tools.showPanel("Channels")
            settle(w)
            Tools.showPanel("Layers")
            settle(w)
            compare(names(w), ["Curves 1", "Làm trắng răng", "Làm sáng mắt"])
            click(w, button(w, "Lọc: adjustment layer · Filter for adjustment layers"))
            click(w, button(w, "Lọc: smart object · Filter for smart objects"))
            compare(names(w), [])
        }

        // ---- keyboard -----------------------------------------------------------------------------------------------
        // A row is a button: Tab reaches it, Enter picks the layer; Tab on, its eye.
        function test_keyboard() {
            const w = open()
            const b = find(row(w, "Heal"), o => o instanceof AbstractButton && o.Accessible.name === "Chọn layer Heal")
            b.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            settle(w)
            compare(Docs.current.selected, [2])
            keyClick(Qt.Key_Tab)
            verify(find(row(w, "Heal"), o => o instanceof AbstractButton && o.Accessible.name === "Ẩn hiện Heal").activeFocus)
            keyClick(Qt.Key_Return)
            compare([named("Heal").props.visible, last()], [false, "Ẩn layer"])
        }

        // ---- many layers --------------------------------------------------------------------------------------------
        // 1.000 layers of every kind, made with Doc's addLayer on a document not shown, then shown; the ms they took.
        function thousand(w) {
            const d = Docs.create({ name: "nghin-layer.psd", w: 2000, h: 2000, ppi: 72, mode: "RGB", bits: 8, background: "white" })
            Docs.select(0)
            const t0 = Date.now()
            const kinds = [["pixel", { photo: 0 }], ["pixel", { thumb: "empty", mask: "radial-gradient(ellipse 30% 26% at 57% 42%, #ffffff 0 70%, #000000 100%)" }],
                ["text", {}], ["shape", { shape: "ellipS", color: "#c0587e" }], ["adj", { adj: "Curves", mask: "#ffffff", vals: {} }], ["so", { photo: 7 }],
                ["fill", { color: "#b3202a", mask: "#ffffff" }], ["pixel", { thumb: "gray", blend: "softLight" }], ["pixel", { photo: 0, look: "blur(1.4px)" }]]
            let g = 0
            for (let k = 1; d.layers.length < 1000; k++) {
                if (k % 10 === 0) {  // a group, holding the next four
                    verify(d.run({ op: "addLayer", parent: 0, index: d.tree.children.length, kind: "group", props: { name: "Nhóm " + k, blend: "passThrough" } }))
                    g = d.active.id
                    continue
                }
                const c = kinds[k % kinds.length], parent = g && k % 10 < 5 ? g : 0
                verify(d.run({ op: "addLayer", parent: parent, index: parent ? d.layer(parent).count : d.tree.children.length, kind: c[0],
                               props: Object.assign({ name: "Layer " + k }, c[1]) }))
            }
            const made = Date.now() - t0
            console.log("1.000 layers made in " + made + " ms")
            Docs.select(Docs.docs.indexOf(d))
            settle(w)
            return made
        }
        // A row a step, quick scrolling (as snapone's test_grid_thousands): the work of a step within a 60 Hz frame,
        // and only the rows in view made. Other jobs on the machine only add time: the best of three tries counts, and the
        // frame is stretched by how much longer than on the idle dev laptop (under 2 s) the layers took to make.
        function test_thousand() {
            const w = open()
            const slow = Math.max(1, thousand(w) / 2000)
            const l = list(w)
            compare(l.count, 1000)
            let best = Infinity
            for (let t = 0; t < 3 && best >= 16 * slow; t++) {
                const steps = []
                for (let k = 0; k < 60; k++) {
                    const t0 = Date.now()
                    l.contentY += panel(w).rowHeight
                    l.forceLayout()
                    steps.push(Date.now() - t0)
                    if (k % 10 === 0)
                        waitForRendering(l)
                }
                steps.sort((a, b) => a - b)
                console.log("1.000 layers: a row a step, median " + steps[30] + " ms, worst " + steps[59] + " ms")
                best = Math.min(best, steps[30])
            }
            verify(best < 16 * slow, "under a frame, " + slow.toFixed(1) + " × the idle laptop's")
            verify(all(panel(w), o => o instanceof LayerRow).length < 40, "only the rows in view")
        }
        // A change keeps the view where it is, though the count changes; a new layer comes into view.
        function test_view_kept() {
            const w = open()
            thousand(w)
            const l = list(w)
            l.contentY = 32 * 500
            settle(w)
            pick(w, rows(w)[3].text)
            const y = l.contentY
            verify(y > 32 * 490, "still there: " + y)
            eye(w, rows(w)[2])
            compare(l.contentY, y)
            click(w, button(w, "Layer mới"))
            compare(l.contentY, y, "a new count, the same place")
            verify(rows(w).some(r => r.entry.id === Docs.current.active.id), "the new layer in view")
            const g = rows(w).find(r => r.kind === "group" && r.open && r.entry.count > 0)
            verify(g, "a group in view")
            click(w, find(g, o => o instanceof AbstractButton && o.Accessible.name === "Mở hay thu nhóm " + g.text))
            compare(l.contentY, y)
            compare(l.count, 1001 - 4)
        }
    }
}
