import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtCore
import QtTest
import PhotoBeauty

// U21: Thiết lập (SettingsDialog): its 16 sections, every kind of row into Prefs, Hiệu năng, closing, the keyboard; on
// Main: how it opens, theme, accent and text size at once in the whole window, the values at the next start.
Item {
    id: root
    Component { id: settingsC; SettingsDialog {} }
    Component { id: mainC; Main {} }
    Component {
        id: pickerC
        DialogBox {  // ColorPicker (U11) as Main opens it
            property var params: ({})
            title: "Picker"
        }
    }
    Component {
        id: hostC
        Window {  // the window as the dialog sees Main: openDialog(), hasDialog()
            property var dialogs: []
            property var calls: []
            function openDialog(name, params) { calls = calls.concat([[name, params]]) }
            function hasDialog(name) { return dialogs.indexOf(name) >= 0 }
            width: 1440
            height: 900
            visible: true
            color: Theme.bg
        }
    }
    Component { id: fileC; Settings { category: "prefs" } }  // Prefs' JSON in ~/.qttest (the tests' Setup)
    Component { id: pasteC; TextEdit {} }

    TestCase {
        name: "U21"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanup() {  // the Theme the other files expect; no Prefs left writing to ~/.qttest, nor what it wrote
            Theme.dark = true
            Theme.accent = Theme.accents[0]
            Theme.fontSize = "m"
            if (!Prefs.store)
                return
            const s = Prefs.store
            Prefs.persist = false
            Prefs.store = null
            s.setValue("json", "")
            s.destroy()
            Prefs.reset()
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
        function open(params, dialogs) {
            const host = createTemporaryObject(hostC, root, { dialogs: dialogs || [] })
            const d = createTemporaryObject(settingsC, host.contentItem, { params: params || {} })
            d.open()
            tryCompare(d, "opened", true)  // after the fade in, laid out and drawn
            return d
        }
        function host(d) { return d.contentItem.Window.window }
        function rows(d) { return findAll(d.contentItem, o => o.visible && o.modelData && o.modelData.kind !== undefined && o.r !== undefined) }
        function rowOf(d, key) { return find(d.contentItem, o => o.modelData && o.r !== undefined && (o.modelData.key === key || (o.modelData.keys || []).indexOf(key) >= 0)) }
        function inRow(d, key, type) { return find(rowOf(d, key), o => o instanceof type) }
        function button(item, text) { return find(item, o => o instanceof T.AbstractButton && o.text === text && o.visible) }
        function heading(d) { return find(d.contentItem, o => o instanceof Text && o.font.pointSize === Theme.pt(15) && o.x === 22).text }
        function go(d, id) {
            mouseClick(button(d.contentItem, Prefs.sections.find(s => s[0] === id)[1]))
            compare(d.page, id)
            waitForRendering(d.contentItem)
        }
        function slide(s, frac) { mouseClick(s, Math.min(frac * s.width, s.width - 2), s.height / 2) }  // events land on whole pixels
        function openMain(props) {  // as tst_u4: active and drawn
            const w = createTemporaryObject(mainC, root, props || {})
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function settingsOn(w) {  // the Thiết lập Main opened, once it is in
            let d = null
            tryVerify(() => (d = w.dialogs.find(x => x instanceof SettingsDialog) || null) && d.opened)
            return d
        }
        function menuRow(w, i, label) {  // a row of the i-th menu of the menu bar, opened
            const b = find(w.contentItem, o => o instanceof AppMenu), m = b.menuAt(i)
            mouseClick(b.itemAt(i))
            tryVerify(() => m.opened)
            waitForRendering(w.contentItem)
            for (let j = 0; j < m.count; j++)
                if (m.itemAt(j).text === label)
                    return m.itemAt(j)
            return null
        }

        function test_sections() {  // 16 with icons; Hiệu năng first, params.page another; each pane's rows
            let d = open()
            compare(d.page, "performance")
            compare(heading(d), "Hiệu năng")
            const nav = findAll(d.contentItem, o => o instanceof T.AbstractButton && typeof o.modelData === "object" && o.modelData.length === 3)
            compare(nav.map(b => b.text), Prefs.sections.map(s => s[1]))
            compare(nav.map(b => find(b, o => o instanceof Icon).name), Prefs.sections.map(s => s[2]))
            compare(nav.filter(b => b.on).length, 1)
            for (const s of Prefs.sections) {
                go(d, s[0])
                compare(heading(d), s[1])
                const perf = s[0] === "performance"
                compare(!!find(d.contentItem, o => o instanceof SliderField && o.visible && o.Accessible.name === "Ngân sách RAM"), perf, s[0])
                compare(rows(d).length, perf ? 0 : Prefs.panes[s[0]].length, s[0])
            }
            d.close()
            d = open({ page: "about" })
            compare(heading(d), "Báo lỗi, cập nhật, giấy phép")
            compare(rows(d).map(r => r.modelData.label), Prefs.panes.about.map(r => r.label))
        }

        function test_toggle() {
            const d = open({ page: "general" })
            const t = inRow(d, "placeSmartObject", Toggle)
            verify(t.checked)
            mouseClick(t)
            compare(Prefs.value("placeSmartObject"), false)
            verify(!t.checked)
            compare(Prefs.value("placeResize"), true, "its row only")
            mouseClick(t)
            compare(Prefs.value("placeSmartObject"), true)
        }

        function test_select() {  // index of the option; the box as wide as its value, 150 or more, the list as the longest at its right
            const d = open({ page: "files" })
            const s = inRow(d, "recentFiles", Select)
            compare(s.displayText, "20")
            mouseClick(s)
            tryCompare(s.popup, "opened", true)
            waitForRendering(s.popup.contentItem)
            mouseClick(s.popup.contentItem.itemAtIndex(3))
            compare(Prefs.value("recentFiles"), 3)
            tryCompare(s, "displayText", "50")
            compare([s.height, s.width], [26, 150], "the prototype's min-width")
            const place = inRow(d, "quickPlace", Select)
            compare(place.width, 150, "Hỏi mỗi lần: the box as its value (U23)")
            tryCompare(s.popup, "visible", false)  // a click while another list fades out opens nothing
            mouseClick(place)
            tryCompare(place.popup, "opened", true)
            waitForRendering(place.popup.contentItem)
            const longest = place.popup.contentItem.itemAtIndex(1)
            verify(place.popup.width > place.width, "the list wider than the box")
            verify(longest.contentItem.implicitWidth <= longest.availableWidth, "Thư mục assets cạnh tài liệu whole")
            compare(place.popup.x + place.popup.width, place.width, "at the box's right, as the prototype's right: 0")
            keyClick(Qt.Key_Escape)
            tryCompare(place.popup, "visible", false)
            place.forceActiveFocus()
            keyClick(Qt.Key_Down)
            compare(Prefs.value("quickPlace"), 1, "↓ picks the next")
            tryVerify(() => place.width > 150 && place.contentItem.implicitWidth <= place.availableWidth + 1, 1000, "the box follows its value")
        }
        // Nền quanh ảnh's Tùy chọn…: the Color Picker first, as Photoshop's Select Custom Color…; OK keeps its colour and
        // picks it, Hủy leaves both (U23). The canvas around the image takes it (tst_u6's test_surround).
        function test_custom_canvas() {
            const d = open({ page: "interface" }, ["ColorPicker"])
            const s = inRow(d, "canvasStandard", Select), calls = () => host(d).calls
            compare(s.options[4][1], "Tùy chọn…")
            s.activated(4)
            compare(calls().length, 1)
            compare([calls()[0][0], calls()[0][1].color, calls()[0][1].title], ["ColorPicker", "#282828", "Nền quanh ảnh · chuẩn"])
            compare(Prefs.value("canvasStandard"), 1, "nothing until the colour is picked")
            calls()[0][1].picked(Qt.color("#336699"))
            compare([Prefs.value("canvasStandard"), Prefs.value("canvasStandardColor")], [4, "#336699"])
            compare(s.displayText, "Tùy chọn…")
            s.activated(4)  // again: the colour to change, from the one kept
            compare(calls()[1][1].color, "#336699")
            inRow(d, "canvasFull", Select).activated(4)
            compare(calls()[2][1].color, "#000000", "a colour each screen mode")
            s.activated(2)
            compare([Prefs.value("canvasStandard"), calls().length], [2, 3], "another option at once")
        }

        function test_group() {
            const d = open({ page: "cursors" })
            const g = inRow(d, "paintCursor", Segmented)
            compare(g.current, 3)
            mouseClick(button(g, "Precise"))
            compare(Prefs.value("paintCursor"), 1)
            compare(g.current, 1)
            compare(Prefs.value("otherCursor"), 0)
            mouseClick(button(inRow(d, "otherCursor", Segmented), "Precise"))
            compare(Prefs.value("otherCursor"), 1)
        }

        function test_range() {
            const d = open({ page: "history" })
            const s = inRow(d, "historyBudget", SliderField)
            const shown = () => find(rowOf(d, "historyBudget"), o => o instanceof Text && o.font.family === Theme.mono).text
            compare(shown(), "25%")
            slide(s, 1)
            compare(Prefs.value("historyBudget"), 75)
            compare(shown(), "75%")
            slide(s, 25 / 70)
            compare(Prefs.value("historyBudget"), 30, "from 5 to 75 by 5")
            s.forceActiveFocus()
            keyClick(Qt.Key_Left)
            compare(Prefs.value("historyBudget"), 25)
        }

        function test_colors() {  // a well asks Main's ColorPicker, which answers into Prefs; dimmed without one
            let d = open({ page: "transparency" })
            verify(!inRow(d, "checkerColor1", Swatch).enabled, "no ColorPicker in the window")
            d.close()
            d = open({ page: "transparency" }, ["ColorPicker"])
            const wells = findAll(rowOf(d, "checkerColor1"), o => o instanceof Swatch)
            compare(wells.length, 2)
            verify(Qt.colorEqual(wells[1].color, "#c9c9c9"))
            mouseClick(wells[1])
            const calls = host(d).calls
            compare(calls.length, 1)
            compare([calls[0][0], calls[0][1].color, calls[0][1].title], ["ColorPicker", "#c9c9c9", "Màu ô 2"])
            calls[0][1].picked("#123456")
            compare([Prefs.value("checkerColor1"), Prefs.value("checkerColor2")], ["#ffffff", "#123456"])
            verify(Qt.colorEqual(wells[1].color, "#123456"))
            calls[0][1].picked(Qt.color("#abcdef"))  // a colour: kept as its "#rrggbb", as Prefs keeps JSON
            compare(Prefs.value("checkerColor2"), "#abcdef")
            compare(typeof Prefs.value("checkerColor2"), "string")
            go(d, "guides")
            mouseClick(inRow(d, "gridColor", Swatch))
            compare(host(d).calls[1][1].title, "Lưới")
            host(d).calls[1][1].picked("#00ff00")
            compare([Prefs.value("gridColor"), Prefs.value("guideColor")], ["#00ff00", "#29d3f5"])
        }

        // As the artboard (U23): the 1000 × 700 box, its 1 px ring outside it; a colour well 30 × 22 in a ring of t4
        // outside it (32 × 24 in all), 8 between the wells (6 between the rings), the ring 1 px past the column.
        function test_frame_wells() {
            const d = open({ page: "transparency" })
            compare([d.width, d.height, d.availableWidth, d.availableHeight], [1002, 702, 1000, 700])
            const row = rowOf(d, "checkerColor1"), w = findAll(row, o => o instanceof Swatch), x = o => o.mapToItem(row, 0, 0).x
            compare([w[0].width, w[0].height, x(w[1]) - x(w[0]) - w[0].width, x(w[1]) + w[1].width - row.width], [32, 24, 6, 1])
        }

        function test_accent() {
            const d = open({ page: "interface" })
            const dots = findAll(rowOf(d, "accent"), o => o instanceof T.AbstractButton)
            compare(dots.map(a => a.Accessible.name), ["Xanh ngọc #2FB5B0", "Xanh dương #4C8DF6", "Vàng hổ phách #E0A53A", "Cam san hô #E8664A"])
            compare(dots.map(a => a.on), [true, false, false, false])
            mouseClick(dots[3])
            compare(Prefs.value("accent"), "#E8664A")
            compare(dots.map(a => a.on), [false, false, false, true])
            Prefs.set("accent", "#4c8df6")  // a kept value in lower case
            compare(dots.map(a => a.on), [false, true, false, false])
        }

        function test_buttons() {  // model, button, link
            let d = open({ page: "ai" })
            const label = key => find(rowOf(d, key), o => o instanceof Text && o.visible).text
            compare(label("modelYuNet"), "YuNet · 0,3 MB · chưa tải")
            mouseClick(button(rowOf(d, "modelYuNet"), "Tải"))
            compare(Prefs.value("modelYuNet"), true)
            compare(label("modelYuNet"), "YuNet · 0,3 MB · đã tải")
            mouseClick(button(rowOf(d, "modelYuNet"), "Xóa"))
            compare(Prefs.value("modelYuNet"), false)
            compare(Prefs.value("modelBiRefNet"), true)

            go(d, "about")
            const copy = button(d.contentItem, "Sao chép mã")
            mouseClick(copy)
            compare(copy.text, "Đã chép")
            const paste = createTemporaryObject(pasteC, root)
            paste.paste()
            compare(paste.text, "7F3A-92C1-4D0E-B85A")
            verify(button(d.contentItem, "Nhập mã kích hoạt").enabled)

            go(d, "shortcuts")
            verify(!button(d.contentItem, "Mở Phím tắt…").enabled, "no Shortcuts in the window")
            d.close()
            d = open({ page: "shortcuts" }, ["Shortcuts"])
            mouseClick(button(d.contentItem, "Mở Phím tắt…"))
            compare(host(d).calls.map(c => c[0]), ["Shortcuts"])
            tryCompare(d, "visible", false, 2000, "Thiết lập leaves for it")
            compare(Object.keys(Prefs.values).filter(k => JSON.stringify(Prefs.values[k]) !== JSON.stringify(Prefs.defaults[k])), [])
        }

        // Hiệu năng as the artboard (U23): a budget's slider the 11 px .rg, its 3 px track in the middle: 7 under the value
        // line and 4 in, then 4 and the page's 14 to the next row; the meters 25 apart, a line of 12 px × 1.4 and 8.
        function test_perf_rows() {
            const d = open()
            const y = o => o.mapToItem(d.contentItem, 0, 0).y
            const s = findAll(d.contentItem, o => o instanceof SliderField && o.visible), val = s[0].parent.children[0]
            const m = ["RAM", "VRAM", "Scratch"].map(n => find(d.contentItem, o => o instanceof Text && o.visible && o.text === n).parent)
            compare([y(s[0]) - y(val) - val.height, y(s[1].parent.parent) - y(s[0]) - s[0].height, y(m[1]) - y(m[0]), y(m[2]) - y(m[1])], [11, 18, 25, 25])
        }

        function test_notes() {
            const d = open({ page: "pen" })
            const t = find(d.contentItem, o => o instanceof Text && o.visible && o.text === "Wintab cho bảng vẽ Wacom đời cũ.")
            verify(t)
            compare(t.color, Theme.t3)
        }

        function test_perf() {  // Hiệu năng: budgets and meters, GPU, the disks in order, Cache Levels
            const d = open()
            const text = s => !!find(d.contentItem, o => o instanceof Text && o.visible && o.text === s)
            const slider = n => find(d.contentItem, o => o instanceof SliderField && o.Accessible.name === n)
            verify(text("60% · 9,6 GB") && text("RAM máy 16 GB") && text("3,1 / 9,6 GB"))
            slide(slider("Ngân sách RAM"), 1)
            compare(Prefs.value("ramBudget"), 90)
            verify(text("90% · 14,4 GB") && text("3,1 / 14,4 GB"))
            verify(text("1,5 GB") && text("0,9 / 1,5 GB"))
            slider("Ngân sách VRAM").forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(Prefs.value("vramBudget"), 1.75)
            verify(text("1,75 GB") && text("0,9 / 1,75 GB"))
            const meter = find(d.contentItem, o => o.percent !== undefined && o.name === "VRAM")
            compare(meter.percent, 51)
            slide(slider("Ngân sách VRAM"), 0)
            compare(Prefs.value("vramBudget"), 0.5)
            compare(meter.percent, 100)

            const gpu = find(d.contentItem, o => o instanceof Toggle && o.Accessible.name === "Dùng GPU")
            mouseClick(gpu)
            compare(Prefs.value("gpu"), false)

            const order = () => Prefs.value("scratchDisks").map(x => x.name + (x.on ? "+" : "-")).join(" ")
            const places = () => findAll(d.contentItem, o => o instanceof Text && o.font.family === Theme.mono && o.width === 22).map(o => o.text)
            compare(order(), "C:+ D:+ E:-")
            compare(places(), ["1", "2", ""])
            verify(text("trống 520 GB trên ổ đã chọn") && text("trống 1,2 TB"))
            mouseClick(find(d.contentItem, o => o instanceof CheckRow && o.Accessible.name === "Dùng ổ E:"))
            compare(order(), "C:+ D:+ E:+")
            verify(text("trống 1,7 TB trên ổ đã chọn"))
            const up = n => find(d.contentItem, o => o instanceof IconButton && o.Accessible.name === "Đưa ổ " + n + " lên")
            const down = n => find(d.contentItem, o => o instanceof IconButton && o.Accessible.name === "Đưa ổ " + n + " xuống")
            verify(!up("C:").enabled && down("C:").enabled && !down("E:").enabled)
            mouseClick(up("E:"))
            mouseClick(up("E:"))
            compare(order(), "E:+ C:+ D:+")
            verify(!up("E:").enabled && !down("D:").enabled)
            mouseClick(down("C:"))
            compare(order(), "E:+ D:+ C:+")
            mouseClick(find(d.contentItem, o => o instanceof CheckRow && o.Accessible.name === "Dùng ổ E:"))
            compare(places(), ["", "1", "2"])
            for (const n of ["D:", "C:"])
                mouseClick(find(d.contentItem, o => o instanceof CheckRow && o.Accessible.name === "Dùng ổ " + n))
            verify(text("chưa chọn ổ nào"))

            slide(slider("Ngân sách scratch"), 1)
            compare(Prefs.value("scratchBudget"), 500)
            verify(text("500 GB") && text("1,2 / 500 GB"))
            const cache = find(d.contentItem, o => o instanceof Segmented && o.options.length === 8)
            compare(cache.current, 3)
            mouseClick(button(cache, "7"))
            compare([Prefs.value("cacheLevels"), cache.current], [7, 6])
        }

        function test_close() {  // Esc, ×, a click outside; nothing changed by it
            let d = open()
            keyClick(Qt.Key_Escape)
            tryCompare(d, "visible", false)
            d = open({ page: "general" })
            mouseClick(find(d.contentItem, o => o instanceof IconButton && o.icon.name === "close"))
            tryCompare(d, "visible", false)
            d = open()
            mouseClick(host(d).contentItem, 10, 10)
            tryCompare(d, "visible", false)
            compare(JSON.stringify(Prefs.values), JSON.stringify(Prefs.defaults))
        }

        function test_keyboard() {  // Tab reaches the sections and the rows; Enter, Space press them
            const d = open()
            const general = button(d.contentItem, "Chung")
            general.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            compare(d.page, "general")
            let f = host(d).activeFocusItem
            for (let i = 0; i < 40 && !(f instanceof Toggle); i++) {
                keyClick(Qt.Key_Tab)
                f = host(d).activeFocusItem
            }
            verify(f instanceof Toggle, "Tab goes on to the rows")
            keyClick(Qt.Key_Space)
            compare(Prefs.value("placeSmartObject"), false)
            keyClick(Qt.Key_Return)
            compare(Prefs.value("placeSmartObject"), true)
            keyClick(Qt.Key_Escape)
            tryCompare(d, "visible", false)
        }

        // ---- on Main (U4) ---------------------------------------------------------------------------------------------
        function test_open() {  // Ctrl K and Edit › Thiết lập… on Hiệu năng, Help › Giấy phép… on its section; with no document too
            const w = openMain({ startState: "Main" })
            keySequence("Ctrl+K")
            let d = settingsOn(w)
            compare([d.page, heading(d)], ["performance", "Hiệu năng"])
            keyClick(Qt.Key_Escape)
            tryVerify(() => w.dialogs.length === 0, 2000, "gone once closed")
            mouseClick(menuRow(w, 1, "Thiết lập…"))
            d = settingsOn(w)
            compare(d.page, "performance")
            mouseClick(find(d.contentItem, o => o instanceof IconButton && o.icon.name === "close"))
            tryVerify(() => w.dialogs.length === 0)
            mouseClick(menuRow(w, 9, "Giấy phép…"))
            d = settingsOn(w)
            compare([d.page, heading(d)], ["about", "Báo lỗi, cập nhật, giấy phép"])
            mouseClick(w.contentItem, 5, 5)
            tryVerify(() => w.dialogs.length === 0, 2000, "a click outside")
            Docs.reset("Start")
            waitForRendering(w.contentItem)
            keySequence("Ctrl+K")
            d = settingsOn(w)
            compare(d.page, "performance")
            keyClick(Qt.Key_Escape)
            tryVerify(() => w.dialogs.length === 0)
        }

        function test_apply() {  // theme, accent and text size: the whole window at once, Thiết lập with it
            const w = openMain({ startState: "Main" })
            keySequence("Ctrl+K")
            const d = settingsOn(w)
            go(d, "interface")
            const bar = find(w.contentItem, o => o instanceof StatusBar)
            const top = find(w.contentItem, o => o instanceof TopBar)
            const mark = find(top, o => o instanceof Rectangle && Qt.colorEqual(o.color, "#2FB5B0"))  // the accent's square
            const word = find(top, o => o instanceof Text && o.text === "photo-beauty")
            const head = find(d.contentItem, o => o instanceof Text && o.text === "Giao diện" && o.x === 22)
            verify(Qt.colorEqual(w.color, "#161617") && Qt.colorEqual(bar.color, "#1c1c1e") && Qt.colorEqual(d.background.color, "#1c1c1e"))
            mouseClick(button(inRow(d, "theme", Segmented), "Sáng"))
            verify(!Theme.dark)
            verify(Qt.colorEqual(w.color, "#e7e7e8"), "the window")
            verify(Qt.colorEqual(bar.color, "#f3f3f3"), "its bars")
            verify(Qt.colorEqual(d.background.color, "#f3f3f3"), "Thiết lập")
            mouseClick(findAll(rowOf(d, "accent"), o => o instanceof T.AbstractButton)[3])
            verify(Qt.colorEqual(Theme.accent, "#E8664A"))
            verify(Qt.colorEqual(mark.color, "#E8664A"), "the top bar's mark")
            const sw = inRow(d, "channelsInColor", Toggle)
            mouseClick(sw)
            verify(Qt.colorEqual(find(sw, o => o instanceof Rectangle && o.width === 30).color, "#E8664A"), "a switch on")
            const wordPt = word.font.pointSize, headPt = head.font.pointSize
            mouseClick(button(inRow(d, "fontSize", Segmented), "Lớn"))
            compare(Theme.fontSize, "l")
            fuzzyCompare(word.font.pointSize, wordPt * 13.5 / 12, 0.001, "the window's text")
            fuzzyCompare(head.font.pointSize, headPt * 13.5 / 12, 0.001, "Thiết lập's")
            mouseClick(button(inRow(d, "fontSize", Segmented), "Nhỏ"))
            fuzzyCompare(word.font.pointSize, wordPt * 11 / 12, 0.001)
            mouseClick(button(inRow(d, "theme", Segmented), "Tối"))
            verify(Theme.dark && Qt.colorEqual(w.color, "#161617"))
        }

        function test_prefs_kept() {  // as snapone's: what Thiết lập set is back at the next start of the app
            const file = createTemporaryObject(fileC, root)
            file.setValue("json", "")  // not what a run before left
            let w = openMain({ startState: "Main", persist: true })
            verify(Prefs.store, "Main keeps Prefs")
            w.types = Object.assign({}, w.types, { ColorPickerDialog: pickerC })
            keySequence("Ctrl+K")
            let d = settingsOn(w)
            go(d, "interface")
            mouseClick(button(inRow(d, "theme", Segmented), "Sáng"))
            mouseClick(findAll(rowOf(d, "accent"), o => o instanceof T.AbstractButton)[2])
            mouseClick(button(inRow(d, "fontSize", Segmented), "Lớn"))
            go(d, "guides")
            mouseClick(inRow(d, "guideColor", Swatch))
            let picker = null
            tryVerify(() => (picker = w.dialogs.find(x => x.title === "Picker") || null) && picker.opened)
            compare([picker.params.color, picker.params.title], ["#29d3f5", "Guide"])
            picker.params.picked("#ff00aa")
            picker.close()
            tryVerify(() => w.dialogs.length === 1)
            go(d, "performance")
            mouseClick(find(d.contentItem, o => o instanceof IconButton && o.Accessible.name === "Đưa ổ D: lên"))
            mouseClick(button(find(d.contentItem, o => o instanceof Segmented && o.options.length === 8), "2"))
            keyClick(Qt.Key_Escape)
            tryVerify(() => w.dialogs.length === 0)
            compare(JSON.parse(file.value("json")).accent, "#E0A53A", "written as it changes")
            w.destroy()
            wait(50)

            // The next start: Theme and Prefs from their defaults, Prefs reads the file once Main keeps it.
            const s = Prefs.store
            Prefs.persist = false
            Prefs.store = null
            s.destroy()
            Prefs.reset()
            Theme.dark = true
            Theme.accent = Theme.accents[0]
            Theme.fontSize = "m"
            w = openMain({ startState: "Main", persist: true })
            verify(!Theme.dark && Qt.colorEqual(w.color, "#e7e7e8"), "light from the start")
            verify(Qt.colorEqual(Theme.accent, "#E0A53A"))
            compare(Theme.fontSize, "l")
            compare([Prefs.value("guideColor"), Prefs.value("cacheLevels"), Prefs.value("scratchDisks").map(x => x.name).join(" ")], ["#ff00aa", 2, "D: C: E:"])
            keySequence("Ctrl+K")
            d = settingsOn(w)
            const places = () => findAll(d.contentItem, o => o instanceof Text && o.visible && o.font.family === Theme.mono && o.width === 22).map(o => o.text)
            compare(places(), ["1", "2", ""])
            compare(find(d.contentItem, o => o instanceof Segmented && o.options.length === 8).current, 1)
            go(d, "interface")
            compare([inRow(d, "theme", Segmented).current, inRow(d, "fontSize", Segmented).current], [1, 2])
            compare(findAll(rowOf(d, "accent"), o => o instanceof T.AbstractButton).map(a => a.on), [false, false, true, false])
            go(d, "guides")
            verify(Qt.colorEqual(inRow(d, "guideColor", Swatch).color, "#ff00aa"))
        }
    }
}
