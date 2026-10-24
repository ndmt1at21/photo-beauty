pragma ComponentBehavior: Bound
import QtQuick
import "View.js" as View
import "MenuSpec.js" as MenuSpec
import "ToolSpec.js" as ToolSpec

// The canvas area (docs/ui-tasks.md, U6; artboard Main): rulers, then in the frame the canvas (MockCanvas; V1's Canvas
// in its place), the overlays (U7) and the pointer, on the grey Thiết lập picks for the screen mode. Pan: the Hand tool
// (Space held: Tools.tool hand), the middle button, the wheel; zoom, about the pointer by ZOOMS' steps: the wheel with
// Alt (or without, Thiết lập › Zoom bằng con lăn), the Zoom tool's click (Alt or its bar: out), its Scrubby Zoom drag,
// held still with Zoom có chuyển động; the view turned by the Rotate View drag, Shift 15° steps, Esc back to 0°. The
// menus' zoom (Ctrl +, −, 0, 1) go through Docs.exec(). Right click: the tool's menu, made of the menu bar's items.
Rectangle {
    id: vp
    property Doc doc: Docs.current
    property real dpr: Screen.devicePixelRatio
    readonly property bool rulers: Tools.rulers && !!doc
    readonly property var size: ({ w: frame.width, h: frame.height, dpr: vp.dpr })
    readonly property bool sized: frame.width > 0 && frame.height > 0
    // What the canvas, the overlays and the rulers show: Doc.view moved into bounds (View.constrain), and written back,
    // so the menus' zoom go on from what is seen.
    readonly property var view: doc && doc.view ? (sized ? View.constrain(doc.view, size, docSize.width, docSize.height) : doc.view) : null
    // The canvas's document while it shows a file (PB_CANVAS=<path>; N1 gives Doc the real size), else Doc's.
    readonly property size docSize: canvas.docSize.width > 0 ? canvas.docSize : Qt.size(doc ? doc.w : 0, doc ? doc.h : 0)
    property var origins: ({})  // the rulers' 0 of each document by file (the corner dragged), document pixels
    readonly property var origin: (doc && origins[doc.file]) || ({ x: 0, y: 0 })
    readonly property var unitNames: Prefs.panes.units[0].options  // Thiết lập › Thước: Pixels … Percent
    // Brush and Eraser paint on a real document (C3b, N3: Tools.real): input lets the left button through to the canvas,
    // which takes it once its document is up; the mock's clicks (CanvasClicks) make no step then.
    readonly property bool paints: Tools.real && (Tools.tool === "brush" || Tools.tool === "eraser") && !Tools.xf
    readonly property int cursor: {
        if (!doc)
            return Qt.ArrowCursor
        if (input.mode === "pan")
            return Qt.ClosedHandCursor
        if (Tools.xf)
            return Qt.SizeAllCursor
        switch (Tools.tool) {
        case "hand": return Qt.OpenHandCursor
        case "zoom": return Qt.BlankCursor  // the magnifier below
        case "type": return Qt.IBeamCursor
        case "move": return Qt.ArrowCursor
        default: return Qt.CrossCursor  // painting tools too: U7 draws the brush and hides this
        }
    }

    implicitWidth: 1052
    implicitHeight: 768
    color: { const k = ["canvasStandard", "canvasFullMenu", "canvasFull"][Tools.screen]; return Theme.views[Prefs.value(k)] ?? Prefs.value(k + "Color") }  // Tùy chọn…: <key>Color
    onViewChanged: Qt.callLater(keep)
    function keep() { if (doc && view && view !== doc.view) doc.setView(view) }  // after the binding, or it would loop
    onSizeChanged: if (sized) Docs.viewport = { w: Math.round(size.w * dpr), h: Math.round(size.h * dpr) }  // what Vừa màn hình fits, device px
    Keys.onEscapePressed: e => {
        e.accepted = Tools.tool === "rot" && !!view && view.angle !== 0
        if (e.accepted)
            show(View.rotateTo(view, 0))
    }

    function show(v) { if (doc) doc.setView(sized ? View.constrain(v, size, docSize.width, docSize.height) : v) }
    // Document pixels in a unit of Thiết lập › Thước: px, in, cm, mm, pt, pica, % of the side.
    function perUnit(vertical) {
        const ppi = doc ? doc.ppi : 72
        return [1, ppi, ppi / 2.54, ppi / 25.4, ppi / 72, ppi / 6, (doc ? (vertical ? doc.h : doc.w) : 100) / 100][Prefs.value("rulerUnits")] ?? 1
    }
    // A frame point as the rulers read it, the view not turned; document pixels, rounded.
    function unturned(p) {
        const s = View.scale(view, size)
        return { x: Math.round(view.x + (p.x - size.w / 2) / s), y: Math.round(view.y + (p.y - size.h / 2) / s) }
    }
    function pixelAt(x, y) {  // the document pixel at a frame point: (i, j) covers [i, i + 1) × [j, j + 1)
        const d = View.toDoc(view, size, { x: x, y: y })
        return { x: Math.floor(d.x), y: Math.floor(d.y) }
    }
    // Tools.pointer, for Info (U12): the document pixel under the pointer while it is in the frame, outside the image too
    // as Photoshop's; null off it. Only the Viewport the pointer is in writes it, so a second one (U15's spaces) keeps out;
    // one going away takes its pixel along. (A Binding would not: it restores no JS object, nor anything when destroyed.)
    readonly property var pointer: input.containsMouse && view ? pixelAt(input.mouseX, input.mouseY) : null
    property bool pointing: false  // Tools.pointer is this one's
    onPointerChanged: if (pointer || pointing) { Tools.pointer = pointer; pointing = !!pointer }
    Component.onDestruction: if (pointing) Tools.pointer = null
    function setOrigin(o) {
        const all = Object.assign({}, origins)
        if (o)
            all[doc.file] = o
        else
            delete all[doc.file]
        origins = all
    }
    function zoomsOut(mods) { return (Tools.value("zoom:2", 0) === 1) !== !!(mods & Qt.AltModifier) }  // the Zoom tool: its bar, Alt the other way

    // ---- the pointer ------------------------------------------------------------------------------------------------
    function press(m) {
        vp.forceActiveFocus()
        if (vp.paints && m.button === Qt.LeftButton) {  // the canvas's, under this: BrushCursor's handlers on this take all buttons
            m.accepted = false
            return
        }
        if (m.button === Qt.RightButton)
            return menu.show(resolve(menuItems()), input, m.x, m.y)
        const t = m.button === Qt.MiddleButton ? "hand" : Tools.tool
        input.mode = t === "hand" ? "pan" : t === "rot" ? "rotate" : t === "zoom" ? "zoom" : ""
        input.start = { x: m.x, y: m.y, view: view, a: Math.atan2(m.y - size.h / 2, m.x - size.w / 2), out: zoomsOut(m.modifiers), t: Date.now() }
        input.moved = false
        input.held = false
        if (input.mode === "zoom" && Prefs.value("animatedZoom"))
            hold.start()
    }
    function move(m) {
        const st = input.start
        if (!st || !input.mode)
            return
        const dx = m.x - st.x, dy = m.y - st.y
        if (input.mode === "zoom" && !input.moved && Math.abs(dx) < 3 && Math.abs(dy) < 3)
            return
        input.moved = true
        if (input.mode === "pan")
            show(View.pan(st.view, size, dx, dy))
        else if (input.mode === "rotate") {
            let a = st.view.angle + (Math.atan2(m.y - size.h / 2, m.x - size.w / 2) - st.a) * 180 / Math.PI
            if (m.modifiers & Qt.ShiftModifier)
                a = Math.round(a / 15) * 15
            show(View.rotateTo(st.view, a))
        } else if (input.mode === "zoom" && !input.held && Tools.value("zoom:4", true)) {  // Scrubby Zoom: right in, left out
            show(View.zoomAt(st.view, size, st, st.view.zoom * Math.pow(2, dx / 150)))
        }
    }
    function release(m) {
        hold.stop()
        const st = input.start
        if (st && input.mode === "zoom" && !input.moved && !input.held)  // a click: a step, about it (Zoom vào chỗ nhấp) or the centre
            show(View.zoomAt(view, size, Prefs.value("zoomToClick") ? st : { x: size.w / 2, y: size.h / 2 }, View.step(view.zoom, zoomsOut(m.modifiers) ? -1 : 1)))
        input.mode = ""
        input.start = null
    }
    property real notches: 0  // the wheel's turn not zoomed yet, in 1/120 of a notch
    function wheel(w) {
        if (!!Prefs.value("wheelZoom") !== !!(w.modifiers & Qt.AltModifier)) {
            notches += w.angleDelta.y || w.angleDelta.x  // Alt turns some wheels sideways
            const n = Math.trunc(notches / 120)
            notches -= n * 120
            let z = view.zoom
            for (let i = 0; i < Math.abs(n); i++)
                z = View.step(z, n)
            if (n)
                show(View.zoomAt(view, size, { x: w.x, y: w.y }, z))
            return
        }
        const dx = w.pixelDelta.x || w.angleDelta.x / 2, dy = w.pixelDelta.y || w.angleDelta.y / 2  // a notch: 60 px
        const across = w.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)
        show(View.pan(view, size, across ? dy : dx, across ? dx : dy))
    }

    // ---- the right-click menu ---------------------------------------------------------------------------------------
    // The first item of MenuSpec.MENUS[m] with this act, dialog, space, keys or label, a menu's rows before its
    // submenus'; ">act": the item whose submenu has it.
    function item(m, key) {
        const sub = key[0] === ">", k = sub ? key.slice(1) : key
        const hit = x => sub ? Array.isArray(x.sub) && x.sub.some(y => y.act === k) : x.act === k || x.dialog === k || x.space === k || x.k === k || x.l === k
        for (let level = MenuSpec.MENUS[m].items; level.length; level = level.reduce((a, x) => Array.isArray(x.sub) ? a.concat(x.sub) : a, [])) {
            const it = level.find(hit)
            if (it)
                return it
        }
        return null
    }
    // As Photoshop's, by tool: a selection tool's from Select, Move's from Layer, Hand, Zoom and Rotate View's from View,
    // the rest Edit's; Free Transform's Transform.
    function menuItems() {
        const E = 1, L = 3, S = 5, V = 7
        const of = keys => keys.map(k => k ? item(k[0], k[1]) : MenuSpec.SEP).filter(it => !!it)
        if (Tools.xf)
            return item(E, ">@rot:180").sub.slice(2)
        switch (Tools.tool) {
        case "rect": case "ellip": case "lasso": case "plasso": case "wand": case "selbrush":
            return of([[S, "@deselect"], [S, "@selAll"], [S, "@reselect"], [S, "@inverse"], 0, [S, "SelectMask"], [S, "⇧ F6"], [S, "Transform Selection"], [S, "@saveSel"], 0,
                [L, "Ctrl J"], [L, "@cutLayer"], 0, [E, "@xf"], [E, "Fill"], [E, "Stroke"]])
        case "move":
            return of([[L, "@dupLayer"], [L, "@rename"], [L, ">@delLayer"], 0, [L, "@groupLayer"], [L, "@ungroup"], [L, "@hideLayer"], 0, [L, ">@arr:top"], 0,
                [L, "@merge:down"], [L, "@merge:visible"], [L, "@merge:flat"], 0, [E, "@xf"]])
        case "hand": case "zoom": case "rot":
            return of([[V, "@zoomIn"], [V, "@zoomOut"], [V, "@zoomFit"], [V, "@zoom100"]]).concat([{ l: qsTr("Lấp đầy màn hình"), act: "@zoomFill" }, MenuSpec.SEP,
                { l: qsTr("Đặt lại view"), disabled: !view.angle, act: () => show(View.rotateTo(view, 0)) }])
        default:
            return of([[E, "Ctrl X"], [E, "Ctrl C"], [E, "Ctrl V"], 0, [E, "Fill"], [E, "Stroke"], 0, [E, "@xf"], [E, ">@rot:180"]])
        }
    }
    function run(item) {  // a menu item picked: as the menu bar's (its own act a function, PopupMenu called it)
        const w = vp.Window.window as Main
        if (typeof item.act === "function")
            return
        if (w)
            w.act(item)
        else
            Docs.exec(item.act)
    }
    function resolve(items) {  // greyed as the menu bar's: when Main says it cannot run now
        const w = vp.Window.window as Main
        return items.map(it => it.t === "sep" ? it : Object.assign({}, it, {
            disabled: !!it.disabled || (typeof it.act !== "function" && !(w ? w.can(it) : Docs.can(it.req || it.act || ""))),
            sub: Array.isArray(it.sub) ? resolve(it.sub) : undefined
        }))
    }

    // ---- areas ------------------------------------------------------------------------------------------------------
    Rectangle {  // the rulers' corner: drag it to where 0 goes, double click for the top left again
        visible: vp.rulers
        width: 18
        height: 18
        color: Theme.p1
        Rectangle { x: 17; width: 1; height: 18; color: Theme.ln }
        Rectangle { y: 17; width: 18; height: 1; color: Theme.ln }
        MouseArea {
            id: corner
            anchors.fill: parent
            property bool dragging: false
            onPositionChanged: m => dragging = pressed && (m.x > 18 || m.y > 18)
            onReleased: m => {
                const p = { x: m.x - frame.x, y: m.y - frame.y }
                if (dragging && p.x >= 0 && p.y >= 0 && p.x <= frame.width && p.y <= frame.height)
                    vp.setOrigin(vp.unturned(p))
                dragging = false
            }
            onDoubleClicked: vp.setOrigin(null)
        }
    }
    Ruler {
        visible: vp.rulers
        x: frame.x
        width: frame.width
        height: 18
        view: vp.view
        size: vp.size
        origin: vp.origin.x
        perUnit: vp.perUnit(false)
        mark: input.containsMouse ? input.mouseX : corner.dragging ? corner.mouseX - frame.x : NaN
        units: vp.unitNames
        unit: Prefs.value("rulerUnits")
        onPicked: u => Prefs.set("rulerUnits", u)
    }
    Ruler {
        vertical: true
        visible: vp.rulers
        y: frame.y
        width: 18
        height: frame.height
        view: vp.view
        size: vp.size
        origin: vp.origin.y
        perUnit: vp.perUnit(true)
        mark: input.containsMouse ? input.mouseY : corner.dragging ? corner.mouseY - frame.y : NaN
        units: vp.unitNames
        unit: Prefs.value("rulerUnits")
        onPicked: u => Prefs.set("rulerUnits", u)
    }
    Item {
        id: frame
        x: vp.rulers ? 18 : 0
        y: vp.rulers ? 18 : 0
        width: vp.width - x
        height: vp.height - y
        clip: true
        MockCanvas {
            anchors.fill: parent
            visible: !Tools.real
            doc: vp.doc
            view: vp.view
            size: vp.size
        }
        DocCanvas {  // V1, V2b: the test card with PB_CANVAS=pattern, over the mock canvas; a real document in its place
            id: canvas
            anchors.fill: parent
            visible: (CanvasEnv.pattern || Tools.real) && !!vp.view
            view: vp.view ?? ({})
            source: CanvasEnv.document
            pasteboard: vp.color
            onDocSizeChanged: if (docSize.width > 0) vp.show({ x: docSize.width / 2, y: docSize.height / 2, zoom: View.fit(docSize.width, docSize.height, vp.size, 0), angle: 0 })
            // Brush or Eraser and their options (N3): the bar's size, hardness, Opacity, Flow, its pen buttons, Brush
            // Settings' Spacing, the foreground colour (Tools.brushOf())
            tool: vp.paints ? Tools.tool : ""
            brush: Tools.brushOf(Tools.tool)
            // The mock document's History has the step too, as CanvasClicks' click made it (Ctrl Z goes by it).
            onPainted: step => { if (vp.doc) vp.doc.run({ op: "step", name: step, icon: ToolSpec.tool(step === "Eraser" ? "eraser" : "brush").icn }) }
        }
        Connections {  // Edit › Undo, Redo (menus, keys) on the canvas's history too. ponytail: two histories, one in step, until N1
            target: canvas.docSize.width > 0 ? vp.Window.window as Main : null
            function onActed(item) {
                const n = MenuSpec.name(item.act)
                if (n === "undo")
                    canvas.undo()
                else if (n === "redo")
                    canvas.redo()
            }
        }
        MouseArea {
            id: input
            property string mode: ""  // what the press drags: "pan", "rotate", "zoom" (the Zoom tool)
            property var start: null  // { x, y, view, a: the pointer's angle about the centre, out, t }
            property bool moved: false  // the Zoom tool's press went past 3 px
            property bool held: false  // Zoom có chuyển động zoomed it
            anchors.fill: parent
            enabled: !!vp.doc
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            cursorShape: vp.cursor
            onPressed: m => vp.press(m)
            onPositionChanged: m => vp.move(m)
            onReleased: m => vp.release(m)
            onCanceled: {
                hold.stop()
                mode = ""
                start = null
            }
            onWheel: w => vp.wheel(w)
        }
        Overlays {
            anchors.fill: parent
            doc: vp.doc
            view: vp.view
            size: vp.size
        }
        Icon {  // the Zoom tool's pointer: Qt has no magnifier
            visible: input.containsMouse && Tools.tool === "zoom" && !Tools.xf && input.mode !== "pan"
            x: input.mouseX - 8
            y: input.mouseY - 8
            name: Tools.value("zoom:2", 0) === 1 ? "zoom_out" : "zoom_in"
            color: "#ffffff"
            style: Text.Outline
            styleColor: "#000000"
        }
        Rectangle {  // where 0 goes while the corner is dragged
            visible: corner.dragging
            x: corner.mouseX - frame.x
            width: 1
            height: frame.height
            color: "#ffffff"
        }
        Rectangle {
            visible: corner.dragging
            y: corner.mouseY - frame.y
            width: frame.width
            height: 1
            color: "#ffffff"
        }
    }
    Timer {  // Zoom có chuyển động: the Zoom tool held still zooms on after 300 ms
        id: hold
        interval: 16
        repeat: true
        onTriggered: {
            const st = input.start
            if (!st || input.moved || Date.now() - st.t < 300)
                return
            input.held = true
            vp.show(View.zoomAt(vp.view, vp.size, st, vp.view.zoom * (st.out ? 1 / 1.03 : 1.03)))
        }
    }
    PopupMenu {
        id: menu
        width: 270  // the Layer menu's
        modal: true  // the press that closes it ends there, not on the canvas
        dim: false
        onActivated: item => vp.run(item)
    }
}
