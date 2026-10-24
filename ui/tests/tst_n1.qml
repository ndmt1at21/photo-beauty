import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty
import "../qml/Mock.js" as Mock
import "../qml/MenuSpec.js" as MenuSpec

// N1: files opened for real (Session, DocModel): JPEG, PNG and TIFF read in a Background job into a Doc whose core is
// the core's history, beside the mock documents; Layers' commands change its DocState, each one step, undo giving back
// the very tiles (T2); History's steps and snapshots; the canvas painting into the same history; 1.000 layers shaped
// as D4 scrolled within a frame; what the core lacks greyed (decision 4); the mock documents as they were. The files
// are made for the test (fixtures, ui/tests/main.cpp).
Item {
    id: root
    width: 400
    height: 300
    Component { id: mainC; Main {} }
    Component {
        id: dcC
        DocCanvas {
            property var steps: []  // painted()'s
            onPainted: step => steps.push(step)
        }
    }
    Component { id: historyC; HistoryPanel { width: 280 } }

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
        name: "N1"
        when: windowShown

        function init() {
            Docs.reset("Main")
            Tools.reset("Main")
            Docs.error = ""
        }

        // ---- helpers ------------------------------------------------------------------------------------------------
        function file(name, kind, w, h, o) {
            o = o || {}
            const p = fixtures.image(name, kind, w, h, o.bits || 8, !!o.gray, !!o.alpha)
            verify(p, "made " + name)
            return p
        }
        function opened(path) {
            const d = Docs.openFile(path)
            verify(d && d.real, "opened for real: " + path)
            tryVerify(() => !d.model.loading, 20000, "read")
            compare(d.model.error, "")
            return d
        }
        function core(d) { return JSON.parse(d.model.dump()) }  // the core's DocState: layers bottom first, tile ids
        // The tab of `file`, chosen: every Main opens PB_CANVAS's file after the rest (N1), so the one in front is not
        // always the one a test means.
        function pick(file) {
            const i = Docs.docs.findIndex(d => d.file === file)
            verify(i >= 0, file + " open")
            Docs.select(i)
            return Docs.current
        }
        function open(o) {
            const w = createTemporaryObject(mainC, root, Object.assign({ startState: "Main" }, o || {}))
            Tools.dockMin = Object.assign({}, Tools.dockMin, { p: true })  // Properties folded: Layers taller (tst_u9)
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function panel(w) { return find(w.contentItem, o => o instanceof LayersPanel && o.visible) }
        function list(w) { return find(panel(w), o => o instanceof ListView) }
        function rows(w) {
            const l = list(w)
            return all(l.contentItem, o => o instanceof LayerRow && o.visible && !o.fx).sort((a, b) => a.y - b.y)
        }
        function button(item, name) { return find(item, o => o instanceof AbstractButton && o.Accessible.name === name) }
        function items(menus) {  // every MenuSpec item, submenus' too
            let out = []
            for (const m of menus)
                for (const it of m.items || m)
                    if (it.t !== "sep") {
                        out.push(it)
                        if (Array.isArray(it.sub))
                            out = out.concat(items([it.sub]))
                    }
            return out
        }

        // ---- opening --------------------------------------------------------------------------------------------------
        function test_open_data() {
            return [
                { tag: "png", kind: "png", mode: "RGB", bits: 8 },
                { tag: "png-16-grey-alpha", kind: "png", bits: 16, gray: true, alpha: true, mode: "Gray" },
                { tag: "png-alpha", kind: "png", alpha: true, mode: "RGB", bits: 8 },
                { tag: "jpg", kind: "jpg", mode: "RGB", bits: 8 },
                { tag: "jpg-grey", kind: "jpg", gray: true, mode: "Gray", bits: 8 },
                { tag: "tif", kind: "tif", mode: "RGB", bits: 8 },
                { tag: "tif-16-alpha", kind: "tif", bits: 16, alpha: true, mode: "RGB" },
                { tag: "tif-grey", kind: "tif", gray: true, mode: "Gray", bits: 8 }
            ]
        }
        // In the background, a tab at once with the task on the canvas and nothing to run; then its Background.
        function test_open(data) {
            const p = file("mo-" + data.tag + "." + data.kind, data.kind, 300, 200, data)
            verify(Session.canOpen(p))
            const n = Docs.docs.length
            const d = Docs.openFile(p)
            verify(d.real && d.model.loading, "read in the background")
            compare(Docs.docs.length, n + 1)
            compare(Docs.current, d)
            compare(d.file, "mo-" + data.tag + "." + data.kind)
            compare(d.task, "Đang mở " + d.file)
            verify(d.progress >= 0 && d.progress < 1)
            compare([d.w, d.h], [300, 200], "its size from its header, before it is read")
            verify(!d.can("newLayer") && !d.can("@delLayer") && !d.run({ op: "newLayer" }), "nothing runs on it meanwhile")
            verify(d.can("@zoomIn"), "the view does")
            tryVerify(() => !d.model.loading, 20000)
            compare(d.model.error, "")
            compare([d.w, d.h, d.mode, d.bits], [300, 200, data.mode, data.bits])
            compare(d.layers.length, 1)
            const bg = d.layers[0]
            compare([bg.kind, bg.props.name, bg.props.background, bg.props.lockAll, bg.depth, bg.parent], ["pixel", "Background", true, true, 0, 0])
            for (const k of Object.keys(Mock.PROPS))
                verify(k in bg.props, "props as the mock's: " + k)
            compare(d.selected, [bg.id])
            compare(d.active, bg, "the active layer: the entry of layers")
            compare(d.history.map(h => [h.name, h.icon]), [["Mở", "folder_open"]])
            compare([d.index, d.snapshot], [0, -1])
            compare(d.snapshots.map(s => s.name), [d.file])
            verify(!d.dirty)
            compare([d.task, d.progress], ["", -1])
            verify(/MB|KB/.test(d.mem), d.mem)
            compare(Docs.recent[0].path, d.model.path)
            compare(Docs.recent[0].file, d.file)
            const c = core(d)
            compare([c.width, c.height, c.layers.length, c.selected], [300, 200, 1, [bg.id]])
            verify(c.layers[0].tiles.length > 0, "its pixels in tiles")
            compare(Docs.openFile(p), d, "open already: its tab")
            compare(Docs.docs.length, n + 1)
        }

        // Mở…, a drop, the command line, Gần đây: the same way in; what is not an image here the mock opens by name.
        function test_ways_in() {
            // a name its file: URL escapes: "file://" + the path is not its URL (nor any on Windows, C: its host)
            const p = file("vao #1.png", "png", 200, 100), q = file("dong-lenh.tif", "tif", 120, 80)
            const w = open({ startFiles: [q] })
            // the command line's, for real; in front unless PB_CANVAS names a file, which Main opens after it (N1)
            tryVerify(() => Docs.docs.some(d => d.real && d.model.path === q), 5000, "the command line's")
            const given = Docs.docs.find(d => d.real && d.model.path === q)
            compare(given.file, "dong-lenh.tif")
            compare(Docs.docs.indexOf(given), Docs.docs.length - (CanvasEnv.document ? 2 : 1), "the last tab, or the one before PB_CANVAS's")
            compare(Docs.docs.indexOf(Docs.current), Docs.docs.length - 1, "the last file given in front")
            w.openFiles([fixtures.url(p)])  // dropped, picked in Mở…: a file: URL, as Qt makes it
            const d = Docs.current
            verify(d.real)
            compare(d.model.path, p)
            tryVerify(() => !d.model.loading, 20000)
            w.openFiles([p])
            compare(Docs.current, d, "a path to a file open already: its tab")
            w.openFiles(["file:///khong/co/anh%20moi.jpg"])  // not there: the mock's, by name (tst_u4)
            verify(!Docs.current.real)
            compare(Docs.current.file, "anh moi.jpg")
            // Gần đây: closed, then from the list again, for real
            const path = d.model.path
            Docs.close(d)
            const i = Docs.recent.findIndex(r => r.path === path)
            verify(i >= 0)
            const again = Docs.openRecent(i)
            verify(again.real && again.model.path === path)
            tryVerify(() => !again.model.loading, 20000)
            compare(Docs.open("vao #1.png"), again, "by name, as the start screen opens it")
            verify(Docs.recent.filter(r => r.path === path).length === 1, "once in the list")
        }

        // A file that does not open: closed, why in a toast; one cancelled while read: closed, its result dropped.
        function test_fails() {
            const w = open()
            const n = Docs.docs.length
            const d = Docs.openFile(file("hong.png", "broken", 300, 200))
            verify(d.real)
            tryVerify(() => Docs.docs.length === n, 20000, "closed")
            verify(Docs.error.indexOf("Không mở được hong.png: ") === 0, Docs.error)
            const t = find(w.contentItem.Window.window.Overlay.overlay, o => o instanceof Toast)
            verify(t && t.visible && t.err, "a toast says why")
            compare(t.text, Docs.error)
            const big = Docs.openFile(file("huy.png", "png", 3000, 2000))
            verify(big.model.loading)
            Docs.cancelTask(big)  // CanvasBar's cancel
            compare(Docs.docs.length, n)
            wait(1500)
            compare(Docs.docs.length, n, "its result went nowhere")
            verify(!Session.canOpen(fixtures.folder() + "/khong-co.png"))
            verify(!Session.open(fixtures.folder() + "/khong-co.png", 100, 100))
        }

        // ---- commands, history --------------------------------------------------------------------------------------
        // Layers' commands change the core's state, each one step named as the mock names it; undo gives back each state,
        // the same tiles (ids); History jumps; what the core does not have is refused, no step.
        function test_commands() {
            const d = opened(file("lenh.png", "png", 600, 400))
            const states = [d.model.dump()], names = ["Mở"]
            const step = (cmd, name) => {
                verify(d.run(cmd), JSON.stringify(cmd))
                states.push(d.model.dump())
                names.push(name)
                compare(d.history.map(h => h.name), names)
                compare(d.index, names.length - 1)
                verify(d.dirty)
            }
            const bg = d.layers[0].id, tiles = core(d).layers[0].tiles
            step({ op: "newLayer" }, "Layer mới")
            const l1 = d.active.id
            compare(d.active.props.name, "Layer 1")
            compare(d.layers.map(l => l.id), [l1, bg], "top first")
            compare(core(d).layers.map(l => l.id), [bg, l1], "the core's: bottom first")
            step({ op: "setProps", props: { name: "Lá" } }, "Đổi tên layer")
            compare(core(d).layers[1].props.name, "Lá")
            step({ op: "setProps", props: { blend: "multiply" } }, "Blend mode")
            compare(core(d).layers[1].props.blend, "multiply")
            step({ op: "setProps", props: { opacity: 0.5 } }, "Opacity")
            compare([core(d).layers[1].props.opacity, d.active.props.opacity], [0.5, 0.5])
            step({ op: "setProps", props: { fill: 0.25 } }, "Fill")
            step({ op: "setProps", props: { visible: false } }, "Ẩn layer")
            compare(core(d).layers[1].props.visible, false)
            step({ op: "setProps", props: { visible: true } }, "Hiện layer")
            step({ op: "setProps", props: { lockAll: true, lockTransparency: false, lockPixels: false, lockPosition: false } }, "Khóa layer")
            step({ op: "setProps", props: { tag: "#e8664a" } }, "Thuộc tính layer")
            compare([d.active.props.tag, core(d).layers[1].props.tag], ["#e8664a", "#e8664a"], "the colour label kept by the core")
            step({ op: "groupLayers" }, "Nhóm layer")
            const g = d.active.id
            compare([d.active.kind, d.active.props.name, d.active.props.blend], ["group", "Group 1", "passThrough"])
            compare(d.layer(l1).parent, g)
            compare(core(d).layers[1].layers.map(l => l.id), [l1])
            verify(d.run({ op: "selectLayers", layers: [l1] }))  // joins the step it is on
            states[states.length - 1] = d.model.dump()
            compare(d.history.length, names.length, "choosing a layer: no step")
            step({ op: "duplicateLayers" }, "Nhân bản layer")
            const copy = d.active.id
            compare([d.active.props.name, d.active.props.lockAll], ["Lá bản sao", false], "named as the mock's copies, unlocked")
            compare(core(d).layers[1].layers.map(l => l.id), [l1, copy])
            compare(core(d).layers[1].layers[1].tiles, core(d).layers[1].layers[0].tiles, "the copy shares its tiles")
            step({ op: "moveLayer", layer: copy, parent: 0, index: 1 }, "Sắp xếp layer")
            compare(core(d).layers.map(l => l.id), [bg, copy, g])
            step({ op: "arrange", dir: "top" }, "Sắp xếp layer")
            compare(core(d).layers.map(l => l.id), [bg, g, copy])
            step({ op: "ungroup", group: g }, "Bỏ nhóm")
            compare(core(d).layers.map(l => l.id), [bg, l1, copy])
            step({ op: "newGroup" }, "Nhóm mới")
            step({ op: "deleteLayers", layers: [l1] }, "Xóa layer")
            compare(d.selected, [bg], "the layer in its place selected, as the mock's")
            compare(core(d).selected, [bg])
            // refused, no step: the Background stays and nothing goes below it, no change, what P1 adds
            verify(!d.run({ op: "moveLayer", layer: bg, parent: 0, index: 1 }))
            verify(!d.run({ op: "moveLayer", layer: copy, parent: 0, index: 0 }))
            verify(!d.run({ op: "groupLayers", layers: [bg] }))
            verify(!d.run({ op: "setProps", layer: copy, props: { name: "Lá bản sao" } }), "no change")
            verify(!d.run({ op: "setProps", layer: copy, props: { fx: { dropShadow: { on: true } } } }), "a layer style: P1")
            verify(!d.run({ op: "setProps", layer: copy, props: { opacity: 2 } }))
            verify(!d.run({ op: "mask", action: "reveal" }), "masks: P1")
            verify(!d.run({ op: "addLayer", parent: 0, index: 0, kind: "text", props: {} }), "type layers: P1")
            verify(!d.run({ op: "nope" }))
            compare(d.history.length, names.length)
            compare(d.model.dump(), states[states.length - 1], "nothing changed")
            // undo: each state back, the same tile ids (T2); redo; History's jumps
            for (let i = names.length - 1; i > 0; i--) {
                d.undo()
                compare(d.model.dump(), states[i - 1], "undone to step " + (i - 1))
            }
            compare(core(d).layers[0].tiles, tiles, "the file's tiles, the same ones")
            verify(!d.can("undo") && !d.dirty)
            for (let i = 1; i < names.length; i++) {
                d.redo()
                compare(d.model.dump(), states[i], "redone to step " + i)
            }
            for (const i of [3, 0, names.length - 1, 7]) {
                d.jumpTo(i)
                compare([d.model.dump(), d.index], [states[i], i], "History: step " + i)
            }
            d.toggleLast()
            compare(d.index, names.length - 1)
            d.revert()
            compare(d.index, 0)
            d.jumpTo(5)
            d.deleteStep()
            compare(d.history.length, 5, "the step and those after it gone")
            compare(d.model.dump(), states[4])
            verify(d.run({ op: "newLayer" }))
            compare(d.history.length, 6)
            d.markSaved()
            verify(!d.dirty)
            d.undo()
            verify(d.dirty)
        }

        // A drag on Opacity: one step; the panel's setAll(), every layer picked; lock, the eye; the active one after.
        function test_gesture() {
            const d = opened(file("keo.png", "png", 300, 200))
            verify(d.run({ op: "newLayer" }) && d.run({ op: "newLayer" }))
            const ids = d.layers.map(l => l.id)
            verify(d.run({ op: "selectLayers", layers: [ids[1], ids[0]] }))
            d.beginGesture()
            for (const o of [0.9, 0.7, 0.4])
                for (const id of d.selected)
                    verify(d.run({ op: "setProps", layer: id, props: { opacity: o } }))
            d.endGesture()
            compare(d.history.map(h => h.name), ["Mở", "Layer mới", "Layer mới", "Opacity"])
            compare(d.layers.slice(0, 2).map(l => l.props.opacity), [0.4, 0.4])
            d.undo()
            compare(d.layers.slice(0, 2).map(l => l.props.opacity), [1, 1])
            compare(d.selected, [ids[1], ids[0]], "the layers picked are part of the state")
            verify(Docs.exec("@hideLayer"))
            compare(d.layers.slice(0, 2).map(l => l.props.visible), [false, false])
            compare(d.history[d.index].name, "Ẩn layer")
            verify(Docs.exec("@lockAll"))
            compare(d.active.props.lockAll, true)
            verify(Docs.exec("@delHidden"))
            compare(d.layers.length, 1)
            compare(d.history[d.index].name, "Xóa layer đang ẩn")
            verify(!Docs.exec("@delHidden"), "none hidden")
        }

        function test_snapshots() {
            const d = opened(file("chup.png", "png", 300, 200))
            const s0 = d.model.dump()
            verify(d.run({ op: "newLayer" }))
            d.newSnapshot()
            compare(d.snapshots.map(s => s.name), [d.file, "Snapshot 1"])
            verify(d.snapshots.every(s => /^image:\/\/pb\/doc\/\d+$/.test(s.url)), "thumbnails of the states")
            verify(d.run({ op: "newLayer" }))
            const s2 = d.model.dump()
            d.jumpToSnapshot(0)
            compare([d.model.dump(), d.snapshot, d.index], [s0, 0, 0], "the file as it opened: its step")
            d.jumpToSnapshot(1)
            compare([d.snapshot, d.index, d.layers.length], [1, 1, 2])
            d.jumpTo(2)
            compare([d.snapshot, d.model.dump()], [-1, s2])
        }

        // Image › Nhân bản, History's Tài liệu mới từ bước này: the state shown, a tab of its own.
        function test_duplicate() {
            const d = opened(file("goc.png", "png", 300, 200))
            verify(d.run({ op: "newLayer" }))
            const c = Docs.duplicate(d)
            verify(c.real && c !== d)
            compare(c.file, "goc bản sao.png")
            compare(c.history.map(h => h.name), ["Nhân bản"])
            compare(c.layers.map(l => l.props.name), d.layers.map(l => l.props.name))
            compare(core(c).layers, core(d).layers, "the same layers, the same tiles")
            verify(c.run({ op: "newLayer" }))
            compare([c.layers.length, d.layers.length], [3, 2], "histories of their own")
        }

        // ---- the panels -----------------------------------------------------------------------------------------------
        // Layers on a real document: its Background with the layer's pixels as the thumbnail; the panel's commands; the
        // mask button greyed until the core has masks.
        function test_layers_panel() {
            const w = open()
            const d = opened(file("panel.png", "png", 800, 600))
            waitForRendering(w.contentItem)
            tryVerify(() => rows(w).length === 1)
            const r = rows(w)[0]
            compare(r.text, "Background")
            compare(r.thumb, "photo")
            verify(/^image:\/\/pb\/layer\/\d+$/.test(String(r.src)), String(r.src))
            tryVerify(() => find(r, o => o instanceof Image && o.visible && o.status === Image.Ready), 5000, "the thumbnail drawn")
            verify(!button(panel(w), "Thêm Layer Mask").enabled, "masks: greyed on a real document")
            mouseClick(button(panel(w), "Layer mới"))
            compare(d.layers.length, 2)
            tryVerify(() => rows(w).length === 2)
            compare(rows(w)[0].text, "Layer 1")
            panel(w).setAll({ opacity: 0.3 })
            compare(d.active.props.opacity, 0.3)
            mouseClick(button(rows(w)[0], "Ẩn hiện Layer 1"))
            compare(d.active.props.visible, false)
            compare(d.history.map(h => h.name), ["Mở", "Layer mới", "Opacity", "Ẩn layer"])
            panel(w).startRename(d.active.id)
            const field = find(panel(w), o => o instanceof TextInput && o.visible && o.activeFocus)
            verify(field)
            field.selectAll()
            keyClick(Qt.Key_L)
            keyClick(Qt.Key_A)
            keyClick(Qt.Key_Return)
            compare(d.active.props.name, "la")
            mouseClick(button(panel(w), "Xóa layer"))
            compare(d.layers.length, 1)
            // the menus' keys on a document opened in front while it was read (their bindings follow Doc.rev)
            w.contentItem.forceActiveFocus()
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            compare(d.layers.length, 2, "Ctrl Z")
            keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
            compare(d.layers.length, 1, "Ctrl Shift Z")
            Docs.select(0)  // the portrait: the mask button as before
            waitForRendering(w.contentItem)
            verify(button(panel(w), "Thêm Layer Mask").enabled)
        }

        // History on a real document: its steps; a click jumps; a snapshot's thumbnail its own state.
        function test_history_panel() {
            const d = opened(file("lich-su.png", "png", 300, 200))
            verify(d.run({ op: "newLayer" }) && d.run({ op: "setProps", props: { name: "x" } }))
            const h = createTemporaryObject(historyC, root)
            waitForRendering(h)
            const step = name => find(h, o => o instanceof AbstractButton && o.Accessible.name === name)
            verify(step("Layer mới") && step("Đổi tên layer"))
            mouseClick(step("Layer mới"))
            compare(d.index, 1)
            compare(d.layers[0].props.name, "Layer 1")
            const thumb = find(h, o => o instanceof Image && o.visible && String(o.source).indexOf("image://pb/doc/") === 0)
            verify(thumb, "the snapshot's thumbnail")
            tryVerify(() => thumb.status === Image.Ready, 5000)
        }

        // What the core lacks greyed on a real document (decision 4): dialogs, spaces and P1's commands; P0's on. A mock
        // document as before.
        function test_greyed() {
            const w = open()
            verify(!pick("chân dung.psd").real)
            const all = items(MenuSpec.MENUS)
            const before = it => {  // Main.can() before N1
                if (!it || it.t === "sep" || it.disabled || !(it.free || Docs.current))
                    return false
                if (it.dialog ? !w.hasDialog(it.dialog) : it.space ? !w.hasSpace(it.space) : false)
                    return false
                return Docs.can(it.req || it.act || "")
            }
            for (const it of all)
                compare(w.can(it), before(it), "a mock document: " + it.l)
            const d = opened(file("mo.png", "png", 300, 200))
            verify(d.run({ op: "newLayer" }))
            const p1 = ["@mode:Gray", "@bits:16", "@merge:down", "@merge:flat", "@selAll", "@xf", "@mask:reveal", "@clip", "@toSO", "@rast",
                "@quickExport", "@adj:Curves", "@fill:Solid Color", "@crot:90", "@cflip:h", "@invert", "@paste", "@place", "@qm",
                "@ants:subject", "@newGuide", "@rot:90", "@flip:h"]
            for (const it of all) {
                const p = MenuSpec.name(it.act || "")
                if ((it.dialog || it.space) && !it.free && it.dialog !== "SaveOptions" || p1.indexOf(it.act) >= 0)
                    verify(!w.can(it), "greyed: " + it.l)
            }
            for (const it of all.filter(x => x.dialog === "SaveOptions"))  // Lưu thành…, Lưu bản sao…: a .pbd (N2)
                verify(w.can(it), "the core has it: " + it.l)
            const on = ["@undo", "@newLayer", "@newGroup", "@dupLayer", "@delLayer", "@groupLayer", "@hideLayer", "@arr:up", "@lockAll",
                "@zoomIn", "@zoom100", "@win:Layers", "@tog:rulers", "@save"]
            for (const a of on) {
                const it = all.find(x => x.act === a)
                verify(it, a)
                verify(w.can(it), "the core has it: " + it.l)
            }
            for (const it of all.filter(x => x.free))
                compare(w.can(it), before(it), "free: " + it.l)
            verify(!Docs.exec("@mode:Gray") && d.mode === "RGB")
            verify(!d.supports("mask") && !d.supports({ dialog: "ImageSize" }) && d.supports({ dialog: "NewDoc", free: true }))
            Docs.select(0)
            verify(Docs.current.supports("mask") && Docs.current.supports({ dialog: "ImageSize" }) && !Docs.current.real)
            compare(Docs.current.thumbUrl(Docs.current.layers[0]), Mock.photoUrl(Docs.current.layers[0].props.photo))
            compare([Docs.current.notice, Docs.current.task, Docs.current.progress], ["", "", -1])
            verify(!Docs.exec("@nope"), "an action without a command: false, on the mock too")
            // a binding on supports() follows the file being read (Doc.rev): no while it is, yes once it is
            const probe = Qt.createQmlObject("import QtQuick; QtObject { property var doc; readonly property bool takes: !!doc && doc.supports('newLayer') }", root)
            probe.doc = Docs.openFile(file("doc-sau.png", "png", 300, 200))
            verify(probe.doc.model.loading && !probe.takes, "while it is read")
            tryVerify(() => probe.takes, 20000, "once read")
            probe.destroy()
        }

        // Decision 4 in the dock's other panels: on a real document Properties, Retouch and Channels (its menu too) are
        // greyed whole and Adjustments' buttons, as the core has none of their commands yet; a mock document's as they were.
        function test_panels_greyed() {
            const w = open()
            const d = opened(file("panels.png", "png", 300, 200))
            const kinds = { Properties: PropertiesPanel, Adjustments: AdjustmentsPanel, Retouch: RetouchPanel, Channels: ChannelsPanel }
            const shown = name => {
                Tools.showPanel(name)
                waitForRendering(w.contentItem)
                const p = find(w.contentItem, o => o instanceof kinds[name] && o.visible)
                verify(p, name + " shown")
                return p
            }
            const buttons = p => all(p, o => o instanceof AbstractButton)
            const check = real => {
                for (const name of Object.keys(kinds)) {
                    const p = shown(name)
                    verify(buttons(p).length > 0, name)
                    if (name === "Adjustments")
                        verify(buttons(p).every(b => b.enabled !== real), name + "'s buttons")
                    else
                        compare(p.enabled, !real, name)
                    if (name === "Channels")
                        verify(real ? p.menu.every(m => m.t === "sep" || m.disabled) : !p.menu[0].disabled && !p.menu[4].disabled, "its menu")
                }
            }
            check(true)
            mouseClick(button(shown("Channels"), "Red"))
            compare(d.channel, "RGB", "a greyed row does nothing")
            Docs.select(0)
            verify(!Docs.current.real)
            check(false)
        }

        // The notice bar and the progress on the canvas: the document's (CanvasBar in Overlays).
        function test_canvas_bar() {
            const w = open()
            const d = Docs.openFile(file("thanh.png", "png", 2000, 1500))
            waitForRendering(w.contentItem)
            const bar = find(w.contentItem, o => o instanceof CanvasBar && o.visible)
            verify(bar)
            if (d.model.loading)
                compare([bar.task, bar.progress >= 0], ["Đang mở thanh.png", true])
            tryVerify(() => !d.model.loading, 20000)
            compare([bar.task, bar.progress, bar.notice], ["", -1, ""])
            d.model.setNotice("Có layer chưa hỗ trợ")
            compare(bar.notice, "Có layer chưa hỗ trợ")
            bar.dismissed()
            compare(d.notice, "")
            d.model.setTask("Gaussian Blur · cả ảnh", 0.4)
            compare([bar.task, bar.progress], ["Gaussian Blur · cả ảnh", 0.4])
        }

        // ---- the canvas -----------------------------------------------------------------------------------------------
        // DocCanvas on the document's history: its pixels; a stroke one of its steps (History, the unsaved dot); a
        // command of the panels ends the stroke first; Hoàn tác through Docs; closed, the canvas lets go of it.
        function test_canvas() {
            const d = opened(file("ve.png", "png", 400, 300))
            const c = createTemporaryObject(dcC, root, { width: 400, height: 300, model: d.model, tool: "brush",
                view: { x: 200, y: 150, zoom: 1, angle: 0 }, brush: { size: 16, hardness: 1, color: "#0000ff" } })
            tryVerify(() => c.frames > 0, 10000)
            compare(c.docSize, Qt.size(400, 300))
            const px = (x, y) => [Math.floor(255 * x / 400) * 257, Math.floor(255 * y / 300) * 257, ((x + y) & 255) * 257, 65535]
            compare(c.composite(0, 100, 150), px(100, 150), "the file's pixels")
            const blue = [0, 0, 65535, 65535]
            mousePress(c, 100, 150)
            mouseMove(c, 150, 150)
            mouseRelease(c, 150, 150)
            c.wait()
            compare(c.steps, ["Brush"])
            compare(d.history.map(h => [h.name, h.icon]), [["Mở", "folder_open"], ["Brush", "brush"]])
            verify(d.dirty)
            compare(c.composite(0, 125, 150), blue)
            mousePress(c, 100, 200)
            mouseMove(c, 150, 200)
            verify(d.run({ op: "newLayer" }), "a command mid-stroke")
            mouseRelease(c, 150, 200)
            c.wait()
            compare(d.history.map(h => h.name), ["Mở", "Brush", "Brush", "Layer mới"], "the stroke ended and its step first")
            compare(c.composite(0, 125, 200), blue)
            verify(Docs.exec("@undo") && Docs.exec("@undo"))
            c.wait()
            compare([c.composite(0, 125, 200), c.composite(0, 125, 150)], [px(125, 200), blue], "back to the first stroke")
            verify(c.undo(), "the canvas's own undo: the document's")
            compare(d.index, 0)
            c.wait()
            compare(c.composite(0, 125, 150), px(125, 150))
            Docs.close(d)
            tryVerify(() => c.model === null, 5000, "let go of once closed")
            compare(c.docSize, Qt.size(0, 0))
        }

        // Main's Viewport on a real document: its DocCanvas on the document's history, a stroke a step of it, which History's
        // jumps and Ctrl Z move; the brush pointer round as the core's tip; the document saved as a .pbd opens for real.
        function test_viewport() {
            const w = open()
            const d = opened(file("vp.png", "png", 400, 300))
            const dc = find(w.contentItem, o => o instanceof DocCanvas)
            verify(dc.visible && dc.model === d.model)
            verify(!find(w.contentItem, o => o instanceof MockCanvas).visible)
            tryVerify(() => dc.frames > 0 && !dc.shown.pending, 10000, "a frame")
            Tools.setTool("brush")
            Tools.setFg("#0000ff")
            Tools.setValues({ "brush:size": 16, "brush:hard": 100, "brush:7": 100 })
            const was = dc.composite(0, 200, 150), blue = [0, 0, 65535, 65535]
            const a = dc.toCanvas(Qt.point(180, 150)), b = dc.toCanvas(Qt.point(220, 150))
            mousePress(dc, a.x, a.y)
            mouseMove(dc, (a.x + b.x) / 2, a.y)
            mouseMove(dc, b.x, b.y)
            mouseRelease(dc, b.x, b.y)
            dc.wait()
            compare(d.history.map(h => h.name), ["Mở", "Brush"], "the stroke a step of the document's history")
            compare(dc.composite(0, 200, 150), blue)
            d.jumpTo(0)  // a click on History's first step
            dc.wait()
            compare(dc.composite(0, 200, 150), was, "History's jump on the canvas too")
            d.jumpTo(1)
            dc.wait()
            compare(dc.composite(0, 200, 150), blue)
            w.contentItem.forceActiveFocus()
            keyClick(Qt.Key_Z, Qt.ControlModifier)
            dc.wait()
            compare([d.index, dc.composite(0, 200, 150)], [0, was], "Ctrl Z")
            keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
            dc.wait()
            compare(dc.composite(0, 200, 150), blue)

            const cursor = find(w.contentItem, o => o instanceof BrushCursor)
            Tools.setValues({ "brush:roundness": 50, "brush:angle": 30 })
            compare([cursor.roundness, cursor.tipAngle, cursor.square], [1, 0, false], "the core's tip: round")
            const pbd = fixtures.folder() + "/vp.pbd"
            compare(dc.save(pbd), "")
            Docs.select(0)
            verify(!Tools.real)
            compare(cursor.roundness, 0.5, "a mock document's: the tip set")

            let previewed = 0  // the frame the .pbd's composite was up in
            const seen = () => { previewed = previewed || (dc.shown.preview ? dc.frames : 0) }
            dc.framed.connect(seen)
            const e = opened(pbd)
            compare(Docs.current, e)
            compare(e.history.map(h => h.name), ["Mở"])
            compare(e.layers.map(l => [l.props.name, l.props.background, l.props.lockAll]), [["Background", true, true]], "as saved")
            verify(dc.model === e.model)
            tryVerify(() => previewed && dc.frames > previewed && !dc.shown.pending && !dc.shown.preview, 10000, "its composite up first, then its frame")
            dc.framed.disconnect(seen)
            compare(dc.composite(0, 200, 150), blue, "its pixels")
        }

        // ---- 1.000 layers ---------------------------------------------------------------------------------------------
        // D4's shape: 1.000 layers, groups 6 deep (bench/docs.cpp), made on a document not shown; then a row a step as
        // tst_u9 scrolls, nine steps in ten within a 60 Hz frame (the best of three tries of 60 steps), with no allowance
        // for a busy machine: the panel and its rows are found before the steps, as walking Main's items (find()) took
        // most of a step. Only the rows in view made, their thumbnails from image://pb.
        function test_thousand() {
            const w = open()
            const d = opened(file("nghin.png", "png", 1200, 900))
            Docs.select(0)
            const t0 = Date.now(), count = {}
            let k = 0
            const add = (parent, kind) => {
                const name = (kind === "group" ? "Nhóm " : "Layer ") + ++k
                verify(d.run({ op: "addLayer", parent: parent, index: count[parent] || 0, kind: kind, props: { name: name, blend: kind === "group" ? "passThrough" : "normal" } }))
                count[parent] = (count[parent] || 0) + 1
                return d.active.id
            }
            count[0] = 1  // the Background
            while (d.layers.length < 1000) {
                let parent = 0
                for (let depth = 0; depth < 6 && d.layers.length < 1000; depth++) {
                    parent = add(parent, "group")
                    for (let i = 0; i < 2 && d.layers.length < 1000; i++)
                        add(parent, "pixel")
                }
                for (let i = 0; i < 8 && d.layers.length < 1000; i++)
                    add(0, "pixel")
            }
            console.log("1.000 layers made on a real document in " + (Date.now() - t0) + " ms")
            compare(Math.max(...d.layers.map(l => l.depth)), 6, "groups 6 deep")
            Docs.select(Docs.docs.indexOf(d))
            waitForRendering(w.contentItem)
            const l = list(w), rowHeight = panel(w).rowHeight
            compare(l.count, 1000)
            let best = Infinity
            for (let t = 0; t < 3 && best >= 16; t++) {
                const steps = []
                l.contentY = 0
                for (let s = 0; s < 60; s++) {
                    const t1 = Date.now()
                    l.contentY += rowHeight
                    l.forceLayout()
                    steps.push(Date.now() - t1)
                    if (s % 10 === 0)
                        waitForRendering(l)
                }
                steps.sort((a, b) => a - b)
                console.log("1.000 real layers: a row a step, median " + steps[30] + " ms, 9 in 10 within " + steps[54] + " ms, worst " + steps[59] + " ms")
                best = Math.min(best, steps[54])
            }
            verify(best < 16, "9 steps in 10 within a frame: " + best + " ms")
            verify(all(panel(w), o => o instanceof LayerRow).length < 40, "only the rows in view")
            tryVerify(() => rows(w).filter(r => r.kind === "pixel").every(r => r.thumb === "photo" && /^image:\/\/pb\/layer\//.test(String(r.src))
                && find(r, o => o instanceof Image && o.visible && o.status === Image.Ready)), 5000, "the thumbnails in view, from image://pb")
            const t1 = Date.now()
            verify(d.run({ op: "setProps", props: { visible: false } }))
            console.log("a command on 1.000 layers: " + (Date.now() - t1) + " ms")
        }
    }
}
