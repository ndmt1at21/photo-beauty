import QtQuick
import QtTest
import PhotoBeauty
import "../qml/Mock.js" as Mock

// U3: the mock documents (Doc, Mock.js), Docs, Tools, Prefs.
Item {
    id: root
    Component { id: docC; Doc {} }

    TestCase {
        name: "U3"

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }

        function rng(seed) {  // mulberry32
            let a = seed >>> 0
            return () => {
                a = (a + 0x6d2b79f5) >>> 0
                let t = a
                t = Math.imul(t ^ (t >>> 15), t | 1)
                t ^= t + Math.imul(t ^ (t >>> 7), t | 61)
                return ((t ^ (t >>> 14)) >>> 0) / 4294967296
            }
        }
        // Any command on d, now and then one it refuses.
        function randomCommand(d, r) {
            const L = d.layers
            const pick = (a) => a[Math.floor(r() * a.length)]
            const any = () => (L.length ? pick(L).id : 1)
            const groups = [0].concat(L.filter((l) => l.kind === "group").map((l) => l.id))
            const ops = [
                () => ({ op: "newLayer" }), () => ({ op: "newGroup" }), () => ({ op: "paste" }),
                () => ({ op: "addLayer", parent: pick(groups), index: Math.floor(r() * 4), kind: pick(["pixel", "group", "adj", "text"]), props: { name: "L" + Math.floor(r() * 100) } }),
                () => ({ op: "deleteLayers", layers: [any()] }), () => ({ op: "deleteLayers" }),
                () => ({ op: "duplicateLayers", layers: [any(), any()] }), () => ({ op: "duplicateLayers" }),
                () => ({ op: "moveLayer", layer: any(), parent: pick(groups), index: Math.floor(r() * 5) }),
                () => ({ op: "groupLayers", layers: [any(), any()] }), () => ({ op: "groupLayers" }),
                () => ({ op: "ungroup", group: pick(groups) }),
                () => ({ op: "setProps", layer: any(), props: pick([{ opacity: Math.round(r() * 100) / 100 }, { visible: r() < 0.5 }, { blend: pick(Mock.BLENDS) }, { name: "n" + Math.floor(r() * 9) },
                    { lockAll: r() < 0.5 }, { clip: r() < 0.5 }, { fill: 0.5 }, { vals: { s: Math.floor(r() * 10) } }, { maskDensity: 0.4 }]) }),
                () => ({ op: "setProps", props: { opacity: 0.3 } }),
                () => ({ op: "selectLayers", layers: [any()] }), () => ({ op: "selectLayers", layers: [any(), any()] }), () => ({ op: "selectLayers", layers: [any()] }),
                () => ({ op: "mask", action: pick(["reveal", "hide", "sel", "selInv", "del", "apply", "off", "inv"]) }),
                () => ({ op: "mask", layer: any(), action: pick(["reveal", "inv", "off"]) }),
                () => ({ op: "arrange", dir: pick(["top", "up", "down", "bottom"]) }),
                () => ({ op: "mergeDown" }), () => ({ op: "mergeVisible" }), () => ({ op: "deleteHidden" }),
                () => (r() < 0.2 ? { op: "flattenImage" } : { op: "newLayer" }),
                () => ({ op: "clippingMask" }), () => ({ op: "toSmartObject" }), () => ({ op: "rasterize" }), () => ({ op: "resetAdjustment" }), () => ({ op: "removeBackground" }),
                () => ({ op: "newAdjustmentLayer", adjustment: pick(Object.keys(Mock.ADJ_ICONS)) }), () => ({ op: "newFillLayer", fill: pick(["Solid Color", "Gradient"]), color: "#123456" }),
                () => ({ op: "newTypeLayer" }), () => ({ op: "newShapeLayer", shape: pick(Object.keys(Mock.SHAPE_TOOLS)) }), () => ({ op: "placeEmbedded" }), () => ({ op: "cutToLayer" }),
                () => ({ op: "rotateLayer", angle: pick([90, 180, 270]) }), () => ({ op: "flipLayer", axis: pick(["h", "v"]) }),
                () => ({ op: "rotateCanvas", angle: pick([90, 180, 270]) }), () => ({ op: "flipCanvas", axis: pick(["h", "v"]) }),
                () => ({ op: "setMode", mode: pick(["RGB", "Gray"]) }), () => ({ op: "setMode", bits: pick([8, 16]) }), () => ({ op: "swapOrientation" }),
                () => ({ op: "crop", w: 100 + Math.floor(r() * 900), h: 100 + Math.floor(r() * 900) }), () => ({ op: "cropToSelection" }),
                () => ({ op: "imageSize", w: 2000, h: 1000, ppi: 240 }), () => ({ op: "canvasSize", w: 3000, h: 3000 }), () => ({ op: "convertProfile", profile: pick(["Display P3", "sRGB IEC61966-2.1"]) }),
                () => ({ op: "select", shape: pick(Object.keys(Mock.SHAPES)) }), () => ({ op: "deselect" }), () => ({ op: "reselect" }), () => ({ op: "inverse" }),
                () => ({ op: "saveSelection" }), () => ({ op: "newChannel" }), () => ({ op: "deleteChannel", channel: pick(d.alphas.concat(["x"])) }),
                () => ({ op: "step", name: "Gaussian Blur" }), () => ({ op: "frequencySeparation" }), () => ({ op: "autoMask", parts: ["skin"], output: Math.floor(r() * 2) }),
                () => ({ op: "smoothSkin" }), () => ({ op: "dodgeBurn", method: Math.floor(r() * 2) }), () => ({ op: "eyesTeeth" })
            ]
            return pick(ops)()
        }
        function content(s) { return JSON.stringify(Object.assign({}, s, { selected: null })) }  // what replay must give back
        // Each History step of d, run again from the first one's state with the commands it keeps, gives the state it
        // holds: the same tree (JSON), the same everything but which layers are selected.
        function checkHistory(d, what) {
            const r = createTemporaryObject(docC, root)
            r.core = Mock.makeDoc({ file: "replay", state: d.history[0].state })
            for (let k = 0; k < d.history.length; k++) {
                const step = d.history[k]
                for (const c of step.cmds)
                    verify(r.run(c) || c.op === "selectLayers", what + ": step " + k + " (" + step.name + ") runs " + JSON.stringify(c) + " again: " + r.core.error)
                compare(JSON.stringify(r.tree), JSON.stringify(step.state.root), what + ": step " + k + " (" + step.name + "), its tree")
                compare(content(r.state), content(step.state), what + ": step " + k + " (" + step.name + ")")
                Mock.validate(step.state)
            }
            verify(d.state === d.history[d.index].state && d.tree === d.state.root, what + ": the document shows the step History is on")
        }

        function test_random_history() {  // 1.000 commands with undo, redo, jumps, gestures between them
            const d = Docs.current
            const r = rng(3)
            let ran = 0, most = 0
            for (let i = 1; i <= 1000; i++) {
                const x = r()
                if (x < 0.62) {
                    if (d.run(randomCommand(d, r)))
                        ran++
                } else if (x < 0.72) {
                    d.undo()
                } else if (x < 0.8) {
                    d.redo()
                } else if (x < 0.87) {
                    d.jumpTo(Math.floor(r() * d.history.length))
                } else if (x < 0.9) {
                    d.toggleLast()
                } else if (x < 0.96) {
                    d.beginGesture()
                    const id = d.active ? d.active.id : 1
                    for (let k = 0; k < 3; k++)
                        d.run({ op: "setProps", layer: id, props: { opacity: Math.round(r() * 100) / 100 } })
                    d.endGesture()
                } else if (x < 0.97) {
                    d.deleteStep()
                } else {
                    d.run({ op: "selectLayers", layers: [] })
                }
                most = Math.max(most, d.history.length)
                if (i % 50 === 0)
                    checkHistory(d, "after " + i)
            }
            verify(ran > 400, ran + " commands ran")
            verify(most > 20, "History reached " + most + " steps")
        }

        function test_drag_is_one_step() {  // opacity through 100 values
            const d = Docs.current, id = d.active.id, n = d.history.length
            d.beginGesture()
            for (let v = 99; v >= 0; v--)
                verify(d.run({ op: "setProps", layer: id, props: { opacity: v / 100 } }))
            d.endGesture()
            compare(d.history.length, n + 1)
            compare([d.history[d.index].name, d.history[d.index].icon, d.history[d.index].cmds.length], ["Opacity", "opacity", 100])
            compare(d.layer(id).props.opacity, 0)
            d.undo()
            compare(d.layer(id).props.opacity, 1)
            d.redo()
            compare(d.layer(id).props.opacity, 0)
            verify(d.run({ op: "setProps", layer: id, props: { opacity: 0.5 } }))
            compare(d.history.length, n + 2, "after the gesture, a step of its own")
            d.beginGesture()  // a gesture that changes nothing makes no step
            verify(!d.run({ op: "setProps", layer: id, props: { opacity: 0.5 } }))
            d.endGesture()
            compare(d.history.length, n + 2)
            d.beginGesture()  // an undo ends the gesture
            verify(d.run({ op: "setProps", layer: id, props: { opacity: 0.2 } }))
            d.undo()
            verify(d.run({ op: "setProps", layer: id, props: { opacity: 0.3 } }))
            compare(d.history.length, n + 3)
            compare(d.history[d.index].cmds.length, 1)
        }

        function test_can() {  // as REQ and cx: without layers, with a selection, with a mask, nothing open
            const expect = (d, cx, what) => {
                for (const k of Mock.CX)
                    compare(d ? d.can(k) : Docs.can(k), !!cx[k], what + ": " + k)
                for (const a in Mock.REQ)
                    compare(d ? d.can(a) : Docs.can(a), !!cx[Mock.REQ[a]], what + ": " + a)
                compare(Docs.can("@merge:down"), !!cx.multi, what + ": @merge:down")
                compare(Docs.can("!bgl"), !cx.bgl, what + ": !bgl")
                verify(Docs.can("@noop") && Docs.can("win"), what + ": what has no requirement")
            }
            const e = Docs.create({ name: "trống", w: 800, h: 600, ppi: 72, mode: "RGB", bits: 8, background: "white" })
            compare(Docs.current, e)
            verify(e.run({ op: "deleteLayers", layers: [e.layers[0].id] }))
            compare(e.layers.length, 0)
            expect(e, { doc: true, undo: true, hist: true }, "no layers")
            Docs.select(0)
            const p = Docs.current
            verify(p.run({ op: "select", shape: "rect", name: "Rectangular Marquee" }))
            expect(p, { doc: true, sel: true, undo: true, hist: true, layer: true, multi: true, below: true, faces: true }, "a selection")
            verify(p.run({ op: "selectLayers", layers: [p.layers[0].id] }))  // Curves 1
            verify(p.run({ op: "deselect" }))
            expect(p, { doc: true, prev: true, undo: true, hist: true, layer: true, multi: true, mask: true, below: true, faces: true }, "a mask")
            p.undo()
            verify(p.run({ op: "selectLayers", layers: [p.layers[8].id] }))  // Background
            verify(p.run({ op: "setProps", layer: p.layers[4].id, props: { visible: false } }))  // Tách tần số
            expect(p, { doc: true, sel: true, undo: true, redo: false, hist: true, layer: true, multi: true, hidden: true, bgl: true, faces: true }, "the Background, a layer hidden")
            p.undo()
            expect(p, { doc: true, sel: true, undo: true, redo: true, hist: true, layer: true, multi: true, bgl: true, faces: true }, "undone")
            verify(p.run({ op: "selectLayers", layers: [p.layers[4].id] }))
            compare([p.can("grp"), p.can("ungroup"), p.can("text"), p.can("so")], [true, true, false, false])
            Docs.closeAll()
            expect(null, {}, "nothing open")
        }

        function test_close_duplicate() {
            compare(Docs.docs.map((d) => d.file), ["chân dung.psd", "san-pham.webp", "hoang-hon.tif"])
            const first = Docs.docs[0], second = Docs.docs[1]
            compare(Docs.current, first)
            Docs.select(1)
            Docs.close(first)
            compare([Docs.current, Docs.index, Docs.docs.length], [second, 0, 2], "the tab before the current one")
            Docs.reset()
            const a = Docs.docs[0], b = Docs.docs[1], c = Docs.docs[2]
            Docs.select(2)
            compare([Docs.current, Docs.index], [c, 2])
            Tools.xf = true
            Docs.close(b)
            compare(Docs.docs, [a, c])
            compare([Docs.current, Docs.index, Tools.xf], [c, 1, false])
            Docs.close(c)
            compare([Docs.current, Docs.index], [a, 0])
            verify(a.run({ op: "newLayer" }))
            const d = Docs.duplicate(a)
            compare(Docs.current, d)
            compare([d.file, d.dirty, d.history.length, d.history[0].name, d.snapshots[0].name], ["chân dung bản sao.psd", true, 1, "Nhân bản", "Bản sao"])
            compare(JSON.stringify(d.tree), JSON.stringify(a.tree))
            compare([d.w, d.h, d.bits, d.faces, d.photo], [4400, 5600, 16, true, 0])
            verify(d.run({ op: "newLayer" }))
            compare([a.layers.length, d.layers.length], [10, 11], "each its own History")
            verify(Docs.exec("@dupDoc"))
            compare(Docs.current.file, "chân dung bản sao bản sao.psd")
            Docs.closeAll()
            compare([Docs.docs.length, Docs.current], [0, null])
            verify(!Docs.exec("@close") && !Docs.exec("@undo"), "nothing to act on")
            const o = Docs.openRecent(1)  // poster-tet.pbd
            compare([o.file, o.w, o.h, o.ppi, o.bits, o.photo, o.dirty, o.layers.length, o.layers[0].props.name], ["poster-tet.pbd", 2480, 3508, 300, 8, 3, false, 1, "Background"])
            compare(o.view.zoom, Mock.fitZoom(2480, 3508, 1034, 750) / 100)
            compare(Docs.open("poster-tet.pbd"), o, "open already: shown")
            compare(Docs.open("chân dung.psd").layers.length, 9, "the portrait")
            const dropped = Docs.open("ảnh thả vào.jpg")
            compare([dropped.file, Docs.docs.length, Docs.current], ["ảnh thả vào.jpg", 3, dropped])
            Docs.forgetRecent("poster-tet.pbd")
            compare(Docs.recent.length, 7)
            Docs.clearRecent()
            compare(Docs.recent.length, 0)
            compare(Docs.recovery.map((r) => r.file), ["chân dung-final.psd", "Chưa đặt tên-3"])
            const rec = Docs.recover(0)
            compare([rec.file, rec.dirty, Docs.recovery.length], ["chân dung-final.psd", true, 1])
            Docs.dropRecovery(0)
            compare(Docs.recovery.length, 0)
        }

        function test_create() {  // as the New dialog asks
            Tools.bg = "#123456"
            const n = Docs.create({ name: "In", w: 2480, h: 3508, ppi: 300, mode: "Gray", bits: 16, background: "background", profile: "Dot Gain 20%" })
            compare([n.file, n.w, n.h, n.ppi, n.mode, n.bits, n.profile, n.dirty, n.photo], ["In", 2480, 3508, 300, "Gray", 16, "Dot Gain 20%", false, null])
            compare([n.layers.length, n.layers[0].props.name, n.layers[0].props.background, n.layers[0].props.color], [1, "Background", true, "#123456"])
            compare([n.history.length, n.history[0].name, n.mem], [1, "Tạo mới", "17,4 MB / 17,4 MB"])
            const t = Docs.create({ name: "Trong", w: 100, h: 50, background: "transparent" })
            compare([t.layers[0].props.name, t.layers[0].props.background, t.mode, t.bits, t.profile], ["Layer 1", false, "RGB", 8, "sRGB IEC61966-2.1"])
            ignoreWarning(/Docs.create: pb: no size 0 × 10/)
            compare(Docs.create({ name: "x", w: 0, h: 10 }), null, "no size")
            compare(Docs.docs.length, 5)
        }

        function test_job() {  // as the prototype opens: Đang lưu chân dung.psd 64 %
            const p = Docs.docs[0], h = Docs.docs[2]
            compare([Docs.job.on, Docs.job.run, Docs.job.p, Docs.job.label], [true, true, 64, "Đang lưu chân dung.psd"])
            verify(p.dirty && h.dirty && !Docs.docs[1].dirty)
            for (let i = 0; i < 40 && Docs.job.run; i++)
                Docs.tick()
            compare([Docs.job.p, Docs.job.done, Docs.job.label, p.dirty], [100, true, "Đã lưu chân dung.psd", false])
            for (let i = 0; i < 11; i++)
                Docs.tick()
            verify(!Docs.job.on, "shown done for 10 ticks")
            Docs.select(2)
            verify(Docs.exec("@save"))
            compare([Docs.job.name, Docs.job.p, Docs.job.sp], ["hoang-hon.tif", 0, 2])
            for (let i = 0; i < 60 && Docs.job.run; i++)
                Docs.tick()
            verify(!h.dirty)
            verify(h.run({ op: "newLayer" }))
            verify(h.dirty)
            Docs.save(h)
            Docs.tick()
            Docs.cancelJob()
            verify(!Docs.job.on && !Docs.job.run)
            for (let i = 0; i < 60; i++)
                Docs.tick()
            verify(h.dirty, "cancelled: still unsaved")
            verify(Docs.exec("@quickExport"))
            compare(Docs.job.label, "Đang xuất hoang-hon.png")
            for (let i = 0; i < 30 && Docs.job.run; i++)
                Docs.tick()
            compare(Docs.job.label, "Đã xuất hoang-hon.png")
            verify(h.dirty, "an export leaves it unsaved")
        }

        function test_layers() {  // T1's commands, by its JSON
            const d = Docs.docs[1]
            const bg = d.layers[0].id
            const names = () => d.layers.map((l) => l.props.name + ":" + l.depth)
            verify(d.run({ op: "addLayer", parent: 0, index: 1, kind: "pixel", props: { name: "A" } }))
            const A = d.active.id
            compare([A, d.history[d.index].name, d.history[d.index].icon], [2, "Layer mới", "add_box"])
            verify(d.run({ op: "addLayer", parent: 0, index: 2, kind: "group", props: { name: "G", blend: "passThrough" } }))
            const G = d.active.id
            compare(d.history[d.index].name, "Nhóm mới")
            verify(!d.run({ op: "addLayer", parent: 0, index: 9, kind: "pixel", props: {} }), "no index 9")
            verify(!d.run({ op: "addLayer", parent: A, index: 0, kind: "pixel", props: {} }), "a pixel layer holds none")
            verify(!d.run({ op: "setProps", layer: A, props: { blend: "passThrough" } }), "Pass Through is for groups")
            verify(!d.run({ op: "setProps", layer: A, props: { opacity: 2 } }), "opacity 0..1")
            verify(d.run({ op: "moveLayer", layer: A, parent: G, index: 0 }))
            compare(names(), ["G:0", "A:1", "Background:0"])
            compare(d.history[d.index].name, "Sắp xếp layer")
            verify(!d.run({ op: "moveLayer", layer: G, parent: G, index: 0 }), "a group into itself")
            verify(!d.run({ op: "moveLayer", layer: bg, parent: 0, index: 1 }), "the Background stays")
            verify(!d.run({ op: "moveLayer", layer: G, parent: 0, index: 0 }), "nothing below the Background")
            verify(!d.run({ op: "moveLayer", layer: G, parent: 0, index: 1 }), "where it is")
            verify(!d.run({ op: "moveLayer", layer: A, parent: G, index: 1 }), "the index counts without the layer")
            verify(d.run({ op: "duplicateLayers", layers: [A, G] }))
            compare(names(), ["G bản sao:0", "A:1", "G:0", "A:1", "Background:0"], "A goes along with G")
            compare([d.layers[0].id, d.layers[1].id, d.selected], [4, 5, [4]], "new ids, each before what it holds")
            compare(d.history[d.index].name, "Nhân bản layer")
            verify(d.run({ op: "duplicateLayers", layers: [bg] }))
            compare([d.active.props.name, d.active.props.background, d.active.props.lockAll], ["Background bản sao", false, false])
            verify(d.run({ op: "deleteLayers", layers: [d.active.id] }))
            verify(d.run({ op: "deleteLayers", layers: [4] }))
            compare(names(), ["G:0", "A:1", "Background:0"])
            compare(d.selected, [G], "the layer that took its place")
            compare(d.history[d.index].name, "Xóa layer")
            verify(d.run({ op: "groupLayers", layers: [A], props: { name: "H", blend: "passThrough" } }))
            compare(names(), ["G:0", "H:1", "A:2", "Background:0"])
            compare(d.history[d.index].name, "Nhóm layer")
            const H = d.active.id
            verify(d.run({ op: "ungroup", group: G }))
            compare(names(), ["H:0", "A:1", "Background:0"])
            compare([d.selected, d.history[d.index].name], [[H], "Bỏ nhóm"])
            const n = d.history.length
            verify(d.run({ op: "selectLayers", layers: [A, bg, A] }))
            compare([d.selected, d.active.id, d.history.length], [[A, bg], bg, n], "no step of its own")
            verify(!d.run({ op: "selectLayers", layers: [99] }))
            verify(!d.run({ op: "groupLayers" }), "the Background stays out of groups")
            compare(names(), ["H:0", "A:1", "Background:0"])
            verify(d.run({ op: "selectLayers", layers: [A] }) && d.run({ op: "groupLayers" }))
            compare(names(), ["H:0", "Group 1:1", "A:2", "Background:0"])
            verify(d.run({ op: "newLayer" }))
            compare(names(), ["H:0", "Layer 1:1", "Group 1:1", "A:2", "Background:0"], "above the active one, in its group")
            verify(d.run({ op: "setProps", props: { name: "B" } }) && d.history[d.index].name === "Đổi tên layer")
            verify(d.run({ op: "setProps", props: { visible: false } }) && d.history[d.index].name === "Ẩn layer")
            verify(d.run({ op: "setProps", props: { visible: true } }) && d.history[d.index].name === "Hiện layer")
            verify(d.run({ op: "setProps", props: { blend: "multiply" } }) && d.history[d.index].name === "Blend mode")
            verify(d.run({ op: "setProps", props: { fill: 0.5 } }) && d.history[d.index].name === "Fill")
            verify(d.run({ op: "setProps", props: { lockPixels: true } }) && d.history[d.index].name === "Khóa layer")
            verify(!d.run({ op: "setProps", props: { lockPixels: true } }), "no change, no step")
            compare(d.active.props.blend, "multiply")
            d.toggleOpen(H)
            compare(d.rows.map((l) => l.props.name), ["H", "Background"], "a collapsed group's rows")
            compare(d.layers.length, 5)
            d.toggleOpen(H)
            compare(d.rows.length, 5)
        }

        function test_history() {
            const d = Docs.docs[1]
            compare([d.history.length, d.index, d.history[0].name, d.history[0].icon], [1, 0, "Mở", "folder_open"])
            verify(d.run({ op: "newLayer" }) && d.run({ op: "newLayer" }) && d.run({ op: "newLayer" }))
            compare([d.history.length, d.index, d.layers.length], [4, 3, 4])
            d.toggleLast()
            compare([d.index, d.layers.length], [2, 3])
            d.toggleLast()
            compare(d.index, 3)
            d.jumpTo(1)
            compare([d.index, d.layers.length, d.can("redo")], [1, 2, true])
            d.newSnapshot()
            compare(d.snapshots.map((s) => s.name + "@" + s.at), ["san-pham.webp@0", "Snapshot 1@1"])
            d.redo()
            d.jumpToSnapshot(1)
            compare([d.snapshot, d.index, d.layers.length], [1, 1, 2])
            d.revert()
            compare([d.index, d.layers.length, d.snapshot], [0, 1, -1])
            d.jumpTo(3)
            d.jumpTo(2)
            d.deleteStep()
            compare([d.history.length, d.index, d.layers.length], [2, 1, 2])
            verify(d.run({ op: "newLayer" }))
            compare([d.history.length, d.layers[0].props.name], [3, "Layer 2"], "after a jump, the steps after it go")
            const p = Docs.docs[0]
            compare(p.history.map((h) => h.name), ["Mở", "Tách tần số", "Spot Healing Brush", "Spot Healing Brush", "Liquify", "Layer Curves mới", "Brush", "Brush", "Brush", "Brush"])
            compare([p.index, p.snapshots.length, p.snapshots[1].name, p.snapshots[1].at], [9, 2, "Trước khi Liquify", 3])
            p.jumpToSnapshot(1)
            compare([p.index, p.snapshot], [3, 1])
            compare(p.layers.length, 9)
        }

        function test_portrait() {  // chân dung.psd as the prototype has it
            const p = Docs.current
            compare([p.file, p.w, p.h, p.ppi, p.bits, p.mode, p.faces, p.dirty, p.mem, p.view.zoom], ["chân dung.psd", 4400, 5600, 300, 16, "RGB", true, true, "148 MB / 1,02 GB", 0.125])
            compare(p.layers.map((l) => l.props.name + ":" + l.kind + ":" + l.depth), ["Curves 1:adj:0", "Làm trắng răng:adj:0", "Làm sáng mắt:adj:0", "Dodge & Burn:pixel:0",
                "Tách tần số:group:0", "Cao:pixel:1", "Thấp:pixel:1", "Heal:pixel:0", "Background:pixel:0"])
            compare([p.active.props.name, p.alphas, p.selection], ["Dodge & Burn", ["Vùng chọn da"], null])
            const props = (i) => p.layers[i].props
            compare([props(3).blend, props(4).blend, props(5).blend, props(8).background, props(8).lockAll, props(8).photo], ["softLight", "passThrough", "linearLight", true, true, 0])
            compare([props(0).adj, props(0).mask, props(0).vals.pre], ["Curves", "#ffffff", "Medium Contrast (RGB)"])
            compare(Mock.photoUrl(p.photo), "qrc:/qt/qml/PhotoBeauty/mock/photos/chandung2.jpg")
            Docs.reset("Retouch")
            Tools.reset("Retouch")
            const r = Docs.current
            compare([r.active.props.name, r.rows.length, Docs.job.on], ["Heal", 7, false])
            compare([Tools.tool, Tools.dockTab.p, Tools.rulers, Tools.dockMin.c, Tools.panelVisible("Retouch")], ["spot", "Retouch", false, true, true])
            Docs.reset("Start")
            compare([Docs.docs.length, Docs.current], [0, null])
        }

        function test_masks_and_kinds() {  // P1 layer commands
            const p = Docs.current
            const c1 = p.layers[0].id, db = p.active.id
            verify(!p.run({ op: "mask", action: "del" }), "Dodge & Burn has no mask")
            verify(p.run({ op: "mask", action: "reveal" }))
            compare([p.active.props.mask, p.history[p.index].name, p.history[p.index].icon], ["#ffffff", "Thêm Layer Mask", "mask"])
            verify(p.run({ op: "mask", action: "inv" }) && p.active.props.mask === "#000000")
            verify(p.run({ op: "mask", action: "off" }) && p.active.props.maskOff)
            p.setEditMask(true)
            verify(p.editMask)
            verify(p.run({ op: "mask", action: "apply" }))
            compare([p.active.props.mask, p.active.props.maskOff, p.editMask, p.history[p.index].name], [null, false, false, "Áp Layer Mask"])
            p.setEditMask(true)
            verify(!p.editMask, "no mask to edit")
            verify(p.run({ op: "selectLayers", layers: [c1] }))
            p.setEditMask(true)
            verify(p.editMask)
            verify(p.run({ op: "selectLayers", layers: [db] }))
            verify(!p.editMask, "another layer")
            verify(p.run({ op: "clippingMask", layer: c1 }) && p.layer(c1).props.clip && p.history[p.index].name === "Clipping Mask")
            verify(p.run({ op: "setProps", layer: c1, props: { vals: { pre: "Lighter (RGB)" } } }))
            compare([p.history[p.index].name, p.history[p.index].icon], ["Sửa Curves 1", "curve"])
            verify(p.run({ op: "resetAdjustment", layer: c1 }) && p.history[p.index].name === "Đặt lại Curves 1")
            compare(p.layer(c1).props.vals, {})
            verify(!p.run({ op: "rasterize" }), "a pixel layer")
            verify(p.run({ op: "toSmartObject" }) && p.active.kind === "so" && p.can("so"))
            verify(p.run({ op: "rasterize" }) && p.active.kind === "pixel" && p.active.props.thumb === "gray")
            const fs = p.layers[4].id
            verify(p.run({ op: "toSmartObject", layer: fs }))
            compare([p.layer(fs).kind, p.layer(fs).props.blend, p.layer(fs).props.photo, p.layers.length], ["so", "normal", 0, 7], "a group's layers go inside it")
            verify(p.run({ op: "removeBackground" }))
            compare(p.layers[p.layers.length - 1].props.mask.indexOf("radial-gradient"), 0)
            verify(p.run({ op: "newAdjustmentLayer", adjustment: "Hue/Saturation" }))
            compare([p.active.props.name, p.active.props.mask, p.history[p.index].name, p.history[p.index].icon], ["Hue/Saturation 1", "#ffffff", "Layer Hue/Saturation mới", "looks"])
            verify(p.run({ op: "newFillLayer", fill: "Gradient", color: "linear-gradient(90deg, #000000, #ffffff)" }))
            compare([p.active.kind, p.active.props.name, p.history[p.index].name], ["fill", "Gradient Fill 1", "Layer Gradient mới"])
            verify(p.run({ op: "newShapeLayer", shape: "ellipS" }))
            compare([p.active.kind, p.active.props.name, p.history[p.index].name, p.history[p.index].icon], ["shape", "Ellipse 1", "Ellipse", "circle"])
            verify(p.run({ op: "newTypeLayer" }) && p.active.kind === "text" && p.can("text"))
            verify(p.run({ op: "placeEmbedded" }) && p.active.kind === "so" && p.active.props.photo === 7)
            verify(!p.run({ op: "cutToLayer" }), "no selection to cut")
        }

        function test_merge_arrange() {
            const p = Docs.current
            const below = p.layers[4].id  // Tách tần số, under Dodge & Burn
            verify(p.run({ op: "mergeDown" }))
            compare([p.layers.length, p.active.id, p.history[p.index].name], [8, below, "Gộp xuống"])
            verify(!p.run({ op: "mergeDown" }), "a group")
            verify(p.run({ op: "arrange", dir: "top" }))
            compare([p.layers[0].id, p.history[p.index].name], [below, "Sắp xếp layer"])
            verify(!p.run({ op: "arrange", dir: "up" }), "at the top")
            verify(p.run({ op: "arrange", dir: "bottom" }))
            compare(p.layers.map((l) => l.props.name).slice(-4), ["Tách tần số", "Cao", "Thấp", "Background"], "above the Background")
            verify(p.run({ op: "setProps", layer: p.layers[0].id, props: { visible: false } }))
            verify(p.run({ op: "mergeVisible" }))
            compare(p.layers.map((l) => l.props.name + ":" + l.props.visible), ["Curves 1:false", "Background:true"], "hidden ones stay")
            compare([p.active.props.background, p.history[p.index].name], [true, "Gộp layer đang hiện"])
            verify(p.run({ op: "deleteHidden" }) && p.layers.length === 1 && p.history[p.index].name === "Xóa layer đang ẩn")
            verify(!p.run({ op: "deleteHidden" }), "none hidden")
            verify(p.run({ op: "newLayer" }) && p.run({ op: "flattenImage" }))
            compare([p.layers.length, p.layers[0].props.name, p.layers[0].props.background, p.history[p.index].name], [1, "Background", true, "Làm phẳng ảnh"])
        }

        function test_document_commands() {  // Image, Select, Channels, transforms
            const d = Docs.docs[1]  // 1800 × 1200
            verify(d.run({ op: "rotateCanvas", angle: 90 }))
            compare([d.w, d.h, d.rot, d.history[d.index].name], [1200, 1800, 90, "Image Rotation"])
            verify(d.run({ op: "flipCanvas", axis: "h" }) && d.flipH && d.history[d.index].name === "Lật canvas ngang")
            verify(d.run({ op: "rotateLayer", angle: 180 }) && d.layerRot === 180 && d.history[d.index].name === "Xoay 180°")
            verify(d.run({ op: "flipLayer", axis: "v" }) && d.layerFlipV && d.history[d.index].name === "Lật dọc")
            verify(d.run({ op: "setMode", mode: "Gray" }) && d.mode === "Gray" && d.history[d.index].name === "Grayscale")
            verify(d.run({ op: "setMode", bits: 16 }) && d.bits === 16 && d.history[d.index].name === "16 Bits/Channel")
            verify(d.run({ op: "setMode", mode: "RGB", bits: 8 }) && d.history[d.index].name === "Mode")
            verify(!d.run({ op: "setMode", mode: "CMYK" }))
            verify(d.run({ op: "swapOrientation" }) && d.w === 1800)
            verify(d.run({ op: "imageSize", w: 900, h: 600, ppi: 144 }))
            compare([d.w, d.h, d.ppi, d.history[d.index].name], [900, 600, 144, "Image Size"])
            verify(d.run({ op: "canvasSize", w: 1000, h: 700 }) && d.history[d.index].name === "Canvas Size")
            verify(d.run({ op: "convertProfile", profile: "Display P3" }) && d.profile === "Display P3")
            verify(!d.run({ op: "cropToSelection" }), "no selection")
            verify(d.run({ op: "select", shape: "lasso", name: "Lasso" }) && d.selection === "lasso" && d.history[d.index].name === "Lasso")
            verify(d.run({ op: "inverse" }) && d.inverse)
            verify(d.run({ op: "select", shape: "all" }) && !d.inverse && d.history[d.index].name === "Chọn tất cả")
            verify(d.run({ op: "deselect" }) && d.selection === null && d.prevSelection === "all")
            verify(d.run({ op: "reselect" }) && d.selection === "all")
            verify(d.run({ op: "saveSelection" }))
            compare([d.alphas, d.history[d.index].name], [["Alpha 1"], "Lưu vùng chọn"])
            verify(d.run({ op: "newChannel" }) && d.alphas.length === 2)
            d.setChannel("Alpha 2")
            compare(d.channel, "Alpha 2")
            verify(d.run({ op: "deleteChannel", channel: "Alpha 2" }))
            compare([d.alphas, d.channel, d.history[d.index].name], [["Alpha 1"], "RGB", "Xóa kênh"])
            d.setChannel("Red")
            verify(d.run({ op: "setMode", mode: "Gray" }))
            compare(d.channel, "RGB", "no Red in Grayscale")
            d.toggleChannel("Alpha 1")
            compare(d.hiddenChannels, { "Alpha 1": true })
            verify(d.run({ op: "cropToSelection" }))
            compare([d.w, d.h, d.selection], [375, 322, null])
            verify(d.run({ op: "crop", w: 300, h: 200 }) && d.w === 300 && d.history[d.index].name === "Crop")
            verify(!d.run({ op: "crop", w: 0, h: 200 }))
            verify(d.run({ op: "step", name: "Gaussian Blur" }))
            compare([d.history[d.index].name, d.history[d.index].icon], ["Gaussian Blur", "filter"])
            verify(!d.run({ op: "step" }), "a step needs a name")
            verify(!d.run({ op: "nothing" }))
            const z = d.view
            d.setView({ zoom: 2 })
            compare([d.view.zoom, d.view.x, d.history.length > 0], [2, z.x, true])
        }

        function test_retouch() {  // one step per button
            const p = Docs.current
            const n = p.history.length
            verify(p.run({ op: "frequencySeparation" }))
            compare([p.history.length, p.history[p.index].name, p.layers.length], [n + 1, "Tách tần số", 9], "Tạo lại: the group is there")
            const d = Docs.docs[1]
            verify(d.run({ op: "frequencySeparation" }))
            compare(d.layers.map((l) => l.props.name + ":" + l.props.blend), ["Tách tần số:passThrough", "Cao:linearLight", "Thấp:normal", "Background:normal"])
            compare([d.active.props.name, d.history.length, d.layers[1].props.photo], ["Cao", 2, 4])
            verify(!d.run({ op: "autoMask", parts: ["skin"], output: 0 }), "no face")
            verify(p.run({ op: "dodgeBurn", method: 0 }))
            compare(p.layers.slice(3, 5).map((l) => l.props.name + ":" + l.props.mask), ["Dodge:#000000", "Burn:#000000"])
            compare([p.history.length, p.history[p.index].name, p.history[p.index].cmds.length], [n + 2, "Dodge & Burn", 2])
            verify(p.run({ op: "dodgeBurn", method: 1 }))
            compare([p.active.props.name, p.active.props.blend, p.active.props.thumb], ["Dodge & Burn 1", "softLight", "gray"])
            verify(p.run({ op: "smoothSkin" }) && p.active.props.name === "Làm mịn da 1" && p.history[p.index].name === "Làm mịn da")
            verify(p.run({ op: "eyesTeeth" }) && p.active.props.name === "Làm sáng mắt 1" && p.history[p.index].name === "Mắt, răng")
            verify(p.run({ op: "autoMask", parts: ["eyes", "lips"], output: 0 }))
            compare([p.selection, p.history[p.index].name], ["eyes", "Mask tự động"])
            verify(p.run({ op: "autoMask", parts: ["skin"], output: 1 }))
            compare([p.active.props.mask.indexOf("radial-gradient"), p.history[p.index].name], [0, "Mask từ vùng chọn"])
            verify(!p.run({ op: "autoMask", parts: [], output: 0 }))
        }

        function test_exec() {  // the menus' actions
            const p = Docs.current
            const last = () => p.history[p.index].name
            verify(Docs.exec("@newLayer") && last() === "Layer mới")
            verify(Docs.exec("@undo") && Docs.exec("@redo") && last() === "Layer mới")
            verify(Docs.exec("@adj:Curves") && last() === "Layer Curves mới")
            compare(Tools.dockTab.p, "Properties")
            Tools.fg = "#abcdef"
            verify(Docs.exec("@fill:Solid Color") && p.active.props.color === "#abcdef")
            Tools.pickGradient(2, 1)
            verify(Docs.exec("@fill:Gradient") && p.active.props.color === Tools.gradientCss())
            verify(Docs.exec("@mask:hide") && p.active.props.mask === "#000000")
            verify(Docs.exec("@hideLayer") && !p.active.props.visible && last() === "Ẩn layer")
            verify(Docs.exec("@lockAll") && p.active.props.lockAll && last() === "Khóa layer")
            verify(Docs.exec("@dupLayer") && p.active.props.name === "Gradient Fill 1 bản sao")
            verify(Docs.exec("@groupLayer") && p.active.kind === "group" && p.can("ungroup"))
            verify(Docs.exec("@ungroup") && last() === "Bỏ nhóm")
            verify(Docs.exec("@delLayer") && last() === "Xóa layer")
            verify(Docs.exec("@arr:bottom") && last() === "Sắp xếp layer")
            verify(Docs.exec("@selAll") && p.selection === "all")
            verify(Docs.exec("@inverse") && p.inverse)
            verify(Docs.exec("@deselect") && Docs.exec("@reselect") && p.selection === "all")
            verify(Docs.exec("@saveSel") && p.alphas.length === 2)
            verify(Docs.exec("@ants:teeth") && p.selection === "teeth" && last() === "Chọn răng")
            verify(Docs.exec("@loadSel") && last() === "Nạp vùng chọn")
            verify(Docs.exec("@alignSel") && last() === "Căn theo vùng chọn")
            verify(Docs.exec("@cropSel") && p.w === 1650)
            verify(Docs.exec("@crot:90") && p.w === 2576)
            verify(Docs.exec("@cflip:v") && p.flipV)
            verify(Docs.exec("@rot:90") && p.layerRot === 90 && last() === "Xoay 90°")
            verify(Docs.exec("@flip:h") && p.layerFlipH)
            verify(Docs.exec("@mode:Gray") && Docs.exec("@bits:8") && p.mode === "Gray" && p.bits === 8)
            verify(Docs.exec("@invert") && last() === "Invert")
            verify(Docs.exec("@step:Auto Tone") && last() === "Auto Tone")
            verify(!Docs.exec("@alignSel"), "no selection after Crop")
            verify(Docs.exec("@xf") && Tools.xf && Docs.exec("@warp") && Tools.warp)
            verify(Docs.exec("@xfApply") && last() === "Warp" && !Tools.xf)
            verify(Docs.exec("@warpOn") && Docs.exec("@xfCancel") && !Tools.xf && !Tools.warp)
            verify(Docs.exec("@paste") && last() === "Dán")
            verify(Docs.exec("@place") && last() === "Place Embedded")
            verify(!Docs.exec("@cutLayer") && Docs.exec("@selAll") && Docs.exec("@cutLayer") && last() === "Cắt sang layer mới")
            verify(Docs.exec("@newGroup") && last() === "Nhóm mới")
            verify(Docs.exec("@clip") && p.active.props.clip)
            verify(Docs.exec("@toSO") && p.active.kind === "so")
            verify(Docs.exec("@rast") && p.active.kind === "pixel")
            const n = p.layers.length
            verify(Docs.exec("@merge:down") && p.layers.length === n - 1)
            verify(Docs.exec("@merge:visible") && Docs.exec("@merge:flat") && p.layers.length === 1)
            verify(!Docs.exec("@delLayer"), "the last layer")
            verify(!Docs.exec("@ungroup") && !Docs.exec("@noop") && !Docs.exec("=NewDoc") && !Docs.exec("@dlg:Curves"), "not run here")
            Tools.setValue("crop:2", "1:1 (Square)")
            verify(Docs.exec("@cropApply") && p.w === p.h)
            verify(Docs.exec("@cropSwap") && Tools.value("crop:3") === "5" && Docs.exec("@cropClear") && Tools.value("crop:2") === "Ratio")
            verify(Docs.exec("@cropCancel") && Tools.tool === "move")
            p.zoomTo(12.5)
            verify(Docs.exec("@zoomIn"))
            fuzzyCompare(p.view.zoom, 0.1667, 1e-9, "the next of ZOOMS")
            verify(Docs.exec("@zoomOut") && Docs.exec("@zoomOut"))
            fuzzyCompare(p.view.zoom, 0.0833, 1e-9)
            verify(Docs.exec("@zoom100") && p.view.zoom === 1)
            verify(Docs.exec("@zoomFit"))
            fuzzyCompare(p.view.zoom, Mock.fitZoom(p.w, p.h, 1034, 750) / 100, 1e-9)
            verify(Docs.exec("@zoomFill"))
            fuzzyCompare(p.view.zoom, Math.round(Math.max(1034 / p.w, 750 / p.h) * 10000) / 10000, 1e-9)
            verify(Docs.exec("@tog:rulers") && !Tools.rulers && Docs.exec("@scr:2") && Tools.screen === 2)
            verify(Docs.exec("@snapTo:grid") && !Tools.snapTo.grid && Docs.exec("@snapTo:none") && !Tools.snapTo.guides && Docs.exec("@snapTo:all") && Tools.snapTo.layers)
            verify(Docs.exec("@newGuide") && Tools.guides && Docs.exec("@clearGuides") && !Tools.guides)
            verify(Docs.exec("@qm") && Tools.quickMask)
            verify(Docs.exec("@win:Channels") && Tools.dockTab.l === "Channels" && Tools.panelVisible("Channels"))
            verify(Docs.exec("@win:History") && Tools.sidePanel === "History" && Docs.exec("@win:History") && Tools.sidePanel === "")
            verify(Docs.exec("@side:Info") && Tools.sidePanel === "Info")
            verify(Docs.exec("@aa:Crisp") && Tools.antiAlias === "Crisp" && Docs.exec("@fp:4") && Tools.fontPreview === 4)
            verify(Docs.exec("@toggleLast") && Docs.exec("@revert") && p.index === 0)
            verify(Docs.exec("@open:5") && Docs.current.file === "chan-dung-ho.psd")
            verify(Docs.exec("@closeAll") && Docs.docs.length === 0)
        }

        function test_tools() {
            compare([Tools.tool, Tools.top.heal, Tools.fg, Tools.bg, Tools.recentSwatches.length, Tools.gradientCss()], ["brush", "spot", "#ffffff", "#000000", 10, "linear-gradient(135deg, #ffffff, #000000)"])
            Tools.setTool("patch", "heal")
            compare([Tools.tool, Tools.top.heal, Tools.top.brush], ["patch", "patch", "brush"])
            Tools.setFg("#ff0000", null, true)
            compare([Tools.fg, Tools.fgHue, Tools.recentSwatches[0], Tools.recentSwatches.length], ["#ff0000", 0, "#ff0000", 10])
            Tools.setFg("#808080")
            compare(Tools.fgHue, 0, "grey keeps the hue")
            Tools.setFg("#00ff00")
            compare(Tools.fgHue, 120)
            Tools.addSwatch("#ffffff")
            compare(Tools.recentSwatches.slice(0, 2), ["#ffffff", "#ff0000"])
            Tools.swapColors()
            compare([Tools.fg, Tools.bg], ["#000000", "#00ff00"])
            Tools.resetColors()
            compare([Tools.fg, Tools.bg], ["#000000", "#ffffff"])
            Tools.pickGradient(1, 3)
            compare([Tools.gradient, Tools.recentGradients[0], Tools.recentGradients.length], [[1, 3], [1, 3], 4])
            Tools.pickGradient(3, 1)
            compare(Tools.recentGradients.length, 5)
            Tools.setValue("brush:size", 120)
            compare([Tools.value("brush:size", 400), Tools.value("pencil:size", 4)], [120, 4])
            compare(Tools.cropBox(4400, 5600), { x: 0, y: 50, w: 4400, h: 5500 }, "4:5")
            verify(Tools.panelVisible("Layers") && Tools.panelVisible("Color") && !Tools.panelVisible("Channels"))
            Tools.dockMin = { c: true, p: false, l: false }
            verify(!Tools.panelVisible("Color"))
            Tools.showPanel("Swatches")
            verify(Tools.panelVisible("Swatches") && !Tools.dockMin.c)
            Tools.dockCollapsed = true
            verify(!Tools.panelVisible("Layers"))
            Tools.showPanel("Layers")
            verify(Tools.panelVisible("Layers") && Tools.dockOpen === "l")
            Tools.toggle("grid")
            Tools.toggle("tool")
            verify(Tools.grid && Tools.tool === "patch", "only the switches")
            Tools.setRetouch({ feather: 9 })
            compare([Tools.retouch.feather, Tools.retouch.amount], [9, 35])
        }

        function test_prefs() {  // Settings.dc.html's defaults; kept as JSON
            compare([Prefs.value("theme"), Prefs.value("accent"), Prefs.value("canvasStandard"), Prefs.value("canvasFull"), Prefs.value("fontSize"), Prefs.value("historyBudget")],
                [0, "#2FB5B0", 1, 0, 1, 25])
            compare([Prefs.value("guideColor"), Prefs.value("checkerColor2"), Prefs.value("paintCursor"), Prefs.value("modelBiRefNet"), Prefs.value("modelYuNet")],
                ["#29d3f5", "#c9c9c9", 3, true, false])
            compare([Prefs.value("ramBudget"), Prefs.value("vramBudget"), Prefs.value("scratchBudget"), Prefs.value("gpu"), Prefs.value("cacheLevels"), Prefs.value("scratchDisks")[2]],
                [60, 1.5, 200, true, 4, { name: "E:", on: false }])
            compare(Prefs.sections.length, 16)
            let rows = 0
            for (const id in Prefs.panes)
                for (const r of Prefs.panes[id]) {
                    rows++
                    if (r.key)
                        verify(Prefs.value(r.key) !== undefined, r.key)
                    if (r.kind === "select" || r.kind === "group")
                        verify(r.def >= 0 && r.def < r.options.length, r.key)
                }
            compare([Object.keys(Prefs.panes).length, rows], [15, 68])
            Prefs.set("gpu", false)
            verify(!Prefs.value("gpu"))
            Prefs.reset()
            verify(Prefs.value("gpu"))
            Prefs.persist = true  // ~/.qttest, as the tests' Setup has it
            verify(Prefs.store)
            Prefs.reset()
            Prefs.set("theme", 1)
            compare(JSON.parse(Prefs.store.value("json")).theme, 1)
            const next = Qt.createQmlObject('import QtCore; Settings { category: "prefs" }', root)  // as the next start reads it
            compare(JSON.parse(next.value("json")).theme, 1)
            next.destroy()
            Prefs.restore(JSON.stringify({ theme: 0, gpu: "no", cacheLevels: 7, extra: 1 }))
            compare([Prefs.value("theme"), Prefs.value("gpu"), Prefs.value("cacheLevels"), Prefs.value("extra")], [0, true, 7, undefined], "of the same type, known keys")
            ignoreWarning(/Prefs: ignored, not JSON/)
            Prefs.restore("not JSON")
            compare(Prefs.value("cacheLevels"), 4)
        }
    }
}
