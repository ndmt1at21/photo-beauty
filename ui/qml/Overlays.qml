pragma ComponentBehavior: Bound
import QtQuick
import "View.js" as View
import "Mock.js" as Mock
import "Presets.js" as Presets
import "ToolSpec.js" as ToolSpec

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
        anchors.fill: parent
        visible: ov.has && ov.tool === "crop" && !Tools.xf
        view: ov.view
        size: ov.size
        docW: ov.docW
        docH: ov.docH
        box: visible ? Tools.cropBox(ov.docW, ov.docH) : null
        pointerArea: ov.pointerArea
        ratio: (String(Tools.value("crop:2", "4:5 (8:10)")).match(/^\d+:\d+/) || [""])[0]
    }
    TransformBox {
        anchors.fill: parent
        visible: ov.has && Tools.xf
        view: ov.view
        size: ov.size
        docW: ov.docW
        docH: ov.docH
        warp: Tools.warp
        scaleX: Tools.value("xf:7", 100) / 100
        scaleY: Tools.value("xf:9", 100) / 100
        angle: Tools.value("xf:11", 0)
        pivot: Tools.value("xf:2", 4)
        cells: parseInt(Tools.value("warp:3", "3 × 3")) || 3
        pointerArea: ov.pointerArea
    }
    ShapeHandles {
        // A shape layer picked with a shape tool: its size from the bar of its shape, centred (the mock keeps no place).
        readonly property string bar: ov.active ? ({ rectS: "rectS", ellipS: "ellipS", tri: "rectS", poly: "polygon", line: "line" })[ov.active.props.shape] ?? "rectS" : "rectS"
        readonly property var dims: ({ rectS: [1200, 800], ellipS: [1200, 800], polygon: [600, 570], line: [900, 0] })[bar]
        readonly property real bw: Tools.value(bar + ":10", dims[0])
        readonly property real bh: Tools.value(bar + (bar === "line" ? ":11" : ":12"), dims[1])
        anchors.fill: parent
        visible: ov.has && !!ov.active && ov.active.kind === "shape" && !!Mock.SHAPE_TOOLS[ov.tool]
        view: ov.view
        size: ov.size
        box: visible ? { x: (ov.docW - bw) / 2, y: (ov.docH - bh) / 2, w: bw, h: bh } : null
        radius: bar === "rectS" && ov.active && ov.active.props.shape === "rectS" ? Tools.value("rectS:17", 144) : -1
        ellipse: !!ov.active && ov.active.props.shape === "ellipS"
        onRadiusDragged: r => Tools.setValue("rectS:17", r)
    }
    TypeBox {
        // A text layer picked with the Type tool: one line of the bar's size, centred (the mock keeps no place).
        readonly property real em: Tools.value("type:4", 48) * (ov.doc ? ov.doc.ppi : 72) / 72  // pt to document px
        readonly property real w: Math.min(ov.docW - 2 * em, 9.2 * em)
        anchors.fill: parent
        visible: ov.has && !!ov.active && ov.active.kind === "text" && ov.tool === "type"
        view: ov.view
        size: ov.size
        box: visible ? { x: (ov.docW - w) / 2, y: (ov.docH - 2 * em) / 2, w: w, h: 2 * em } : null
        text: ov.active ? ov.active.props.name : ""
        fontSize: em
        color: Tools.value("type:10", "#f4ede4")
    }
    GradientLine {
        anchors.fill: parent
        visible: !!ov.gradientLayer
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
        enabled: ov.has && (ov.tool === "clone" || ov.tool === "heal")
        acceptedModifiers: Qt.AltModifier
        onTapped: ep => {
            ov.cloneSource = View.toDoc(ov.view, ov.size, ep.position)
            ov.cloneOffset = null
        }
    }
    PointHandler {  // the first stroke after it: the offset
        enabled: ov.has && (ov.tool === "clone" || ov.tool === "heal") && !!ov.cloneSource && !ov.cloneOffset
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
        enabled: ov.has && ov.tool === "eyedrop"
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active)
                was = Tools.fg
            else
                Tools.setFg("#d9a68e")
        }
    }
    BrushCursor {
        id: cursor
        anchors.fill: parent
        visible: ov.brushBar !== ""
        view: ov.view
        size: ov.size
        diameter: Tools.value(ov.brushBar + ":size", ov.brushDef.size)
        hardness: Tools.value(ov.brushBar + ":hard", ov.brushDef.hard)
        roundness: ov.tipOf[4]
        tipAngle: ov.tipOf[3]
        square: ov.tipOf[2] !== "50%"
        tip: ov.picking ? 0 : Prefs.value("paintCursor") ?? 3  // the Eyedropper for a moment: its own pointer
        crosshair: Prefs.value("crosshair") ?? true
        crosshairWhilePainting: !!Prefs.value("crosshairWhilePainting")
        previewColor: Prefs.value("brushPreviewColor") ?? "#ff0000"
        pointerArea: ov.pointerArea
        onAdjusted: (d, h) => Tools.setValues({ [ov.brushBar + ":size"]: d, [ov.brushBar + ":hard"]: h })
    }
    CanvasBar {
        // ponytail: only the faint selection's notice and no progress yet: the mock opens no PSD it cannot show and runs
        // no filter in the background (N's tasks set notice, task, progress).
        property var letGo: null  // the selection whose notice was let go
        anchors.fill: parent
        notice: ants.faint && ants.ok && letGo !== ov.doc.selection ? qsTr("Không có điểm ảnh nào được chọn quá 50%, nên viền vùng chọn không hiện.") : ""
        onDismissed: letGo = ov.doc.selection
    }
}
