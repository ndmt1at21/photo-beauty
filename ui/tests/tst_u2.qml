import QtQuick
import QtQuick.Shapes
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtTest
import PhotoBeauty
import "../qml/Fmt.js" as Fmt

// U2: the shared controls and Fmt.js (docs/ui-tasks.md). Each control is driven as its owner would
// drive it: `value` bound, moved() writing it back; `log` records the value protocol.
Item {
    id: root
    width: 1000
    height: 800
    property var log: []

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
    function popup(item, pred) {  // a Popup an item declares
        for (const o of item.resources) if (o instanceof T.Popup && (!pred || pred(o))) return o
        return null
    }
    function record(c) {  // the value protocol of c into log
        root.log = []
        c.started.connect(() => root.log.push("s"))
        c.moved.connect(v => root.log.push("m" + v))
        c.committed.connect(v => root.log.push("c" + v))
        return c
    }

    Component { id: numberC; NumberField { width: 150; onMoved: v => value = v } }
    Component { id: sliderC; SliderField { width: 200; label: "Amount"; onMoved: v => value = v } }
    Component { id: dropC; Dropdown { width: 140 } }
    Component { id: segC; Segmented {} }
    Component { id: swatchC; Swatch {} }
    Component { id: pairC; ColorPair {} }
    Component { id: checkC; CheckRow { text: "Flip X" } }
    Component { id: toggleC; ToolToggle { icon.name: "brush" } }
    Component { id: sectionC; Section { width: 280; title: "Tách tần số"; Text { text: "body" } } }
    Component { id: tabsC; PanelTabs { width: 308; tabs: ["Layers", "Channels"]; current: "Layers" } }
    Component { id: menuC; PopupMenu {} }
    Component { id: chipsC; Chips { width: 280 } }
    Component { id: anchorC; AnchorGrid {} }
    Component { id: gridC; PresetGrid { width: 280 } }
    Component { id: tileC; Rectangle { required property var modelData; anchors.fill: parent; color: modelData } }
    Component { id: histC; Histogram { width: 256 } }

    TestCase {
        name: "U2"
        when: windowShown

        function cleanup() { root.forceActiveFocus() }

        // ---- Fmt.js ----------------------------------------------------------------------------
        function test_fmt_prints() {
            compare(Fmt.vn(1234.5, 1), "1.234,5")
            compare(Fmt.vn(2480), "2.480")
            compare(Fmt.vn(-3), "−3")
            compare(Fmt.vn(-0.4), "0", "no minus on a zero")
            compare(Fmt.vn(1234567.891, 2), "1.234.567,89")
            compare(Fmt.sg(12), "+12")
            compare(Fmt.sg(-3.5, 1), "−3,5")
            compare(Fmt.sg(0), "0")
            compare(Fmt.zt(12.5), "12,5%")
            compare(Fmt.zt(100 / 3), "33,33%")
            compare(Fmt.zt(1600), "1600%")
            compare(Fmt.unit(2480, "px"), "2.480 px")
            compare(Fmt.unit(62, "%"), "62%")
            compare(Fmt.unit(30, "°"), "30°")
            compare(Fmt.unit(0, "°", 2), "0,00°")
            compare(Fmt.unit(7, ""), "7")
        }
        function test_fmt_reads() {
            compare(Fmt.pnum("1.234,5"), 1234.5)
            compare(Fmt.pnum("12,5%"), 12.5)
            compare(Fmt.pnum("−3"), -3)
            compare(Fmt.pnum("1.234"), 1234, "dots between thousands")
            compare(Fmt.pnum("12.5"), 12.5, "a dot that groups no thousands")
            compare(Fmt.pnum("0.125"), 0.125)
            compare(Fmt.pnum("4.400 px"), 4400)
            compare(Fmt.pnum("abc"), null)
            compare(Fmt.parse("1240*2", "px"), 2480)
            compare(Fmt.parse("100*2", "px"), 200)
            compare(Fmt.parse("(1+2)*3-4/2", ""), 7)
            compare(Fmt.parse("−5+2", "px"), -3)
            fuzzyCompare(Fmt.parse("5cm", "px", 300), 590.551, 0.001)
            compare(Fmt.parse("2in", "px", 300), 600)
            compare(Fmt.parse("72pt", "in"), 1)
            compare(Fmt.parse("10mm+1cm", "mm"), 20)
            compare(Fmt.parse("50%", "px", 72, 1240), 620)
            compare(Fmt.parse("50%", "%"), 50)
            compare(Fmt.parse("2.480 px", "px"), 2480, "a field's own text reads back")
            compare(Fmt.parse("0,00°", "°"), 0)
            compare(Fmt.parse("300 ppi", "ppi"), 300)
            compare(Fmt.parse("5 CM", "px", 254), 500)
            verify(isNaN(Fmt.parse("5cm", "°")), "no length in degrees")
            verify(isNaN(Fmt.parse("1/0", "")))
            verify(isNaN(Fmt.parse("2+", "")))
            verify(isNaN(Fmt.parse("2 3", "")))
            verify(isNaN(Fmt.parse("(2", "")))
            verify(isNaN(Fmt.parse("", "px")))
            verify(isNaN(Fmt.parse("50%", "px")), "no 100 % to take half of")
            fuzzyCompare(Fmt.convert(2480, "px", "cm", 300), 20.997, 0.001)
            compare(Fmt.convert(50, "%", "px", 72, 400), 200)
            compare(Fmt.convert(200, "px", "%", 72, 400), 50)
            compare(Fmt.convert(1, "in", "mm", 72), 25.4)
            compare(Fmt.convert(3, "px", "px", 0), 3)
            verify(isNaN(Fmt.convert(3, "°", "px", 72)))
        }

        // ---- NumberField: the cases of the Controls board ------------------------------------
        function input(f) { return root.find(f, o => o instanceof TextField) }
        function edit(f, text) {  // click into the field and type text over its value
            mouseClick(input(f))
            tryVerify(() => f.editing)
            input(f).text = text
        }
        function test_number_shows() {  // Thường
            const f = createTemporaryObject(numberC, root, { label: "Opacity", value: 100, unit: "%" })
            compare(input(f).text, "100%")
            f.value = 62
            compare(input(f).text, "62%")
            f.unit = "px"
            f.value = -2480
            compare(input(f).text, "−2.480 px")
            f.unit = "°"
            f.decimals = 2
            f.value = 0
            compare(input(f).text, "0,00°")
            compare(f.height, 24)
            verify(!f.editing && !f.scrubbing)
        }
        function test_number_scrub() {  // Kéo nhãn: ±1 a step, Shift ±10
            const f = root.record(createTemporaryObject(numberC, root, { label: "Opacity", value: 50, unit: "%", min: 0, max: 100 }))
            const lbl = root.find(f, o => o instanceof Text && o.text === "Opacity" && o.visible)
            const icon = root.find(f, o => o instanceof Icon && o.name === "scrub")
            mousePress(lbl, 5, 8)
            mouseMove(lbl, 7, 8, -1, Qt.LeftButton)
            compare(root.log, [], "a 3 px dead zone")
            mouseMove(lbl, 10, 8, -1, Qt.LeftButton)
            compare(root.log, ["s", "m55"])
            verify(f.scrubbing)
            verify(icon.visible, "the double arrow shows")
            compare(lbl.color, Theme.t1)
            mouseMove(lbl, 12, 8, -1, Qt.LeftButton, Qt.ShiftModifier)
            compare(f.value, 75, "Shift: 10 a step")
            mouseMove(lbl, 20, 8, -1, Qt.LeftButton, Qt.ShiftModifier)
            compare(f.value, 100, "held at max")
            mouseMove(lbl, 14, 8, -1, Qt.LeftButton)
            compare(f.value, 94)
            mouseRelease(lbl, 14, 8)
            compare(root.log, ["s", "m55", "m75", "m100", "m94", "c94"], "one gesture")
            verify(!f.scrubbing && !icon.visible)
            mouseClick(lbl, 5, 8)
            compare(root.log.length, 6, "a click is no gesture")
        }
        function test_number_sum() {  // Gõ phép tính: 1240*2, Enter gives 2.480 px
            const f = root.record(createTemporaryObject(numberC, root, { label: "W", value: 1240, unit: "px" }))
            mouseClick(input(f))
            tryVerify(() => f.editing)
            tryVerify(() => input(f).selectedText === "1.240 px", 1000, "the value is selected to type over")
            for (const k of "1240*2") keyClick(k)
            compare(input(f).text, "1240*2")
            compare(f.preview, "", "no unit, nothing to preview")
            const ring = root.find(f, o => o instanceof Rectangle && o.border.width === 2)
            verify(ring.visible)
            compare(ring.border.color, Theme.accent)
            keyClick(Qt.Key_Return)
            compare(root.log, ["s", "m2480", "c2480"])
            compare(input(f).text, "2.480 px")
            verify(!f.editing)
        }
        function test_number_units() {  // Gõ đơn vị: 5cm, 2in, 50% at the document's ppi
            const f = root.record(createTemporaryObject(numberC, root, { label: "W", value: 1240, unit: "px", ppi: 300 }))
            edit(f, "5cm")
            compare(f.preview, "591 px")
            const prev = root.find(f, o => o instanceof Text && o.text === "591 px")
            verify(prev && prev.visible)
            keyClick(Qt.Key_Return)
            compare(f.value, 591)
            edit(f, "2in")
            keyClick(Qt.Key_Return)
            compare(f.value, 600)
            edit(f, "50%")
            compare(f.preview, "300 px", "half of the value")
            keyClick(Qt.Key_Return)
            compare(f.value, 300)
            f.percentOf = 2000
            edit(f, "10%")
            keyClick(Qt.Key_Return)
            compare(f.value, 200, "percentOf is 100 %")
            edit(f, "2.480 px")
            compare(f.preview, "", "its own unit")
            const p = createTemporaryObject(numberC, root, { label: "Opacity", value: 40, unit: "%" })
            edit(p, "50%")
            compare(p.preview, "")
            keyClick(Qt.Key_Return)
            compare(p.value, 50)
            root.log = []
            const a = root.record(createTemporaryObject(numberC, root, { label: "Angle", value: 30, unit: "°" }))
            edit(a, "5cm")
            keyClick(Qt.Key_Return)
            compare(root.log, [], "not an angle: nothing")
            compare(input(a).text, "30°")
        }
        function test_number_limits() {  // Vượt giới hạn: "Tối đa 1.000 px", Enter takes the limit
            const f = root.record(createTemporaryObject(numberC, root, { label: "Radius", value: 250, unit: "px", min: 1, max: 1000 }))
            edit(f, "1200")
            verify(f.over)
            compare(f.note, "Tối đa 1.000 px")
            const note = root.popup(f, o => o.contentItem instanceof Text)
            tryVerify(() => note.visible)
            compare(note.contentItem.text, "Tối đa 1.000 px")
            compare(note.contentItem.color, Theme.danger)
            const ring = root.find(f, o => o instanceof Rectangle && o.border.color === Theme.danger)
            verify(ring && ring.visible && ring.border.width === 1, "a 1 px red ring")
            keyClick(Qt.Key_Return)
            compare(root.log, ["s", "m1000", "c1000"])
            tryVerify(() => !note.visible)
            edit(f, "-5")
            compare(f.note, "Tối thiểu 1 px")
            keyClick(Qt.Key_Return)
            compare(f.value, 1)
            edit(f, "999")
            verify(!f.over)
            compare(f.note, "")
        }
        function test_number_escape_and_focus() {  // Esc trả lại; leaving the field applies
            const f = root.record(createTemporaryObject(numberC, root, { label: "Opacity", value: 40, unit: "%" }))
            edit(f, "55")
            keyClick(Qt.Key_Escape)
            compare(root.log, [])
            compare(input(f).text, "40%")
            verify(!f.editing)
            edit(f, "77")
            root.forceActiveFocus()
            compare(root.log, ["s", "m77", "c77"])
            edit(f, "77%")
            keyClick(Qt.Key_Return)
            compare(root.log.length, 3, "no change, no step")
            edit(f, "abc")
            keyClick(Qt.Key_Return)
            compare(root.log.length, 3, "not a number: dropped")
            compare(input(f).text, "77%")
        }
        function test_number_arrows() {  // ↑ ↓ ±1, Shift ±10
            const f = root.record(createTemporaryObject(numberC, root, { label: "Opacity", value: 40, unit: "%", min: 0, max: 100 }))
            mouseClick(input(f))
            tryVerify(() => f.editing)
            keyClick(Qt.Key_Up)
            compare(root.log, ["s", "m41", "c41"])
            compare(input(f).text, "41%")
            keyClick(Qt.Key_Up, Qt.ShiftModifier)
            keyClick(Qt.Key_Down)
            compare(f.value, 50)
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            compare(f.value, 40)
            input(f).text = "95"
            keyClick(Qt.Key_Up, Qt.ShiftModifier)
            compare(f.value, 100, "from what is typed, held at max")
            root.log = []
            keyClick(Qt.Key_Up)
            compare(root.log, [], "at max: nothing")
            verify(f.editing, "arrows keep the field open")
        }
        function test_number_double_click_label() {  // Nhấp đúp nhãn về mặc định
            const f = root.record(createTemporaryObject(numberC, root, { label: "Opacity", value: 40, unit: "%", defaultValue: 100 }))
            const lbl = root.find(f, o => o instanceof Text && o.text === "Opacity" && o.visible)
            mouseDoubleClickSequence(lbl, 5, 8)
            compare(root.log, ["s", "m100", "c100"])
            const g = root.record(createTemporaryObject(numberC, root, { label: "W", value: 40, unit: "px" }))
            mouseDoubleClickSequence(root.find(g, o => o instanceof Text && o.text === "W" && o.visible), 3, 8)
            compare(root.log, [], "no default, nothing")
        }
        function test_number_label_outside() {  // options bar, Layers: label before the box
            const f = createTemporaryObject(numberC, root, { label: "Opacity", labelInside: false, value: 100, unit: "%", width: undefined })
            const lbl = root.find(f, o => o instanceof Text && o.text === "Opacity")
            const box = root.find(f, o => o instanceof Rectangle && o.radius === 4)
            compare(box.x, lbl.width + 6)
            compare(box.width, 64)
            compare(f.implicitWidth, lbl.width + 6 + 64)
            verify(lbl.mapToItem(box, 0, 0).x < 0, "outside the box")
        }
        function test_number_slider() {  // nút bung thanh trượt nhỏ (FC)
            const f = root.record(createTemporaryObject(numberC, root, { label: "Flow", labelInside: false, value: 45, unit: "%", min: 0, max: 100, slider: true }))
            const chev = root.find(f, o => o instanceof AbstractButton && o.visible)
            verify(chev, "the chevron")
            const pop = root.popup(f, o => o.contentItem instanceof SliderField)
            verify(!pop.visible)
            mouseClick(chev)
            tryVerify(() => pop.opened)
            const s = pop.contentItem
            waitForRendering(s)
            compare(s.value, 45)
            tryVerify(() => s.activeFocus, 1000, "keys go to the slider")
            keyClick(Qt.Key_Left)
            compare(root.log, ["s", "m44", "c44"])
            root.log = []
            mousePress(s, s.width * 0.2, 7)
            mouseMove(s, s.width * 0.3, 7, -1, Qt.LeftButton)
            mouseRelease(s, s.width * 0.3, 7)
            compare(root.log, ["s", "m20", "m30", "c30"], "the slider speaks for the field")
            compare(input(f).text, "30%")
            keyClick(Qt.Key_Right)
            compare(f.value, 31)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !pop.visible)
        }

        // ---- SliderField --------------------------------------------------------------------
        function test_slider_drag() {  // kéo đổi liền, thả tay gọi commit
            const s = root.record(createTemporaryObject(sliderC, root, { value: 35 }))
            compare(s.valueText, "35")
            mousePress(s, 100, 23)
            compare(root.log, ["s", "m50"])
            mouseMove(s, 150, 23, -1, Qt.LeftButton)
            compare(s.value, 75)
            verify(s.dragging)
            mouseRelease(s, 150, 23)
            compare(root.log, ["s", "m50", "m75", "c75"])
            mouseClick(s, 150, 23)
            compare(root.log.length, 4, "a click on the value is no gesture")
            mousePress(s, 190, 23)
            mouseMove(s, 260, 23, -1, Qt.LeftButton)
            mouseRelease(s, 260, 23)
            compare(s.value, 100, "held at max")
            s.min = -100
            s.value = 20
            compare(s.valueText, "+20", "signed when it goes both ways")
            s.unit = "px"
            compare(s.valueText, "+20 px")
        }
        function test_slider_thumb() {  // the prototype's .sl thumb (U23): 11 px, 2 of it a ring of the panel colour, on the track
            const s = createTemporaryObject(sliderC, root, { value: 35 })
            const knob = root.find(s, o => o instanceof Rectangle && Qt.colorEqual(o.color, Theme.thumb))
            compare([knob.width, knob.height, knob.parent.width, knob.parent.height], [7, 7, 11, 11])
            verify(Qt.colorEqual(knob.parent.color, Theme.p1))
            const mid = knob.mapToItem(s, 3.5, 3.5)
            compare([mid.x, mid.y], [70, 23.5], "its middle on the value, on the track's middle")
        }
        function test_slider_keys_track() {
            const s = root.record(createTemporaryObject(sliderC, root, { value: 35, track: ["#ff0000", "#ffff00", "#00ff00", "#00ffff"] }))
            s.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Right)
            compare(root.log, ["s", "m36", "c36"])
            keyClick(Qt.Key_Left, Qt.ShiftModifier)
            compare(s.value, 26)
            keyClick(Qt.Key_Up, Qt.ShiftModifier)
            keyClick(Qt.Key_Down)
            compare(s.value, 35)
            const pieces = root.all(s, o => o instanceof Rectangle && o.height === 3 && !!o.gradient)
            compare(pieces.length, 3, "a piece between each pair of colours")
            compare(pieces[0].width, 200 / 3)
            s.decimals = 2
            s.step = 0.01
            s.max = 1
            s.value = 0.5
            keyClick(Qt.Key_Right)
            compare(s.value, 0.51)
            compare(s.valueText, "0,51")
            const bare = createTemporaryObject(sliderC, root, { label: "" })
            compare(bare.height, 15, "the track alone")
        }

        // ---- Dropdown ------------------------------------------------------------------------
        function test_dropdown_pick() {
            const d = createTemporaryObject(dropC, root, { options: ["Normal", "Multiply", "Screen"], value: "Multiply", prefix: "Mode: " })
            let picked = null
            d.picked.connect(v => picked = v)
            compare(d.height, 24)
            compare(d.displayText, "Multiply")
            verify(root.find(d, o => o instanceof Text && o.text === "Mode: ").visible)
            mouseClick(d)
            tryVerify(() => d.popup.opened)
            waitForRendering(d.popup.contentItem)
            const rows = root.all(d.popup.contentItem, o => o instanceof ItemDelegate)
            compare(rows.length, 3)
            compare(rows[0].height, 28)
            verify(root.find(rows[1], o => o instanceof Icon && o.name === "check").visible, "a tick at the value")
            verify(!root.find(rows[0], o => o instanceof Icon && o.name === "check").visible)
            mouseClick(rows[2])
            compare(picked, "Screen")
            tryVerify(() => !d.popup.visible)
            compare(d.displayText, "Multiply", "the owner sets value")
            d.value = "Screen"
            compare(d.displayText, "Screen")
            d.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Up)
            compare(picked, "Multiply", "↑ on the closed box")
        }
        function test_dropdown_pairs_long_up() {
            const blend = []
            for (let i = 0; i < 27; i++) blend.push(["m" + i, "Blend mode " + i])
            const d = createTemporaryObject(dropC, root, { options: blend, value: "m20", y: 20 })
            let picked = null
            d.picked.connect(v => picked = v)
            compare(d.displayText, "Blend mode 20")
            mouseClick(d)
            tryVerify(() => d.popup.opened)
            compare(d.popup.height, 320, "at most 320 px, then it scrolls")
            verify(d.popup.y > 0, "under the box")
            const list = d.popup.contentItem
            verify(list.contentHeight > list.height)
            verify(list.contentY > 0, "scrolled to the value")
            d.popup.close()
            tryVerify(() => !d.popup.visible)
            d.y = root.height - 40
            mouseClick(d)
            tryVerify(() => d.popup.opened)
            verify(d.popup.y < 0, "above the box near the bottom")
            waitForRendering(list)
            const row = root.find(list, o => o instanceof ItemDelegate && o.index === 20)
            mouseClick(row)
            compare(picked, "m20")
        }

        // ---- Segmented, Swatch, ColorPair, CheckRow, ToolToggle, Chips --------------------------
        function test_segmented() {
            const g = createTemporaryObject(segC, root, { options: ["Cả hai", "Thấp", "Cao"], current: 0 })
            let picked = -1
            g.picked.connect(i => picked = i)
            const b = root.all(g, o => o instanceof AbstractButton)
            compare(b.length, 3)
            compare(b[0].background.color, Theme.p3)
            compare(b[1].background.color, Qt.rgba(0, 0, 0, 0))
            compare(b[0].height, 22)
            mouseClick(b[2])
            compare(picked, 2)
            g.current = 2
            compare(b[2].background.color, Theme.p3)
            g.fill = true
            g.width = 244
            compare(b[0].width, 80)
            tryCompare(b[1], "x", 81)
            const i = createTemporaryObject(segC, root, { options: [{ icon: "add", tip: "Cộng vào vùng chọn (⇧)" }, { grad: "radial", t: "Radial" }, { grad: "diamond", t: "Diamond" }] })
            const ib = root.all(i, o => o instanceof AbstractButton)
            verify(root.find(ib[0], o => o instanceof Icon && o.name === "add").visible)
            compare(ib[0].ToolTip.text, "Cộng vào vùng chọn (⇧)")
            verify(root.find(ib[1], o => o instanceof Shape).visible, "a gradient swatch")
            compare(ib[1].ToolTip.text, "Radial")
            compare(ib[0].width, 26)
        }
        function test_swatch_pair() {
            const s = createTemporaryObject(swatchC, root, { color: "#c0587e" })
            let n = 0
            s.clicked.connect(() => n++)
            compare(s.width, 18)
            compare(s.background.color, Qt.color("#c0587e"))
            mouseClick(s)
            compare(n, 1)
            const line = root.find(s, o => o instanceof Rectangle && o.height === 2)
            verify(!line.visible)
            s.none = true
            verify(line.visible, "no colour: struck through")
            compare(line.color, Theme.danger)
            compare(s.background.color, Qt.color("#ffffff"))

            const p = createTemporaryObject(pairC, root, { fg: "#e9c7b3", bg: "#000000" })
            const hits = []
            for (const sig of ["fgClicked", "bgClicked", "swapClicked", "resetClicked"]) p[sig].connect(() => hits.push(sig))
            mouseClick(p, 6, 6)
            mouseClick(p, 36, 36)
            compare(hits, ["fgClicked", "bgClicked"])
            compare(p.width, 40)
            verify(root.all(p, o => o instanceof AbstractButton && o.visible).length === 2, "no small buttons")
            p.compact = true
            compare(p.width, 34)
            compare(p.cell, 19)
            const small = root.all(p, o => o instanceof AbstractButton && o.visible && o.width < 15)
            compare(small.length, 2)
            mouseClick(small[0])
            mouseClick(small[1])
            compare(hits, ["fgClicked", "bgClicked", "swapClicked", "resetClicked"])
        }
        function test_check_toggle_chips() {
            const c = createTemporaryObject(checkC, root)
            let n = 0
            c.clicked.connect(() => n++)
            const box = root.find(c, o => o instanceof Rectangle && o.width === 13)
            compare(box.color, Qt.rgba(0, 0, 0, 0))
            mouseClick(c)
            compare(n, 1)
            verify(!c.checked, "the owner flips it")
            c.checked = true
            compare(box.color, Theme.accent)

            const t = createTemporaryObject(toggleC, root)
            t.clicked.connect(() => n++)
            compare(t.width, 26)
            compare(t.height, 24)
            mouseClick(t)
            compare(n, 2)
            t.checked = true
            compare(t.background.color, Theme.acc2)
            compare(t.contentItem.color, Theme.accent)
            t.enabled = false
            compare(t.contentItem.color, Theme.t4)

            const ch = createTemporaryObject(chipsC, root, { options: [["skin", "Da", "#ff2d55"], ["eyes", "Mắt"]], value: { skin: true } })
            let key = ""
            ch.picked.connect(k => key = k)
            const b = root.all(ch, o => o instanceof AbstractButton)
            compare(b[0].height, 24)
            compare(b[0].background.color, Theme.acc2)
            compare(b[1].background.color, Theme.p2)
            verify(root.find(b[0], o => o instanceof Rectangle && o.width === 8).visible, "the dot")
            verify(!root.find(b[1], o => o instanceof Rectangle && o.width === 8).visible)
            mouseClick(b[1])
            compare(key, "eyes")
        }

        // ---- Section, PanelTabs ---------------------------------------------------------------
        function test_section() {
            const s = createTemporaryObject(sectionC, root, { open: true, badge: "Đã có nhóm" })
            let n = 0
            s.toggled.connect(() => n++)
            const head = root.find(s, o => o instanceof AbstractButton)
            compare(head.height, 30)
            const body = root.find(s, o => o instanceof Text && o.text === "body")
            verify(body.visible)
            verify(root.find(s, o => o instanceof Text && o.text === "Đã có nhóm").visible)
            mouseClick(head)
            compare(n, 1)
            s.open = false
            verify(!body.visible)
            compare(root.find(head, o => o instanceof Icon).name, "chevron_right")
            s.badge = ""
            verify(!root.find(s, o => o instanceof Text && o.text === "Đã có nhóm"))
        }
        function test_panel_tabs() {
            let acted = 0
            const p = createTemporaryObject(tabsC, root, { menu: [{ l: "Đóng", act: () => acted++ }] })
            const hits = []
            p.picked.connect(t => hits.push("p" + t))
            p.tabDoubleClicked.connect(t => hits.push("d" + t))
            compare(p.height, 30)
            const tabs = root.all(p, o => o instanceof AbstractButton && o.text !== "")
            compare(tabs.length, 2)
            const bar = t => root.find(t, o => o instanceof Rectangle && o.height === 2)
            verify(bar(tabs[0]).visible)
            verify(!bar(tabs[1]).visible)
            mouseClick(tabs[1])
            compare(hits, ["pChannels"])
            mouseDoubleClickSequence(tabs[0])
            compare(hits, ["pChannels", "pLayers", "dLayers"])
            const rule = root.find(p, o => o instanceof Rectangle && o.height === 1)
            compare(rule.color, Theme.ln)
            p.collapsed = true
            compare(rule.color, Qt.rgba(0, 0, 0, 0))
            const more = root.find(p, o => o instanceof ToolToggle)
            compare(more.x + more.width, p.width - 4)
            mouseClick(more)
            const m = root.popup(p)
            tryVerify(() => m.opened)
            waitForRendering(m.contentItem)
            mouseClick(m.contentItem, 50, 14)
            compare(acted, 1)
        }

        // ---- PopupMenu ------------------------------------------------------------------------
        function test_menu_rows_keys_submenu() {
            const m = createTemporaryObject(menuC, root)
            let hit = "", inner = 0
            const acts = []
            m.activated.connect(s => acts.push(typeof s.act === "function" ? s.l : s.act))
            const items = [{ l: "Hoàn tác", k: "Ctrl Z", act: () => hit += "A" }, { l: "Crop", disabled: true, act: () => hit += "X" }, { t: "sep" },
                           { l: "Thước", k: "Ctrl R", check: true, act: "@tog:rulers" },
                           { l: "Transform", w: 300, sub: [{ l: "Lật ngang", act: () => inner++ }] }]
            m.show(items, root, 100, 100)
            tryCompare(m, "opened", true)
            waitForRendering(m.contentItem)
            compare(m.count, 5)
            compare(m.width, 236)
            compare(m.itemAt(0).height, 28)
            compare(m.itemAt(2).height, 11, "a separator")
            const keys = root.find(m.itemAt(0), o => o instanceof Text && o.text === "Ctrl Z")
            verify(keys.visible)
            compare(keys.font.family, Theme.mono)
            verify(root.find(m.itemAt(3), o => o instanceof Icon && o.name === "check").visible, "the tick")
            verify(!root.find(m.itemAt(0), o => o instanceof Icon && o.name === "check").visible)
            compare(root.find(m.itemAt(1), o => o instanceof Text && o.text === "Crop").color, Theme.t4)
            verify(root.find(m.itemAt(4), o => o instanceof Icon && o.name === "chevron_right").visible)
            mouseClick(m.contentItem, 100, 28 + 14)
            compare(hit, "", "a greyed row does nothing")
            verify(m.visible)
            mouseClick(m.contentItem, 100, 14)
            compare(hit, "A")
            compare(acts, ["Hoàn tác"])
            tryVerify(() => !m.visible)

            m.show(items, root, 100, 100)
            tryCompare(m, "opened", true)
            compare(m.count, 5, "rebuilt, not appended")
            waitForRendering(m.contentItem)
            mouseMove(m.contentItem, 100, 28 + 28 + 11 + 28 + 14)
            const sub = m.itemAt(4).subMenu
            tryCompare(sub, "opened", true)
            compare(sub.width, 300)
            waitForRendering(sub.contentItem)
            mouseClick(sub.contentItem, 100, 14)
            compare(inner, 1)
            compare(acts, ["Hoàn tác", "Lật ngang"], "a submenu's items reach the menu's owner")
            tryVerify(() => !m.visible)

            mouseMove(root, 900, 700)  // off the menu, or hover picks the row under the cursor
            m.show(items, root, 100, 100)
            tryCompare(m, "opened", true)
            tryVerify(() => m.activeFocus)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(acts, ["Hoàn tác", "Lật ngang", "@tog:rulers"], "Down skips the greyed row and the separator")
            tryVerify(() => !m.visible)
            m.show(items, root, 100, 100)
            tryCompare(m, "opened", true)
            tryVerify(() => m.activeFocus)
            const sub2 = m.itemAt(4).subMenu  // built anew
            for (let k = 0; k < 3; k++) keyClick(Qt.Key_Down)
            compare(m.currentIndex, 4)
            keyClick(Qt.Key_Right)
            tryCompare(sub2, "opened", true)
            keyClick(Qt.Key_Left)
            tryCompare(sub2, "visible", false)
            verify(m.visible)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !m.visible)
        }
        function test_menu_tool_group() {  // the flyout of a tool group: mark, icons, keys
            const m = createTemporaryObject(menuC, root)
            m.show([{ l: "Spot Healing Brush", k: "J", icon: "healing", mark: true, act: "spot" }, { l: "Healing Brush", k: "J", icon: "healing", act: "heal" }], root, 10, 10)
            tryCompare(m, "opened", true)
            waitForRendering(m.contentItem)
            const sq = root.find(m.itemAt(0), o => o instanceof Rectangle && o.width === 5)
            verify(sq.visible)
            verify(!root.find(m.itemAt(1), o => o instanceof Rectangle && o.width === 5).visible)
            const icon = root.find(m.itemAt(0), o => o instanceof Icon && o.name === "healing")
            verify(icon.visible)
            compare(icon.size, 18)
            compare(sq.parent.width, 10)
            m.close()
        }

        // ---- AnchorGrid, PresetGrid, Histogram ------------------------------------------------
        function test_anchor_grid() {
            const g = createTemporaryObject(anchorC, root, { anchor: 4 })
            let picked = -1
            g.picked.connect(i => picked = i)
            const cells = root.all(g, o => o instanceof AbstractButton)
            compare(cells.length, 9)
            compare(cells[0].width, 22)
            const arrow = c => root.find(c, o => o instanceof Icon && o.name === "arrow")
            const arrows = () => cells.map(c => arrow(c).visible)
            compare(arrows(), [true, true, true, true, false, true, true, true, true])
            compare(cells[4].background.color, Theme.t1)
            compare(arrow(cells[7]).rotation, 0, "down")
            compare(arrow(cells[3]).rotation, 90, "left")
            compare(arrow(cells[0]).rotation, 135, "up left")
            compare(arrow(cells[8]).rotation, -45, "down right")
            compare(arrow(cells[7]).size, 14)
            mouseClick(cells[0])
            compare(picked, 0)
            g.anchor = 0
            compare(arrows(), [false, true, false, true, true, false, false, false, false], "only next to the anchor")
            compare(arrow(cells[1]).rotation, -90, "right")
            g.compact = true
            compare(cells[0].width, 5)
            compare(arrows().filter(a => a).length, 0)
            compare(cells[1].background.color, Theme.t4)
            compare(g.border.color, Theme.t4)
        }
        function test_preset_grid() {
            const g = createTemporaryObject(gridC, root, {
                groups: [["", ["#ff0000", "#00ff00"]], ["Da và son", ["#f7d7c4", "#c98f74", "#6b3b2a"]], ["Ánh sáng", ["#fff4e0"]]],
                open: [false, true, false], current: [1, 2], tileWidth: 30, tileHeight: 20, delegate: tileC })
            const hits = []
            g.picked.connect((gi, i) => hits.push(gi + ":" + i))
            g.toggled.connect(gi => hits.push("t" + gi))
            waitForPolish(g)
            const heads = root.all(g, o => o instanceof AbstractButton && o.text !== "")
            compare(heads.filter(h => h.visible).map(h => h.text), ["Da và son", "Ánh sáng"], "no heading for a group without a name")
            compare(heads[1].height, 26, "the artboard's .grp (U23)")
            const tiles = () => root.all(g, o => o instanceof AbstractButton && o.text === "" && o.visible)
            compare(tiles().length, 5, "a closed group hides its tiles")
            compare(tiles()[0].width, 30)
            compare(tiles()[0].height, 20)
            compare(tiles()[4].children[1].color, Qt.color("#6b3b2a"), "the delegate draws the item")
            const ring = t => root.find(t, o => o instanceof Rectangle && o.border.width === 2)
            verify(ring(tiles()[4]).visible, "the tile on")
            verify(!ring(tiles()[3]).visible)
            compare(ring(tiles()[4]).border.color, Theme.accent)
            compare(tiles()[2].mapToItem(g, 0, 0).x, 20, "tiles under a heading are indented")
            compare(tiles()[0].mapToItem(g, 0, 0).x, 0)
            mouseClick(tiles()[3])
            mouseClick(heads[1])
            compare(hits, ["1:1", "t2"])
            g.open = [false, true, true]
            compare(tiles().length, 6)
            g.current = [1, 0]
            compare(tiles().filter(t => ring(t).visible).length, 1, "one tile on, in its own group")
            verify(ring(tiles()[2]).visible)
        }
        function test_histogram() {
            const h = createTemporaryObject(histC, root)
            compare(h.height, 96)
            compare(h.color, Theme.bg)
            const flat = [], peak = []
            for (let i = 0; i < 256; i++) { flat.push(0); peak.push(i === 128 ? 1000 : 10) }
            h.counts = [peak, flat, flat]
            const at = (img, x, y) => img.pixel(x, y)
            let img = null
            tryVerify(() => { img = grabImage(h); return !Qt.colorEqual(at(img, 128, 80), Theme.bg) }, 2000, "red fills under its peak")
            verify(Qt.colorEqual(at(img, 128, 1), Theme.bg) || at(img, 128, 1).r > 0, "drawn from the bottom")
            verify(Qt.colorEqual(at(img, 20, 60), Theme.bg), "low elsewhere")
            h.counts = null
            tryVerify(() => Qt.colorEqual(grabImage(h).pixel(128, 80), Theme.bg))
        }

        // ---- the keyboard ---------------------------------------------------------------------
        function test_tab_order() {
            const f = createTemporaryObject(numberC, root, { label: "W", value: 1 })
            const d = createTemporaryObject(dropC, root, { options: ["a", "b"], value: "a", y: 40 })
            const g = createTemporaryObject(segC, root, { options: ["x", "y"], y: 80 })
            const s = createTemporaryObject(sliderC, root, { y: 120 })
            input(f).forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            verify(d.activeFocus)
            keyClick(Qt.Key_Tab)
            verify(root.all(g, o => o instanceof AbstractButton)[0].activeFocus)
            keyClick(Qt.Key_Tab)
            keyClick(Qt.Key_Tab)
            verify(s.activeFocus)
        }
    }
}
