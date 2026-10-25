import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtTest
import PhotoBeauty
import "../qml/Mock.js" as Mock
import "../qml/MenuSpec.js" as MenuSpec
import "../qml/ToolSpec.js" as ToolSpec
import "../qml/Presets.js" as Presets

// U31: Photoshop's keys and Layers clicks the prototype had not, and the menu items it greyed or lacked, on the mock:
// each the mock command it stands for (one History step, or the layers picked; a tool's own in Tools), Hoàn tác giving
// back what was; no key on two shortcuts in one context, by the tables (the menus, MenuSpec.KEYS, the toolbar's, Main's,
// the dock's, the menu bar's Alt letters) and by the window's Shortcuts on; Phím tắt's tab Khác, found by English
// names; on a real document what the core has runs, the rest is greyed and does nothing.
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
        name: "U31"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
            MenuSpec.setDocCan(n => Docs.can(n))  // this file's MenuSpec.js (another URL than the module's), as Main sets its own
        }
        function cleanupTestCase() { init() }

        // Photoshop 2025's keys (Windows) and what each stands for: the act of MenuSpec.KEYS' row Main registers them by.
        readonly property var expected: [
            ["Alt+[", "@layerSel:down"], ["Alt+]", "@layerSel:up"], ["Alt+,", "@layerSel:bottom"], ["Alt+.", "@layerSel:top"],
            ["Shift+Alt+[", "@layerSel:addDown"], ["Shift+Alt+]", "@layerSel:addUp"], ["Ctrl+Shift+Alt+E", "@stampVisible"],
            ["Ctrl+Alt+E", "@stampLayers"], ["Ctrl+Shift+Alt+N", "@newLayer"], ["\\", "@maskView:overlay"],
            ["Alt+Backspace", "@fillWith:foreground"], ["Ctrl+Backspace", "@fillWith:background"],
            ["Shift+Alt+Backspace", "@fillWith:foreground,keep"], ["Ctrl+Shift+Backspace", "@fillWith:background,keep"],
            ["Shift+Backspace", "Fill"], ["Left", "@nudge:Left"], ["Right", "@nudge:Right"], ["Up", "@nudge:Up"], ["Down", "@nudge:Down"],
            ["Shift+Left", "@nudge:Shift+Left"], ["Shift+Right", "@nudge:Shift+Right"], ["Shift+Up", "@nudge:Shift+Up"],
            ["Shift+Down", "@nudge:Shift+Down"], ["Ctrl+Shift+Alt+T", "@transformAgain"], ["Ctrl+Tab", "@nextDoc:1"],
            ["Ctrl+Shift+Tab", "@nextDoc:-1"], [",", "@brushPreset:-1"], [".", "@brushPreset:1"], ["Shift+,", "@brushPreset:first"],
            ["Shift+.", "@brushPreset:last"], ["Shift++", "@blend:+1"], ["Shift+-", "@blend:-1"]
        ].concat(Object.keys(letters).map(k => ["Shift+Alt+" + k, "@blend:" + letters[k]]))
        // Alt ⇧ and a letter: the blend mode (Q, R a painting tool's only).
        readonly property var letters: ({ N: "normal", I: "dissolve", Q: "behind", R: "clear", K: "darken", M: "multiply", B: "colorBurn", A: "linearBurn",
            G: "lighten", S: "screen", D: "colorDodge", W: "linearDodge", O: "overlay", F: "softLight", H: "hardLight", V: "vividLight", J: "linearLight",
            Z: "pinLight", L: "hardMix", E: "difference", X: "exclusion", U: "hue", T: "saturation", C: "color", Y: "luminosity" })

        // ---- helpers ------------------------------------------------------------------------------------------------
        // Main as the Main artboard (chân dung.psd, Dodge & Burn picked, Brush), its Properties folded: Layers' rows in view.
        function open() {
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            Tools.dockMin = Object.assign({}, Tools.dockMin, { p: true })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function settle(w) { waitForRendering(w.contentItem) }
        function press(w, seq) {  // a key, no field or control holding the focus
            w.contentItem.forceActiveFocus()
            keySequence(seq)
            settle(w)
        }
        function named(name) { return Docs.current.layers.find(l => l.props.name === name) || null }
        function id(name) { return named(name).id }
        function pickOnly(names) {
            const ids = names.map(id)
            if (JSON.stringify(Docs.current.selected) !== JSON.stringify(ids))
                verify(Docs.current.run({ op: "selectLayers", layers: ids }), "picked " + names)
        }
        // What History keeps of the document: its layers, those picked, the selection.
        function snap() { const d = Docs.current; return JSON.stringify([d.tree, d.selected, d.selection, d.inverse]) }
        function last() { return Docs.current.history[Docs.current.index] }
        function cmds() { return JSON.parse(JSON.stringify(last().cmds)) }
        // f() makes one History step named `name` (check() looks at what it did), and Hoàn tác gives back what was.
        function oneStep(f, name, what, check) {
            const d = Docs.current, was = snap(), at = d.index
            f()
            compare([d.index, d.history.length], [at + 1, at + 2], what + ": one step")
            compare(last().name, name, what)
            if (check)
                check()
            d.undo()
            compare(snap(), was, what + ": undo gives back what was")
        }
        function nothing(f, what) {  // no step, and what History keeps the same
            const d = Docs.current, was = [snap(), d.index, d.history.length]
            f()
            compare([snap(), d.index, d.history.length], was, what + ": nothing")
        }
        function picks(f, ids, what) {  // the layers picked (the active one last) and no step: Doc adds it to the step it is on
            const d = Docs.current, was = [d.index, d.history.length, JSON.stringify(d.tree)]
            f()
            compare(d.selected, ids, what)
            compare([d.index, d.history.length, JSON.stringify(d.tree)], was, what + ": no step")
        }
        function item(path) {  // a MenuSpec item by its menus' labels: ["Layer", "Mới", /Background/]
            let items = MenuSpec.MENUS.find(m => m.title.replace("&", "") === path[0]).items, it = null
            for (const l of path.slice(1)) {
                it = items.find(x => x.l !== undefined && (l instanceof RegExp ? l.test(x.l) : x.l === l))
                verify(it, String(l))
                items = it.sub || []
            }
            return it
        }
        // Layers (as tst_u9): a row by its layer's name, brought into view; a click on a part of it.
        function panel(w) { return find(w.contentItem, o => o instanceof LayersPanel && o.visible) }
        function row(w, name) {
            const p = panel(w), l = find(p, o => o instanceof ListView), i = p.shown.findIndex(x => !x.fx && x.l.props.name === name)
            verify(i >= 0, name)
            l.positionViewAtIndex(i, ListView.Contain)
            l.forceLayout()
            return all(p, o => o instanceof LayerRow && l.itemAtIndex(o.index) === o && !o.fx && o.text === name)[0] || null
        }
        function xOf(r, part) {
            for (let x = 31; x < r.width; x++)
                if (r.partAt(x) === part)
                    return x + 3
            return -1
        }
        function clickRow(w, name, part, mods, y) {
            const r = row(w, name)
            verify(r && xOf(r, part) > 0, name + " › " + part)
            mouseClick(r, xOf(r, part), y === undefined ? r.height / 2 : y, Qt.LeftButton, mods || Qt.NoModifier)
            settle(w)
        }
        function dialogOn(w, type) {
            let d = null
            tryVerify(() => (d = w.dialogs.find(x => x instanceof type) || null) && d.opened)
            return d
        }
        function button(d, text) { return find(d.contentItem.parent, o => o instanceof T.AbstractButton && o.text === text && o.visible) }
        // The Shortcuts of the window (Main's, the toolbar's, the dock's…), as tst_u20's; their sequences as written.
        function shortcuts(w) {
            const out = []
            const walk = o => {
                if (o instanceof Shortcut)
                    out.push(o)
                if (o instanceof Instantiator)
                    for (let i = 0; i < o.count; i++)
                        walk(o.objectAt(i))
                for (const r of o.resources || [])
                    walk(r)
                for (const c of o.children || [])
                    walk(c)
            }
            walk(w.contentItem)
            return out
        }
        function sequencesOf(s) { return (s.sequences && s.sequences.length ? s.sequences : [s.sequence]).map(String) }
        // "Shift+Ctrl+z" → "Ctrl+Shift+Z": the modifiers in one order, the key as Qt names it.
        function norm(seq) {
            const m = /^((?:(?:Ctrl|Shift|Alt|Meta)\+)*)(.+)$/.exec(String(seq)), mods = m[1].split("+")
            const key = { Esc: "Escape", Return: "Enter" }[m[2]] || (m[2].length === 1 ? m[2].toUpperCase() : m[2])
            return ["Ctrl", "Shift", "Alt", "Meta"].filter(x => mods.indexOf(x) >= 0).concat([key]).join("+")
        }
        function mode(bar) {  // a painting tool's Mode, the blend mode it paints with
            const i = ToolSpec.modeCell(bar)
            return Tools.value(ToolSpec.key(bar, i), ToolSpec.cell(bar, i).def)
        }

        // ---- the table -----------------------------------------------------------------------------------------------
        // Main registers each key of Photoshop's above, for its act, and those only; a row of Phím tắt for each.
        function test_bound() {
            const got = MenuSpec.bound().map(b => [b.sequences[0], b.item.act || b.item.dialog])
            const key = x => x[0]
            compare(got.map(key).sort(), expected.map(key).sort())
            for (const e of expected)
                compare(got.find(g => g[0] === e[0])[1], e[1], e[0])
            compare(MenuSpec.bound().find(b => b.sequences[0] === "Shift++").sequences, ["Shift++", "Shift+="], "⇧ + is Shift = on Windows")
            compare(MenuSpec.sequences("⇧ ← → ↑ ↓"), ["Shift+Left", "Shift+Right", "Shift+Up", "Shift+Down"])
            compare(MenuSpec.sequences("Ctrl +"), ["Ctrl++", "Ctrl+="])
            const plain = MenuSpec.bound().filter(b => b.plain).map(b => b.sequences[0]).sort()
            compare(plain, [",", ".", "Down", "Left", "Right", "Shift++", "Shift+,", "Shift+-", "Shift+.", "Shift+Backspace", "Shift+Down", "Shift+Left", "Shift+Right",
                "Shift+Up", "Up", "\\"].sort())
        }

        // ---- layers ----------------------------------------------------------------------------------------------------
        // Alt [ ] , . pick the layer below, above, at the bottom, at the top of the rows (inside open groups, not closed
        // ones); with Shift [ ], add it. No step.
        function test_layer_picks() {
            const w = open(), d = Docs.current
            compare(d.selected, [id("Dodge & Burn")])
            const cases = [["Alt+[", ["Tách tần số"]], ["Alt+]", ["Làm sáng mắt"]], ["Alt+,", ["Background"]], ["Alt+.", ["Curves 1"]],
                ["Shift+Alt+[", ["Dodge & Burn", "Tách tần số"]], ["Shift+Alt+]", ["Dodge & Burn", "Làm sáng mắt"]]]
            for (const c of cases) {
                pickOnly(["Dodge & Burn"])
                picks(() => press(w, c[0]), c[1].map(id), c[0])
                verify(!d.editMask, c[0] + ": the layer, not its mask")
            }
            pickOnly(["Tách tần số"])
            picks(() => press(w, "Alt+["), [id("Cao")], "into the open group")
            d.toggleOpen(id("Tách tần số"))
            pickOnly(["Tách tần số"])
            picks(() => press(w, "Alt+["), [id("Heal")], "past the closed group")
            pickOnly(["Heal"])
            picks(() => press(w, "Shift+Alt+]"), [id("Heal"), id("Tách tần số")], "Shift: added")
            pickOnly(["Background"])
            nothing(() => press(w, "Alt+["), "Alt [ on the bottom layer")
            pickOnly(["Curves 1"])
            nothing(() => press(w, "Alt+]"), "Alt ] on the top layer")
            picks(() => press(w, "Alt+,"), [id("Background")], "Alt , from the top")
        }
        // Stamp Visible, Stamp Selected, New Layer without the dialog, Duplicate and Transform Again: one step each.
        function test_new_layers() {
            const w = open(), d = Docs.current, n = d.layers.length
            const above = name => d.rows[d.rows.findIndex(l => l.id === d.active.id) + 1].props.name === name
            oneStep(() => press(w, "Ctrl+Shift+Alt+E"), "Gộp layer đang hiện vào layer mới", "Stamp Visible", () => {
                compare(d.layers.length, n + 1)
                verify(/^Layer \d+$/.test(d.active.props.name) && d.active.kind === "pixel", d.active.props.name)
                verify(above("Dodge & Burn"), "above the layer picked")
                compare([cmds()[0].op, cmds()[0].props.photo], ["addLayer", d.photo], "the image, as the mock merges no pixels")
            })
            oneStep(() => press(w, "Ctrl+Alt+E"), "Gộp layer đang chọn vào layer mới", "Stamp Selected", () => {
                compare([d.layers.length, d.active.props.name], [n + 1, "Dodge & Burn (gộp)"])
                verify(above("Dodge & Burn"))
            })
            pickOnly(["Heal", "Làm sáng mắt"])
            oneStep(() => press(w, "Ctrl+Alt+E"), "Gộp layer đang chọn vào layer mới", "two layers: above the top one", () => {
                compare(d.active.props.name, "Làm sáng mắt (gộp)")
                verify(above("Làm sáng mắt"))
            })
            oneStep(() => press(w, "Ctrl+Shift+Alt+N"), "Layer mới", "New Layer, no dialog", () => {
                compare([d.layers.length, w.dialogs.length], [n + 1, 0])
                compare([cmds()[0].op, cmds()[0].props.thumb], ["addLayer", "empty"])
            })
            oneStep(() => press(w, "Ctrl+Shift+Alt+T"), "Nhân bản rồi Transform lại", "Duplicate and Transform Again", () => {
                compare(d.layers.length, n + 2, "copies of the two picked")
                compare(cmds()[0].op, "transformAgain")
            })
        }
        // \ shows the active layer's mask as red over the image, Alt on a mask's thumbnail the mask alone (Layers marks the
        // row; Tools "layers:maskView"); Shift on it, the mask off (a step). Again, back.
        function test_mask_view() {
            const w = open(), d = Docs.current
            const view = () => Tools.value("layers:maskView", null) || {}
            nothing(() => press(w, "\\"), "\\ without a mask")
            compare(Object.keys(view()), [])
            pickOnly(["Curves 1"])
            nothing(() => press(w, "\\"), "\\ is no step")
            verify(view().doc === d)
            compare([view().layer, view().view], [id("Curves 1"), "overlay"])
            compare([row(w, "Curves 1").maskView, row(w, "Làm trắng răng").maskView], ["overlay", ""])
            press(w, "\\")
            compare([Object.keys(view()), row(w, "Curves 1").maskView], [[], ""], "again: off")
            picks(() => clickRow(w, "Làm trắng răng", "mask", Qt.AltModifier), [id("Làm trắng răng")], "Alt on a mask")
            verify(d.editMask, "the mask takes the brush")
            compare([view().layer, view().view, row(w, "Làm trắng răng").maskView], [id("Làm trắng răng"), "alone", "alone"])
            Docs.select((Docs.index + 1) % Docs.docs.length)
            settle(w)
            verify(all(panel(w), o => o instanceof LayerRow && o.visible && !!o.entry).every(r => r.maskView === ""), "another document: not its view")
            Docs.select(Docs.docs.indexOf(d))
            settle(w)
            clickRow(w, "Làm trắng răng", "mask", Qt.AltModifier)
            compare(row(w, "Làm trắng răng").maskView, "", "Alt again: the image")
            oneStep(() => clickRow(w, "Làm trắng răng", "mask", Qt.ShiftModifier), "Tắt Layer Mask", "Shift on a mask", () => verify(named("Làm trắng răng").props.maskOff))
        }
        // Ctrl on a thumbnail: what the layer covers made the selection; Shift adds, Alt takes away, both cross; on a
        // mask, the mask. The layers picked stay. A step each; undo gives each back.
        function test_load_selection() {
            const w = open(), d = Docs.current, db = id("Dodge & Burn")
            const sel = () => d.selection ? (d.inverse ? "!" : "") + d.selection : null
            const steps = [["Heal", "thumb", Qt.ControlModifier, "transparency", "new", "Nạp vùng chọn", "skin"],
                ["Làm trắng răng", "thumb", Qt.ControlModifier | Qt.ShiftModifier, "transparency", "add", "Cộng vào vùng chọn", "skin+teeth"],
                ["Làm sáng mắt", "thumb", Qt.ControlModifier | Qt.AltModifier, "transparency", "sub", "Trừ khỏi vùng chọn", "skin+teeth−eyes"],
                ["Làm trắng răng", "mask", Qt.ControlModifier | Qt.ShiftModifier | Qt.AltModifier, "mask", "int", "Giao với vùng chọn", "skin+teeth−eyes∩teeth"]]
            const was = [snap()]
            for (const s of steps) {
                const at = d.index
                clickRow(w, s[0], s[1], s[2])
                compare([d.index, last().name, sel()], [at + 1, s[5], s[6]], s[5])
                compare(cmds(), [{ op: "loadSelection", layer: id(s[0]), from: s[3], mode: s[4] }], s[5])
                compare(d.selected, [db], s[5] + ": the layers picked stay")
                was.push(snap())
            }
            for (let i = steps.length - 1; i >= 0; i--) {
                d.undo()
                compare(snap(), was[i], "undo " + steps[i][5])
            }
            oneStep(() => clickRow(w, "Curves 1", "mask", Qt.ControlModifier), "Nạp vùng chọn", "Ctrl on a white mask", () => compare(sel(), "all"))
            d.run({ op: "select", shape: "all" })
            nothing(() => clickRow(w, "Dodge & Burn", "thumb", Qt.ControlModifier | Qt.ShiftModifier), "all added to all")
            picks(() => clickRow(w, "Heal", "name", Qt.ControlModifier), [db, id("Heal")], "Ctrl on the name: added to those picked, as before")
        }
        // Alt on the line between two rows: the upper layer clipped to the lower one, or released (a step); not across a
        // group's edge; in the middle of a row, an ordinary click.
        function test_clip_line() {
            const w = open(), d = Docs.current
            const clip = name => () => {
                compare(cmds(), [{ op: "clippingMask", layer: id(name) }])
                verify(named(name).props.clip, name + " clipped")
            }
            oneStep(() => clickRow(w, "Làm trắng răng", "name", Qt.AltModifier, 1), "Clipping Mask", "the line above", clip("Curves 1"))
            oneStep(() => clickRow(w, "Làm trắng răng", "name", Qt.AltModifier, row(w, "Làm trắng răng").height - 2), "Clipping Mask", "the line below", clip("Làm trắng răng"))
            d.run({ op: "clippingMask", layer: id("Curves 1") })
            oneStep(() => clickRow(w, "Curves 1", "name", Qt.AltModifier, row(w, "Curves 1").height - 2), "Clipping Mask", "again: released",
                () => verify(!named("Curves 1").props.clip))
            nothing(() => clickRow(w, "Cao", "name", Qt.AltModifier, 1), "the group's edge")
            nothing(() => clickRow(w, "Thấp", "name", Qt.AltModifier, row(w, "Thấp").height - 2), "out of the group")
            nothing(() => clickRow(w, "Curves 1", "name", Qt.AltModifier, 1), "above the top row")
            picks(() => clickRow(w, "Heal", "name", Qt.AltModifier), [id("Heal")], "Alt in the middle: a click")
        }

        // ---- blend modes ---------------------------------------------------------------------------------------------
        // Without a painting tool: the layers picked but the Background, one step; Q and R a painting tool's only.
        function test_blend_layer() {
            const w = open(), d = Docs.current
            Tools.setTool("move")
            compare(named("Dodge & Burn").props.blend, "softLight")
            const to = (v, names) => (names || ["Dodge & Burn"]).map(n => ({ op: "setProps", layer: id(n), props: { blend: v } }))
            oneStep(() => press(w, "Shift++"), "Blend mode", "Shift +", () => compare(cmds(), to("hardLight")))
            oneStep(() => press(w, "Shift+="), "Blend mode", "Shift = (Shift + on Windows)", () => compare(cmds(), to("hardLight")))
            oneStep(() => press(w, "Shift+-"), "Blend mode", "Shift −", () => compare(cmds(), to("overlay")))
            for (const k in letters) {
                const v = letters[k], seq = "Shift+Alt+" + k
                if (v === "behind" || v === "clear" || v === "softLight")
                    nothing(() => press(w, seq), seq + " (" + v + ")")
                else
                    oneStep(() => press(w, seq), "Blend mode", seq, () => compare(cmds(), to(v)))
            }
            pickOnly(["Background", "Heal", "Dodge & Burn"])
            oneStep(() => press(w, "Shift+Alt+M"), "Blend mode", "three picked: two, not the Background", () => compare(cmds(), to("multiply", ["Heal", "Dodge & Burn"])))
            pickOnly(["Tách tần số"])
            oneStep(() => press(w, "Shift+-"), "Blend mode", "a group: Pass Through too", () => compare(cmds(), to("luminosity", ["Tách tần số"])))
            oneStep(() => press(w, "Shift++"), "Blend mode", "a group: after Pass Through", () => compare(cmds(), to("normal", ["Tách tần số"])))
            pickOnly(["Background"])
            nothing(() => press(w, "Shift++"), "the Background")
            compare(mode("brush"), "Normal", "the brush's Mode as it was")
        }
        // With a painting tool, its Mode (Tools, not History), as Photoshop's; Alt held for the Eyedropper too.
        function test_blend_brush() {
            const w = open()
            compare(mode("brush"), "Normal")
            nothing(() => press(w, "Shift++"), "Shift + with Brush")
            compare(mode("brush"), "Dissolve")
            press(w, "Shift+-")
            press(w, "Shift+-")
            compare(mode("brush"), "Luminosity", "round")
            for (const k in letters) {
                press(w, "Shift+Alt+" + k)
                compare(mode("brush"), { behind: "Behind", clear: "Clear" }[letters[k]] || Mock.BLEND_LABELS[Mock.BLENDS.indexOf(letters[k])], k)
            }
            compare(named("Dodge & Burn").props.blend, "softLight", "the layer's as it was")
            keyPress(Qt.Key_Alt)  // held: the Eyedropper for a moment, the brush's Mode still
            tryCompare(Tools, "tool", "eyedrop")
            keySequence("Shift+Alt+M")
            keyRelease(Qt.Key_Alt)
            tryCompare(Tools, "tool", "brush")
            compare(mode("brush"), "Multiply")
            Tools.setTool("clone")
            press(w, "Shift+Alt+Q")
            compare(mode("clone"), "Behind")
            Tools.setTool("eraser")  // no blend modes in its Mode: the layer's
            oneStep(() => press(w, "Shift+Alt+S"), "Blend mode", "Eraser", () => compare(named("Dodge & Burn").props.blend, "screen"))
            nothing(() => press(w, "Shift+Alt+Q"), "Behind is a painting tool's")
        }

        // ---- fill, nudge -----------------------------------------------------------------------------------------------
        function test_fill_keys() {
            const w = open(), d = Docs.current
            const fill = (use, keep, layer) => [{ op: "fill", layer: id(layer || "Dodge & Burn"), use: use, color: use === "foreground" ? Tools.fg : Tools.bg, preserve: keep, selection: d.selection }]
            oneStep(() => press(w, "Alt+Backspace"), "Fill", "Alt Backspace", () => compare(cmds(), fill("foreground", false)))
            oneStep(() => press(w, "Ctrl+Backspace"), "Fill", "Ctrl Backspace", () => compare(cmds(), fill("background", false)))
            oneStep(() => press(w, "Shift+Alt+Backspace"), "Fill", "Shift Alt Backspace", () => compare(cmds(), fill("foreground", true)))
            oneStep(() => press(w, "Ctrl+Shift+Backspace"), "Fill", "Ctrl Shift Backspace", () => compare(cmds(), fill("background", true)))
            d.run({ op: "select", shape: "skin" })
            oneStep(() => press(w, "Alt+Backspace"), "Fill", "within the selection", () => compare(cmds()[0].selection, "skin"))
            pickOnly(["Background"])
            oneStep(() => press(w, "Ctrl+Backspace"), "Fill", "the Background, locked", () => compare(cmds(), fill("background", false, "Background")))
            pickOnly(["Tách tần số"])
            nothing(() => press(w, "Alt+Backspace"), "a group takes none")
            const was = [d.index, d.history.length]
            press(w, "Shift+Backspace")
            const f = dialogOn(w, FillDialog)
            compare([d.index, d.history.length], was, "the dialog: no step yet")
            f.close()
            tryVerify(() => !w.dialogs.length)
            // typing in a field: the field's keys
            pickOnly(["Dodge & Burn"])
            const field = find(panel(w), o => o instanceof TextBox && o.Accessible.name === "Tìm layer theo tên")
            field.forceActiveFocus()
            nothing(() => keySequence("Alt+Backspace"), "Alt Backspace in a text field")
        }
        // The arrows: Move nudges the layers picked, a selection tool the selection, 1 px, 10 with Shift; another tool,
        // or a control with the focus (it takes them), nothing.
        function test_nudge() {
            const w = open(), d = Docs.current
            nothing(() => press(w, "Left"), "Brush")
            Tools.setTool("move")
            const by = (what, dx, dy) => [what === "layer" ? { op: "nudge", what: what, dx: dx, dy: dy, layers: [id("Dodge & Burn")] } : { op: "nudge", what: what, dx: dx, dy: dy }]
            const moves = [["Left", -1, 0], ["Right", 1, 0], ["Up", 0, -1], ["Down", 0, 1], ["Shift+Left", -10, 0], ["Shift+Right", 10, 0], ["Shift+Up", 0, -10], ["Shift+Down", 0, 10]]
            for (const m of moves)
                oneStep(() => press(w, m[0]), "Dời layer", "Move " + m[0], () => compare(cmds(), by("layer", m[1], m[2])))
            pickOnly(["Background"])
            nothing(() => press(w, "Left"), "the Background stays")
            pickOnly(["Dodge & Burn"])
            Tools.xf = true  // Free Transform's arrows are its own (U34)
            nothing(() => press(w, "Left"), "Free Transform on")
            Tools.warp = true
            nothing(() => press(w, "Shift+Up"), "Warp on")
            Tools.xf = false
            Tools.warp = false
            const b = find(panel(w), o => o instanceof ToolToggle && o.Accessible.name === "Nhóm mới")
            b.forceActiveFocus()
            nothing(() => keySequence("Left"), "a control's arrow")
            Tools.setTool("rect")
            nothing(() => press(w, "Right"), "no selection")
            d.run({ op: "select", shape: "rect" })
            for (const m of moves)
                oneStep(() => press(w, m[0]), "Dời vùng chọn", "Rectangular Marquee " + m[0], () => compare(cmds(), by("selection", m[1], m[2])))
            Tools.setTool("lasso")
            oneStep(() => press(w, "Down"), "Dời vùng chọn", "Lasso", () => compare(cmds(), by("selection", 0, 1)))
            Tools.setTool("crop")
            nothing(() => press(w, "Down"), "Crop")
        }

        // ---- documents, brushes ------------------------------------------------------------------------------------
        function test_documents() {
            const w = open(), n = Docs.docs.length, at = Docs.index
            compare(n, 3)
            press(w, "Ctrl+Tab")
            compare(Docs.index, (at + 1) % n)
            press(w, "Ctrl+Tab")
            press(w, "Ctrl+Tab")
            compare(Docs.index, at, "round")
            const steps = () => Docs.docs.map(x => [x.index, x.history.length])
            const was = steps()
            press(w, "Ctrl+Shift+Tab")
            compare(Docs.index, (at + n - 1) % n)
            compare(steps(), was, "no steps")
            // File › Đóng các tài liệu khác: those not saved asked about first
            Docs.select(at)
            Docs.cancelJob()
            const keep = Docs.current, dirty = Docs.docs.filter(x => x !== keep && x.dirty)
            verify(dirty.length > 0)
            press(w, "Ctrl+Alt+P")
            const s = dialogOn(w, SaveChangesDialog)
            compare(s.params.docs.length, dirty.length)
            for (let i = 0; i < dirty.length; i++) {
                let b = null
                tryVerify(() => (b = button(s, "Không lưu")))
                waitForPolish(b.parent)
                mouseClick(b)
                if (i + 1 < dirty.length)
                    compare(Docs.docs.length, n, "nothing closed before the last answer")
            }
            tryVerify(() => !w.dialogs.length)
            verify(Docs.docs.length === 1 && Docs.current === keep)
            verify(!w.can(item(["File", "Đóng các tài liệu khác"])), "one document: greyed")
            press(w, "Ctrl+Tab")
            verify(Docs.current === keep)
        }
        // , . the brush preset before, after; with Shift the first, the last (Brushes' presets in order): a painting
        // tool's brush, as Brushes picks one; not with Move.
        function test_brush_presets() {
            const w = open(), d = Docs.current
            const all = Presets.BR_GROUPS.reduce((o, g) => o.concat(g[1]), []), n = all.length
            const now = () => [Tools.value("brush:size", ToolSpec.brush("brush").size), Tools.value("brush:hard", ToolSpec.brush("brush").hard)]
            const preset = i => [all[i][1], all[i][2]], was = [d.index, d.history.length]
            nothing(() => press(w, "Shift+."), "Shift .")
            compare(now(), preset(n - 1), "the last")
            press(w, ",")
            compare(now(), preset(n - 2), "the one before")
            press(w, "Shift+,")
            compare(now(), preset(0), "the first")
            press(w, ",")
            compare(now(), preset(0), "none before the first")
            press(w, ".")
            compare(now(), preset(1), "the one after")
            Tools.setTool("move")
            press(w, ".")
            compare(now(), preset(1), "Move: no brush")
            Tools.setTool("eraser")
            press(w, ".")
            verify(all.some(p => p[1] === Tools.value("eraser:size", -1) && p[2] === Tools.value("eraser:hard", -1)), "Eraser's own brush, a preset's")
            compare(now(), preset(1), "Brush's as it was")
            compare([d.index, d.history.length], was, "no steps")
        }

        // ---- menu items ----------------------------------------------------------------------------------------------
        // Layer › Mới's Chuyển thành Background is Layer từ Background… while the Background is picked, as Photoshop's.
        function test_background_items() {
            const w = open(), d = Docs.current
            const it = item(["Layer", "Mới", /Background/])
            compare([it.l, w.can(it)], ["Chuyển thành Background", false], "a Background there")
            pickOnly(["Background"])
            const bg = id("Background"), was = snap()
            compare([it.l, it.act, it.en, w.can(it)], ["Layer từ Background…", "@bgToLayer", "Layer from Background", true])
            compare(panel(w).subOf(x => x.act === "@groupLayer").find(x => /Background/.test(x.l || "")).l, "Layer từ Background…", "Main's menus")
            w.act(it)
            compare(last().name, "Layer từ Background")
            compare([named("Layer 0").id, named("Layer 0").props.background, named("Layer 0").props.lockAll, d.selected], [bg, false, false, [bg]])
            compare([it.l, w.can(it)], ["Chuyển thành Background", true], "no Background now")
            pickOnly(["Heal"])
            w.act(it)
            compare(last().name, "Chuyển thành Background")
            const b = d.layers[d.layers.length - 1]
            compare([b.id, b.props.name, b.props.background, b.props.lockAll, b.depth], [id("Background"), "Background", true, true, 0], "Heal at the bottom")
            verify(!named("Heal"))
            d.undo()
            d.undo()
            compare(snap(), was, "undo, undo")
            pickOnly(["Tách tần số"])
            verify(!w.can(it), "a group is no Background")
        }
        function test_layer_items() {
            const w = open(), d = Docs.current
            // Layer Style › Xóa Layer Style
            const clear = item(["Layer", "Layer Style", "Xóa Layer Style"])
            verify(!w.can(clear), "no layer style")
            d.run({ op: "setProps", layer: id("Dodge & Burn"), props: { fx: { dropShadow: { on: true } } } })
            verify(w.can(clear))
            oneStep(() => w.act(clear), "Xóa Layer Style", "Xóa Layer Style", () => compare(named("Dodge & Burn").props.fx, null))
            // Sắp xếp › Đảo thứ tự: two of a group or more
            const rev = item(["Layer", "Sắp xếp", "Đảo thứ tự"])
            verify(!w.can(rev), "one layer")
            pickOnly(["Curves 1", "Làm sáng mắt"])
            oneStep(() => w.act(rev), "Đảo thứ tự layer", "Đảo thứ tự", () => compare(d.rows.slice(0, 3).map(l => l.props.name), ["Làm sáng mắt", "Làm trắng răng", "Curves 1"]))
            pickOnly(["Dodge & Burn", "Cao"])
            nothing(() => w.act(rev), "one of each group")
            // Phân bố: three layers or more
            const dist = item(["Layer", "Phân bố"]).sub.filter(x => x.l)
            compare(dist.map(x => x.act), ["@distribute:top", "@distribute:vcenter", "@distribute:bottom", "@distribute:left", "@distribute:hcenter", "@distribute:right"])
            verify(dist.every(x => !w.can(x)), "two layers")
            pickOnly(["Curves 1", "Làm sáng mắt", "Heal"])
            for (const x of dist)
                oneStep(() => w.act(x), "Phân bố layer", x.l, () => compare([cmds()[0].edge, cmds()[0].layers.slice().sort()], [MenuSpec.arg(x.act), ["Curves 1", "Làm sáng mắt", "Heal"].map(id).sort()]))
            // Mới › Nhóm từ layer…: as Nhóm layer
            pickOnly(["Heal"])
            oneStep(() => w.act(item(["Layer", "Mới", "Nhóm từ layer…"])), "Nhóm layer", "Nhóm từ layer…", () => compare(cmds()[0].op, "groupLayers"))
            // Select › Bỏ chọn layer
            const none = item(["Select", "Bỏ chọn layer"])
            picks(() => w.act(none), [], "Bỏ chọn layer")
            verify(!w.can(none), "none picked")
        }
        // Liên kết layer (two layers picked) is Bỏ liên kết layer while they are linked; the rows show the link, Layers'
        // foot runs it, the right button's menu has it while it runs (Photoshop's), never Phân bố.
        function test_link() {
            const w = open(), d = Docs.current
            const link = item(["Layer", /iên kết layer$/]), foot = find(panel(w), o => o instanceof ToolToggle && o.Accessible.name === "Liên kết layer")
            compare([link.l, w.can(link), foot.enabled], ["Liên kết layer", false, false])
            pickOnly(["Heal", "Dodge & Burn"])
            verify(w.can(link) && foot.enabled)
            const was = snap()
            w.act(link)
            compare(last().name, "Liên kết layer")
            const k = named("Heal").props.link
            verify(k && named("Dodge & Burn").props.link === k, "one link")
            compare([link.l, panel(w).linkItem.l, row(w, "Heal").linked, row(w, "Dodge & Burn").linked, row(w, "Cao").linked], ["Bỏ liên kết layer", "Bỏ liên kết layer", true, true, false])
            // the right button on a row picked: the menu keeps them, with the item
            const r = row(w, "Heal")
            mouseClick(r, xOf(r, "name"), r.height / 2, Qt.RightButton)
            let pop = null
            for (let i = 0; i < panel(w).resources.length; i++)
                if (panel(w).resources[i] instanceof PopupMenu)
                    pop = panel(w).resources[i]
            tryVerify(() => pop.opened)
            const texts = []
            for (let i = 0; i < pop.count; i++)
                texts.push(pop.itemAt(i) instanceof MenuItem ? pop.itemAt(i).text : "-")
            verify(texts.indexOf("Bỏ liên kết layer") >= 0 && texts.indexOf("Phân bố") < 0, texts.join(", "))
            pop.close()
            tryVerify(() => !pop.visible)
            compare(d.selected, [id("Heal"), id("Dodge & Burn")], "kept")
            mouseClick(foot)
            compare(last().name, "Bỏ liên kết layer")
            verify(!named("Heal").props.link && !named("Dodge & Burn").props.link)
            compare(link.l, "Liên kết layer")
            d.undo()
            d.undo()
            compare(snap(), was)
            // one picked: nor in the right button's menu
            pickOnly(["Cao"])
            mouseClick(row(w, "Cao"), xOf(row(w, "Cao"), "name"), 10, Qt.RightButton)
            tryVerify(() => pop.opened)
            let has = false
            for (let i = 0; i < pop.count; i++)
                has = has || (pop.itemAt(i) instanceof MenuItem && /iên kết layer/.test(pop.itemAt(i).text))
            verify(!has, "one layer: no Liên kết layer")
            pop.close()
        }

        // ---- no key twice ----------------------------------------------------------------------------------------------
        // The tables: every key of the menus, MenuSpec.KEYS, the toolbar (ToolSpec.KEYS, 0–9 and Shift), Main's F and Tab,
        // Esc, Space, the dock's Ctrl 2–9 and the menu bar's Alt letters, by context (KEYS' ctx: a tool or a selection
        // tool's); two in one context run neither. View's Extras and Hiện › Viền vùng chọn share Ctrl H (the prototype's,
        // Photoshop's): Main registers the first (keyed()).
        function test_no_key_twice() {
            const w = open()
            const keys = []  // { seq, ctx, what }
            const walk = (items, path) => items.forEach(it => {
                if (it.k)
                    MenuSpec.sequences(it.k).forEach(s => keys.push({ seq: s, ctx: "", what: path.concat([it.l]).join(" › ") }))
                if (Array.isArray(it.sub))
                    walk(it.sub, path.concat([it.l]))
            })
            MenuSpec.MENUS.forEach(m => walk(m.items, [m.title.replace("&", "")]))
            MenuSpec.MENUS.forEach(m => keys.push({ seq: "Alt+" + m.title[m.title.indexOf("&") + 1], ctx: "", what: m.title }))
            MenuSpec.bound().forEach(b => b.sequences.forEach(s => keys.push({ seq: s, ctx: b.item.ctx || "", what: "Khác › " + b.item.l })))
            const tb = find(w.contentItem, o => o instanceof Toolbar)
            tb.keys.forEach(k => keys.push({ seq: k.seq, ctx: "", what: "toolbar " + k.act }))
            for (const s of ["F", "Tab", "Esc", "Space", "Ctrl+2", "Ctrl+3", "Ctrl+4", "Ctrl+5", "Ctrl+6", "Ctrl+7", "Ctrl+8", "Ctrl+9"])
                keys.push({ seq: s, ctx: "", what: s })
            verify(keys.length > 200, keys.length)
            const tools = Object.keys(ToolSpec.TOOL)
            const on = (ctx, t) => ctx === "brush" ? !!ToolSpec.PAINT_T[t] : ctx === "paint" ? ToolSpec.modeCell(ToolSpec.tool(t).bar) >= 0
                : ctx === "nudge" ? t === "move" || !!ToolSpec.SEL_T[t] : true
            const by = {}, twice = []
            for (const k of keys)
                (by[norm(k.seq)] = by[norm(k.seq)] || []).push(k)
            for (const s in by)
                for (let i = 0; i < by[s].length; i++)
                    for (let j = i + 1; j < by[s].length; j++) {
                        const a = by[s][i], b = by[s][j]
                        if (a.what !== b.what && tools.some(t => on(a.ctx, t) && on(b.ctx, t)))
                            twice.push(s + ": " + a.what + " / " + b.what)
                    }
            compare(twice, ["Ctrl+H: View › Extras / View › Hiện › Viền vùng chọn"])
        }
        // The window's Shortcuts on, with Brush, Move, a selection tool with a selection, a layer with a mask: no sequence
        // on two of them.
        function test_no_shortcut_twice() {
            const w = open()
            const states = [["Brush", () => {}], ["Move", () => Tools.setTool("move")],
                ["Rectangular Marquee, a selection", () => { Tools.setTool("rect"); Docs.current.run({ op: "select", shape: "rect" }) }],
                ["a mask", () => pickOnly(["Curves 1"])], ["Clone Stamp, three picked", () => { Tools.setTool("clone"); pickOnly(["Heal", "Cao", "Thấp"]) }]]
            for (const st of states) {
                Docs.reset("Main")
                Tools.reset()
                st[1]()
                settle(w)
                w.contentItem.forceActiveFocus()
                const by = {}, twice = []
                let n = 0
                for (const s of shortcuts(w).filter(x => x.enabled)) {
                    n++
                    for (const q of sequencesOf(s)) {
                        const k = norm(q)
                        if (by[k] && by[k] !== s)
                            twice.push(k)
                        by[k] = s
                    }
                }
                verify(n > 100, st[0] + ": " + n + " on")
                compare(twice, [], st[0])
            }
        }

        // ---- Phím tắt -----------------------------------------------------------------------------------------------
        function test_shortcuts_other() {
            const w = open()
            keySequence("Ctrl+Shift+Alt+K")
            const d = dialogOn(w, ShortcutsDialog)
            const names = () => d.shown.map(g => g[1].map(r => r.n)).reduce((a, b) => a.concat(b), [])
            const seg = find(d.contentItem, o => o instanceof Segmented)
            compare(seg.options.length, 4)
            mouseClick(find(seg, o => o instanceof T.AbstractButton && o.text === "Khác"))
            compare(d.tab, 3)
            compare(d.shown.map(g => g[0]), ["Layer", "Tô màu", "Dời, biến đổi", "Tài liệu", "Cọ", "Blend mode"])
            compare(names(), MenuSpec.KEYS.map(g => g[1].map(r => r.l)).reduce((a, b) => a.concat(b), []))
            compare(names().length, 18 + 5 + 3 + 2 + 4 + 27)
            for (const g of d.lists[3])
                g[1].forEach(r => verify(!/[À-ỹĐđ]/.test(r.n) || r.en !== "", g[0] + " › " + r.n + " has no English name"))
            compare(d.columns.map(c => c.map(g => g[0])), [["Layer", "Tô màu", "Dời, biến đổi", "Tài liệu"], ["Cọ", "Blend mode"]], "two columns about as tall")
            // every key Main registers, listed
            const listed = d.lists[3].map(g => g[1]).reduce((a, b) => a.concat(b), []).filter(r => r.keys.length).map(r => MenuSpec.sequences(r.keys.join(" ")))
                .reduce((a, b) => a.concat(b), [])
            compare(MenuSpec.bound().map(b => b.sequences[0]).filter(s => listed.indexOf(s) < 0), [])
            // a click's row: its modifiers as caps, where after them; no keys to press
            const t = find(d.contentItem, o => o instanceof Text && o.text === "Nạp mask làm vùng chọn")
            verify(t && t.visible)
            compare(all(t.parent, o => o instanceof Text && o.visible).map(x => x.text), ["Nạp mask làm vùng chọn", "Load Layer Mask as Selection", "Ctrl", "nhấp mask"])
            const arrows = find(d.contentItem, o => o instanceof Text && o.text === "Dời 10 px")
            compare(all(arrows.parent, o => o instanceof Text && o.visible).map(x => x.text), ["Dời 10 px", "Nudge 10 Pixels", "⇧", "←", "→", "↑", "↓"])
            // found by English names, keys, where a click is
            const found = [["stamp visible", ["Gộp layer đang hiện vào layer mới"]], ["next brush", ["Preset cọ sau"]], ["rubylith", ["Xem mask thành lớp phủ đỏ"]],
                ["multiply", ["Multiply"]], ["clipping", ["Tạo, bỏ Clipping Mask"]], ["nhap mask", ["Nạp mask làm vùng chọn", "Xem riêng mask", "Tắt, bật mask"]],
                ["ctrl tab", ["Tài liệu kế"]], ["preserve transparency", ["Tô màu trước, giữ chỗ trong suốt", "Tô màu sau, giữ chỗ trong suốt"]],
                ["intersect", ["Giao độ trong suốt với vùng chọn"]]]
            for (const f of found) {
                d.query = f[0]
                compare(names(), f[1], f[0])
            }
            keyClick(Qt.Key_Escape)
            tryVerify(() => !w.dialogs.length)
        }
        // The English of what U31 added, as pb_en.qm has it (Photoshop's names).
        function test_english() {
            const en = (ctx, vi, note) => Language.translate("en", ctx, vi, note || "")
            const pairs = [["MenuSpec", "Đóng các tài liệu khác", "Close Others"], ["MenuSpec", "Nhóm từ layer…", "Group from Layers…"],
                ["MenuSpec", "Layer từ Background…", "Layer from Background…"], ["MenuSpec", "Xóa Layer Style", "Clear Layer Style"],
                ["MenuSpec", "Đảo thứ tự", "Reverse"], ["MenuSpec", "Bỏ liên kết layer", "Unlink Layers"], ["MenuSpec", "Bỏ chọn layer", "Deselect Layers"],
                ["MenuSpec", "Gộp layer đang hiện vào layer mới", "Stamp Visible"], ["MenuSpec", "Tô màu", "Fill"], ["MenuSpec", "nhấp mask", "click mask"],
                ["Mock", "Gộp layer đang hiện vào layer mới", "Stamp Visible"], ["Mock", "%1 (gộp)", "%1 (merged)"], ["Mock", "Dời layer", "Nudge"],
                ["Mock", "Liên kết layer", "Link Layers"], ["Mock", "Giao với vùng chọn", "Intersect with Selection"], ["ShortcutsDialog", "Khác", "Other"]]
            for (const p of pairs)
                compare(en(p[0], p[1]), p[2], p[1])
            compare(en("MenuSpec", "Layer", "Phím tắt › Khác"), "Layers")
            compare(MenuSpec.KEYS.map(g => en("MenuSpec", g[0], g[0] === "Layer" ? "Phím tắt › Khác" : "")),
                ["Layers", "Fill", "Nudge, Transform", "Documents", "Brushes", "Blend Modes"])
        }

        // ---- a real document ----------------------------------------------------------------------------------------
        // What the core has runs (the layer keys, a layer's blend mode, Bỏ chọn layer, a document's keys); the rest is
        // greyed and does nothing (decision 4).
        function test_real_document() {
            const w = open()
            const p = fixtures.image("u31.png", "png", 300, 200, 8, false, false)
            verify(p)
            const d = Docs.openFile(p)
            verify(d && d.real && Docs.current === d)
            tryVerify(() => !d.model.loading, 20000)
            verify(d.run({ op: "newLayer" }))
            settle(w)
            compare(d.layers.length, 2)
            const top = d.layers[0].id, bottom = d.layers[1].id
            compare(d.selected, [top])
            picks(() => press(w, "Alt+["), [bottom], "Alt [")
            picks(() => press(w, "Alt+]"), [top], "Alt ]")
            picks(() => press(w, "Shift+Alt+["), [top, bottom], "Shift Alt [")
            pickOnly([d.layers[0].props.name])
            Tools.setTool("eraser")  // Move is not the core's yet: a tool without blend modes, the layer's
            const at = d.index
            press(w, "Shift++")
            compare([d.index, d.layers[0].props.blend], [at + 1, "dissolve"], "Shift +: the core's setProps")
            d.undo()
            compare(d.layers[0].props.blend, "normal")
            pickOnly([d.layers[1].props.name])  // the PNG's Background: its blend mode stays, as Photoshop's
            verify(d.layers[1].props.background)
            nothing(() => press(w, "Shift++"), "the Background's blend mode")
            nothing(() => press(w, "Shift+Alt+M"), "nor by a letter")
            pickOnly([d.layers[0].props.name])
            const greyed = [item(["Layer", "Phân bố", "Mép trên"]), item(["Layer", /iên kết layer$/]), item(["Layer", "Sắp xếp", "Đảo thứ tự"]),
                item(["Layer", "Layer Style", "Xóa Layer Style"]), item(["Layer", "Mới", /Background/])]
                .concat(MenuSpec.bound().filter(b => /stamp|fillWith|nudge|transformAgain/.test(b.item.act || "")).map(b => b.item))
            for (const it of greyed)
                verify(!w.can(it), "greyed: " + it.l + " " + (it.act || ""))
            for (const seq of ["Ctrl+Shift+Alt+E", "Ctrl+Alt+E", "Alt+Backspace", "Ctrl+Shift+Alt+T", "Left"])
                nothing(() => press(w, seq), "the core has not: " + seq)
            nothing(() => clickRow(w, d.layers[0].props.name, "thumb", Qt.ControlModifier), "Ctrl on a thumbnail")
            nothing(() => clickRow(w, d.layers[0].props.name, "name", Qt.AltModifier, row(w, d.layers[0].props.name).height - 2), "Alt on the line")
            verify(w.can(item(["File", "Đóng các tài liệu khác"])))
            picks(() => w.act(item(["Select", "Bỏ chọn layer"])), [], "Bỏ chọn layer")
            press(w, "Ctrl+Tab")
            verify(Docs.current !== d, "Ctrl Tab")
        }
    }
}
