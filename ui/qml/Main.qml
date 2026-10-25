pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Templates as T
import QtCore
import "MenuSpec.js" as MenuSpec
import "ToolSpec.js" as ToolSpec
import "Presets.js" as Presets
import "Mock.js" as Mock

// The app's window (docs/ui-tasks.md, U4; artboards Main, Start, the grid of Tokens): top bar 44, options bar 36,
// toolbar 44, document tabs 28, the canvas, icon column 36, dock 308, status bar 24, each a type of its own that the task
// filling it replaces, sized by its implicit size; the start screen while no document is open. F goes through the three
// screen modes, Tab hides the panels; the menus' keys; dialogs and full-window spaces by name (openDialog, openSpace);
// files dropped on the window open. Thiết lập's theme, accent and font size go to Theme.
ApplicationWindow {
    id: win
    // --state: Main, Start, Retouch (the artboards' documents and tools), a dialog or a space by name, which opens over
    // Main; "Filter:gaussianBlur" gives a dialog its main param (filter, adjust, page).
    property string startState: ""
    property string startTheme: ""  // --theme: "dark" or "light", over Thiết lập's for this run; "" without it
    // main.cpp: keep Prefs between starts; tests and screenshots start from the defaults.
    property bool persist: false
    // main.cpp: this machine's numbers in Thiết lập › Hiệu năng, its budget and scratch folder set from there (Prefs.machine,
    // N2); tests and screenshots keep the prototype's machine.
    property bool machine: false
    property var startFiles: []  // main.cpp: the files on the command line, opened once the window is up (N1)
    readonly property bool hasDoc: Docs.docs.length > 0
    readonly property int screenMode: hasDoc ? Tools.screen : 0  // 0 chuẩn, 1 toàn màn hình có menu, 2 toàn màn hình
    property bool panelsHidden: false  // Tab: toolbar, options bar, icon column and dock
    property Item space: null  // the full-window space open (openSpace())
    signal acted(var item)  // act() runs a MenuSpec item: a menu's or its keys'
    signal requested(string action)  // what Main leaves to the panels: "@rename" (Layers, U9), "@noop"

    width: 1440
    height: 900
    minimumWidth: 1280
    minimumHeight: 800
    visible: true
    title: "photo-beauty"
    color: Theme.bg
    font.family: Theme.sans
    font.pointSize: Theme.pt(12)

    Binding { target: Theme; property: "dark"; value: win.startTheme ? win.startTheme !== "light" : Prefs.value("theme") !== 1 }
    Binding { target: Theme; property: "accent"; value: Prefs.value("accent") || Theme.accents[0] }
    Binding { target: Theme; property: "fontSize"; value: ["s", "m", "l"][Prefs.value("fontSize")] || "m" }

    Component.onCompleted: {
        Prefs.persist = persist
        Prefs.machine = machine
        MenuSpec.setDocCan(n => Docs.can(n))  // the items that follow the layer picked (Layer › Mới's Background, Liên kết layer)
        TitleBar.use(win)
        if (startState)
            start(startState)
        // the command line's files, then PB_CANVAS's (V1, V2b: a file to paint and measure on), as if given there too
        const files = startFiles.concat(CanvasEnv.document ? [CanvasEnv.document] : [])
        if (files.length)
            openFiles(files)
    }
    function start(state) {
        const at = state.indexOf(":"), name = at < 0 ? state : state.slice(0, at), arg = at < 0 ? "" : state.slice(at + 1)
        const board = name === "Start" || name === "Retouch" ? name : "Main"
        Docs.reset(board)
        Tools.reset(board)
        if (board === "Start")
            Docs.cancelJob()  // the artboard has no job
        if (name === board)
            return
        const key = ({ Filter: "filter", Adjust: "adjust", LayerStyle: "page", Settings: "page", Update: "state" })[name]
        const params = key && arg ? { [key]: arg } : {}
        if (hasSpace(name))
            openSpace(name, params)
        else if (hasDialog(name))
            openDialog(name, params)
        else
            console.warn("--state: no artboard, dialog or space", state)
    }

    // ---- items, dialogs, spaces --------------------------------------------------------------------------------------
    // Whether a MenuSpec item can run now: a document open unless it is free, its requirement (Docs.can()), its dialog's
    // or space's file in the module, the context of a key of MenuSpec.KEYS (U31).
    function can(item) {
        if (!item || item.t === "sep" || item.disabled || !(item.free || Docs.current))
            return false
        if (item.dialog ? !hasDialog(item.dialog) : item.space ? !hasSpace(item.space) : false)
            return false
        if (Docs.current && !Docs.current.supports(item.uses !== undefined ? item.uses : item))  // a real document: what the core has (decision 4)
            return false
        if (!inContext(item))
            return false
        return Docs.can(item.req || item.act || "")
    }
    // A key's ctx (MenuSpec.KEYS: a tool with a brush, one whose Mode has the blend modes, Move or a selection tool with
    // a selection but while Free Transform or Warp is on), and another document for Đóng các tài liệu khác, Ctrl Tab.
    function inContext(item) {
        const n = MenuSpec.name(item.act), t = baseTool
        if ((n === "closeOthers" || n === "nextDoc") && Docs.docs.length < 2)
            return false
        switch (item.ctx) {
        case "brush": return !!ToolSpec.PAINT_T[t] && !Tools.xf
        case "paint": return ToolSpec.modeCell(ToolSpec.barOf(t, Tools.xf, Tools.warp)) >= 0
        case "nudge": return !Tools.xf && (t === "move" || (!!ToolSpec.SEL_T[t] && !!Docs.current && !!Docs.current.selection))  // Free Transform, Warp: theirs (U34)
        default: return true
        }
    }
    // The tool in use, but the one under a key held a moment (U5: Space Hand, Alt Eyedropper, Ctrl Move): Alt ⇧ M is the
    // brush's Multiply after Alt held long enough to pick colours.
    readonly property string baseTool: toolbar.held ? toolbar.heldBase : Tools.tool
    // Runs a MenuSpec item (a menu's, a key's): its dialog, its space, Main's own actions, else Docs.exec(); false when it
    // cannot run.
    function act(item) {
        if (!can(item))
            return false
        acted(item)
        if (item.dialog === "SaveOptions" && Docs.current.real) {  // Lưu thành, Lưu bản sao: a .pbd, where the FileDialog says
            askPath(Docs.current, !!(item.params && item.params.copy), null)
            return true
        }
        if (item.dialog)
            return !!openDialog(item.dialog, item.params)
        if (item.space)
            return !!openSpace(item.space, item.params)
        const d = Docs.current, arg = MenuSpec.arg(item.act)
        switch (MenuSpec.name(item.act)) {
        case "openFile": files.open(); return true
        case "quit": win.close(); return true
        case "close": closeDoc(Docs.current); return true
        case "closeAll": closeAll(); return true
        case "closeOthers": closeOthers(); return true
        case "clearRecent": Docs.clearRecent(); return true
        case "rename":
        case "noop":
        case "": requested(item.act || ""); return true
        // MenuSpec.KEYS' own (U31); the rest of its acts are mock commands, Docs.exec()'s
        case "nextDoc": Docs.select((Docs.index + Number(arg) + Docs.docs.length) % Docs.docs.length); return true
        case "layerSel": return pickLayers(MenuSpec.layerSel(d.rows, d.selected, arg))
        case "deselectLayers": return pickLayers([])
        case "blend": return blend(arg)
        case "fillWith": {  // "foreground" or "background", ",keep" what is transparent
            const a = arg.split(",")
            return d.run({ op: "fill", use: a[0], color: a[0] === "foreground" ? Tools.fg : Tools.bg, preserve: a[1] === "keep" })
        }
        case "nudge": {  // "Left", "Shift+Left"…: 1 px, 10 with Shift; the layers with Move, the selection with a selection tool
            const px = /Shift/.test(arg) ? 10 : 1, dir = { Left: [-px, 0], Right: [px, 0], Up: [0, -px], Down: [0, px] }[arg.replace(/^.*\+/, "")]
            return d.run({ op: "nudge", what: baseTool === "move" ? "layer" : "selection", dx: dir[0], dy: dir[1] })
        }
        case "maskView": {  // \: the active layer's mask as a red overlay, or not (Layers shows it; not History's)
            if (!d.active || !d.active.props.mask)
                return false
            Tools.setValue("layers:maskView", MenuSpec.maskView(Tools.value("layers:maskView", null), d, d.active.id, arg))
            return true
        }
        case "brushPreset": return brushPreset(arg)
        default: return Docs.exec(item.act)
        }
    }
    // MenuSpec.KEYS' layer keys: ids picked (the active one last), the layer and not its mask; false for none, or no change.
    function pickLayers(ids) {
        if (!ids || !Docs.current.run({ op: "selectLayers", layers: ids }))
            return false
        Docs.current.setEditMask(false)
        return true
    }
    // Shift + −, Alt ⇧ + a letter: the Mode of the painting tool in use (its bar's, Tools.values), else the blend mode of
    // the layers picked but the Background, one History step, as Photoshop's.
    function blend(arg) {
        const d = Docs.current, bar = ToolSpec.barOf(baseTool, Tools.xf, Tools.warp), i = ToolSpec.modeCell(bar)
        if (i >= 0) {
            const key = ToolSpec.key(bar, i), c = ToolSpec.cell(bar, i), now = Tools.value(key, c.def), v = MenuSpec.blendAfter(c.list, now, arg, true)
            if (!v || v === now)
                return false
            Tools.setValue(key, v)
            return true
        }
        const a = d.active
        if (!a || a.props.background)
            return false
        const v = MenuSpec.blendAfter(Mock.BLENDS.filter(b => b !== "passThrough" || a.kind === "group"), a.props.blend, arg, false)
        let ok = false
        d.beginGesture()
        for (const id of v ? d.selected : []) {
            const l = d.layer(id)
            if (l && !l.props.background && l.props.blend !== v && (v !== "passThrough" || l.kind === "group"))
                ok = d.run({ op: "setProps", layer: id, props: { blend: v } }) || ok
        }
        d.endGesture()
        return ok
    }
    // , . and with Shift: the brush preset before, after, first, last (Brushes' presets in order, the one in use the last
    // with the size and hardness of the painting tool's brush, as the panel's) as that brush.
    function brushPreset(arg) {
        const bar = ToolSpec.brushBar(baseTool, Tools.xf), b = ToolSpec.brush(bar)
        const all = Tools.value("brushes:groups", Presets.BR_GROUPS).reduce((o, g) => o.concat(g[1]), [])
        const size = Tools.value(bar + ":size", b.size), hard = Tools.value(bar + ":hard", b.hard)
        let at = -1
        all.forEach((p, i) => { if (p[1] === size && p[2] === hard) at = i })
        const to = MenuSpec.presetAfter(all.length, at, arg)
        if (to < 0)
            return false
        Tools.setValues({ [bar + ":size"]: all[to][1], [bar + ":hard"]: all[to][2] })
        return true
    }
    // The Component of a type of the module, null when there is none, looked up once.
    property var types: ({})
    function type(name) {
        if (!(name in types)) {
            const c = Qt.createComponent("PhotoBeauty", name, Component.PreferSynchronous)
            types[name] = c.status === Component.Ready ? c : null
        }
        return types[name]
    }
    function hasDialog(name) { return !!type(name + "Dialog") }
    function hasSpace(name) { return !!type(name + "Space") }
    // <name>Dialog.qml (a DialogBox) over the window, its params set, open; destroyed once closed. Null when there is no
    // such dialog.
    property var dialogs: []  // those open, the last on top
    function openDialog(name, params) {
        const c = type(name + "Dialog"), d = c ? c.createObject(win.contentItem, { params: params || {} }) : null
        if (!d) {
            console.warn("openDialog:", name, c ? c.errorString() : "no such dialog")
            return null
        }
        d.closed.connect(() => {
            dialogs = dialogs.filter(x => x !== d)
            d.destroy()
        })
        dialogs = dialogs.concat([d])
        d.open()
        return d
    }
    // <name>Space.qml over the whole window in the work area's place, until it emits finished(ok); then the document and
    // the tool are those it was opened on, but a document it opened (Select and Mask's Output To New Document) stays
    // in front, as in Photoshop. Null when there is no such space, or one is open.
    function openSpace(name, params) {
        const c = type(name + "Space")
        if (c && !space)  // a key held for a tool (Space, Alt, Ctrl: U5) lets go of it, so the tool given back is the one before
            toolbar.release(toolbar.held)
        const s = c && !space ? c.createObject(win.contentItem, { params: params || {} }) : null
        if (!s) {
            console.warn("openSpace:", name, c ? (space ? "a space is open" : c.errorString()) : "no such space")
            return null
        }
        const doc = Docs.current, docs = Docs.docs, tool = Tools.tool
        s.anchors.fill = win.contentItem
        s.finished.connect(() => {
            win.space = null
            s.destroy()
            const i = Docs.docs.indexOf(doc)
            if (i >= 0 && docs.indexOf(Docs.current) >= 0)
                Docs.select(i)
            Tools.tool = tool
        })
        space = s
        s.forceActiveFocus()
        return s
    }

    // ---- documents ------------------------------------------------------------------------------------------------
    // Closing what is not saved asks first, once SaveChanges (U20) is there: { docs, done } where done() goes on closing.
    function closeDoc(d) {
        if (!d)
            return
        if (d.dirty && hasDialog("SaveChanges"))
            openDialog("SaveChanges", { docs: [d], done: () => Docs.close(d) })
        else
            Docs.close(d)
    }
    // File › Đóng các tài liệu khác (U31): all but the one in front, asking first about those not saved.
    function closeOthers() {
        const keep = Docs.current, others = Docs.docs.filter(d => d !== keep), dirty = others.filter(d => d.dirty)
        const go = () => others.forEach(d => Docs.close(d))
        if (dirty.length && hasDialog("SaveChanges"))
            openDialog("SaveChanges", { docs: dirty, done: go })
        else
            go()
    }
    function closeAll() {
        const dirty = Docs.docs.filter(d => d.dirty)
        if (dirty.length && hasDialog("SaveChanges"))
            openDialog("SaveChanges", { docs: dirty, done: () => Docs.closeAll() })
        else
            Docs.closeAll()
    }
    property bool leaving: false  // SaveChanges let the window close
    onClosing: close => {
        const dirty = Docs.docs.filter(d => d.dirty)
        if (leaving || !dirty.length || !hasDialog("SaveChanges"))
            return
        close.accepted = false
        openDialog("SaveChanges", { docs: dirty, done: () => { win.leaving = true; win.close() } })
    }
    // Files dropped on the window, picked in Mở…, given on the command line (paths or file: URLs), each its tab: JPEG,
    // PNG, TIFF and .pbd read for real, the rest as the mock opens them, by name (Docs.openFile()).
    function openFiles(urls) {
        for (const u of urls)
            Docs.openFile(u)
    }
    // Lưu thành, Lưu bản sao of a real document, and Lưu of one without a .pbd yet (Docs.pathWanted): where its .pbd goes
    // (N2; JPEG, PNG, TIFF, PSD wait for P1), beside its file and named after it at first. Once a file is picked Docs
    // saves there, then then() (SaveChanges goes on); nothing when none is.
    function askPath(d, copy, then) {
        if (!d || !d.real)
            return
        const path = d.model.path
        const folder = path ? Session.fileUrl(path.replace(/[\/\\][^\/\\]*$/, "")) : StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        const name = (d.file.replace(/\.[^.]+$/, "") || "photo-beauty") + (copy ? qsTr(" bản sao") : "") + ".pbd"
        savePath.doc = d
        savePath.copy = !!copy
        savePath.then = then || null
        savePath.currentFolder = folder
        savePath.selectedFile = String(folder).replace(/\/?$/, "/") + encodeURIComponent(name)
        savePath.open()
    }
    FileDialog {
        id: savePath
        objectName: "savePath"
        property Doc doc: null
        property bool copy: false
        property var then: null
        title: copy ? qsTr("Lưu bản sao") : qsTr("Lưu thành")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pbd"
        nameFilters: [qsTr("Tài liệu photo-beauty (*.pbd)")]
        onAccepted: {
            const next = savePath.then
            savePath.then = null
            if (Docs.saveAs(savePath.doc, savePath.selectedFile, savePath.copy) && next)
                next()
        }
        onRejected: savePath.then = null
    }
    FileDialog {
        id: files
        title: qsTr("Mở")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Ảnh và tài liệu (*.jpg *.jpeg *.png *.tif *.tiff *.webp *.heic *.psd *.psb *.pbd *.dng *.cr2 *.cr3 *.nef *.arw *.raf *.orf *.rw2)"), qsTr("Mọi file (*)")]
        onAccepted: win.openFiles(selectedFiles)
    }
    DropArea {  // under the rest, so a drop area of the start screen comes first
        anchors.fill: parent
        enabled: !win.space
        onEntered: drag => drag.accepted = drag.hasUrls
        onDropped: drop => {
            win.openFiles(drop.urls)
            drop.acceptProposedAction()
        }
    }

    // ---- the window's areas -------------------------------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: !win.space
        TopBar {
            visible: win.screenMode < 2
            Layout.fillWidth: true
            can: item => win.can(item)
            onActivated: item => win.act(item)
        }
        StartScreen { visible: !win.hasDoc; Layout.fillWidth: true; Layout.fillHeight: true }
        OptionsBar { visible: win.hasDoc && win.screenMode < 2 && Tools.showOpts && !win.panelsHidden; Layout.fillWidth: true }
        RowLayout {
            visible: win.hasDoc
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Toolbar { id: toolbar; visible: win.screenMode < 2 && Tools.showTools && !win.panelsHidden; Layout.fillHeight: true }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    DocTabs { visible: win.screenMode === 0; Layout.fillWidth: true }
                    Viewport { Layout.fillWidth: true; Layout.fillHeight: true }
                }
                // A press while a menu or popup is open closes it and ends there, not on the canvas, as the prototype's
                // scrim. Qt closes the popup first, so this stays up until that press is through.
                MouseArea { id: scrim; anchors.fill: parent; visible: false; acceptedButtons: Qt.AllButtons }
            }
            SideColumn { visible: win.screenMode < 2 && !win.panelsHidden; Layout.fillHeight: true }
            Dock { visible: win.screenMode < 2 && !win.panelsHidden; Layout.fillHeight: true }
        }
        StatusBar { visible: win.hasDoc && win.screenMode === 0; Layout.fillWidth: true }
    }
    // A popup open that a press outside it closes and lets through (TitleBar.popupOpen(): not modal, tooltips aside):
    // the top bar's menus, the status bar's list, a panel's menu or drop-down list, a slider under an options bar's
    // field. Looked up as Qt Quick puts a popup on its overlay or takes one off.
    property bool popupOpen: false
    Connections {
        target: win.Overlay.overlay
        function onChildrenChanged() { win.popupOpen = TitleBar.popupOpen(win.contentItem) }
    }
    onPopupOpenChanged: {
        if (popupOpen)
            scrim.visible = true
        else
            Qt.callLater(() => scrim.visible = win.popupOpen)
    }
    AbstractButton {
        id: exitFull
        visible: win.screenMode === 2 && !win.space
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        implicitHeight: 28
        leftPadding: 12
        rightPadding: 12
        text: qsTr("Thoát toàn màn hình (F)")
        onClicked: Tools.screen = 0
        background: Rectangle { radius: 4; color: Theme.onPhoto }
        contentItem: Text { text: exitFull.text; color: "white"; font.pointSize: Theme.pt(12); verticalAlignment: Text.AlignVCenter }
    }
    // Toàn màn hình, with the menus or not, fills the screen with the window, as Photoshop's; back in Chế độ chuẩn the
    // window is as it was. Not offscreen, where there is no screen to fill.
    property int windowed: Window.Windowed
    onScreenModeChanged: {
        if (Qt.platform.pluginName === "offscreen")
            return
        if (screenMode > 0 && visibility !== Window.FullScreen) {
            windowed = visibility
            showFullScreen()
        } else if (screenMode === 0 && visibility === Window.FullScreen) {
            visibility = windowed
        }
    }

    // ---- keys ---------------------------------------------------------------------------------------------------------
    // The menus' (MenuSpec.keyed()), each on while its item can run; a text field keeps the keys it takes.
    Instantiator {
        model: MenuSpec.keyed()
        delegate: Shortcut {
            required property var modelData
            sequences: modelData.sequences
            enabled: !win.space && win.can(modelData.item)
            onActivated: win.act(modelData.item)
        }
    }
    // The keys no menu item has (MenuSpec.KEYS, bound(); U31): on as an item can run (its ctx too), and while no text
    // field has the focus; a plain one (no Ctrl or Alt: arrows, , . \) also while no control has it, as Tab.
    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    readonly property bool controlFocus: activeFocusItem instanceof T.Control
    Instantiator {
        model: MenuSpec.bound()
        delegate: Shortcut {
            required property var modelData
            sequences: modelData.sequences
            enabled: !win.space && !win.typing && !(modelData.plain && win.controlFocus) && win.can(modelData.item)
            onActivated: win.act(modelData.item)
        }
    }
    Shortcut {
        sequence: "F"
        enabled: win.hasDoc && !win.space
        onActivated: Tools.screen = (Tools.screen + 1) % 3
    }
    // Tab hides the panels while no control has the focus; from a control, Tab goes on to the next.
    Shortcut {
        sequence: "Tab"
        enabled: win.hasDoc && !win.space && !(win.activeFocusItem instanceof T.Control) && !(win.activeFocusItem instanceof TextInput)
        onActivated: win.panelsHidden = !win.panelsHidden
    }

    // A file that did not open (Docs.error): why, in a toast made for it (as BugReportDialog's) at the foot of the window.
    Connections {
        target: Docs
        function onPathWanted(d, copy, then) { win.askPath(d, copy, then) }
        function onErrorChanged() {
            const t = Docs.error ? win.type("Toast")?.createObject(win.contentItem, { text: Docs.error, err: true }) : null
            if (!t)
                return
            t.done.connect(() => {
                t.destroy()
                Docs.error = ""
            })
            t.restart()
        }
    }

    FocusRing {}
}
