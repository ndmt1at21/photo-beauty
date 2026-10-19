pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Templates as T
import "MenuSpec.js" as MenuSpec

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
        TitleBar.use(win)
        if (startState)
            start(startState)
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
    // or space's file in the module.
    function can(item) {
        if (!item || item.t === "sep" || item.disabled || !(item.free || Docs.current))
            return false
        if (item.dialog ? !hasDialog(item.dialog) : item.space ? !hasSpace(item.space) : false)
            return false
        return Docs.can(item.req || item.act || "")
    }
    // Runs a MenuSpec item (a menu's, a key's): its dialog, its space, Main's own actions, else Docs.exec(); false when it
    // cannot run.
    function act(item) {
        if (!can(item))
            return false
        acted(item)
        if (item.dialog)
            return !!openDialog(item.dialog, item.params)
        if (item.space)
            return !!openSpace(item.space, item.params)
        switch (MenuSpec.name(item.act)) {
        case "openFile": files.open(); return true
        case "quit": win.close(); return true
        case "close": closeDoc(Docs.current); return true
        case "closeAll": closeAll(); return true
        case "clearRecent": Docs.clearRecent(); return true
        case "rename":
        case "noop":
        case "": requested(item.act || ""); return true
        default: return Docs.exec(item.act)
        }
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
    // Files dropped on the window or picked in Mở…, each its tab: the mock opens a document by the file's name (Docs.open).
    function openFiles(urls) {
        for (const u of urls) {
            const file = decodeURIComponent(String(u).replace(/[?#].*$/, "")).replace(/^.*[\/\\]/, "")
            if (file)
                Docs.open(file)
        }
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

    FocusRing {}
}
