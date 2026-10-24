pragma Singleton
import QtQuick
import "Presets.js" as Presets
import "ToolSpec.js" as ToolSpec

// Tools, the options bar's values, colours, Quick Mask, screen mode, the switches of View and Window, the dock and
// Retouch (docs/ui-tasks.md, U3), as the prototype's state (build/proto/Main.dc.html). Objects are replaced, never
// edited in place, so bindings on them follow.
QtObject {
    id: tools

    // The tool, and the one shown first in each toolbar group (GROUPS' first).
    property string tool: "brush"
    property var top: ({})
    // The document in front is a real one, the core's (decision 4 of docs/ui-tasks.md, N3): the canvas shows it, Brush
    // and Eraser paint on it, and what the core has nothing for yet is dimmed and locked: the tools but coreTools', the
    // options no BrushParams takes (OptionsBar, Brush Settings). The one place that says so: a document that says it is
    // (N1's DocModel has real: true; the mock Doc no such property), or every one while PB_CANVAS names a file (each
    // Viewport shows it). Tests set it; reset() binds it again. (isReal() takes the document untyped: the mock Doc type
    // has no real for qmllint to find.)
    property bool real: isReal(Docs.current)
    function isReal(doc) { return doc?.real === true || CanvasEnv.document !== "" }
    // The tools a real document has; N tasks add theirs as the core gets them.
    readonly property var coreTools: ({ brush: 1, eraser: 1, hand: 1, rot: 1, zoom: 1 })
    function usable(id) { return !real || !!coreTools[id] }
    // Its brush tips, of Presets.TIPS: the round ones (Tròn mềm, cứng, vừa), as the core's dabs go by hardness alone.
    readonly property var coreTips: [0, 1, 2]
    function tipUsable(i) { return !real || coreTips.indexOf(i) >= 0 }
    // Real now: a tool locked gives way to Brush, a tip the core lacks to the round one nearest its hardness.
    onRealChanged: {
        if (!usable(tool))
            setTool("brush", "brush")
        const o = {}
        for (const k in values)
            if (/:tip$/.test(k) && values[k] !== null && !tipUsable(values[k])) {
                const bar = k.slice(0, -4), h = value(bar + ":hard", ToolSpec.brush(bar).hard)
                Object.assign(o, { [k]: h < 25 ? 0 : h > 75 ? 1 : 2, [bar + ":angle"]: null, [bar + ":roundness"]: null })
            }
        if (Object.keys(o).length)
            setValues(o)
    }
    // Free Transform (Edit › Free Transform, Ctrl T) is on, as Warp.
    property bool xf: false
    property bool warp: false
    // Each box of the options bars, by "<bar>:<index of the item in its BARS row>" (the prototype's v), and the brush of
    // each painting bar: "<bar>:size" (px), ":hard" (%), ":spacing" (%), ":tip" (an index of Presets.TIPS). A key not set
    // has the bar's own default (BARS, U5).
    property var values: ({})

    // Colours
    property string fg: "#ffffff"
    property real fgHue: 0  // the hue kept while the colour is grey, for the Color panel's H
    property string bg: "#000000"
    property string colorMode: "HSB"  // the Color panel's sliders: HSB, RGB, Lab
    property var recentSwatches: []  // 10, newest first
    property var gradient: [0, 0]  // [group, index] of Presets.GR_GROUPS
    property var recentGradients: []  // 5, as gradient
    property bool quickMask: false

    property int screen: 0  // 0 chuẩn, 1 toàn màn hình có menu, 2 toàn màn hình (F)
    // View
    property bool rulers: true
    property bool extras: true
    property bool pixGrid: true
    property bool grid: false
    property bool guides: false
    property bool snap: true
    property bool lockGuides: false
    property bool edges: false
    property bool selEdges: true
    property bool brushPrev: true
    property var snapTo: ({})  // { guides, grid, layers, bounds: bool }
    // The document pixel under the pointer on the canvas, { x, y } (Viewport sets it, U6; Info reads it, U12), outside
    // the image too as Photoshop's Info; null while the pointer is off the canvas frame.
    property var pointer: null
    // Window
    property bool showOpts: true
    property bool showTools: true
    property var dockTab: ({})  // the tab shown in each dock group: { c: "Color", p: "Properties", l: "Layers" }
    property var dockMin: ({})  // groups folded to their tabs: { c, p, l: bool }
    property bool dockCollapsed: false  // the dock as a column of icons
    property string dockOpen: ""  // the group shown beside that column: "c", "p", "l"
    property string sidePanel: ""  // the panel open beside the icon column: History, Brushes, … ("" none)
    // Type
    property string antiAlias: "Sharp"  // None, Sharp, Crisp, Strong, Smooth
    property int fontPreview: 2  // Type › Cỡ xem trước font: 0 Không … 4 Rất lớn
    property int statInfo: 0  // the status bar's box: Tài liệu, Hồ sơ màu, Kích thước, RAM và scratch, Thời gian thao tác, Công cụ đang dùng
    // Retouch: the panel's sections open, overlay chips (skin, eyes, lips, teeth), Feather px, output (0 Vùng chọn,
    // 1 Mask), Amount, Texture, Tách tần số's view (0 Cả hai, 1 Thấp, 2 Cao), Dodge & Burn's method, the eye and teeth sliders.
    property var retouch: ({})

    readonly property var toggles: ["rulers", "extras", "pixGrid", "grid", "guides", "snap", "lockGuides", "edges", "selEdges", "brushPrev", "showOpts", "showTools"]
    readonly property var dockOf: ({ Color: "c", Swatches: "c", Gradients: "c", Properties: "p", Adjustments: "p", Retouch: "p", Layers: "l", Channels: "l" })

    Component.onCompleted: reset()

    // As the prototype opens: "Main"; "Retouch" as Retouch.dc.html's INIT has it.
    function reset(artboard) {
        real = Qt.binding(() => isReal(Docs.current))
        tool = "brush"
        top = { move: "move", marquee: "rect", lasso: "lasso", wand: "wand", crop: "crop", eye: "eyedrop", heal: "spot", brush: "brush", stamp: "clone", eraser: "eraser",
            grad: "gradient", dodge: "dodge", type: "type", shape: "rectS", hand: "hand", zoom: "zoom" }
        xf = false
        warp = false
        values = {}
        fg = "#ffffff"
        fgHue = 0
        bg = "#000000"
        colorMode = "HSB"
        recentSwatches = Presets.RECENT_SW.slice()
        gradient = [0, 0]
        recentGradients = [[1, 3], [0, 1], [2, 0], [1, 1]]
        quickMask = false
        screen = 0
        rulers = true
        extras = true
        pixGrid = true
        grid = false
        guides = false
        snap = true
        lockGuides = false
        edges = false
        selEdges = true
        brushPrev = true
        snapTo = { guides: true, grid: true, layers: true, bounds: true }
        pointer = null
        showOpts = true
        showTools = true
        dockTab = { c: "Color", p: "Properties", l: "Layers" }
        dockMin = { c: false, p: false, l: false }
        dockCollapsed = false
        dockOpen = ""
        sidePanel = ""
        antiAlias = "Sharp"
        fontPreview = 2
        statInfo = 0
        retouch = { open: { fs: false, mask: true, smooth: true, db: false, eyes: false }, chips: { skin: true, eyes: false, lips: false, teeth: false }, overlay: true,
            feather: 4, out: 1, amount: 35, texture: 60, fsView: 0, dbm: 0, eyeW: 20, eyeB: 30, teeth: 25 }
        if (artboard === "Retouch") {
            tool = usable("spot") ? "spot" : "brush"
            rulers = false
            dockTab = { c: "Color", p: "Retouch", l: "Layers" }
            dockMin = { c: true, p: false, l: false }
        }
    }

    // A tool picked; with its toolbar group, that group shows it first from then on. One a real document locks: nothing.
    function setTool(id, group) {
        if (!usable(id))
            return
        tool = id
        if (group)
            top = Object.assign({}, top, { [group]: id })
        xf = false
        warp = false
    }
    function value(key, def) {
        const x = values[key]
        return x === undefined || x === null ? def : x
    }
    function setValue(key, v) { values = Object.assign({}, values, { [key]: v }) }
    function setValues(o) { values = Object.assign({}, values, o) }

    // The brush a tool paints with, as DocCanvas.brush takes it (BrushParams of core/stroke.h), from its bar
    // (ToolSpec.brushBar(): Brush's for a tool that paints nothing): size (document px), hardness, Opacity, Flow (0..1;
    // 1 where the bar has none), the pen's pressure on the size, Opacity, Flow (the bar's buttons), Brush Settings'
    // Spacing (a fraction of the size; its box, the dabs by the stroke's speed when off, is the core's to come), the
    // foreground colour. The rest of the options the core has none of: brushCells() says which cells these are.
    function brushOf(id) {
        const bar = ToolSpec.brushBar(id, false), b = ToolSpec.brush(bar), c = brushCells(bar)
        const cell = i => i < 0 ? undefined : value(bar + ":" + i, ToolSpec.def(bar, i))
        return {
            size: value(bar + ":size", b.size),
            hardness: value(bar + ":hard", b.hard) / 100,
            opacity: c.opacity < 0 ? 1 : cell(c.opacity) / 100,
            flow: c.flow < 0 ? 1 : cell(c.flow) / 100,
            spacing: value(bar + ":spacing", b.spacing) / 100,
            pressureSize: !!cell(c.pressureSize),
            pressureOpacity: !!cell(c.pressureOpacity),
            pressureFlow: !!cell(c.pressureFlow),
            color: fg
        }
    }
    // The cells of a painting bar brushOf() reads, by index in its BARS row (-1: none): the brush (BP, its size and
    // hardness), opacity (OPACITY's), flow (FLOW's), the pen's buttons beside them, the one for the size.
    function brushCells(bar) {
        const row = ToolSpec.BARS[bar] || [], at = (re, from) => from >= 0 && re.test(row[from + 1]) ? from + 1 : -1
        const op = ToolSpec.OPACITY[bar] ?? -1, fl = ToolSpec.FLOW[bar] ?? -1
        return { brush: row.findIndex(x => /^BP~/.test(x)), opacity: op, flow: fl, pressureOpacity: at(/^T~stylus~/, op), pressureFlow: at(/^T~stylus~/, fl),
            pressureSize: row.findIndex(x => /^T~line_weight~/.test(x)) }
    }

    // The foreground colour, "#rrggbb"; hue: the H to keep for it (else its own unless it is grey); addRecent: to the
    // swatches' recent row too.
    function setFg(hex, hue, addRecent) {
        const n = parseInt(hex.slice(1), 16), r = (n >> 16) & 255, g = (n >> 8) & 255, b = n & 255
        const mx = Math.max(r, g, b), d = mx - Math.min(r, g, b)
        if (hue !== undefined && hue !== null)
            fgHue = hue
        else if (mx && d / mx > 0.005)
            fgHue = ((mx === r ? (g - b) / d % 6 : mx === g ? (b - r) / d + 2 : (r - g) / d + 4) * 60 + 360) % 360
        fg = hex
        if (addRecent)
            addSwatch(hex)
    }
    function addSwatch(hex) { recentSwatches = [hex].concat(recentSwatches.filter((c) => c !== hex)).slice(0, 10) }  // into the recent row
    function swapColors() {  // X
        const f = fg
        fg = bg
        bg = f
    }
    function resetColors() {  // D
        fg = "#000000"
        bg = "#ffffff"
    }
    function pickGradient(g, i) {
        gradient = [g, i]
        recentGradients = [[g, i]].concat(recentGradients.filter((p) => !(p[0] === g && p[1] === i))).slice(0, 5)
    }
    function gradientCss() {  // the picked gradient's CSS
        const grp = Presets.GR_GROUPS[gradient[0]]
        return ((grp && grp[1][gradient[1]]) || Presets.GR_GROUPS[0][1][0])[1]
    }

    function toggle(key) {  // a View or Window switch, by its property's name
        if (toggles.indexOf(key) >= 0)
            tools[key] = !tools[key]
    }
    function toggleSnap(key) {  // View › Bắt dính vào: guides, grid, layers, bounds, all, none
        snapTo = key === "all" ? { guides: true, grid: true, layers: true, bounds: true } : key === "none" ? {} : Object.assign({}, snapTo, { [key]: !snapTo[key] })
    }
    // Panels by name (Window's): a dock tab, or the panel beside the icon column.
    function panelVisible(name) {
        const g = dockOf[name]
        if (g)
            return dockCollapsed ? dockOpen === g && dockTab[g] === name : dockTab[g] === name && !dockMin[g]
        return sidePanel === name
    }
    function showPanel(name) {  // a side panel already open closes, as the prototype's
        const g = dockOf[name]
        if (!g) {
            sidePanel = sidePanel === name ? "" : name
            return
        }
        dockTab = Object.assign({}, dockTab, { [g]: name })
        dockMin = Object.assign({}, dockMin, { [g]: false })
        if (dockCollapsed)
            dockOpen = g
    }
    function setRetouch(patch) { retouch = Object.assign({}, retouch, patch) }

    // The Crop tool's box on a w × h document, by the ratio picked in its bar (crop:2), centred.
    function cropBox(w, h) {
        const m = String(value("crop:2", "4:5 (8:10)")).match(/^(\d+):(\d+)/)
        if (!m)
            return { x: 0, y: 0, w: w, h: h }
        const r = +m[1] / +m[2], wide = w / h > r
        const cw = wide ? h * r : w, ch = wide ? h : w / r
        return { x: (w - cw) / 2, y: (h - ch) / 2, w: cw, h: ch }
    }
}
