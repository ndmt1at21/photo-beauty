pragma Singleton
import QtQuick
import "Mock.js" as Mock
import "View.js" as View

// The open documents, recent files, files to recover and the background job (docs/ui-tasks.md, U3), as the prototype
// opens (build/proto/Main.dc.html): three documents, "Đang lưu chân dung.psd" at 64 %. exec() runs the menus' actions
// (the prototype's exec). Files opened for real (N1: JPEG, PNG, TIFF, .pbd through Session) are Docs whose core is a
// DocModel (C++), beside the mock ones; the rest still opens as the mock's, by name. Session (N2) saves for real.
QtObject {
    id: ds
    property var docs: []  // Doc, in tab order
    property int index: 0  // of current (select() changes it)
    property Doc current: null  // the document shown, null without any
    property var recent: []  // as Mock.RECENT: { file, photo, w, h, ppi, bits, mem, size, mb, when, order }
    property var recovery: []  // { file, time }
    // One at a time: { on, run (going), done, verb ("save", "export"), name, p (0..100), sp (per tick), doc (unsaved no
    // more when it is done), wait (ticks shown done), label ("Đang lưu chân dung.psd") }.
    property var job: ({ on: false })
    property var viewport: ({ w: 1034, h: 750 })  // the canvas frame, px: what Vừa màn hình fits (U6 sets it)
    property string error: ""  // why the last file did not open, "" once Main has shown it

    readonly property Component docC: Component { Doc {} }
    readonly property Timer ticker: Timer { interval: 250; repeat: true; running: !!ds.job.on; onTriggered: ds.tick() }

    Component.onCompleted: reset()

    // As the prototype opens: "Main" (also no name), "Retouch" (Retouch.dc.html's INIT), "Start" (nothing open).
    function reset(artboard) {
        for (const d of docs)
            if (d.real)
                Session.close(d.model)
        const p = Mock.portrait()
        const list = [make(p), make(Mock.opened(recentOf("san-pham.webp"), viewport.w, viewport.h)),
            make(Object.assign(Mock.opened(recentOf("hoang-hon.tif"), viewport.w, viewport.h), { dirty: true }))]
        recent = Mock.RECENT.slice()
        recovery = Mock.RECOVERY.slice()
        setJob({ on: true, run: true, done: false, verb: "save", name: list[0].file, p: 64, sp: 1, doc: list[0], wait: 0 })
        if (artboard === "Retouch") {
            list[0].run({ op: "selectLayers", layers: [p.ids[Mock.RETOUCH_INIT.docSel]] })
            if (Mock.RETOUCH_INIT.fsClosed)
                list[0].toggleOpen(p.ids.fs)
            setJob({ on: false, run: false, done: false, verb: "save", name: "", p: 0, sp: 1, doc: null, wait: 0 })
        }
        setDocs(artboard === "Start" ? [] : list, 0)
    }
    // core set once made: createObject() copies a JS object given as an initial property.
    // ponytail: a closed document stays a child of Docs until the app quits; destroy() it if sessions open hundreds.
    function make(spec) {
        const d = docC.createObject(ds)
        d.core = Mock.makeDoc(spec)
        return d
    }
    function recentOf(file) { return recent.find((r) => r.file === file) || Mock.RECENT.find((r) => r.file === file) }
    function setDocs(list, i) {
        docs = list
        index = Math.max(0, Math.min(i, list.length - 1))
        current = list[index] || null
    }
    function add(d) {
        setDocs(docs.concat([d]), docs.length)
        return d
    }
    function select(i) {  // a tab
        setDocs(docs, i)
        Tools.xf = false
        Tools.warp = false
    }

    // A file by name: switched to when open, else opened (from recent: for real when it was, or a mock of it).
    function open(file) {
        const at = docs.findIndex((d) => d.file === file)
        if (at >= 0) {
            select(at)
            return docs[at]
        }
        const r = recentOf(file)
        if (r && r.path)
            return openFile(r.path)
        return add(make(file === "chân dung.psd" ? Object.assign(Mock.portrait(), { dirty: false }) : Mock.opened(r || Mock.guess(file), viewport.w, viewport.h)))
    }
    // A file by its path or file: URL (Mở…, a drop, the command line): a JPEG, PNG, TIFF or .pbd opened for real in the
    // background (Session), switched to when open already; any other by its name, as the mock opens it (open()).
    function openFile(file) {
        const path = Session.localPath(String(file))
        const at = path ? docs.findIndex((d) => d.real && d.model.path === path) : -1
        if (at >= 0) {
            select(at)
            return docs[at]
        }
        const m = path ? Session.open(path, viewport.w, viewport.h) : null
        if (m)
            return add(adopt(m))
        const name = (path || decodeURIComponent(String(file).replace(/[?#].*$/, ""))).replace(/^.*[\/\\]/, "")
        return name ? open(name) : null
    }
    // A DocModel's Doc. Read: first in Gần đây; not read: closed, `error` saying why (Main shows it).
    function adopt(m) {
        const d = docC.createObject(ds)
        d.model = m
        let known = false
        m.changed.connect(() => {
            if (known || m.loading)
                return
            known = true
            if (m.error) {
                error = qsTr("Không mở được %1: %2").arg(m.file).arg(m.error)
                close(d)
            } else if (m.path) {
                listed(d)
            }
        })
        return d
    }
    function listed(d) {
        const info = Session.fileInfo(d.model.path), mb = (info.bytes || 0) / 1e6
        const size = mb >= 1000 ? Mock.vn(mb / 1000, 1) + " GB" : mb >= 1 ? Mock.vn(mb, mb >= 10 ? 0 : 1) + " MB" : Mock.vn(mb * 1000, 0) + " KB"
        const r = { file: d.file, path: d.model.path, photo: null, w: d.w, h: d.h, ppi: d.ppi, bits: d.bits, mem: d.mem, size: size, mb: mb,
            when: qsTr("Hôm nay, %1").arg(Qt.formatTime(new Date(), "HH:mm")), order: Date.now() }
        recent = [r].concat(recent.filter((x) => x.path !== r.path))
    }
    // CanvasBar's cancel: a file being read closes; P1's tasks hear DocModel.canceled().
    function cancelTask(d) {
        if (d && d.real && d.model.loading)
            close(d)
        else if (d && d.real)
            d.model.cancelTask()
    }
    function openRecent(i) { return recent[i] ? open(recent[i].file) : null }
    function forgetRecent(file) { recent = recent.filter((r) => r.file !== file) }
    function clearRecent() { recent = [] }
    // Tạo mới: { name, w, h (px), ppi, mode "RGB" | "Gray", bits 8 | 16, background "white" | "black" | "background"
    // (Màu nền sau) | "transparent" | "custom" (color "#rrggbb"), profile }; null when it cannot be made.
    function create(o) {
        try {
            const bg = o.background === "background" ? { background: "custom", color: Tools.bg } : {}
            return add(make(Mock.created(Object.assign({}, o, bg), viewport.w, viewport.h)))
        } catch (e) {
            console.warn("Docs.create:", e.message)
            return null
        }
    }
    function close(d) {  // asking about unsaved changes is the caller's (U20)
        const i = docs.indexOf(d)
        if (i < 0)
            return
        setDocs(docs.slice(0, i).concat(docs.slice(i + 1)), index > i ? index - 1 : index)
        Tools.xf = false
        Tools.warp = false
        if (d.real)
            Session.close(d.model)
    }
    function closeAll() {
        const gone = docs
        setDocs([], 0)
        Tools.xf = false
        Tools.warp = false
        for (const d of gone)
            if (d.real)
                Session.close(d.model)
    }
    function duplicate(d) {  // Image › Nhân bản, History's new document
        if (!d)
            return null
        if (!d.real)
            return add(make(Mock.duplicated(d.core)))
        const m = Session.duplicate(d.model, Mock.duplicated(d).file)
        return m ? add(adopt(m)) : null
    }
    function recover(i) {  // a recovered file, open and unsaved
        const r = recovery[i]
        if (!r)
            return null
        recovery = recovery.filter((x) => x !== r)
        return add(make(Object.assign(Mock.opened(Mock.guess(r.file), viewport.w, viewport.h), { dirty: true })))
    }
    function dropRecovery(i) { recovery = recovery.filter((x, k) => k !== i) }

    // The background job: a save clears the document's unsaved dot when it ends; done, it shows "Đã lưu …" 10 ticks.
    function save(d) { if (d) startJob("save", d.file, d, 2) }
    function quickExport(d) { if (d) startJob("export", d.file.replace(/\.[^.]+$/, "") + ".png", null, 4) }
    function exportAs(d, file) { if (d) startJob("export", file, null, 4) }
    function startJob(verb, name, d, sp) { setJob({ on: true, run: true, done: false, verb: verb, name: name, p: 0, sp: sp, doc: d, wait: 0 }) }
    function cancelJob() { setJob(Object.assign({}, job, { on: false, run: false })) }
    function setJob(j) {
        const save = j.verb === "save"
        j.label = (j.done ? (save ? qsTr("Đã lưu %1") : qsTr("Đã xuất %1")) : (save ? qsTr("Đang lưu %1") : qsTr("Đang xuất %1"))).arg(j.name)
        job = j
    }
    function tick() {
        const j = job
        if (!j.on)
            return
        if (j.run && j.p + j.sp < 100)
            return setJob(Object.assign({}, j, { p: j.p + j.sp }))
        if (j.run) {
            if (j.doc)
                j.doc.markSaved()
            return setJob(Object.assign({}, j, { p: 100, run: false, done: true, wait: 0 }))
        }
        setJob(Object.assign({}, j, j.wait >= 10 ? { on: false } : { wait: j.wait + 1 }))
    }

    // Whether a menu requirement or action can run on the current document (Doc.can()), none open too.
    function can(name) { return current ? current.can(name) : Mock.can(null, name) }
    // A menu's action, as the prototype's exec(): "@name" or "@name:arg" ("@merge:down"). False when it is not one of
    // these (an artboard "=Board", "@dlg:…", "@rename", "@noop": the caller's) or cannot run.
    function exec(action) {
        const a = String(action).replace(/^@/, ""), at = a.indexOf(":")
        const n = at < 0 ? a : a.slice(0, at), arg = at < 0 ? "" : a.slice(at + 1)
        const d = current
        if (!can(n))
            return false
        const run = (c) => !!d && d.run(c)
        const zoomStep = (dir) => {
            const z = d.view.zoom * 100
            const L = Mock.ZOOMS.filter((x) => (dir > 0 ? x > z + 0.001 : x < z - 0.001))
            return L.length ? (dir > 0 ? L[0] : L[L.length - 1]) : z
        }
        const transformOff = () => {
            Tools.xf = false
            Tools.warp = false
        }
        switch (n) {
        case "undo": d.undo(); return true
        case "redo": d.redo(); return true
        case "toggleLast": d.toggleLast(); return true
        case "revert": d.revert(); return true
        case "close": close(d); return true
        case "closeAll": closeAll(); return true
        case "save": save(d); return true
        case "quickExport": quickExport(d); return true
        case "open": return !!openRecent(+arg)
        case "dupDoc": return !!duplicate(d)
        case "place": return run({ op: "placeEmbedded" })
        case "paste": return run({ op: "paste" })
        case "xf": Tools.xf = true; Tools.warp = false; return true
        case "warpOn": Tools.xf = true; Tools.warp = true; return true
        case "warp": Tools.xf = true; Tools.warp = !Tools.warp; return true
        case "xfApply": { const ok = run({ op: "step", name: Tools.warp ? qsTr("Warp") : qsTr("Free Transform"), icon: "crop_free" }); transformOff(); return ok }
        case "xfCancel": transformOff(); return true
        case "rot": return run({ op: "rotateLayer", angle: +arg })
        case "flip": return run({ op: "flipLayer", axis: arg })
        case "crot": return run({ op: "rotateCanvas", angle: +arg })
        case "cflip": return run({ op: "flipCanvas", axis: arg })
        case "mode": return run({ op: "setMode", mode: arg })
        case "bits": return run({ op: "setMode", bits: +arg })
        case "invert": return run({ op: "step", name: qsTr("Invert"), icon: "invert_colors" })
        case "step": return run({ op: "step", name: arg })
        case "cropSel": return run({ op: "cropToSelection" })
        case "newLayer": return run({ op: "newLayer" })
        case "newGroup": return run({ op: "newGroup" })
        case "dupLayer": return run({ op: "duplicateLayers" })
        case "cutLayer": return run({ op: "cutToLayer" })
        case "delLayer": return run({ op: "deleteLayers" })
        case "delHidden": return run({ op: "deleteHidden" })
        case "fill": return run({ op: "newFillLayer", fill: arg, color: arg === "Gradient" ? Tools.gradientCss() : Tools.fg })
        case "adj": {
            const ok = run({ op: "newAdjustmentLayer", adjustment: arg })
            if (ok)
                Tools.showPanel("Properties")
            return ok
        }
        case "mask": return run({ op: "mask", action: arg })
        case "clip": return run({ op: "clippingMask" })
        case "toSO": return run({ op: "toSmartObject" })
        case "rast": return run({ op: "rasterize" })
        case "groupLayer": return run({ op: "groupLayers" })
        case "ungroup": return run({ op: "ungroup" })
        case "hideLayer": {  // the selected layers, to the opposite of the active one's
            const shown = !d.active.props.visible
            let ok = false
            d.beginGesture()
            for (const id of d.selected)
                ok = run({ op: "setProps", layer: id, props: { visible: shown } }) || ok
            d.endGesture()
            return ok
        }
        case "arr": return run({ op: "arrange", dir: arg })
        case "alignSel": return run({ op: "step", name: qsTr("Căn theo vùng chọn"), icon: "align_horizontal_center" })
        case "lockAll": return run({ op: "setProps", props: { lockAll: !d.active.props.lockAll, lockTransparency: false, lockPixels: false, lockPosition: false } })
        case "merge": return run({ op: arg === "flat" ? "flattenImage" : arg === "visible" ? "mergeVisible" : "mergeDown" })
        case "selAll": return run({ op: "select", shape: "all" })
        case "deselect": return run({ op: "deselect" })
        case "reselect": return run({ op: "reselect" })
        case "inverse": return run({ op: "inverse" })
        case "ants": return run({ op: "select", shape: arg })
        case "loadSel": return run({ op: "select", shape: "skin", name: qsTr("Nạp vùng chọn") })
        case "saveSel": return run({ op: "saveSelection" })
        case "qm": Tools.quickMask = !Tools.quickMask; return true
        case "zoomIn": d.zoomTo(zoomStep(1)); return true
        case "zoomOut": d.zoomTo(zoomStep(-1)); return true
        case "zoomFit": d.zoomTo(View.fit(d.w, d.h, viewport, d.view.angle) * 100); return true  // the view turned too
        case "zoomFill": d.zoomTo(Math.max(viewport.w / d.w, viewport.h / d.h) * 100); return true
        case "zoom100": d.zoomTo(100); return true
        case "tog": Tools.toggle(arg); return true
        case "scr": Tools.screen = +arg; return true
        case "snapTo": Tools.toggleSnap(arg); return true
        case "clearGuides": Tools.guides = false; return true
        case "newGuide": Tools.guides = true; return true
        case "win": Tools.showPanel(arg); return true
        case "side": Tools.sidePanel = arg; return true
        case "aa": Tools.antiAlias = arg; return true
        case "fp": Tools.fontPreview = +arg; return true
        case "cropClear": Tools.setValues({ "crop:2": "Ratio", "crop:3": "", "crop:5": "" }); return true
        case "cropSwap": Tools.setValues({ "crop:3": Tools.value("crop:5", "5"), "crop:5": Tools.value("crop:3", "4") }); return true
        case "cropCancel": Tools.tool = "move"; return true
        case "cropApply": {
            if (!d)
                return false
            const b = Tools.cropBox(d.w, d.h)
            return run({ op: "crop", w: Math.round(b.w), h: Math.round(b.h) })
        }
        default: return run({ op: n, arg: arg })  // a command of the core (P1's N tasks): only MenuSpec names it
        }
    }
}
