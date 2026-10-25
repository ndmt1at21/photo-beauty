import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtCore
import QtTest
import PhotoBeauty
import "../qml/Fmt.js" as Fmt
import "../qml/Mock.js" as Mock
import "../qml/MenuSpec.js" as MenuSpec
import "../qml/ToolSpec.js" as ToolSpec

// U30: English beside Vietnamese, the language of the sources. Thiết lập › Giao diện › Ngôn ngữ kept for the next start
// (main.cpp reads it; ctest i18n checks that start), numbers as the language writes them (Fmt.js), Photoshop's English
// names for menus and tools in ui/i18n/pb_en.ts (Language.translate() reads the built pb_en.qm as qsTr() would), and the
// Vietnamese tooltips that carry them. The app here stays Vietnamese: no translator is installed, Qt.uiLanguage is "".
Item {
    id: root
    Component { id: mainC; Main {} }
    Component { id: settingsC; SettingsDialog {} }
    Component { id: exportC; ExportAsDialog {} }
    Component {
        id: hostC
        Window { width: 1440; height: 900; visible: true; color: Theme.bg }
    }
    Component { id: fileC; Settings { category: "prefs" } }  // Prefs' JSON in ~/.qttest (the tests' Setup)

    TestCase {
        name: "U30"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanup() {  // Vietnamese again; no Prefs left writing to ~/.qttest, nor what it wrote
            Qt.uiLanguage = ""
            if (!Prefs.store) {
                Prefs.reset()
                return
            }
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
        function text(item, s) { return find(item, o => o instanceof Text && o.visible && o.text === s) }
        function button(item, s) { return find(item, o => o instanceof T.AbstractButton && o.visible && o.text === s) }
        function rowOf(d, key) { return find(d.contentItem, o => o.modelData && o.r !== undefined && o.modelData.key === key) }
        function openMain(props) {  // as tst_u4: active and drawn
            const w = createTemporaryObject(mainC, root, props || {})
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function settingsOn(w) {
            let d = null
            tryVerify(() => (d = w.dialogs.find(x => x instanceof SettingsDialog) || null) && d.opened)
            return d
        }
        function interfacePage(d) {
            mouseClick(button(d.contentItem, "Giao diện"))
            compare(d.page, "interface")
            waitForRendering(d.contentItem)
        }

        function test_vietnamese_here() {  // what every other test file expects
            compare(Qt.uiLanguage, "")
            compare(Fmt.vn(1234.5, 1), "1.234,5")
            compare(Language.translate("en", "MenuSpec", "Tạo mới…"), "New…", "the English of pb_en.qm")
            compare(MenuSpec.MENUS[0].items[0].l, "Tạo mới…", "the app's own strings Vietnamese")
        }

        // Ngôn ngữ: Tiếng Việt, English (by default the one this start took), the note under it; a choice is kept in
        // Prefs' JSON at once and the window stays in its language.
        function test_language_row() {
            const host = createTemporaryObject(hostC, root)
            const d = createTemporaryObject(settingsC, host.contentItem, { params: { page: "interface" } })
            d.open()
            tryCompare(d, "opened", true)
            const row = rowOf(d, "language"), seg = find(row, o => o instanceof Segmented)
            verify(text(row, "Ngôn ngữ"), "its label")
            compare(seg.options, ["Tiếng Việt", "English"])
            compare(seg.current, 0)
            const note = text(d.contentItem, "Ngôn ngữ mới có hiệu lực từ lần mở app sau.")
            verify(note, "the note, as Độ trễ thấp's")
            const y = o => o.mapToItem(d.contentItem, 0, 0).y
            verify(y(note) > y(row) && y(note) < y(rowOf(d, "channelsInColor")), "right under the row")
            mouseClick(button(row, "English"))
            compare(Prefs.value("language"), 1)
            compare(seg.current, 1)
            verify(text(row, "Ngôn ngữ") && button(d.contentItem, "Giao diện"), "still Vietnamese until the next start")
            mouseClick(button(row, "Tiếng Việt"))
            compare(Prefs.value("language"), 0)
        }

        // The first start takes the system's language (main.cpp) and keeps it: by default Prefs has the one this start
        // took, so its JSON says English once an English start has written it.
        function test_first_start() {
            compare(Prefs.defaults.language, 0, "Qt.uiLanguage \"\": Vietnamese")
            Qt.uiLanguage = "en"  // as main.cpp sets it on an English system
            compare(Prefs.defaults.language, 1)
            Prefs.restore("{}")
            compare(Prefs.value("language"), 1, "nothing kept: this start's language")
            Prefs.restore(JSON.stringify({ language: 0 }))
            compare(Prefs.value("language"), 0, "what was kept wins")
            Prefs.restore(JSON.stringify({ language: "0" }))
            compare(Prefs.value("language"), 1, "not a number: the default")
            Qt.uiLanguage = "vi"
            compare(Prefs.defaults.language, 0)
        }

        // As tst_u21's test_prefs_kept: English picked in Thiết lập is in the file at once and back at the next start.
        function test_language_kept() {
            const file = createTemporaryObject(fileC, root)
            file.setValue("json", "")  // not what a run before left
            let w = openMain({ startState: "Main", persist: true })
            verify(Prefs.store, "Main keeps Prefs")
            keySequence("Ctrl+K")
            let d = settingsOn(w)
            interfacePage(d)
            mouseClick(button(rowOf(d, "language"), "English"))
            compare(JSON.parse(file.value("json")).language, 1, "written as it changes")
            keyClick(Qt.Key_Escape)
            tryVerify(() => w.dialogs.length === 0)
            w.destroy()
            wait(50)

            // The next start: Prefs from its defaults, then the file once Main keeps it.
            const s = Prefs.store
            Prefs.persist = false
            Prefs.store = null
            s.destroy()
            Prefs.reset()
            compare(Prefs.value("language"), 0)
            w = openMain({ startState: "Main", persist: true })
            compare(Prefs.value("language"), 1, "English kept")
            keySequence("Ctrl+K")
            d = settingsOn(w)
            interfacePage(d)
            compare(find(rowOf(d, "language"), o => o instanceof Segmented).current, 1)
        }

        // Numbers as the language writes and reads them: 1.234,5 in Vietnamese, 1,234.5 in English, "−" in both.
        function test_fmt() {
            const both = (vi, en) => {
                Qt.uiLanguage = ""
                vi()
                Qt.uiLanguage = "en"
                en()
                Qt.uiLanguage = "vi"
                vi()
            }
            both(() => {
                compare([Fmt.vn(1234.5, 1), Fmt.vn(-1234.5, 1), Fmt.vn(-0.04, 1), Fmt.vn(1234567)], ["1.234,5", "−1.234,5", "0,0", "1.234.567"])
                compare([Fmt.sg(2.5, 1), Fmt.zt(12.5), Fmt.zt(3200), Fmt.unit(2480, "px"), Fmt.unit(1.25, "cm", 2)], ["+2,5", "12,5%", "3200%", "2.480 px", "1,25 cm"])
                compare([Fmt.pnum("1.234,5"), Fmt.pnum("12.5"), Fmt.pnum("1.234"), Fmt.pnum("0,125"), Fmt.pnum("−3,5%")], [1234.5, 12.5, 1234, 0.125, -3.5])
                compare([Fmt.parse("1.200 + 2,5", "px"), Fmt.parse("2,5cm", "mm"), Fmt.parse("50%", "px", 300, 900)], [1202.5, 25, 450])
                compare([Mock.vn(1.02, 2), Mock.vn(1234)], ["1,02", "1.234"])
            }, () => {
                compare([Fmt.vn(1234.5, 1), Fmt.vn(-1234.5, 1), Fmt.vn(-0.04, 1), Fmt.vn(1234567)], ["1,234.5", "−1,234.5", "0.0", "1,234,567"])
                compare([Fmt.sg(2.5, 1), Fmt.zt(12.5), Fmt.zt(3200), Fmt.unit(2480, "px"), Fmt.unit(1.25, "cm", 2)], ["+2.5", "12.5%", "3200%", "2,480 px", "1.25 cm"])
                compare([Fmt.pnum("1,234.5"), Fmt.pnum("12,5"), Fmt.pnum("1,234"), Fmt.pnum("0.125"), Fmt.pnum("−3.5%")], [1234.5, 12.5, 1234, 0.125, -3.5])
                compare([Fmt.parse("1,200 + 2.5", "px"), Fmt.parse("2.5cm", "mm"), Fmt.parse("50%", "px", 300, 900)], [1202.5, 25, 450])
                compare([Mock.vn(1.02, 2), Mock.vn(1234)], ["1.02", "1,234"])
            })
            // Export As' scales and their file name suffixes, the second scale's at the start too
            Docs.reset("Main")
            const host = createTemporaryObject(hostC, root)
            const d = createTemporaryObject(exportC, host.contentItem)
            both(() => compare([d.scaleText(0.5), d.suffix(1.5), d.suffix(1), d.scales[1].suf], ["0,5×", "@1,5x", "", "@0,5x"]),
                 () => compare([d.scaleText(0.5), d.suffix(1.5), d.suffix(1), d.scales[1].suf], ["0.5×", "@1.5x", "", "@0.5x"]))
        }

        // 43 names of menus and tools as Photoshop 2025's English menus and tools have them, from Vietnamese labels the
        // app shows; "Xóa" and "Layer" with the disambiguation of their other meaning.
        function test_photoshop_names() {
            const names = [
                ["Tạo mới…", "", "New…"], ["Mở…", "", "Open…"], ["Mở gần đây", "", "Open Recent"], ["Lưu thành…", "", "Save As…"],
                ["Lưu bản sao…", "", "Save a Copy…"], ["Xuất thành…", "", "Export As…"], ["Thoát", "", "Exit"], ["Hoàn tác", "", "Undo"],
                ["Đổi qua lại bước cuối", "", "Toggle Last State"], ["Sao chép hợp nhất", "", "Copy Merged"], ["Xóa", "Edit › Clear", "Clear"],
                ["Xóa", "", "Delete"], ["Xoay 90° theo chiều kim đồng hồ", "", "Rotate 90° Clockwise"], ["Phím tắt…", "", "Keyboard Shortcuts…"],
                ["Lật canvas ngang", "", "Flip Canvas Horizontal"], ["Sao chép sang layer mới", "", "Layer via Copy"],
                ["Chuyển thành Smart Object", "", "Convert to Smart Object"], ["Tạo Clipping Mask", "", "Create Clipping Mask"],
                ["Hiện vùng chọn", "", "Reveal Selection"], ["Đưa lên trên cùng", "", "Bring to Front"], ["Gộp layer đang hiện", "", "Merge Visible"],
                ["Làm phẳng ảnh", "", "Flatten Image"], ["Đảo vùng chọn", "", "Inverse"], ["Sửa trong Quick Mask", "", "Edit in Quick Mask Mode"],
                ["Vừa màn hình", "", "Fit on Screen"], ["Toàn màn hình có menu", "", "Full Screen Mode With Menu Bar"], ["Bắt dính vào", "", "Snap To"],
                ["Layer", "View › Snap To › Layers", "Layers"], ["Thông tin hệ thống…", "", "System Info…"]
            ]
            const tools = [
                ["Move", "Move"], ["Rectangular Marquee", "Rectangular Marquee"], ["Lasso", "Lasso"], ["Magic Wand", "Magic Wand"], ["Crop", "Crop"],
                ["Eyedropper", "Eyedropper"], ["Spot Healing Brush", "Spot Healing Brush"], ["Clone Stamp", "Clone Stamp"], ["Gradient", "Gradient"],
                ["Horizontal Type", "Horizontal Type"], ["Hand", "Hand"], ["Vùng chọn mới", "New Selection"], ["Cộng vào vùng chọn (⇧)", "Add to Selection (⇧)"],
                ["Giao với vùng chọn (⇧ Alt)", "Intersect with Selection (⇧ Alt)"]
            ]
            verify(names.length + tools.length >= 40)
            const labels = []
            const walk = items => {
                for (const it of items) {
                    if (it.l)
                        labels.push(it.l)
                    if (it.sub)
                        walk(it.sub)
                }
            }
            for (const m of MenuSpec.MENUS)
                walk(m.items)
            for (const [vi, comment, en] of names) {
                verify(labels.indexOf(vi) >= 0, "a menu item: " + vi)
                compare(Language.translate("en", "MenuSpec", vi, comment), en, vi)
            }
            const shown = Object.keys(ToolSpec.TOOL).map(k => ToolSpec.TOOL[k].n).concat(ToolSpec.MODES.map(m => m[1]))
            for (const [vi, en] of tools) {
                verify(shown.indexOf(vi) >= 0, "a tool or its option: " + vi)
                compare(Language.translate("en", "ToolSpec", vi), en, vi)
            }
            compare(MenuSpec.MENUS.map(m => Language.translate("en", "MenuSpec", m.title)),
                    ["&File", "&Edit", "&Image", "&Layer", "T&ype", "&Select", "Fil&ter", "&View", "&Window", "&Help"], "the menu bar")
        }

        // Counts: the number is qsTr()'s third argument, so English says "1 file", "3 files".
        function test_counts() {
            const n = (ctx, s, k) => Language.translate("en", ctx, s, "", k)
            compare([n("DocTabs", "%n tài liệu khác", 1), n("DocTabs", "%n tài liệu khác", 3)], ["%n other document", "%n other documents"])
            compare([n("ExportAsDialog", "%1 file", 1), n("ExportAsDialog", "%1 file", 2)], ["%1 file", "%1 files"])
            compare([n("SaveChangesDialog", "Không lưu thì mất %1 bước chỉnh từ lần lưu trước.", 1),
                     n("SaveChangesDialog", "Không lưu thì mất %1 bước chỉnh từ lần lưu trước.", 5)],
                    ["Without saving, %1 edit since the last save is lost.", "Without saving, %1 edits since the last save are lost."])
            compare(Language.translate("fr", "MenuSpec", "Tạo mới…"), "", "no such language")
        }

        // Tooltips with Photoshop's name: Vietnamese keeps it after " · " (the window as it shows them), English is that
        // name alone, in each of the 20 files that write tooltips so.
        function test_tooltips() {
            const w = openMain({ startState: "Main" })
            const tip = s => find(w.contentItem, o => o instanceof Item && o.ToolTip.text === s)
            for (const s of ["Chế độ màn hình · Screen Mode (F)", "Layer mới · Create a new layer (Ctrl ⇧ N)",
                             "Đổi màu trước, sau · Switch Foreground and Background Colors (X)", "Lọc: layer chữ · Filter for type layers"])
                verify(tip(s), s)
            const samples = [
                ["AdjustRows", "Tùy chọn Auto · Auto Options"],
                ["AdjustSpec", "Kéo trên ảnh để chỉnh · Targeted Adjustment"],
                ["BrushSettingsPanel", "Lưu thành Brush preset mới · Create new brush"],
                ["BrushesPanel", "Nhóm mới · Create new group"],
                ["ChannelsPanel", "Ẩn hiện · Indicates channel visibility"],
                ["CharacterPanel", "Kiểu chữ · Set the font style"],
                ["ColorPair", "Màu sau · Background Color"],
                ["ColorPanel", "Thêm vào Swatches · Add to Swatches"],
                ["ExportAsDialog", "Thu nhỏ · Zoom Out"],
                ["FilterDialog", "Thu nhỏ · Zoom Out"],
                ["GradientsPanel", "Nhóm mới · Create new group"],
                ["HistogramPanel", "Tính lại từ ảnh hiện tại · Uncached Refresh"],
                ["HistoryPanel", "Tài liệu mới từ bước này · New Document from Current State"],
                ["ImageSizeDialog", "Giữ tỉ lệ W và H · Constrain Proportions"],
                ["LayersPanel", "Lọc: layer điểm ảnh · Filter for pixel layers"],
                ["NavigatorPanel", "Thu nhỏ · Zoom Out"],
                ["ParagraphPanel", "Căn trái · Left Align Text"],
                ["PropertiesPanel", "Dọc · Portrait"],
                ["SwatchesPanel", "Nhóm mới · Create new group"],
                ["Toolbar", "Chế độ màn hình · Screen Mode (F)"]
            ]
            for (const [ctx, s] of samples)
                compare(Language.translate("en", ctx, s), s.slice(s.lastIndexOf(" · ") + 3), ctx + ": " + s)
        }
    }
}
