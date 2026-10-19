import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtTest
import PhotoBeauty
import "../qml/MenuSpec.js" as MenuSpec

// U25c: Auto Color Correction Options (build/design2/project/AutoColor.dc.html) from Levels' gear, and Help's boxes
// (HelpDialogs.dc.html): Về photo-beauty, Thông tin hệ thống, Gửi báo lỗi, Kiểm tra cập nhật in its two states. Each
// Help item opens its box with no document too; the options go into the adjustment (one step in Properties), Lưu làm
// mặc định into Prefs; each box measures as its artboard (Chrome's boxes, ±2 px). No test reads the version:
// pb-ui-tests has none.
Item {
    id: root
    width: 200
    height: 100
    Component { id: mainC; Main {} }
    Component { id: autoC; AutoColorDialog {} }
    Component { id: aboutC; AboutDialog {} }
    Component { id: infoC; SystemInfoDialog {} }
    Component { id: bugC; BugReportDialog {} }
    Component { id: updateC; UpdateDialog {} }
    Component { id: pasteC; TextEdit {} }
    Component { id: hostC; Window { width: 1440; height: 900; visible: true; color: Theme.bg } }

    TestCase {
        name: "U25c"
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
        function label(d, text) { return find(frame(d), o => o instanceof Text && o.visible && o.text === text) }
        function radios(d) { return findAll(frame(d), o => o.objectName === "radio" && o.visible) }
        function checks(d) { return findAll(frame(d), o => o instanceof CheckRow && o.visible) }
        function log(d) { return find(frame(d), o => o instanceof Text && o.visible && o.font.family === Theme.mono && o.text.indexOf("\n") >= 0) }
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
        // Where an item sits in its box, as the artboard's boxes: [x, y, w, h] in whole pixels.
        function rect(d, item) {
            verify(item)
            const p = item.mapToItem(frame(d), 0, 0)
            return [Math.round(p.x), Math.round(p.y), Math.round(item.width), Math.round(item.height)]
        }
        // Each number within 2 px of the artboard's; null skips one.
        function near(got, want, what) {
            verify(got.every((g, i) => want[i] === null || Math.abs(g - want[i]) <= 2), what + ": " + got + ", the artboard " + want)
        }
        function typeIn(field, text) {  // a NumberField: typed, then Enter
            const input = find(field, o => o instanceof T.TextField)
            input.forceActiveFocus()
            input.text = text
            keyClick(Qt.Key_Return)
        }

        // ---- Help ------------------------------------------------------------------------------------------------------
        function test_help_items() {  // each opens its box, with no document as with one
            const help = MenuSpec.MENUS.find(m => m.title === "&Help").items
            const boxes = [["Thông tin hệ thống…", SystemInfoDialog], ["Kiểm tra cập nhật…", UpdateDialog],
                           ["Gửi báo lỗi…", BugReportDialog], ["Về photo-beauty", AboutDialog]]
            const w = openMain({ startState: "Start" })
            for (const state of ["Start", "Main"]) {
                w.start(state)
                compare(Docs.docs.length > 0, state === "Main")
                for (const b of boxes) {
                    const it = help.find(x => x.l === b[0])
                    verify(w.can(it), b[0])
                    verify(w.act(it), b[0])
                    dialogOn(w, b[1]).close()
                    tryVerify(() => !w.dialogs.length, 1000, b[0])
                }
            }
        }

        // ---- Auto Color Correction Options -----------------------------------------------------------------------------
        function test_auto_color_as_artboard() {  // four algorithms, Snap, the targets and clips, Lưu làm mặc định
            const d = box(autoC)
            compare([d.title, d.width], ["Auto Color Correction Options", 460])
            near([d.height], [452], "height")
            const r = radios(d)
            compare(r.map(x => x.text), ["Enhance Monochromatic Contrast", "Enhance Per Channel Contrast", "Find Dark & Light Colors",
                                         "Enhance Brightness and Contrast"])
            compare(r.map(x => x.checked), [false, false, true, false])
            near(r.map(x => rect(d, x)[1]), [88, 112, 137, 161], "radios")
            near(rect(d, r[0]), [18, 88, null, 16], "the first radio")
            const c = checks(d)
            compare(c.map(x => [x.text, x.checked]), [["Snap Neutral Midtones", true], ["Lưu làm mặc định", false]])
            near(c.map(x => rect(d, x)[1]), [189, 367], "the checks")
            near([rect(d, label(d, "Thuật toán"))[1], rect(d, label(d, "Màu đích và cắt"))[1]], [62, 221], "the captions")
            near(rect(d, label(d, "Shadows")), [18, 253, null, 16], "Shadows")
            near(rect(d, label(d, "Clip")), [296, null, 40, null], "Clip")
            const sw = findAll(frame(d), o => o instanceof ColorButton && o.visible)
            compare(sw.map(x => String(x.color)), ["#000000", "#808080", "#ffffff"])
            near(rect(d, sw[0]), [118, 249, 40, 24], "Shadows' colour")
            near([rect(d, sw[1])[1], rect(d, sw[2])[1]], [287, 325], "Midtones', Highlights' colours")
            const f = findAll(frame(d), o => o instanceof NumberField && o.visible)
            compare(f.map(x => x.shown), ["0,10%", "0,10%"])
            near(rect(d, f[0]), [346, 247, 96, 28], "Shadows' clip")
            near([rect(d, f[1])[1]], [323], "Highlights' clip")
            near(rect(d, button(d, "Hủy")), [301, 412, 51, 28], "Hủy")
            near(rect(d, button(d, "OK")), [360, 412, 84, 28], "OK")
        }
        function test_auto_color_clip() {  // 0 to 9,99 %: past either end, the end
            const d = box(autoC)
            const f = findAll(frame(d), o => o instanceof NumberField && o.visible)
            typeIn(f[0], "12")
            compare([d.opts.clipShadow, f[0].shown], [9.99, "9,99%"])
            typeIn(f[1], "-1")
            compare([d.opts.clipHigh, f[1].shown], [0, "0,00%"])
            typeIn(f[1], "0,5")
            compare(d.opts.clipHigh, 0.5)
        }
        function test_auto_color_ok_cancel() {  // the adjustment's options over the defaults; OK gives them, Hủy, Esc, × nothing
            const got = []
            let d = box(autoC, { options: { algo: 0, snap: false, clipHigh: 2 }, picked: o => got.push(o) })
            compare(radios(d).map(x => x.checked), [true, false, false, false])
            mouseClick(radios(d)[3])
            compare(radios(d).map(x => x.checked), [false, false, false, true])
            click(d, "OK")
            tryCompare(d, "visible", false)
            compare(got, [{ algo: 3, snap: false, shadow: "#000000", mid: "#808080", high: "#ffffff", clipShadow: 0.1, clipHigh: 2 }])
            compare(Prefs.value("autoColor"), null, "not kept")
            for (const how of ["Hủy", "Esc", "×"]) {
                d = box(autoC, { picked: o => got.push(o) })
                mouseClick(radios(d)[1])
                if (how === "Hủy")
                    click(d, "Hủy")
                else if (how === "Esc")
                    keyClick(Qt.Key_Escape)
                else
                    mouseClick(find(frame(d), o => o instanceof IconButton && o.icon.name === "close"))
                tryCompare(d, "visible", false)
                compare(got.length, 1, how)
            }
        }
        function test_auto_color_keep() {  // Lưu làm mặc định: Prefs keeps the options, a box with none opens on them
            let d = box(autoC)
            mouseClick(radios(d)[1])
            mouseClick(checks(d)[0])  // Snap off
            typeIn(findAll(frame(d), o => o instanceof NumberField && o.visible)[0], "0,3")
            mouseClick(checks(d)[1])  // Lưu làm mặc định
            click(d, "OK")
            tryCompare(d, "visible", false)
            const kept = { algo: 1, snap: false, shadow: "#000000", mid: "#808080", high: "#ffffff", clipShadow: 0.3, clipHigh: 0.1 }
            compare(Prefs.value("autoColor"), kept)
            d = box(autoC)
            compare(d.opts, kept)
            compare(radios(d).map(x => x.checked), [false, true, false, false])
            compare(checks(d).map(x => x.checked), [false, false], "Snap off, Lưu làm mặc định not ticked again")
            d.close()
            Prefs.restore(JSON.stringify(Prefs.values))
            compare(Prefs.value("autoColor"), kept, "over a restart")
            d = box(autoC, { options: { algo: 3 } })
            compare([d.opts.algo, d.opts.clipShadow], [3, 0.3], "the adjustment's own over the kept")
        }
        function test_levels_gear_in_properties() {  // a Levels layer's gear: OK keeps the options in it, one step; Hủy none
            const w = openMain({ startState: "Main" })
            const doc = Docs.current
            verify(doc.run({ op: "newAdjustmentLayer", adjustment: "Levels" }))
            Tools.showPanel("Properties")
            waitForRendering(w.contentItem)
            const p = find(w.contentItem, o => o instanceof PropertiesPanel && o.visible)
            tryCompare(p, "page", "adj")
            waitForRendering(p)
            const gear = () => find(p, o => o instanceof ToolToggle && o.icon.name === "settings" && o.visible)
            verify(gear().enabled)
            const n = doc.history.length
            mouseClick(gear())
            let d = dialogOn(w, AutoColorDialog)
            mouseClick(radios(d)[0])
            click(d, "Hủy")
            tryVerify(() => !w.dialogs.length)
            compare([doc.history.length, doc.active.props.vals.auto], [n, undefined])

            mouseClick(gear())
            d = dialogOn(w, AutoColorDialog)
            compare(d.opts.algo, 2)
            mouseClick(radios(d)[0])
            click(d, "OK")
            tryVerify(() => !w.dialogs.length)
            compare([doc.history.length, doc.active.props.vals.auto.algo, doc.active.props.vals.auto.snap], [n + 1, 0, true])
            mouseClick(gear())
            compare(dialogOn(w, AutoColorDialog).opts.algo, 0, "the layer's own, opened again")
        }
        function test_levels_gear_in_adjust_dialog() {  // Image › Adjustments › Levels: the dialog's values until its OK
            const w = openMain({ startState: "Main" })
            const doc = Docs.current, n = doc.history.length
            const a = w.openDialog("Adjust", { adjust: "levels" })
            tryVerify(() => a.opened)
            mouseClick(button(a, "Auto"))
            compare([a.values.in0, a.values.in1], [8, 238])
            const gear = find(frame(a), o => o instanceof ToolToggle && o.icon.name === "settings" && o.visible)
            verify(gear.enabled)
            mouseClick(gear)
            const d = dialogOn(w, AutoColorDialog)
            mouseClick(checks(d)[0])  // Snap off
            click(d, "OK")
            tryVerify(() => w.dialogs.length === 1)
            compare([a.values.auto.snap, a.values.in0, a.values.in1], [false, 8, 238], "Auto on: run again with them")
            compare(doc.history.length, n, "no step before the dialog's OK")
            click(a, "OK")
            tryVerify(() => !w.dialogs.length)
            compare(doc.history.length, n + 1)
            const v = doc.history[n].cmds[0].values
            compare([v.in0, v.in1, v.auto.snap, v.auto.algo], [8, 238, false, 2])
        }

        // ---- Về photo-beauty -------------------------------------------------------------------------------------------
        function test_about() {  // icon, wordmark, version, what it is, copyright, Giấy phép… and Đóng
            const d = box(aboutC)
            compare([d.title, d.width], ["Về photo-beauty", 420])
            near([d.height], [357], "height")
            const img = find(frame(d), o => o instanceof Image && o.visible)
            compare([String(img.source), img.status], ["qrc:/qt/qml/PhotoBeauty/brand/icon.png", Image.Ready])
            near(rect(d, img), [174, 74, 72, 72], "icon")
            const mark = label(d, "photo-beauty")
            compare([mark.font.family, mark.font.weight], ["Manrope", Font.ExtraBold])
            near(rect(d, mark), [130, 156, 160, 32], "wordmark")
            near(rect(d, find(frame(d), o => o instanceof Text && o.visible && o.text.indexOf("Phiên bản ") === 0)), [null, 198, null, 16], "version")
            near(rect(d, label(d, "Chỉnh ảnh, retouch chân dung trên GPU.")), [null, 224, null, 16], "what it is")
            const c = label(d, "© 2026 photo-beauty. Dùng Qt, Vulkan, Slang và các thư viện mã nguồn mở khác.")
            compare(c.lineCount, 2)
            near(rect(d, c), [24, 250, 372, 30], "copyright")
            near(rect(d, button(d, "Giấy phép…")), [16, 317, 94, 28], "Giấy phép…")
            near(rect(d, button(d, "Đóng")), [320, 317, 84, 28], "Đóng")
        }
        function test_about_licence() {  // Giấy phép… closes it for Thiết lập on Báo lỗi, cập nhật, giấy phép
            const w = openMain({ startState: "Start" })
            w.openDialog("About")
            click(dialogOn(w, AboutDialog), "Giấy phép…")
            compare(dialogOn(w, SettingsDialog).page, "about")
            verify(!w.dialogs.some(x => x instanceof AboutDialog))
        }

        // ---- Thông tin hệ thống ----------------------------------------------------------------------------------------
        function test_system_info() {  // the block of the artboard, from Prefs where it can; Sao chép copies it
            const d = box(infoC)
            compare([d.title, d.width], ["Thông tin hệ thống", 520])
            near([d.height], [411], "height")
            const t = log(d), lines = t.text.split("\n")
            compare(lines.length, 11)
            verify(lines[0].indexOf("photo-beauty ") === 0 && lines[0].indexOf(", Qt 6.11.3") > 0, lines[0])
            compare(lines.slice(2).map(l => l.slice(0, 11)), ["CPU        ", "RAM        ", "GPU        ", "           ", "           ",
                                                              "Màn hình   ", "Scratch    ", "Font       ", "Nhật ký    "])
            compare(lines[3], "RAM        16 GB; ngân sách 9,6 GB (60%)")
            compare(lines[4], "GPU        Intel Iris Xe Graphics")
            compare(lines[8], "Scratch    C: còn 118 GB trống, D: còn 402 GB trống")
            compare(lines[9], "Font       Space Grotesk, JetBrains Mono, Manrope")
            let b = t
            while (!(b instanceof Rectangle))
                b = b.parent
            near(rect(d, b), [18, 62, 484, 280], "the block")
            near(rect(d, button(d, "Sao chép")), [16, 371, 104, 28], "Sao chép")
            near(rect(d, button(d, "Đóng")), [420, 371, 84, 28], "Đóng")
            click(d, "Sao chép")
            verify(button(d, "Đã chép"))
            const paste = createTemporaryObject(pasteC, root)
            paste.paste()
            compare(paste.text, t.text)
        }

        // ---- Gửi báo lỗi -----------------------------------------------------------------------------------------------
        function test_bug_report() {  // what happened, the email, three things with it; Gửi once something is told
            const d = box(bugC)
            compare([d.title, d.width], ["Gửi báo lỗi", 480])
            near([d.height], [443], "height")
            near(rect(d, label(d, "Chuyện gì đã xảy ra?")), [18, 62, null, 16], "the first label")
            near(rect(d, find(frame(d), o => o instanceof ScrollView)), [18, 90, 444, 110], "what happened")
            near(rect(d, label(d, "Email để chúng tôi hỏi lại (không bắt buộc)")), [18, 212, null, 16], "the second label")
            const mail = find(frame(d), o => o instanceof TextBox)
            compare(mail.placeholderText, "ban@vidu.com")
            near(rect(d, mail), [18, 240, 444, 28], "email")
            const c = checks(d)
            compare(c.map(x => [x.text, x.checked]), [["Kèm nhật ký 7 ngày gần nhất", true], ["Kèm thông tin hệ thống", true],
                                                     ["Kèm ảnh chụp cửa sổ", false]])
            near(c.map(x => rect(d, x)[1]), [282, 307, 331], "the checks")
            near(rect(d, label(d, "Không gửi ảnh hay tài liệu bạn đang mở.")), [18, 359, null, 15], "the note")
            near(rect(d, find(frame(d), o => o instanceof Link)), [16, 409, 117, 16], "Xem nội dung sẽ gửi")
            near(rect(d, button(d, "Hủy")), [321, 403, 51, 28], "Hủy")
            near(rect(d, button(d, "Gửi")), [380, 403, 84, 28], "Gửi")

            const what = find(frame(d), o => o instanceof T.TextArea)
            compare(what.placeholderText, "Bạn đang làm gì, mong đợi gì, thấy gì…")
            verify(!button(d, "Gửi").enabled, "nothing told")
            mouseClick(what)
            keyClick(Qt.Key_Space)
            keyClick(Qt.Key_Return)
            verify(!button(d, "Gửi").enabled, "only spaces")
            keyClick("a")
            verify(button(d, "Gửi").enabled)
        }
        function test_bug_report_send() {  // Xem nội dung sẽ gửi shows the report; Gửi closes it with a toast
            const w = openMain({ startState: "Start" })
            w.openDialog("BugReport")
            const d = dialogOn(w, BugReportDialog)
            find(frame(d), o => o instanceof T.TextArea).text = "  Treo khi mở PSD\n"
            find(frame(d), o => o instanceof TextBox).text = "a@b.vn"
            mouseClick(checks(d)[2])
            const link = find(frame(d), o => o instanceof Link)
            mouseClick(link)
            let s = dialogOn(w, SystemInfoDialog)
            compare(s.title, "Nội dung sẽ gửi")
            const lead = "Chuyện gì đã xảy ra?\nTreo khi mở PSD\n\nEmail: a@b.vn\nKèm theo: "
            const t = log(s).text
            compare(t.slice(0, t.indexOf("\n\nphoto-beauty ")), lead + "nhật ký 7 ngày gần nhất, thông tin hệ thống, ảnh chụp cửa sổ")
            click(s, "Đóng")
            tryVerify(() => w.dialogs.length === 1)
            mouseClick(checks(d)[1])  // no system information: none shown
            mouseClick(checks(d)[0])
            mouseClick(link)
            s = dialogOn(w, SystemInfoDialog)
            compare(log(s).text, lead + "ảnh chụp cửa sổ")
            keyClick(Qt.Key_Escape)
            tryVerify(() => w.dialogs.length === 1)
            const overlay = d.parent  // the window's, where the toast goes
            click(d, "Gửi")
            tryVerify(() => !w.dialogs.length)
            const toast = find(overlay, o => o instanceof Toast)
            verify(toast && toast.visible)
            compare([toast.text, toast.ok], ["Đã gửi báo lỗi, cảm ơn bạn", true])
            tryVerify(() => !find(overlay, o => o instanceof Toast), 5000, "gone after its time")
        }

        // ---- Kiểm tra cập nhật -----------------------------------------------------------------------------------------
        function test_update_latest() {  // the mock's: this version is the latest, when it checked; Đóng
            const d = box(updateC)
            compare([d.title, d.width], ["Kiểm tra cập nhật", 400])
            near([d.height], [167], "height")
            const ic = find(frame(d), o => o instanceof Icon && o.visible && o.size === 28)
            compare(ic.name, "check")
            near(rect(d, ic), [18, 62, 28, 28], "icon")
            near(rect(d, label(d, "Bạn đang dùng bản mới nhất")), [58, 62, null, 16], "title")
            const at = find(frame(d), o => o instanceof Text && o.visible && /kiểm tra lúc \d\d:\d\d$/.test(o.text))
            near(rect(d, at), [58, 82, null, 16], "checked at")
            compare(findAll(frame(d), o => o instanceof Btn && o.visible).map(b => b.text), ["Đóng"])
            near(rect(d, button(d, "Đóng")), [300, 127, 84, 28], "Đóng")
        }
        function test_update_available() {  // params.state "available": the version, its notes, [Để sau][Tải về]
            let d = box(updateC, { state: "available" })
            near([d.height], [190], "height")
            compare(find(frame(d), o => o instanceof Icon && o.visible && o.size === 28).name, "system_update")
            const lines = ["Có bản mới: [phiên bản mới]", "[Ghi chú phát hành: hai, ba dòng về điều mới.]", "Tải ở nền; cài khi bạn đóng app."]
            near(lines.map(l => rect(d, label(d, l))[1]), [62, 84, 106], "the lines")
            near(lines.map(l => rect(d, label(d, l))[3]), [16, 16, 15], "their heights")
            near([rect(d, label(d, lines[0]))[0]], [58], "x")
            compare(findAll(frame(d), o => o instanceof Btn && o.visible).map(b => b.text), ["Để sau", "Tải về"])
            near(rect(d, button(d, "Để sau")), [225, 150, 67, 28], "Để sau")
            near(rect(d, button(d, "Tải về")), [300, 150, 84, 28], "Tải về")
            d = box(updateC, { state: "available", version: "0.2.0", notes: "Sửa lỗi lưu PSD." })
            verify(label(d, "Có bản mới: 0.2.0") && label(d, "Sửa lỗi lưu PSD."))
        }
        function test_update_state() {  // --state Update:available, then Update
            const w = openMain({ startState: "Start" })
            w.start("Update:available")
            let d = dialogOn(w, UpdateDialog)
            verify(d.available)
            click(d, "Tải về")
            tryVerify(() => !w.dialogs.length)
            w.start("Update")
            d = dialogOn(w, UpdateDialog)
            verify(!d.available)
            keyClick(Qt.Key_Return)  // Đóng
            tryVerify(() => !w.dialogs.length)
        }

        // ---- keys ------------------------------------------------------------------------------------------------------
        function test_keys() {  // Enter is OK, Esc Hủy; Tab leaves what happened for the email without typing a tab
            let got = 0
            let d = box(autoC, { picked: () => got++ })
            keyClick(Qt.Key_Return)
            tryCompare(d, "visible", false)
            compare(got, 1)
            d = box(autoC, { picked: () => got++ })
            const f = findAll(frame(d), o => o instanceof NumberField && o.visible)[1]
            for (let i = 0; i < 12 && !find(f, o => o instanceof T.TextField).activeFocus; i++)
                keyClick(Qt.Key_Tab)
            verify(find(f, o => o instanceof T.TextField).activeFocus, "Tab to Highlights' clip")
            keyClick(Qt.Key_Escape)  // the field's typing
            keyClick(Qt.Key_Escape)
            tryCompare(d, "visible", false)
            compare(got, 1)

            d = box(bugC)
            const what = find(frame(d), o => o instanceof T.TextArea), mail = find(frame(d), o => o instanceof TextBox)
            mouseClick(what)
            keyClick("x")
            keyClick(Qt.Key_Tab)
            verify(mail.activeFocus, "Tab to the email")
            compare(what.text, "x")
            keyClick(Qt.Key_Backtab)
            verify(what.activeFocus, "back")
            keyClick(Qt.Key_Backtab)
            verify(!what.activeFocus)
            compare(what.text, "x")
        }
    }
}
