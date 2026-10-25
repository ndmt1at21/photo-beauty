import QtQuick
import QtTest
import PhotoBeauty
import "../qml/AdjustSpec.js" as Spec
import "../qml/ToolSpec.js" as ToolSpec

// U35: each page of the Properties panel a file, PropertiesNone, PropertiesDocument, PropertiesPixel,
// PropertiesSmartObject, PropertiesText, PropertiesShape, PropertiesFill, PropertiesAdjustment, PropertiesMask, all but
// the first a PropertiesPage (what they share): each page is its file's item, given doc, active and cur by the panel;
// their strings' English in their new contexts (a qsTr's is its file's name); and, while build/u35/shots/ of this
// worktree is there, each page's image in many states saved into it, to compare pixel by pixel with the panel's before
// a change (build/orch/api/u35.md). On chân dung.psd as Docs.reset() opens it.
Item {
    id: root
    width: 1280
    height: 1000

    Component { id: mainC; Main {} }
    Component {
        id: hostC
        Rectangle {  // the dock's colour behind, as it shows; tst_u10's width
            property alias panel: pp
            width: 307
            height: pp.implicitHeight
            color: Theme.p1
            PropertiesPanel {
                id: pp
                width: parent.width
                height: implicitHeight
            }
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

    TestCase {
        name: "U35"
        when: windowShown

        readonly property var files: ({ none: PropertiesNone, doc: PropertiesDocument, px: PropertiesPixel, so: PropertiesSmartObject, text: PropertiesText,
                                        shape: PropertiesShape, fill: PropertiesFill, adj: PropertiesAdjustment, mask: PropertiesMask })

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
            Theme.dark = true
            Theme.fontSize = "m"
        }
        function cleanupTestCase() { init() }

        function doc() { return Docs.current }
        function id(name) { return doc().layers.find(l => l.props.name === name).id }
        function pick(layerId) { doc().run({ op: "selectLayers", layers: layerId === undefined ? [] : [layerId] }) }
        function host() {
            const h = createTemporaryObject(hostC, root)
            waitForRendering(h)
            return h
        }
        function pageOf(p) { return find(p, o => o.objectName.indexOf("page:") === 0) }

        // Each page: its file's item, the panel's Loader's own (no page inline in the panel), given the panel's doc, active
        // layer and document (stand-ins without them) as they change.
        function test_files() {
            const p = host().panel, d = doc()
            const is = (name, what) => {
                tryCompare(p, "page", name, 1000, what)
                const pg = pageOf(p)
                compare(pg.objectName, "page:" + name, what)
                verify(pg instanceof files[name], what + ": " + name + " from its file")
                verify(pg.parent instanceof Loader && pg.parent.parent === p && pg.parent.item === pg, what + ": the panel's Loader's item")
                for (const other of Object.keys(files))
                    verify(other === name || !(pg instanceof files[other]), what + ": not " + other + "'s")
                if (name !== "none") {
                    verify(pg instanceof PropertiesPage, what + ": a PropertiesPage")
                    verify(pg.doc === p.doc && pg.active === p.active && pg.cur === p.cur, what + ": doc, active, cur from the panel")
                    compare(pg.half, (p.width - 60) / 2, what + ": a pair's field")
                } else {
                    verify(!(pg instanceof PropertiesPage), what + ": no document, nothing to give")
                }
                return pg
            }
            is("px", "Dodge & Burn: a pixel layer")
            compare(pageOf(p).active.props.name, "Dodge & Burn")
            pick(id("Tách tần số"))
            compare(is("px", "a group").active.props.name, "Tách tần số", "the active layer as it changes")
            pick()
            compare(is("doc", "no layer").cur, d)
            compare(pageOf(p).active.id, -1, "without a layer, the panel's stand-in")
            pick(id("Curves 1"))
            is("adj", "an adjustment layer")
            d.setEditMask(true)
            compare(is("mask", "its mask").active.props.name, "Curves 1")
            d.setEditMask(false)
            d.run({ op: "newTypeLayer" })
            is("text", "a type layer")
            d.run({ op: "newShapeLayer", shape: "ellipS" })
            is("shape", "a shape layer")
            d.run({ op: "newFillLayer", fill: "Solid Color", color: "#336699" })
            is("fill", "a fill layer")
            d.run({ op: "placeEmbedded" })
            is("so", "a smart object")
            d.run({ op: "mask", action: "reveal" })
            d.setEditMask(true)
            is("mask", "a smart object's mask")
            Docs.closeAll()
            is("none", "no document")
        }

        // Giữ để xem trạng thái trước: the adjustment page's hold is the panel's peeking.
        function test_peeking() {
            const p = host().panel
            pick(id("Curves 1"))
            tryCompare(p, "page", "adj")
            waitForRendering(p)
            const b = find(p, o => o instanceof ToolToggle && o.visible && o.icon.name === "preview")
            verify(!p.peeking && !pageOf(p).peeking)
            mousePress(b)
            tryVerify(() => p.peeking && pageOf(p).peeking, 1000, "held")
            mouseRelease(b)
            tryVerify(() => !p.peeking && !pageOf(p).peeking, 1000, "let go")
        }

        // The strings moved out of PropertiesPanel keep their English, each in its file's context.
        function test_strings() {
            const en = (ctx, s) => Language.translate("en", ctx, s)
            for (const [ctx, s, t] of [["PropertiesNone", "Chưa mở tài liệu nào.", "No document is open."],
                                       ["PropertiesDocument", "Thao tác nhanh", "Quick Actions"],
                                       ["PropertiesPage", "Giữ tỉ lệ · Link width and height", "Link width and height"],
                                       ["PropertiesPixel", "Lật ngang · Flip Horizontal", "Flip Horizontal"],
                                       ["PropertiesSmartObject", "Smart object nhúng", "Embedded Smart Object"],
                                       ["PropertiesText", "Mở Character", "Open Character"],
                                       ["PropertiesShape", "Bo góc dưới phải", "Bottom Right Corner Radius"],
                                       ["PropertiesFill", "Màu của fill layer · Mở Color Picker", "Fill Layer Color · Open Color Picker"],
                                       ["PropertiesAdjustment", "Đặt lại mặc định · Reset to Adjustment Defaults", "Reset to Adjustment Defaults"],
                                       ["PropertiesMask", "Tinh chỉnh", "Refine"]])
                compare(en(ctx, s), t, ctx + ": " + s)
            compare(en("PropertiesPanel", "Chưa mở tài liệu nào."), "", "none left in the panel's")
        }

        // ---- images: build/u35/shots/ of this worktree, each page in many states, to compare before and after ----------
        function local(url) { return decodeURIComponent(String(url).replace(/^file:\/\/(\/(?=[A-Za-z]:))?/, "")) }
        readonly property string shots: local(Qt.resolvedUrl("../../build/u35/shots/"))

        function test_shots() {
            const h = host(), p = h.panel
            mouseMove(root, root.width - 2, root.height - 2)  // nothing hovered
            let n = 0
            const shot = (name, page) => {
                tryCompare(p, "page", page, 1000, name)
                waitForRendering(h)
                const file = shots + String(++n).padStart(3, "0") + "-" + name.replace(/[^A-Za-z0-9-]+/g, "_") + ".png"
                try {
                    grabImage(h).save(file)
                } catch (e) {
                    skip("no " + shots + " to save into")
                }
            }
            const kinds = tag => {  // every page, on chân dung.psd as it opens and as layers are added
                const d = doc(), at = d.active.id
                shot(tag + "px", "px")
                for (const [k, v] of [["w", 1234], ["h", 987], ["x", -12], ["y", 34], ["a", 12.5], ["link", false]])
                    Tools.setValue("L:" + at + ":" + k, v)
                shot(tag + "px-values", "px")
                pick(id("Tách tần số"))
                shot(tag + "group", "px")
                pick()
                shot(tag + "doc", "doc")
                pick(id("Curves 1"))
                shot(tag + "adj-curves", "adj")
                pick(id("Làm trắng răng"))
                shot(tag + "adj-teeth", "adj")
                d.setEditMask(true)
                shot(tag + "mask", "mask")
                d.run({ op: "setProps", props: { maskDensity: 0.5 } })
                d.run({ op: "setProps", props: { maskFeather: 12.5 } })
                d.run({ op: "mask", action: "off" })
                shot(tag + "mask-values", "mask")
                d.setEditMask(false)
                pick(id("Curves 1"))
                d.run({ op: "clippingMask" })
                d.run({ op: "setProps", props: { visible: false } })
                shot(tag + "adj-clip-hidden", "adj")
                d.run({ op: "newTypeLayer" })
                shot(tag + "text", "text")
                Tools.setValue("char:font", ToolSpec.LISTS.font[2])
                Tools.setValue("char:style", ToolSpec.LISTS.fstyle[1])
                Tools.setValue("char:size", 72)
                Tools.setValue("char:leading", 80)
                Tools.setValue("char:tracking", 25)
                Tools.setValue("char:color", "#3366cc")
                Tools.setValue("para:align", 2)
                Tools.antiAlias = "Crisp"
                shot(tag + "text-values", "text")
                for (const s of ["rectS", "ellipS", "tri", "poly", "line"]) {
                    d.run({ op: "newShapeLayer", shape: s })
                    shot(tag + "shape-" + s, "shape")
                }
                Tools.setValue("L:" + d.active.id + ":stroke", "#ffffff")
                Tools.setValue("L:" + d.active.id + ":link", false)
                Tools.setValue("f:sh_sw", "5 px")
                Tools.setValue("f:sh_r1", "8 px")
                Tools.setValue("sh:rlink", false)
                Tools.setValue("sh:op", 2)
                shot(tag + "shape-values", "shape")
                d.run({ op: "newFillLayer", fill: "Solid Color", color: "#336699" })
                shot(tag + "fill-solid", "fill")
                d.run({ op: "newFillLayer", fill: "Gradient", color: "linear-gradient(90deg, #ff0000 0%, #00ff00 50%, #0000ff 100%)" })
                shot(tag + "fill-gradient", "fill")
                d.run({ op: "placeEmbedded" })
                shot(tag + "so", "so")
                d.run({ op: "mask", action: "reveal" })
                d.setEditMask(true)
                shot(tag + "so-mask", "mask")
                d.setEditMask(false)
            }
            kinds("")
            for (const name of Spec.ORDER) {  // the 16 adjustments' rows
                verify(doc().run({ op: "newAdjustmentLayer", adjustment: name }), name)
                shot("adj-" + name, "adj")
            }
            const d = doc()
            d.run({ op: "setMode", mode: "Gray", bits: 8 })
            d.run({ op: "canvasSize", w: 6000, h: 4000 })
            d.run({ op: "imageSize", w: 6000, h: 4000, ppi: 72 })
            pick()
            shot("doc-gray-landscape", "doc")
            d.run({ op: "deleteLayers", layers: d.layers.map(l => l.id) })
            shot("doc-no-layers", "doc")
            verify(Docs.create({ w: 1920, h: 1080, ppi: 150, mode: "Gray", bits: 16 }))
            shot("new-px", "px")
            pick()
            shot("new-doc", "doc")
            Docs.closeAll()
            shot("none", "none")
            for (const [tag, dark, size] of [["large-", true, "l"], ["small-", true, "s"], ["light-", false, "m"]]) {
                Docs.reset()
                Tools.reset()
                Theme.dark = dark
                Theme.fontSize = size
                kinds(tag)
                Docs.closeAll()
                shot(tag + "none", "none")
            }
            Theme.dark = true
            Theme.fontSize = "m"

            // In the window (Main: openDialog, Color Range…): the panel as the dock shows it
            Docs.reset()
            Tools.reset()
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            Tools.showPanel("Properties")
            waitForRendering(w.contentItem)
            const mp = find(w.contentItem, o => o instanceof PropertiesPanel && o.visible)
            verify(mp)
            const inMain = (name, page) => {
                tryCompare(mp, "page", page, 1000, name)
                waitForRendering(w.contentItem)
                let done = false
                verify(mp.grabToImage(r => { done = r.saveToFile(shots + String(++n).padStart(3, "0") + "-main-" + name + ".png") || true }), name)
                tryVerify(() => done, 3000, name)
            }
            const md = doc()
            inMain("px", "px")
            md.run({ op: "selectLayers", layers: [md.layers.find(l => l.props.name === "Làm trắng răng").id] })
            inMain("adj", "adj")
            md.setEditMask(true)
            inMain("mask", "mask")
            md.setEditMask(false)
            md.run({ op: "selectLayers", layers: [] })
            inMain("doc", "doc")
            md.run({ op: "newTypeLayer" })
            inMain("text", "text")
        }
    }
}
