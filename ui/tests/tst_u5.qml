import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty
import "../qml/ToolSpec.js" as ToolSpec
import "../qml/Fmt.js" as Fmt

// U5: the toolbar (artboard Toolbar), the 28 options bars (artboard OptionsBars), the brush picker, ToolSpec.js and the
// keys: each key, button and flyout changes Tools as it should, each bar has the artboard's cells.
Item {
    id: root
    width: 200
    height: 100
    Component { id: mainC; Main {} }
    // Stand-ins for dialogs and spaces other tasks add (Main.types caches the module's types).
    Component {
        id: probeDialogC
        DialogBox {
            property var params: ({})
            title: "Probe"
        }
    }
    Component {
        id: probeSpaceC
        Item {
            property var params: ({})
            signal finished(bool ok)
        }
    }
    Component { id: spyC; SignalSpy {} }
    // Records what the canvas gets: presses, and the tool in Tools at each.
    Component {
        id: probeC
        MouseArea {
            property var tools: []
            anchors.fill: parent
            onPressed: tools = tools.concat([Tools.tool])
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
    function findAll(item, pred, out) {
        out = out || []
        for (const c of item.children) {
            if (pred(c))
                out.push(c)
            findAll(c, pred, out)
        }
        return out
    }

    TestCase {
        name: "U5"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanupTestCase() {
            init()
        }
        function open(props) {
            const w = createTemporaryObject(mainC, root, props || { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            w.contentItem.forceActiveFocus()
            return w
        }
        function region(w, type) { return root.find(w.contentItem, o => o instanceof type) }
        function toolbar(w) { return region(w, Toolbar) }
        function buttons(w) { return root.findAll(toolbar(w), o => o instanceof ToolBtn && o.visible) }
        function group(w, gid) { return buttons(w).find(b => b.gid === gid) }
        function canvas(w) { return createTemporaryObject(probeC, region(w, Viewport)) }
        function inject(w, types) { w.types = Object.assign({}, w.types, types) }
        function probe(w) { return w.dialogs.find(d => d.title === "Probe") || null }
        // The options bar's cells, in order: the Loader of each, item and all.
        function cells(w) {
            const row = root.find(region(w, OptionsBar), o => o instanceof RowLayout)
            return row.children.filter(c => c instanceof Loader)
        }
        function cell(w, i) { return cells(w)[i].item }
        function bar(w, tool, xf, warp) {
            Tools.setTool(tool)
            Tools.xf = !!xf
            Tools.warp = !!warp
            waitForRendering(w.contentItem)
        }
        function field(it) { return root.find(it, o => o instanceof NumberField) }
        function type(nf, text) {
            const tf = root.find(nf, o => o instanceof TextField)
            mouseClick(tf)
            tryVerify(() => tf.activeFocus)
            keySequence("Ctrl+A")
            for (const ch of text)
                keyClick(ch)
            keyClick(Qt.Key_Return)
        }
        function segments(it) { return root.findAll(it, o => o instanceof AbstractButton) }
        function empty(it) { return !!root.find(it, o => o.objectName === "empty" && o.visible) }
        function pick(w, dd, text) {
            mouseClick(dd)
            tryVerify(() => dd.popup.opened)
            waitForRendering(w.contentItem)
            mouseClick(root.findAll(dd.popup.contentItem, o => o instanceof ItemDelegate && o.text === text)[0])
            tryVerify(() => !dd.popup.visible)
        }
        function rows(m) {  // a PopupMenu's rows: "■" marked, "name|keys"
            const out = []
            for (let i = 0; i < m.count; i++) {
                const it = m.itemAt(i)
                out.push((it.mark ? "■" : "") + it.text + "|" + it.keys)
            }
            return out
        }

        // ---- ToolSpec ----------------------------------------------------------------------------------------------------
        // The groups, names, keys and separators of the Toolbar artboard; the keys of Phím tắt's Công cụ tab.
        function test_spec() {
            const board = [
                ["move", [["Move", "V"]]], 0,
                ["marquee", [["Rectangular Marquee", "M"], ["Elliptical Marquee", "M"]]], ["lasso", [["Lasso", "L"], ["Polygonal Lasso", "L"]]],
                ["wand", [["Magic Wand", "W"], ["Selection Brush", "W"]]], ["crop", [["Crop", "C"], ["Perspective Crop", "C"]]], ["eye", [["Eyedropper", "I"]]], 0,
                ["heal", [["Spot Healing Brush", "J"], ["Healing Brush", "J"], ["Patch", "J"]]], ["brush", [["Brush", "B"], ["Pencil", "B"]]], ["stamp", [["Clone Stamp", "S"]]],
                ["eraser", [["Eraser", "E"]]], ["grad", [["Gradient", "G"], ["Paint Bucket", "G"]]], ["dodge", [["Dodge", "O"], ["Burn", "O"], ["Sponge", "O"]]], 0,
                ["type", [["Horizontal Type", "T"]]], ["shape", [["Rectangle", "U"], ["Ellipse", "U"], ["Triangle", "U"], ["Polygon", "U"], ["Line", "U"]]], 0,
                ["hand", [["Hand", "H"], ["Rotate View", "R"]]], ["zoom", [["Zoom", "Z"]]]]
            compare(ToolSpec.GROUPS.map(g => g ? [g[0], ToolSpec.members(g[0]).map(t => [t.n, t.k])] : 0), board)
            compare(Object.keys(ToolSpec.TOOL).length, 31)
            const keys = [
                ["Chọn, cắt, lấy mẫu", ["Move V", "Rectangular Marquee M", "Elliptical Marquee M", "Lasso L", "Polygonal Lasso L", "Magic Wand W", "Selection Brush W", "Crop C",
                    "Perspective Crop C", "Eyedropper I"]],
                ["Vẽ và sửa", ["Spot Healing Brush J", "Healing Brush J", "Patch J", "Brush B", "Pencil B", "Clone Stamp S", "Eraser E", "Gradient G", "Paint Bucket G", "Dodge O",
                    "Burn O", "Sponge O"]],
                ["Chữ và hình", ["Horizontal Type T", "Rectangle U", "Ellipse U", "Triangle U", "Polygon U", "Line U"]],
                ["Xem", ["Hand H", "Rotate View R", "Zoom Z", "Đổi chế độ màn hình F Screen Mode"]],
                ["Màu và cọ", ["Màu mặc định D Default Colors", "Đổi màu trước, sau X Switch Colors", "Quick Mask Q", "Giảm cỡ cọ [", "Tăng cỡ cọ ]", "Giảm độ cứng ⇧ [",
                    "Tăng độ cứng ⇧ ]"]]]
            compare(ToolSpec.KEYS.map(g => [g[0], g[1].map(r => [r.l, r.k, r.en].filter(x => x).join(" "))]), keys)
            // The 28 bars of OptionsBars, and Ellipse's (Rectangle's but the corners).
            const bars = ["move", "marquee", "lasso", "wand", "selbrush", "crop", "pcrop", "eyedrop", "spot", "heal", "patch", "brush", "pencil", "clone", "eraser", "gradient",
                "bucket", "dodge", "sponge", "type", "rectS", "polygon", "line", "hand", "rot", "zoom", "xf", "warp", "ellipS"]
            compare(Object.keys(ToolSpec.BARS).sort(), bars.slice().sort())
            compare(ToolSpec.BARS.ellipS, ToolSpec.BARS.rectS.slice(0, -2))
            for (const id in ToolSpec.TOOL)
                verify(ToolSpec.BARS[ToolSpec.TOOL[id].bar], id)
            // Cells: a number's default, unit, decimals, limits; the number keys' cells.
            const op = ToolSpec.cell("brush", 7)
            compare([op.kind, op.text, op.def, op.unit, op.min, op.max, op.w], ["FC", "Opacity", 6, "%", 1, 100, 64])
            const rot = ToolSpec.cell("xf", 11)
            compare([rot.kind, rot.def, rot.unit, rot.decimals, rot.min, rot.max], ["IF", 0, "°", 2, -180, 180])
            verify(isNaN(ToolSpec.def("pcrop", 2)))
            compare(ToolSpec.def("rectS", 10), 1200)
            compare(ToolSpec.brush("heal"), { size: 40, hard: 100, spacing: 25, tip: 0 })
            for (const b in ToolSpec.OPACITY)
                verify(["Opacity", "Exposure", "Flow"].indexOf(ToolSpec.cell(b, ToolSpec.OPACITY[b]).text) >= 0, b)
            for (const b in ToolSpec.FLOW)
                compare(ToolSpec.cell(b, ToolSpec.FLOW[b]).text, "Flow")
            compare(ToolSpec.GTYPES.map(g => g[0]), ["linear", "radial", "angle", "reflected", "diamond"])
            compare(ToolSpec.hint("brush"), "Brush: nhấp và kéo để vẽ. Shift nhấp để vẽ đường thẳng; giữ Alt để lấy màu.")
            compare(ToolSpec.hint("lasso"), "Lasso: nhấp lên canvas để dùng.")
            // [ ] as Photoshop steps, Shift [ ] by quarters.
            compare([1, 9, 10, 60, 100, 200, 300, 400, 5000].map(s => ToolSpec.sizeStep(s, 1)), [2, 10, 20, 70, 125, 250, 400, 500, 5000])
            compare([1, 10, 100, 125, 200, 400].map(s => ToolSpec.sizeStep(s, -1)), [1, 9, 90, 100, 175, 300])
            compare([0, 10, 25, 100].map(h => ToolSpec.hardStep(h, 1)), [25, 25, 50, 100])
            compare([0, 10, 25, 100].map(h => ToolSpec.hardStep(h, -1)), [0, 0, 0, 75])
        }

        // ---- the toolbar -------------------------------------------------------------------------------------------------
        // One column as the Toolbar artboard: 32 px buttons at x 6, separators 9 high, then the colours, Quick Mask, F.
        function test_toolbar() {
            const w = open()
            const tb = toolbar(w)
            compare([tb.width, tb.height], [44, 796])
            const bs = buttons(w)
            compare(bs.length, 18)
            const groups = bs.slice(0, 16)
            compare(groups.map(b => b.mapToItem(tb, 0, 0).y), [6, 51, 85, 119, 153, 187, 232, 266, 300, 334, 368, 402, 447, 481, 526, 560])
            verify(bs.every(b => b.mapToItem(tb, 0, 0).x === 6 && b.width === 32 && b.height === 32))
            compare(groups.map(b => b.icon.name), ["open_with", "select", "lasso_select", "auto_fix_normal", "crop", "colorize", "spot_heal", "brush", "approval",
                "ink_eraser", "gradient", "dodge", "title", "rectangle", "pan_tool", "zoom_in"])
            compare(groups.map(b => b.ToolTip.text), ["Move (V)", "Rectangular Marquee (M)", "Lasso (L)", "Magic Wand (W)", "Crop (C)", "Eyedropper (I)",
                "Spot Healing Brush (J)", "Brush (B)", "Clone Stamp (S)", "Eraser (E)", "Gradient (G)", "Dodge (O)", "Horizontal Type (T)", "Rectangle (U)", "Hand (H)",
                "Zoom (Z)"])
            compare(groups.filter(b => b.many).map(b => b.gid), ["marquee", "lasso", "wand", "crop", "heal", "brush", "grad", "dodge", "shape", "hand"])
            compare(groups.filter(b => b.checked).map(b => b.gid), ["brush"])
            const pair = region(w, ColorPair)
            compare([pair.mapToItem(tb, 0, 0).x, pair.mapToItem(tb, 0, 0).y, pair.width], [5, 605, 34])
            compare(bs.slice(16).map(b => [b.icon.name, b.mapToItem(tb, 0, 0).y]), [["quick_mask", 645], ["desktop_windows", 679]])
            Prefs.set("tooltipKeys", false)
            compare(group(w, "brush").ToolTip.text, "Brush")
            // The smallest window: separators 5 high, the screen mode's button still in.
            w.width = 1280
            w.height = 800
            waitForRendering(w.contentItem)
            compare(tb.height, 696)
            const last = buttons(w)[17].mapToItem(tb, 0, 0).y + 32
            verify(last <= tb.height, last)
            compare(group(w, "marquee").mapToItem(tb, 0, 0).y, 47)
        }
        // A click picks the tool on top of the group; one in Free Transform leaves it.
        function test_toolbar_click() {
            const w = open()
            for (const g of ToolSpec.GROUPS.filter(g => g)) {
                Tools.xf = true
                mouseClick(group(w, g[0]))
                compare(Tools.tool, Tools.top[g[0]], g[0])
                verify(!Tools.xf)
                compare(buttons(w).filter(b => b.checked).map(b => b.gid), [g[0]])
            }
            Tools.setTool("ellip", "marquee")
            Tools.setTool("brush")
            mouseClick(group(w, "marquee"))
            compare(Tools.tool, "ellip")
            compare(group(w, "marquee").icon.name, "mq_ellipse")
            compare(group(w, "marquee").ToolTip.text, "Elliptical Marquee (M)")
        }
        // Each group of more than one tool: right button or a long press opens its tools, the one on top marked; a row
        // picks its tool and puts it on top; Esc closes; a press outside closes the flyout and stops there.
        function test_flyout() {
            const w = open()
            const tb = toolbar(w), fly = tb.flyout
            for (const g of ToolSpec.GROUPS.filter(g => g && g[1].length > 1)) {
                const gid = g[0], b = group(w, gid), ts = ToolSpec.members(gid)
                mouseClick(b, 16, 16, Qt.RightButton)
                tryVerify(() => fly.opened, 1000, gid)
                waitForRendering(w.contentItem)
                compare(rows(fly), ts.map((t, i) => (i === 0 ? "■" : "") + t.n + "|" + t.k), gid)
                compare(fly.currentIndex, 0, gid)
                const p = fly.itemAt(0).mapToItem(tb, 0, 0)
                compare(Math.round(p.x), 44, "at the toolbar's right edge")
                const last = ts[ts.length - 1]
                mouseClick(fly.itemAt(ts.length - 1))
                tryVerify(() => !fly.visible, 1000, gid)
                compare(Tools.tool, last.id, gid)
                compare(Tools.top[gid], last.id, gid)
                compare(b.icon.name, last.icn, gid)
            }
            // A long press opens it; that press ends with no click.
            Tools.reset()
            const m = group(w, "marquee")
            mousePress(m)
            tryVerify(() => fly.opened, 1000)
            mouseRelease(m)
            compare(Tools.tool, "brush")
            waitForRendering(w.contentItem)
            compare(rows(fly), ["■Rectangular Marquee|M", "Elliptical Marquee|M"])
            keyClick(Qt.Key_Escape)
            tryVerify(() => !fly.visible)
            mouseClick(m)  // the next click picks
            compare(Tools.tool, "rect")
            mouseClick(m, 16, 16, Qt.RightButton)  // keys: from the tool on top, ↓ and Enter
            tryVerify(() => fly.opened)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryVerify(() => !fly.visible)
            compare(Tools.tool, "ellip")
            Tools.setTool("rect", "marquee")
            // A press on the canvas closes it and does not reach the canvas.
            const c = canvas(w)
            mouseClick(group(w, "brush"), 16, 16, Qt.RightButton)
            tryVerify(() => fly.opened)
            mouseClick(c, 300, 300)
            tryVerify(() => !fly.visible)
            compare(c.tools.length, 0)
            compare(Tools.tool, "rect")
            mouseClick(group(w, "move"), 16, 16, Qt.RightButton)  // one tool: no flyout
            wait(50)
            verify(!fly.visible)
        }
        // Double-click Hand: the view fits; Zoom: 100 %.
        function test_double_click() {
            const w = open()
            const d = Docs.current
            d.zoomTo(50)
            mouseDoubleClickSequence(group(w, "hand"))
            compare(Tools.tool, "hand")
            const fit = d.view.zoom
            verify(fit !== 0.5)
            d.zoomTo(50)
            Docs.exec("@zoomFit")
            compare(d.view.zoom, fit)
            mouseDoubleClickSequence(group(w, "zoom"))
            compare(Tools.tool, "zoom")
            compare(d.view.zoom, 1)
        }
        // The colours: X, D and their buttons; a click on a swatch opens the Color Picker with the colour, its title and
        // where the pick goes; without that dialog nothing opens.
        function test_colors() {
            const w = open()
            const pair = region(w, ColorPair)
            Tools.setFg("#e9c7b3")
            keyClick(Qt.Key_X)
            compare([Tools.fg, Tools.bg], ["#000000", "#e9c7b3"])
            keyClick(Qt.Key_D)
            compare([Tools.fg, Tools.bg], ["#000000", "#ffffff"])
            mouseClick(pair, 30, 2)  // ⇄
            compare([Tools.fg, Tools.bg], ["#ffffff", "#000000"])
            mouseClick(pair, 1, 30)  // the small black and white
            compare([Tools.fg, Tools.bg], ["#000000", "#ffffff"])
            inject(w, { ColorPickerDialog: null })  // without that dialog (before U11): nothing
            mouseClick(pair, 8, 8)
            compare(w.dialogs.length, 0)
            inject(w, { ColorPickerDialog: probeDialogC })
            mouseClick(pair, 8, 8)
            tryVerify(() => !!probe(w))
            let p = probe(w).params
            compare([p.color, p.title], ["#000000", "màu trước"])
            p.picked("#123456")
            compare(Tools.fg, "#123456")
            probe(w).close()
            tryVerify(() => !probe(w))
            mouseClick(pair, 26, 26)
            tryVerify(() => !!probe(w))
            p = probe(w).params
            compare([p.color, p.title], ["#ffffff", "màu sau"])
            p.picked("#abcdef")
            compare(Tools.bg, "#abcdef")
            probe(w).close()
            tryVerify(() => !probe(w))
        }
        // Quick Mask on and off, lit while on; the screen mode button goes through the three modes.
        function test_quick_mask_screen() {
            const w = open()
            const bs = buttons(w), qm = bs[16], scr = bs[17]
            mouseClick(qm)
            verify(Tools.quickMask)
            verify(qm.lit)
            mouseClick(qm)
            verify(!Tools.quickMask)
            compare(scr.Accessible.name, "Chế độ màn hình: chế độ chuẩn (F)")
            mouseClick(scr)
            compare(Tools.screen, 1)
            waitForRendering(w.contentItem)
            mouseClick(scr)
            compare(Tools.screen, 2)
            verify(!toolbar(w).visible)
        }
        // Two columns (Thiết lập › Workspace › Thanh công cụ hai cột, twoColumnTools): 80 wide, the groups two by two with
        // no separators, the canvas beside it; the value kept between starts comes back.
        function test_two_columns() {
            const w = open()
            compare(Prefs.defaults.twoColumnTools, false)
            verify(Prefs.panes.workspace.some(r => r.key === "twoColumnTools" && r.kind === "toggle"))
            compare(toolbar(w).width, 44)
            Prefs.restore(JSON.stringify({ twoColumnTools: true }))
            waitForRendering(w.contentItem)
            compare(toolbar(w).width, 80)
            Prefs.reset()
            waitForRendering(w.contentItem)
            compare(toolbar(w).width, 44)
            Prefs.set("twoColumnTools", true)
            waitForRendering(w.contentItem)
            const tb = toolbar(w)
            compare(tb.width, 80)
            compare(region(w, Viewport).mapToItem(w.contentItem, 0, 0).x, 80)
            const bs = buttons(w)
            compare(bs.length, 18)
            compare(bs.slice(0, 16).map(b => { const p = b.mapToItem(tb, 0, 0); return [p.x, p.y] }),
                    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15].map(i => [i % 2 ? 42 : 6, 6 + Math.floor(i / 2) * 34]))
            const q = bs[16].mapToItem(tb, 0, 0), s = bs[17].mapToItem(tb, 0, 0)
            compare([q.y, s.x - q.x], [s.y, 36])
            mouseClick(bs[15])
            compare(Tools.tool, "zoom")
            Prefs.set("twoColumnTools", false)
            waitForRendering(w.contentItem)
            compare(toolbar(w).width, 44)
        }

        // ---- keys ----------------------------------------------------------------------------------------------------------
        // A tool's key picks its group's tool on top, Shift and the key the next with that key; with Thiết lập's Shift off
        // the key again goes on to the next.
        function test_tool_keys() {
            const w = open()
            const keys = { V: "move", M: "rect", L: "lasso", W: "wand", C: "crop", I: "eyedrop", J: "spot", B: "brush", S: "clone", E: "eraser", G: "gradient", O: "dodge",
                T: "type", U: "rectS", H: "hand", R: "rot", Z: "zoom" }
            for (const k in keys) {
                Tools.setTool(k === "V" ? "zoom" : "move")
                keyClick(k)
                compare(Tools.tool, keys[k], k)
            }
            for (const k of ["M", "L", "W", "C", "J", "B", "G", "O", "U"]) {
                const ids = Object.keys(ToolSpec.TOOL).filter(id => ToolSpec.TOOL[id].k === k)
                keyClick(k)
                for (let i = 1; i <= ids.length; i++) {
                    keyClick(k, Qt.ShiftModifier)
                    compare(Tools.tool, ids[i % ids.length], k + i)
                    compare(Tools.top[ToolSpec.TOOL[ids[0]].g], ids[i % ids.length])
                }
            }
            keyClick(Qt.Key_M)
            keyClick(Qt.Key_M, Qt.ShiftModifier)
            compare(Tools.tool, "ellip")
            keyClick(Qt.Key_V)
            keyClick(Qt.Key_M)  // the group's top stays
            compare(Tools.tool, "ellip")
            keyClick(Qt.Key_R)
            keyClick(Qt.Key_H)  // Hand and Rotate View: a key each
            compare(Tools.tool, "hand")
            Prefs.set("shiftCycle", false)
            keyClick(Qt.Key_J)
            compare(Tools.tool, "spot")
            keyClick(Qt.Key_J)
            compare(Tools.tool, "heal")
            keyClick(Qt.Key_J)
            compare(Tools.tool, "patch")
            keyClick(Qt.Key_J)
            compare(Tools.tool, "spot")
        }
        // [ ] the size, Shift [ ] the hardness of the painting tool's brush; nothing with other tools or in Free Transform.
        function test_brush_keys() {
            const w = open()
            keyClick(Qt.Key_BracketLeft)
            compare(Tools.value("brush:size"), 300)
            keyClick(Qt.Key_BracketRight)
            keyClick(Qt.Key_BracketRight)
            compare(Tools.value("brush:size"), 500)
            keyClick(Qt.Key_BracketRight, Qt.ShiftModifier)
            compare(Tools.value("brush:hard"), 25)
            keyClick(Qt.Key_BracketLeft, Qt.ShiftModifier)
            keyClick(Qt.Key_BracketLeft, Qt.ShiftModifier)
            compare(Tools.value("brush:hard"), 0)
            Tools.setTool("burn")  // Burn's brush is Dodge's bar
            keyClick(Qt.Key_BracketRight)
            compare(Tools.value("dodge:size"), 400)
            Tools.setTool("pencil")
            keyClick(Qt.Key_BracketRight)
            compare(Tools.value("pencil:size"), 5)
            const before = JSON.stringify(Tools.values)
            Tools.setTool("move")
            keyClick(Qt.Key_BracketRight)
            Tools.setTool("brush")
            Tools.xf = true
            keyClick(Qt.Key_BracketRight)
            compare(JSON.stringify(Tools.values), before)
            waitForRendering(w.contentItem)
        }
        // 1 … 9 set Opacity 10 … 90 %, 0 100 % (Exposure of Dodge), Shift and a number the Flow.
        function test_number_keys() {
            const w = open()
            keyClick(Qt.Key_5)
            compare(Tools.value("brush:7"), 50)
            keyClick(Qt.Key_0)
            compare(Tools.value("brush:7"), 100)
            keyClick(Qt.Key_3, Qt.ShiftModifier)
            compare(Tools.value("brush:9"), 30)
            waitForRendering(w.contentItem)
            compare(field(cell(w, 9)).shown, "30%")
            Tools.setTool("gradient")
            keyClick(Qt.Key_7)
            compare(Tools.value("gradient:9"), 70)
            Tools.setTool("burn")
            keyClick(Qt.Key_4)
            compare(Tools.value("dodge:7"), 40)
            const before = JSON.stringify(Tools.values)
            Tools.setTool("move")
            keyClick(Qt.Key_5)
            keyClick(Qt.Key_5, Qt.ShiftModifier)
            compare(JSON.stringify(Tools.values), before)
        }
        // Keys only with a document open and no space; with the toolbar hidden too; not into a text field.
        function test_key_scope() {
            const w = open()
            Tools.showTools = false
            Tools.showOpts = false
            keyClick(Qt.Key_E)
            compare(Tools.tool, "eraser")
            keyPress(Qt.Key_Space)
            compare(Tools.tool, "hand")
            keyRelease(Qt.Key_Space)
            compare(Tools.tool, "eraser")
            Tools.showTools = true
            Tools.showOpts = true
            inject(w, { SelectMaskSpace: probeSpaceC })
            w.openSpace("SelectMask")
            verify(w.space)
            keyClick(Qt.Key_B)
            compare(Tools.tool, "eraser")
            w.space.finished(true)
            keyClick(Qt.Key_B)
            compare(Tools.tool, "brush")
            const zoom = root.find(region(w, StatusBar), o => o instanceof TextField)
            mouseClick(zoom)
            tryVerify(() => zoom.activeFocus)
            keyClick(Qt.Key_E)
            keyPress(Qt.Key_Space)
            keyRelease(Qt.Key_Space)
            keyPress(Qt.Key_Control)
            wait(300)
            compare(Tools.tool, "brush")
            keyRelease(Qt.Key_Control)
            keyClick(Qt.Key_Escape)
            const bar = region(w, AppMenu)  // the menu bar's keyboard mode (Alt on Windows): its letters open menus
            bar.forceActiveFocus()
            keyClick(Qt.Key_E)
            compare(Tools.tool, "brush")
            tryVerify(() => bar.open)
            keyClick(Qt.Key_Escape)
            keyClick(Qt.Key_Escape)
            w.contentItem.forceActiveFocus()
            keyClick(Qt.Key_E)
            compare(Tools.tool, "eraser")
            Docs.reset("Start")
            waitForRendering(w.contentItem)
            keyClick(Qt.Key_B)
            compare(Tools.tool, "eraser")
        }
        // Space held: Hand, back on release; a tool picked meanwhile stays.
        function test_hold_space() {
            const w = open()
            keyPress(Qt.Key_Space)
            compare(Tools.tool, "hand")
            keyRelease(Qt.Key_Space)
            compare(Tools.tool, "brush")
            keyPress(Qt.Key_Space)
            mouseClick(group(w, "eraser"))
            keyRelease(Qt.Key_Space)
            compare(Tools.tool, "eraser")
            Tools.setTool("hand")
            keyPress(Qt.Key_Space)
            keyRelease(Qt.Key_Space)
            compare(Tools.tool, "hand")
        }
        // Alt held while painting: Eyedropper after 250 ms, or at once with a press on the canvas, which then samples; back
        // on release. Becoming held it takes the focus away and back, which ends MenuBar's Alt (its keyboard mode at the
        // release, Windows only: offscreen has no such mode). A quick Alt changes nothing, nor Alt with another tool.
        function test_hold_alt() {
            const w = open()
            const spy = createTemporaryObject(spyC, root, { target: Tools, signalName: "toolChanged" })
            const e = group(w, "eraser")
            e.forceActiveFocus(Qt.TabFocusReason)
            const focus = createTemporaryObject(spyC, root, { target: e, signalName: "activeFocusChanged" })
            keyPress(Qt.Key_Alt)
            compare(Tools.tool, "brush")
            tryCompare(Tools, "tool", "eyedrop", 1000)
            compare(focus.count, 2, "away and back")
            verify(e.activeFocus)
            keyRelease(Qt.Key_Alt, Qt.AltModifier)
            compare(Tools.tool, "brush")
            spy.clear()
            focus.clear()
            keyPress(Qt.Key_Alt)
            keyRelease(Qt.Key_Alt, Qt.AltModifier)
            wait(300)
            compare([spy.count, focus.count], [0, 0], "a quick Alt")
            keyClick(Qt.Key_Escape)
            w.contentItem.forceActiveFocus()
            const c = canvas(w)
            keyPress(Qt.Key_Alt)
            mousePress(c, 300, 300, Qt.LeftButton, Qt.AltModifier)
            compare(c.tools, ["eyedrop"])
            mouseRelease(c, 300, 300, Qt.LeftButton, Qt.AltModifier)
            keyRelease(Qt.Key_Alt, Qt.AltModifier)
            compare(Tools.tool, "brush")
            for (const t of ["move", "clone", "heal", "dodge", "eraser"]) {  // Alt is theirs: a source, Burn, …
                Tools.setTool(t)
                keyPress(Qt.Key_Alt)
                wait(300)
                compare(Tools.tool, t)
                keyRelease(Qt.Key_Alt, Qt.AltModifier)
            }
            Tools.setTool("gradient")
            keyPress(Qt.Key_Alt)
            tryCompare(Tools, "tool", "eyedrop", 1000)
            keyRelease(Qt.Key_Alt, Qt.AltModifier)
            compare(Tools.tool, "gradient")
            keyClick(Qt.Key_B)  // keys go on to the tools
            compare(Tools.tool, "brush")
        }
        // Ctrl held: Move after 250 ms or at a press; a shortcut with Ctrl leaves the tool alone (no bar flickers).
        function test_hold_ctrl() {
            const w = open()
            const spy = createTemporaryObject(spyC, root, { target: Tools, signalName: "toolChanged" })
            keyPress(Qt.Key_Control)
            tryCompare(Tools, "tool", "move", 1000)
            keyRelease(Qt.Key_Control)
            compare(Tools.tool, "brush")
            spy.clear()
            const steps = Docs.current.index
            keyPress(Qt.Key_Control)
            keyPress(Qt.Key_Z, Qt.ControlModifier)
            keyRelease(Qt.Key_Z)  // Ctrl still down
            wait(300)
            compare(Tools.tool, "brush")
            keyRelease(Qt.Key_Control)
            compare(Docs.current.index, steps - 1, "undone")
            compare(spy.count, 0, "Ctrl Z: no Move")
            const c = canvas(w)
            keyPress(Qt.Key_Control)
            mouseClick(c, 200, 200, Qt.LeftButton, Qt.ControlModifier)
            compare(c.tools, ["move"])
            keyRelease(Qt.Key_Control)
            compare(Tools.tool, "brush")
            Tools.setTool("zoom")  // Ctrl is the view's own
            keyPress(Qt.Key_Control)
            wait(300)
            compare(Tools.tool, "zoom")
            keyRelease(Qt.Key_Control)
            Tools.setTool("brush")
            // Not in a dialog.
            inject(w, { ColorPickerDialog: probeDialogC })
            w.openDialog("ColorPicker", {})
            tryVerify(() => !!probe(w) && probe(w).opened)
            keyPress(Qt.Key_Control)
            wait(300)
            compare(Tools.tool, "brush")
            keyRelease(Qt.Key_Control)
            probe(w).close()
            tryVerify(() => !probe(w))
        }

        // ---- the options bars --------------------------------------------------------------------------------------------
        // Each of the 28 bars of OptionsBars has its cells, in order, as ToolSpec.BARS (the artboard's B) writes them:
        // kinds, texts, icons, widths, defaults; dimmed ones off.
        function test_bars() {
            const w = open()
            const ob = region(w, OptionsBar)
            const all = [["move", "move"], ["marquee", "rect"], ["lasso", "lasso"], ["wand", "wand"], ["selbrush", "selbrush"], ["crop", "crop"], ["pcrop", "pcrop"],
                ["eyedrop", "eyedrop"], ["spot", "spot"], ["heal", "heal"], ["patch", "patch"], ["brush", "brush"], ["pencil", "pencil"], ["clone", "clone"],
                ["eraser", "eraser"], ["gradient", "gradient"], ["bucket", "bucket"], ["dodge", "dodge"], ["sponge", "sponge"], ["type", "type"], ["rectS", "rectS"],
                ["polygon", "poly"], ["line", "line"], ["hand", "hand"], ["rot", "rot"], ["zoom", "zoom"], ["xf", "brush"], ["warp", "brush"], ["ellipS", "ellipS"]]
            for (const a of all) {
                bar(w, a[1], a[0] === "xf" || a[0] === "warp", a[0] === "warp")
                compare(ob.bar, a[0])
                const ls = cells(w), spec = ToolSpec.cells(a[0])
                compare(ls.length, spec.length, a[0])
                spec.forEach((c, i) => checkCell(w, a[0], i, c, ls[i].item))
            }
        }
        function checkCell(w, b, i, c, it) {
            const at = b + ":" + i + " " + c.kind
            if (c.kind === "SP") {
                verify(!it, at)
                return
            }
            verify(it, at)
            const off = c.dis || (b === "marquee" && (i === 5 || (i >= 9 && i <= 11))) || (!!c.act && c.act[0] === "=" && !w.hasSpace(c.act.slice(1)))
            if (c.kind !== "ic" && c.kind !== "|" && c.kind !== "L")
                compare(it.enabled, !off, at + " enabled")
            switch (c.kind) {
            case "ic": compare(root.find(it, o => o instanceof Icon).name, b === "xf" ? "crop_free" : b === "warp" ? "grid_on" : ToolSpec.tool(Tools.tool).icn, at); break
            case "|": compare([it.width, it.height], [1, 20], at); break
            case "L": compare(it.text, c.text, at); break
            case "D":
                verify(it instanceof Dropdown, at)
                compare([it.width, it.displayText, it.options.length], [c.w, c.def, c.list.length], at)
                break
            case "F":
            case "FC":
            case "IF": {
                const nf = root.find(it, o => o instanceof NumberField), box = root.find(nf, o => o instanceof TextField).parent.parent
                compare([nf.label, nf.slider], [c.kind === "IF" ? "" : c.text, c.kind === "FC"], at)
                verify(Math.abs(box.width - c.w) < 1, at + " box " + box.width)
                if (isNaN(c.def))
                    verify(empty(it), at + " empty")
                else
                    compare(nf.shown, Fmt.unit(c.def, c.unit, c.decimals), at)
                break
            }
            case "T":
            case "B":
                verify(it instanceof ToolToggle, at)
                compare([it.icon.name, it.ToolTip.text, it.checked], [c.icon, c.tip, c.kind === "T" && c.def], at)
                break
            case "C":
                verify(it instanceof CheckRow, at)
                compare([it.text, it.checked], [c.text, c.def], at)
                break
            case "BT":
            case "BI":
                verify(it instanceof Btn, at)
                compare([it.text, it.icon.name, it.height], [c.text, c.icon || "", 24], at)
                break
            case "M":
            case "S":
            case "S2":
            case "GT":
                verify(it instanceof Segmented, at)
                compare([it.options.length, it.current], [c.options.length, c.def], at)
                break
            case "BP":
                verify(it instanceof BrushPicker, at)
                compare([it.bar, it.size, it.hard], [b, c.size, c.hard], at)
                break
            case "SW":
                verify(it instanceof Swatch, at)
                compare([it.none, it.width, it.height], [c.def === "none", 30, 22], at)
                break
            case "G": verify(root.find(it, o => o instanceof Canvas), at); break
            case "AN": compare([it.anchor, it.compact], [4, true], at); break
            default: fail("unknown cell " + at)
            }
        }
        // Cells keep their values in Tools.values by "<bar>:<index>".
        function test_bar_values() {
            const w = open()
            bar(w, "rect")
            mouseClick(segments(cell(w, 2))[1])  // M: Cộng
            compare(Tools.value("marquee:2"), 1)
            type(field(cell(w, 4)), "20")
            compare(Tools.value("marquee:4"), 20)
            verify(!cell(w, 9).enabled)
            pick(w, cell(w, 8), "Fixed Ratio")
            compare(Tools.value("marquee:8"), "Fixed Ratio")
            verify(cell(w, 9).enabled && cell(w, 10).enabled && cell(w, 11).enabled)
            verify(!cell(w, 5).enabled)
            bar(w, "ellip")
            verify(cell(w, 5).enabled)
            mouseClick(cell(w, 5))
            compare(Tools.value("marquee:5"), false)
            bar(w, "brush")
            type(field(cell(w, 7)), "45")
            compare(Tools.value("brush:7"), 45)
            mouseClick(cell(w, 8))  // T: pressure for Opacity, on by default
            compare(Tools.value("brush:8"), false)
            verify(!cell(w, 8).checked)
            mouseClick(cell(w, 3))  // BS: Brush Settings
            compare(Tools.sidePanel, "Brush Settings")
            bar(w, "sponge")
            mouseClick(segments(cell(w, 6))[0])  // S: Desaturate
            compare(Tools.value("sponge:6"), 0)
            bar(w, "gradient")
            mouseClick(segments(cell(w, 5))[1])  // GT: Radial
            compare(Tools.value("gradient:5"), 1)
            mouseClick(cell(w, 2))  // G: the Gradients panel
            compare(Tools.dockTab.c, "Gradients")
            bar(w, "zoom")
            mouseClick(segments(cell(w, 2))[1])  // S2: zoom out
            compare(Tools.value("zoom:2"), 1)
            Docs.current.zoomTo(50)
            mouseClick(cell(w, 5))  // 100%
            compare(Docs.current.view.zoom, 1)
            bar(w, "pencil")
            mouseClick(cell(w, 15))  // C: Auto Erase
            compare(Tools.value("pencil:15"), true)
            inject(w, { SelectMaskSpace: null })  // without its space (before U15): dimmed
            bar(w, "wand")
            mouseClick(cell(w, 11))  // BI: Select Subject
            compare(Docs.current.selection, "subject")
            verify(!cell(w, 12).enabled, "Select and Mask… waits for its space")
            inject(w, { SelectMaskSpace: probeSpaceC })
            bar(w, "lasso")
            bar(w, "wand")
            verify(cell(w, 12).enabled)
            mouseClick(cell(w, 12))
            verify(w.space)
            w.space.finished(false)
            bar(w, "crop")
            mouseClick(cell(w, 4))  // ⇄
            compare([Tools.value("crop:3"), Tools.value("crop:5")], ["5", "4"])
            compare([field(cell(w, 3)).shown, field(cell(w, 5)).shown], ["5", "4"])
            mouseClick(cell(w, 6))  // Xóa W, H
            compare([Tools.value("crop:2"), Tools.value("crop:3")], ["Ratio", ""])
            verify(empty(cell(w, 3)), "an empty W")
            type(field(cell(w, 3)), "3")
            compare(Tools.value("crop:3"), 3)
            mouseClick(cell(w, 14))  // ✕ Hủy
            compare(Tools.tool, "move")
            bar(w, "crop")
            pick(w, cell(w, 2), "1:1 (Square)")
            const steps = Docs.current.history.length
            mouseClick(cell(w, 15))  // ✓ Áp dụng
            compare(Docs.current.history.length, steps + 1)
            bar(w, "brush", true)
            mouseClick(cell(w, 18))  // T: to Warp
            verify(Tools.warp)
            waitForRendering(w.contentItem)
            compare(region(w, OptionsBar).bar, "warp")
            mouseClick(cell(w, 9))  // ✓ Áp dụng
            verify(!Tools.xf)
            compare(Docs.current.history[Docs.current.index].name, "Warp")
            bar(w, "brush", true)
            mouseClick(segments(cell(w, 2))[0])  // AN: top left
            compare(Tools.value("xf:2"), 0)
            bar(w, "rot")
            type(field(cell(w, 2)), "30")
            compare(Docs.current.view.angle, 30)
            mouseClick(cell(w, 3))  // Đặt lại view
            compare(Docs.current.view.angle, 0)
            bar(w, "type")
            const aa = cell(w, 5)
            compare(aa.displayText, "aA Sharp")
            pick(w, aa, "aA Smooth")
            compare(Tools.antiAlias, "Smooth")
            Tools.antiAlias = "Crisp"
            compare(aa.displayText, "aA Crisp")
            mouseClick(cell(w, 11))  // Character, Paragraph
            compare(Tools.sidePanel, "Character")
        }
        // A colour cell opens the Color Picker with its colour and title; the pick goes back to the cell.
        function test_bar_colors() {
            const w = open()
            inject(w, { ColorPickerDialog: probeDialogC })
            bar(w, "rectS")
            mouseClick(cell(w, 4))
            tryVerify(() => !!probe(w))
            const p = probe(w).params
            compare([p.color, p.title], ["#c0587e", "màu Fill"])
            p.picked("#00ff00")
            compare(Tools.value("rectS:4"), "#00ff00")
            verify(Qt.colorEqual(cell(w, 4).color, "#00ff00"))
            probe(w).close()
            tryVerify(() => !probe(w))
            mouseClick(cell(w, 6))  // Stroke: none, white in the picker
            tryVerify(() => !!probe(w))
            compare(probe(w).params.color, "#ffffff")
            probe(w).close()
            tryVerify(() => !probe(w))
            bar(w, "selbrush")
            verify(Qt.colorEqual(cell(w, 8).color, Qt.rgba(1, 0, 0, 0.5)))
        }
        // A drop-down's list, open, keeps a press on the canvas from it.
        function test_bar_popup_press() {
            const w = open()
            const c = canvas(w)
            const mode = cell(w, 6)
            mouseClick(mode)
            tryVerify(() => mode.popup.opened)
            mouseClick(c, 400, 600)
            tryVerify(() => !mode.popup.visible)
            compare(c.tools.length, 0)
        }
        // The brush picker: Size, Hardness, Spacing, a tip (with its hardness), a preset (size, hardness), of the painting
        // tool's bar; a press outside closes it and stops there.
        function test_brush_picker() {
            const w = open()
            const bp = cell(w, 2), pk = bp.picker
            compare(bp.size, 400)
            mouseClick(bp)
            tryVerify(() => pk.opened)
            waitForRendering(w.contentItem)
            const sliders = root.findAll(pk.contentItem, o => o instanceof SliderField)
            compare(sliders.map(s => [s.label, s.min, s.max, s.valueText]), [["Size", 1, 5000, "400 px"], ["Hardness", 0, 100, "0%"], ["Spacing", 1, 1000, "25%"]])
            sliders[2].forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(Tools.value("brush:spacing"), 26)
            const tips = root.findAll(pk.contentItem, o => o instanceof AbstractButton && o.height === 40)
            compare(tips.length, 6)
            mouseClick(tips[3])
            compare([Tools.value("brush:tip"), Tools.value("brush:hard")], [3, 100])
            const presets = root.findAll(pk.contentItem, o => o instanceof AbstractButton && o.height === 32)
            compare(presets.map(p => p.Accessible.name), ["Tròn mềm", "Tròn cứng", "Tròn vừa", "Dodge & Burn mềm", "Heal cứng", "Mask tóc"])
            mouseClick(presets[1])
            compare([Tools.value("brush:size"), Tools.value("brush:hard")], [60, 100])
            verify(presets[1].on && !presets[0].on)
            compare(sliders[0].valueText, "60 px")
            waitForRendering(w.contentItem)
            compare(root.find(bp, o => o instanceof Text && o.text === "60") !== null, true)
            const c = canvas(w)
            mouseClick(c, 500, 500)
            tryVerify(() => !pk.visible)
            compare(c.tools.length, 0)
            bar(w, "burn")  // Dodge's bar: its own brush
            compare([cell(w, 2).bar, cell(w, 2).size], ["dodge", 300])
            mouseClick(cell(w, 2))
            tryVerify(() => cell(w, 2).picker.opened)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !cell(w, 2).picker.visible)
        }
        // The picker's presets are Brushes' ("brushes:groups", U13): one made or deleted there shows here too (U23).
        function test_brush_picker_presets() {
            const w = open()
            const bp = cell(w, 2)
            const names = () => root.findAll(bp.picker.contentItem, o => o instanceof AbstractButton && o.height === 32).map(p => p.Accessible.name)
            mouseClick(bp)
            tryVerify(() => bp.picker.opened)
            compare(names().length, 6)
            Tools.setValue("brushes:groups", [["Của tôi", [["Cọ mới", 77, 30, ""]]], ["Cơ bản", [["Tròn cứng", 60, 100, ""]]]])
            compare(names(), ["Cọ mới", "Tròn cứng"])
            Tools.setValue("brushes:groups", null)  // Khôi phục: the presets
            compare(names().length, 6)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !bp.picker.visible)
        }
        // A cell another panel also shows keeps one value with it, as Photoshop's (U23): a painting bar's tip angle is
        // Brush Settings' "<bar>:angle" (its tip's own until set), the Type bar's font, style, size and colour Character's.
        function test_bar_shared() {
            const w = open()
            inject(w, { ColorPickerDialog: probeDialogC })
            bar(w, "brush")
            compare(field(cell(w, 16)).value, 0)
            Tools.setValue("brush:tip", 3)  // Dẹt: 30° anticlockwise
            compare(field(cell(w, 16)).value, 30)
            type(field(cell(w, 16)), "45")
            compare([Tools.value("brush:angle"), Tools.value("brush:16")], [45, undefined])
            Tools.setValue("brush:angle", -20)  // Brush Settings' Angle
            compare(field(cell(w, 16)).value, -20)
            for (const b of [["burn", 10, "dodge"], ["pencil", 13, "pencil"], ["eraser", 16, "eraser"], ["sponge", 10, "sponge"]]) {
                bar(w, b[0])
                type(field(cell(w, b[1])), "15")
                compare(Tools.value(b[2] + ":angle"), 15, b[0])
            }
            bar(w, "type")
            pick(w, cell(w, 2), "Montserrat")
            pick(w, cell(w, 3), "Bold")
            type(field(cell(w, 4)), "36")
            compare([Tools.value("char:font"), Tools.value("char:style"), Tools.value("char:size")], ["Montserrat", "Bold", 36])
            type(field(cell(w, 4)), "2000")
            compare(Tools.value("char:size"), 1296, "Character's limit")
            mouseClick(cell(w, 10))
            tryVerify(() => !!probe(w))
            compare(probe(w).params.color, "#f4ede4")
            probe(w).params.picked("#00ff00")
            compare(Tools.value("char:color"), "#00ff00")
            probe(w).close()
            tryVerify(() => !probe(w))
            Tools.setValues({ "char:font": "Lora", "char:size": 72, "char:color": "#ff0000" })  // as Character sets them
            compare([cell(w, 2).displayText, field(cell(w, 4)).value], ["Lora", 72])
            verify(Qt.colorEqual(cell(w, 10).color, "#ff0000"))
            compare(Object.keys(Tools.values).filter(k => /^(type|brush|dodge|pencil|eraser|sponge):\d+$/.test(k)), [], "no keys of their own")
        }
        // The status bar's Công cụ đang dùng names the tool from ToolSpec.
        function test_status_tool() {
            const w = open()
            Tools.statInfo = 5
            const sb = region(w, StatusBar)
            compare(sb.infoText, "Brush")
            Tools.setTool("rot")
            compare(sb.infoText, "Rotate View")
            Tools.xf = true
            compare(sb.infoText, "Free Transform")
        }
        // Tab reaches the toolbar's buttons and Enter presses them; a mouse click leaves the keyboard focus where it was.
        function test_keyboard() {
            const w = open()
            const e = group(w, "eraser")
            mouseClick(e)
            verify(!e.activeFocus)
            const g = group(w, "grad")
            g.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            compare(Tools.tool, "gradient")
            keyClick(Qt.Key_Tab)
            verify(group(w, "dodge").activeFocus)
            keyClick(Qt.Key_Return)
            compare(Tools.tool, "dodge")
        }
    }
}
