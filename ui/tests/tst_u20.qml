import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtTest
import PhotoBeauty
import "../qml/MenuSpec.js" as MenuSpec
import "../qml/ToolSpec.js" as ToolSpec

// U20: the notices (Notices.dc.html) and Phím tắt (Shortcuts.dc.html). Closing what is not saved asks, a tab, all of
// them, the window, and the three buttons do what they say; each notice's choices; Phím tắt's tabs and search; every key
// it lists runs (a Shortcut of the window has it, and fires).
Item {
    id: root
    width: 200
    height: 100
    Component { id: mainC; Main {} }
    Component { id: saveC; SaveChangesDialog {} }
    Component { id: profileC; ProfileMismatchDialog {} }
    Component { id: fontsC; MissingFontsDialog {} }
    Component { id: psdC; PsdNoticeDialog {} }
    Component { id: recoveryC; RecoveryDialog {} }
    Component { id: modelC; ModelDownloadDialog {} }
    Component { id: pasteC; TextEdit {} }
    Component { id: hostC; Window { width: 1440; height: 900; visible: true; color: Theme.bg } }
    // Stand-ins for the dialogs and spaces the menus open (Main.types is its cache of the module's types), as tst_u4.
    Component {
        id: probeDialogC
        DialogBox {
            property var params: ({})
            title: "Probe"
            buttons: [{ text: "OK", primary: true, act: () => close() }]
        }
    }
    Component {
        id: probeSpaceC
        Item {
            property var params: ({})
            signal finished(bool ok)
        }
    }

    TestCase {
        name: "U20"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanupTestCase() { init() }

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
        function frame(d) { return d.contentItem.parent }  // the box: header, content, footer
        function button(d, text) { return find(frame(d), o => o instanceof T.AbstractButton && o.text === text && o.visible) }
        function texts(d) { return findAll(frame(d), o => o instanceof Text && o.visible && o.text !== "").map(t => t.text) }
        function has(d, text) { return texts(d).some(t => t.indexOf(text) >= 0) }
        function openMain(props) {  // as tst_u4: active and drawn
            const w = createTemporaryObject(mainC, root, props || {})
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function dialogOn(w, type) {  // the one Main opened, faded in
            let d = null
            tryVerify(() => (d = w.dialogs.find(x => x instanceof type) || null) && d.opened)
            return d
        }
        function box(c, params) {  // a dialog on its own, in a window of Main's size
            const host = createTemporaryObject(hostC, root)
            const d = createTemporaryObject(c, host.contentItem, { params: params || {} })
            d.open()
            tryCompare(d, "opened", true)
            return d
        }
        function click(d, text) {
            const b = button(d, text)
            verify(b, text)
            waitForPolish(b.parent)  // buttons just made (the footer's after a phase) take their place on the row's polish
            mouseClick(b)
        }

        // ---- Lưu thay đổi -----------------------------------------------------------------------------------------
        function test_close_asks() {  // a tab not saved: Hủy keeps it, Không lưu closes it, Lưu saves it, then closes it
            const w = openMain({ startState: "Main" })
            Docs.cancelJob()  // the artboard's save of chân dung.psd
            const d0 = Docs.current
            verify(d0.dirty)
            keySequence("Ctrl+W")
            let d = dialogOn(w, SaveChangesDialog)
            compare(d.params.docs, [d0])
            verify(has(d, "Lưu thay đổi của “chân dung.psd”?"))
            verify(has(d, "Không lưu thì mất 9 bước chỉnh từ lần lưu trước."))  // its History past Mở
            verify(!has(d, "Còn"), "one document")
            click(d, "Hủy")
            tryVerify(() => !w.dialogs.length)
            compare(Docs.docs.length, 3)
            verify(!Docs.job.on)

            keySequence("Ctrl+W")
            mouseClick(button(dialogOn(w, SaveChangesDialog), "Không lưu"))
            tryVerify(() => !w.dialogs.length)
            compare(Docs.docs.indexOf(d0), -1)
            verify(!Docs.job.on, "not saved")

            const d2 = Docs.docs.find(x => x.dirty)  // hoang-hon.tif, opened unsaved: no step yet
            Docs.select(Docs.docs.indexOf(d2))
            keySequence("Ctrl+W")
            d = dialogOn(w, SaveChangesDialog)
            verify(has(d, "Lưu thay đổi của “hoang-hon.tif”?"))
            verify(has(d, "Không lưu thì mất các thay đổi từ lần lưu trước."))
            click(d, "Lưu")
            tryVerify(() => !w.dialogs.length)
            compare(Docs.docs.indexOf(d2), -1)
            compare([Docs.job.on, Docs.job.verb, Docs.job.name, Docs.job.p], [true, "save", "hoang-hon.tif", 0])

            keySequence("Ctrl+W")  // san-pham.webp is saved: no question
            compare(Docs.docs.length, 0)
            verify(!w.dialogs.length)
        }
        function test_close_keys() {  // Esc is Hủy; Tab reaches Lưu, Enter presses it
            const w = openMain({ startState: "Main" })
            Docs.cancelJob()
            const d0 = Docs.current
            keySequence("Ctrl+W")
            dialogOn(w, SaveChangesDialog)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !w.dialogs.length)
            compare(Docs.docs.length, 3)
            keySequence("Ctrl+W")
            const d = dialogOn(w, SaveChangesDialog), save = button(d, "Lưu")
            for (let i = 0; i < 4 && !save.activeFocus; i++)
                keyClick(Qt.Key_Tab)
            verify(save.activeFocus, "Tab to Lưu")
            keyClick(Qt.Key_Return)
            tryVerify(() => !w.dialogs.length)
            compare(Docs.docs.indexOf(d0), -1)
            compare([Docs.job.verb, Docs.job.name], ["save", "chân dung.psd"])
        }
        function test_quit_asks() {  // the window, with two not saved: one after the other, then it closes; Hủy keeps it
            const w = openMain({ startState: "Main" })
            Docs.cancelJob()
            const dirty = Docs.docs.filter(x => x.dirty)
            compare(dirty.map(x => x.file), ["chân dung.psd", "hoang-hon.tif"])
            w.close()
            click(dialogOn(w, SaveChangesDialog), "Hủy")
            tryVerify(() => !w.dialogs.length)
            verify(w.visible)
            compare(Docs.docs.length, 3)

            w.close()
            const d = dialogOn(w, SaveChangesDialog)
            compare(d.params.docs, dirty)
            verify(has(d, "Lưu thay đổi của “chân dung.psd”?"))
            compare(d.after, 1)
            verify(!has(d, "Còn"), "the artboard's one sentence (U23)")
            click(d, "Lưu")
            tryVerify(() => has(d, "Lưu thay đổi của “hoang-hon.tif”?"))
            verify(!has(d, "Còn"))
            compare([Docs.job.verb, Docs.job.name], ["save", "chân dung.psd"])
            verify(w.visible)
            click(d, "Không lưu")
            tryVerify(() => !w.visible, 1000, "closed")
        }
        function test_close_all_asks() {  // Đóng tất cả: each not saved, then all close
            const w = openMain({ startState: "Main" })
            Docs.cancelJob()
            keySequence("Ctrl+Alt+W")
            const d = dialogOn(w, SaveChangesDialog)
            compare(d.params.docs.length, 2)
            click(d, "Không lưu")
            tryVerify(() => has(d, "hoang-hon.tif"))
            compare(Docs.docs.length, 3, "nothing closed before the last answer")
            click(d, "Không lưu")
            tryVerify(() => !w.dialogs.length)
            compare(Docs.docs.length, 0)
            verify(!Docs.job.on)
        }

        function test_save_held() {  // no docs given (--state SaveChanges): those not saved, held as a save ends
            Docs.cancelJob()
            const d = box(saveC)
            const first = d.doc
            compare(d.after, 1)
            click(d, "Lưu")
            while (Docs.job.run)
                Docs.tick()
            verify(!first.dirty, "saved")
            verify(has(d, "Lưu thay đổi của “hoang-hon.tif”?"))
            click(d, "Không lưu")
            tryCompare(d, "visible", false)
        }

        // ---- the notices ------------------------------------------------------------------------------------------
        function test_profile() {  // three choices, the embedded profile first; OK gives the one picked, Hủy nothing
            let got = ""
            let d = box(profileC, { picked: c => got = c })
            verify(has(d, "cuoi-trang-an.psd"))
            verify(has(d, "Adobe RGB (1998)"))
            verify(has(d, "sRGB IEC61966-2.1"))
            const radios = findAll(d.contentItem, o => o.objectName === "radio")
            compare(radios.map(r => r.text), ["Dùng hồ sơ nhúng", "Chuyển màu sang không gian làm việc", "Bỏ hồ sơ, không quản lý màu"])
            compare(radios.map(r => r.checked), [true, false, false])
            mouseClick(radios[2])
            compare(radios.map(r => r.checked), [false, false, true])
            click(d, "OK")
            tryCompare(d, "visible", false)
            compare(got, "discard")
            d = box(profileC, { picked: c => got = c })
            mouseClick(findAll(d.contentItem, o => o.objectName === "radio")[1])
            click(d, "Hủy")
            tryCompare(d, "visible", false)
            compare(got, "discard", "Hủy picks nothing")
        }
        function test_missing_fonts() {  // a font for each missing one; Thay font is one step, Để sau none
            const doc = Docs.current, n = doc.history.length
            let d = box(fontsC)
            compare(d.title, "Thiếu 2 font")
            verify(has(d, "3 layer chữ của <font"))
            verify(has(d, "poster-tet.pbd"))
            verify(has(d, "Tết Bính Ngọ 2026, Ưu đãi 30%"))
            const sels = findAll(d.contentItem, o => o instanceof Select)
            compare(sels.map(s => s.displayText), ["Be Vietnam Pro Bold", "Space Grotesk Regular"])
            compare(sels[0].parent.parent.height, 1 + 8 + 2 * 17 + 8, ".lr: its rule, 8, two lines of 12 px × 1.4 (the font, its layers), 8 (U23)")
            mouseClick(sels[0])
            tryVerify(() => sels[0].popup.opened)
            mouseClick(find(sels[0].popup.contentItem, o => o instanceof T.ItemDelegate && o.text === "Roboto Bold"))
            tryCompare(sels[0], "displayText", "Roboto Bold")
            tryVerify(() => !sels[0].popup.visible)  // faded out: a press now is the button's
            click(d, "Thay font")
            tryCompare(d, "visible", false)
            compare(doc.history.length, n + 1)
            compare(doc.history[n].name, "Thay font")
            compare(doc.history[n].cmds[0].fonts, { "SVN-Gilroy Bold": "Roboto Bold", "UTM Avo": "Space Grotesk Regular" })
            d = box(fontsC)
            click(d, "Để sau")
            tryCompare(d, "visible", false)
            compare(doc.history.length, n + 1)
        }
        function test_psd_notice() {  // the layers and what of them; Chép danh sách copies them
            const d = box(psdC)
            compare(d.title, "PSD: 3 layer có phần chưa sửa được")
            verify(has(d, "poster-tet.psd"))
            const lines = findAll(d.contentItem, o => o instanceof Text && o.visible && o.text !== "")
            compare(lines.map(t => t.height), lines.map(t => 17 * t.lineCount), "Notices' 12 px × 1.4 a line, whole pixels (U23)")
            for (const l of ["Tiêu đề (layer chữ)", "Ưu đãi 30% (layer chữ)", "Logo khách (smart object)", "Smart filter Gaussian Blur: chưa sửa được, giữ nguyên khi lưu."])
                verify(has(d, l), l)
            click(d, "Chép danh sách")
            verify(button(d, "Đã chép"))
            const paste = createTemporaryObject(pasteC, root)
            paste.paste()
            compare(paste.text.split("\n")[0], "poster-tet.psd")
            compare(paste.text.split("\n")[3], "Logo khách (smart object): Smart filter Gaussian Blur: chưa sửa được, giữ nguyên khi lưu.")
            mouseClick(find(frame(d), o => o instanceof IconButton && o.icon.name === "close"))  // ×
            tryCompare(d, "visible", false)
        }
        function test_recovery() {  // Bỏ drops one, Mở opens one and closes the box, Mở tất cả opens all
            let d = box(recoveryC)
            compare(d.title, "Khôi phục tài liệu chưa lưu")
            const inBtn = o => !!o && (o instanceof Btn || inBtn(o.parent))
            const lines = findAll(d.contentItem, o => o instanceof Text && o.visible && o.text !== "" && !inBtn(o))
            compare(lines.map(t => t.height), lines.map(t => 17 * t.lineCount), "Notices' 12 px × 1.4 a line, whole pixels (U23)")
            verify(has(d, "14:41"))
            verify(has(d, "chân dung-final.psd"))
            verify(has(d, "tự lưu 14:38 hôm nay"))
            const n = Docs.docs.length
            mouseClick(find(d.contentItem, o => o instanceof Btn && o.text === "Bỏ"))
            tryVerify(() => !has(d, "chân dung-final.psd"))
            compare(Docs.recovery.map(r => r.file), ["Chưa đặt tên-3"])
            compare(Docs.docs.length, n, "Bỏ opens nothing")
            click(d, "Mở")
            tryCompare(d, "visible", false)
            tryVerify(() => Docs.docs.length === n + 1)
            compare([Docs.current.file, Docs.current.dirty, Docs.recovery.length], ["Chưa đặt tên-3", true, 0])
            d = box(recoveryC)  // none left
            verify(has(d, "Đã bỏ hết bản tự lưu."))
            verify(!button(d, "Mở tất cả"))
            click(d, "Đóng")
            tryCompare(d, "visible", false)

            Docs.reset()
            d = box(recoveryC)
            click(d, "Mở tất cả")
            tryCompare(d, "visible", false)
            compare(Docs.docs.slice(-2).map(x => x.file), ["chân dung-final.psd", "Chưa đặt tên-3"])
            compare(Docs.recovery.length, 0)
            Docs.reset()
            d = box(recoveryC)
            click(d, "Để sau")
            tryCompare(d, "visible", false)
            compare(Docs.recovery.length, 2, "kept for later")
        }
        function test_model_download() {  // name, size, licence; Tải downloads it into Prefs; Hủy stops it
            Prefs.set("modelBiRefNet", false)
            let d = box(modelC)
            compare(d.title, "Select Subject cần một model")
            for (const t of ["BiRefNet_lite", "85 MB", "MIT", "Xóa được ở Thiết lập › AI."])
                verify(has(d, t), t)
            click(d, "Tải 85 MB")
            compare(d.phase, "load")
            tryVerify(() => has(d, " / 85 MB"))
            click(d, "Hủy")
            tryCompare(d, "visible", false)
            verify(!Prefs.value("modelBiRefNet"), "stopped")
            d = box(modelC)
            click(d, "Tải 85 MB")
            tryCompare(d, "visible", false, 4000)
            verify(Prefs.value("modelBiRefNet"), "downloaded")
        }
        function run(d) {  // the download's ticks at once, till it fails or ends (the box closes)
            for (let i = 0; i < 17 && d.phase === "load" && d.opened; i++)
                d.step()
            verify(d.phase === "fail" || !d.opened)
        }
        function test_model_fails() {  // the connection lost at 32 MB, then Thử lại; a wrong SHA-256, then Đóng
            Prefs.set("modelBiRefNet", false)
            let d = box(modelC, { fail: "network" })
            click(d, "Tải 85 MB")
            run(d)
            compare(d.title, "Tải không xong: mất kết nối")
            compare(d.icon, "error")
            const lost = find(d.contentItem, o => o instanceof Text && o.visible && o.text.indexOf("dừng ở 32 / 85 MB") >= 0)
            compare([lost.lineCount, lost.height], [2, 2 * 17], "Notices' 12 px × 1.4 a line, whole pixels (U23)")
            verify(has(d, "BiRefNet_lite dừng ở 32 / 85 MB. Select Subject dùng được khi tải xong; kiểm tra mạng rồi thử lại."))
            verify(!find(frame(d), o => o instanceof IconButton && o.visible), "no ×")
            verify(!Prefs.value("modelBiRefNet"))
            click(d, "Thử lại")
            run(d)
            tryCompare(d, "visible", false)
            verify(Prefs.value("modelBiRefNet"))

            Prefs.set("modelBiRefNet", false)
            d = box(modelC, { fail: "sha" })
            click(d, "Tải 85 MB")
            run(d)
            compare(d.title, "Tải không xong: sai SHA-256")
            verify(has(d, "Tệp tải về sai SHA-256, đã xóa. Thử lại sẽ tải lại BiRefNet_lite (85 MB) từ đầu."))
            click(d, "Đóng")
            tryCompare(d, "visible", false)
            verify(!Prefs.value("modelBiRefNet"))
        }

        // ---- Phím tắt -----------------------------------------------------------------------------------------------
        function names(d) { return d.shown.map(g => g[1].map(r => r.n)).reduce((a, b) => a.concat(b), []) }
        function type(s) {
            for (const ch of s)
                keyClick(ch)
        }
        function test_shortcuts_tabs() {  // three tabs from MenuSpec, the panels' menus, ToolSpec; two columns
            const w = openMain({ startState: "Main" })
            keySequence("Ctrl+Shift+Alt+K")
            const d = dialogOn(w, ShortcutsDialog)
            const seg = find(d.contentItem, o => o instanceof Segmented)
            compare(seg.options, ["Menu ứng dụng", "Menu panel", "Công cụ", "Khác"])
            const keyed = MenuSpec.keyed()
            compare(d.shown.map(g => g[0]), ["File", "Edit", "Image", "Layer", "Select", "Filter", "View", "Window", "Help"])
            compare(names(d).length, keyed.length)
            compare(names(d).slice(0, 3), ["Tạo mới…", "Mở…", "Đóng"])
            verify(names(d).indexOf("Mới › Layer…") >= 0, "a submenu's item named after it")
            // Every Vietnamese label has its English name, so a search in English finds it.
            const rows = d.lists[0].map(g => g[1]).reduce((a, b) => a.concat(b), [])
            keyed.forEach((k, i) => verify(!/[À-ỹĐđ]/.test(k.item.l) || rows[i].en !== "", k.item.l + " has no English name"))
            const save = find(d.contentItem, o => o instanceof Text && o.text === "Lưu thành…")
            verify(save && save.visible)
            const row = save.parent
            compare(findAll(row, o => o instanceof Text && o.visible).map(t => t.text), ["Lưu thành…", "Save As", "Ctrl", "⇧", "S"])

            mouseClick(find(seg, o => o instanceof T.AbstractButton && o.text === "Menu panel"))
            compare(d.tab, 1)
            compare(d.lists[1].slice(0, 4).map(g => g[0]), ["Layers", "History", "Channels", "Brushes"], "the artboard's order first (U23)")
            const channels = d.shown.find(g => g[0] === "Channels")  // its menu, keys or none
            compare(channels[1].map(r => r.n), ["Kênh mới", "Xóa kênh", "Lưu vùng chọn thành kênh", "Nạp kênh làm vùng chọn"])
            compare(channels[1][0].en, "New Channel")
            for (const g of d.lists[1])  // each panel's Vietnamese item has its English name too
                g[1].forEach(r => verify(!/[À-ỹĐđ]/.test(r.n) || r.en !== "", g[0] + " › " + r.n + " has no English name"))

            mouseClick(find(seg, o => o instanceof T.AbstractButton && o.text === "Công cụ"))
            compare(d.shown.map(g => g[0]), ToolSpec.KEYS.map(g => g[0]))
            compare(names(d).length, 39)
            compare(d.columns.map(c => c.map(g => g[0])), [["Chọn, cắt, lấy mẫu", "Vẽ và sửa"], ["Chữ và hình", "Xem", "Màu và cọ"]])  // as CSS columns
            keyClick(Qt.Key_Escape)
            tryVerify(() => !w.dialogs.length)
        }
        function test_shortcuts_search() {  // by name without marks, English name, keys; nothing found says so
            const w = openMain({ startState: "Main" })
            keySequence("Ctrl+Shift+Alt+K")
            const d = dialogOn(w, ShortcutsDialog)
            type("luu")  // the search box has the focus
            compare(d.query, "luu")
            compare(names(d), ["Lưu", "Lưu thành…", "Lưu bản sao…"])
            d.query = "save"
            compare(names(d), ["Lưu", "Lưu thành…", "Lưu bản sao…"])
            d.query = "ctrl shift s"
            compare(names(d), ["Lưu thành…"])
            d.query = "  Ctrl ⇧ S "
            compare(names(d), ["Lưu thành…"])
            d.query = "dao vung"  // marks and đ folded
            compare(names(d), ["Đảo vùng chọn"])
            d.query = "merge down"
            compare(names(d), ["Gộp xuống"])
            d.query = "zzz"
            compare(d.shown, [])
            verify(has(d, "Không có lệnh nào khớp “zzz”."))
            d.tab = 2
            d.query = "mau"
            compare(names(d), ["Màu mặc định", "Đổi màu trước, sau"])
            d.query = "lasso"
            compare(names(d), ["Lasso", "Polygonal Lasso"])
            d.query = "shift ["
            compare(names(d), ["Giảm độ cứng"])
            d.tab = 1
            d.query = "new channel"
            compare(names(d), ["Kênh mới"])
            d.query = ""
            verify(names(d).length >= 4)
        }
        function test_shortcuts_no_document() {  // from the start screen too: its key, the panels made with no document
            const w = openMain({ startState: "Start" })
            keySequence("Ctrl+Shift+Alt+K")
            const d = dialogOn(w, ShortcutsDialog)
            d.tab = 1
            verify(d.shown.some(g => g[0] === "Channels"))
            click(d, "Đóng")
            tryVerify(() => !w.dialogs.length)
        }
        function test_shortcuts_from_settings() {  // Thiết lập › Phím tắt's link opens it
            const w = openMain({ startState: "Main" })
            w.openDialog("Settings", { page: "shortcuts" })
            const s = dialogOn(w, SettingsDialog)
            mouseClick(find(s.contentItem, o => o instanceof T.AbstractButton && o.text === "Mở Phím tắt…" && o.visible))
            dialogOn(w, ShortcutsDialog)
        }
        // Every Shortcut of the window: Main's, the toolbar's, the dock's, made by Instantiators or not.
        function shortcuts(w) {
            const out = []
            const walk = o => {
                if (o instanceof Shortcut)
                    out.push(o)
                if (o instanceof Instantiator)
                    for (let i = 0; i < o.count; i++)
                        walk(o.objectAt(i))
                for (const r of o.resources || [])
                    walk(r)
                for (const c of o.children || [])
                    walk(c)
            }
            walk(w.contentItem)
            return out
        }
        function sequencesOf(s) { return (s.sequences && s.sequences.length ? s.sequences : [s.sequence]).map(String) }
        function test_keys_run() {  // each key of the three tabs: a Shortcut of the window has it, and a press fires it
            const w = openMain({ startState: "Main" })
            const sd = w.openDialog("Shortcuts")
            tryVerify(() => sd.opened)
            const keys = {}  // sequence → where the dialog lists it
            for (const tab of sd.lists)
                for (const g of tab)
                    for (const r of g[1])
                        if (r.keys.length)
                            keys[MenuSpec.sequences(r.keys.join(" "))[0]] = g[0] + " › " + r.n
            sd.close()
            tryVerify(() => !w.dialogs.length)
            const stand = {}
            const walk = items => items.forEach(it => {
                if (it.dialog)
                    stand[it.dialog + "Dialog"] = probeDialogC
                if (it.space)
                    stand[it.space + "Space"] = probeSpaceC
                if (Array.isArray(it.sub))
                    walk(it.sub)
            })
            MenuSpec.MENUS.forEach(m => walk(m.items))
            w.types = Object.assign({}, w.types, stand)
            const all = shortcuts(w)
            verify(all.length > 100)
            const setups = [() => {}, () => Docs.current.run({ op: "select", shape: "all" }),
                () => { Docs.current.run({ op: "select", shape: "all" }); Docs.current.run({ op: "deselect" }) },
                () => Docs.current.run({ op: "selectLayers", layers: [Docs.current.layers.find(l => l.kind === "group").id] }), () => Docs.current.undo(),
                () => Tools.setTool("move"), () => Docs.current.run({ op: "selectLayers", layers: [Docs.current.layers.find(l => !!l.props.mask).id] })]
            let ran = 0
            for (const seq in keys) {
                if (seq === "Ctrl+Q")  // Thoát closes the window (tst_u4)
                    continue
                const name = keys[seq] + " (" + seq + ")"
                const mine = all.filter(s => sequencesOf(s).indexOf(seq) >= 0)
                verify(mine.length > 0, name + ": no Shortcut has it")
                let on = false
                for (const s of setups) {
                    Docs.reset()
                    Tools.reset()
                    w.panelsHidden = false
                    w.contentItem.forceActiveFocus()
                    s()
                    if ((on = mine.some(x => x.enabled)))
                        break
                }
                verify(on, name + ": never on")
                let fired = 0
                const count = () => fired++
                mine.forEach(x => x.activated.connect(count))
                keySequence(seq)
                mine.forEach(x => x.activated.disconnect(count))
                compare(fired, 1, name)
                if (seq === "Ctrl+O")
                    keyClick(Qt.Key_Escape)  // its file dialog
                if (w.space)
                    w.space.finished(false)
                w.dialogs.forEach(x => x.close())
                tryVerify(() => !w.dialogs.length, 1000, name)
                ran++
            }
            compare(ran, Object.keys(keys).length - 1, "all but Thoát")
        }
    }
}
