import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty
import "../qml/MenuSpec.js" as MenuSpec

// U4: the window's frame (Main), the menus (AppMenu, MenuSpec.js) against the Menus artboard, the keys, screen modes,
// Tab, the background job, the status bar, dialogs and spaces by name, files dropped, the title bar's hit test.
Item {
    id: root
    width: 200
    height: 100
    Component { id: mainC; Main {} }
    // Stand-ins for dialogs and spaces other tasks add (Main.types is its cache of the module's types).
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
        name: "U4"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanupTestCase() {  // as the other files expect them
            init()
            Theme.dark = true
            Theme.accent = Theme.accents[0]
            Theme.fontSize = "m"
        }

        function open(props) {
            const w = createTemporaryObject(mainC, root, props || {})
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function region(w, type) { return find(w.contentItem, o => o instanceof type) }
        function box(w, o) {
            const p = o.mapToItem(w.contentItem, 0, 0)
            return [p.x, p.y, o.width, o.height]
        }
        function bar(w) { return region(w, AppMenu) }
        function openMenu(w, i) {
            const b = bar(w), m = b.menuAt(i)
            if (!m.visible)
                mouseClick(b.itemAt(i))
            tryVerify(() => m.opened)
            waitForRendering(w.contentItem)
            return m
        }
        // A menu's rows as the Menus artboard writes them: "!" greyed, "*" ticked, ">" a submenu, "label|keys", "-".
        function rows(m) {
            const out = []
            for (let i = 0; i < m.count; i++) {
                const it = m.itemAt(i)
                out.push(!(it instanceof MenuItem) ? "-" : (it.enabled ? "" : "!") + (it.tick ? "*" : "") + (it.subMenu ? ">" : "") + it.text + (it.keys ? "|" + it.keys : ""))
            }
            return out
        }
        function row(m, label) {
            for (let i = 0; i < m.count; i++)
                if (m.itemAt(i) instanceof MenuItem && m.itemAt(i).text === label)
                    return m.itemAt(i)
            return null
        }
        // The artboard's rows as this build draws them: an item whose dialog or space is not in the module yet is greyed
        // (Main.can()), and a greyed submenu has no arrow (PopupMenu).
        function drawn(w, rowsOf, spec) {
            return rowsOf.map((r, i) => {
                if (r === "-")
                    return r
                const it = spec[i]
                let s = r.replace("^", "")
                if ((it.dialog && !w.hasDialog(it.dialog)) || (it.space && !w.hasSpace(it.space)))
                    s = "!" + s.replace(/^!/, "")
                return s.replace(/^(!\*?)>/, "$1")
            })
        }
        function inject(w, types) { w.types = Object.assign({}, w.types, types) }

        // ---- the frame -----------------------------------------------------------------------------------------------
        // Lưới bố cục of Tokens: 44, 36, 44, 28, 36, 308, 24 at 1.440 × 900; the canvas as Docs.viewport less the rulers.
        function test_frame() {
            const w = open({ startState: "Main" })
            compare(box(w, region(w, TopBar)), [0, 0, 1440, 44])
            compare(box(w, region(w, OptionsBar)), [0, 44, 1440, 36])
            compare(box(w, region(w, Toolbar)), [0, 80, 44, 796])
            compare(box(w, region(w, DocTabs)), [44, 80, 1052, 28])
            compare(box(w, region(w, Viewport)), [44, 108, 1052, 768])
            compare(box(w, region(w, SideColumn)), [1096, 80, 36, 796])
            compare(box(w, region(w, Dock)), [1132, 80, 308, 796])
            compare(box(w, region(w, StatusBar)), [0, 876, 1440, 24])
            verify(!region(w, StartScreen).visible)
            compare([w.minimumWidth, w.minimumHeight], [1280, 800])
            w.width = 1280
            w.height = 800
            waitForRendering(w.contentItem)
            compare(box(w, region(w, Viewport)), [44, 108, 892, 668], "the canvas takes what the window loses")
        }
        function test_start() {  // no document: the top bar and the start screen
            const w = open({ startState: "Start" })
            compare(Docs.docs.length, 0)
            compare(box(w, region(w, StartScreen)), [0, 44, 1440, 856])
            for (const t of [OptionsBar, Toolbar, DocTabs, Viewport, SideColumn, Dock, StatusBar])
                verify(!region(w, t).visible, t)
            verify(!region(w, JobIndicator).visible, "the artboard has no job")
            keyClick(Qt.Key_F)
            compare(Tools.screen, 0, "no screen modes on the start screen")
            verify(region(w, TopBar).visible)
        }
        function test_retouch() {  // Retouch.dc.html's INIT
            const w = open({ startState: "Retouch" })
            compare(Tools.tool, "spot")
            compare(Tools.dockTab.p, "Retouch")
            compare(Docs.current.active.props.name, "Heal")
            verify(!Docs.job.on)
            verify(!region(w, JobIndicator).visible)
        }
        function test_theme() {  // Thiết lập's theme, accent and font size; --theme over the theme
            const w = open({ startState: "Main" })
            verify(Theme.dark)
            verify(Qt.colorEqual(w.color, "#161617"))
            Prefs.set("theme", 1)
            verify(!Theme.dark)
            verify(Qt.colorEqual(w.color, "#e7e7e8"))
            verify(Qt.colorEqual(region(w, StatusBar).color, "#f3f3f3"))
            Prefs.set("accent", "#E0A53A")
            verify(Qt.colorEqual(Theme.accent, "#E0A53A"))
            Prefs.set("fontSize", 2)
            compare(Theme.fontSize, "l")
        }
        function test_theme_flag() {  // --theme light holds over Thiết lập's for the run
            const w = open({ startState: "Main", startTheme: "light" })
            verify(!Theme.dark)
            Prefs.set("theme", 0)
            verify(!Theme.dark)
            verify(Qt.colorEqual(w.color, "#e7e7e8"))
        }

        // ---- menus ---------------------------------------------------------------------------------------------------
        // The Menus artboard's menus, copied: on chân dung.psd as Main opens (Dodge & Burn picked, no selection).
        readonly property var artboard: [
            ['Tạo mới…|Ctrl N', 'Mở…|Ctrl O', '>Mở gần đây', '-', 'Đóng|Ctrl W', 'Đóng tất cả|Ctrl Alt W', '-', 'Lưu|Ctrl S', 'Lưu thành…|Ctrl ⇧ S', 'Lưu bản sao…|Ctrl Alt S', 'Hoàn nguyên|F12', '-', 'Place Embedded…', '>Xuất', '-', 'Thoát|Ctrl Q'],
            ['Hoàn tác Brush|Ctrl Z', '!Làm lại|Ctrl ⇧ Z', 'Đổi qua lại bước cuối|Ctrl Alt Z', 'Fade Brush…|Ctrl ⇧ F', '-', '!Cắt|Ctrl X', '!Sao chép|Ctrl C', '!Sao chép hợp nhất|Ctrl ⇧ C', 'Dán|Ctrl V', 'Dán tại chỗ|Ctrl ⇧ V', '!Xóa|Del', '-', 'Fill…|⇧ F5', '!Stroke…', '-', 'Free Transform|Ctrl T', '>Transform', '-', 'Define Brush Preset…', '-', 'Color Settings…|Ctrl ⇧ K', 'Assign Profile…', 'Convert to Profile…', '-', 'Phím tắt…|Ctrl ⇧ Alt K', 'Thiết lập…|Ctrl K'],
            ['>Mode', '-', '^>Adjustments', '-', 'Auto Tone|Ctrl ⇧ L', 'Auto Contrast|Ctrl ⇧ Alt L', 'Auto Color|Ctrl ⇧ B', '-', 'Image Size…|Ctrl Alt I', 'Canvas Size…|Ctrl Alt C', '>Image Rotation', '!Crop', 'Trim…', 'Reveal All', '-', 'Nhân bản…'],
            // The artboard draws Bỏ nhóm on; the prototype's REQ (ungroup: grp) greys it with Dodge & Burn picked.
            ['>Mới', 'Nhân bản layer…', 'Đổi tên layer…', '>Xóa', '-', '>Layer Style', '-', '>Fill Layer mới', '>Adjustment Layer mới', '-', '>Layer Mask', 'Tạo Clipping Mask|Ctrl Alt G', '-', '>Smart Objects', '>Rasterize', '-', 'Nhóm layer|Ctrl G', '!Bỏ nhóm|Ctrl ⇧ G', 'Ẩn layer|Ctrl ,', '-', '>Sắp xếp', '>Căn', '>Phân bố', '-', 'Khóa layer…|Ctrl /', '!Liên kết layer', '-', 'Gộp xuống|Ctrl E', 'Gộp layer đang hiện|Ctrl ⇧ E', 'Làm phẳng ảnh'],
            ['>Panel', '>Anti-Alias', '-', '!Chuyển thành chữ đoạn', '!Rasterize Type Layer', '-', '>Cỡ xem trước font', 'Cập nhật mọi layer chữ', 'Thay font thiếu…'],
            ['Chọn tất cả|Ctrl A', '!Bỏ chọn|Ctrl D', '!Chọn lại|Ctrl ⇧ D', '!Đảo vùng chọn|Ctrl ⇧ I', '-', 'Mọi layer|Ctrl Alt A', '-', 'Color Range…', 'Subject', '^>Chân dung', '-', 'Select and Mask…|Ctrl Alt R', '!>Modify', '!Grow', '!Similar', '!Transform Selection', '-', 'Sửa trong Quick Mask|Q', '-', 'Nạp vùng chọn…', '!Lưu vùng chọn…'],
            ['Gaussian Blur|Ctrl Alt F', '-', '>Neural Filters', 'Liquify…|Ctrl ⇧ X', '-', '^>Blur', '>Noise', '>Sharpen', '>Other'],
            ['Phóng to|Ctrl +', 'Thu nhỏ|Ctrl −', 'Vừa màn hình|Ctrl 0', '100%|Ctrl 1', '-', '*Lưới điểm ảnh', '>Chế độ màn hình', '-', '*Extras|Ctrl H', '>Hiện', '-', '*Thước|Ctrl R', '-', '*Bắt dính|Ctrl ⇧ ;', '>Bắt dính vào', '-', 'Khóa guide|Ctrl Alt ;', 'Xóa guide', 'Guide mới…'],
            ['Adjustments', 'Brush Settings|F5', 'Brushes', 'Channels', 'Character', '*Color|F6', 'Gradients', 'Histogram', 'History', 'Info|F8', '*Layers|F7', 'Navigator', 'Paragraph', '*Properties', 'Retouch', 'Swatches', '-', '*Thanh tùy chọn', '*Thanh công cụ'],
            ['Hướng dẫn photo-beauty|F1', 'Phím tắt…', '-', 'Thông tin hệ thống…', 'Mở thư mục nhật ký', '-', 'Kiểm tra cập nhật…', 'Gửi báo lỗi…', '-', 'Giấy phép…', 'Về photo-beauty']
        ]
        readonly property var artboardSubs: ({
            "Image>Adjustments": ['Brightness/Contrast…', 'Levels…|Ctrl L', 'Curves…|Ctrl M', 'Exposure…', '-', 'Vibrance…', 'Hue/Saturation…|Ctrl U', 'Color Balance…|Ctrl B', 'Black & White…|Ctrl ⇧ Alt B', 'Photo Filter…', 'Channel Mixer…', 'Color Lookup…', '-', 'Invert|Ctrl I', 'Posterize…', 'Threshold…', 'Gradient Map…', 'Selective Color…', '-', 'Desaturate|Ctrl ⇧ U', 'Equalize'],
            "Select>Chân dung": ['Da', 'Mắt', 'Môi', 'Răng', '-', 'Da, trừ mắt và môi'],
            "Filter>Blur": ['Average', 'Blur', 'Blur More', 'Box Blur…', 'Gaussian Blur…', 'Motion Blur…', 'Surface Blur…']
        })
        // The other submenus: SUBS of Main.dc.html, labels and keys ("!" always greyed).
        readonly property var protoSubs: ({
            "File>Xuất": ["Xuất nhanh PNG", "Xuất thành…|Ctrl ⇧ Alt W"],
            "Edit>Transform": ["Lặp lại|Ctrl ⇧ T", "-", "Scale", "Rotate", "Skew", "Distort", "Perspective", "Warp", "-", "Xoay 180°", "Xoay 90° theo chiều kim đồng hồ", "Xoay 90° ngược chiều kim đồng hồ", "-", "Lật ngang", "Lật dọc"],
            "Image>Mode": ["RGB Color", "Grayscale", "-", "8 Bits/Channel", "16 Bits/Channel"],
            "Image>Image Rotation": ["180°", "90° theo chiều kim đồng hồ", "90° ngược chiều kim đồng hồ", "Tùy ý…", "-", "Lật canvas ngang", "Lật canvas dọc"],
            "Layer>Mới": ["Layer…|Ctrl ⇧ N", "Nhóm…", "Chuyển thành Background", "-", "Sao chép sang layer mới|Ctrl J", "Cắt sang layer mới|Ctrl ⇧ J"],
            "Layer>Xóa": ["Layer", "Layer đang ẩn"],
            "Layer>Layer Style": ["Blending Options…", "-", "Drop Shadow…", "Stroke…", "Color Overlay…", "Gradient Overlay…"],
            "Layer>Fill Layer mới": ["Solid Color…", "Gradient…"],
            "Layer>Adjustment Layer mới": ["Brightness/Contrast…", "Levels…", "Curves…", "Exposure…", "Vibrance…", "Hue/Saturation…", "Color Balance…", "Black & White…", "Photo Filter…", "Channel Mixer…", "Color Lookup…", "Invert…", "Posterize…", "Threshold…", "Gradient Map…", "Selective Color…"],
            "Layer>Layer Mask": ["Hiện tất cả", "Ẩn tất cả", "Hiện vùng chọn", "Ẩn vùng chọn", "-", "Xóa", "Áp dụng", "-", "Tắt", "Bỏ liên kết"],
            "Layer>Smart Objects": ["Chuyển thành Smart Object", "Sửa nội dung", "Rasterize"],
            "Layer>Rasterize": ["Layer chữ", "Hình", "Smart Object", "Layer"],
            "Layer>Sắp xếp": ["Đưa lên trên cùng|Ctrl ⇧ ]", "Đưa lên một bậc|Ctrl ]", "Đưa xuống một bậc|Ctrl [", "Đưa xuống dưới cùng|Ctrl ⇧ ["],
            "Layer>Căn": ["Mép trên", "Giữa theo chiều dọc", "Mép dưới", "-", "Mép trái", "Giữa theo chiều ngang", "Mép phải"],
            "Layer>Phân bố": ["!Mép trên", "!Giữa theo chiều dọc", "!Mép dưới", "-", "!Mép trái", "!Giữa theo chiều ngang", "!Mép phải"],
            "Type>Panel": ["Character", "Paragraph"],
            "Type>Anti-Alias": ["None", "Sharp", "Crisp", "Strong", "Smooth"],
            "Type>Cỡ xem trước font": ["Không", "Nhỏ", "Vừa", "Lớn", "Rất lớn"],
            "Select>Modify": ["Border…", "Smooth…", "Expand…", "Contract…", "Feather…|⇧ F6"],
            "Filter>Neural Filters": ["Skin Smoothing…"],
            "Filter>Noise": ["Add Noise…", "Despeckle", "Dust & Scratches…", "Median…", "Reduce Noise…"],
            "Filter>Sharpen": ["Sharpen", "Sharpen More", "Sharpen Edges", "Unsharp Mask…"],
            "Filter>Other": ["Custom…", "High Pass…", "Maximum…", "Minimum…", "Offset…"],
            "View>Chế độ màn hình": ["Chế độ chuẩn", "Toàn màn hình có menu", "Toàn màn hình"],
            "View>Hiện": ["Viền layer", "Viền vùng chọn|Ctrl H", "Xem trước cọ", "-", "Guide|Ctrl ;", "Lưới|Ctrl '"],
            "View>Bắt dính vào": ["Guide", "Lưới", "Layer", "Biên tài liệu", "-", "Tất cả", "Không"]
        })

        function test_menus() {
            const w = open({ startState: "Main" })
            const b = bar(w)
            compare(b.count, 10)
            const titles = []
            for (let i = 0; i < b.count; i++)
                titles.push(b.itemAt(i).contentItem.text)
            compare(titles, ["File", "Edit", "Image", "Layer", "Type", "Select", "Filter", "View", "Window", "Help"])
            for (let i = 0; i < 10; i++) {
                const m = openMenu(w, i)
                compare(m.width, MenuSpec.MENUS[i].w, titles[i])
                compare(rows(m), drawn(w, artboard[i], m.spec), titles[i])
                for (const k in artboardSubs) {
                    const at = k.split(">")
                    if (at[0] !== titles[i])
                        continue
                    const spec = m.spec.find(s => s.l === at[1])
                    compare(rows(row(m, at[1]).subMenu), drawn(w, artboardSubs[k], spec.sub), k)
                    compare(row(m, at[1]).subMenu.width, spec.w, k)
                }
                keyClick(Qt.Key_Escape)
                tryVerify(() => !m.visible)
            }
            // The other submenus' items, with a selection so that Modify opens too.
            Docs.current.run({ op: "select", shape: "all" })
            const plain = r => r.replace(/^[!*>]+/, "")
            for (const k in protoSubs) {
                const at = k.split(">"), m = openMenu(w, titles.indexOf(at[0])), sub = row(m, at[1]).subMenu
                verify(sub, k)
                compare(rows(sub).map(plain), protoSubs[k].map(plain), k)
                keyClick(Qt.Key_Escape)
                tryVerify(() => !m.visible)
            }
            const recent = row(openMenu(w, 0), "Mở gần đây").subMenu  // the recent files, then Xóa danh sách
            compare(rows(recent), Docs.recent.map(r => r.file).concat(["-", "Xóa danh sách"]))
            keyClick(Qt.Key_Escape)
            const ticks = (i, sub) => { const r = rows(row(openMenu(w, i), sub).subMenu); keyClick(Qt.Key_Escape); return r }
            compare(ticks(2, "Mode"), ["*RGB Color", "Grayscale", "-", "8 Bits/Channel", "*16 Bits/Channel"], "RGB/16")
            compare(ticks(4, "Anti-Alias"), ["None", "*Sharp", "Crisp", "Strong", "Smooth"])
            compare(ticks(4, "Cỡ xem trước font"), ["Không", "Nhỏ", "*Vừa", "Lớn", "Rất lớn"])
            compare(ticks(7, "Bắt dính vào"), ["*Guide", "*Lưới", "*Layer", "*Biên tài liệu", "-", "Tất cả", "Không"])
            keySequence("Ctrl+Shift+;")  // Bắt dính off
            verify(!Tools.snap)
            Docs.current.run({ op: "setMode", mode: "Gray", bits: 8 })
            compare(ticks(2, "Mode"), ["RGB Color", "*Grayscale", "-", "*8 Bits/Channel", "16 Bits/Channel"], "Gray/8")
        }
        // No document: what Start.dc.html's menus leave on (and the recent files), the rest greyed.
        function test_menus_no_document() {
            const w = open({ startState: "Start" })
            const on = [["Tạo mới…", "Mở…", "Mở gần đây", "Thoát"], ["Color Settings…", "Phím tắt…", "Thiết lập…"], [], [], ["Panel"], [], [], [],
                MenuSpec.MENUS[8].items.filter(s => s.l).map(s => s.l), MenuSpec.MENUS[9].items.filter(s => s.l).map(s => s.l)]
            for (let i = 0; i < 10; i++) {
                const m = openMenu(w, i)
                for (let r = 0; r < m.count; r++) {
                    const it = m.itemAt(r), spec = m.spec[r]
                    if (!(it instanceof MenuItem))
                        continue
                    const there = !spec.dialog || w.hasDialog(spec.dialog)
                    compare(it.enabled, on[i].indexOf(it.text) >= 0 && there, it.text)
                }
                keyClick(Qt.Key_Escape)
                tryVerify(() => !m.visible)
            }
            keySequence("Ctrl+Z")  // greyed keys do nothing
            keySequence("Ctrl+A")
            compare(Docs.docs.length, 0)
        }
        function test_menus_selection() {
            const w = open({ startState: "Main" })
            let m = openMenu(w, 5)
            compare(["Bỏ chọn", "Chọn lại", "Đảo vùng chọn", "Lưu vùng chọn…"].map(l => row(m, l).enabled), [false, false, false, false])
            keyClick(Qt.Key_Escape)
            keySequence("Ctrl+A")
            compare(Docs.current.selection, "all")
            m = openMenu(w, 5)
            compare(["Bỏ chọn", "Chọn lại", "Đảo vùng chọn", "Lưu vùng chọn…"].map(l => row(m, l).enabled), [true, false, true, true])
            verify(row(m, "Modify").subMenu, "Modify opens")
            keyClick(Qt.Key_Escape)
            verify(row(openMenu(w, 1), "Cắt").enabled)
            keyClick(Qt.Key_Escape)
            verify(row(openMenu(w, 2), "Crop").enabled)
            keyClick(Qt.Key_Escape)
            keySequence("Ctrl+D")
            compare(Docs.current.selection, null)
            m = openMenu(w, 5)
            compare(["Bỏ chọn", "Chọn lại"].map(l => row(m, l).enabled), [false, true])
            keyClick(Qt.Key_Escape)
        }
        function test_undo_label() {  // Hoàn tác, Làm lại and Fade name their step
            const w = open({ startState: "Main" })
            let m = openMenu(w, 1)
            compare([m.itemAt(0).text, m.itemAt(1).text, m.itemAt(3).text], ["Hoàn tác Brush", "Làm lại", "Fade Brush…"])
            keyClick(Qt.Key_Escape)
            keySequence("Ctrl+Shift+N")
            m = openMenu(w, 1)
            compare([m.itemAt(0).text, m.itemAt(1).text, m.itemAt(3).text], ["Hoàn tác Layer mới", "Làm lại", "Fade Layer mới…"])
            keyClick(Qt.Key_Escape)
            keySequence("Ctrl+Z")
            m = openMenu(w, 1)
            compare([m.itemAt(0).text, m.itemAt(1).text], ["Hoàn tác Brush", "Làm lại Layer mới"])
            verify(m.itemAt(1).enabled)
            keyClick(Qt.Key_Escape)
            Docs.current.jumpTo(0)
            m = openMenu(w, 1)
            compare([m.itemAt(0).text, m.itemAt(0).enabled, m.itemAt(3).text], ["Hoàn tác", false, "Fade…"])
            keyClick(Qt.Key_Escape)
        }
        // With a menu open, the mouse on another title opens it, ← → go along, Esc closes; Alt and the letter open one.
        function test_menu_bar() {
            const w = open({ startState: "Main" })
            const b = bar(w)
            mouseClick(b.itemAt(0))
            tryVerify(() => b.menuAt(0).opened)
            verify(b.open)
            mouseMove(b.itemAt(1), 5, 5)
            mouseMove(b.itemAt(1), 10, 10)
            tryVerify(() => b.menuAt(1).opened && !b.menuAt(0).visible)
            keyClick(Qt.Key_Right)
            tryVerify(() => b.menuAt(2).opened)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            tryVerify(() => b.menuAt(0).opened)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !b.open)
            mouseMove(b.itemAt(3), 5, 5)  // no menu open: the mouse opens nothing
            mouseMove(b.itemAt(3), 10, 10)
            wait(50)
            verify(!b.open)
            const letters = [Qt.Key_F, Qt.Key_E, Qt.Key_I, Qt.Key_L, Qt.Key_Y, Qt.Key_S, Qt.Key_T, Qt.Key_V, Qt.Key_W, Qt.Key_H]
            for (let i = 0; i < 10; i++) {
                keyClick(letters[i], Qt.AltModifier)
                tryVerify(() => b.menuAt(i).opened, 1000, b.menuAt(i).title)
                keyClick(Qt.Key_Escape)
                tryVerify(() => !b.open)
            }
            mouseClick(b.itemAt(4))  // a title clicked again closes its menu
            tryVerify(() => b.menuAt(4).opened)
            mouseClick(b.itemAt(4))
            tryVerify(() => !b.open)
            keyClick(Qt.Key_S, Qt.AltModifier)  // keys: ↓ to a submenu, → opens it, Esc closes one, then the other
            const m = b.menuAt(5)
            tryVerify(() => m.opened)
            while (m.currentIndex < 0 || m.itemAt(m.currentIndex).text !== "Chân dung")
                keyClick(Qt.Key_Down)
            const sub = m.itemAt(m.currentIndex).subMenu
            keyClick(Qt.Key_Right)
            tryVerify(() => sub.opened)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !sub.visible)
            verify(m.visible)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !b.open)
        }
        function test_menu_runs() {  // a click runs the item, in a submenu too; Enter the one ↓ picked
            const w = open({ startState: "Main" })
            const spy = createTemporaryObject(spyC, root, { target: w, signalName: "acted" })
            const n = Docs.current.history.length
            mouseClick(row(openMenu(w, 3), "Nhóm layer"))
            tryCompare(Docs.current.history, "length", n + 1)
            compare(Docs.current.history[n].name, "Nhóm layer")
            compare(spy.count, 1)
            let m = openMenu(w, 8)
            mouseClick(row(m, "Channels"))
            compare(Tools.dockTab.l, "Channels")
            m = openMenu(w, 0)
            const recent = row(m, "Mở gần đây")
            mouseClick(recent)
            tryVerify(() => recent.subMenu.opened)
            waitForRendering(w.contentItem)
            mouseClick(row(recent.subMenu, "cuoi-trang-an.psd"))
            compare(Docs.current.file, "cuoi-trang-an.psd")
            verify(!bar(w).open)
            m = openMenu(w, 7)  // View: Thước by keys
            keyClick(Qt.Key_Down)
            while (m.currentIndex >= 0 && m.itemAt(m.currentIndex).text !== "Thước")
                keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            verify(!Tools.rulers)
            const rename = createTemporaryObject(spyC, root, { target: w, signalName: "requested" })
            mouseClick(row(openMenu(w, 3), "Đổi tên layer…"))  // Layers' (U9)
            compare(rename.count, 1)
            compare(rename.signalArguments[0][0], "@rename")
        }
        Component { id: spyC; SignalSpy {} }

        // ---- keys ----------------------------------------------------------------------------------------------------
        // Each menu key runs its item once it can run: as Main opens, with a selection, one to bring back, a group
        // picked, a step undone; dialogs and spaces stood in for. Ctrl Q quits.
        function test_keys() {
            const w = open({ startState: "Main" })
            const spy = createTemporaryObject(spyC, root, { target: w, signalName: "acted" })
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
            inject(w, stand)
            const setups = [() => {}, () => Docs.current.run({ op: "select", shape: "all" }),
                () => { Docs.current.run({ op: "select", shape: "all" }); Docs.current.run({ op: "deselect" }) },
                () => Docs.current.run({ op: "selectLayers", layers: [Docs.current.layers.find(l => l.kind === "group").id] }), () => Docs.current.undo()]
            let ran = 0
            for (const k of MenuSpec.keyed()) {
                if (k.item.act === "@quit")
                    continue
                const name = k.path.join(" › ")
                for (const s of setups) {
                    Docs.reset()
                    Tools.reset()
                    w.panelsHidden = false
                    w.contentItem.forceActiveFocus()
                    s()
                    if (!w.can(k.item))
                        continue
                    spy.clear()
                    keySequence(k.sequences[0])
                    compare(spy.count, 1, name)
                    compare(spy.signalArguments[0][0].l, k.item.l, name)
                    if (k.item.dialog) {
                        let d = null
                        tryVerify(() => (d = probe(w)) && d.opened, 1000, name)
                        d.close()
                        tryVerify(() => !probe(w), 1000, name)
                    } else if (k.item.space) {
                        verify(w.space, name)
                        w.space.finished(false)
                    } else if (k.item.act === "@openFile") {
                        keyClick(Qt.Key_Escape)  // its file dialog
                    }
                    ran++
                    break
                }
            }
            compare(ran, MenuSpec.keyed().length - 1, "all but Thoát")
            Docs.reset()
            spy.clear()
            keySequence("Ctrl+D")  // off without a selection
            compare(spy.count, 0)
            keySequence("Ctrl+Z")  // a step back
            compare(Docs.current.index, Docs.current.history.length - 2)
            keySequence("F7")
            compare(Tools.dockTab.l, "Layers")
            keySequence("Ctrl+H")  // Extras, not Viền vùng chọn
            compare([Tools.extras, Tools.selEdges], [false, true])
            keySequence("Ctrl+=")  // as Ctrl +
            verify(Docs.current.view.zoom > 0.125)
            keySequence("Ctrl+Q")
            tryVerify(() => !w.visible, 1000, "Thoát closes the window")
        }
        // F: chuẩn, toàn màn hình có menu (no tabs, no status bar), toàn màn hình (the canvas only, black, a way out).
        function test_screen_modes() {
            const w = open({ startState: "Main" })
            keyClick(Qt.Key_F)
            compare(Tools.screen, 1)
            waitForRendering(w.contentItem)
            verify(region(w, TopBar).visible && region(w, Toolbar).visible && region(w, Dock).visible)
            verify(!region(w, DocTabs).visible && !region(w, StatusBar).visible)
            compare(box(w, region(w, Viewport)), [44, 80, 1052, 820])
            keyClick(Qt.Key_F)
            compare(Tools.screen, 2)
            waitForRendering(w.contentItem)
            for (const t of [TopBar, OptionsBar, Toolbar, DocTabs, SideColumn, Dock, StatusBar])
                verify(!region(w, t).visible, t)
            compare(box(w, region(w, Viewport)), [0, 0, 1440, 900])
            verify(Qt.colorEqual(region(w, Viewport).color, "#000000"))
            const out = find(w.contentItem, o => o instanceof AbstractButton && o.text === "Thoát toàn màn hình (F)")
            verify(out.visible)
            keyClick(Qt.Key_F)
            compare(Tools.screen, 0)
            verify(!out.visible)
            waitForRendering(w.contentItem)
            compare(box(w, region(w, Viewport)), [44, 108, 1052, 768])
            Tools.screen = 2
            waitForRendering(w.contentItem)
            mouseClick(out)
            compare(Tools.screen, 0)
            waitForRendering(w.contentItem)  // the tabs and the canvas take clicks: laid out again, they leave the top bar's
            const m = openMenu(w, 7)  // View › Chế độ màn hình, ticked
            compare(rows(row(m, "Chế độ màn hình").subMenu), ["*Chế độ chuẩn", "Toàn màn hình có menu", "Toàn màn hình"])
            keyClick(Qt.Key_Escape)
        }
        function test_tab() {  // Tab hides the panels while no control has the focus
            const w = open({ startState: "Main" })
            keyClick(Qt.Key_Tab)
            verify(w.panelsHidden)
            waitForRendering(w.contentItem)
            for (const t of [OptionsBar, Toolbar, SideColumn, Dock])
                verify(!region(w, t).visible, t)
            verify(region(w, TopBar).visible && region(w, DocTabs).visible && region(w, StatusBar).visible)
            compare(box(w, region(w, Viewport)), [0, 72, 1440, 804])
            keyClick(Qt.Key_Tab)
            verify(!w.panelsHidden)
            const zoom = find(region(w, StatusBar), o => o instanceof TextField)
            zoom.forceActiveFocus()
            keyClick(Qt.Key_Tab)  // from a field, on to the next control
            verify(!w.panelsHidden)
            verify(!zoom.activeFocus)
            const ji = region(w, JobIndicator), show = find(ji, o => o instanceof AbstractButton && !(o instanceof IconButton))
            show.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)  // from a button too: the job's Hủy next
            verify(!w.panelsHidden)
            verify(find(ji, o => o instanceof IconButton).activeFocus)
        }
        // A press with a menu open closes it and stops there, not on the canvas.
        function test_scrim() {
            const w = open({ startState: "Main" })
            const vp = region(w, Viewport)
            const probe = Qt.createQmlObject("import QtQuick; MouseArea { anchors.fill: parent; property int n: 0; onPressed: n++ }", vp)
            openMenu(w, 0)
            mouseClick(vp, 300, 300)
            tryVerify(() => !bar(w).open)
            compare(probe.n, 0)
            wait(10)  // that press done with
            mouseClick(vp, 300, 300)
            compare(probe.n, 1)
            mouseClick(find(region(w, JobIndicator), o => o instanceof AbstractButton))  // the job's details too
            tryVerify(() => region(w, TopBar).popupOpen)
            mouseClick(vp, 300, 300)
            tryVerify(() => !region(w, TopBar).popupOpen)
            compare(probe.n, 1)
        }

        // ---- the background job, the status bar ----------------------------------------------------------------------
        function test_job() {  // running: label, progress, Hủy; done: "Đã lưu …" and a tick, then gone
            const w = open({ startState: "Main" })
            const ji = region(w, JobIndicator)
            const has = t => !!find(ji, o => o instanceof Text && o.text === t && o.visible)
            verify(ji.visible)
            verify(has("Đang lưu chân dung.psd"))
            verify(has(Math.round(Docs.job.p) + "%"))
            const cancel = find(ji, o => o instanceof IconButton)
            mouseClick(cancel)
            verify(!Docs.job.on)
            verify(!ji.visible)
            const doc = Docs.current
            Docs.save(doc)
            verify(ji.visible)
            verify(has("0%"))
            while (Docs.job.run)
                Docs.tick()
            verify(has("Đã lưu chân dung.psd"))
            verify(find(ji, o => o instanceof Icon && o.name === "check").visible)
            verify(!cancel.visible && !has("100%"))
            verify(!doc.dirty)
            for (let i = 0; i < 11; i++)
                Docs.tick()
            verify(!ji.visible)
        }
        function test_job_details() {  // a click shows the details; their Hủy cancels; Esc closes them
            const w = open({ startState: "Main" })
            const ji = region(w, JobIndicator), tb = region(w, TopBar)
            const show = find(ji, o => o instanceof AbstractButton && !(o instanceof IconButton))
            const details = ji.data.find(o => o instanceof Popup)
            mouseClick(show)
            tryVerify(() => details.opened)
            verify(tb.popupOpen)
            compare(box(w, details.contentItem.parent).slice(2), [300, details.height])
            keyClick(Qt.Key_Escape)
            tryVerify(() => !details.visible)
            verify(!tb.popupOpen)
            mouseClick(show)
            tryVerify(() => details.opened)
            mouseClick(show)  // the button again closes them
            tryVerify(() => !details.visible)
            mouseClick(show)
            tryVerify(() => details.opened)
            waitForRendering(w.contentItem)
            mouseClick(find(details.contentItem, o => o instanceof Btn && o.text === "Hủy"))
            verify(!Docs.job.on)
            tryVerify(() => !details.visible)
        }
        function test_zoom_field() {  // "12,5%"; typed and Enter sets it; nonsense or Esc gives it back
            const w = open({ startState: "Main" })
            const zoom = find(region(w, StatusBar), o => o instanceof TextField)
            compare(zoom.text, "12,5%")
            mouseClick(zoom)
            zoom.selectAll()
            keyClick(Qt.Key_5)
            keyClick(Qt.Key_0)
            keyClick(Qt.Key_Return)
            compare(Docs.current.view.zoom, 0.5)
            compare(zoom.text, "50%")
            zoom.selectAll()
            keyClick(Qt.Key_X)
            keyClick(Qt.Key_Return)
            compare(zoom.text, "50%")
            zoom.selectAll()
            keyClick(Qt.Key_7)
            keyClick(Qt.Key_Escape)
            compare(zoom.text, "50%")
            compare(Docs.current.view.zoom, 0.5)
            keySequence("Ctrl+1")  // the view's zoom shows
            compare(zoom.text, "100%")
        }
        function test_status() {  // size, mode and profile; the box Thông tin picks
            const w = open({ startState: "Main" })
            const sb = region(w, StatusBar)
            const has = t => !!find(sb, o => o instanceof Text && o.text === t && o.visible)
            verify(has("4.400 × 5.600 px · 300 ppi"))
            verify(has("RGB/16 · sRGB IEC61966-2.1"))
            verify(has("148 MB / 1,02 GB"))
            verify(has("3,1 / 9,6 GB") && has("1,2 GB"))
            const dd = find(sb, o => o instanceof Dropdown)
            const shown = ["148 MB / 1,02 GB", "sRGB IEC61966-2.1", "37,25 × 47,41 cm", "3,1 GB · 1,2 GB", "0,4 giây", "Brush"]
            for (let i = 0; i < 6; i++) {
                mouseClick(dd)
                tryVerify(() => dd.popup.opened)
                waitForRendering(w.contentItem)
                const opt = dd.popup.contentItem.itemAtIndex(i)
                mouseClick(opt)
                compare(Tools.statInfo, i)
                verify(has(shown[i]), shown[i])
            }
            Tools.xf = true
            verify(has("Free Transform"))
            Docs.select(2)  // hoang-hon.tif
            verify(has("1.800 × 1.013 px · 300 ppi"))
            verify(has("RGB/8 · sRGB IEC61966-2.1"))
        }

        // ---- dialogs, spaces, files ------------------------------------------------------------------------------------
        function test_dialogs() {  // by name, with params; greyed while the file is not there
            const w = open({ startState: "Main" })
            verify(!w.hasDialog("Nope"))
            ignoreWarning(/openDialog: Nope/)
            compare(w.openDialog("Nope"), null)
            const settings = MenuSpec.MENUS[1].items.find(s => s.l === "Thiết lập…")
            if (!w.hasDialog("Settings"))
                verify(!w.can(settings), "greyed while SettingsDialog.qml is not there")
            inject(w, { SettingsDialog: probeDialogC })
            verify(w.can(settings))
            keySequence("Ctrl+K")  // its key
            let d = null
            tryVerify(() => (d = probe(w)) && d.opened)
            compare(d.params, {})
            const n = Docs.current.index
            keySequence("Ctrl+Z")  // the window's keys rest under a dialog
            compare(Docs.current.index, n)
            d.close()
            tryVerify(() => !probe(w), 1000, "gone once closed")
            mouseClick(row(openMenu(w, 9), "Giấy phép…"))  // Help › Giấy phép: { page: "about" }
            tryVerify(() => (d = probe(w)) && d.opened)
            compare(d.params.page, "about")
            d.close()
            tryVerify(() => !probe(w))
            inject(w, { FilterDialog: probeDialogC, AdjustDialog: probeDialogC })
            const blur = row(openMenu(w, 6), "Blur")
            mouseClick(blur)
            tryVerify(() => blur.subMenu.opened)
            waitForRendering(w.contentItem)
            mouseClick(row(blur.subMenu, "Gaussian Blur…"))
            tryVerify(() => (d = probe(w)) && d.opened)
            compare(d.params.filter, "gaussianBlur")
            d.close()
            tryVerify(() => !probe(w))
            keySequence("Ctrl+M")  // Image › Adjustments › Curves…
            tryVerify(() => (d = probe(w)) && d.opened)
            compare(d.params.adjust, "curves")
            d.close()
            tryVerify(() => !probe(w))
            w.start("Filter:unsharpMask")  // --state with the main param
            tryVerify(() => (d = probe(w)) && d.opened)
            compare(d.params.filter, "unsharpMask")
            d.close()
        }
        function probe(w) { return w.dialogs.find(d => d.title === "Probe") || null }  // open, or closing
        function test_space() {  // over the window in the work area's place; finished: the document and tool as before
            const w = open({ startState: "Main" })
            const sm = MenuSpec.MENUS[5].items.find(s => s.l === "Select and Mask…")
            if (!w.hasSpace("SelectMask"))
                verify(!w.can(sm))
            inject(w, { SelectMaskSpace: probeSpaceC })
            Docs.select(1)
            Tools.setTool("brush", "brush")
            keySequence("Ctrl+Alt+R")
            verify(w.space)
            compare(w.space.width, w.width)
            compare(w.space.height, w.height)
            verify(!region(w, TopBar).visible, "the frame gives way")
            Docs.select(2)
            Tools.tool = "hand"
            const n = Docs.current.history.length
            keySequence("Ctrl+Shift+N")  // the menus' keys rest meanwhile
            compare(Docs.current.history.length, n)
            ignoreWarning(/openSpace: SelectMask a space is open/)
            compare(w.openSpace("SelectMask"), null, "one at a time")
            w.space.finished(true)
            compare(w.space, null)
            compare(Docs.index, 1)
            compare(Tools.tool, "brush")
            verify(region(w, TopBar).visible)
        }
        function test_close_asks() {  // closing what is not saved asks SaveChanges once there is one (U20)
            const w = open({ startState: "Main" })
            verify(Docs.current.dirty)
            if (!w.hasDialog("SaveChanges")) {
                keySequence("Ctrl+W")
                compare(Docs.docs.length, 2, "no SaveChanges yet: closed")
                Docs.reset()
            }
            inject(w, { SaveChangesDialog: probeDialogC })
            const d0 = Docs.current
            keySequence("Ctrl+W")
            let d = null
            tryVerify(() => (d = probe(w)) && d.opened)
            compare(d.params.docs, [d0])
            compare(Docs.docs.length, 3, "asked first")
            d.params.done()
            compare(Docs.docs.length, 2)
            d.close()
            tryVerify(() => !probe(w))
            Docs.select(0)  // san-pham.webp, saved: no question
            keySequence("Ctrl+W")
            compare(Docs.docs.length, 1)
            verify(!probe(w))
        }
        function test_files() {  // dropped or picked in Mở…, each its tab, by the file's name
            const w = open({ startState: "Main" })
            w.openFiles(["file:///tmp/a%20b/ảnh mới.jpg", "file:///C:/Users/x/cau-vang.jpg"])
            compare(Docs.docs.length, 5)
            compare(Docs.docs[3].file, "ảnh mới.jpg")
            compare(Docs.current.file, "cau-vang.jpg")
            w.openFiles(["file:///x/chân dung.psd"])  // open already: its tab
            compare(Docs.docs.length, 5)
            compare(Docs.index, 0)
            verify(find(w.contentItem, o => o instanceof DropArea))
        }

        // ---- the title bar (TitleBar, Windows) -------------------------------------------------------------------------
        function test_title_bar() {
            const w = open({ startState: "Main" })
            const tb = region(w, TopBar)
            compare(tb.objectName, "titleBar")
            const close = find(tb, o => o instanceof AbstractButton && o.text === "Đóng")
            verify(!close.visible, "the system's title bar: no window buttons")
            tb.titleBar = true
            tryVerify(() => close.visible && close.mapToItem(null, close.width, 0).x === w.width, 1000, "the buttons end at the window's edge")
            const hit = o => { const p = o.mapToItem(null, o.width / 2, o.height / 2); return TitleBar.hit(tb, p.x, p.y) }
            compare(hit(find(tb, o => o.objectName === "maximize")), TitleBar.Maximize)
            compare(hit(close), TitleBar.Quick)
            compare(hit(bar(w).itemAt(0)), TitleBar.Quick, "a menu")
            const word = find(tb, o => o instanceof Text && o.text === "photo-beauty")
            compare(hit(word), TitleBar.Move, "the wordmark moves the window")
            compare(TitleBar.hit(tb, 900, 20), TitleBar.Move, "so does the bar's empty middle")
            compare(TitleBar.hit(tb, 400, 60), TitleBar.Quick, "under the bar")
            openMenu(w, 0)
            compare(hit(word), TitleBar.Quick, "with a menu open, a press closes it")
            keyClick(Qt.Key_Escape)
            tryVerify(() => hit(word) === TitleBar.Move, 1000, "closed")
        }
    }
}
