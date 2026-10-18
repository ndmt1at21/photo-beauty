import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty

// U14: the Retouch panel in the dock (RetouchPanel): its buttons as History steps of the document, what they leave in
// Tools.retouch (U7's FaceOverlay draws from it), the badges, the faces.
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

    TestCase {
        name: "U14"
        when: windowShown

        function init() {
            Docs.reset("Retouch")
            Tools.reset("Retouch")
            Prefs.reset()
        }
        function cleanupTestCase() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }

        function open() {  // Main as the Retouch artboard: the portrait, Heal picked, the panel shown
            const w = createTemporaryObject(mainC, root, { startState: "Retouch" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function panel(w) { return find(w.contentItem, o => o instanceof RetouchPanel && o.visible) }
        function section(w, title) { return find(panel(w), o => o instanceof Section && o.title === title) }
        function head(w, title) { return find(section(w, title), o => o instanceof AbstractButton && o.text === title) }
        function named(item, text) { return find(item, o => o instanceof AbstractButton && o.text === text && o.visible) }
        // Scrolled into the dock's 360 px, then clicked; laid out again after (waitForRendering would wait out its
        // timeout on a click that changes no pixel).
        function press(w, item) {
            verify(item, "nothing to press")
            let f = panel(w).parent
            while (!(f instanceof Flickable))
                f = f.parent
            const y = item.mapToItem(f.contentItem, 0, 0).y
            if (y < f.contentY || y + item.height > f.contentY + f.height)
                f.contentY = Math.max(0, Math.min(y - 8, f.contentHeight - f.height))
            verify(waitForPolish(w))
            mouseClick(item)
            verify(waitForPolish(w))
        }
        function unfold(w, title) {
            if (!section(w, title).open)
                press(w, head(w, title))
            verify(section(w, title).open, title)
        }
        function names(doc) { return doc.layers.map(l => "  ".repeat(l.depth) + l.props.name) }
        function last(doc) { return doc.history[doc.index].name }

        // As the artboard: the face, five sections, Mask tự động and Làm mịn da open, as tall as the prototype's.
        function test_panel() {
            const w = open()
            const p = panel(w)
            verify(p)
            compare(p.width, 307)
            compare(p.implicitHeight, 489)
            verify(named(p, qsTr("Tìm lại")))
            verify(find(p, o => o instanceof Text && o.text === "1 khuôn mặt" && o.visible))
            verify(find(p, o => o.objectName === "face" && o.visible))
            compare(["Tách tần số", "Mask tự động", "Làm mịn da", "Dodge & Burn", "Mắt, răng"].map(t => section(w, t).open), [false, true, true, false, false])
            for (const t of ["Tách tần số", "Dodge & Burn", "Mắt, răng"])
                unfold(w, t)
            compare(p.implicitHeight, 810, "the prototype's 811")
            let f = p.parent
            while (!(f instanceof Flickable))
                f = f.parent
            compare(f.contentHeight, 810)
        }
        // A document with no face says so; the buttons still work on it.
        function test_noFace() {
            const w = open()
            Docs.select(1)  // san-pham.webp
            waitForRendering(w.contentItem)
            const p = panel(w)
            verify(find(p, o => o instanceof Text && o.text === "Không thấy khuôn mặt nào trong ảnh này." && o.visible))
            verify(!find(p, o => o.objectName === "face" && o.visible))
            verify(named(p, "Tìm lại"))
        }

        // Tạo: a group Tách tần số, Cao (Linear Light) over Thấp, above the Background, one step; Cao picked.
        function test_frequencySeparation() {
            const w = open()
            Docs.select(1)
            const d = Docs.current, n = d.index
            unfold(w, "Tách tần số")
            compare(section(w, "Tách tần số").badge, "")
            press(w, named(section(w, "Tách tần số"), "Tạo"))
            compare(d.index, n + 1)
            compare(last(d), "Tách tần số")
            compare(names(d), ["Tách tần số", "  Cao", "  Thấp", "Background"])
            compare(d.layers.map(l => l.props.blend), ["passThrough", "linearLight", "normal", "normal"])
            compare(d.active.props.name, "Cao")
            compare(section(w, "Tách tần số").badge, "Đã có nhóm")
            verify(named(section(w, "Tách tần số"), "Tạo lại"))
            d.undo()
            compare(names(d), ["Background"])
            compare(section(w, "Tách tần số").badge, "")
            // The portrait has one: Tạo lại is a step that changes no layer (the core makes them again).
            Docs.select(0)
            const q = Docs.current, layers = q.layers, m = q.index
            compare(section(w, "Tách tần số").badge, "Đã có nhóm")
            press(w, named(section(w, "Tách tần số"), "Tạo lại"))
            compare([q.index, last(q)], [m + 1, "Tách tần số"])
            compare(names(q), layers.map(l => "  ".repeat(l.depth) + l.props.name))
        }
        // The chips pick the parts; Ra: Mask puts one on the active layer, Vùng chọn makes a selection; no chip, nothing.
        function test_autoMask() {
            const w = open()
            const d = Docs.current, heal = d.active.id, n = d.index
            const s = section(w, "Mask tự động")
            compare(d.active.props.mask, null)
            press(w, named(s, "Tạo"))  // Da, Mask
            compare([d.index, last(d)], [n + 1, "Mask từ vùng chọn"])
            verify(d.layer(heal).props.mask)
            d.undo()
            compare(d.layer(heal).props.mask, null)
            press(w, named(s, "Vùng chọn"))
            compare(Tools.retouch.out, 0)
            press(w, named(s, "Mắt"))
            press(w, named(s, "Da"))
            compare(Tools.retouch.chips, { skin: false, eyes: true, lips: false, teeth: false })
            press(w, named(s, "Tạo"))
            compare([d.index, last(d), d.selection], [n + 1, "Mask tự động", "eyes"])
            d.undo()
            compare(d.selection, null)
            press(w, named(s, "Mắt"))
            press(w, named(s, "Tạo"))
            compare(d.index, n, "no part, no step")
        }
        // Áp dụng: a pixel layer above the active one with the skin mask, one step.
        function test_smoothSkin() {
            const w = open()
            const d = Docs.current, n = d.index
            press(w, named(section(w, "Làm mịn da"), "Áp dụng"))
            compare([d.index, last(d)], [n + 1, "Làm mịn da"])
            compare(names(d).slice(6, 9), ["  Thấp", "Làm mịn da 1", "Heal"])
            compare([d.active.props.name, d.active.kind, d.active.props.photo], ["Làm mịn da 1", "pixel", 0])
            verify(d.active.props.mask)
            d.undo()
            compare(names(d).slice(6, 8), ["  Thấp", "Heal"])
        }
        // Tạo: the Curves pair Burn, Dodge with black masks, or a 50 % grey layer in Soft Light; one step either way;
        // then the Brush at Opacity 6 %. Only the grey layer's name gives the badge, as the prototype's.
        function test_dodgeBurn() {
            const w = open()
            Docs.select(1)
            const d = Docs.current
            unfold(w, "Dodge & Burn")
            const s = section(w, "Dodge & Burn")
            compare(s.badge, "")
            Tools.setTool("pencil", "brush")  // Pencil heads the Brush group: Brush heads it again
            press(w, named(s, "Tạo"))
            compare([d.index, last(d)], [1, "Dodge & Burn"])
            compare(names(d), ["Dodge", "Burn", "Background"])
            compare(d.layers.slice(0, 2).map(l => [l.kind, l.props.adj, l.props.mask, l.props.vals.pre]), [["adj", "Curves", "#000000", "Lighter (RGB)"], ["adj", "Curves", "#000000", "Darker (RGB)"]])
            compare([Tools.tool, Tools.top.brush, Tools.value("brush:7")], ["brush", "brush", 6])
            compare(s.badge, "")
            d.undo()
            compare(names(d), ["Background"])
            Tools.setTool("spot", "heal")
            const pick = named(s, "Layer xám 50% Soft Light")
            verify(pick instanceof Radio && pick.spacing === 8 && pick.height === 16, "the shared Radio, the artboard's gap:8px (U23)")
            press(w, pick)
            compare(Tools.retouch.dbm, 1)
            press(w, named(s, "Tạo"))
            compare([d.index, last(d)], [1, "Dodge & Burn"])
            compare(names(d), ["Dodge & Burn 1", "Background"])
            compare([d.active.props.blend, d.active.props.thumb], ["softLight", "gray"])
            compare(Tools.tool, "brush")
            compare(s.badge, "Đã có layer")
            d.undo()
            compare(s.badge, "")
        }
        // Tạo: Curves Làm sáng mắt with the eyes' mask, one step.
        function test_eyesTeeth() {
            const w = open()
            const d = Docs.current, n = d.index
            unfold(w, "Mắt, răng")
            press(w, named(section(w, "Mắt, răng"), "Tạo"))
            compare([d.index, last(d)], [n + 1, "Mắt, răng"])
            compare([d.active.props.name, d.active.props.adj, d.active.props.vals.pre], ["Làm sáng mắt 1", "Curves", "Lighter (RGB)"])
            verify(d.active.props.mask)
            d.undo()
            verify(names(d).indexOf("Làm sáng mắt 1") < 0)
        }

        // The overlay's chips, switch and Feather, and every value, go to Tools.retouch, which outlives the panel.
        function test_values() {
            const w = open()
            const s = section(w, "Mask tự động")
            press(w, named(s, "Răng"))
            compare(Tools.retouch.chips, { skin: true, eyes: false, lips: false, teeth: true })
            press(w, find(s, o => o instanceof Toggle))
            compare(Tools.retouch.overlay, false)
            const feather = find(s, o => o instanceof SliderField)
            feather.forceActiveFocus()
            keyClick(Qt.Key_Right, Qt.ShiftModifier)
            compare(Tools.retouch.feather, 14)
            press(w, head(w, "Mask tự động"))
            compare(Tools.retouch.open.mask, false)
            const smooth = section(w, "Làm mịn da")
            const amount = find(smooth, o => o instanceof SliderField && o.label === "Amount")
            amount.forceActiveFocus()
            keyClick(Qt.Key_Left)
            compare(Tools.retouch.amount, 34)
            const texture = find(smooth, o => o instanceof SliderField && o.label === "Texture")
            texture.forceActiveFocus()
            keyClick(Qt.Key_Up)
            compare(Tools.retouch.texture, 61)
            const radius = find(smooth, o => o instanceof NumberField)
            const input = find(radius, o => o instanceof TextField)
            press(w, input)
            input.selectAll()
            keyClick(Qt.Key_2)
            keyClick(Qt.Key_0)
            keyClick(Qt.Key_Return)
            compare(Tools.retouch.radius, 20)
            unfold(w, "Tách tần số")
            press(w, named(section(w, "Tách tần số"), "Cao"))
            compare(Tools.retouch.fsView, 2)
            const fsr = find(section(w, "Tách tần số"), o => o instanceof NumberField)
            fsr.forceActiveFocus()
            const fin = find(fsr, o => o instanceof TextField)
            fin.forceActiveFocus()
            keyClick(Qt.Key_Up)
            compare(Tools.retouch.fsRadius, 9)
            unfold(w, "Mắt, răng")
            const eyes = section(w, "Mắt, răng")
            for (const [label, key, v] of [["Làm trắng mắt", "eyeW", 21], ["Làm sáng mắt", "eyeB", 31], ["Làm trắng răng", "teeth", 26]]) {
                find(eyes, o => o instanceof SliderField && o.label === label).forceActiveFocus()
                keyClick(Qt.Key_Right)
                compare(Tools.retouch[key], v, label)
            }
            // The tab shown again: a new panel, as left.
            Tools.showPanel("Properties")
            waitForRendering(w.contentItem)
            verify(!panel(w))
            Tools.showPanel("Retouch")
            waitForRendering(w.contentItem)
            compare(["Tách tần số", "Mask tự động", "Làm mịn da", "Dodge & Burn", "Mắt, răng"].map(t => section(w, t).open), [true, false, true, false, true])
            compare(find(section(w, "Mask tự động"), o => o instanceof SliderField).value, 14)
            compare(find(section(w, "Tách tần số"), o => o instanceof NumberField).value, 9)
            compare(find(section(w, "Làm mịn da"), o => o instanceof NumberField).value, 20)
            verify(!find(section(w, "Mask tự động"), o => o instanceof Toggle).checked)
            // The badges follow the document: the group gone, so is its badge.
            const d = Docs.current
            compare(section(w, "Dodge & Burn").badge, "Đã có layer")
            d.run({ op: "deleteLayers", layers: [d.layers.find(l => l.props.name === "Tách tần số").id, d.layers.find(l => l.props.name === "Dodge & Burn").id] })
            compare([section(w, "Tách tần số").badge, section(w, "Dodge & Burn").badge], ["", ""])
            verify(named(section(w, "Tách tần số"), "Tạo"))
            d.undo()
            compare([section(w, "Tách tần số").badge, section(w, "Dodge & Burn").badge], ["Đã có nhóm", "Đã có layer"])
        }
        // The chips, Xem lớp phủ and Feather drive U7's FaceOverlay through Tools.retouch, while the panel shows.
        function test_overlay() {
            const w = open()
            const o = find(w.contentItem, x => x instanceof FaceOverlay)
            verify(o && o.active)
            compare(o.caption, "Xem trước Mask: Da · Feather 4 px")
            const s = section(w, "Mask tự động")
            press(w, named(s, "Răng"))
            compare(o.caption, "Xem trước Mask: Da, Răng · Feather 4 px")
            find(s, x => x instanceof SliderField).forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(o.caption, "Xem trước Mask: Da, Răng · Feather 5 px")
            press(w, find(s, x => x instanceof Toggle))
            compare([o.caption, o.parts], ["", { skin: false, eyes: false, lips: false, teeth: false }])
            Tools.showPanel("Properties")
            verify(!o.active, "only while the panel shows")
        }
        // Tab goes from button to button; Enter folds a section and presses Tạo.
        function test_keys() {
            const w = open()
            const p = panel(w), d = Docs.current, n = d.index
            named(p, "Tìm lại").forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            verify(head(w, "Tách tần số").activeFocus)
            keyClick(Qt.Key_Return)
            compare(Tools.retouch.open.fs, true)
            const make = named(section(w, "Tách tần số"), "Tạo lại")
            make.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            compare([d.index, last(d)], [n + 1, "Tách tần số"])
        }
    }
}
