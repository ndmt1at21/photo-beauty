import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty
import "../qml/NewDocSpec.js" as Spec

// U16: the Start screen (Gần đây opened, found, sorted, taken off; recovery; presets), the New dialog (a unit or an
// orientation keeps the size, a resolution the printed size; Tạo makes the document as set), and their tables.
Item {
    id: root
    width: 1440
    height: 856
    Component { id: startC; StartScreen { anchors.fill: parent } }
    Component { id: newC; NewDocDialog {} }
    Component { id: mainC; Main {} }
    Component { id: signalSpy; SignalSpy {} }

    TestCase {
        name: "U16"
        when: windowShown

        function init() {
            Docs.reset("Start")
            Tools.reset()
            Prefs.reset()
            const tw = root.Window.window  // keys go to the active window: Main's took it in test_in_main
            tw.requestActivate()
            tryVerify(() => tw.active)
        }

        function items(from, pred) {
            const out = []
            const walk = (it) => {
                if (pred(it))
                    out.push(it)
                for (const c of it.children)
                    walk(c)
            }
            walk(from)
            return out
        }
        function one(from, pred, what) {
            const l = items(from, pred)
            verify(l.length > 0, what)
            return l[0]
        }
        function button(from, text) { return one(from, (i) => i instanceof AbstractButton && i.visible && i.text === text, text) }
        function start() {
            const s = createTemporaryObject(startC, root)
            waitForRendering(s)
            return s
        }
        function cards(s) {  // Gần đây's cards or rows, in order
            return items(s, (i) => i instanceof AbstractButton && i.modelData !== undefined && i.modelData.file !== undefined && i.modelData.order !== undefined)
        }
        function files(s) { return cards(s).map((c) => c.modelData.file) }
        function menuOf(s) { return s.resources.find((o) => o instanceof PopupMenu) }
        function newDialog() {
            const d = createTemporaryObject(newC, root)
            d.open()
            tryVerify(() => d.opened)
            waitForRendering(d.contentItem)
            return d
        }
        function nums(d) { return items(d.contentItem, (i) => i instanceof TextBox && i.decimals !== undefined) }  // NewDocDialog.Num: W, H, resolution
        function select(d, n) { return items(d.contentItem, (i) => i instanceof Select)[n] }  // unit, res unit, mode, bits, bg, profile
        function type(field, text) {
            field.forceActiveFocus()
            field.selectAll()
            for (const ch of text)
                keyClick(ch)
        }

        function test_tables() {
            compare(Spec.CATS.length, 8)
            compare(Spec.presets("print").length, 8)
            compare(Spec.num(10, 2), "10")
            compare(Spec.num(8.5, 2), "8,5")
            compare(Spec.num(1234.567, 2), "1.234,57")
            compare(Spec.sizeText(Spec.presets("print")[3]), "8,5 × 11 in · 300 ppi")
            compare(Spec.shown(2480, "Millimeters", 300), "209,97")
            compare(Spec.info(2480, 3508, 300, "RGB", 8), "26,1 MB · 8,27 × 11,69 in")
            compare(Spec.info(100, 100, 72, "Gray", 8), "10 KB · 1,39 × 1,39 in")
            compare(Spec.untitled([{ file: "Chưa đặt tên-1" }, { file: "x" }]), "Chưa đặt tên-2")
        }

        function test_recent_grid() {
            const s = start()
            compare(files(s), Docs.recent.map((r) => r.file))  // Ngày sửa, newest first
            const c = cards(s)[2]
            verify(items(c, (i) => i instanceof Text && i.text === "1.800 × 1.200 px · 410 KB").length === 1, "dims")
            mouseClick(c, c.width / 2, 60)
            tryVerify(() => Docs.docs.length === 1)
            compare(Docs.current.file, "san-pham.webp")
        }

        function test_recent_list_open_enter() {
            const s = start()
            mouseClick(items(s, (i) => i instanceof ToolToggle)[1])
            verify(s.list)
            const rows = cards(s)
            compare(rows.length, 8)
            compare(rows[0].height, 45)
            rows[6].forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            tryVerify(() => Docs.docs.length === 1)
            compare(Docs.current.file, "hoang-hon.tif")
        }

        function test_search() {
            const s = start()
            const box = one(s, (i) => i instanceof TextBox, "search")
            box.forceActiveFocus()
            for (const ch of "DUNG")
                keyClick(ch)
            compare(files(s), ["chân dung.psd", "chan-dung-ho.psd"])  // no diacritics, no case
            s.query = "chan dung"  // â is a
            compare(files(s), ["chân dung.psd"])
            s.query = "  ĐUNG "  // đ is d
            compare(files(s), ["chân dung.psd", "chan-dung-ho.psd"])
            s.query = "xyz"
            compare(files(s), [])
            verify(items(s, (i) => i instanceof Text && i.visible && i.text === "Không có tài liệu nào khớp “xyz”.").length === 1, "note")
            s.query = ""
            compare(files(s).length, 8)
        }

        function test_sort() {
            const s = start()
            const dd = one(s, (i) => i instanceof Dropdown, "sort")
            verify(dd.plain, "Start's .mi (U23)")
            dd.forceActiveFocus()
            keyClick(Qt.Key_Down)  // Tên, in Vietnamese order: a < â, so "chan-" before "chân"
            compare(s.sort, 1)
            compare(files(s), ["cau-vang.jpg", "chan-dung-ho.psd", "chân dung.psd", "cuoi-trang-an.psd", "halong-pano.psb", "hoang-hon.tif", "poster-tet.pbd", "san-pham.webp"])
            keyClick(Qt.Key_Down)  // Dung lượng, largest first
            compare(files(s), ["halong-pano.psb", "cuoi-trang-an.psd", "chan-dung-ho.psd", "chân dung.psd", "poster-tet.pbd", "hoang-hon.tif", "san-pham.webp", "cau-vang.jpg"])
            s.sort = 0
            compare(files(s), dateOrder())
            compare(Spec.byName("ăn", "an"), 1)
            compare(Spec.byName("ăn", "ân"), -1)
            compare(Spec.byName("đa", "dz"), 1)
            compare(Spec.byName("ơ", "ô"), 1)
            compare(Spec.byName("Bé", "be"), -1)  // a tone breaks a tie only
        }
        function dateOrder() { return ["chân dung.psd", "poster-tet.pbd", "san-pham.webp", "halong-pano.psb", "cuoi-trang-an.psd", "chan-dung-ho.psd", "hoang-hon.tif", "cau-vang.jpg"] }

        function test_forget() {
            const s = start()
            const menu = menuOf(s)
            let c = cards(s)[1]
            const more = one(c, (i) => i instanceof IconButton, "more")
            mouseClick(more)
            tryVerify(() => menu.opened)
            compare(s.menuFile, "poster-tet.pbd")
            waitForRendering(s)
            compare(menu.itemAt(4).text, "Bỏ khỏi danh sách")
            mouseClick(menu.itemAt(4))
            tryVerify(() => !menu.visible)
            tryVerify(() => files(s).length === 7)
            verify(files(s).indexOf("poster-tet.pbd") < 0)
            verify(!Docs.recent.some((r) => r.file === "poster-tet.pbd"))
            compare(s.menuFile, "")
            c = cards(s)[0]  // Delete on the card Tab reached
            c.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Delete)
            tryVerify(() => files(s).length === 6)
            compare(files(s)[0], "san-pham.webp")
            Docs.clearRecent()
            verify(items(s, (i) => i instanceof Text && i.visible && i.text === "Chưa có tài liệu nào mở gần đây.").length === 1, "no recent")
        }

        function test_menu_right_click_copy() {
            const s = start()
            const menu = menuOf(s)
            verify(menu.plain, "Start's .mi (U23)")
            const c = cards(s)[3]
            mouseClick(c, c.width / 2, 40, Qt.RightButton)
            tryVerify(() => menu.opened)
            compare(s.menuFile, "halong-pano.psb")
            waitForRendering(s)
            compare(menu.itemAt(2).text, "Sao chép đường dẫn")
            mouseClick(menu.itemAt(2))
            tryVerify(() => !menu.visible)
            compare(s.copied, "halong-pano.psb")
            mouseClick(c, c.width / 2, 40, Qt.RightButton)
            tryVerify(() => menu.opened)
            compare(menu.itemAt(2).text, "Đã sao chép đường dẫn")
            waitForRendering(s)
            mouseClick(menu.itemAt(0))  // Mở
            tryVerify(() => Docs.docs.length === 1)
            compare(Docs.current.file, "halong-pano.psb")
            compare(Docs.current.w, 36000)
        }

        function test_recovery() {
            const s = start()
            compare(Docs.recovery.length, 2)
            mouseClick(items(s, (i) => i instanceof Btn && i.text === "Bỏ")[1])  // the second row's
            tryVerify(() => Docs.recovery.length === 1)
            compare(Docs.recovery[0].file, "chân dung-final.psd")
            waitForRendering(s)
            mouseClick(button(s, "Mở"))
            tryVerify(() => Docs.docs.length === 1)
            compare(Docs.current.file, "chân dung-final.psd")
            verify(Docs.current.dirty)
            compare(Docs.recovery.length, 0)
            waitForRendering(s)
            compare(items(s, (i) => i instanceof Btn && i.visible && i.text === "Bỏ").length, 0)
        }

        function test_preset_creates() {
            const s = start()
            mouseClick(button(s, "Full HD"))
            compare(Docs.docs.length, 1)
            compare(Docs.current.w, 1920)
            compare(Docs.current.h, 1080)
            compare(Docs.current.ppi, 72)
            compare(Docs.current.file, "Chưa đặt tên-1")
            mouseClick(button(s, "A4 · 300 ppi"))
            compare(Docs.current.file, "Chưa đặt tên-2")
            compare([Docs.current.w, Docs.current.h, Docs.current.ppi], [2480, 3508, 300])
        }

        function test_start_new() {  // Tạo mới… and Tùy chỉnh… open New; without Main, the screen's own
            const s = start()
            for (const name of ["Tạo mới…", "Tùy chỉnh…"]) {
                mouseClick(button(s, name))
                const ov = s.Overlay.overlay
                tryVerify(() => items(ov, (i) => i instanceof Text && i.text === "Chi tiết").length === 1)
                waitForRendering(ov)
                mouseClick(button(ov, name === "Tạo mới…" ? "Tạo" : "Hủy"))
                tryVerify(() => items(ov, (i) => i instanceof Text && i.text === "Chi tiết").length === 0)
            }
            compare(Docs.docs.length, 1)
            compare(Docs.current.w, 2480)
            compare(Docs.current.h, 3508)
            compare(Docs.current.ppi, 300)
        }

        function test_in_main() {  // Main shows the start screen without a document; File › New, Tạo mới… open New through it
            const w = createTemporaryObject(mainC, root, { startState: "Start" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            const s = one(w.contentItem, (i) => i instanceof StartScreen, "start screen")
            verify(s.visible)
            compare([s.width, s.height], [1440, 856])
            const menus = one(w.contentItem, (i) => i instanceof AppMenu, "menu bar")
            verify(menus.menuAt(0).plain, "Start's .mi (U23)")
            keySequence("Ctrl+N")  // File › Tạo mới…
            tryVerify(() => w.dialogs.length === 1)
            verify(w.dialogs[0] instanceof NewDocDialog)
            keyClick(Qt.Key_Escape)
            tryVerify(() => w.dialogs.length === 0)
            const acted = signalSpy.createObject(root, { target: w, signalName: "acted" })
            mouseClick(button(s, "Mở…"))  // File › Mở…: Main's file dialog
            compare(acted.count, 1)
            compare(acted.signalArguments[0][0].act, "@openFile")
            let files = null
            for (const o of w.contentItem.resources)
                if (o.fileMode !== undefined)
                    files = o
            tryVerify(() => files.visible)
            files.reject()
            tryVerify(() => !files.visible)
            mouseClick(button(s, "Tạo mới…"))
            tryVerify(() => w.dialogs.length === 1)
            waitForRendering(w.contentItem)
            mouseClick(button(w.dialogs[0].footer, "Tạo"))
            tryVerify(() => Docs.docs.length === 1)
            tryVerify(() => !s.visible)
            compare(Docs.current.w, 2480)
            verify(!menus.menuAt(0).plain, "Main's .mi")
            acted.destroy()
        }

        function test_new_box_heights() {  // the artboard's 28 px boxes (U23): W, H, resolution and the drop-downs
            const d = newDialog(), boxes = nums(d).concat(items(d.contentItem, (i) => i instanceof Select && i.visible))
            verify(boxes.length >= 8)
            compare(boxes.map((b) => b.height), boxes.map(() => 28))
            const sizes = items(d.contentItem, (i) => i instanceof Text && i.visible && i.font.family === Theme.mono && i.parent instanceof Column && i.parent.parent instanceof AbstractButton)
            verify(sizes.length >= 4, "the cards' sizes")
            compare(sizes.map((t) => t.height), sizes.map((t) => 14 * t.lineCount), "a card's size: lines of 10.5 px × 1.35 (U23)")
            const note = one(d.footer, (i) => i instanceof Text && i.text === "Nền: trắng, đen, màu sau, trong suốt hay màu tùy chọn.", "the footer's note (U23)")
            compare([note.x, note.font.pointSize], [16, Theme.pt(11)])
            verify(Qt.colorEqual(note.color, Theme.t3))
        }
        function test_new_units() {
            const d = newDialog()
            const f = nums(d)
            compare(f.length, 3)
            compare(f[0].text, "2.480")
            compare(f[1].text, "3.508")
            const unit = select(d, 0)
            unit.forceActiveFocus()
            keyClick(Qt.Key_Down)  // Inches
            compare(d.d.unit, "Inches")
            compare(d.d.w, 2480)  // the size stays
            compare(f[0].text, "8,27")
            unit.picked("Centimeters")
            compare(d.d.w, 2480)
            compare(f[0].text, "21")  // 20,997 cm
            compare(f[1].text, "29,7")
            type(f[0], "10")
            fuzzyCompare(d.d.w, 1181.1, 0.05)
            type(f[1], "2in+1cm")  // a sum and units, at 300 ppi
            fuzzyCompare(d.d.h, 718.1, 0.05)
            keyClick(Qt.Key_Up)
            fuzzyCompare(d.d.h, 718.1 + 300 / 2.54, 0.05)
            unit.forceActiveFocus()
            compare(f[1].text, "7,08")  // a field shows the model again once left
            for (const u of ["Millimeters", "Points", "Picas", "Pixels"]) {
                unit.picked(u)
                fuzzyCompare(d.d.w, 1181.1, 0.05)
            }
            compare(f[0].text, "1.181")
            type(f[0], "abc")  // not a number: nothing changes
            fuzzyCompare(d.d.w, 1181.1, 0.05)
            type(f[0], "999999")
            compare(d.d.w, 300000)  // at most 300.000 px
            compare(Docs.docs.length, 0)
        }

        function test_new_resolution_orientation() {
            const d = newDialog()
            const f = nums(d)
            compare(f[2].text, "300")
            type(f[2], "150")  // in pixels: the pixels stay
            compare(d.d.w, 2480)
            compare(d.d.ppi, 150)
            d.d = Object.assign({}, d.d, { unit: "Centimeters" })
            type(f[2], "300")  // in cm: the printed size stays
            compare(d.d.w, 4960)
            compare(f[0].text, "41,99")
            select(d, 1).forceActiveFocus()  // a field shows the model once left
            select(d, 1).picked("Pixels/Centimeter")
            compare(f[2].text, "118,11")
            type(f[2], "100")
            fuzzyCompare(d.d.ppi, 254, 1e-9)
            const portrait = one(d.contentItem, (i) => i instanceof IconButton && i.icon.name === "crop_portrait", "portrait")
            const landscape = one(d.contentItem, (i) => i instanceof IconButton && i.icon.name === "crop_landscape", "landscape")
            verify(portrait.on && !landscape.on)
            const w = d.d.w, h = d.d.h
            mouseClick(landscape)
            compare(d.d.w, h)
            compare(d.d.h, w)
            verify(!portrait.on && landscape.on)
            mouseClick(landscape)  // already
            compare(d.d.w, h)
            mouseClick(portrait)
            compare(d.d.w, w)
            d.d = Object.assign({}, d.d, { w: 500, h: 500 })
            mouseClick(landscape)  // a square has no other way
            compare(d.d.w, 500)
            verify(portrait.on)
        }

        function test_new_create() {
            const d = newDialog()
            const info = one(d.contentItem, (i) => i instanceof Text && i.text.indexOf(" MB · ") > 0, "info")
            compare(info.text, "26,1 MB · 8,27 × 11,69 in")
            mouseClick(button(d.contentItem, "Web"))
            compare(d.cat, "web")
            waitForRendering(d.contentItem)
            mouseClick(button(d.contentItem, "Web lớn"))
            compare(d.sel, "web 1")
            compare(d.d.name, "Web lớn")
            compare(d.d.w, 1920)
            compare(d.d.ppi, 72)
            const mode = select(d, 2)
            mode.forceActiveFocus()
            keyClick(Qt.Key_Down)  // Grayscale: its profiles
            compare(d.d.mode, "Gray")
            compare(d.d.profile, "Gray Gamma 2.2")
            compare(select(d, 5).options.length, 3)
            select(d, 3).picked(16)
            compare(info.text, "4,1 MB · 26,67 × 15 in")
            mouseClick(button(d.footer, "Tạo"))
            tryVerify(() => !d.visible)
            compare(Docs.docs.length, 1)
            const doc = Docs.current
            compare(doc.file, "Web lớn")
            compare([doc.w, doc.h, doc.ppi, doc.mode, doc.bits, doc.profile], [1920, 1080, 72, "Gray", 16, "Gray Gamma 2.2"])
            compare(doc.layers.length, 1)
            verify(doc.layers[0].props.background)
        }

        function test_new_backgrounds() {
            Tools.bg = "#123456"
            const want = { white: "#ffffff", black: "#000000", background: "#123456", custom: "#e9c7b3" }
            for (const bg of ["white", "black", "background", "transparent", "custom"]) {
                const d = newDialog()
                select(d, 4).picked(bg)
                const swatch = one(d.contentItem, (i) => i instanceof AbstractButton && i.ToolTip.text === "Màu nền", "swatch")
                if (want[bg])
                    compare(swatch.fill, want[bg])
                keyClick(Qt.Key_Return)  // in the name field: Tạo
                tryVerify(() => !d.visible)
                const l = Docs.current.layers[0]
                compare(l.props.background, bg !== "transparent", bg)
                if (want[bg])
                    compare(l.props.color, want[bg], bg)
            }
            compare(Docs.docs.length, 5)
        }

        function test_new_cancel() {
            let d = newDialog()
            type(nums(d)[0], "1000")
            mouseClick(button(d.footer, "Hủy"))
            tryVerify(() => !d.visible)
            d = newDialog()
            keyClick(Qt.Key_Escape)
            tryVerify(() => !d.visible)
            d = newDialog()
            const close = one(d.header, (i) => i instanceof IconButton, "×")
            mouseClick(close)
            tryVerify(() => !d.visible)
            compare(Docs.docs.length, 0)
        }

        function test_new_preset_kept() {  // Đã lưu lives in Prefs, kept between starts as Prefs keeps its JSON
            compare(Prefs.value("newDocPresets"), [])
            let d = newDialog()
            mouseClick(one(d.contentItem, (i) => i instanceof IconButton && i.icon.name === "save", "save"))
            compare(Prefs.value("newDocPresets").length, 1)
            const json = JSON.stringify(Prefs.values)
            d.close()
            Prefs.reset()
            compare(Prefs.value("newDocPresets"), [])
            Prefs.restore(json)
            compare(Prefs.value("newDocPresets").length, 1)
            compare(Prefs.value("newDocPresets")[0].n, "Chưa đặt tên-1")
            d = newDialog()
            mouseClick(button(d.contentItem, "Đã lưu"))
            compare(d.cards.length, Spec.SAVED.length + 1)
            waitForRendering(d.contentItem)
            mouseClick(button(d.contentItem, "Chưa đặt tên-1"))
            compare(d.sel, "saved " + Spec.SAVED.length)
            compare([d.d.w, d.d.h, d.d.unit], [2480, 3508, "Pixels"])
        }

        function test_prefs_keys() {  // the keys U16 put in Prefs: New's saved presets, U5's two-column toolbar
            compare(Prefs.defaults.newDocPresets, [])
            compare(Prefs.defaults.twoColumnTools, false)
            compare(Prefs.value("twoColumnTools"), false)
            verify(Prefs.panes.workspace.some((r) => r.kind === "toggle" && r.key === "twoColumnTools"))
            Prefs.set("twoColumnTools", true)
            const json = JSON.stringify(Prefs.values)
            Prefs.reset()
            compare(Prefs.value("twoColumnTools"), false)
            Prefs.restore(json)
            compare(Prefs.value("twoColumnTools"), true)
        }

        function test_new_save_preset() {
            let d = newDialog()
            type(one(d.contentItem, (i) => i instanceof TextBox && i.decimals === undefined, "name"), "Bia sach")
            d.d = Object.assign({}, d.d, { unit: "Centimeters", bits: 16 })
            mouseClick(one(d.contentItem, (i) => i instanceof IconButton && i.icon.name === "save", "save"))
            compare(d.cat, "saved")
            compare(d.sel, "saved 4")
            compare(d.cards.length, 5)
            const p = d.cards[4]
            compare([p.n, p.u, p.bits], ["Bia sach", "Centimeters", 16])
            fuzzyCompare(p.w, 2480 / 300 * 2.54, 1e-9)
            d.close()
            d = newDialog()  // kept for the next New
            mouseClick(button(d.contentItem, "Đã lưu"))
            waitForRendering(d.contentItem)
            mouseClick(button(d.contentItem, "Bia sach"))
            compare(d.d.unit, "Centimeters")
            fuzzyCompare(d.d.w, 2480, 1e-9)
            compare(d.d.bits, 16)
            mouseClick(button(d.contentItem, "Mockup trong suốt"))
            compare(d.d.bg, "transparent")
        }
    }
}
