import QtQuick
import "Mock.js" as Mock

// One open document (docs/ui-tasks.md, U3), made by Docs: the face of Mock.makeDoc(), whose commands are T1's (core/doc.h)
// and Photoshop's; N1's DocModel has the same API for files opened for real. The properties are for reading (the
// functions change them); each is replaced when it changes, never edited in place, so bindings on it follow. A binding
// that calls a function (can(), layer()) re-runs on rev.
QtObject {
    id: doc
    // Mock.makeDoc(), set once made: createObject() would copy it as an initial property, and its functions change the
    // original.
    property var core: null
    onCoreChanged: sync()
    property int rev: 0  // bumped by every change

    property string file
    property string mem  // the status bar's Tài liệu, "148 MB / 1,02 GB"
    property bool dirty  // unsaved: the tab's dot
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
        if (!core)
            return
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
    // A menu requirement ("sel", "!bgl") or action ("delLayer", "@merge:down"), as the prototype's REQ and cx.
    function can(name) { return rev >= 0 && core.can(name) }
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
