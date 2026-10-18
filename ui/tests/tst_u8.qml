import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty

// U8: the dock (Dock), the icon column (SideColumn) and what floats beside it (SidePanel, a folded dock's group), the
// Channels panel; the Window menu, F5–F8 and Ctrl 2–9 on them.
Item {
    id: root
    width: 200
    height: 100
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
        name: "U8"
        when: windowShown

        readonly property var types: ({ Color: ColorPanel, Swatches: SwatchesPanel, Gradients: GradientsPanel, Properties: PropertiesPanel, Adjustments: AdjustmentsPanel,
            Retouch: RetouchPanel, Layers: LayersPanel, Channels: ChannelsPanel, History: HistoryPanel, Brushes: BrushesPanel, "Brush Settings": BrushSettingsPanel,
            Navigator: NavigatorPanel, Histogram: HistogramPanel, Info: InfoPanel, Character: CharacterPanel, Paragraph: ParagraphPanel })
        readonly property var dockTabs: ({ c: ["Color", "Swatches", "Gradients"], p: ["Properties", "Adjustments", "Retouch"], l: ["Layers", "Channels"] })
        readonly property var sides: ["History", "Brushes", "Brush Settings", "Navigator", "Histogram", "Info", "Character", "Paragraph"]

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function cleanupTestCase() { init() }

        function open(props) {
            const w = createTemporaryObject(mainC, root, props || { startState: "Main" })
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
        // The panels of that name made, and the one shown.
        function made(w, name) { return all(w.contentItem, o => o instanceof types[name]) }
        function shown(w, name) { return find(w.contentItem, o => o instanceof types[name] && o.visible) }
        function only(w, name) {  // shown, the one of its kind
            const m = made(w, name)
            return m.length === 1 && m[0].visible ? m[0] : null
        }
        // The tab strip showing a tab of that name, and its tab.
        function strip(w, name) { return find(w.contentItem, o => o instanceof PanelTabs && o.visible && o.tabs.indexOf(name) >= 0) }
        function tab(w, name) { return find(strip(w, name), o => o instanceof AbstractButton && o.text === name) }
        function icon(w, tag) { return find(region(w, SideColumn), o => o.objectName === "icon:" + tag) }
        function click(w, item) {
            verify(item, "nothing to click")
            mouseClick(item)
            waitForRendering(w.contentItem)
        }
        function bar(w) { return region(w, AppMenu) }
        function windowMenu(w) {
            const b = bar(w), m = b.menuAt(8)
            if (!m.visible)
                mouseClick(b.itemAt(8))
            tryVerify(() => m.opened)
            waitForRendering(w.contentItem)
            return m
        }
        function row(m, label) {
            for (let i = 0; i < m.count; i++)
                if (m.itemAt(i) instanceof MenuItem && m.itemAt(i).text === label)
                    return m.itemAt(i)
            return null
        }
        function fromMenu(w, name) {
            const m = windowMenu(w)
            mouseClick(row(m, name))
            tryVerify(() => !m.visible)
            waitForRendering(w.contentItem)
        }
        function ticked(w, name) {
            const m = windowMenu(w), t = row(m, name).tick
            keyClick(Qt.Key_Escape)
            tryVerify(() => !m.visible)
            return t
        }
        function probe(w) {  // counts the presses the canvas gets
            return Qt.createQmlObject("import QtQuick; MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; property int n: 0; onPressed: n++ }", region(w, Viewport))
        }

        // ---- the dock --------------------------------------------------------------------------------------------------
        // Color as tall as it is, Properties too up to 360, Layers the rest; 307 wide inside the dock's rule.
        function test_dock() {
            const w = open()
            compare(box(w, region(w, Dock)), [1132, 80, 308, 796])
            const c = strip(w, "Color").parent, p = strip(w, "Properties").parent, l = strip(w, "Layers").parent
            compare(box(w, c), [1133, 80, 307, 154])
            compare(box(w, p), [1133, 235, 307, 192])
            compare(box(w, l), [1133, 428, 307, 448])
            compare(box(w, only(w, "Color")), [1133, 110, 307, 124])
            compare(box(w, only(w, "Properties")), [1133, 265, 307, 162])
            compare(box(w, only(w, "Layers")), [1133, 458, 307, 418])
            click(w, tab(w, "Retouch"))  // 489 tall: 360 of it, scrolled
            compare(box(w, p), [1133, 235, 307, 390])
            const f = find(p, o => o instanceof Flickable)
            compare([f.height, f.contentHeight], [360, 489])
            compare(box(w, l), [1133, 626, 307, 250])
            w.height = 800
            waitForRendering(w.contentItem)
            compare(box(w, l), [1133, 626, 307, 150], "Layers gives up what the window loses")
        }
        // Every dock panel from its tab, one of a kind; Tools keeps the tab; the panel's menu is its group's.
        function test_tabs() {
            const w = open()
            for (const key in dockTabs) {
                for (const name of dockTabs[key].slice().reverse()) {
                    click(w, tab(w, name))
                    compare(Tools.dockTab[key], name)
                    verify(only(w, name), name)
                    verify(Tools.panelVisible(name), name)
                    for (const other of dockTabs[key])
                        if (other !== name)
                            compare(made(w, other).length, 0, other + " is not kept")
                    compare(strip(w, name).menu, only(w, name).menu)
                }
            }
            click(w, tab(w, "Channels"))
            verify(strip(w, "Channels").menu.length > 0, "Channels has a menu")
        }
        // A double click on a tab folds its group to the tabs; Layers takes what it gives up; a click opens it again.
        function test_fold() {
            const w = open()
            const c = strip(w, "Color").parent, l = strip(w, "Layers").parent
            mouseDoubleClickSequence(tab(w, "Color"))
            compare(Tools.dockMin.c, true)
            waitForRendering(w.contentItem)
            verify(strip(w, "Color").collapsed)
            compare(box(w, c), [1133, 80, 307, 30])
            compare(box(w, l), [1133, 304, 307, 572])
            compare(only(w, "Layers").height, 542)
            verify(!shown(w, "Color"))
            verify(!Tools.panelVisible("Color"))
            mouseDoubleClickSequence(tab(w, "Layers"))
            compare(Tools.dockMin.l, true)
            waitForRendering(w.contentItem)
            compare(box(w, l), [1133, 304, 307, 30], "folded, Layers leaves the dock's bottom empty")
            click(w, tab(w, "Channels"))  // a click on a folded group's tab opens it on that tab
            compare([Tools.dockMin.l, Tools.dockTab.l], [false, "Channels"])
            verify(only(w, "Channels"))
            mouseDoubleClickSequence(tab(w, "Swatches"))
            waitForRendering(w.contentItem)
            compare([Tools.dockMin.c, Tools.dockTab.c], [true, "Swatches"], "a double click on a folded group: open, then folded again")
        }
        // » folds the dock to the icon column; each group floats beside it from its icon; « unfolds the dock with the
        // tabs it had.
        function test_collapse() {
            const w = open()
            click(w, tab(w, "Swatches"))
            click(w, tab(w, "Adjustments"))
            click(w, tab(w, "Channels"))
            const fold = find(strip(w, "Swatches"), o => o instanceof ToolToggle && o.icon.name === "keyboard_double_arrow_right")
            click(w, fold)
            verify(Tools.dockCollapsed)
            compare(region(w, Dock).width, 0)
            compare(box(w, region(w, SideColumn)), [1404, 80, 36, 796])
            compare(box(w, region(w, Viewport)), [44, 108, 1360, 768])
            for (const k of ["c", "p", "l"])
                verify(icon(w, k).visible, k)
            verify(!strip(w, "Swatches"), "no group shows")
            compare(made(w, "Swatches").length, 0)
            click(w, icon(w, "c"))
            compare(Tools.dockOpen, "c")
            verify(icon(w, "c").on)
            const g = strip(w, "Swatches").parent
            compare(box(w, g), [1095, 86, 308, 30 + only(w, "Swatches").implicitHeight])  // the tabs, the panel (U11's)
            verify(only(w, "Swatches"))
            verify(Tools.panelVisible("Swatches") && !Tools.panelVisible("Color"))
            verify(!find(strip(w, "Swatches"), o => o instanceof ToolToggle && o.icon.name === "keyboard_double_arrow_right").visible, "no » floating")
            click(w, tab(w, "Gradients"))  // its tabs work there
            compare([Tools.dockTab.c, Tools.dockOpen], ["Gradients", "c"])
            verify(only(w, "Gradients"))
            click(w, icon(w, "p"))
            compare(Tools.dockOpen, "p")
            verify(only(w, "Adjustments"))
            compare(made(w, "Gradients").length, 0)
            click(w, icon(w, "p"))  // again: closed
            compare(Tools.dockOpen, "")
            verify(!shown(w, "Adjustments"))
            click(w, icon(w, "l"))
            compare(box(w, strip(w, "Channels").parent), [1095, 86, 308, 780])
            compare(only(w, "Channels").height, 750)
            click(w, find(region(w, SideColumn), o => o instanceof ToolToggle && o.icon.name === "keyboard_double_arrow_left"))
            verify(!Tools.dockCollapsed)
            compare(Tools.dockOpen, "")
            compare(box(w, region(w, Dock)), [1132, 80, 308, 796])
            compare([Tools.dockTab.c, Tools.dockTab.p, Tools.dockTab.l], ["Gradients", "Adjustments", "Channels"])
            for (const name of ["Gradients", "Adjustments", "Channels"])
                verify(only(w, name), name)
            verify(!icon(w, "c").visible)
        }
        // Every panel from the Window menu, ticked while shown; the dock folded, its groups float from there too; a
        // side panel picked again closes.
        function test_window_menu() {
            const w = open()
            for (const name in types) {
                fromMenu(w, name)
                verify(only(w, name), name)
                verify(Tools.panelVisible(name), name)
                verify(ticked(w, name), name)
            }
            fromMenu(w, "Paragraph")  // shown: closed
            compare(Tools.sidePanel, "")
            verify(!shown(w, "Paragraph"))
            Tools.dockCollapsed = true
            for (const name of ["Layers", "Properties", "Color", "Channels"]) {
                fromMenu(w, name)
                compare(Tools.dockOpen, Tools.dockOf[name])
                verify(only(w, name), name)
                verify(ticked(w, name), name)
            }
            verify(!ticked(w, "Layers"), "Channels floats, Layers does not")
        }
        // Every side panel from its icon, beside the column; from the tabs of its group; again or × closes it.
        function test_column() {
            const w = open()
            const sp = region(w, SidePanel)
            verify(!sp.visible)
            for (const name of sides) {
                click(w, icon(w, name))
                compare(Tools.sidePanel, name)
                verify(icon(w, name).on)
                verify(only(w, name), name)
                compare(box(w, sp), [812, 86, 280, Math.min(30 + only(w, name).implicitHeight, 784)])
                click(w, icon(w, name))
                compare(Tools.sidePanel, "")
                verify(!sp.visible)
            }
            compare(icon(w, "Brush Settings").ToolTip.text, "Brush Settings (F5)")
            click(w, icon(w, "Navigator"))
            compare(strip(w, "Navigator").tabs, ["Navigator", "Histogram", "Info"])
            click(w, tab(w, "Info"))
            compare(Tools.sidePanel, "Info")
            verify(only(w, "Info"))
            compare(made(w, "Navigator").length, 0)
            verify(icon(w, "Info").on && !icon(w, "Navigator").on)
            click(w, find(sp, o => o instanceof ToolToggle && o.icon.name === "close"))
            compare(Tools.sidePanel, "")
            click(w, icon(w, "Brush Settings"))  // 856 tall (its .bsh rows 28, U23): as tall as the column lets it, scrolled
            compare(box(w, sp), [812, 86, 280, 784])
            const f = find(sp, o => o instanceof Flickable)
            compare([f.height, f.contentHeight], [754, 856])
        }
        // A press on the canvas closes what floats and goes no further; one on it, or off the canvas, does not.
        function test_scrim() {
            const w = open()
            const vp = region(w, Viewport), n = probe(w)
            click(w, icon(w, "History"))
            mouseClick(region(w, SidePanel), 140, 300)  // on it
            mouseClick(region(w, Toolbar), 20, 400)  // off the canvas, as the prototype
            compare(Tools.sidePanel, "History")
            mouseClick(vp, 300, 300)
            compare(Tools.sidePanel, "")
            compare(n.n, 0)
            mouseClick(vp, 300, 300)
            compare(n.n, 1)
            mouseClick(vp, 300, 300, Qt.RightButton)
            compare(n.n, 2)
            Tools.dockCollapsed = true
            waitForRendering(w.contentItem)
            click(w, icon(w, "l"))
            mouseClick(only(w, "Layers"), 150, 400)  // on the floating group
            compare(Tools.dockOpen, "l")
            mouseClick(vp, 300, 300, Qt.RightButton)
            compare(Tools.dockOpen, "")
            compare(n.n, 2)
        }
        // One floats at a time.
        function test_one_floats() {
            const w = open()
            Tools.dockCollapsed = true
            waitForRendering(w.contentItem)
            click(w, icon(w, "History"))
            click(w, icon(w, "c"))
            compare([Tools.sidePanel, Tools.dockOpen], ["", "c"])
            verify(!region(w, SidePanel).visible)
            keySequence("F5")
            compare([Tools.sidePanel, Tools.dockOpen], ["Brush Settings", ""])
            keySequence("F7")
            compare([Tools.sidePanel, Tools.dockOpen], ["", "l"])
        }
        // F5–F8 as the Window menu; Esc closes what floats and gives its icon the focus; opened from the keyboard, the
        // focus goes into it.
        function test_keys() {
            const w = open()
            keySequence("F5")
            compare(Tools.sidePanel, "Brush Settings")
            verify(only(w, "Brush Settings"))
            keySequence("F5")
            compare(Tools.sidePanel, "")
            keySequence("F8")
            verify(only(w, "Info"))
            click(w, tab(w, "Swatches"))
            click(w, tab(w, "Channels"))
            keySequence("F6")
            compare(Tools.dockTab.c, "Color")
            verify(only(w, "Color"))
            keySequence("F7")
            compare(Tools.dockTab.l, "Layers")
            verify(only(w, "Layers"))
            compare(Tools.sidePanel, "Info", "the dock's panels leave the side panel open")
            const sp = region(w, SidePanel)
            find(sp, o => o instanceof AbstractButton && o.text === "Info").forceActiveFocus()
            keyClick(Qt.Key_Escape)
            compare(Tools.sidePanel, "")
            verify(icon(w, "Info").activeFocus, "the icon has the focus")
            keyClick(Qt.Key_Return)  // that icon, from the keyboard
            compare(Tools.sidePanel, "Info")
            tryVerify(() => sp.activeFocus && w.activeFocusItem !== sp, 1000, "the focus goes into the panel")
            keyClick(Qt.Key_Tab)
            verify(!w.panelsHidden, "Tab goes on from a control")
            keyClick(Qt.Key_Escape)
            compare(Tools.sidePanel, "")
            Tools.dockCollapsed = true
            waitForRendering(w.contentItem)
            keySequence("F7")
            compare(Tools.dockOpen, "l")
            const g = strip(w, "Layers").parent
            find(g, o => o instanceof AbstractButton && o.text === "Layers").forceActiveFocus()
            keyClick(Qt.Key_Escape)
            compare(Tools.dockOpen, "")
            verify(icon(w, "l").activeFocus)
            const spy = Qt.createQmlObject("import QtQuick; Shortcut { sequence: 'Esc'; property int n: 0; onActivated: n++ }", w.contentItem)
            keyClick(Qt.Key_Return)
            tryVerify(() => g.activeFocus && w.activeFocusItem !== g, 1000)
            keyClick(Qt.Key_Escape)
            compare([Tools.dockOpen, spy.n], ["", 0], "Esc in the floating group is its own, not a shortcut's")
            keyClick(Qt.Key_Escape)
            compare(spy.n, 1, "with nothing floating, it is the shortcut's")
            spy.destroy()
        }

        // ---- Channels --------------------------------------------------------------------------------------------------
        function channels(w) {
            click(w, tab(w, "Channels"))
            return only(w, "Channels")
        }
        function chRows(p) { return all(p, o => o instanceof Rectangle && o.modelData !== undefined).sort((a, b) => a.index - b.index) }
        function names(p) { return chRows(p).map(r => r.modelData) }
        function chRow(p, name) { return chRows(p).find(r => r.modelData === name) }
        function eye(r) { return find(r, o => o instanceof AbstractButton && o.width === 30) }
        function pick(r) { return find(r, o => o instanceof AbstractButton && o.width > 30) }
        function footButton(p, icon) { return find(p, o => o instanceof ToolToggle && o.icon.name === icon) }
        function lit(p) { return chRows(p).filter(r => r.on).map(r => r.modelData) }

        // The composite and colour channels, the alpha channels, their keys; a click shows one alone, Ctrl 2–6 too.
        function test_channels() {
            const w = open(), p = channels(w), d = Docs.current
            compare(names(p), ["RGB", "Red", "Green", "Blue", "Vùng chọn da"])
            compare(chRows(p).map(r => find(r, o => o instanceof Text && o.text.indexOf("Ctrl") === 0).text), ["Ctrl 2", "Ctrl 3", "Ctrl 4", "Ctrl 5", "Ctrl 6"])
            compare(lit(p), ["RGB", "Red", "Green", "Blue"])
            compare(box(w, chRow(p, "Red")), [1133, 498, 307, 40])
            const n = d.history.length
            click(w, pick(chRow(p, "Red")))
            compare(d.channel, "Red")
            compare(lit(p), ["Red"])
            click(w, pick(chRow(p, "Vùng chọn da")))
            compare(lit(p), ["Vùng chọn da"])
            click(w, pick(chRow(p, "RGB")))
            compare(lit(p), ["RGB", "Red", "Green", "Blue"])
            for (const [k, ch] of [["Ctrl+4", "Green"], ["Ctrl+6", "Vùng chọn da"], ["Ctrl+3", "Red"], ["Ctrl+7", "Red"], ["Ctrl+5", "Blue"], ["Ctrl+2", "RGB"]]) {
                keySequence(k)
                compare(d.channel, ch, k)
            }
            Tools.dockTab = Object.assign({}, Tools.dockTab, { l: "Layers" })
            keySequence("Ctrl+5")
            compare(d.channel, "Blue", "the keys with the panel not shown")
            compare(d.history.length, n, "showing a channel is no step")
        }
        // The eye hides and shows a channel, no step; rows stay.
        function test_channel_eye() {
            const w = open(), p = channels(w), d = Docs.current, n = d.history.length
            const r = chRow(p, "Blue")
            compare(find(eye(r), o => o instanceof Icon).name, "visibility")
            click(w, eye(r))
            verify(d.hiddenChannels.Blue)
            compare(find(eye(r), o => o instanceof Icon).name, "visibility_off")
            verify(chRow(p, "Blue") === r, "the row is kept")
            click(w, eye(r))
            verify(!d.hiddenChannels.Blue)
            compare(d.history.length, n)
        }
        // The portrait opens with its alpha channel hidden, as PanelsA and Photoshop; the colour channels shown (U23).
        function test_alpha_hidden() {
            const w = open(), p = channels(w), d = Docs.current
            compare(d.hiddenChannels, { "Vùng chọn da": true })
            compare(["RGB", "Red", "Vùng chọn da"].map(n => find(eye(chRow(p, n)), o => o instanceof Icon).name), ["visibility", "visibility", "visibility_off"])
        }
        // New, delete: an alpha channel each, one step each; undo gives it back; delete only on an alpha channel.
        function test_channel_add_delete() {
            const w = open(), p = channels(w), d = Docs.current, n = d.history.length
            const del = footButton(p, "delete")
            verify(!del.enabled, "the composite is not deleted")
            click(w, footButton(p, "add_box"))
            compare(d.alphas, ["Vùng chọn da", "Alpha 2"])
            compare([d.history.length, d.history[n].name], [n + 1, "Kênh mới"])
            compare(names(p), ["RGB", "Red", "Green", "Blue", "Vùng chọn da", "Alpha 2"])
            compare(find(chRows(p)[5], o => o instanceof Text && o.text.indexOf("Ctrl") === 0).text, "Ctrl 7")
            keySequence("Ctrl+7")
            compare(d.channel, "Alpha 2")
            verify(del.enabled)
            click(w, del)
            compare(d.alphas, ["Vùng chọn da"])
            compare([d.history.length, d.history[n + 1].name, d.channel], [n + 2, "Xóa kênh", "RGB"])
            verify(!del.enabled)
            d.undo()
            compare(d.alphas, ["Vùng chọn da", "Alpha 2"])
            compare(names(p).length, 6)
        }
        // The selection saved as a channel (only with one), the channel loaded as the selection: a step each; the
        // panel's menu does the same.
        function test_channel_selection() {
            const w = open(), p = channels(w), d = Docs.current, n = d.history.length
            const save = footButton(p, "mask")
            verify(!save.enabled)
            click(w, footButton(p, "load_sel"))
            compare([d.selection, d.history[n].name], ["skin", "Nạp kênh làm vùng chọn"])
            verify(save.enabled)
            click(w, save)
            compare(d.alphas, ["Vùng chọn da", "Alpha 2"])
            compare([d.history.length, d.history[n + 1].name], [n + 2, "Lưu vùng chọn"])
            const s = strip(w, "Channels"), items = s.menu.filter(i => i.t !== "sep")
            compare(items.map(i => i.l), ["Kênh mới", "Xóa kênh", "Lưu vùng chọn thành kênh", "Nạp kênh làm vùng chọn"])
            click(w, find(s, o => o instanceof ToolToggle && o.icon.name === "more_horiz"))
            let menu = null
            for (const o of s.resources)
                if (o instanceof PopupMenu)
                    menu = o
            tryVerify(() => menu.opened)
            waitForRendering(w.contentItem)
            verify(!menu.itemAt(1).enabled, "Xóa kênh greyed on the composite")
            mouseClick(menu.itemAt(0))
            compare(d.alphas, ["Vùng chọn da", "Alpha 2", "Alpha 3"])
            compare(d.history[n + 2].name, "Kênh mới")
        }
        // A grey document: Gray, then its alpha channels; Ctrl 3–5 do nothing.
        function test_channels_gray() {
            const w = open(), p = channels(w), d = Docs.current
            verify(d.run({ op: "setMode", mode: "Gray" }))
            compare(names(p), ["Gray", "Vùng chọn da"])
            compare(lit(p), ["Gray"])
            keySequence("Ctrl+4")
            compare(d.channel, "RGB")
            keySequence("Ctrl+6")
            compare(lit(p), ["Vùng chọn da"])
            click(w, pick(chRow(p, "Gray")))
            compare(d.channel, "RGB")
        }
        // Thumbnails: the photo; Red, Green, Blue in grey, or in their colour with channelsInColor; the mask of an
        // alpha channel, white in black.
        function test_channel_thumbs() {
            const w = open(), p = channels(w)
            const thumb = name => find(chRow(p, name), o => o instanceof Canvas)
            const at = (name, x, y) => {  // grabImage() of an item crops the window at the item's x, y in its parent
                const q = thumb(name).mapToItem(w.contentItem, x, y)
                return grabImage(w.contentItem).pixel(q.x, q.y)
            }
            const grey = c => Math.abs(c.r - c.g) < 0.02 && Math.abs(c.g - c.b) < 0.02
            tryVerify(() => { const c = at("Red", 13, 16); return grey(c) && c.r > 0.05 }, 3000, "Red in grey")
            const rgb = at("RGB", 13, 16), red = at("Red", 13, 16), blue = at("Blue", 13, 16)
            verify(!grey(rgb), "the colours")
            verify(Math.abs(red.r - rgb.r) < 0.02 && Math.abs(blue.r - rgb.b) < 0.02, "each its channel")
            tryVerify(() => { const c = at("Vùng chọn da", 13, 13); return c.r > 0.95 && c.g > 0.95 })
            const corner = at("Vùng chọn da", 2, 30)
            verify(corner.r < 0.05 && corner.g < 0.05)
            Prefs.set("channelsInColor", true)
            tryVerify(() => { const c = at("Red", 13, 16); return c.g < 0.01 && c.b < 0.01 && c.r > 0.05 }, 3000, "Red in red")
            verify(grey(at("RGB", 13, 16)) === grey(rgb))
        }
        // The smallest window, Gradients over Retouch: the groups above give way, the Layers tabs stay whole in the dock
        // (not under the status bar), Retouch scrolls in its group (U23).
        function test_low_dock() {
            const w = open({ startState: "Main", width: 1280, height: 800 })
            Tools.dockTab = { c: "Gradients", p: "Retouch", l: "Layers" }
            waitForRendering(w.contentItem)
            const d = region(w, Dock), s = strip(w, "Layers"), rt = shown(w, "Retouch")
            const bottom = s.mapToItem(d, 0, s.height).y
            verify(bottom <= d.height, "the Layers tabs end at " + bottom + ", the dock at " + d.height)
            const top = strip(w, "Retouch").mapToItem(d, 0, 0).y
            verify(rt.mapToItem(d, 0, 0).y >= top && rt.height > s.mapToItem(d, 0, 0).y - top - 30, "Retouch taller than its room")
            let f = rt.parent
            while (f && !(f instanceof Flickable))
                f = f.parent
            verify(f.clip && f.mapToItem(d, 0, f.height).y <= s.mapToItem(d, 0, 0).y, "scrolled above the Layers tabs")
        }
    }
}
