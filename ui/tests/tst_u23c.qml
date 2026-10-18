import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtTest
import PhotoBeauty

// U23 part C: the keyboard across the window. Enter that no control takes presses a dialog's main button (a field's
// Enter applies its value, a focused button or an open list keeps it); Tab goes through a dialog from the × of its title
// bar down to its buttons; Esc closes a panel floating beside the icon column wherever the focus is.
Item {
    id: root
    width: 200
    height: 100
    Component { id: mainC; Main {} }
    Component { id: hostC; Window { width: 800; height: 600; visible: true } }
    Component {  // main buttons in the content, one hidden (a page not shown)
        id: probeC
        DialogBox {
            property string hit
            title: "Probe"
            buttons: [{ text: "Hủy", act: () => close() }]
            Column {
                Btn { primary: true; visible: false; text: "Ẩn"; onClicked: hit = "hidden" }
                Btn { primary: true; text: "Hiện"; onClicked: hit = "shown" }
            }
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
    function frame(d) { return d.contentItem.parent }  // the popup's item: title bar, content, buttons

    TestCase {
        name: "U23c"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanupTestCase() { init() }

        function openMain() {
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function dialog(w, name, params) {
            const d = w.openDialog(name, params || {})
            verify(d, name)
            tryVerify(() => d.opened)
            waitForRendering(w.contentItem)
            return d
        }
        function shut(w, d) { tryVerify(() => w.dialogs.indexOf(d) < 0) }
        function button(d, text) { return root.find(root.frame(d), o => o instanceof T.AbstractButton && o.text === text && o.visible) }
        function field(d) { return root.find(d.contentItem, o => o instanceof T.TextField && o.visible && o.enabled) }

        // ---- Enter --------------------------------------------------------------------------------------------------------
        function test_enter_presses_main_button() {  // as it opens: Fill's OK, one step
            const w = openMain(), doc = Docs.current, n = doc.history.length
            const d = dialog(w, "Fill")
            keyClick(Qt.Key_Return)
            shut(w, d)
            compare(doc.history.length, n + 1)
            compare(doc.history[doc.index].name, "Fill")
            const d2 = dialog(w, "Fill")
            keyClick(Qt.Key_Enter)  // the keypad's, twice while it closes: once
            keyClick(Qt.Key_Enter)
            shut(w, d2)
            compare(doc.history.length, n + 2)
        }
        function test_enter_in_field_applies_it() {  // the field's value first; the next Enter is the dialog's
            const w = openMain(), doc = Docs.current, n = doc.history.length
            const d = dialog(w, "ImageSize"), f = field(d)
            mouseClick(f)
            f.text = "2200"
            keyClick(Qt.Key_Return)
            wait(50)
            verify(w.dialogs.indexOf(d) >= 0, "the field took Enter")
            compare(d.size[0], 2200)
            keyClick(Qt.Key_Return)
            shut(w, d)
            compare([doc.w, doc.history.length], [2200, n + 1])
        }
        function test_enter_on_focused_button() {  // Tab to Hủy, Enter: Hủy, not OK
            const w = openMain(), doc = Docs.current, n = doc.history.length
            const d = dialog(w, "Fill"), cancel = button(d, "Hủy")
            for (let i = 0; i < 20 && !cancel.activeFocus; i++)
                keyClick(Qt.Key_Tab)
            verify(cancel.activeFocus)
            keyClick(Qt.Key_Return)
            shut(w, d)
            compare(doc.history.length, n)
        }
        function test_enter_in_open_list() {  // the list's: it picks, the dialog stays
            const w = openMain(), doc = Docs.current, n = doc.history.length
            const d = dialog(w, "Fill"), dd = root.find(d.contentItem, o => o instanceof Dropdown)
            mouseClick(dd)
            tryVerify(() => dd.popup.opened)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryVerify(() => !dd.popup.visible)
            verify(w.dialogs.indexOf(d) >= 0)
            compare(d.contents, "bg")
            keyClick(Qt.Key_Return)
            shut(w, d)
            compare([doc.history.length, doc.history[doc.index].cmds[0].contents], [n + 1, "bg"])
        }
        function test_enter_on_tabbed_list() {  // Tab reached the closed list: Enter opens it, as a button
            const w = openMain()
            const d = dialog(w, "Fill"), dd = root.find(d.contentItem, o => o instanceof Dropdown)
            for (let i = 0; i < 20 && !dd.activeFocus; i++)
                keyClick(Qt.Key_Tab)
            verify(dd.visualFocus)
            keyClick(Qt.Key_Return)
            tryVerify(() => dd.popup.opened)
            verify(w.dialogs.indexOf(d) >= 0)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !dd.popup.visible)
            keyClick(Qt.Key_Escape)
            shut(w, d)
        }
        function test_enter_main_button_off() {  // OK greyed (no such filter): Enter does nothing
            const w = openMain()
            const d = dialog(w, "Filter", { filter: "nope" })
            verify(!button(d, "OK").enabled)
            keyClick(Qt.Key_Return)
            wait(300)  // past the closing transition, were it closing
            verify(d.opened)
            keyClick(Qt.Key_Escape)
            shut(w, d)
        }
        function test_enter_own_footer() {  // a dialog laying out its own buttons: SaveChanges' Lưu
            const w = openMain()
            Docs.cancelJob()
            const d0 = Docs.current
            keySequence("Ctrl+W")
            let d = null
            tryVerify(() => (d = w.dialogs.find(x => x instanceof SaveChangesDialog) || null) && d.opened)
            keyClick(Qt.Key_Return)
            shut(w, d)
            compare(Docs.docs.indexOf(d0), -1)
            compare([Docs.job.verb, Docs.job.name], ["save", "chân dung.psd"])
        }
        function test_enter_shown_button() {  // the main button shown, not a hidden one
            const host = createTemporaryObject(hostC, root), d = createTemporaryObject(probeC, host.contentItem)
            host.requestActivate()
            tryVerify(() => host.active)
            d.open()
            tryVerify(() => d.opened)
            keyClick(Qt.Key_Return)
            compare(d.hit, "shown")
        }
        function test_enter_once() {  // New's name field makes the document itself: one, not two
            const w = openMain(), n = Docs.docs.length
            const d = dialog(w, "NewDoc")
            verify(w.activeFocusItem instanceof TextInput)
            keyClick(Qt.Key_Return)
            shut(w, d)
            compare(Docs.docs.length, n + 1)
        }

        // ---- Tab ----------------------------------------------------------------------------------------------------------
        function test_tab_order_in_dialog() {  // × of the title bar, the content top down, Hủy, OK, and round
            const w = openMain()
            const d = dialog(w, "Fill"), seen = []
            for (let i = 0; i < 12; i++) {
                keyClick(Qt.Key_Tab)
                const f = w.activeFocusItem
                if (seen.indexOf(f) >= 0)
                    break
                seen.push(f)
            }
            const close = root.find(d.header, o => o instanceof IconButton && o.icon.name === "close")
            compare(seen[0], close, "first the ×")
            compare(seen.slice(-2), [button(d, "Hủy"), button(d, "OK")], "the buttons last")
            const ys = seen.slice(1, -2).map(f => f.mapToItem(null, 0, 0).y)
            compare(ys, ys.slice().sort((a, b) => a - b), "the content top down")
            keyClick(Qt.Key_Escape)
            shut(w, d)
        }

        // ---- Cỡ chữ Lớn ---------------------------------------------------------------------------------------------------
        function rowsCut(m) {  // the rows wider than the menu, its submenus' too (made with it, shown or not)
            let cut = []
            for (let k = 0; k < m.count; k++) {
                const it = m.itemAt(k)
                if (it.implicitWidth > m.availableWidth + 0.5)
                    cut.push(it.text)
                if (it instanceof T.MenuItem && it.subMenu)
                    cut = cut.concat(rowsCut(it.subMenu))
            }
            return cut
        }
        function test_menus_fit_large_font() {  // every menu and submenu of the bar as wide as its rows
            Prefs.set("fontSize", 2)
            const w = openMain(), bar = root.find(w.contentItem, o => o instanceof AppMenu)
            let cut = []
            for (let i = 0; i < bar.count; i++) {
                const m = bar.menuAt(i)
                mouseClick(bar.itemAt(i))
                tryVerify(() => m.opened)
                cut = cut.concat(rowsCut(m))
                m.close()
                tryVerify(() => !m.visible)
            }
            compare(cut, [])
        }

        function test_properties_large_font() {  // the document's page: the resolution's value whole, the buttons' labels in them
            const w = openMain()
            Docs.current.run({ op: "selectLayers", layers: [] })
            Tools.dockTab = Object.assign({}, Tools.dockTab, { p: "Properties" })
            waitForRendering(w.contentItem)
            const page = root.find(w.contentItem, o => o.objectName === "page:doc" && o.visible)
            verify(page)
            const quick = ["Select Subject", "Remove Background"].map(t => root.find(page, o => o instanceof Btn && o.text === t))
            compare(quick[0].y, quick[1].y, "Vừa: side by side, as the artboard")
            Prefs.set("fontSize", 2)
            waitForRendering(w.contentItem)
            const input = root.find(root.find(page, o => o instanceof NumberField && o.unit === "ppi"), o => o instanceof T.TextField)
            const room = input.width - input.leftPadding - input.rightPadding
            verify(input.contentWidth <= room + 0.5, "300 ppi whole: " + input.contentWidth + " in " + room)
            for (const text of ["Select Subject", "Remove Background"]) {  // in the button, the button in the page
                const b = root.find(page, o => o instanceof Btn && o.text === text)
                verify(b.implicitWidth <= b.width + 0.5 && b.mapToItem(page, 0, 0).x + b.width <= page.width + 0.5, text)
            }
        }

        function test_color_mode_large_font() {  // the Color panel's drop-down: "Color Wheel" whole
            Prefs.set("fontSize", 2)
            const w = openMain()
            Tools.colorMode = "Wheel"
            Tools.dockTab = Object.assign({}, Tools.dockTab, { c: "Color" })
            waitForRendering(w.contentItem)
            const dd = root.find(root.find(w.contentItem, o => o instanceof ColorPanel && o.visible), o => o instanceof Dropdown)
            const t = root.find(dd.contentItem, o => o instanceof Text && o.text === "Color Wheel")
            verify(t && !t.truncated, "Color Wheel whole")
        }
        function test_missing_fonts_large_font() {  // each font put in a missing one's place, whole
            Prefs.set("fontSize", 2)
            const w = openMain(), d = dialog(w, "MissingFonts"), all = []
            const walk = it => { for (const c of it.children) { if (c instanceof Select) all.push(c); walk(c) } }
            walk(d.contentItem)
            compare(all.map(s => [s.displayText, s.contentItem.truncated]), [["Be Vietnam Pro Bold", false], ["Space Grotesk Regular", false]])
            keyClick(Qt.Key_Escape)
            shut(w, d)
        }

        function test_brush_picker_large_font() {  // the presets' names whole
            Prefs.set("fontSize", 2)
            const w = openMain()
            Tools.setTool("brush")
            waitForRendering(w.contentItem)
            const bp = root.find(w.contentItem, o => o instanceof BrushPicker && o.visible), cut = []
            mouseClick(bp)
            tryVerify(() => bp.picker.opened)
            const walk = it => { for (const c of it.children) { if (c instanceof Text && c.visible && c.truncated) cut.push(c.text); walk(c) } }
            walk(bp.picker.contentItem)
            compare(cut, [])
        }
        function test_options_bars_large_font() {  // every tool's bar at Lớn in the smallest window: values whole, the bar in it
            Prefs.set("fontSize", 2)
            const w = createTemporaryObject(mainC, root, { startState: "Main", width: 1280, height: 800 })
            w.requestActivate()
            tryVerify(() => w.active)
            const ob = root.find(w.contentItem, o => o instanceof OptionsBar), bad = []
            const walk = (t, it) => {
                for (const c of it.children) {
                    if (!c.visible)
                        continue
                    if (c instanceof T.TextField && c.contentWidth > c.width - c.leftPadding - c.rightPadding + 0.5)
                        bad.push(t + ": " + c.text)
                    if (c instanceof T.Control && c.mapToItem(null, c.width, 0).x > w.width + 0.5)
                        bad.push(t + ": out " + c.Accessible.name)
                    walk(t, c)
                }
            }
            for (const t of ["move", "rect", "lasso", "wand", "selbrush", "crop", "pcrop", "eyedrop", "spot", "heal", "patch", "brush", "pencil",
                             "clone", "eraser", "gradient", "bucket", "dodge", "sponge", "type", "rectS", "poly", "line", "hand", "rot", "zoom"]) {
                Tools.setTool(t)
                waitForRendering(w.contentItem)
                walk(t, ob)
            }
            compare(bad, [])
        }
        function test_layers_low_group() {  // Gradients and Retouch over it: the foot under the head's rows, never over them
            for (const size of [[1440, 900], [1280, 800]]) {
                const w = createTemporaryObject(mainC, root, { startState: "Main", width: size[0], height: size[1] })
                w.requestActivate()
                tryVerify(() => w.active)
                Tools.dockTab = { c: "Gradients", p: "Retouch", l: "Layers" }
                waitForRendering(w.contentItem)
                const lp = root.find(w.contentItem, o => o instanceof LayersPanel && o.visible)
                const lock = root.find(lp, o => o instanceof Text && o.text === "Khóa")
                const link = root.find(lp, o => o instanceof ToolToggle && o.Accessible.name === "Liên kết layer")
                verify(lp.height < 129, "a low group: " + lp.height)
                verify(link.mapToItem(lp, 0, 0).y >= lock.mapToItem(lp, 0, 0).y + lock.height, "the foot under Khóa at " + size)
                verify(lp.clip, "what is below cut at the panel's bottom")
                w.close()
            }
        }
        function test_layers_menus_large_font() {  // the panel's menu as wide as its rows (keys too); a foot menu 220 at the right
            Prefs.set("fontSize", 2)
            Tools.dockMin = Object.assign({}, Tools.dockMin, { p: true })
            const w = openMain()
            const tabs = root.find(w.contentItem, o => o instanceof PanelTabs && o.visible && o.menu.some(it => it.l === "Create Clipping Mask"))
            const pop = tabs.data.find(o => o instanceof PopupMenu)
            mouseClick(root.find(tabs, o => o instanceof ToolToggle && o.icon.name === "more_horiz"))
            tryVerify(() => pop.opened)
            compare(rowsCut(pop), [])
            pop.close()
            tryVerify(() => !pop.visible)
            Prefs.set("fontSize", 1)
            const lp = root.find(w.contentItem, o => o instanceof LayersPanel && o.visible), foot = lp.data.find(o => o instanceof PopupMenu)
            mouseClick(root.find(lp, o => o instanceof ToolToggle && o.Accessible.name === "Thêm fill hay adjustment layer"))
            tryVerify(() => foot.opened)
            compare([foot.width, foot.x + foot.width], [220, lp.width - 8])
        }

        // ---- Tab in a layer row -------------------------------------------------------------------------------------------
        function test_tab_in_layer_row() {  // as the prototype's row: the eye, the twist, the layer, its mask; Enter on the mask
            Tools.dockMin = Object.assign({}, Tools.dockMin, { p: true })  // room for the rows
            const w = openMain(), doc = Docs.current
            const rowOf = text => root.find(w.contentItem, o => o instanceof LayerRow && o.visible && o.text === text && !o.fx)
            const named = (r, name) => root.find(r, o => o instanceof T.AbstractButton && o.Accessible.name === name)
            const tab = back => {
                keyClick(Qt.Key_Tab, back ? Qt.ShiftModifier : Qt.NoModifier)
                return w.activeFocusItem ? w.activeFocusItem.Accessible.name : ""
            }
            named(rowOf("Tách tần số"), "Ẩn hiện Tách tần số").forceActiveFocus(Qt.TabFocusReason)
            compare([tab(), tab()], ["Mở hay thu nhóm Tách tần số", "Chọn layer Tách tần số"])
            named(rowOf("Curves 1"), "Ẩn hiện Curves 1").forceActiveFocus(Qt.TabFocusReason)
            compare([tab(), tab()], ["Chọn layer Curves 1", "Chọn Layer Mask của Curves 1"])
            keyClick(Qt.Key_Return)
            compare([doc.selected, doc.editMask], [[doc.layers.find(l => l.props.name === "Curves 1").id], true])
            compare(tab(true), "Chọn layer Curves 1")
            keyClick(Qt.Key_Return)  // the layer again: the brush back on it
            compare(doc.editMask, false)
        }

        // ---- Esc ----------------------------------------------------------------------------------------------------------
        function test_esc_closes_what_floats() {  // opened with the mouse: the focus is on the column's icon
            const w = openMain(), col = root.find(w.contentItem, o => o instanceof SideColumn)
            const icon = tag => root.find(col, o => o.objectName === "icon:" + tag)
            mouseClick(icon("History"))
            compare(Tools.sidePanel, "History")
            keyClick(Qt.Key_Escape)
            compare(Tools.sidePanel, "")
            Tools.dockCollapsed = true
            waitForRendering(w.contentItem)
            mouseClick(icon("l"))
            compare(Tools.dockOpen, "l")
            keyClick(Qt.Key_Escape)
            compare(Tools.dockOpen, "")
            keySequence("F5")  // the focus stays where it was
            compare(Tools.sidePanel, "Brush Settings")
            keyClick(Qt.Key_Escape)
            compare(Tools.sidePanel, "")
        }
        function test_esc_in_field_while_floating() {  // a field being typed in keeps its Esc: one layer a press
            const w = openMain(), sb = root.find(w.contentItem, o => o instanceof StatusBar)
            const zoom = root.find(sb, o => o instanceof T.TextField)
            Tools.sidePanel = "History"
            waitForRendering(w.contentItem)
            zoom.forceActiveFocus()
            zoom.text = "50"
            keyClick(Qt.Key_Escape)
            compare([zoom.text, Tools.sidePanel], ["12,5%", "History"])
            verify(!zoom.activeFocus)
            keyClick(Qt.Key_Escape)
            compare(Tools.sidePanel, "")
        }
    }
}
