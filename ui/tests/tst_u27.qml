import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty

// U27: the Layers panel follows a command row by row. Adding, deleting, duplicating, grouping, ungrouping, moving or
// folding layers inserts, removes or moves just those rows; every other row keeps its delegate (none made again), the
// view stays where it was and the picked rows stay picked, on a mock document of 250 layers and on a real one (a PNG
// opened through Docs.openFile, its layers the core's). The panel's part of a command (with a panel less without one)
// within a 60 Hz frame. Dropdown's list makes no rows while shut (Blend's options change with the active layer).
Item {
    id: root
    width: 400
    height: 700
    Component { id: mainC; Main {} }
    Component { id: panelC; LayersPanel { width: 308; height: 640 } }
    Component {
        id: dropC
        Dropdown {
            width: 140
            onPicked: v => value = v  // as its owners do
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
        name: "U27"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
            Docs.error = ""
        }
        function cleanupTestCase() { init() }

        // ---- documents ------------------------------------------------------------------------------------------------
        // n layers, every tenth a group holding the next four, made with addLayer while the document is not shown. On a
        // mock document the rows show every kind of thumbnail, and some layers have effects (their rows under them).
        function fill(d, n) {
            const kinds = [["pixel", { photo: 0 }], ["pixel", { thumb: "empty", mask: "#ffffff" }], ["text", {}], ["shape", { shape: "ellipS", color: "#c0587e" }],
                ["adj", { adj: "Curves", vals: {} }], ["so", { photo: 7 }], ["fill", { color: "#b3202a" }], ["pixel", { thumb: "gray", blend: "softLight" }]]
            const fx = { dropShadow: { on: true, blend: "multiply", color: "#2b1d4e", opacity: 35, angle: 120, distance: 12, spread: 0, size: 24, noise: 0 },
                stroke: { on: false, size: 6, position: "outside", blend: "normal", opacity: 100, color: "#f7c873" } }
            const count = { 0: d.layers.length }
            let g = 0
            for (let k = 1; d.layers.length < n; k++) {
                const group = k % 10 === 0, parent = !group && g && k % 10 < 5 ? g : 0
                const c = group ? ["group", { name: "Nhóm " + k, blend: "passThrough" }]
                        : d.real ? ["pixel", { name: "Layer " + k }]
                        : [kinds[k % kinds.length][0], Object.assign({ name: "Layer " + k }, kinds[k % kinds.length][1], k % 25 === 7 ? { fx: fx } : {})]
                verify(d.run({ op: "addLayer", parent: parent, index: count[parent] || 0, kind: c[0], props: c[1] }), "layer " + k)
                count[parent] = (count[parent] || 0) + 1
                if (group)
                    g = d.active.id
            }
        }
        // A mock document of n layers (Docs.create), or a real one: a small PNG made for the test, opened as a file is.
        function make(real, n, name) {
            let d
            if (real) {
                const path = fixtures.image(name + ".png", "png", 64, 48, 8, false, false)
                verify(path, "made " + name)
                d = Docs.openFile(path)
                verify(d && d.real, "opened for real")
                tryVerify(() => !d.model.loading, 20000, "read")
            } else {
                d = Docs.create({ name: name + ".psd", w: 2000, h: 2000, ppi: 72, mode: "RGB", bits: 8, background: "white" })
                verify(d)
            }
            const shown = Docs.current
            Docs.select(Docs.docs.indexOf(d) === 0 ? 1 : 0)  // not shown while it fills
            fill(d, n)
            if (shown === d)
                Docs.select(Docs.docs.indexOf(d))
            return d
        }
        function byName(d, name) { return d.layers.find(l => l.props.name === name) || null }

        // ---- the panel --------------------------------------------------------------------------------------------------
        function open() {
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            Tools.dockMin = Object.assign({}, Tools.dockMin, { p: true })  // Properties folded: Layers taller (tst_u9)
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function list(p) { return find(p, o => o instanceof ListView) }
        function keyOf(r) { return r.entry.id + "/" + r.fx }
        // The rows in view, top first: the view's own for their index (a pooled one keeps an old index), on screen.
        function inView(p) {
            const l = list(p)
            return all(l.contentItem, o => o instanceof LayerRow && !!o.entry && l.itemAtIndex(o.index) === o && o.y + o.height > l.contentY && o.y < l.contentY + l.height)
                .sort((a, b) => a.y - b.y)
        }
        // What the panel has made and shows now: every LayerRow it holds (pooled ones too), the rows in view by key, top first.
        function look(p) {
            const l = list(p), rows = inView(p), by = {}
            for (const r of rows)
                by[keyOf(r)] = r
            return { made: all(l.contentItem, o => o instanceof LayerRow), keys: rows.map(keyOf), by: by, y: l.contentY }
        }
        // Row i of the panel at the view's top: the view scrolled there.
        function scrollTo(p, i) {
            const l = list(p)
            l.contentY = l.originY + i * p.rowHeight
            l.forceLayout()
            waitForRendering(l)
        }
        // After a command: each row in view before that is still in view is shown by the same delegate; at most `made`
        // delegates are new; the view has not moved; the rows show what Doc picked, each its own key's.
        function kept(p, was, made, what) {
            const l = list(p)
            l.forceLayout()
            waitForRendering(l)
            const now = look(p)
            const fresh = now.made.filter(o => was.made.indexOf(o) < 0)
            verify(fresh.length <= made, what + ": " + fresh.length + " rows made, at most " + made)
            let same = 0
            for (const k of now.keys)
                if (was.by[k]) {
                    verify(now.by[k] === was.by[k], what + ": the row of " + k + " made again")
                    same++
                }
            verify(same >= now.keys.length - Math.max(made, 1) - 2, what + ": the rows in view still there, " + same + " of " + now.keys.length)
            compare(l.contentY, was.y, what + ": the view where it was")
            const sel = p.doc.selected
            for (const r of inView(p))
                compare(r.selected, sel.indexOf(r.entry.id) >= 0, what + ": " + keyOf(r) + " picked as Doc says")
            for (const r of inView(p))  // and each row is its own key's
                verify(p.shown[r.index].l.id === r.entry.id && p.shown[r.index].fx === r.fx, what + ": row " + r.index + " shows " + keyOf(r))
        }
        function pickIds(d, ids) {  // false when they are picked already
            d.run({ op: "selectLayers", layers: ids })
            compare(d.selected, ids)
        }

        // Rows folded, unfolded and (mock) effects' rows added above the view, no layer picked there (the panel would
        // scroll to it): the rows in view stay on screen, the list's origin moves. Rows the list keeps made just out of
        // view (cacheBuffer) may be made for those that come or go there.
        function above(p, d) {
            const first = inView(p)[0].index
            const up = p.shown.findIndex((r, k) => k < first - 6 && r.l.kind === "group" && !r.fx && r.l.count > 0)
            verify(up >= 0, "a group above the view")
            const n = p.shown[up].l.count
            let was = look(p)
            d.toggleOpen(p.shown[up].l.id)
            kept(p, was, n, "fold above the view")
            compare(look(p).keys, was.keys)
            was = look(p)
            d.toggleOpen(p.shown[up].l.id)
            kept(p, was, n, "unfold above the view")
            compare(look(p).keys, was.keys)
            if (!d.real) {  // effects: rows of their own, Hiệu ứng and Drop Shadow
                const c = p.shown.find((r, k) => k < first - 2 && r.l.kind === "pixel" && !r.fx && !r.l.props.fx).l
                was = look(p)
                verify(d.run({ op: "setProps", layer: c.id, props: { fx: { dropShadow: { on: true, blend: "multiply", color: "#000000", opacity: 35, angle: 120, distance: 4, spread: 0, size: 8, noise: 0 } } } }))
                kept(p, was, 2, "effects above the view")
                compare(look(p).keys, was.keys)
            }
        }
        // Add, delete, duplicate, group, ungroup, move, fold: in view, the rest of the rows keep their delegates.
        function commands(real) {
            const w = open()
            const d = make(real, 250, real ? "u27-that" : "u27-gia")
            Docs.select(Docs.docs.indexOf(d))
            waitForRendering(w.contentItem)
            const p = find(w.contentItem, o => o instanceof LayersPanel && o.visible), l = list(p)
            compare(l.count, p.shown.length)
            verify(p.shown.length >= 250)
            const a = byName(d, "Layer 106"), i = p.shown.findIndex(r => r.l.id === a.id && !r.fx)
            scrollTo(p, i - 6)
            pickIds(d, [a.id])
            waitForRendering(l)
            let was = look(p)
            verify(was.keys.length > 12, "rows in view: " + was.keys.length)
            verify(was.by[a.id + "/"], "Layer 106 in view")

            verify(d.run({ op: "newLayer" }))
            kept(p, was, 1, "newLayer")
            const added = d.active.id
            verify(look(p).by[added + "/"], "the new layer in view")

            was = look(p)
            verify(d.run({ op: "deleteLayers" }))
            kept(p, was, 1, "deleteLayers")
            verify(!look(p).by[added + "/"])

            was = look(p)
            pickIds(d, [a.id])
            verify(d.run({ op: "duplicateLayers" }))
            kept(p, was, 1, "duplicateLayers")
            compare(d.active.props.name, "Layer 106 bản sao")

            was = look(p)
            pickIds(d, [byName(d, "Layer 107").id, byName(d, "Layer 106").id])
            verify(d.run({ op: "groupLayers" }))
            kept(p, was, 1, "groupLayers")
            const g = d.active
            compare(g.kind, "group")
            compare(d.layers.filter(x => x.parent === g.id).map(x => x.props.name), ["Layer 107", "Layer 106"])

            was = look(p)
            verify(d.run({ op: "ungroup", group: g.id }))
            kept(p, was, 1, "ungroup")
            verify(!d.layer(g.id))

            was = look(p)  // Layer 106 two rows down: a move
            const b = byName(d, "Layer 106")
            verify(d.run({ op: "moveLayer", layer: b.id, parent: b.parent, index: b.index - 2 }))
            kept(p, was, 0, "moveLayer")

            const grp = inView(p).find(r => r.kind === "group" && !r.fx && r.entry.count > 0 && r.open)
            verify(grp, "a group open in view")
            was = look(p)
            d.toggleOpen(grp.entry.id)  // folded: its rows go, as many come in from below
            kept(p, was, grp.entry.count, "fold")
            was = look(p)
            d.toggleOpen(grp.entry.id)
            kept(p, was, grp.entry.count, "unfold")

            above(p, d)
            // a command on a layer far from the active one: the view stays there (U9 took it back to the active one)
            scrollTo(p, 20)
            verify(!look(p).by[d.active.id + "/"], "the active layer out of view")
            was = look(p)
            const r = inView(p).find(q => !q.fx)
            mouseClick(r, 15, r.height / 2)  // its eye
            compare(r.entry.props.visible, false)
            kept(p, was, 0, "the eye of a row far from the active one")
            compare(look(p).keys, was.keys)
            // another document: its rows from the top, each row its key's; back, the same
            for (const x of [Docs.docs[0], d]) {
                Docs.select(Docs.docs.indexOf(x))
                compare(l.contentY, l.originY, x.file + " from the top")
                waitForRendering(l)
                compare(l.count, p.shown.length)
                for (const r of inView(p))
                    verify(p.shown[r.index].l.id === r.entry.id && p.shown[r.index].fx === r.fx, x.file + ": row " + r.index + " shows " + keyOf(r))
            }
            tryVerify(() => inView(p).some(r => r.entry.id === d.active.id), 2000, "the active layer in view")
            return { w: w, d: d, p: p }
        }
        function test_mock() { commands(false) }
        function test_real() { commands(true) }

        // A drag after rows came and went above the view (the list's origin moved): the drop is where the pointer is.
        function test_drag_after_changes() {
            const t = commands(false), p = t.p, d = t.d, l = list(p)
            scrollTo(p, 100)
            above(p, d)
            verify(l.originY !== 0, "the origin moved: " + l.originY)
            const rows = inView(p), plain = q => !!q && !q.fx && q.kind !== "group" && !q.hasFx
            const i = rows.findIndex((q, k) => k >= 1 && plain(q) && plain(rows[k + 3]) && !rows.slice(k, k + 4).some(x => x.fx))
            verify(i >= 0, "two plain rows three apart")
            const r = rows[i], under = rows[i + 3], x = 140, y = r.height / 2
            mousePress(r, x, y)
            for (let k = 1; k <= 5; k++)
                mouseMove(r, x, y + (3 * p.rowHeight + 4) * k / 5, -1, Qt.LeftButton)
            compare(p.drop.at, under.index, "the drop at the row under the pointer")
            compare(p.drop.mode, "below")
            const line = find(p, o => o instanceof Rectangle && o.visible && o.height === 2 && Qt.colorEqual(o.color, Theme.accent))
            verify(line, "the drop's line")
            fuzzyCompare(line.y, under.mapToItem(p, 0, under.height).y - 1, 0.5, "under that row")
            mouseRelease(r, x, y + 3 * p.rowHeight + 4)
            waitForRendering(l)
            const order = d.layers.map(q => q.id)
            compare(order.indexOf(r.entry.id), order.indexOf(under.entry.id) + 1, "below the row it was let go on")
        }

        // Rows the list keeps for reuse still have the key of a row, maybe of one in view (another document's layers
        // have the same ids): hidden, and renaming in the row shown only.
        function test_pooled() {
            const w = open()
            // the portrait chosen: Main opens PB_CANVAS's file last (N1), so that one is in front when it is set
            Docs.select(Docs.docs.findIndex(x => x.file === "chân dung.psd"))
            const p = find(w.contentItem, o => o instanceof LayersPanel && o.visible), l = list(p)
            compare(p.shown.length, 9)
            const d = Docs.create({ name: "ba-layer.psd", w: 800, h: 600, ppi: 72, mode: "RGB", bits: 8, background: "white" })
            verify(d.run({ op: "newLayer" }) && d.run({ op: "newLayer" }))
            waitForRendering(l)
            compare(p.shown.length, 3)
            compare(all(l.contentItem, o => o instanceof LayerRow && o.visible).length, 3, "the three rows shown, no other")
            for (const id of d.layers.map(x => x.id)) {
                p.startRename(id)
                waitForRendering(l)
                const field = find(p, o => o instanceof TextInput && o.activeFocus)
                verify(field && field.visible, "the field of " + id + " in its row shown")
                keyClick(Qt.Key_Escape)
                compare(p.renaming, 0)
            }
            p.startRename(d.active.id)
            waitForRendering(l)
            keyClick(Qt.Key_A)
            keyClick(Qt.Key_Return)
            compare(d.active.props.name, "a")
            // another document whose active layer has the same id, far down: the view goes there too
            const e = Docs.duplicate(d)
            verify(e && e !== d)
            for (const x of [d, e]) {
                Docs.select(Docs.docs.indexOf(x))
                for (let k = 0; k < 30; k++)
                    verify(x.run({ op: "newLayer" }))
                x.run({ op: "selectLayers", layers: [x.layers[x.layers.length - 1].id] })  // the Background, at the bottom
            }
            compare(d.active.id, e.active.id)
            Docs.select(Docs.docs.indexOf(d))
            waitForRendering(l)
            tryVerify(() => l.itemAtIndex(p.shown.length - 1) && inView(p).indexOf(l.itemAtIndex(p.shown.length - 1)) >= 0, 2000, "the Background in view")
            l.positionViewAtBeginning()
            Docs.select(Docs.docs.indexOf(e))
            tryVerify(() => l.itemAtIndex(p.shown.length - 1) && inView(p).indexOf(l.itemAtIndex(p.shown.length - 1)) >= 0, 2000, "its Background in view")
        }

        // ---- the list's model ---------------------------------------------------------------------------------------
        // follow() makes the model any list of keys: rows moved, a run moved, in one change; random removals, insertions
        // and moves of runs, the model as asked each time. On a panel with no document, so the list is the test's.
        function test_follow() {
            Docs.reset("Start")
            const p = createTemporaryObject(panelC, root), m = list(p).model
            compare([Docs.current, m.count], [null, 0])
            let ops = 0, seed = 27
            const op = () => ops++
            m.rowsInserted.connect(op)
            m.rowsRemoved.connect(op)
            m.rowsMoved.connect(op)
            const keys = () => {
                const out = []
                for (let i = 0; i < m.count; i++)
                    out.push(m.get(i).key)
                return out
            }
            const rnd = n => {
                seed = (seed * 1103515245 + 12345) % 2147483648
                return Math.floor(seed / 65536) % n
            }
            let cur = [], made = 0
            for (; made < 40; made++)
                cur.push("k" + made)
            p.follow(cur)
            compare(keys(), cur)
            for (let t = 0; t < 60; t++) {  // a row, or a run of rows, moved: one change
                const next = cur.slice(), c = t < 30 ? 1 : 1 + rnd(12), from = rnd(next.length - c + 1), run = next.splice(from, c)
                Array.prototype.splice.apply(next, [rnd(next.length + 1), 0].concat(run))
                ops = 0
                p.follow(next)
                compare(keys(), next, "moved " + c)
                verify(ops <= 1, c + " moved in " + ops + " changes")
                cur = next
            }
            for (let t = 0; t < 300; t++) {  // anything
                let next = cur.filter(() => rnd(8) > 0)
                for (let k = rnd(4); k > 0; k--) {
                    const c = 1 + rnd(5), run = next.splice(rnd(next.length + 1), c)
                    Array.prototype.splice.apply(next, [rnd(next.length + 1), 0].concat(run))
                }
                for (let k = rnd(4); k > 0; k--) {
                    const at = rnd(next.length + 1), run = []
                    for (let c = 1 + rnd(4); c > 0; c--)
                        run.push("k" + made++)
                    Array.prototype.splice.apply(next, [at, 0].concat(run))
                }
                p.follow(next)
                compare(keys(), next, "round " + t)
                cur = next
            }
            p.follow([])
            compare(m.count, 0)
        }

        // ---- Blend's list --------------------------------------------------------------------------------------------
        // A shut Dropdown makes no rows for its list (a change of options made them all again, ~10 ms: Blend's 27 or 28
        // modes as the active layer is a group or not); open, it has them, and works as before: the keys on the shut box,
        // above the box when there is no room under it, its height the rows'.
        function test_dropdown_shut() {
            const modes = []
            for (let i = 0; i < 28; i++)
                modes.push(["m" + i, "Mode " + i])
            const d = createTemporaryObject(dropC, root, { options: modes.slice(1), value: "m20", y: 20 })
            const e = createTemporaryObject(dropC, root, { options: modes.slice(0, 3), value: "m1", y: 60 })
            const list = d.popup.contentItem, rows = () => all(list, o => o instanceof ItemDelegate).length
            waitForRendering(d)
            for (let k = 0; k < 6; k++)
                d.options = modes.slice(k % 2)
            compare(rows(), 0, "a shut list makes no rows")
            compare(d.displayText, "Mode 20")
            d.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Down)
            compare(d.value, "m21", "↓ on the shut box")
            keyClick(Qt.Key_Up)
            compare(d.value, "m20", "↑")
            keyClick(Qt.Key_Down, Qt.AltModifier)  // as ↓ (Qt's ComboBox opens on Space, Enter by look.h)
            compare([d.value, d.popup.visible], ["m21", false], "Alt ↓")
            keyClick(Qt.Key_Up)
            for (const k of [Qt.Key_Return, Qt.Key_Space]) {
                keyClick(k)
                tryVerify(() => d.popup.opened, 2000, "opened by " + k)
                waitForRendering(list)
                verify(rows() > 8, "its rows: " + rows())
                compare(d.popup.height, 320)
                verify(d.popup.y > 0, "under the box")
                const r = list.itemAtIndex(d.valueIndex)
                verify(r && r.y >= list.contentY && r.y + r.height <= list.contentY + list.height, "the value's row in view")
                keyClick(Qt.Key_Escape)
                tryVerify(() => !d.popup.visible)
                compare(d.value, "m20", "Esc: as it was")
                waitForRendering(d)
                compare(rows(), 0, "shut again: no rows")
            }
            keyClick(Qt.Key_Tab)
            verify(e.activeFocus, "Tab goes on")
            keyClick(Qt.Key_Return)
            tryVerify(() => e.popup.opened)
            compare(all(e.popup.contentItem, o => o instanceof ItemDelegate).map(r => r.index), [0, 1, 2], "its rows made in their order")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryVerify(() => !e.popup.visible)
            compare(e.value, "m2", "a row picked with the keys")
            // near the window's bottom: 3 rows (96 tall) do not fit in the 50 under the box, so above it
            e.y = root.height - e.height - 50
            mouseClick(e)
            tryVerify(() => e.popup.opened)
            compare(e.popup.height, 3 * 28 + 12)
            verify(e.popup.y < 0, "above the box: " + e.popup.y)
            e.popup.close()
            tryVerify(() => !e.popup.visible)
            e.y = root.height - e.height - 120  // room for them
            mouseClick(e)
            tryVerify(() => e.popup.opened)
            verify(e.popup.y > 0, "under the box: " + e.popup.y)
            e.popup.close()
        }

        // ---- time -------------------------------------------------------------------------------------------------------
        // Add, delete, duplicate, delete, group, ungroup, round after round on a document of 250 layers: timed with a
        // panel showing it (the command and the list laid out; find() done before), then with no panel. The panel's part
        // of a command is the difference of their medians, within a 60 Hz frame. Other jobs on the machine only add
        // time: up to three tries, the best of each command counts.
        function timed(real) {
            const d = make(real, 250, real ? "u27-do" : "u27-do-gia")
            Docs.select(Docs.docs.indexOf(d))
            // two layers in the middle, next to each other, neither a group nor in one: group, ungroup turn the active
            // layer into a group and back (Blend's list changes)
            const plain = q => !!q && q.kind !== "group" && !q.depth && !q.props.fx
            const at = d.rows.findIndex((q, i) => i >= d.rows.length / 2 && plain(q) && plain(d.rows[i + 1]))
            const mid = d.rows[at].props.name, under = d.rows[at + 1].props.name
            const steps = [
                ["newLayer", () => pickIds(d, [byName(d, mid).id]), { op: "newLayer" }],
                ["deleteLayers", () => {}, { op: "deleteLayers" }],
                ["duplicateLayers", () => pickIds(d, [byName(d, mid).id]), { op: "duplicateLayers" }],
                ["deleteLayers", () => {}, { op: "deleteLayers" }],
                ["groupLayers", () => pickIds(d, [byName(d, under).id, byName(d, mid).id]), { op: "groupLayers" }],
                ["ungroup", () => {}, { op: "ungroup" }]]
            const pass = l => {  // ms of each command, by op
                const ms = {}
                for (let round = 0; round < 8; round++) {
                    for (const s of steps) {
                        s[1]()
                        const t0 = Date.now()
                        verify(d.run(s[2]), s[0])
                        if (l)
                            l.forceLayout()
                        ;(ms[s[0]] = ms[s[0]] || []).push(Date.now() - t0)
                    }
                    wait(0)
                }
                return ms
            }
            const median = a => a.slice().sort((x, y) => x - y)[Math.floor(a.length / 2)]
            const best = {}
            for (let t = 0; t < 3 && !(Object.keys(best).length && Object.keys(best).every(k => best[k] <= 16)); t++) {
                const p = createTemporaryObject(panelC, root)
                waitForRendering(p)
                scrollTo(p, p.shown.findIndex(r => r.l.props.name === mid && !r.fx) - 6)
                const shown = pass(list(p))
                p.destroy()
                wait(0)
                const alone = pass(null)
                for (const k in shown) {
                    const part = median(shown[k]) - median(alone[k])
                    best[k] = Math.min(best[k] ?? Infinity, part)
                    console.log((real ? "real " : "mock ") + k + ": " + median(shown[k]) + " ms with the panel, " + median(alone[k]) + " ms without, the panel's " + part + " ms")
                }
            }
            return best
        }
        function test_time_mock() {
            const t = timed(false)
            for (const k in t)
                verify(t[k] <= 16, k + ": " + t[k] + " ms")
        }
        function test_time_real() {
            const t = timed(true)
            for (const k in t)
                verify(t[k] <= 16, k + ": " + t[k] + " ms")
        }
    }
}
