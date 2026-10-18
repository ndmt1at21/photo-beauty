pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty

// U19: Export As, the save options, Color Settings, Convert to Profile, Assign Profile (docs/ui-tasks.md). Each format
// shows its own options; Xuất is one background job of Docs that can be cancelled; Convert and Assign are one History
// step, Hủy and Esc change nothing; Color Settings goes to Prefs; Tab, Enter, Esc. The dialogs are made here, as
// Main.openDialog() makes them, without Main.
Item {
    id: root
    width: 1440
    height: 900

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
    function named(d, name) { return find(d.contentItem, c => c.objectName === name) }
    function button(d, text) { return find(d.footer, c => c instanceof Btn && c.text === text) }
    function labelled(d, text) { return find(d.contentItem, c => c instanceof AbstractButton && c.text === text) }  // a check or radio
    function field(item, label) { return find(item, c => c instanceof NumberField && c.label === label) }

    Component { id: exportC; ExportAsDialog {} }
    Component { id: saveC; SaveOptionsDialog {} }
    Component { id: colorC; ColorSettingsDialog {} }
    Component { id: convertC; ConvertProfileDialog {} }
    Component { id: assignC; AssignProfileDialog {} }
    Component { id: mainC; Main {} }

    TestCase {
        name: "U19"
        when: windowShown

        function init() {
            Docs.reset()
            Prefs.reset()
            Theme.fontSize = "m"
            mouseMove(root, 1, 1)
        }
        function cleanupTestCase() { init() }
        function open(c, params) {
            const d = createTemporaryObject(c, root, params ? { params: params } : {})
            d.open()
            tryCompare(d, "opened", true)  // after its 120 ms fade: laid out
            return d
        }
        function shut(d) {
            tryCompare(d, "visible", false)
            tryCompare(d, "opened", false)
        }
        // A value typed into a number field, then Enter.
        function type(nf, text) {
            const tf = root.find(nf, c => c instanceof TextField)
            tf.forceActiveFocus()
            tf.text = text
            keyClick(Qt.Key_Return)
            verify(!tf.activeFocus)
        }
        // An option of a drop-down (Dropdown, Select) by its label, with the mouse.
        function choose(dd, label) {
            mouseClick(dd)
            tryCompare(dd.popup, "opened", true)
            verify(waitForPolish(dd.popup.contentItem))  // its rows placed
            const it = root.find(dd.popup.contentItem, c => c instanceof ItemDelegate && c.text === label)
            verify(it, "no option " + label)
            mouseClick(it)
            tryCompare(dd.popup, "visible", false)
        }
        function steps() { return Docs.current.history.length }
        function lastStep() { return Docs.current.history[Docs.current.index] }

        // ---- Export As ----
        // Each format shows its group of options and only it (the prototype's opt.q, png, webp, tiff).
        function test_export_formats_data() {
            return [{ tag: "JPEG", shown: ["quality"] }, { tag: "PNG", shown: ["transparent", "small"] }, { tag: "WebP", shown: ["quality", "transparent"] },
                    { tag: "TIFF", shown: ["compression", "transparent"] }]
        }
        function test_export_formats(data) {
            const d = open(exportC)
            const dd = root.find(d.contentItem, c => c instanceof Dropdown && c.value === "JPEG")
            if (data.tag !== "JPEG")
                choose(dd, data.tag)
            compare(d.format, data.tag)
            for (const name of ["quality", "transparent", "small", "compression"])
                compare(root.named(d, name).visible, data.shown.indexOf(name) >= 0, name)
            compare(d.files, ["chân dung" + { JPEG: ".jpg", PNG: ".png", WebP: ".webp", TIFF: ".tif" }[data.tag], "chân dung@0,5x" + { JPEG: ".jpg", PNG: ".png", WebP: ".webp", TIFF: ".tif" }[data.tag]])
        }
        // The artboard's boxes (U23): number fields 26 high, as its drop-downs.
        function test_export_box_heights() {
            const d = open(exportC)
            const boxes = root.findAll(d.contentItem, c => (c instanceof NumberField || c instanceof Dropdown) && c.visible)
            verify(boxes.length >= 8)
            compare(boxes.map(c => c.height), boxes.map(() => 26))
        }
        // Lớn (U23): the settings' column grows with the text, "Resample Bicubic Sharper" whole.
        function test_export_large_font() {
            Theme.fontSize = "l"
            const d = open(exportC)
            const dd = root.findAll(d.contentItem, c => c instanceof Dropdown && c.prefix === "Resample\u00a0")[0]
            const value = dd.contentItem.children[1]
            compare([value.text, value.truncated], ["Bicubic Sharper", false])
        }
        // Xuất: one job of Docs (the files of every scale), the dialog closes; the job runs, then cancels and stays so.
        function test_export_job() {
            const d = open(exportC)
            mouseClick(root.button(d, "Xuất"))
            shut(d)
            compare(Docs.job.on, true)
            compare(Docs.job.run, true)
            compare(Docs.job.verb, "export")
            compare(Docs.job.label, "Đang xuất 2 file")
            compare(Docs.job.doc, null)  // an export leaves the unsaved dot
            const p = Docs.job.p
            Docs.tick()
            verify(Docs.job.p > p)
            Docs.cancelJob()
            compare(Docs.job.on, false)
            Docs.tick()
            compare(Docs.job.on, false)
            verify(Docs.current.dirty)

            const e = open(exportC)  // one scale: the job is its file
            mouseClick(root.findAll(e.contentItem, c => c instanceof ToolToggle && c.icon.name === "delete")[1])
            compare(e.scales, [{ s: 1, suf: "" }])
            choose(root.find(e.contentItem, c => c instanceof Dropdown && c.value === "JPEG"), "PNG")
            mouseClick(root.button(e, "Xuất"))
            shut(e)
            compare(Docs.job.name, "chân dung.png")
            for (let i = 0; i < 40 && Docs.job.run; i++)
                Docs.tick()
            compare(Docs.job.done, true)
            compare(Docs.job.label, "Đã xuất chân dung.png")
        }
        // W and H keep the ratio while chained, Tỉ lệ scales the document, the canvas follows the image until typed.
        function test_export_size() {
            const d = open(exportC)
            compare(d.iw, 2400)  // as the prototype opens
            compare(Math.round(d.ih), 3055)
            compare(d.info, "2.400 × 3.055 px · ước 1,8 MB")
            const image = root.find(d.contentItem, c => c instanceof NumberField && c.label === "W")
            const scale = root.field(d.contentItem, "Tỉ lệ")
            type(image, "1100")
            compare(d.iw, 1100)
            fuzzyCompare(d.ih, 1400, 1e-9)
            compare(scale.value, 25)
            compare([d.canvasW, d.canvasH], [1100, 1400])
            type(scale, "50")
            compare([d.iw, d.ih], [2200, 2800])
            mouseClick(root.find(d.contentItem, c => c instanceof ToolToggle && c.icon.name === "link"))
            compare(d.link, false)
            type(image, "1000")
            compare([d.iw, d.ih], [1000, 2800])  // unchained
            const canvas = root.findAll(d.contentItem, c => c instanceof NumberField && c.label === "W")[1]
            type(canvas, "640")
            compare([d.canvasW, d.canvasH], [640, 2800])
            type(image, "40000")  // past the limit: the limit
            compare(d.iw, 30000)
            compare(d.canvasW, 640)
            mouseClick(root.find(d.contentItem, c => c instanceof Btn && c.text === "Đặt lại"))
            compare([d.canvasW, d.canvasH], [30000, 2800])
        }
        // Scales: a picked factor takes its suffix unless one was typed; typing keeps the field; one stays.
        function test_export_scales() {
            const d = open(exportC)
            const rows = () => root.findAll(d.contentItem, c => c.objectName === "scale")
            compare(rows().length, 2)
            choose(root.find(rows()[1], c => c instanceof Dropdown), "3×")
            compare(d.scales[1], { s: 3, suf: "@3x" })
            const box = root.find(rows()[1], c => c instanceof TextBox)
            box.forceActiveFocus()
            box.selectAll()
            for (const ch of "-nho")
                keyClick(ch)
            verify(box.activeFocus, "the field stays while typing")
            compare(d.scales[1].suf, "-nho")
            choose(root.find(rows()[1], c => c instanceof Dropdown), "4×")
            compare(d.scales[1], { s: 4, suf: "-nho" })
            compare(d.files, ["chân dung.jpg", "chân dung-nho.jpg"])
            const add = root.find(d.contentItem, c => c instanceof Btn && c.text === "Thêm tỉ lệ")
            mouseClick(add)
            compare(d.scales[2], { s: 2, suf: "@2x" })
            for (let i = 0; i < 6 && add.enabled; i++) {
                waitForRendering(d.contentItem)  // the rows laid out again, the button under them
                mouseClick(add)
            }
            compare(d.scales.length, 7)
            verify(!add.enabled)
            for (let i = 0; i < 6; i++) {
                waitForRendering(d.contentItem)
                mouseClick(root.find(rows()[0], c => c instanceof ToolToggle))
            }
            compare(rows().length, 1)
            verify(!root.find(rows()[0], c => c instanceof ToolToggle).enabled, "the last scale stays")
        }
        // The preview: Vừa fits the canvas, − + step through 12,5–400 %, held (image or button) shows the original.
        function test_export_preview() {
            const d = open(exportC)
            compare(d.zoom, -1)
            fuzzyCompare(d.fit * d.canvasH, 472, 2)  // the prototype's 470 px
            const zin = root.find(d.contentItem, c => c instanceof ToolToggle && c.icon.name === "zoom_in")
            const zout = root.find(d.contentItem, c => c instanceof ToolToggle && c.icon.name === "zoom_out")
            mouseClick(zin)
            compare(d.zoomPct, 25)
            mouseClick(zout)
            mouseClick(zout)
            compare(d.zoomPct, 12.5)
            verify(!zout.enabled)
            choose(root.find(d.contentItem, c => c instanceof Dropdown && c.value === 0), "400%")
            compare(d.zoom, 7)
            verify(!zin.enabled)
            const frame = root.find(d.contentItem, c => c instanceof Image).parent
            fuzzyCompare(frame.scale * d.fit, 4, 1e-9)
            const badge = root.find(d.contentItem, c => c instanceof Text && c.text === "Ảnh gốc").parent
            verify(!badge.visible)
            mousePress(frame)
            verify(d.comparing && badge.visible)
            mouseRelease(frame)
            verify(!d.comparing && !badge.visible)
            const cmp = root.find(d.contentItem, c => c instanceof ToolToggle && c.icon.name === "compare")
            cmp.forceActiveFocus()
            keyPress(Qt.Key_Space)
            verify(d.comparing && cmp.checked)
            keyRelease(Qt.Key_Space)
            verify(!d.comparing)
        }
        // The estimate follows the format and its options (the prototype's formula).
        function test_export_estimate() {
            const d = open(exportC)
            const at = q => { d.quality = q; return d.info }
            compare(at(82), "2.400 × 3.055 px · ước 1,8 MB")
            compare(at(1), "2.400 × 3.055 px · ước 246 KB")
            compare(at(100), "2.400 × 3.055 px · ước 7,4 MB")
            d.format = "WebP"
            compare(d.info, "2.400 × 3.055 px · ước 5,2 MB")
            d.format = "PNG"
            compare(d.info, "2.400 × 3.055 px · ước 11 MB")
            d.small = true
            compare(d.info, "2.400 × 3.055 px · ước 4,6 MB")
            d.format = "TIFF"
            compare(d.info, "2.400 × 3.055 px · ước 16,5 MB")
            d.compression = "none"
            compare(d.info, "2.400 × 3.055 px · ước 22 MB")
        }

        // ---- the save options ----
        function test_save_formats_data() {
            return [{ tag: "jpeg", title: "Tùy chọn JPEG" }, { tag: "psd", title: "Tùy chọn PSD" }, { tag: "tiff", title: "Tùy chọn TIFF" }, { tag: "png", title: "Tùy chọn PNG" }]
        }
        function test_save_formats(data) {
            const d = open(saveC, { format: data.tag })
            compare(d.title, data.title)
            for (const f of ["jpeg", "psd", "tiff", "png"])
                compare(root.named(d, f).visible, f === data.tag, f)
            verify(root.find(root.named(d, data.tag), c => c instanceof CheckRow && c.text === "Nhúng hồ sơ màu").checked)
        }
        // Without params.format, the document's own; a format it has no options for is PSD.
        function test_save_default_format() {
            compare(open(saveC).format, "psd")  // chân dung.psd
            Docs.select(2)  // hoang-hon.tif
            compare(open(saveC).format, "tiff")
            Docs.select(1)  // san-pham.webp
            compare(open(saveC).format, "psd")
        }
        // JPEG: the four levels set 3, 5, 8, 10; the field and slider 0–12; the estimate by quality and size.
        function test_save_jpeg() {
            const d = open(saveC, { format: "jpeg" })
            const level = root.find(d.contentItem, c => c instanceof Dropdown)
            compare(level.displayText, "Tối đa")
            compare(d.estimate, "5,6 MB")
            choose(level, "Thấp")
            compare(d.quality, 3)
            compare(d.estimate, "1,3 MB")
            const nf = root.find(d.contentItem, c => c instanceof NumberField)
            type(nf, "20")
            compare(d.quality, 12)  // its maximum
            compare(level.displayText, "Tối đa")
            compare(d.estimate, "10,2 MB")
            type(nf, "6")
            compare(level.displayText, "Vừa")
            mouseClick(root.labelled(d, "Progressive"))
            compare(d.kind, "progressive")
            verify(root.labelled(d, "Progressive").checked && !root.labelled(d, "Baseline Optimized").checked)
            Docs.select(1)  // san-pham.webp, 1.800 × 1.200
            compare(open(saveC, { format: "jpeg" }).estimate, "0,5 MB")
        }
        // TIFF: JPEG compression only for 8 bits.
        function test_save_tiff() {
            const d = open(saveC, { format: "tiff" })
            verify(!root.labelled(d, "JPEG").enabled)  // chân dung.psd is 16 bits
            mouseClick(root.labelled(d, "ZIP"))
            compare(d.compression, "zip")
            mouseClick(root.labelled(d, "Bỏ layer, lưu bản phẳng"))
            compare(d.layers, "flat")
            Docs.select(2)  // hoang-hon.tif, 8 bits
            const e = open(saveC)
            verify(root.labelled(e, "JPEG").enabled)
            mouseClick(root.labelled(e, "JPEG"))
            compare(e.compression, "jpeg")
        }
        // OK saves in the background: the document's dot goes once done; a copy leaves it. PSD: not asking again goes to Prefs.
        function test_save_job() {
            const doc = Docs.current
            verify(doc.dirty)
            const d = open(saveC, { format: "jpeg" })
            mouseClick(root.button(d, "OK"))
            shut(d)
            compare(Docs.job.verb, "save")
            compare(Docs.job.label, "Đang lưu chân dung.jpg")
            for (let i = 0; i < 60 && Docs.job.run; i++)
                Docs.tick()
            compare(Docs.job.label, "Đã lưu chân dung.jpg")
            verify(!doc.dirty)

            doc.run({ op: "newLayer" })
            const c = open(saveC, { format: "png", copy: true })
            mouseClick(root.button(c, "OK"))
            shut(c)
            for (let i = 0; i < 60 && Docs.job.run; i++)
                Docs.tick()
            compare(Docs.job.label, "Đã lưu chân dung.png")
            verify(doc.dirty, "a copy leaves the dot")

            const p = open(saveC, { format: "psd" })
            mouseClick(root.labelled(p, "Không hỏi lại (đổi ở Thiết lập › File và xuất)"))
            mouseClick(root.button(p, "OK"))
            shut(p)
            compare(Prefs.value("psdCompatibility"), 0)  // Luôn
            const q = open(saveC, { format: "psd" })
            mouseClick(root.labelled(q, "Maximize Compatibility"))
            mouseClick(root.labelled(q, "Không hỏi lại (đổi ở Thiết lập › File và xuất)"))
            mouseClick(root.button(q, "OK"))
            shut(q)
            compare(Prefs.value("psdCompatibility"), 2)  // Không
            verify(!open(saveC, { format: "psd" }).maximize)
        }
        function test_save_cancel() {
            const d = open(saveC, { format: "png" })
            mouseClick(root.button(d, "Hủy"))
            shut(d)
            const e = open(saveC, { format: "psd" })
            mouseClick(root.labelled(e, "Không hỏi lại (đổi ở Thiết lập › File và xuất)"))
            keyClick(Qt.Key_Escape)
            shut(e)
            compare(Docs.job.name, "chân dung.psd")  // the save Docs.reset() starts, no other
            compare(Prefs.value("psdCompatibility"), 1)
        }

        // ---- Convert to Profile, Assign Profile ----
        // OK is one step, undone in one; with Làm phẳng ảnh the flattening is in the same step.
        function test_convert() {
            const doc = Docs.current, n = steps()
            const d = open(convertC)
            compare(d.source, "sRGB IEC61966-2.1")
            compare(d.target, "Display P3")
            choose(root.find(d.contentItem, c => c instanceof Select && c.value === "Display P3"), "Adobe RGB (1998)")
            mouseClick(root.labelled(d, "Dither"))
            mouseClick(root.button(d, "OK"))
            shut(d)
            compare(doc.profile, "Adobe RGB (1998)")
            compare(steps(), n + 1)
            compare(lastStep().name, "Convert to Profile")
            compare(lastStep().cmds[0].intent, "Perceptual")
            compare(lastStep().cmds[0].dither, false)
            doc.undo()
            compare(doc.profile, "sRGB IEC61966-2.1")

            const layers = doc.layers.length
            verify(layers > 1)
            const f = open(convertC)
            mouseClick(root.labelled(f, "Làm phẳng ảnh để giữ diện mạo"))
            mouseClick(root.button(f, "OK"))
            shut(f)
            compare(doc.layers.length, 1)
            compare(doc.profile, "Display P3")
            compare(steps(), n + 1)  // the step undone is gone, this one took its place
            doc.undo()
            compare([doc.layers.length, doc.profile], [layers, "sRGB IEC61966-2.1"])
        }
        function test_convert_cancel() {
            const doc = Docs.current, n = steps()
            const d = open(convertC)
            choose(root.find(d.contentItem, c => c instanceof Select && c.value === "Display P3"), "ProPhoto RGB")
            mouseClick(root.button(d, "Hủy"))
            shut(d)
            const e = open(convertC)
            keyClick(Qt.Key_Escape)
            shut(e)
            const x = open(convertC)
            mouseClick(root.find(x.header, c => c instanceof IconButton))  // ×
            shut(x)
            compare([steps(), doc.profile], [n, "sRGB IEC61966-2.1"])
        }
        // A grey document converts among grey profiles.
        function test_convert_gray() {
            Docs.current.run({ op: "setMode", mode: "Gray" })
            Docs.current.run({ op: "assignProfile", profile: "Gray Gamma 2.2" })
            const d = open(convertC)
            compare(d.profiles, ["Gray Gamma 2.2", "Gray Gamma 1.8", "sGray", "Dot Gain 20%"])
            compare(d.target, "Gray Gamma 1.8")
        }
        // Opens on the document's choice; Hồ sơ unlocks its list; OK one step; the same profile is no step.
        function test_assign() {
            const doc = Docs.current, n = steps()
            const d = open(assignC)
            compare(d.choice, "ws")
            const list = root.find(d.contentItem, c => c instanceof Select)
            verify(!list.enabled)
            mouseClick(root.labelled(d, "Hồ sơ"))
            verify(list.enabled)
            choose(list, "Adobe RGB (1998)")
            mouseClick(root.button(d, "OK"))
            shut(d)
            compare(doc.profile, "Adobe RGB (1998)")
            compare(steps(), n + 1)
            compare(lastStep().name, "Assign Profile")

            const e = open(assignC)
            compare([e.choice, e.picked], ["prof", "Adobe RGB (1998)"])
            mouseClick(root.labelled(e, "Không quản lý màu cho tài liệu này"))
            mouseClick(root.button(e, "OK"))
            shut(e)
            compare(doc.profile, "Không quản lý màu")
            compare(steps(), n + 2)
            compare(open(assignC).choice, "none")

            doc.undo()
            doc.undo()
            const same = open(assignC)  // sRGB, the working space, again
            mouseClick(root.button(same, "OK"))
            shut(same)
            compare(steps(), n + 2)  // redo steps kept: nothing ran
            compare(doc.index, n - 1)
        }
        function test_assign_cancel() {
            const doc = Docs.current, n = steps()
            const d = open(assignC)
            mouseClick(root.labelled(d, "Không quản lý màu cho tài liệu này"))
            keyClick(Qt.Key_Escape)
            shut(d)
            compare([steps(), doc.profile], [n, "sRGB IEC61966-2.1"])
        }

        // ---- Color Settings ----
        // Values to Prefs on OK, none on Hủy; a preset sets them all, a change is Tùy chỉnh; Assign reads the working space.
        function test_color_settings() {
            const d = open(colorC)
            const preset = root.find(d.contentItem, c => c instanceof Select && c.value === 0)
            compare(preset.displayText, "photo-beauty mặc định")
            choose(root.find(d.contentItem, c => c instanceof Select && c.value === "sRGB IEC61966-2.1"), "Adobe RGB (1998)")
            compare(preset.displayText, "Tùy chỉnh")
            mouseClick(root.labelled(d, "Hỏi khi dán"))
            mouseClick(root.button(d, "OK"))
            shut(d)
            compare(Prefs.value("colorRgb"), "Adobe RGB (1998)")
            compare(Prefs.value("colorWarnPaste"), true)
            compare(Prefs.value("colorPolicyRgb"), 1)  // the rest as they were

            const e = open(colorC)
            compare(e.vals.colorRgb, "Adobe RGB (1998)")
            compare(e.preset, 2)
            choose(root.find(e.contentItem, c => c instanceof Select && c.value === 2), "Monitor Color")
            compare(e.preset, 1)
            compare([e.vals.colorRgb, e.vals.colorPolicyRgb, e.vals.colorWarnOpen], ["Display P3", 0, false])
            mouseClick(root.button(e, "Hủy"))
            shut(e)
            compare(Prefs.value("colorRgb"), "Adobe RGB (1998)")

            const a = open(assignC)
            compare(a.working, "Adobe RGB (1998)")
            verify(root.labelled(a, "Không gian làm việc: Adobe RGB (1998)"))
            compare(a.choice, "prof")  // chân dung.psd is sRGB, no longer the working space
        }
        // The box at the foot explains the row under the mouse.
        function test_color_hint() {
            const d = open(colorC)
            const box = root.find(d.contentItem, c => c instanceof Text && c.text === "Rê chuột lên một mục để xem giải thích.")
            verify(box)
            const row = root.find(d.contentItem, c => c.tip !== undefined && c.label === "Intent")
            mouseMove(row, 40, row.height / 2)
            tryCompare(box, "text", row.tip)
            mouseMove(root, 1, 1)
            tryCompare(box, "text", "Rê chuột lên một mục để xem giải thích.")
        }

        // ---- from the menus: U4's Main opens them by MenuSpec's names, params and keys ----
        function test_menus() {
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            const top = () => w.dialogs[w.dialogs.length - 1]
            const opened = (type, keys) => {
                if (keys)
                    keySequence(keys)
                tryVerify(() => w.dialogs.length === 1 && top().opened, 2000, keys)
                verify(top() instanceof type, keys)
                return top()
            }
            const gone = () => tryVerify(() => w.dialogs.length === 0)
            const menuItem = (i, label) => {  // a row of the i-th menu, clicked
                const bar = root.find(w.contentItem, o => o instanceof AppMenu), m = bar.menuAt(i)
                mouseClick(bar.itemAt(i))
                tryVerify(() => m.opened)
                waitForRendering(w.contentItem)
                for (let k = 0; k < m.count; k++)
                    if (m.itemAt(k) instanceof MenuItem && m.itemAt(k).text === label)
                        return mouseClick(m.itemAt(k))
                fail("no " + label)
            }
            let d = opened(SaveOptionsDialog, "Ctrl+Shift+S")  // Lưu thành…
            compare([d.format, !!d.params.copy], ["psd", false])
            mouseClick(root.button(d, "OK"))
            gone()
            compare(Docs.job.label, "Đang lưu chân dung.psd")
            d = opened(SaveOptionsDialog, "Ctrl+Alt+S")  // Lưu bản sao…
            compare(d.params.copy, true)
            keyClick(Qt.Key_Escape)
            gone()
            d = opened(ExportAsDialog, "Ctrl+Shift+Alt+W")  // File › Xuất › Xuất thành…
            keyClick(Qt.Key_Escape)
            gone()
            d = opened(ColorSettingsDialog, "Ctrl+Shift+K")
            keyClick(Qt.Key_Escape)
            gone()
            const n = steps()
            menuItem(1, "Assign Profile…")
            d = opened(AssignProfileDialog)
            keyClick(Qt.Key_Escape)
            gone()
            menuItem(1, "Convert to Profile…")
            d = opened(ConvertProfileDialog)
            mouseClick(root.button(d, "OK"))
            gone()
            compare([steps(), Docs.current.profile], [n + 1, "Display P3"])
            Docs.closeAll()  // Color Settings needs no document; the others are greyed
            keySequence("Ctrl+Shift+K")
            d = opened(ColorSettingsDialog)
            keyClick(Qt.Key_Escape)
            gone()
            keySequence("Ctrl+Shift+Alt+W")
            wait(50)
            compare(w.dialogs.length, 0)
        }

        // ---- keyboard ----
        // Tab reaches every control of each dialog and its buttons; Enter on OK runs it.
        function test_tab_data() {
            return [{ tag: "export", c: exportC, n: 29 }, { tag: "save", c: saveC, n: 5 }, { tag: "color", c: colorC, n: 14 },
                    { tag: "convert", c: convertC, n: 9 }, { tag: "assign", c: assignC, n: 7 }]
        }
        function test_tab(data) {
            const d = open(data.c)
            const seen = []
            for (let i = 0; i < 40; i++) {
                keyClick(Qt.Key_Tab)
                const f = root.Window.activeFocusItem
                if (seen.indexOf(f) >= 0)
                    break
                seen.push(f)
            }
            compare(seen.length, data.n, seen.map(f => f.text || f.objectName || f).join(", "))
            verify(seen.indexOf(root.button(d, "Hủy")) >= 0)
            if (data.tag === "export")  // passing through a field changes nothing
                compare([d.iw, d.canvasW, d.canvasH], [2400, 2400, 3055])
            keyClick(Qt.Key_Escape)
            shut(d)
        }
        function test_enter() {
            const doc = Docs.current, n = steps()
            const d = open(convertC)
            const ok = root.button(d, "OK")
            for (let i = 0; i < 20 && root.Window.activeFocusItem !== ok; i++)
                keyClick(Qt.Key_Tab)
            compare(root.Window.activeFocusItem, ok)
            keyClick(Qt.Key_Return)
            shut(d)
            compare(steps(), n + 1)
            compare(doc.profile, "Display P3")
        }
    }
}
