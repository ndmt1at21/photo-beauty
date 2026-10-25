pragma ComponentBehavior: Bound
import QtQuick
import "View.js" as View
import "Mock.js" as Mock
import "Presets.js" as Presets
import "ToolSpec.js" as ToolSpec
import "Fmt.js" as Fmt

// What is drawn over the canvas (docs/ui-tasks.md, U7; artboard Overlays, the prototype's ov and cur): Viewport lays it
// over the canvas, the frame's size, above its own pointer handling, and gives it what the canvas shows; each overlay
// maps document points with View.js. Here they are put together as Tools, the document and Thiết lập have it, in the
// prototype's order: grid and guides, Retouch's masks, the selection (Quick Mask), Crop, Free Transform, a shape's,
// a text's, a gradient's handles, the clone source, the sampling ring, the brush, the notice bar. What takes a press
// (handles, the text) takes it first; the rest falls through to Viewport.
Item {
    id: ov
    property Doc doc: null  // the document shown, null without one
    property var view: null  // Viewport.view: { x, y, zoom, angle }
    property var size: null  // Viewport.size: { w, h, dpr }

    readonly property bool has: !!doc && !!view && !!size
    readonly property real docW: doc ? doc.w : 0
    readonly property real docH: doc ? doc.h : 0
    readonly property var active: doc ? doc.active : null  // the active layer
    readonly property string tool: Tools.tool
    // Viewport's MouseArea under this: the hover handlers go on it, as one above it would take its hover (the rulers'
    // mark of the pointer).
    readonly property Item pointerArea: {
        for (const c of parent ? parent.children : [])
            if (c instanceof MouseArea && c.hoverEnabled)
                return c
        return null
    }

    // The painting tool whose brush the pointer shows: the tool in use, or Brush, Pencil while Alt held makes them the
    // Eyedropper for a moment (U5's Toolbar): Alt and the right button still resize their brush, as Photoshop's.
    property string lastPaint: ToolSpec.PAINT_T[Tools.tool] ? Tools.tool : ""
    onToolChanged: if (ToolSpec.PAINT_T[tool]) lastPaint = tool
    readonly property bool picking: tool === "eyedrop" && !!ToolSpec.ALT_PICK[lastPaint]
    readonly property string brushBar: has && !Tools.xf && (ToolSpec.PAINT_T[tool] || picking) ? ToolSpec.brushBar(picking ? lastPaint : tool, false) : ""
    readonly property var brushDef: ToolSpec.brush(brushBar || "brush")  // { size, hard, spacing, tip } before the bar sets them
    readonly property var tipOf: Presets.TIPS[Tools.value(brushBar + ":tip", 0)] ?? Presets.TIPS[0]  // [name, hardness, corner, °, roundness]

    // Clone Stamp, Healing Brush: the source Alt set, then the offset the first stroke after it fixed (document px).
    property var cloneSource: null
    property var cloneOffset: null
    onDocChanged: {
        cloneSource = null
        cloneOffset = null
        gradientStops = null
        dropCrop(true)
    }
    // A gradient fill layer's stops as dragged on the canvas, until another layer is picked (ponytail: the mock keeps
    // them nowhere else).
    property var gradientStops: null
    readonly property var gradientLayer: has && tool === "gradient" && active && active.kind === "fill" && /gradient\(/.test(String(active.props.color)) ? active : null
    readonly property int activeId: active ? active.id : -1
    onActiveIdChanged: gradientStops = null
    // The stops of a CSS gradient as the presets have them: colours spread evenly.
    function stopsOf(css) {
        const parts = String(css).replace(/^[a-z-]+\(|\)$/g, "").split(/,\s*(?![^(]*\))/).filter(x => !/deg$|^to /.test(x))
        return parts.map((c, i) => ({ pos: parts.length > 1 ? i / (parts.length - 1) : 0, color: c.trim() }))
    }
    function num(key, def) {  // a number box of the bar as a number: def when empty
        const v = Tools.value(key, def), n = typeof v === "number" ? v : Fmt.pnum(String(v))
        return isFinite(n) ? n : def
    }

    // ---- Free Transform (U34): what TransformBox draws and its drags make, in the xf bar's boxes -------------------
    // X, Y (xf:3, xf:5: where the reference point is, document px; with Relative, xf:4, from where it was as that went
    // on), W, H (xf:7, xf:9 %), the angle (xf:11 °), H, V skew (xf:13, xf:14 °), the reference point (xf:2, 0–8; −1 off
    // them: xf:ref, fractions of the box), the link (xf:8: corners keep proportions, Shift the other way); and
    // xf:corners (Distort, Perspective: the box's corners before the rest, box px), warp:mesh ({ cells, pts }: Warp's
    // net, box px). Each Free Transform starts from the document's box as it is; a reference point picked in the bar goes
    // there on the box, W or H typed with the link on takes the other along.
    property var xfOrigin: ({ x: 0, y: 0 })  // Relative's 0
    property var xfLast: null  // the boxes as last written or seen: { pivot, rel, W, H, t (TransformBox's transform) }
    property bool xfWriting: false
    property string xfTool: ""  // the tool Free Transform began with
    readonly property var xfAt: xfPos()  // where the reference point is, document px (NaN: its place on the box)
    function xfPos() {
        const x = num("xf:3", NaN), y = num("xf:5", NaN)
        return Tools.value("xf:4", false) ? { x: xfOrigin.x + x, y: xfOrigin.y + y } : { x: x, y: y }
    }
    function xfNow() {  // TransformBox's transform as the boxes have it
        const P = xfPos()
        return { scaleX: num("xf:7", 100) / 100, scaleY: num("xf:9", 100) / 100, angle: num("xf:11", 0), skewX: num("xf:13", 0), skewY: num("xf:14", 0),
            pivot: Tools.value("xf:2", 4), ref: Tools.value("xf:ref", null), refX: P.x, refY: P.y, corners: Tools.value("xf:corners", null) }
    }
    function xfWrite(o) {
        xfWriting = true
        if (Object.keys(o).length)
            Tools.setValues(o)
        xfWriting = false
        xfLast = { pivot: Tools.value("xf:2", 4), rel: !!Tools.value("xf:4", false), W: num("xf:7", 100), H: num("xf:9", 100), t: xfNow() }
    }
    function startXf() {
        if (!has)
            return
        xfTool = Tools.tool
        const k = Tools.value("xf:2", 4), pv = k >= 0 && k <= 8 ? k : 4, rel = !!Tools.value("xf:4", false)
        xfOrigin = { x: pv % 3 / 2 * docW, y: Math.floor(pv / 3) / 2 * docH }  // the box is the document's (the mock has no layer bounds)
        const fresh = { 2: pv, 3: rel ? 0 : xfOrigin.x, 5: rel ? 0 : xfOrigin.y, 7: 100, 8: Prefs.value("transformProportional") !== false, 9: 100, 11: 0, 13: 0, 14: 0 }, o = {}
        for (const i in fresh)  // only what is not so already: the bar's own defaults stand for the portrait as it opens
            if (Tools.value("xf:" + i, ToolSpec.def("xf", +i)) !== fresh[i])
                o["xf:" + i] = fresh[i]
        for (const k of ["xf:ref", "xf:corners", "warp:mesh"])
            if (Tools.value(k, null) !== null)
                o[k] = null
        xfWrite(o)
    }
    function writeXf(t) {  // TransformBox's drag
        const rel = !!Tools.value("xf:4", false)
        xfWrite({ "xf:2": t.pivot, "xf:ref": t.ref, "xf:3": rel ? t.refX - xfOrigin.x : t.refX, "xf:5": rel ? t.refY - xfOrigin.y : t.refY, "xf:7": t.scaleX * 100,
            "xf:9": t.scaleY * 100, "xf:11": t.angle, "xf:13": t.skewX, "xf:14": t.skewY, "xf:corners": t.corners })
    }
    function syncXf() {  // the bar's boxes changed: what follows from them
        const was = xfLast, k = Tools.value("xf:2", 4), rel = !!Tools.value("xf:4", false), W = num("xf:7", 100), H = num("xf:9", 100), o = {}
        if (k !== was.pivot && k >= 0 && k <= 8) {
            const t = xform.withPivot(was.t, k)
            Object.assign(o, { "xf:ref": null, "xf:3": rel ? t.refX - xfOrigin.x : t.refX, "xf:5": rel ? t.refY - xfOrigin.y : t.refY })
        } else if (rel !== was.rel) {
            if (rel)
                xfOrigin = { x: was.t.refX, y: was.t.refY }
            Object.assign(o, { "xf:3": rel ? 0 : was.t.refX, "xf:5": rel ? 0 : was.t.refY })
        }
        if (Tools.value("xf:8", true) && W !== was.W && H === was.H && was.W)
            o["xf:9"] = H * W / was.W
        else if (Tools.value("xf:8", true) && H !== was.H && W === was.W && was.H)
            o["xf:7"] = W * H / was.H
        xfWrite(o)
    }
    Connections {
        target: Tools
        function onXfChanged() {
            if (Tools.xf)
                ov.startXf()
            else
                ov.xfLast = null
        }
        function onValuesChanged() {
            if (Tools.xf && ov.xfLast && !ov.xfWriting)
                ov.syncXf()
        }
    }

    // ---- Crop, Perspective Crop (U34): CropBox's drags in crop:box, crop:angle (Tools.cropBox() gives the box), and
    // pcrop:quad; dropped when the ratio or the document (its size: Crop applied, undone) changes, Esc, Enter.
    // The ratio a crop's drag keeps, width over height (0: none): the bar's n:m, Original Ratio, or its W and H.
    readonly property real cropAspect: {
        const r = String(Tools.value("crop:2", "4:5 (8:10)")), m = r.match(/^(\d+):(\d+)/)
        if (m)
            return +m[1] / +m[2]
        if (r === "Original Ratio")
            return docH > 0 ? docW / docH : 0
        const w = num("crop:3", 4), h = num("crop:5", 5)
        return w > 0 && h > 0 ? w / h : 0
    }
    readonly property string cropKey: [Tools.value("crop:2", ""), Tools.value("crop:3", ""), Tools.value("crop:5", "")].join("|")
    onCropKeyChanged: dropCrop(false)
    onDocWChanged: dropCrop(true)
    onDocHChanged: dropCrop(true)
    function dropCrop(quadToo) {
        const o = {}
        if (Tools.value("crop:box", null) || Tools.value("crop:angle", null) !== null)
            Object.assign(o, { "crop:box": null, "crop:angle": null })
        if (quadToo && Tools.value("pcrop:quad", null))
            o["pcrop:quad"] = null
        if (Object.keys(o).length)
            Tools.setValues(o)
    }
    readonly property bool cropping: has && !Tools.xf && (tool === "crop" || tool === "pcrop" && !!Tools.value("pcrop:quad", null))

    // ---- keys: Enter applies, Esc cancels, the arrows move the box by 1 px (Shift 10), as Photoshop's: the window's
    // own (as Toolbar's held keys), the arrows taken before Main's keys have them (the Move tool's nudge, U31).
    readonly property Item keyRoot: visible && (Tools.xf && has || cropping) ? Window.contentItem : null
    function keysFree() {
        const f = Window.activeFocusItem, w = Window.window as Main
        return !(f instanceof TextInput) && !(f instanceof TextEdit) && !(w && w.dialogs.length)
    }
    function arrow(e) { return [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].indexOf(e.key) >= 0 && !(e.modifiers & (Qt.ControlModifier | Qt.AltModifier)) }
    function boxKey(e) {
        if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Escape) {
            if (e.isAutoRepeat)
                return true
            const ok = e.key !== Qt.Key_Escape
            if (Tools.xf) {
                Docs.exec(ok ? "@xfApply" : "@xfCancel")
            } else if (tool === "crop") {
                if (ok)
                    Docs.exec("@cropApply")
                dropCrop(false)
            } else {
                Docs.exec(ok ? "@pcropApply" : "@pcropCancel")  // as the bar's ✓, ✗
            }
            return true
        }
        if (!arrow(e))
            return false
        const n = e.modifiers & Qt.ShiftModifier ? 10 : 1, d = { [Qt.Key_Left]: [-n, 0], [Qt.Key_Right]: [n, 0], [Qt.Key_Up]: [0, -n], [Qt.Key_Down]: [0, n] }[e.key]
        if (Tools.xf)
            xform.nudge(d[0], d[1])
        else
            cropper.nudge(d[0], d[1])
        return true
    }
    // An Enter or Esc a text field had first (it takes them: a box of the bar lets the focus go at Enter, the key going on).
    property bool keyInField: false
    Connections {
        target: ov.keyRoot ? ov.keyRoot.Keys : null
        function onShortcutOverride(e) {
            if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Escape)
                ov.keyInField = !ov.keysFree()
            else if (ov.arrow(e) && ov.keysFree())
                e.accepted = true
        }
        function onPressed(e) {
            const inField = ov.keyInField
            ov.keyInField = false
            if (!inField && ov.keysFree() && ov.boxKey(e))
                e.accepted = true
        }
    }

    GuidesGrid {
        anchors.fill: parent
        view: ov.view
        size: ov.size
        docW: ov.docW
        docH: ov.docH
        grid: Tools.grid && Tools.extras
        guides: Tools.guides && Tools.extras
        pixelGrid: Tools.pixGrid && Tools.extras && !!Prefs.value("pixelGrid")
        spacing: [25, 50, 100, 200][Prefs.value("gridSpacing")] ?? 100
        subdivisions: [1, 2, 4, 8][Prefs.value("gridSubdivisions")] ?? 4
        gridColor: Prefs.value("gridColor") ?? "#8a8a8a"
        gridStyle: Prefs.value("gridStyle") ?? 0
        guideColor: Prefs.value("guideColor") ?? "#29d3f5"
        guideStyle: Prefs.value("guideStyle") ?? 0
        guidesX: ov.doc ? [Math.round(ov.docW / 3)] : []  // the prototype's guides
        guidesY: ov.doc ? [Math.round(ov.docH * 0.4)] : []
    }
    FaceOverlay {
        anchors.fill: parent
        view: ov.view
        size: ov.size
        docW: ov.docW
        docH: ov.docH
        active: ov.has && ov.doc.faces && Tools.panelVisible("Retouch")
    }
    Ants {
        id: ants
        anchors.fill: parent
        view: ov.view
        size: ov.size
        docW: ov.docW
        docH: ov.docH
        shape: ov.doc ? ov.doc.selection : null
        inverse: !!ov.doc && ov.doc.inverse
        ants: Tools.extras && Tools.selEdges && !Tools.quickMask
        mask: Tools.quickMask ? Qt.rgba(1, 0, 0, 0.5) : "transparent"
        faint: false  // ponytail: the mock never feathers a selection; N's selection says when no pixel is over 50 %
    }
    CanvasBar.Hud {  // Quick Mask on
        x: 12
        y: 12
        visible: ov.has && Tools.quickMask
        text: qsTr("Quick Mask · Q để thoát")
    }
    CropBox {
        id: cropper
        readonly property bool persp: ov.tool === "pcrop"
        anchors.fill: parent
        visible: ov.has && (ov.tool === "crop" || persp) && !Tools.xf
        view: ov.view
        size: ov.size
        docW: ov.docW
        docH: ov.docH
        perspective: persp
        box: visible && !persp ? Tools.cropBox(ov.docW, ov.docH) : null
        angle: visible && !persp ? ov.num("crop:angle", 0) : 0
        quad: visible && persp ? Tools.value("pcrop:quad", null) : null
        grid: !!Tools.value("pcrop:9", true)
        aspect: ov.cropAspect
        interactive: !Tools.real
        pointerArea: ov.pointerArea
        ratio: (String(Tools.value("crop:2", "4:5 (8:10)")).match(/^\d+:\d+/) || [""])[0]
        onBoxEdited: (b, a) => Tools.setValues({ "crop:box": b, "crop:angle": a })
        onQuadEdited: q => Tools.setValue("pcrop:quad", q)
    }
    TransformBox {
        id: xform
        anchors.fill: parent
        visible: ov.has && Tools.xf
        view: ov.view
        size: ov.size
        docW: ov.docW
        docH: ov.docH
        warp: Tools.warp
        scaleX: ov.num("xf:7", 100) / 100
        scaleY: ov.num("xf:9", 100) / 100
        angle: ov.num("xf:11", 0)
        skewX: ov.num("xf:13", 0)
        skewY: ov.num("xf:14", 0)
        pivot: Tools.value("xf:2", 4)
        ref: Tools.value("xf:ref", null)
        refX: ov.xfAt.x
        refY: ov.xfAt.y
        corners: Tools.value("xf:corners", null)
        cells: parseInt(Tools.value("warp:3", "3 × 3")) || 3
        mesh: { const m = Tools.value("warp:mesh", null); return m && m.cells === cells ? m.pts : null }
        proportional: !!Tools.value("xf:8", true)
        // Space held: Viewport pans (Free Transform begun with the Hand tool keeps the drags); a real document's is N49's.
        interactive: !Tools.real && (Tools.tool !== "hand" || ov.xfTool === "hand")
        pointerArea: ov.pointerArea
        onTransformed: t => ov.writeXf(t)
        onWarped: m => Tools.setValue("warp:mesh", { cells: cells, pts: m })
    }
    ShapeHandles {
        // A shape layer picked with a shape tool: its size from the bar of its shape, centred (the mock keeps no place).
        readonly property string bar: ov.active ? ({ rectS: "rectS", ellipS: "ellipS", tri: "rectS", poly: "polygon", line: "line" })[ov.active.props.shape] ?? "rectS" : "rectS"
        readonly property var dims: ({ rectS: [1200, 800], ellipS: [1200, 800], polygon: [600, 570], line: [900, 0] })[bar]
        readonly property real bw: Tools.value(bar + ":10", dims[0])
        readonly property real bh: Tools.value(bar + (bar === "line" ? ":11" : ":12"), dims[1])
        anchors.fill: parent
        visible: ov.has && !!ov.active && ov.active.kind === "shape" && !!Mock.SHAPE_TOOLS[ov.tool] && !Tools.xf
        view: ov.view
        size: ov.size
        box: visible ? { x: (ov.docW - bw) / 2, y: (ov.docH - bh) / 2, w: bw, h: bh } : null
        radius: bar === "rectS" && ov.active && ov.active.props.shape === "rectS" ? Tools.value("rectS:17", 144) : -1
        ellipse: !!ov.active && ov.active.props.shape === "ellipS"
        onRadiusDragged: r => Tools.setValue("rectS:17", r)
    }
    TypeBox {
        // A text layer picked with the Type tool: one line of the bar's size, centred (the mock keeps no place).
        readonly property real em: Tools.value("char:size", 48) * (ov.doc ? ov.doc.ppi : 72) / 72  // Character's (the Type bar's) pt to document px
        readonly property real w: Math.min(ov.docW - 2 * em, 9.2 * em)
        anchors.fill: parent
        visible: ov.has && !!ov.active && ov.active.kind === "text" && ov.tool === "type" && !Tools.xf
        view: ov.view
        size: ov.size
        box: visible ? { x: (ov.docW - w) / 2, y: (ov.docH - 2 * em) / 2, w: w, h: 2 * em } : null
        text: ov.active ? ov.active.props.name : ""
        fontSize: em
        color: Tools.value("char:color", "#f4ede4")
    }
    GradientLine {
        anchors.fill: parent
        visible: !!ov.gradientLayer && !Tools.xf
        view: ov.view
        size: ov.size
        from: visible ? { x: 0.13 * ov.docW, y: 0.83 * ov.docH } : null  // as the artboard draws it: the mock keeps no line
        to: visible ? { x: 0.87 * ov.docW, y: 0.2 * ov.docH } : null
        stops: ov.gradientStops || (ov.gradientLayer ? ov.stopsOf(ov.gradientLayer.props.color) : [])
        onStopMoved: (i, pos) => ov.gradientStops = stops.map((st, k) => k === i ? { pos: pos, color: st.color } : st)
        onStopRemoved: i => {
            ov.gradientStops = stops.filter((st, k) => k !== i)
            current = -1
        }
        onStopPicked: i => {
            const w = ov.Window.window as Main
            if (w)
                w.openDialog("ColorPicker", { color: stops[i].color === "transparent" ? "#000000" : stops[i].color, title: qsTr("điểm màu"),
                    picked: c => ov.gradientStops = stops.map((st, k) => k === i ? { pos: st.pos, color: String(c) } : st) })
        }
    }
    CloneSource {
        anchors.fill: parent
        visible: ov.has && (ov.tool === "clone" || ov.tool === "heal") && !Tools.xf
        view: ov.view
        size: ov.size
        docW: ov.docW
        docH: ov.docH
        source: ov.cloneSource
        offset: ov.cloneOffset
        point: cursor.point
        diameter: cursor.diameter
        photo: ov.doc ? Mock.photoUrl(ov.doc.photo) : ""
        photoSize: ov.doc && Mock.PHOTOS[ov.doc.photo] ? Qt.size(Mock.PHOTOS[ov.doc.photo].w, Mock.PHOTOS[ov.doc.photo].h) : Qt.size(0, 0)
        hint: qsTr("Alt nhấp để đặt nguồn") + (Tools.value(ov.tool === "clone" ? "clone:13" : "heal:8", true) ? " · Aligned" : "")
    }
    TapHandler {  // Alt click: the source
        enabled: ov.has && (ov.tool === "clone" || ov.tool === "heal") && !Tools.xf
        acceptedModifiers: Qt.AltModifier
        onTapped: ep => {
            ov.cloneSource = View.toDoc(ov.view, ov.size, ep.position)
            ov.cloneOffset = null
        }
    }
    PointHandler {  // the first stroke after it: the offset
        enabled: ov.has && (ov.tool === "clone" || ov.tool === "heal") && !!ov.cloneSource && !ov.cloneOffset && !Tools.xf
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: Qt.NoModifier
        onActiveChanged: {
            if (active) {
                const p = View.toDoc(ov.view, ov.size, point.position)
                ov.cloneOffset = { x: ov.cloneSource.x - p.x, y: ov.cloneSource.y - p.y }
            }
        }
    }
    SampleRing {
        anchors.fill: parent
        visible: ov.has && ov.tool === "eyedrop" && pick.active && Tools.value("eyedrop:6", true)
        point: pick.active ? pick.point.position : null
        newColor: "#d9a68e"  // the prototype's: the mock samples no pixel
        oldColor: pick.was
    }
    PointHandler {  // the Eyedropper held down: the ring; let go: the colour
        id: pick
        property color was: "#ffffff"
        enabled: ov.has && ov.tool === "eyedrop" && !Tools.xf  // not Alt's Eyedropper for a moment in Free Transform
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active)
                was = Tools.fg
            else
                Tools.setFg("#d9a68e")
        }
    }
    CanvasClicks { doc: ov.doc; pointerArea: ov.pointerArea as MouseArea; enabled: ov.visible }  // the other tools' clicks (U7b)
    BrushCursor {
        id: cursor
        anchors.fill: parent
        visible: ov.brushBar !== ""
        view: ov.view
        size: ov.size
        diameter: Tools.value(ov.brushBar + ":size", ov.brushDef.size)
        hardness: Tools.value(ov.brushBar + ":hard", ov.brushDef.hard)
        // Brush Settings' Roundness, Angle (the bar's too; anticlockwise, the tip's own not set), mirrored by one flip; on a
        // real document the core's tip, round (N3)
        roundness: Tools.real ? 1 : Math.round(Tools.value(ov.brushBar + ":roundness", ov.tipOf[4] * 100)) / 100  // whole %, as its field
        tipAngle: Tools.real ? 0 : (Tools.value(ov.brushBar + ":flipX", false) !== Tools.value(ov.brushBar + ":flipY", false) ? 1 : -1) * Tools.value(ov.brushBar + ":angle", -ov.tipOf[3])
        square: !Tools.real && ov.tipOf[2] !== "50%"
        tip: ov.picking ? 0 : Prefs.value("paintCursor") ?? 3  // the Eyedropper for a moment: its own pointer
        crosshair: Prefs.value("crosshair") ?? true
        crosshairWhilePainting: !!Prefs.value("crosshairWhilePainting")
        previewColor: Prefs.value("brushPreviewColor") ?? "#ff0000"
        pointerArea: ov.pointerArea
        onAdjusted: (d, h) => Tools.setValues({ [ov.brushBar + ":size"]: d, [ov.brushBar + ":hard"]: h })
    }
    // The document's notice, what it runs in the background and its share done (Doc.notice, task, progress: a file
    // being read, P1's tasks), else the faint selection's notice.
    CanvasBar {
        property var letGo: null  // the selection whose notice was let go
        anchors.fill: parent
        notice: ov.doc && ov.doc.notice ? ov.doc.notice
            : ants.faint && ants.ok && letGo !== ov.doc.selection ? qsTr("Không có điểm ảnh nào được chọn quá 50%, nên viền vùng chọn không hiện.") : ""
        task: ov.doc ? ov.doc.task : ""
        progress: ov.doc ? ov.doc.progress : -1
        onDismissed: {
            if (ov.doc && ov.doc.notice)
                ov.doc.dismissNotice()
            else
                letGo = ov.doc.selection
        }
        onCanceled: Docs.cancelTask(ov.doc)
    }
}
