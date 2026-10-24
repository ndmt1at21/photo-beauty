import QtQuick
import "Mock.js" as Mock

// One open document (docs/ui-tasks.md, U3, N1), made by Docs: the face of Mock.makeDoc(), whose commands are T1's
// (core/doc.h) and Photoshop's, or of a DocModel (ui/docmodel.h, C++), a file opened for real, with the same API: the
// panels cannot tell them apart. On a real document what the core cannot do yet can() says no to and run() refuses
// (decision 4: greyed, not hidden). The properties are for reading (the functions change them); each is replaced when it
// changes, never edited in place, so bindings on it follow. A binding that calls a function (can(), layer()) re-runs on
// rev.
QtObject {
    id: doc
    // Mock.makeDoc() or the DocModel, set once made: createObject() would copy a JS object given as an initial property,
    // and its functions change the original.
    property var core: null
    onCoreChanged: sync()
    property int rev: 0  // bumped by every change
    // A file opened for real (Session, N1): its DocModel, which is the core; null for a mock document.
    property var model: null
    onModelChanged: if (model) core = model
    readonly property bool real: !!model  // N3 locks the tools the core lacks by it
    // For CanvasBar (Overlays): the notice bar's text, what runs in the background and its share done (0..1; below 0,
    // none). A mock document has none.
    readonly property string notice: model ? model.notice : ""
    readonly property string task: model ? model.task : ""
    readonly property real progress: model ? model.progress : -1
    property int seen: -1  // the model's version synced
    property Connections changes: Connections {  // what changes in the model outside these functions: a stroke, the file read
        target: doc.model
        function onChanged() { doc.sync() }
    }

    property string file
    property string mem  // the status bar's Tài liệu, "148 MB / 1,02 GB"
    property bool dirty  // unsaved: the tab's dot
    // The steps closing it unsaved loses (SaveChanges): from the step saved on a real document (N2); the mock keeps no
    // saved step, so its History index.
    readonly property int unsaved: model ? model.unsaved : index
    property var photo  // the Mock.PHOTOS index of the image, null for a new document
    property bool faces  // Retouch has faces to work on
    property var view  // { x, y: the document point at the frame's centre; zoom: 1 is 100 %; angle: °, clockwise }

    // What History keeps
    property int w
    property int h
    property real ppi
    property string mode  // "RGB" or "Gray"
    property int bits  // 8 or 16
    property string profile
    property var tree  // the root group, as T1's: { id: 0, kind: "group", children }; a layer { id, kind, props[, children] }
    property var layers  // every layer, top first: { id, kind, props, depth, parent, index, count }
    property var rows  // layers but those inside a collapsed group: the Layers panel's rows
    property var selected  // ids, the active one last
    property var active  // the active layer (an entry of layers), what Properties shows; null without one
    property var selection  // a key of Mock.SHAPES, null without one
    property var prevSelection  // what Chọn lại brings back
    property bool inverse
    property var alphas  // the alpha channels' names
    property int rot  // Image Rotation, ° clockwise
    property bool flipH
    property bool flipV
    property int layerRot  // Edit › Transform on a layer (the mock turns the image)
    property bool layerFlipH
    property bool layerFlipV
    property var state  // all of the above as one History step holds it

    // What History does not keep
    property bool editMask  // the active layer's mask takes the brush (setEditMask)
    property var collapsed  // { group id: true }
    property string channel  // shown: "RGB" (also a grey document's own), "Red", "Green", "Blue" or an alpha channel
    property var hiddenChannels  // { name: true }
    property var history  // steps { name, icon, cmds, state }: cmds made state from the step before
    property int index  // the step History is on
    property var snapshots  // { name, at (the step it was taken on), look, state }
    property int snapshot  // the snapshot shown, -1 for none

    function sync() {
        if (!core || model && model.version === seen)
            return
        seen = model ? model.version : -1
        for (const k of Mock.FIELDS)
            if (doc[k] !== core[k])
                doc[k] = core[k]
        rev++
    }
    // One command: { op, …fields } (Mock.js lists them). One History step with its name and icon, but within a gesture
    // and for selectLayers; false, nothing changed, when it cannot run.
    function run(cmd) {
        const ok = core.run(cmd)
        if (ok)
            sync()
        return ok
    }
    // A menu requirement ("sel", "!bgl") or action ("delLayer", "@merge:down"), as the prototype's REQ and cx; on a real
    // document, one the core has.
    function can(name) { return rev >= 0 && (model ? model.can(name) && Mock.can(doc, name) : core.can(name)) }
    // Whether this document takes `what`: an op of run() ("mask"), a menu action, "panel:<name>" (a panel greyed whole), a
    // MenuSpec item (by its act, dialog or space; free ones always). A mock document takes all; a real one what the core
    // has (DocModel.can()).
    // Re-runs on rev as can() does: the core's answer changes once the file is read.
    function supports(what) {
        if (!model || !what)
            return true
        if (typeof what === "string")
            return rev >= 0 && model.can(what)
        return !!what.free || rev >= 0 && model.can(what.dialog ? "dialog:" + what.dialog : what.space ? "space:" + what.space : what.act || "")
    }
    // A layer's thumbnail: the mock's sample photo, a real layer's pixels (image://pb, its `photo`).
    function thumbUrl(l) { return model ? l.props.photo || "" : Mock.photoUrl(l.props.photo) }
    function dismissNotice() { if (model) model.setNotice("") }
    function undo() { core.undo(); sync() }
    function redo() { core.redo(); sync() }
    function jumpTo(i) { core.jumpTo(i); sync() }  // History step i
    function toggleLast() { core.toggleLast(); sync() }
    function revert() { core.revert(); sync() }  // to the first step
    function jumpToSnapshot(i) { core.jumpToSnapshot(i); sync() }
    function newSnapshot() { core.newSnapshot(); sync() }
    function deleteStep() { core.deleteStep(); sync() }  // the step History is on and those after it
    // The commands between them are one step, the first one's (a slider dragged, a stroke).
    function beginGesture() { core.beginGesture() }
    function endGesture() { core.endGesture() }
    function setView(v) { core.setView(v); sync() }  // some of { x, y, zoom, angle }
    function zoomTo(pct) { core.zoomTo(pct); sync() }  // %, 1 to 12.800
    function toggleOpen(id) { core.toggleOpen(id); sync() }  // a group's rows
    function setEditMask(on) { core.setEditMask(on); sync() }
    function setChannel(name) { core.setChannel(name); sync() }
    function toggleChannel(name) { core.toggleChannel(name); sync() }  // shown or hidden
    function markSaved() { core.markSaved(); sync() }
    function layer(id) {  // the layers entry of id, null when not there
        for (const l of layers)
            if (l.id === id)
                return l
        return null
    }
    function nextName(base) { return rev >= 0 ? Mock.nextName(state, base) : "" }  // "base N" no layer has yet
}
