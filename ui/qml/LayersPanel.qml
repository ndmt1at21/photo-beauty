pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "Mock.js" as Mock
import "MenuSpec.js" as MenuSpec

// The Layers panel (docs/ui-tasks.md, U9; PanelsA, the LayerRow card of Controls, the lay and rows parts of
// Main.dc.html), the first tab of the dock's group Layers, Channels. At the head: the filter by name and kind, Blend,
// Opacity, Fill (greyed without a layer or on the Background) and the four locks, for the layers picked. The rows
// (LayerRow), a ListView that makes only those in view: a click picks a layer (Ctrl adds, Shift a run), a drag moves the
// picked ones (held at the list's edge, it scrolls), into a group too (Alt: copies), a double click on the name renames
// it (Enter, Esc; Layer › Đổi tên layer… as well), elsewhere opens Layer Style; the right button, the Layer menu's items.
// At the foot: Link, fx (Layer Style), a mask, fill and adjustment layers, a group, a layer, delete; in the panel's menu,
// seven Layer commands and the thumbnail sizes. Every change is a Doc.run() command, a History step;
// a drag on Opacity or Fill is one. What lasts while the panel is made again (it is, on every tab change) is Tools'.
Item {
    id: panel
    readonly property Doc doc: Docs.current
    readonly property var active: doc ? doc.active : null
    readonly property bool off: !active || !!active.props.background  // Blend, Opacity, Fill greyed
    readonly property Main main: Window.window as Main
    // Kept in Tools: the name filter, the kinds shown { pixel: true… }, the layers whose effects are not listed
    // { id: true }, the thumbnail size (0–2: Nhỏ, Vừa, Lớn).
    // ponytail: fxShut by layer id, for every open document; by document once DocModel keeps view state.
    readonly property string query: Tools.value("layers:q", "")
    readonly property var kinds: Tools.value("layers:kinds", {})
    readonly property var fxShut: Tools.value("layers:fxShut", {})
    readonly property int thumbSize: [16, 24, 40][Tools.value("layers:thumb", 1)] || 24
    readonly property int rowHeight: thumbSize + 8
    property var shown: []  // the rows: { l: a Doc layers entry, fx: "" the layer, "effects" or an effect's key }
    property Doc shownOf: null  // the document they are of
    property int renaming: 0  // the layer whose name is being typed
    property var drop: null  // where a drag would put the layers: { id, mode "above" | "below" | "into", at (row) }
    property real dragY  // the pointer's y in the list then
    property Item held: null  // the row dragged, kept made while it is out of sight
    // The panel's menu: the Layer commands of the Shortcuts artboard, as the Layer menu's items (keys, greyed where Main
    // says they cannot run), with English names for Phím tắt (U20); then the thumbnail sizes.
    property var menu: [[qsTr("Layer mới…"), "New Layer", "@newLayer"], [qsTr("Nhân bản layer…"), "Duplicate Layer", "@dupLayer"],
        [qsTr("Xóa layer"), "Delete Layer", "@delLayer"], [qsTr("Gộp xuống"), "Merge Down", "@merge:down"],
        [qsTr("Gộp layer đang hiện"), "Merge Visible", "@merge:visible"], [qsTr("Làm phẳng ảnh"), "Flatten Image", "@merge:flat"],
        [qsTr("Create Clipping Mask"), "", "@clip"]].map(t => {
            const it = panel.layerItem(t[2]), w = panel.main
            return { l: t[0], en: t[1], k: it.k, disabled: !w || !w.can(it), act: () => w && w.act(it) }
        }).concat([MenuSpec.SEP], [[qsTr("Thumbnail nhỏ"), "Small Thumbnails"], [qsTr("Thumbnail vừa"), "Medium Thumbnails"], [qsTr("Thumbnail lớn"), "Large Thumbnails"]]
        .map((t, i) => ({ l: t[0], en: t[1], check: Tools.value("layers:thumb", 1) === i, act: () => Tools.setValue("layers:thumb", i) })))

    // The prototype's KINDS, LOCKS; the effects of props.fx (U18) in its order; colour labels, as Photoshop's (data the
    // document keeps in props.tag, not theme colours).
    readonly property var kindList: [["pixel", qsTr("Lọc: layer điểm ảnh · Filter for pixel layers"), "image"],
        ["adj", qsTr("Lọc: adjustment layer · Filter for adjustment layers"), "contrast"], ["text", qsTr("Lọc: layer chữ · Filter for type layers"), "title"],
        ["shape", qsTr("Lọc: hình · Filter for shape layers"), "rectangle"], ["so", qsTr("Lọc: smart object · Filter for smart objects"), "deployed_code"]]
    readonly property var lockList: [["lockTransparency", qsTr("Khóa điểm ảnh trong suốt · Lock transparent pixels"), "lock_trans"],
        ["lockPixels", qsTr("Khóa điểm ảnh · Lock image pixels"), "brush"], ["lockPosition", qsTr("Khóa vị trí · Lock position"), "open_with"],
        ["lockAll", qsTr("Khóa tất cả · Lock all"), "lock"]]
    readonly property var effects: [["dropShadow", "Drop Shadow"], ["stroke", "Stroke"], ["colorOverlay", "Color Overlay"], ["gradientOverlay", "Gradient Overlay"]]
    readonly property var labels: [[qsTr("Không màu"), null], [qsTr("Đỏ"), "#e8664a"], [qsTr("Cam"), "#e5a33a"], [qsTr("Vàng"), "#dcc244"],
        [qsTr("Xanh lá"), "#5fae62"], [qsTr("Xanh dương"), "#4c8df6"], [qsTr("Tím"), "#9c7ce0"], [qsTr("Xám"), "#8e8e8e"]]

    implicitWidth: 308
    implicitHeight: 320
    clip: true  // too low a group: the rows, then the foot, cut at the bottom as the prototype's (never over the head)

    // ---- rows -----------------------------------------------------------------------------------------------------
    Connections {
        target: panel.doc
        function onRowsChanged() { panel.refresh() }
    }
    onDocChanged: refresh()
    onQueryChanged: refresh()
    onKindsChanged: refresh()
    onFxShutChanged: refresh()
    Component.onCompleted: refresh()
    onActiveChanged: Qt.callLater(reveal)

    // Doc's rows that pass the filter, each with its effects' rows under it unless shut. The list is a count, so a
    // change keeping it updates the rows in place; a new count starts the view at the top, so it goes back where it was.
    function refresh() {
        const y = list.contentY, n = shown.length, same = shownOf === doc
        const q = fold(query), want = Object.keys(kinds).filter(k => kinds[k]), out = []
        for (const l of doc ? doc.rows : []) {
            if (q && fold(l.props.name).indexOf(q) < 0 || want.length && want.indexOf(l.kind === "fill" ? "adj" : l.kind) < 0)
                continue
            out.push({ l: l, fx: "" })
            const fx = effectsOf(l)
            if (fx.length && !fxShut[l.id])
                for (const k of ["effects"].concat(fx))
                    out.push({ l: l, fx: k })
        }
        shown = out
        shownOf = doc
        if (same && out.length !== n)
            list.contentY = Math.max(0, Math.min(y, out.length * rowHeight + 40 - list.height))
    }
    function fold(t) { return String(t).toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/đ/g, "d") }  // the prototype's
    function effectsOf(l) { return l.props.fx ? effects.map(e => e[0]).filter(k => !!l.props.fx[k]) : [] }
    function effectName(k) { return k === "effects" ? qsTr("Hiệu ứng") : (effects.find(e => e[0] === k) || ["", ""])[1] }
    function rowOf(id) { return shown.findIndex(r => r.l.id === id && !r.fx) }
    function reveal() {  // the active layer's row in view
        const i = active ? rowOf(active.id) : -1
        if (i >= 0)
            list.positionViewAtIndex(i, ListView.Contain)
    }
    function thumbUrl(l) { return doc ? doc.thumbUrl(l) : "" }  // the photo of a pixel layer or smart object; a real one's pixels

    // ---- commands -----------------------------------------------------------------------------------------------------
    function run(c) { return !!doc && doc.run(c) }
    // props on every layer picked but the Background (withBg: it too), one History step; those already so are left.
    function setAll(p, withBg) {
        if (!doc)
            return
        doc.beginGesture()
        for (const id of doc.selected) {
            const l = doc.layer(id)
            if (l && (withBg || !l.props.background) && !Object.keys(p).every(k => (l.props[k] ?? null) === p[k]))
                run({ op: "setProps", layer: id, props: p })
        }
        doc.endGesture()
    }
    function lock(k) {  // as the prototype's: all on its own, or one of the three, which takes all off
        const p = active.props
        setAll(k === "lockAll" ? { lockAll: !p.lockAll, lockTransparency: false, lockPixels: false, lockPosition: false }
                               : p[k] ? { [k]: false } : { lockAll: false, [k]: true }, true)
    }
    // A click on a row: the layer alone; Ctrl adds or takes it off; Shift, the rows from the active one to it. On the
    // mask, the mask takes the brush; Shift there turns it off or on.
    function pick(r, mods, part) {
        const id = r.l.id, sel = doc.selected
        if (part === "mask" && (mods & Qt.ShiftModifier))
            return run({ op: "mask", layer: id, action: "off" })
        let ids = [id]
        if (mods & Qt.ControlModifier) {
            ids = sel.indexOf(id) >= 0 ? sel.filter(x => x !== id) : sel.concat([id])
        } else if ((mods & Qt.ShiftModifier) && active) {
            const rows = shown.filter(x => !x.fx).map(x => x.l.id), a = rows.indexOf(active.id), b = rows.indexOf(id)
            if (a >= 0 && b >= 0)
                ids = a < b ? rows.slice(a, b + 1) : rows.slice(b, a + 1).reverse()
        }
        run({ op: "selectLayers", layers: ids })
        doc.setEditMask(part === "mask" && ids[ids.length - 1] === id)
    }
    function deselect() {
        if (!doc)
            return
        run({ op: "selectLayers", layers: [] })
        doc.setEditMask(false)
    }
    // The eye: the layer shown or hidden; Alt, it alone shown (with the groups it is in), or all again. An effect's,
    // that effect on or off; the Hiệu ứng row's, all of them.
    function eye(r, mods) {
        const l = r.l
        if (r.fx) {
            const keys = r.fx === "effects" ? effectsOf(l) : [r.fx], on = !keys.some(k => l.props.fx[k].on)
            const fx = Object.assign({}, l.props.fx)
            for (const k of keys)
                fx[k] = Object.assign({}, fx[k], { on: r.fx === "effects" ? on : !l.props.fx[k].on })
            return run({ op: "setProps", layer: l.id, props: { fx: fx } })
        }
        if (!(mods & Qt.AltModifier))
            return run({ op: "setProps", layer: l.id, props: { visible: !l.props.visible } })
        const keep = {}, under = {}, all = doc.layers, at = all.findIndex(x => x.id === l.id)
        for (let p = l; p; p = p.parent ? doc.layer(p.parent) : null)
            keep[p.id] = true
        for (let i = at + 1; i < all.length && all[i].depth > l.depth; i++)
            under[all[i].id] = true
        const others = all.filter(x => !keep[x.id] && !under[x.id])
        const back = l.props.visible && others.every(x => !x.props.visible)
        doc.beginGesture()
        for (const x of all)
            if (keep[x.id] && !x.props.visible)
                run({ op: "setProps", layer: x.id, props: { visible: true } })
        for (const x of others)
            if (x.props.visible !== back)
                run({ op: "setProps", layer: x.id, props: { visible: back } })
        doc.endGesture()
    }
    function toggleFx(id) {
        Tools.setValue("layers:fxShut", Object.assign({}, fxShut, { [id]: !fxShut[id] }))
    }
    // A double click: the name to rename; elsewhere Layer Style, on the effect's page for an effect's row.
    function opened(r, part) {
        if (part === "name")
            return startRename(r.l.id)
        if (main && (r.fx || !r.l.props.background))
            main.openDialog("LayerStyle", { page: r.fx && r.fx !== "effects" ? r.fx : "blendingOptions", layer: r.l.id })
    }
    function startRename(id) {
        const i = rowOf(id)
        if (i < 0)
            return
        list.positionViewAtIndex(i, ListView.Contain)
        renaming = id
    }
    function rename(l, name, ok) {
        if (!l || renaming !== l.id)
            return
        renaming = 0
        const n = name.trim()
        if (ok && n && n !== l.props.name)
            run({ op: "setProps", layer: l.id, props: { name: n } })
    }
    Connections {  // Layer › Đổi tên layer…
        target: panel.main
        function onRequested(action) {
            if (action === "@rename" && panel.active)
                panel.startRename(panel.active.id)
        }
    }

    // ---- dragging rows ------------------------------------------------------------------------------------------------
    // At y in the list: above or below the row there, or into a group (its middle half, or below it while open). The
    // layer dragged is picked first, as a press picks it in Photoshop.
    function dragTo(r, y) {
        if (!drop && doc.selected.indexOf(r.l.id) < 0)
            run({ op: "selectLayers", layers: [r.l.id] })
        dragY = y
        place()
    }
    function place() {  // the drop at dragY, as the list is scrolled now
        const at = (dragY + list.contentY) / rowHeight, i = Math.max(0, Math.min(shown.length - 1, Math.floor(at)))
        if (i < 0 || !shown[i])
            return
        const t = shown[i], f = at - i, group = t.l.kind === "group" && !t.fx
        const openGroup = group && !doc.collapsed[t.l.id] && t.l.count > 0
        const mode = t.fx || at >= shown.length ? "below" : group ? (f < 0.25 ? "above" : f > 0.75 && !openGroup ? "below" : "into") : f < 0.5 ? "above" : "below"
        drop = { id: t.l.id, mode: mode, at: i }
    }
    // The picked layers (but those inside another of them), top first, moved there one by one in one History step;
    // with Alt their copies. Nothing goes below the Background, or a group into itself (Doc says no).
    function dropAt(mods) {
        const t = drop
        drop = null
        held = null
        if (!t || mods < 0 || !doc)
            return
        const order = doc.layers.map(l => l.id)
        const picked = () => doc.selected.filter(id => {
            for (let p = doc.layer(id).parent; p; p = doc.layer(p).parent)
                if (doc.selected.indexOf(p) >= 0)
                    return false
            return true
        }).sort((a, b) => order.indexOf(a) - order.indexOf(b))
        let ids = picked()
        const alt = !!(mods & Qt.AltModifier)
        if (!ids.length || (!alt && ids.indexOf(t.id) >= 0))
            return
        doc.beginGesture()
        if (alt && run({ op: "duplicateLayers", layers: ids })) {
            const now = doc.layers.map(l => l.id)
            ids = doc.selected.slice().sort((a, b) => now.indexOf(a) - now.indexOf(b))
        }
        for (const id of t.mode === "above" ? ids : ids.slice().reverse()) {  // each next to the last one put
            const l = doc.layer(id), s = doc.layer(t.id)
            if (!l || !s)
                continue
            if (t.mode === "into") {
                run({ op: "moveLayer", layer: id, parent: s.id, index: s.count - (l.parent === s.id ? 1 : 0) })
            } else {
                const k = s.index - (l.parent === s.parent && l.index < s.index ? 1 : 0)  // its index without l
                run({ op: "moveLayer", layer: id, parent: s.parent, index: t.mode === "above" ? k + 1 : k })
            }
        }
        doc.endGesture()
    }

    // ---- menus --------------------------------------------------------------------------------------------------------
    // The Layer menu of MenuSpec (the one with Đổi tên layer…) and the item of it whose submenu holds `pred`'s item.
    function layerMenu() { return MenuSpec.MENUS.find(m => m.items.some(it => it.act === "@rename")) }
    function layerItem(act) {  // its item that runs act: one of its own before a submenu's ("Nhân bản layer…", not Ctrl J's)
        const items = layerMenu().items
        return items.find(it => it.act === act) || items.reduce((o, it) => o || (Array.isArray(it.sub) && it.sub.find(x => x.act === act)) || null, null)
    }
    function subOf(pred) { return (layerMenu().items.find(it => Array.isArray(it.sub) && it.sub.some(pred)) || { sub: [] }).sub }
    // MenuSpec items as the menu bar shows them now: greyed where Main says they cannot run.
    function resolve(items) {
        return items.map(it => it.t === "sep" ? it : Object.assign({}, it, { disabled: !main || !main.can(it), sub: Array.isArray(it.sub) ? resolve(it.sub) : it.sub }))
    }
    // The right button on a row: its layer picked unless it is, then the Layer menu but for what makes new layers,
    // aligns or is always greyed; then the colour labels.
    function contextMenu(r, x, y) {
        if (doc.selected.indexOf(r.entry.id) < 0) {
            run({ op: "selectLayers", layers: [r.entry.id] })
            doc.setEditMask(false)
        }
        const first = it => it.act || it.dialog || (Array.isArray(it.sub) && it.sub.length ? first(it.sub[0]) : "")
        const kept = layerMenu().items.filter(it => it.t === "sep" || (!it.disabled && first(it) && !/^@(newLayer|fill|adj|alignSel)\b/.test(first(it))))
        const items = resolve(kept.filter((it, i) => it.t !== "sep" || (i > 0 && kept[i - 1].t !== "sep")))
        const tag = active ? active.props.tag || null : null
        pop.cap = Infinity
        pop.minWidth = layerMenu().w
        pop.show(items.concat([MenuSpec.SEP, { l: qsTr("Màu nhãn"), w: 180, sub: labels.map(c => ({ l: c[0], check: tag === c[1], act: () => panel.setAll({ tag: c[1] }, true) })) }]), r, x, y)
    }
    // A footer button's menu, above it at the panel's right as the prototype's (.pop right 8, bottom 34), cap tall.
    function footMenu(items, cap) {
        const h = Math.min(cap, 12 + items.reduce((s, it) => s + (it.t === "sep" ? 11 : 28), 0))
        pop.cap = cap
        pop.minWidth = 220
        pop.build(resolve(items))  // first: a long row makes it wider
        pop.popup(panel, panel.width - 8 - pop.width, foot.y - 2 - h)
    }
    PopupMenu {
        id: pop
        property real cap: Infinity
        height: Math.min(implicitHeight, cap)
        modal: true  // a press outside closes it and goes no further: not onto the canvas
        dim: false
        onActivated: item => { if (typeof item.act !== "function" && panel.main) panel.main.act(item) }
    }

    // ---- head ---------------------------------------------------------------------------------------------------------
    component Small: ToolToggle {  // .tb 22 × 22 of the head
        implicitWidth: 22
        implicitHeight: 22
        icon.width: 15
    }
    RowLayout {  // padding 6 8 3
        x: 8
        y: 6
        width: parent.width - 16
        height: 24
        spacing: 2
        TextBox {
            Layout.fillWidth: true
            Layout.rightMargin: 4
            implicitHeight: 24
            leftPadding: 26
            text: panel.query
            placeholderText: qsTr("Tìm layer")
            placeholderTextColor: Theme.t3  // the prototype's placeholder, lighter than TextBox's
            Accessible.name: qsTr("Tìm layer theo tên")
            onTextEdited: Tools.setValue("layers:q", text)
            Icon { x: 6; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 14; color: Theme.t3 }
        }
        Repeater {
            model: panel.kindList
            Small {
                required property var modelData
                icon.name: modelData[2]
                checked: !!panel.kinds[modelData[0]]
                ToolTip.text: modelData[1]
                Accessible.name: modelData[1]
                onClicked: Tools.setValue("layers:kinds", Object.assign({}, panel.kinds, { [modelData[0]]: !panel.kinds[modelData[0]] }))
            }
        }
    }
    RowLayout {  // padding 3 8
        x: 8
        y: 36
        width: parent.width - 16
        height: 24
        spacing: 8
        Dropdown {
            id: blend
            Layout.fillWidth: true
            enabled: !panel.off
            options: Mock.BLENDS.map((b, i) => [b, Mock.BLEND_LABELS[i]]).slice(panel.active && panel.active.kind === "group" ? 0 : 1)
            value: panel.active ? panel.active.props.blend : "normal"
            Accessible.name: qsTr("Blend mode: %1").arg(displayText)
            popup.modal: true  // as the menus: a press outside goes no further
            popup.dim: false
            popup.closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            onPicked: v => panel.setAll({ blend: v })
        }
        NumberField {
            Layout.preferredWidth: implicitWidth - 6  // a 58 box
            label: qsTr("Opacity")
            labelInside: false
            unit: "%"
            min: 0
            max: 100
            enabled: !panel.off
            value: panel.active ? Math.round(panel.active.props.opacity * 100) : 100
            Accessible.name: qsTr("Opacity của layer")
            onStarted: panel.doc.beginGesture()
            onMoved: v => panel.setAll({ opacity: v / 100 })
            onCommitted: panel.doc.endGesture()
        }
    }
    RowLayout {  // padding 3 8 6, the rule under it
        x: 8
        y: 66
        width: parent.width - 16
        height: 24
        spacing: 2
        Text {
            Layout.rightMargin: 4
            text: qsTr("Khóa")
            color: Theme.t3
            font.pointSize: Theme.pt(12)
        }
        Repeater {
            model: panel.lockList
            Small {
                required property var modelData
                enabled: !!panel.active
                icon.name: modelData[2]
                checked: !!panel.active && !!(panel.active.props[modelData[0]] || panel.active.props.lockAll)
                ToolTip.text: modelData[1]
                Accessible.name: modelData[1]
                onClicked: panel.lock(modelData[0])
            }
        }
        Item { Layout.fillWidth: true }
        NumberField {
            Layout.preferredWidth: implicitWidth - 6
            label: qsTr("Fill")
            labelInside: false
            unit: "%"
            min: 0
            max: 100
            enabled: !panel.off
            value: panel.active ? Math.round(panel.active.props.fill * 100) : 100
            Accessible.name: qsTr("Fill của layer")
            onStarted: panel.doc.beginGesture()
            onMoved: v => panel.setAll({ fill: v / 100 })
            onCommitted: panel.doc.endGesture()
        }
    }
    Rectangle { y: 96; width: parent.width; height: 1; color: Theme.ln }

    // ---- rows ---------------------------------------------------------------------------------------------------------
    ListView {
        id: list
        y: 97
        width: parent.width
        height: Math.max(0, parent.height - 97 - 32)
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Qt.NoButton  // the wheel and the scroll bar; drags are the rows'
        Accessible.name: qsTr("Danh sách layer")
        ScrollBar.vertical: ScrollBar {}
        model: panel.shown.length
        delegate: LayerRow {
            id: r
            required property int index
            readonly property var item: panel.shown[index] || { l: null, fx: "" }
            width: ListView.view.width
            height: panel.rowHeight
            size: panel.thumbSize
            entry: item.l
            fx: item.fx
            label: item.fx ? panel.effectName(item.fx) : ""
            shown: !item.l ? true : item.fx === "effects" ? panel.effectsOf(item.l).some(k => item.l.props.fx[k].on) : item.fx ? !!item.l.props.fx[item.fx].on : item.l.props.visible
            hasFx: !!item.l && panel.effectsOf(item.l).length > 0
            fxOpen: !!item.l && !panel.fxShut[item.l.id]
            open: !!item.l && !!panel.doc && !panel.doc.collapsed[item.l.id]
            src: item.l ? panel.thumbUrl(item.l) : ""
            selected: !!item.l && !!panel.doc && panel.doc.selected.indexOf(item.l.id) >= 0
            editMask: !!item.l && !!panel.doc && panel.doc.editMask && !!panel.active && panel.active.id === item.l.id
            renaming: !!item.l && !item.fx && panel.renaming === item.l.id
            onEyeClicked: mods => panel.eye(r.item, mods)
            onTwistClicked: panel.doc.toggleOpen(r.item.l.id)
            onFxClicked: panel.toggleFx(r.item.l.id)
            onPicked: (mods, part) => panel.pick(r.item, mods, part)
            onOpened: part => panel.opened(r.item, part)
            onMenuAsked: (x, y) => panel.contextMenu(r, x, y)
            onDragged: (x, y) => {
                panel.held = r
                panel.dragTo(r.item, r.mapToItem(list, x, y).y)
            }
            ListView.delayRemove: panel.held === r  // scrolled off while dragged: kept, not pooled (a resize destroys pooled rows, the press with them)
            onDropped: mods => panel.dropAt(mods)
            onRenamed: (name, ok) => panel.rename(r.item.l, name, ok)
        }
        footer: Item {  // room below the rows to pick no layer (the button after the list)
            width: ListView.view.width
            height: 40
        }
    }
    AbstractButton {  // the prototype's last item of the list, the room below the rows: no layer picked; after them for Tab
        readonly property real below: list.footerItem ? Math.max(0, list.footerItem.y - list.contentY) : 0  // the rows' end in the list
        y: list.y + below
        width: list.width
        height: Math.max(0, list.height - below)
        Accessible.name: qsTr("Bỏ chọn layer, xem thuộc tính tài liệu")
        onClicked: panel.deselect()
        onActiveFocusChanged: if (activeFocus) list.positionViewAtEnd()  // as Tab reaching it in the list did
    }
    // A drag held at the list's top or bottom edge, or past it, scrolls it, faster the further in.
    // ponytail: the rows between the one held and the view stay made while it scrolls far (all 1.000 at worst).
    Timer {
        readonly property real by: !panel.drop ? 0 : panel.dragY < 24 ? panel.dragY - 24 : Math.max(0, panel.dragY - list.height + 24)
        running: by !== 0
        interval: 16
        repeat: true
        onTriggered: {
            list.contentY = Math.max(list.originY, Math.min(list.contentY + by / 2, list.originY + list.contentHeight - list.height))
            panel.place()
        }
    }
    Rectangle {  // where a drag would put the layers: a line between rows, a frame round a group
        readonly property var d: panel.drop
        visible: !!d && y + height > list.y && y < list.y + list.height
        x: 30
        width: list.width - 30
        y: !d ? 0 : list.y - list.contentY + (d.mode === "into" ? d.at : d.mode === "below" ? d.at + 1 : d.at) * panel.rowHeight - (d.mode === "into" ? 0 : 1)
        height: !d || d.mode !== "into" ? 2 : panel.rowHeight
        color: !d || d.mode !== "into" ? Theme.accent : "transparent"
        border.width: !d || d.mode !== "into" ? 0 : 2
        border.color: Theme.accent
    }

    // ---- foot ---------------------------------------------------------------------------------------------------------
    Item {
        id: foot
        y: list.y + list.height
        width: parent.width
        height: 32
        Rectangle { width: parent.width; height: 1; color: Theme.ln }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 8
            y: 4
            spacing: 2
            ToolToggle {
                enabled: false  // P2 as the prototype's: greyed
                icon.name: "link"
                ToolTip.text: qsTr("Liên kết layer · Link Layers")
                Accessible.name: qsTr("Liên kết layer")
            }
            ToolToggle {
                id: fxButton
                enabled: !!panel.active && !panel.active.props.background
                ToolTip.text: qsTr("Layer style · Add a layer style")
                Accessible.name: qsTr("Thêm layer style")
                onClicked: panel.footMenu(panel.subOf(it => it.dialog === "LayerStyle"), Infinity)
                contentItem: Text {
                    text: "fx"
                    color: !fxButton.enabled ? Theme.t4 : fxButton.hovered ? Theme.t1 : Theme.t2
                    font.pointSize: Theme.pt(12.5)
                    font.italic: true
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            ToolToggle {
                enabled: !!panel.active && panel.doc.supports("mask")  // greyed on a real document until the core has masks
                icon.name: "mask"
                ToolTip.text: qsTr("Thêm Layer Mask · Add layer mask")
                Accessible.name: qsTr("Thêm Layer Mask")
                onClicked: panel.run({ op: "mask", action: panel.doc.selection ? "sel" : "reveal" })
            }
            ToolToggle {
                icon.name: "contrast"
                ToolTip.text: qsTr("Fill, adjustment layer mới · Create new fill or adjustment layer")
                Accessible.name: qsTr("Thêm fill hay adjustment layer")
                onClicked: panel.footMenu(panel.subOf(it => /^@fill:/.test(it.act || "")).concat([MenuSpec.SEP], panel.subOf(it => /^@adj:/.test(it.act || ""))), 420)
            }
            ToolToggle {
                icon.name: "create_new_folder"
                ToolTip.text: qsTr("Nhóm mới · Create a new group (Ctrl G)")
                Accessible.name: qsTr("Nhóm mới")
                onClicked: panel.run({ op: "newGroup" })
            }
            ToolToggle {
                icon.name: "add_box"
                ToolTip.text: qsTr("Layer mới · Create a new layer (Ctrl ⇧ N)")
                Accessible.name: qsTr("Layer mới")
                onClicked: panel.run({ op: "newLayer" })
            }
            ToolToggle {
                enabled: !!panel.active && !!panel.doc && panel.doc.layers.length > 1
                icon.name: "delete"
                ToolTip.text: qsTr("Xóa layer · Delete layer")
                Accessible.name: qsTr("Xóa layer")
                onClicked: panel.run({ op: "deleteLayers" })
            }
        }
    }
}
