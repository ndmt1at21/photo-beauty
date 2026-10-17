import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty
import "../qml/Presets.js" as Presets

// U13: Brushes, Brush Settings, Character, Paragraph, TipShape, StrokePreview (docs/ui-tasks.md). The panels are made 280
// wide as the side panel makes them and driven by mouse and keys; what they change is Tools'. Main shows them beside the
// icon column.
Item {
    id: root
    width: 700
    height: 1100
    property var log: []
    Component { id: brushesC; BrushesPanel { width: 280 } }
    Component { id: settingsC; BrushSettingsPanel { width: 280 } }
    Component { id: charC; CharacterPanel { width: 280 } }
    Component { id: paraC; ParagraphPanel { width: 280 } }
    Component { id: tipC; TipShape { width: 120; height: 120 } }
    Component { id: strokeC; StrokePreview { width: 260; height: 40 } }
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
        name: "U13"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
            root.log = []
        }
        function cleanupTestCase() { init() }

        function make(c, props) {
            const p = createTemporaryObject(c, root, props || {})
            waitForRendering(p)
            return p
        }
        // A button by its text (preset rows, tips, group heads), a tool toggle by its icon, a field by its label.
        function button(p, text) { return root.find(p, o => o instanceof AbstractButton && o.text === text && o.visible) }
        function toggle(p, icon) { return root.find(p, o => o instanceof ToolToggle && o.icon.name === icon && o.visible) }
        function slider(p, label) { return root.find(p, o => o instanceof SliderField && o.label === label && o.visible) }
        function number(p, label) { return root.find(p, o => o instanceof NumberField && o.label === label) }
        function check(p, text) { return root.find(p, o => o instanceof CheckRow && o.text === text && o.visible) }
        function field(p, tip) { return root.find(p, o => o instanceof NumberField && o.Accessible.name === tip) }
        function input(f) { return root.find(f, o => o instanceof TextField) }
        function click(item) {
            verify(item, "nothing to click")
            mouseClick(item)
            waitForRendering(item)
        }
        function type(f, text) {  // into a NumberField, over its value, then Enter
            mouseClick(input(f))
            tryVerify(() => f.editing)
            tryVerify(() => input(f).selectedText !== "", 1000, "the value is selected to type over")
            for (const k of text)
                keyClick(k)
            keyClick(Qt.Key_Return)
        }
        function v(key) { return Tools.values[key] }

        // ---- Brushes ---------------------------------------------------------------------------------------------------
        function test_preset_sets_brush() {  // nhấp preset đặt cỡ, độ cứng cho công cụ vẽ đang dùng
            const p = make(brushesC)
            compare(Tools.tool, "brush")
            verify(button(p, "Tròn mềm").on, "the brush in use, 400 px soft, is lit")
            click(button(p, "Tròn cứng"))
            compare(v("brush:size"), 60)
            compare(v("brush:hard"), 100)
            verify(button(p, "Tròn cứng").on)
            verify(!button(p, "Tròn mềm").on)
            compare(slider(p, "Size").value, 60)

            Tools.setTool("eraser", "eraser")  // its own brush
            waitForRendering(p)
            verify(!button(p, "Tròn cứng").on, "the eraser's brush, 200 px, is none of them")
            click(button(p, "Heal cứng"))
            compare(Tools.tool, "eraser")
            compare(v("eraser:size"), 40)
            compare(v("eraser:hard"), 100)
            compare(v("brush:size"), 60, "Brush's own stays")

            Tools.setTool("burn", "dodge")  // Burn has Dodge's bar
            click(button(p, "Dodge & Burn mềm"))
            compare(v("dodge:size"), 300)
            compare(v("dodge:hard"), 0)

            Tools.setTool("move", "move")  // a tool that paints nothing: Brush's, and Brush taken
            click(button(p, "Tròn vừa"))
            compare(Tools.tool, "brush")
            compare(Tools.top.brush, "brush")
            compare(v("brush:size"), 120)
            compare(v("brush:hard"), 50)
        }
        function test_size_bar() {  // thanh cỡ của Brushes, Brush Settings: "<bar>:size" như BrushPicker
            const p = make(brushesC)
            const s = slider(p, "Size")
            mouseClick(s, s.width / 2, 22)
            verify(v("brush:size") > 2000 && v("brush:size") < 3000, "half way: " + v("brush:size"))
            compare(s.value, v("brush:size"))
            const at = v("brush:size")
            s.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Right)
            compare(v("brush:size"), at + 1)
            keyClick(Qt.Key_Left, Qt.ShiftModifier)
            compare(v("brush:size"), at - 9)
            const b = make(settingsC)
            compare(slider(b, "Size").value, at - 9, "Brush Settings shows the same brush")
            mouseClick(slider(b, "Size"), 0, 22)
            compare(v("brush:size"), 1)
            compare(s.value, 1)
        }
        function test_groups_and_search() {  // nhóm thu gọn, tìm (bỏ dấu)
            const p = make(brushesC)
            verify(button(p, "Tròn mềm"))
            verify(!button(p, "Mực khô"), "Khô và mực starts folded")
            click(button(p, "Khô và mực"))
            verify(button(p, "Mực khô"))
            compare(v("brushes:open"), [true, true, true])
            click(button(p, "Cơ bản"))
            verify(!button(p, "Tròn mềm"))
            compare(v("brushes:open")[0], false)
            const h = p.implicitHeight
            click(button(p, "Retouch"))
            compare(p.implicitHeight, h - 3 * 40, "the panel is as tall as what it shows")

            const box = root.find(p, o => o instanceof TextBox)
            mouseClick(box)
            for (const k of "CUNG")
                keyClick(k)
            compare(v("brushes:query"), "CUNG")
            verify(button(p, "Tròn cứng"), "found in a folded group, which opens")
            verify(button(p, "Heal cứng"))
            verify(!button(p, "Tròn mềm"))
            verify(!button(p, "Mực khô"))
            compare(root.all(p, o => o instanceof StrokePreview && o.visible).length, 2)
            for (let i = 0; i < 4; i++)
                keyClick(Qt.Key_Backspace)
            compare(v("brushes:query"), "")
            verify(!button(p, "Tròn cứng"), "folded again")
            verify(button(p, "Mực khô"))
        }
        function test_new_group_save_delete() {  // nhóm mới, lưu cọ đang dùng, xóa
            const p = make(brushesC)
            click(toggle(p, "create_new_folder"))
            const g = v("brushes:groups")
            compare(g.length, 4)
            compare(g[3][0], "Nhóm 4")
            compare(g[3][1], [])
            verify(button(p, "Nhóm 4"))
            compare(Presets.BR_GROUPS.length, 3, "the presets of the prototype stay")

            click(button(p, "Tròn cứng"))
            click(toggle(p, "add_box"))
            compare(v("brushes:groups")[3][1], [["Tròn mềm 60", 60, 100, ""]], "the tip's name and the size")
            verify(button(p, "Tròn mềm 60").on)
            verify(v("brushes:open")[3])

            const del = toggle(p, "delete")
            verify(del.enabled)
            click(del)
            compare(v("brushes:groups")[3][1], [], "the last preset of the brush in use goes")
            verify(button(p, "Tròn cứng"))
            click(del)
            verify(!button(p, "Tròn cứng"))
            compare(v("brushes:groups")[0][1].length, 2)
            verify(!del.enabled, "no preset is the brush in use")

            const menu = p.menu
            compare(menu.length, 3)
            verify(menu[2].disabled)
            menu[1].act()
            compare(v("brushes:groups").length, 5)
        }
        function test_rows_by_keys() {  // Tab tới preset, Enter chọn
            const p = make(brushesC)
            const row = button(p, "Heal cứng")
            row.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            compare(v("brush:size"), 40)
            keyClick(Qt.Key_Tab)
            const next = button(p, "Mask tóc")
            verify(next.activeFocus)
            keyClick(Qt.Key_Return)
            compare(v("brush:size"), 24)
            compare(v("brush:hard"), 50)
        }

        function test_keys() {  // Tab tới từng nút của panel, Enter bấm
            const b = make(settingsC)
            const tips = root.all(b, o => o instanceof AbstractButton && o.text !== "" && o.height === 40)
            tips[0].forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            verify(tips[1].activeFocus)
            keyClick(Qt.Key_Return)
            compare(v("brush:tip"), 1)
            const flip = check(b, "Flip X")
            flip.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            compare(v("brush:flipX"), true)
            keyClick(Qt.Key_Tab)
            verify(check(b, "Flip Y").activeFocus)
            keyClick(Qt.Key_Tab)
            verify(number(b, "Angle").editing, "past the dial to the Angle field")

            const t = make(paraC)
            const aligns = root.all(t, o => o instanceof ToolToggle)
            aligns[0].forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            keyClick(Qt.Key_Tab)
            keyClick(Qt.Key_Return)
            compare(v("para:align"), 2)

            const c = make(charC)
            const deco = root.all(c, o => o instanceof ToolToggle && o.visible)
            deco[2].forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            compare(v("char:strike"), true)
        }

        // ---- Brush Settings --------------------------------------------------------------------------------------------
        function test_tips() {  // 6 đầu cọ: chọn đầu cọ đặt độ cứng, góc, độ tròn của nó
            const p = make(settingsC)
            const tips = root.all(p, o => o instanceof AbstractButton && o.text !== "" && o.width > 30 && o.height === 40)
            compare(tips.map(t => t.text), Presets.TIPS.map(t => t[0]))
            verify(tips[0].on)
            Tools.setValues({ "brush:angle": 10, "brush:roundness": 80 })
            compare(number(p, "Angle").value, 10)
            click(tips[3])  // Dẹt: 100 %, 30° anticlockwise, 45 %, its own angle and roundness back
            compare(v("brush:tip"), 3)
            compare(v("brush:hard"), 100)
            compare(number(p, "Angle").value, 30)
            compare(number(p, "Roundness").value, 45)
            const ts = root.find(p, o => o instanceof TipShape)
            compare(ts.angle, 30)
            compare(ts.roundness, 45)
            const sp = root.find(p, o => o instanceof StrokePreview)
            compare(sp.angle, 30)
            compare(sp.roundness, 45)
            click(tips[4])
            verify(sp.square, "Vuông")
            compare(number(p, "Angle").value, 0)
            compare(slider(p, "Hardness").value, 100)
        }
        function test_angle_roundness_drag_and_type() {  // Angle, Roundness kéo và gõ khớp nhau
            const p = make(settingsC)
            const ts = root.find(p, o => o instanceof TipShape)
            const angle = number(p, "Angle"), round = number(p, "Roundness")
            compare(input(angle).text, "0°")
            compare(input(round).text, "100%")
            const c = ts.width / 2, r = ts.half * 0.8
            mousePress(ts, c + r, c)  // the arrow, pointing right
            mouseMove(ts, c + r * 0.7, c - r * 0.7, -1, Qt.LeftButton)
            mouseMove(ts, c, c - r, -1, Qt.LeftButton)
            mouseRelease(ts, c, c - r)
            compare(v("brush:angle"), 90)
            compare(ts.angle, 90)
            compare(input(angle).text, "90°")
            compare(v("brush:roundness"), 100, "turning keeps the roundness")

            const dx = ts.dot.x, dy = ts.dot.y  // at 90° the dots sit right and left of the centre
            fuzzyCompare(dx, ts.half, 0.01)
            fuzzyCompare(dy, 0, 0.01)
            mousePress(ts, c + dx, c + dy)
            mouseMove(ts, c + dx * 0.75, c, -1, Qt.LeftButton)
            mouseMove(ts, c + dx * 0.5, c, -1, Qt.LeftButton)
            mouseRelease(ts, c + dx * 0.5, c)
            compare(v("brush:roundness"), 50)
            compare(v("brush:angle"), 90, "flattening keeps the angle")
            compare(input(round).text, "50%")

            type(angle, "-45")
            compare(v("brush:angle"), -45)
            compare(ts.angle, -45)
            type(round, "30")
            compare(v("brush:roundness"), 30)
            compare(ts.roundness, 30)
            fuzzyCompare(ts.dot.x, Math.sin(-Math.PI / 4) * ts.half * 0.3, 0.01)
            fuzzyCompare(ts.dot.y, Math.cos(-Math.PI / 4) * ts.half * 0.3, 0.01)
            type(angle, "200")
            compare(v("brush:angle"), 180, "the field takes its limit")

            mouseDoubleClickSequence(root.find(angle, o => o instanceof Text && o.text === "Angle" && o.visible))
            compare(v("brush:angle"), 0, "a double-click on the label: the tip's own angle")
        }
        function test_tip_shape_control() {  // TipShape alone: the value protocol, Shift steps of 15°
            const t = make(tipC)
            t.started.connect(() => root.log.push("s"))
            t.moved.connect((a, r) => { root.log.push("m" + a + "/" + r); t.angle = a; t.roundness = r })
            t.committed.connect((a, r) => root.log.push("c" + a + "/" + r))
            mousePress(t, 100, 60)
            mouseMove(t, 100, 40, -1, Qt.LeftButton)
            mouseMove(t, 100, 40, -1, Qt.LeftButton, Qt.ShiftModifier)
            mouseRelease(t, 100, 40, Qt.LeftButton, Qt.ShiftModifier)
            compare(root.log, ["s", "m27/100", "m30/100", "c30/100"])
            compare(t.angle, 30)
            mouseClick(t, 60 + 40 * Math.cos(Math.PI / 6), 60 - 40 * Math.sin(Math.PI / 6))
            compare(root.log.length, 4, "a click where it points changes nothing")
            const dx = t.dot.x, dy = t.dot.y
            mousePress(t, 60 + dx, 60 + dy)  // a dot
            mouseMove(t, 60 + dx / 2, 60 + dy / 2, -1, Qt.LeftButton)
            mouseRelease(t, 60 + dx / 2, 60 + dy / 2)
            verify(Math.abs(t.roundness - 50) <= 1, "half way to the centre: " + t.roundness)
            compare(t.angle, 30)
            compare(root.log[root.log.length - 1], "c30/" + t.roundness)
        }
        function test_brush_settings_values() {  // Hardness, Spacing, Flip, các mục có ô bật, Control
            const p = make(settingsC)
            mouseClick(slider(p, "Hardness"), slider(p, "Hardness").width * 0.25, 22)
            compare(v("brush:hard"), 25)
            const sp = root.find(p, o => o instanceof SliderField && o.Accessible.name === "Spacing")
            mouseClick(sp, 0, 6)
            compare(v("brush:spacing"), 1)
            const preview = root.find(p, o => o instanceof StrokePreview)
            compare(preview.spacing, 1)
            click(check(p, "Spacing"))
            compare(v("brush:spacingOn"), false)
            verify(!sp.enabled)
            compare(preview.spacing, 25, "off: as many dabs as a stroke at speed")

            click(check(p, "Flip X"))
            compare(v("brush:flipX"), true)
            Tools.setValue("brush:angle", 20)
            compare(preview.angle, -20, "flipped once")
            click(check(p, "Flip Y"))
            compare(preview.angle, 20, "and back")

            for (const r of Presets.BS_ROWS) {
                const box = check(p, r[1])
                verify(box, r[1])
                compare(box.checked, r[2] === 1)
            }
            click(check(p, "Noise"))
            compare(v("brush:noise"), true)
            verify(slider(p, "Size Jitter"), "Shape Dynamics starts open")
            verify(!slider(p, "Opacity Jitter"))
            const opener = root.all(p, o => o instanceof ToolToggle && o.visible && (o.icon.name === "chevron_right" || o.icon.name === "expand_more"))
            compare(opener.length, 2)
            click(opener[1])
            compare(v("bset:open"), { shape: true, transfer: true })
            verify(slider(p, "Opacity Jitter"))
            mouseClick(slider(p, "Minimum Diameter"), slider(p, "Minimum Diameter").width / 2, 22)
            compare(v("brush:minDiameter"), 50)
            compare(preview.minSize, 0.5, "the faked pressure narrows the dabs to Minimum Diameter")
            click(check(p, "Shape Dynamics"))
            compare(v("brush:shape"), false)
            compare(preview.minSize, 1, "Shape Dynamics off: no pressure on the size")

            const ctl = root.find(p, o => o instanceof Dropdown && o.visible)
            compare(ctl.value, "Pen Pressure")
            ctl.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Down)
            compare(v("brush:sizeControl"), "Pen Tilt")

            Tools.setTool("eraser", "eraser")  // another tool, its own brush
            waitForRendering(p)
            compare(slider(p, "Size").value, 200)
            verify(!check(p, "Noise").checked)
            mouseClick(slider(p, "Size"), slider(p, "Size").width - 1, 22)
            verify(v("eraser:size") > 4900)
            compare(v("brush:size"), undefined)
        }
        function test_save_from_settings() {  // Lưu thành Brush preset mới
            const p = make(settingsC)
            click(root.find(p, o => o instanceof AbstractButton && o.text === "Vuông"))
            click(toggle(p, "add_box"))
            compare(v("brushes:groups")[2][1].slice(-1)[0], ["Vuông 400", 400, 100, ""])
            compare(v("brushes:open")[2], true)
            const b = make(brushesC)
            verify(button(b, "Vuông 400").on)
        }

        // ---- StrokePreview ---------------------------------------------------------------------------------------------
        function coverage(item) {  // pixels more than half inked, black on the window's white
            const img = grabImage(item)
            let n = 0
            for (let y = 0; y < item.height; y++)
                for (let x = 0; x < item.width; x++)
                    if (img.pixel(x, y).r < 0.5)
                        n++
            return n
        }
        function test_stroke_preview() {  // vẽ lại theo cỡ, độ cứng, spacing, lực nhấn giả
            const s = make(strokeC, { size: 400, hardness: 100, color: "black" })
            tryVerify(() => s.dabs > 0)
            const dabs = s.dabs, hard = coverage(s)
            verify(hard > 300, "a stroke: " + hard)
            s.spacing = 200
            tryVerify(() => s.dabs < dabs / 4)
            s.spacing = 25
            tryVerify(() => s.dabs === dabs)
            s.hardness = 0
            waitForRendering(s)
            const soft = coverage(s)
            verify(soft < hard * 0.7, "soft " + soft + " < hard " + hard)
            s.hardness = 100
            s.size = 5
            waitForRendering(s)
            verify(coverage(s) < hard / 2, "a thin brush")
            s.size = 400
            s.minSize = 0.1
            waitForRendering(s)
            verify(coverage(s) < hard * 0.8, "pressure tapers it")
            s.minSize = 1
            s.roundness = 20
            waitForRendering(s)
            verify(coverage(s) < hard * 0.8, "a flat tip draws thin")
        }

        // ---- Character, Paragraph --------------------------------------------------------------------------------------
        function test_character() {
            const p = make(charC)
            const font = root.find(p, o => o instanceof Dropdown && o.value === "Be Vietnam Pro")
            font.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Down)
            compare(v("char:font"), "Montserrat")
            const style = root.find(p, o => o instanceof Dropdown && o.value === "SemiBold")
            style.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Down)
            compare(v("char:style"), "Bold")

            const size = field(p, "Cỡ chữ")
            compare(input(size).text, "48 pt")
            type(size, "36")
            compare(v("char:size"), 36)

            const lead = field(p, "Leading")
            const auto = root.find(lead.parent, o => o instanceof Text && o.text === "Auto")
            verify(auto && auto.visible, "Auto")
            type(lead, "40")
            compare(v("char:leading"), 40)
            verify(!auto.visible)
            compare(input(lead).text, "40 pt")
            const head = root.find(lead.parent, o => o instanceof MouseArea && o.cursorShape === Qt.SizeHorCursor && o.width < 40)
            mouseDoubleClickSequence(head)
            compare(v("char:leading"), null)
            verify(auto.visible, "back to Auto")
            mousePress(head, 2, 12)  // dragged from Auto: from 120 % of the size
            mouseMove(head, 8, 12, -1, Qt.LeftButton)
            mouseRelease(head, 8, 12)
            compare(v("char:leading"), 43 + 6, "from 120 % of 36 pt, a step a pixel")

            const kern = field(p, "Kerning")
            verify(root.find(kern.parent, o => o instanceof Text && o.text === "Metrics").visible)
            type(kern, "-25")
            compare(v("char:kerning"), -25)
            for (const k of [["Tracking", "50", 50], ["Baseline shift", "2", 2], ["Co giãn dọc", "120", 120], ["Co giãn ngang", "90", 90]]) {
                type(field(p, k[0]), k[1])
                compare(v({ "Tracking": "char:tracking", "Baseline shift": "char:baseline", "Co giãn dọc": "char:vscale", "Co giãn ngang": "char:hscale" }[k[0]]), k[2], k[0])
            }

            const deco = root.all(p, o => o instanceof ToolToggle && o.visible)
            compare(deco.length, 3)
            click(deco[0])
            compare(v("char:caps"), true)
            verify(deco[0].checked)
            click(deco[1])
            click(deco[2])
            compare(v("char:underline"), true)
            compare(v("char:strike"), true)
            click(deco[1])
            compare(v("char:underline"), false)

            const aa = root.find(p, o => o instanceof Dropdown && o.value === "Sharp")
            aa.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Down)
            compare(Tools.antiAlias, "Crisp", "as Type › Anti-Alias and the Type bar")
            verify(root.find(p, o => o instanceof Swatch).color === Qt.color("#f4ede4"))

            p.menu[0].act()  // Đặt lại Character
            compare(input(size).text, "48 pt")
            verify(auto.visible)
            verify(!deco[0].checked)
            compare(font.value, "Be Vietnam Pro")
        }
        function test_paragraph() {
            const p = make(paraC)
            const aligns = root.all(p, o => o instanceof ToolToggle)
            compare(aligns.map(a => a.icon.name), ["format_align_left", "format_align_center", "format_align_right", "just_l", "just_c", "just_r", "just_a"])
            verify(aligns[0].checked)
            click(aligns[5])
            compare(v("para:align"), 5)
            compare(aligns.filter(a => a.checked).length, 1)
            const after = field(p, "Khoảng sau đoạn")
            compare(input(after).text, "12 pt")
            type(field(p, "Thụt trái"), "18")
            compare(v("para:indentLeft"), 18)
            type(field(p, "Thụt phải"), "6")
            compare(v("para:indentRight"), 6)
            type(field(p, "Khoảng trước đoạn"), "4")
            compare(v("para:spaceBefore"), 4)
            type(after, "1cm")
            compare(v("para:spaceAfter"), 28, "a unit typed: 1 cm is 28 pt")
            const first = field(p, "Thụt dòng đầu")
            const lbl = root.find(first.parent, o => o instanceof MouseArea && o.cursorShape === Qt.SizeHorCursor && o.width < 80)
            mousePress(lbl, 2, 12)
            mouseMove(lbl, 12, 12, -1, Qt.LeftButton)
            mouseRelease(lbl, 12, 12)
            compare(v("para:indentFirst"), 10, "dragged by its label")
            mouseDoubleClickSequence(lbl)
            compare(input(first).text, "0 pt")
            p.menu[0].act()
            compare(input(after).text, "12 pt")
            verify(aligns[0].checked)
        }

        // ---- in the window ---------------------------------------------------------------------------------------------
        function test_in_side_panel() {  // cột icon mở panel, đổi tab giữ trạng thái ở Tools, F5
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            const side = root.find(w.contentItem, o => o instanceof SideColumn)
            click(root.find(side, o => o.objectName === "icon:Brushes"))
            compare(Tools.sidePanel, "Brushes")
            const b = root.find(w.contentItem, o => o instanceof BrushesPanel && o.visible)
            verify(b)
            compare(b.width, 280)
            click(button(b, "Khô và mực"))
            click(button(b, "Than chì"))
            compare(v("brush:size"), 12)
            const tabs = root.find(w.contentItem, o => o instanceof PanelTabs && o.visible && o.tabs.indexOf("Brush Settings") >= 0)
            click(root.find(tabs, o => o instanceof AbstractButton && o.text === "Brush Settings"))
            const s = root.find(w.contentItem, o => o instanceof BrushSettingsPanel && o.visible)
            verify(s)
            compare(slider(s, "Size").value, 12)
            compare(slider(s, "Hardness").value, 90)
            const box = root.find(w.contentItem, o => o instanceof SidePanel)
            verify(box.height < s.implicitHeight, "taller than the column: it scrolls")
            click(root.find(tabs, o => o instanceof AbstractButton && o.text === "Brushes"))
            const again = root.find(w.contentItem, o => o instanceof BrushesPanel && o.visible)
            verify(again !== b || !b.visible)
            verify(button(again, "Than chì").on, "the groups open and the brush kept in Tools")
            keyClick(Qt.Key_F5)
            compare(Tools.sidePanel, "Brush Settings")
            click(root.find(side, o => o.objectName === "icon:Character"))
            const c = root.find(w.contentItem, o => o instanceof CharacterPanel && o.visible)
            verify(c)
            compare(box.height, 30 + c.implicitHeight)
            click(root.find(side, o => o.objectName === "icon:Paragraph"))
            verify(root.find(w.contentItem, o => o instanceof ParagraphPanel && o.visible))
        }
    }
}
